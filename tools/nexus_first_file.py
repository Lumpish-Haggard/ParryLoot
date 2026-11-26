"""
nexus_first_file.py - create the mod's main file on Nexus Mods with its first version.

    python tools/nexus_first_file.py <zip> <version> --name NAME [--description TEXT] [--changelog FILE]

Environment: NEXUSMODS_API_KEY, NEXUS_MOD_ID (the unique mod ID) and NEXUS_API
(default https://api.nexusmods.com/v3).

Nexus-Mods/upload-action can only add a version to a file that already exists. This creates
that file: the same upload the action makes (multipart upload, finalise, wait until Nexus has
processed it), then POST /mod-files instead of POST /mod-files/{id}/versions, then the version's
changelog. It prints the new file's ID: set it as NEXUS_FILE_ID in
.github/workflows/nexus-release.yml, so that later releases add versions to this file.
When GITHUB_OUTPUT is set, it also writes file_id=... and game_scoped_file_id=... there.
"""
from __future__ import annotations

import argparse
import json
import os
import sys
import time
import urllib.error
import urllib.request
from xml.sax.saxutils import escape

API = os.environ.get("NEXUS_API", "https://api.nexusmods.com/v3").rstrip("/")
USER_AGENT = "ParryLoot-release"


def fail(msg: str) -> None:
    print(f"::error::{msg}", file=sys.stderr)
    sys.exit(1)


def send(req: urllib.request.Request, what: str, timeout: int = 120) -> tuple[bytes, dict]:
    try:
        with urllib.request.urlopen(req, timeout=timeout) as r:
            return r.read(), dict(r.headers)
    except urllib.error.HTTPError as e:
        fail(f"{what} failed: HTTP {e.code}: {e.read().decode('utf-8', 'replace')[:2000]}")
    except urllib.error.URLError as e:
        fail(f"{what} failed: {e.reason}")
    raise AssertionError("unreachable")


def api(method: str, path: str, key: str, body: dict | None = None) -> dict:
    data = json.dumps(body).encode("utf-8") if body is not None else (b"" if method == "POST" else None)
    req = urllib.request.Request(API + path, data=data, method=method, headers={
        "apikey": key,
        "Accept": "application/json",
        "Content-Type": "application/json",
        "User-Agent": USER_AGENT,
    })
    raw, _ = send(req, f"{method} {path}")
    return json.loads(raw) if raw.strip() else {}


def upload(zip_path: str, key: str) -> str:
    """Multipart upload, as Nexus-Mods/upload-action does it. Returns the upload ID."""
    size = os.path.getsize(zip_path)
    d = api("POST", "/uploads/multipart", key,
            {"filename": os.path.basename(zip_path), "size_bytes": str(size)})["data"]
    upload_id, urls, part = d["id"], d["part_presigned_urls"], int(d["part_size_bytes"])
    if not urls or part <= 0 or len(urls) * part < size:
        fail(f"Nexus offered {len(urls)} part(s) of {part} bytes for a file of {size} bytes")
    print(f"Upload {upload_id}: {size} bytes in {len(urls)} part(s) of up to {part} bytes")

    etags = []
    with open(zip_path, "rb") as f:
        for n, url in enumerate(urls, 1):
            chunk = f.read(part)
            req = urllib.request.Request(url, data=chunk, method="PUT",
                                         headers={"Content-Type": "application/octet-stream"})
            _, headers = send(req, f"uploading part {n}", timeout=600)
            etag = next((v for k, v in headers.items() if k.lower() == "etag"), "")
            if not etag:
                fail(f"no ETag returned for part {n}")
            etags.append(etag.replace('"', ""))

    xml = "<CompleteMultipartUpload>\n" + "\n".join(
        f"  <Part>\n    <PartNumber>{n}</PartNumber>\n    <ETag>{escape(t)}</ETag>\n  </Part>"
        for n, t in enumerate(etags, 1)) + "\n</CompleteMultipartUpload>"
    send(urllib.request.Request(d["complete_presigned_url"], data=xml.encode("utf-8"), method="POST",
                                headers={"Content-Type": "application/xml"}), "completing the upload")

    api("POST", f"/uploads/{upload_id}/finalise", key)
    delay = 2.0
    for _ in range(40):
        state = api("GET", f"/uploads/{upload_id}", key)["data"]["state"]
        print(f"Upload {upload_id}: {state}")
        if state == "available":
            return upload_id
        time.sleep(delay)
        delay = min(delay * 1.5, 30.0)
    fail(f"upload {upload_id} was not processed in time")
    raise AssertionError("unreachable")


def main() -> int:
    sys.stdout.reconfigure(encoding="utf-8")
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("zip")
    ap.add_argument("version")
    ap.add_argument("--name", required=True, help="the file's display name on Nexus Mods")
    ap.add_argument("--description", default="", help="the file's description (at most 255 characters)")
    ap.add_argument("--changelog", help="text file with this version's changelog")
    args = ap.parse_args()

    key = os.environ.get("NEXUSMODS_API_KEY", "")
    mod_id = os.environ.get("NEXUS_MOD_ID", "")
    if not key or not mod_id:
        fail("NEXUSMODS_API_KEY and NEXUS_MOD_ID must be set")
    if not os.path.isfile(args.zip):
        fail(f"{args.zip} not found")
    if len(args.description) > 255:
        fail(f"the file description is {len(args.description)} characters; Nexus allows 255")
    changelog = ""
    if args.changelog:
        with open(args.changelog, encoding="utf-8") as f:
            changelog = f.read().strip()

    upload_id = upload(args.zip, key)
    created = api("POST", "/mod-files", key, {
        "upload_id": upload_id,
        "mod_id": mod_id,
        "name": args.name,
        "version": args.version,
        "description": args.description or None,
        "file_category": "main",
        "primary_mod_manager_download": True,
        "allow_mod_manager_download": True,
        "show_requirements_pop_up": False,
        "update_mod_version": True,
    })["data"]
    file_id, scoped = str(created["id"]), str(created.get("game_scoped_id", ""))
    print(f"Created the main file '{created.get('name', args.name)}': file ID {file_id} (game-scoped {scoped})")

    if changelog:
        api("POST", f"/mods/{mod_id}/changelogs", key, {"version": args.version, "changelog": changelog})
        print(f"Added the changelog for {args.version}")

    out = os.environ.get("GITHUB_OUTPUT")
    if out:
        with open(out, "a", encoding="utf-8") as f:
            f.write(f"file_id={file_id}\ngame_scoped_file_id={scoped}\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
