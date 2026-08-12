#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Check local Markdown links and heading fragments without network access."""

from __future__ import annotations

from collections import Counter
from pathlib import Path
import re
import sys
from urllib.parse import unquote


REPO = Path(__file__).resolve().parents[1]
SKIPPED_DIRECTORIES = {".git", ".venv", "venv", "build", ".esphome"}
LINK = re.compile(r"(?<!!)\[[^\]]+\]\(([^)]+)\)")
HEADING = re.compile(r"^#{1,6}\s+(.+?)\s*#*\s*$")
INLINE_MARKUP = re.compile(r"[`*_~]")
HTML_TAG = re.compile(r"<[^>]+>")
URI_SCHEME = re.compile(r"^[a-z][a-z0-9+.-]*:", re.IGNORECASE)


def markdown_files() -> list[Path]:
    return sorted(
        path
        for path in REPO.rglob("*.md")
        if not any(part in SKIPPED_DIRECTORIES for part in path.relative_to(REPO).parts)
    )


def github_slug(text: str) -> str:
    """Return the common subset of GitHub's generated heading identifiers."""

    text = HTML_TAG.sub("", text)
    text = INLINE_MARKUP.sub("", text).strip().lower()
    text = re.sub(r"[^\w\- ]", "", text, flags=re.UNICODE)
    return re.sub(r"\s+", "-", text)


def heading_ids(path: Path) -> set[str]:
    identifiers: set[str] = set()
    duplicates: Counter[str] = Counter()
    fenced = False

    for line in path.read_text(encoding="utf-8").splitlines():
        if line.lstrip().startswith("```"):
            fenced = not fenced
            continue
        if fenced:
            continue
        match = HEADING.match(line)
        if not match:
            continue
        base = github_slug(match.group(1))
        suffix = duplicates[base]
        duplicates[base] += 1
        identifiers.add(base if suffix == 0 else f"{base}-{suffix}")

    return identifiers


def link_destination(raw: str) -> str:
    """Strip an optional Markdown title from the simple links used here."""

    value = raw.strip()
    if value.startswith("<") and ">" in value:
        return value[1 : value.index(">")]
    return value.split(maxsplit=1)[0]


def main() -> int:
    failures: list[str] = []
    files = markdown_files()

    for source in files:
        text = source.read_text(encoding="utf-8")
        for match in LINK.finditer(text):
            line_number = text.count("\n", 0, match.start()) + 1
            destination = unquote(link_destination(match.group(1)))
            if not destination or URI_SCHEME.match(destination):
                continue

            path_text, separator, fragment = destination.partition("#")
            target = source if not path_text else (source.parent / path_text).resolve()

            try:
                target.relative_to(REPO)
            except ValueError:
                failures.append(
                    f"{source.relative_to(REPO)}:{line_number}: "
                    f"local link leaves repository: {destination}"
                )
                continue

            if not target.exists():
                failures.append(
                    f"{source.relative_to(REPO)}:{line_number}: "
                    f"missing local target: {destination}"
                )
                continue

            if separator and fragment and target.suffix.lower() == ".md":
                if fragment.lower() not in heading_ids(target):
                    failures.append(
                        f"{source.relative_to(REPO)}:{line_number}: "
                        f"missing heading fragment: {destination}"
                    )

    if failures:
        print("Documentation link check failed:", file=sys.stderr)
        for failure in failures:
            print(f"- {failure}", file=sys.stderr)
        return 1

    print(f"Validated local links in {len(files)} Markdown files.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
