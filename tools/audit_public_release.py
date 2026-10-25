#!/usr/bin/env python3
"""Check that the repository tree does not contain common private/game-derived outputs."""
from __future__ import annotations

import argparse
from pathlib import Path
import sys

FORBIDDEN_SUFFIXES = {
    ".mld", ".mlt", ".sfd", ".bda", ".adpcm", ".elf",
    ".wav", ".mp3", ".mp4", ".mkv", ".mov", ".avi",
    ".obj", ".ply", ".bin", ".iso", ".rom", ".dol",
}
FORBIDDEN_DIRECTORIES = {
    "private", "private_research", "research", "research_notes",
    "findings", "raw_dumps", "game_dumps", "site", "exports",
}
FORBIDDEN_SUFFIXES_UI = {".html", ".htm"}
SKIP_DIRECTORIES = {".git", ".venv", "venv", "build", "dist", "__pycache__", ".pytest_cache"}
MAX_FILE_BYTES = 25 * 1024 * 1024


def audit(root: Path) -> list[str]:
    issues: list[str] = []
    for path in root.rglob("*"):
        if not path.is_file():
            continue
        relative = path.relative_to(root)
        parts = [part.lower() for part in relative.parts]
        if any(part in SKIP_DIRECTORIES for part in parts):
            continue
        if any(part in FORBIDDEN_DIRECTORIES for part in parts[:-1]):
            issues.append(f"private/research directory: {relative.as_posix()}")
            continue
        suffix = path.suffix.lower()
        if suffix in FORBIDDEN_SUFFIXES_UI:
            issues.append(f"HTML UI is not part of the public desktop project: {relative.as_posix()}")
        elif suffix in FORBIDDEN_SUFFIXES:
            issues.append(f"potential game-derived or generated asset: {relative.as_posix()}")
        try:
            if path.stat().st_size > MAX_FILE_BYTES:
                issues.append(f"file exceeds 25 MiB release threshold: {relative.as_posix()}")
        except OSError:
            issues.append(f"could not stat file: {relative.as_posix()}")
    return issues


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root", nargs="?", type=Path, default=Path(__file__).resolve().parents[1])
    args = parser.parse_args()
    issues = audit(args.root.resolve())
    if issues:
        print("FAIL: public-release audit found items that need review:")
        for issue in issues:
            print(f"  - {issue}")
        return 1
    print("PASS: no blocked media/model dumps, private research directories, or HTML UI files found.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
