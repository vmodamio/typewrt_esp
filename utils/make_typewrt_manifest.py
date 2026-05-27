#!/usr/bin/env python3
"""Create a Typewrt sync manifest that preserves source file mtimes."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path


MANIFEST_NAME = ".typewrt-sync.json"


def sha256_hex(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as src:
        for chunk in iter(lambda: src.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def include_file(path: Path, root: Path, manifest_name: str) -> bool:
    rel = path.relative_to(root).as_posix()
    return (
        path.is_file()
        and rel != manifest_name
        and not rel.startswith(".git/")
    )


def build_manifest(root: Path, manifest_name: str) -> dict:
    files = {}
    for path in sorted(root.rglob("*")):
        if not include_file(path, root, manifest_name):
            continue
        rel = path.relative_to(root).as_posix()
        stat = path.stat()
        files[rel] = {
            "mtime": int(stat.st_mtime),
            "size": stat.st_size,
            "hash": sha256_hex(path),
        }
    return {
        "version": 1,
        "files": files,
    }


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Generate .typewrt-sync.json for a folder before committing it to GitHub."
    )
    parser.add_argument(
        "root",
        nargs="?",
        default=".",
        help="Folder to scan. Defaults to the current directory.",
    )
    parser.add_argument(
        "--manifest-name",
        default=MANIFEST_NAME,
        help=f"Manifest filename to write. Defaults to {MANIFEST_NAME}.",
    )
    args = parser.parse_args()

    root = Path(args.root).expanduser().resolve()
    if not root.is_dir():
        parser.error(f"{root} is not a directory")

    manifest = build_manifest(root, args.manifest_name)
    output = root / args.manifest_name
    output.write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    print(f"Wrote {output} with {len(manifest['files'])} file entries.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
