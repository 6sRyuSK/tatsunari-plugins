#!/usr/bin/env python3
"""
tools/set_release_links.py — point the README's curl install commands at a tag.

The download URLs in the README embed a concrete release tag (`<year>.<n>`) and
its filename token (`v<year>_<n>`). This script rewrites those tokens to a given
tag so the released / committed README always shows the latest bundle. Only the
region between the markers is touched:

  <!-- BEGIN:INSTALL -->  ...  <!-- END:INSTALL -->

Usage:
  python tools/set_release_links.py 2026.1          # rewrite README.md in place
  python tools/set_release_links.py 2026.1 --check  # exit 1 if out of date (CI)

Requires Python 3.
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
README = ROOT / "README.md"
BEGIN = "<!-- BEGIN:INSTALL -->"
END = "<!-- END:INSTALL -->"
TAG_RE = re.compile(r"^[0-9]{4}\.[0-9]+$")


def vus(tag: str) -> str:
    """`2026.1` -> `v2026_1` (filename token; dots would collide with `-`)."""
    return "v" + tag.replace(".", "_")


def rewrite(region: str, tag: str) -> str:
    t = vus(tag)
    # URL path tag: .../releases/download/<year>.<n>/
    region = re.sub(r"releases/download/[0-9]{4}\.[0-9]+/",
                    f"releases/download/{tag}/", region)
    # asset filename token: tatsunari-sounds-v<year>_<n>-<os>.zip
    region = re.sub(r"tatsunari-sounds-v[0-9]{4}_[0-9]+-",
                    f"tatsunari-sounds-{t}-", region)
    # inline `2026.1` / `v2026_1` references in the prose
    region = re.sub(r"`[0-9]{4}\.[0-9]+`", f"`{tag}`", region)
    region = re.sub(r"`v[0-9]{4}_[0-9]+`", f"`{t}`", region)
    return region


def splice(text: str, tag: str) -> str:
    if BEGIN not in text or END not in text:
        raise SystemExit(f"README.md is missing the markers {BEGIN} / {END}.")
    pre, rest = text.split(BEGIN, 1)
    region, post = rest.split(END, 1)
    return f"{pre}{BEGIN}{rewrite(region, tag)}{END}{post}"


def main() -> None:
    args = [a for a in sys.argv[1:] if a != "--check"]
    check = "--check" in sys.argv
    if len(args) != 1 or not TAG_RE.match(args[0]):
        raise SystemExit("usage: set_release_links.py <year>.<n> [--check]")
    tag = args[0]
    current = README.read_text(encoding="utf-8")
    updated = splice(current, tag)
    if check:
        if current != updated:
            print(f"README download links are not set to {tag}. "
                  f"Run: python tools/set_release_links.py {tag}")
            sys.exit(1)
        print(f"README download links are current ({tag}).")
        return
    if current != updated:
        README.write_text(updated, encoding="utf-8")
        print(f"README download links set to {tag}.")
    else:
        print(f"README download links already current ({tag}).")


if __name__ == "__main__":
    main()
