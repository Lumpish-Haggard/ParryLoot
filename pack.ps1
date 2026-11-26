<#
.SYNOPSIS
    Builds and packages ParryLoot-<version>.zip for GitHub Releases and Nexus Mods.

.DESCRIPTION
    1. Reads VERSION cleanly and validates assets\mods\ParryLoot.ini and LICENSE.
    2. With -Build (or if build\ParryLoot.dll does not exist), compiles src\ParryLoot.cpp
       with x86_64-w64-mingw32-g++ / g++ or cl.exe into build\ParryLoot.dll. Without -Build
       it stops if src\ParryLoot.cpp is newer than build\ParryLoot.dll.
    3. Stages ONLY runtime mod files (mods\ParryLoot.dll, mods\ParryLoot.ini,
       mods\ParryLoot\ParryLoot-README.txt, mods\ParryLoot\LICENSE.txt) into build\stage.
    4. Creates dist\ParryLoot-<version>.zip and dist\info.json.
    5. Runs tools\smoke_check.py if Python is available.
    6. With -Install, copies the staged mods\ folder into -GamePath (or ELDEN_RING_GAME): your
       ModEngine 2 folder, the one with modengine2_launcher.exe (often the Elden Ring Game folder).
       Not for a ModEngine 2 that Vortex manages: whenever Vortex saves its load order, it rewrites
       config_eldenring.toml with only the DLL mods it installed, so install the zip through Vortex.

.EXAMPLE
    .\pack.ps1
    .\pack.ps1 -Build
    .\pack.ps1 -Build -Install -GamePath "D:\Games\ELDEN RING\Game"
#>
[CmdletBinding()]
param(
    [switch]$Build,
    [switch]$Install,
    [string]$GamePath = "",
    [string]$Compiler = "",
    [switch]$SkipChecks
)

$ErrorActionPreference = "Stop"
$Root = $PSScriptRoot
$Version = (Get-Content -Raw -Encoding UTF8 (Join-Path $Root "VERSION")).Trim()
if ($Version -notmatch '^\d+\.\d+\.\d+$') {
    throw "VERSION must be MAJOR.MINOR.PATCH, got '$Version'"
}

$SrcFile = Join-Path $Root "src\ParryLoot.cpp"
$BuildDir = Join-Path $Root "build"
$DllOut = Join-Path $BuildDir "ParryLoot.dll"
$StageDir = Join-Path $BuildDir "stage"
$DistDir = Join-Path $Root "dist"
$ZipPath = Join-Path $DistDir "ParryLoot-$Version.zip"
$InfoPath = Join-Path $DistDir "info.json"

New-Item -ItemType Directory -Force $BuildDir | Out-Null
New-Item -ItemType Directory -Force $DistDir | Out-Null

function Find-Compiler {
    if ($Compiler) { return $Compiler }
    foreach ($name in @("x86_64-w64-mingw32-g++", "g++", "cl")) {
        $cmd = Get-Command $name -ErrorAction SilentlyContinue
        if ($cmd) { return $cmd.Source }
    }
    return $null
}

if ($Build -or -not (Test-Path $DllOut)) {
    $cc = Find-Compiler
    if (-not $cc) {
        throw "No C++ compiler found (need x86_64-w64-mingw32-g++, g++, or cl), and build\ParryLoot.dll does not exist."
    }
    Write-Host "Compiling $SrcFile using $cc..." -ForegroundColor Cyan
    $TmpDir = Join-Path $BuildDir "tmp"
    New-Item -ItemType Directory -Force $TmpDir | Out-Null
    $oldTemp = $env:TEMP; $oldTmp = $env:TMP
    $env:TEMP = $TmpDir; $env:TMP = $TmpDir
    try {
        if ($cc -match '(^|\\)cl(\.exe)?$') {
            # _CRT_SECURE_NO_WARNINGS: MSVC marks _wfopen deprecated (C4996), which /WX turns into an error.
            & $cc /nologo /O2 /std:c++20 /EHsc /W4 /WX /DWIN32_LEAN_AND_MEAN /D_WIN32_WINNT=0x0A00 /D_CRT_SECURE_NO_WARNINGS /LD $SrcFile "/Fo:$TmpDir\ParryLoot.obj" "/Fe:$DllOut" /link /NOIMPLIB
        } else {
            & $cc "-std=c++20" "-O2" "-s" "-Wall" "-Wextra" "-Werror" "-DWIN32_LEAN_AND_MEAN" "-D_WIN32_WINNT=0x0A00" "-shared" $SrcFile "-o" $DllOut "-static" "-static-libgcc" "-static-libstdc++" "-Wl,--exclude-all-symbols"
        }
        if ($LASTEXITCODE -ne 0) {
            throw "Compilation failed with exit code $LASTEXITCODE"
        }
    } finally {
        $env:TEMP = $oldTemp; $env:TMP = $oldTmp
    }
} else {
    if ((Get-Item $SrcFile).LastWriteTime -gt (Get-Item $DllOut).LastWriteTime) {
        throw "build\ParryLoot.dll is older than src\ParryLoot.cpp, so it would ship old code. Run .\pack.ps1 -Build."
    }
    Write-Host "Using existing compiled binary: $DllOut" -ForegroundColor DarkGray
}

# Stage clean Vortex / ModEngine 2 zip layout (no source/tooling inside the archive)
if (Test-Path $StageDir) { Remove-Item -Recurse -Force $StageDir }
$StageMods = Join-Path $StageDir "mods"
$StageDoc = Join-Path $StageMods "ParryLoot"
New-Item -ItemType Directory -Force $StageDoc | Out-Null

Copy-Item -Force $DllOut (Join-Path $StageMods "ParryLoot.dll")
Copy-Item -Force (Join-Path $Root "assets\mods\ParryLoot.ini") (Join-Path $StageMods "ParryLoot.ini")
# These two names matter for Vortex. Its Elden Ring support drops files without an extension and
# files named like one in ModEngine 2's own package (README.txt), and a mod left with only a DLL
# and an INI gets no entry on Vortex's Load Order tab, so Vortex never writes it into
# config_eldenring.toml. Kept files give the mod an entry, and the next deploy writes the line.
Copy-Item -Force (Join-Path $Root "assets\mods\ParryLoot\ParryLoot-README.txt") (Join-Path $StageDoc "ParryLoot-README.txt")
Copy-Item -Force (Join-Path $Root "LICENSE") (Join-Path $StageDoc "LICENSE.txt")

if (Test-Path $ZipPath) { Remove-Item -Force $ZipPath }
# Not Compress-Archive: in Windows PowerShell 5.1 it writes entry names with backslashes
# ("mods\ParryLoot.dll"), which the zip format does not allow. Linux and Steam Deck tools then
# extract flat files literally named "mods\ParryLoot.dll", and the release workflow's
# check for mods/ParryLoot.dll fails. Write every name with forward slashes instead.
Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
$Archive = [System.IO.Compression.ZipFile]::Open($ZipPath, [System.IO.Compression.ZipArchiveMode]::Create)
try {
    foreach ($File in Get-ChildItem -LiteralPath $StageDir -Recurse -File | Sort-Object FullName) {
        $EntryName = $File.FullName.Substring($StageDir.Length + 1).Replace('\', '/')
        [System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile(
            $Archive, $File.FullName, $EntryName, [System.IO.Compression.CompressionLevel]::Optimal) | Out-Null
    }
} finally {
    $Archive.Dispose()
}

$Expected = @("mods/ParryLoot.dll", "mods/ParryLoot.ini", "mods/ParryLoot/LICENSE.txt", "mods/ParryLoot/ParryLoot-README.txt")
$Check = [System.IO.Compression.ZipFile]::OpenRead($ZipPath)
try {
    $Names = @($Check.Entries | ForEach-Object { $_.FullName })
} finally {
    $Check.Dispose()
}
$Bad = @($Names | Where-Object { $_ -match '\\' })
if ($Bad) { throw "Zip entry names must use forward slashes: $($Bad -join ', ')" }
$Missing = @($Expected | Where-Object { $_ -notin $Names })
if ($Missing) { throw "Zip is missing: $($Missing -join ', ')" }
$Extra = @($Names | Where-Object { $_ -notin $Expected })
if ($Extra) { throw "Zip contains unexpected files: $($Extra -join ', ')" }

$Hash = (Get-FileHash -Algorithm SHA256 $ZipPath).Hash.ToLowerInvariant()
$ZipSize = (Get-Item $ZipPath).Length
$Info = [ordered]@{
    name = "Parry Loot"
    version = $Version
    archive = [System.IO.Path]::GetFileName($ZipPath)
    size_bytes = $ZipSize
    sha256 = $Hash
}
$Info | ConvertTo-Json -Depth 4 | Set-Content -Encoding UTF8 $InfoPath

Write-Host "Packaged: $ZipPath ($ZipSize bytes, sha256=$Hash)" -ForegroundColor Green

if (-not $SkipChecks) {
    $py = Get-Command python -ErrorAction SilentlyContinue
    if (-not $py) { $py = Get-Command py -ErrorAction SilentlyContinue }
    if ($py) {
        & $py.Source (Join-Path $Root "tools\smoke_check.py")
        if ($LASTEXITCODE -ne 0) { throw "smoke_check.py failed" }
    }
}

if ($Install) {
    if (-not $GamePath) { $GamePath = $env:ELDEN_RING_GAME }
    if (-not $GamePath -or -not (Test-Path $GamePath)) {
        throw "Specify -GamePath <your ModEngine 2 folder, with modengine2_launcher.exe> or set ELDEN_RING_GAME to install."
    }
    $TargetMods = Join-Path $GamePath "mods"
    New-Item -ItemType Directory -Force (Join-Path $TargetMods "ParryLoot") | Out-Null
    Copy-Item -Force (Join-Path $StageMods "ParryLoot.dll") (Join-Path $TargetMods "ParryLoot.dll")
    if (-not (Test-Path (Join-Path $TargetMods "ParryLoot.ini"))) {
        Copy-Item -Force (Join-Path $StageMods "ParryLoot.ini") (Join-Path $TargetMods "ParryLoot.ini")
    }
    Copy-Item -Force (Join-Path $StageDoc "ParryLoot-README.txt") (Join-Path $TargetMods "ParryLoot\ParryLoot-README.txt")
    Write-Host "Installed to $TargetMods" -ForegroundColor Green
}
