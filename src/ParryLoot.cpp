#include <windows.h>

#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>

namespace {

// Keep equal to the VERSION file (tools/smoke_check.py checks it). Written to the first log
// line so that a bug report's ParryLoot.log says which build it came from.
constexpr char kVersion[] = "1.0.0";

// Verified offsets for Elden Ring App Ver. 1.17.2 (exe 2.7.2.0)
constexpr uintptr_t kWorldChrManMainPlayerOffset = 0x1E508;
constexpr uintptr_t kChrInsHandleOffset = 0x08;
constexpr uintptr_t kChrInsSpecialEffectOffset = 0x178;

constexpr uintptr_t kDamageModuleChrInsOffset = 0x08;
constexpr uintptr_t kAttackDataIsParriedOffset = 0x258;

constexpr uintptr_t kSpecialEffectHeadNodeOffset = 0x08;
constexpr uintptr_t kSpEffectNodeEffectIdOffset = 0x08;
constexpr uintptr_t kSpEffectNodeNextOffset = 0x30;
constexpr uintptr_t kSpEffectNodeRemainingOffset = 0x40;
constexpr uintptr_t kSpEffectNodeInitialOffset = 0x44;
constexpr uintptr_t kSpEffectNodeTotalDurationOffset = 0x48;
constexpr uintptr_t kSpEffectNodeFlagsOffset = 0x60;
// Flag bits the game itself tests before it counts an effect (CalcItemDropRateSum skips a node
// with any of them; eldenring.exe 2.7.2.0): 0x80000000 = no valid param (the game clears it when
// it sets the node's durations), 0x000C0000 = held but not applied for now (the game sets and
// clears these on a node that stays in the list, without touching its durations), two low bits.
constexpr uint32_t kSpEffectInactiveMask = 0x800C0003u;
// A node with only 0x000C0000 set still holds the effect, so it is still the one to time.
constexpr uint32_t kSpEffectGoneMask = 0x80000003u;

// Vanilla Silver-Pickled Fowl Foot SpEffect gives +50 Item Discovery (0.50f)
// and displays the Item Discovery buff icon cleanly in ERBuffTimer.
constexpr int32_t kDefaultSpEffectId = 3970;
constexpr float kVanillaSilverFootBonus = 50.0f;

struct Config {
    bool enabled = true;
    bool meleeParry = true;
    bool spellParry = true;
    bool logEnabled = true;
    int32_t spEffectId = kDefaultSpEffectId;
    float durationSeconds = 15.0f;
    float itemDiscoveryBonus = 25.0f;
};

using ResolveParryFn = float(__fastcall*)(void* chrDamageModule, void* attackerChrIns, void* attackData);
using SpellParryCase2Fn = uint8_t(__fastcall*)(void* bulletSubObj, void* eventPayload);
using CalcItemDropRateSumFn = float(__fastcall*)(void* csSpecialEffect);
using ApplySpEffectFn = void(__fastcall*)(void* chrIns, int32_t spEffectId, uint8_t unk0);

Config g_cfg{};
wchar_t g_modDir[MAX_PATH]{};
FILE* g_logFile = nullptr;
SRWLOCK g_logLock = SRWLOCK_INIT;
SRWLOCK g_stateLock = SRWLOCK_INIT;

HANDLE g_stopEvent = nullptr;
HANDLE g_initThread = nullptr;

uintptr_t** g_pWorldChrMan = nullptr;
ApplySpEffectFn g_applySpEffect = nullptr;
ResolveParryFn g_origResolveParry = nullptr;
SpellParryCase2Fn g_origSpellParry = nullptr;
CalcItemDropRateSumFn g_origCalcDropRate = nullptr;

// Trampoline / call-relay page and patch bookkeeping for clean unhook.
uint8_t* g_trampolinePage = nullptr;
uintptr_t g_hookAddrResolveParry = 0;
uint8_t g_origBytesResolveParry[16]{};
bool g_patchedResolveParry = false;

uintptr_t g_hookAddrSpellParry = 0;
uint8_t g_origBytesSpellParry[15]{};
bool g_patchedSpellParry = false;

uintptr_t g_patchAddrDropRateCall = 0;
int32_t g_origRel32DropRateCall = 0;
bool g_patchedDropRateCall = false;

// State tracking so we neither shorten a real 180s Silver-Pickled Fowl Foot
// nor modify its +50 Item Discovery value.
uintptr_t g_managedNodeAddr = 0;
ULONGLONG g_lastParryTriggerMs = 0;

bool IsPlausiblePtr(uintptr_t ptr) {
    return ptr >= 0x10000ULL && ptr < 0x00007FFFFFFFFFFFULL;
}

void LogLine(const char* fmt, ...) {
    if (!g_cfg.logEnabled) {
        return;
    }
    AcquireSRWLockExclusive(&g_logLock);
    if (!g_logFile && g_modDir[0] != L'\0') {
        wchar_t logPath[MAX_PATH]{};
        std::swprintf(logPath, MAX_PATH, L"%ls\\ParryLoot.log", g_modDir);
        g_logFile = _wfopen(logPath, L"w");
    }
    if (g_logFile) {
        SYSTEMTIME st{};
        GetLocalTime(&st);
        std::fprintf(g_logFile, "[%02u:%02u:%02u.%03u] ",
                     st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
        va_list args;
        va_start(args, fmt);
        std::vfprintf(g_logFile, fmt, args);
        va_end(args);
        std::fputc('\n', g_logFile);
        std::fflush(g_logFile);
    }
    ReleaseSRWLockExclusive(&g_logLock);
}

void TrimInPlace(char* s) {
    if (!s) {
        return;
    }
    char* start = s;
    while (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n') {
        ++start;
    }
    if (start != s) {
        std::memmove(s, start, std::strlen(start) + 1);
    }
    size_t len = std::strlen(s);
    while (len > 0) {
        char c = s[len - 1];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            s[--len] = '\0';
        } else {
            break;
        }
    }
}

bool ParseBoolValue(const char* val, bool fallback) {
    if (!val || !*val) {
        return fallback;
    }
    if (_stricmp(val, "1") == 0 || _stricmp(val, "true") == 0 ||
        _stricmp(val, "yes") == 0 || _stricmp(val, "on") == 0) {
        return true;
    }
    if (_stricmp(val, "0") == 0 || _stricmp(val, "false") == 0 ||
        _stricmp(val, "no") == 0 || _stricmp(val, "off") == 0) {
        return false;
    }
    return fallback;
}

void LoadConfig() {
    wchar_t iniPath[MAX_PATH]{};
    std::swprintf(iniPath, MAX_PATH, L"%ls\\ParryLoot.ini", g_modDir);
    FILE* f = _wfopen(iniPath, L"r");
    if (!f) {
        return;
    }

    char line[512]{};
    while (std::fgets(line, sizeof(line), f)) {
        // Strip comments starting with ';' or '#'
        for (size_t i = 0; line[i] != '\0'; ++i) {
            if (line[i] == ';' || line[i] == '#') {
                line[i] = '\0';
                break;
            }
        }
        TrimInPlace(line);
        if (line[0] == '\0' || line[0] == '[') {
            continue;
        }
        char* eq = std::strchr(line, '=');
        if (!eq) {
            continue;
        }
        *eq = '\0';
        char* key = line;
        char* val = eq + 1;
        TrimInPlace(key);
        TrimInPlace(val);

        if (_stricmp(key, "enabled") == 0) {
            g_cfg.enabled = ParseBoolValue(val, g_cfg.enabled);
        } else if (_stricmp(key, "melee_parry") == 0) {
            g_cfg.meleeParry = ParseBoolValue(val, g_cfg.meleeParry);
        } else if (_stricmp(key, "spell_parry") == 0) {
            g_cfg.spellParry = ParseBoolValue(val, g_cfg.spellParry);
        } else if (_stricmp(key, "log") == 0) {
            g_cfg.logEnabled = ParseBoolValue(val, g_cfg.logEnabled);
        } else if (_stricmp(key, "sp_effect_id") == 0) {
            char* end = nullptr;
            long parsed = std::strtol(val, &end, 10);
            if (end != val && parsed > 0) {
                g_cfg.spEffectId = static_cast<int32_t>(parsed);
            }
        } else if (_stricmp(key, "duration") == 0) {
            char* end = nullptr;
            float parsed = std::strtof(val, &end);
            if (end != val && std::isfinite(parsed)) {
                // Below the 180 s of a real Silver-Pickled Fowl Foot: a node whose total duration
                // is longer than this one is taken for a real foot and left alone.
                if (parsed < 0.5f) parsed = 0.5f;
                if (parsed > 170.0f) parsed = 170.0f;
                g_cfg.durationSeconds = parsed;
            }
        } else if (_stricmp(key, "item_discovery_bonus") == 0) {
            char* end = nullptr;
            float parsed = std::strtof(val, &end);
            if (end != val && std::isfinite(parsed)) {
                if (parsed < 0.0f) parsed = 0.0f;
                if (parsed > 999.0f) parsed = 999.0f;
                g_cfg.itemDiscoveryBonus = parsed;
            }
        }
    }
    std::fclose(f);
}

bool GetTextSection(HMODULE mod, const uint8_t*& start, size_t& size) {
    if (!mod) {
        return false;
    }
    const auto* base = reinterpret_cast<const uint8_t*>(mod);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
        return false;
    }
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) {
        return false;
    }
    const IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
        if (std::memcmp(sec[i].Name, ".text", 5) == 0 && sec[i].Name[5] == '\0') {
            start = base + sec[i].VirtualAddress;
            size = sec[i].Misc.VirtualSize;
            return size > 0;
        }
    }
    return false;
}

uintptr_t ScanPatternUnique(const uint8_t* start, size_t size, const char* pattern, const char* label) {
    uint8_t bytes[128]{};
    bool mask[128]{};
    size_t len = 0;

    const char* p = pattern;
    while (*p && len < sizeof(bytes)) {
        while (*p == ' ' || *p == '\t') {
            ++p;
        }
        if (!*p) {
            break;
        }
        if (p[0] == '?' && p[1] == '?') {
            bytes[len] = 0;
            mask[len] = false;
            ++len;
            p += 2;
        } else {
            char hex[3] = {p[0], p[1], '\0'};
            bytes[len] = static_cast<uint8_t>(std::strtoul(hex, nullptr, 16));
            mask[len] = true;
            ++len;
            p += 2;
        }
    }
    if (len == 0 || size < len) {
        return 0;
    }

    uintptr_t firstMatch = 0;
    size_t matchCount = 0;
    const size_t limit = size - len;
    for (size_t i = 0; i <= limit; ++i) {
        bool ok = true;
        for (size_t j = 0; j < len; ++j) {
            if (mask[j] && start[i + j] != bytes[j]) {
                ok = false;
                break;
            }
        }
        if (ok) {
            ++matchCount;
            if (matchCount == 1) {
                firstMatch = reinterpret_cast<uintptr_t>(start + i);
            } else {
                LogLine("ERROR: AOB '%s' matched multiple times (ambiguous).", label);
                return 0;
            }
        }
    }
    if (matchCount == 0) {
        LogLine("ERROR: AOB '%s' not found.", label);
        return 0;
    }
    return firstMatch;
}

uint8_t* AllocExecutableNear(uintptr_t anchor, size_t size) {
    SYSTEM_INFO si{};
    GetSystemInfo(&si);
    const uintptr_t gran = si.dwAllocationGranularity ? si.dwAllocationGranularity : 0x10000;
    const uintptr_t maxDist = 0x7FFF0000ULL;
    const uintptr_t minAddr = (anchor > maxDist) ? (anchor - maxDist) : reinterpret_cast<uintptr_t>(si.lpMinimumApplicationAddress);
    const uintptr_t maxAddr = anchor + maxDist;

    // Search forward from anchor first, then backward, for an unreserved region within +-2GB.
    for (int dir = 0; dir < 2; ++dir) {
        uintptr_t cur = (anchor + gran - 1) & ~(gran - 1);
        while ((dir == 0 && cur < maxAddr) || (dir == 1 && cur > minAddr)) {
            MEMORY_BASIC_INFORMATION mbi{};
            if (VirtualQuery(reinterpret_cast<LPCVOID>(cur), &mbi, sizeof(mbi)) == 0) {
                break;
            }
            if (mbi.State == MEM_FREE && mbi.RegionSize >= size) {
                uintptr_t cand = (dir == 0)
                    ? reinterpret_cast<uintptr_t>(mbi.BaseAddress)
                    : ((reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize - size) & ~(gran - 1));
                if (cand >= reinterpret_cast<uintptr_t>(mbi.BaseAddress) &&
                    cand + size <= reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize &&
                    cand >= minAddr && cand <= maxAddr) {
                    void* mem = VirtualAlloc(reinterpret_cast<LPVOID>(cand), size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
                    if (mem) {
                        return reinterpret_cast<uint8_t*>(mem);
                    }
                }
            }
            if (dir == 0) {
                uintptr_t next = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
                if (next <= cur) break;
                cur = (next + gran - 1) & ~(gran - 1);
            } else {
                uintptr_t base = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
                if (base <= gran || base - gran >= cur) break;
                cur = (base - gran) & ~(gran - 1);
            }
        }
    }
    return nullptr;
}

void WriteAbsJump14(uint8_t* dst, uintptr_t target) {
    dst[0] = 0xFF;
    dst[1] = 0x25;
    dst[2] = 0x00;
    dst[3] = 0x00;
    dst[4] = 0x00;
    dst[5] = 0x00;
    std::memcpy(dst + 6, &target, sizeof(target));
}

bool InstallPrologueHook(
    uintptr_t targetAddr,
    size_t stolenBytesLen,
    void* hookFn,
    uint8_t* trampolineSlot,
    uint8_t* savedOrigBytes,
    void** outOriginalFn) {
    if (stolenBytesLen < 14 || stolenBytesLen > 32) {
        return false;
    }
    auto* target = reinterpret_cast<uint8_t*>(targetAddr);
    std::memcpy(savedOrigBytes, target, stolenBytesLen);

    // Copy stolen prologue instructions verbatim (caller guarantees zero RIP-relative ops inside slice),
    // followed by an absolute jump back to target + stolenBytesLen.
    std::memcpy(trampolineSlot, savedOrigBytes, stolenBytesLen);
    WriteAbsJump14(trampolineSlot + stolenBytesLen, targetAddr + stolenBytesLen);
    FlushInstructionCache(GetCurrentProcess(), trampolineSlot, stolenBytesLen + 14);

    // Publish the way back to the original before the jump into the hook goes live: a game
    // thread can enter the hook as soon as the patch below is written, and the hook calls it.
    *outOriginalFn = trampolineSlot;

    DWORD oldProt = 0;
    if (!VirtualProtect(target, stolenBytesLen, PAGE_EXECUTE_READWRITE, &oldProt)) {
        return false;
    }
    WriteAbsJump14(target, reinterpret_cast<uintptr_t>(hookFn));
    for (size_t i = 14; i < stolenBytesLen; ++i) {
        target[i] = 0x90;
    }
    DWORD dummy = 0;
    VirtualProtect(target, stolenBytesLen, oldProt, &dummy);
    FlushInstructionCache(GetCurrentProcess(), target, stolenBytesLen);
    return true;
}

bool RestoreBytes(uintptr_t addr, const uint8_t* origBytes, size_t len) {
    if (!addr || !origBytes || len == 0) {
        return false;
    }
    auto* p = reinterpret_cast<uint8_t*>(addr);
    DWORD oldProt = 0;
    if (!VirtualProtect(p, len, PAGE_EXECUTE_READWRITE, &oldProt)) {
        return false;
    }
    std::memcpy(p, origBytes, len);
    DWORD dummy = 0;
    VirtualProtect(p, len, oldProt, &dummy);
    FlushInstructionCache(GetCurrentProcess(), p, len);
    return true;
}

uintptr_t GetMainPlayer() {
    if (!g_pWorldChrMan) {
        return 0;
    }
    uintptr_t wcm = reinterpret_cast<uintptr_t>(*g_pWorldChrMan);
    if (!IsPlausiblePtr(wcm)) {
        return 0;
    }
    uintptr_t player = *reinterpret_cast<uintptr_t*>(wcm + kWorldChrManMainPlayerOffset);
    return IsPlausiblePtr(player) ? player : 0;
}

uintptr_t GetSpecialEffectModule(uintptr_t chrIns) {
    if (!IsPlausiblePtr(chrIns)) {
        return 0;
    }
    uintptr_t spMod = *reinterpret_cast<uintptr_t*>(chrIns + kChrInsSpecialEffectOffset);
    return IsPlausiblePtr(spMod) ? spMod : 0;
}

// Counted by the game right now (what CalcItemDropRateSum adds up).
bool IsActiveEffectNode(uintptr_t node) {
    if (!IsPlausiblePtr(node)) {
        return false;
    }
    const uint32_t flags = *reinterpret_cast<uint32_t*>(node + kSpEffectNodeFlagsOffset);
    return (flags & kSpEffectInactiveMask) == 0;
}

// Holds the effect, even if the game does not apply it at this moment (0x000C0000).
bool IsPresentEffectNode(uintptr_t node) {
    if (!IsPlausiblePtr(node)) {
        return false;
    }
    const uint32_t flags = *reinterpret_cast<uint32_t*>(node + kSpEffectNodeFlagsOffset);
    return (flags & kSpEffectGoneMask) == 0;
}

struct EffectNodeInfo {
    uintptr_t node = 0;
    float remaining = 0.0f;
    float total = 0.0f;
    uint32_t flags = 0;
};

bool HasLongerExternalSpEffect(uintptr_t spMod, int32_t effectId, float ourDuration, uintptr_t managedNode,
                               EffectNodeInfo& found) {
    if (!IsPlausiblePtr(spMod)) {
        return false;
    }
    uintptr_t node = *reinterpret_cast<uintptr_t*>(spMod + kSpecialEffectHeadNodeOffset);
    for (int i = 0; i < 4096 && IsPlausiblePtr(node); ++i) {
        if (IsPresentEffectNode(node)) {
            int32_t id = *reinterpret_cast<int32_t*>(node + kSpEffectNodeEffectIdOffset);
            if (id == effectId) {
                const float rem = *reinterpret_cast<float*>(node + kSpEffectNodeRemainingOffset);
                const float total = *reinterpret_cast<float*>(node + kSpEffectNodeTotalDurationOffset);
                const bool longer = (!std::isnan(rem) && rem > ourDuration + 0.05f) ||
                                    (node != managedNode && !std::isnan(total) && total > ourDuration + 0.05f &&
                                     rem > 0.0f);
                if (longer) {
                    found.node = node;
                    found.remaining = rem;
                    found.total = total;
                    found.flags = *reinterpret_cast<uint32_t*>(node + kSpEffectNodeFlagsOffset);
                    return true;
                }
            }
        }
        node = *reinterpret_cast<uintptr_t*>(node + kSpEffectNodeNextOffset);
    }
    return false;
}

void OnParrySucceeded(const char* source, uintptr_t mainPlayer) {
    if (!g_cfg.enabled || !g_applySpEffect || !IsPlausiblePtr(mainPlayer)) {
        return;
    }

    const ULONGLONG nowMs = GetTickCount64();
    AcquireSRWLockExclusive(&g_stateLock);
    // Debounce multi-hitbox / duplicate per-hit resolution within the same 60ms window.
    if (nowMs - g_lastParryTriggerMs < 60ULL) {
        ReleaseSRWLockExclusive(&g_stateLock);
        return;
    }
    g_lastParryTriggerMs = nowMs;
    const uintptr_t prevManaged = g_managedNodeAddr;
    ReleaseSRWLockExclusive(&g_stateLock);

    uintptr_t spMod = GetSpecialEffectModule(mainPlayer);
    if (!spMod) {
        return;
    }

    // Never shorten or overwrite a stronger/longer active Silver-Pickled Fowl Foot (180s).
    EffectNodeInfo longer{};
    if (HasLongerExternalSpEffect(spMod, g_cfg.spEffectId, g_cfg.durationSeconds, prevManaged, longer)) {
        LogLine("Parry (%s): SpEffect %d already runs longer (node 0x%llX%s, %.1fs left of %.1fs, flags 0x%08X); left as it is.",
                source, g_cfg.spEffectId, static_cast<unsigned long long>(longer.node),
                longer.node == prevManaged ? ", timed by this mod" : "", longer.remaining, longer.total,
                longer.flags);
        return;
    }

    g_applySpEffect(reinterpret_cast<void*>(mainPlayer), g_cfg.spEffectId, 0);

    // Re-walk the SpecialEffect linked list and stamp the duration, so that the buff lasts
    // durationSeconds instead of the effect's own (180 s for 3970) and ERBuffTimer counts it
    // down. A node with only 0x000C0000 set right after the apply still counts (see
    // kSpEffectGoneMask).
    uintptr_t updatedNode = 0;
    uint32_t updatedFlags = 0;
    int stampedCount = 0;
    uintptr_t node = *reinterpret_cast<uintptr_t*>(spMod + kSpecialEffectHeadNodeOffset);
    for (int i = 0; i < 4096 && IsPlausiblePtr(node); ++i) {
        if (IsPresentEffectNode(node)) {
            int32_t id = *reinterpret_cast<int32_t*>(node + kSpEffectNodeEffectIdOffset);
            if (id == g_cfg.spEffectId) {
                *reinterpret_cast<float*>(node + kSpEffectNodeRemainingOffset) = g_cfg.durationSeconds;
                *reinterpret_cast<float*>(node + kSpEffectNodeInitialOffset) = g_cfg.durationSeconds;
                *reinterpret_cast<float*>(node + kSpEffectNodeTotalDurationOffset) = g_cfg.durationSeconds;
                updatedNode = node;
                updatedFlags = *reinterpret_cast<uint32_t*>(node + kSpEffectNodeFlagsOffset);
                ++stampedCount;
            }
        }
        node = *reinterpret_cast<uintptr_t*>(node + kSpEffectNodeNextOffset);
    }

    AcquireSRWLockExclusive(&g_stateLock);
    g_managedNodeAddr = updatedNode;
    ReleaseSRWLockExclusive(&g_stateLock);

    if (updatedNode == 0) {
        LogLine("WARNING: Parry (%s): applied SpEffect %d, but its node was not in the effect list yet, so its "
                "duration could not be set: the effect's own duration and Item Discovery apply this time.",
                source, g_cfg.spEffectId);
        return;
    }
    LogLine("Parry (%s) -> applied SpEffect %d for %.1fs (+%.0f Item Discovery) [node 0x%llX, flags 0x%08X%s].",
            source, g_cfg.spEffectId, g_cfg.durationSeconds, g_cfg.itemDiscoveryBonus,
            static_cast<unsigned long long>(updatedNode), updatedFlags,
            stampedCount > 1 ? ", more than one node" : "");
}

float __fastcall Hook_ResolveParryStamina(void* chrDamageModule, void* attackerChrIns, void* attackData) {
    const uintptr_t dmgMod = reinterpret_cast<uintptr_t>(chrDamageModule);
    const uintptr_t atk = reinterpret_cast<uintptr_t>(attackData);
    const uintptr_t mainPlayer = GetMainPlayer();
    const uintptr_t defender = IsPlausiblePtr(dmgMod)
        ? *reinterpret_cast<uintptr_t*>(dmgMod + kDamageModuleChrInsOffset)
        : 0;

    uint8_t isParriedPre = IsPlausiblePtr(atk)
        ? *reinterpret_cast<uint8_t*>(atk + kAttackDataIsParriedOffset)
        : 0;

    float result = g_origResolveParry(chrDamageModule, attackerChrIns, attackData);

    if (g_cfg.enabled && g_cfg.meleeParry && mainPlayer != 0 && defender == mainPlayer) {
        const uintptr_t attacker = reinterpret_cast<uintptr_t>(attackerChrIns);
        if (attacker != 0 && attacker != mainPlayer && isParriedPre != 0 && IsPlausiblePtr(atk)) {
            const uint8_t isParriedPost = *reinterpret_cast<uint8_t*>(atk + kAttackDataIsParriedOffset);
            if (isParriedPost != 0) {
                OnParrySucceeded("melee", mainPlayer);
            }
        }
    }
    return result;
}

uint8_t __fastcall Hook_SpellParryCase2(void* bulletSubObj, void* eventPayload) {
    uint8_t ret = g_origSpellParry(bulletSubObj, eventPayload);

    if (ret != 0 && g_cfg.enabled && g_cfg.spellParry) {
        const uintptr_t mainPlayer = GetMainPlayer();
        const uintptr_t payload = reinterpret_cast<uintptr_t>(eventPayload);
        if (mainPlayer != 0 && IsPlausiblePtr(payload)) {
            const uint64_t playerHandle = *reinterpret_cast<uint64_t*>(mainPlayer + kChrInsHandleOffset);
            const uint64_t eventHandle = *reinterpret_cast<uint64_t*>(payload + 0x08);
            if (eventHandle == playerHandle) {
                OnParrySucceeded("spell", mainPlayer);
            }
        }
    }
    return ret;
}

float __fastcall Hook_CalcItemDropRateSum(void* csSpecialEffect) {
    float sum = g_origCalcDropRate(csSpecialEffect);

    if (!g_cfg.enabled) {
        return sum;
    }

    const float delta = (g_cfg.itemDiscoveryBonus - kVanillaSilverFootBonus) / 100.0f;
    if (std::fabs(delta) < 0.0001f) {
        return sum;
    }

    const uintptr_t mainPlayer = GetMainPlayer();
    const uintptr_t playerSpMod = GetSpecialEffectModule(mainPlayer);
    if (!playerSpMod || reinterpret_cast<uintptr_t>(csSpecialEffect) != playerSpMod) {
        return sum;
    }

    AcquireSRWLockShared(&g_stateLock);
    const uintptr_t managed = g_managedNodeAddr;
    ReleaseSRWLockShared(&g_stateLock);

    bool ourBuffActive = false;
    uintptr_t node = *reinterpret_cast<uintptr_t*>(playerSpMod + kSpecialEffectHeadNodeOffset);
    for (int i = 0; i < 4096 && IsPlausiblePtr(node); ++i) {
        if (node == managed && IsActiveEffectNode(node)) {
            int32_t id = *reinterpret_cast<int32_t*>(node + kSpEffectNodeEffectIdOffset);
            float rem = *reinterpret_cast<float*>(node + kSpEffectNodeRemainingOffset);
            float total = *reinterpret_cast<float*>(node + kSpEffectNodeTotalDurationOffset);
            // If the player consumed a real 180s Silver-Pickled Fowl Foot on top of our buff,
            // its duration becomes 180s (> durationSeconds), so restore vanilla +50 bonus.
            if (id == g_cfg.spEffectId && rem > 0.0f && total <= g_cfg.durationSeconds + 0.05f) {
                ourBuffActive = true;
            }
            break;
        }
        node = *reinterpret_cast<uintptr_t*>(node + kSpEffectNodeNextOffset);
    }

    if (ourBuffActive) {
        sum += delta;
        if (sum < 0.0f) {
            sum = 0.0f;
        }
    }
    return sum;
}

DWORD WINAPI InitThread(LPVOID) {
    LogLine("ParryLoot %s initializing...", kVersion);
    LogLine("Config: enabled=%d melee_parry=%d spell_parry=%d duration=%.1fs bonus=+%.0f sp_effect_id=%d",
            g_cfg.enabled ? 1 : 0,
            g_cfg.meleeParry ? 1 : 0,
            g_cfg.spellParry ? 1 : 0,
            g_cfg.durationSeconds,
            g_cfg.itemDiscoveryBonus,
            g_cfg.spEffectId);

    if (!g_cfg.enabled) {
        LogLine("ParryLoot disabled via ParryLoot.ini.");
        return 0;
    }

    const HMODULE gameMod = GetModuleHandleW(nullptr);
    const uint8_t* textStart = nullptr;
    size_t textSize = 0;
    if (!GetTextSection(gameMod, textStart, textSize)) {
        LogLine("ERROR: Could not locate eldenring.exe .text section.");
        return 1;
    }

    const uintptr_t matchWcm = ScanPatternUnique(
        textStart, textSize,
        "48 8b 0d ?? ?? ?? ?? c7 44 24 20 02 00 00 00 c7 44 24 24 01 00 00 00",
        "WorldChrMan");
    const uintptr_t matchApply = ScanPatternUnique(
        textStart, textSize,
        "48 8b c4 48 89 58 08 48 89 70 10 57 48 81 ec c0 00 00 00 0f 28 05 ?? ?? ?? ?? 48 8b f1",
        "chr_ins_apply_speffect");
    const uintptr_t matchResolveParry = ScanPatternUnique(
        textStart, textSize,
        "48 89 5c 24 20 41 56 48 83 ec 40 48 89 6c 24 50 49 8b d8 48 89 74 24 58 48 8b ea",
        "fn_ResolveParryStamina");
    const uintptr_t matchSpellParry = ScanPatternUnique(
        textStart, textSize,
        "48 89 5c 24 08 48 89 74 24 10 57 48 83 ec 20 48 8b d9 48 8b fa 48 8b 0d ?? ?? ?? ?? 48 85 c9 75 2e",
        "fn_SpellParryCase2");
    const uintptr_t matchDropCallSite = ScanPatternUnique(
        textStart, textSize,
        "ba 42 00 00 00 48 8b cb e8 ?? ?? ?? ?? 84 c0 74 0b 48 8b cb e8 ?? ?? ?? ?? 0f 28 f8",
        "GetItemDiscovery_CalcDropRateCall");

    if (!matchWcm || !matchApply || !matchResolveParry || !matchSpellParry || !matchDropCallSite) {
        LogLine("Aborting hook install due to unresolved AOB(s).");
        return 1;
    }

    const int32_t wcmRel32 = *reinterpret_cast<const int32_t*>(matchWcm + 3);
    g_pWorldChrMan = reinterpret_cast<uintptr_t**>(matchWcm + 7 + wcmRel32);
    g_applySpEffect = reinterpret_cast<ApplySpEffectFn>(matchApply);

    const uintptr_t callSite = matchDropCallSite + 20;
    const int32_t dropRel32 = *reinterpret_cast<const int32_t*>(callSite + 1);
    const uintptr_t calcDropTarget = (callSite + 5) + dropRel32;
    g_origCalcDropRate = reinterpret_cast<CalcItemDropRateSumFn>(calcDropTarget);

    LogLine("Resolved WorldChrMan ptr=0x%llX ApplySpEffect=0x%llX ResolveParry=0x%llX SpellParry=0x%llX CalcDropRate=0x%llX",
            static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(g_pWorldChrMan)),
            static_cast<unsigned long long>(matchApply),
            static_cast<unsigned long long>(matchResolveParry),
            static_cast<unsigned long long>(matchSpellParry),
            static_cast<unsigned long long>(calcDropTarget));

    // Wait briefly until the game finishes initializing WorldChrMan before touching .text.
    for (int i = 0; i < 300; ++i) {
        if (WaitForSingleObject(g_stopEvent, 100) == WAIT_OBJECT_0) {
            return 0;
        }
        if (g_pWorldChrMan && IsPlausiblePtr(reinterpret_cast<uintptr_t>(*g_pWorldChrMan))) {
            break;
        }
    }
    if (WaitForSingleObject(g_stopEvent, 0) == WAIT_OBJECT_0) {
        return 0;
    }

    g_trampolinePage = AllocExecutableNear(reinterpret_cast<uintptr_t>(textStart), 4096);
    if (!g_trampolinePage) {
        LogLine("ERROR: Could not allocate executable trampoline page within +-2GB of .text.");
        return 1;
    }

    g_hookAddrResolveParry = matchResolveParry;
    if (!InstallPrologueHook(
            g_hookAddrResolveParry, 16,
            reinterpret_cast<void*>(&Hook_ResolveParryStamina),
            g_trampolinePage + 0,
            g_origBytesResolveParry,
            reinterpret_cast<void**>(&g_origResolveParry))) {
        LogLine("ERROR: Failed to install melee parry hook.");
        return 1;
    }
    g_patchedResolveParry = true;

    g_hookAddrSpellParry = matchSpellParry;
    if (!InstallPrologueHook(
            g_hookAddrSpellParry, 15,
            reinterpret_cast<void*>(&Hook_SpellParryCase2),
            g_trampolinePage + 64,
            g_origBytesSpellParry,
            reinterpret_cast<void**>(&g_origSpellParry))) {
        LogLine("ERROR: Failed to install spell parry hook.");
        return 1;
    }
    g_patchedSpellParry = true;

    // Install call-site hook on GetItemDiscovery -> CalcItemDropRateSum if custom bonus != +50.
    if (std::fabs(g_cfg.itemDiscoveryBonus - kVanillaSilverFootBonus) >= 0.0001f) {
        uint8_t* relay = g_trampolinePage + 128;
        WriteAbsJump14(relay, reinterpret_cast<uintptr_t>(&Hook_CalcItemDropRateSum));
        FlushInstructionCache(GetCurrentProcess(), relay, 14);

        const intptr_t diff = reinterpret_cast<intptr_t>(relay) - static_cast<intptr_t>(callSite + 5);
        if (diff < INT32_MIN || diff > INT32_MAX) {
            LogLine("ERROR: Relay stub outside rel32 range; item_discovery_bonus override disabled (using +50 vanilla).");
        } else {
            g_patchAddrDropRateCall = callSite + 1;
            g_origRel32DropRateCall = dropRel32;
            const int32_t newRel32 = static_cast<int32_t>(diff);
            DWORD oldProt = 0;
            if (VirtualProtect(reinterpret_cast<LPVOID>(g_patchAddrDropRateCall), 4, PAGE_EXECUTE_READWRITE, &oldProt)) {
                std::memcpy(reinterpret_cast<void*>(g_patchAddrDropRateCall), &newRel32, 4);
                DWORD dummy = 0;
                VirtualProtect(reinterpret_cast<LPVOID>(g_patchAddrDropRateCall), 4, oldProt, &dummy);
                FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<LPCVOID>(g_patchAddrDropRateCall), 4);
                g_patchedDropRateCall = true;
            } else {
                LogLine("ERROR: VirtualProtect failed on CalcDropRate call site.");
            }
        }
    }

    LogLine("All hooks installed successfully. Ready for parries!");
    return 0;
}

void Cleanup(bool processTerminating) {
    if (g_stopEvent) {
        SetEvent(g_stopEvent);
    }
    if (!processTerminating && g_initThread) {
        WaitForSingleObject(g_initThread, 2000);
        if (g_patchedDropRateCall) {
            RestoreBytes(g_patchAddrDropRateCall, reinterpret_cast<const uint8_t*>(&g_origRel32DropRateCall), 4);
            g_patchedDropRateCall = false;
        }
        if (g_patchedSpellParry) {
            RestoreBytes(g_hookAddrSpellParry, g_origBytesSpellParry, sizeof(g_origBytesSpellParry));
            g_patchedSpellParry = false;
        }
        if (g_patchedResolveParry) {
            RestoreBytes(g_hookAddrResolveParry, g_origBytesResolveParry, sizeof(g_origBytesResolveParry));
            g_patchedResolveParry = false;
        }
        if (g_trampolinePage) {
            VirtualFree(g_trampolinePage, 0, MEM_RELEASE);
            g_trampolinePage = nullptr;
        }
    }
    if (g_initThread) {
        CloseHandle(g_initThread);
        g_initThread = nullptr;
    }
    if (g_stopEvent) {
        CloseHandle(g_stopEvent);
        g_stopEvent = nullptr;
    }
    AcquireSRWLockExclusive(&g_logLock);
    if (g_logFile) {
        std::fclose(g_logFile);
        g_logFile = nullptr;
    }
    ReleaseSRWLockExclusive(&g_logLock);
}

}  // namespace

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ulReason, LPVOID lpReserved) {
    if (ulReason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);

        GetModuleFileNameW(hModule, g_modDir, MAX_PATH);
        wchar_t* slash = std::wcsrchr(g_modDir, L'\\');
        if (slash) {
            *slash = L'\0';
        }

        LoadConfig();
        g_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        g_initThread = CreateThread(nullptr, 0, InitThread, nullptr, 0, nullptr);
    } else if (ulReason == DLL_PROCESS_DETACH) {
        Cleanup(lpReserved != nullptr);
    }
    return TRUE;
}
