#!/usr/bin/env python3
"""Report translation coverage without mistaking English fallback for translation (original's tool).

Usage: python3 tools/audit_translations.py [--strict] [--json] [--root DIR]
Reads the languages tools/assets.py builds, the base labels, the community files in lng_src/ (when
downloaded) and the fork overlays in lng/fork/ (their translations: and overrides: maps). No files are changed.

--strict fails on an overlay label the base does not have, or an overlay string whose printf tokens
differ from English. A community string that differs is only a warning: lang.cpp's lngFormatFits
shows English instead. The build writes overlay strings INTO lng_src/, so a value there that equals
the overlay's is still the overlay's -- and stays an error.
"""
import argparse
import json
import re
from pathlib import Path

import yaml

SPEC = re.compile(r"%(?:\d+\$)?[-+#0 ]*(?:\d+|\*)?(?:\.(?:\d+|\*))?(?:hh|h|ll|l|j|z|t|L)?[diouxXfFeEgGaAcspn]")


def format_tokens(value):
    """printf conversions in order; %% is literal, and %<letter> that is no conversion is an error."""
    tokens, i = [], 0
    while i < len(value):
        if value[i] != "%":
            i += 1
        elif value[i + 1:i + 2] == "%":
            i += 2
        elif SPEC.match(value, i):
            tokens.append(SPEC.match(value, i).group())
            i = SPEC.match(value, i).end()
        elif value[i + 1:i + 2].isalpha():
            raise ValueError(f"invalid printf format near {value[i:i + 8]!r}")
        else:
            i += 1  # a plain percentage ("100% full")
    return tokens


def languages(root):
    text = (root / "tools/assets.py").read_text(encoding="utf-8")
    names = re.search(r'TRANSLATIONS = """(.*?)"""', text, re.S).group(1).split()
    if not names or len(names) != len(set(names)):
        raise ValueError("tools/assets.py: malformed or duplicate TRANSLATIONS")
    return names


def read_map(path, section="translations"):
    if not path.exists():
        return {}
    entries = (yaml.safe_load(path.read_text(encoding="utf-8")) or {}).get(section, {}) or {}
    if not isinstance(entries, dict):
        raise ValueError(f"{path}: {section} is not a map")
    return {k: v for k, v in entries.items() if isinstance(v, str)}


def audit(root):
    entries = yaml.safe_load((root / "lng/tmpl/_base.yml").read_text(encoding="utf-8"))["gui_strings"]
    labels = [e["label"] for e in entries]
    if len(labels) != len(set(labels)):
        raise ValueError("duplicate labels in lng/tmpl/_base.yml")
    english = {e["label"]: e["string"] for e in entries}
    report = {"total_strings": len(labels), "upstream_present": (root / "lng_src").is_dir(),
              "languages": {}, "errors": [], "warnings": []}
    for name in languages(root):
        overlay = read_map(root / "lng/fork" / (name + ".yml"))
        overrides = read_map(root / "lng/fork" / (name + ".yml"), "overrides")
        upstream = read_map(root / "lng_src" / (name + ".yml"))
        # The build applies overrides last, so they also win over upstream (and are checked as ours).
        overlay = {**overlay, **overrides}
        upstream = {k: v for k, v in upstream.items() if k not in overrides}
        merged = {**overlay, **upstream}
        report["errors"] += [f"{name}: overlay has unknown label {k}" for k in sorted(set(overlay) - set(english))]
        for key, value in merged.items():
            if key not in english:
                continue
            community = key in upstream and upstream[key] != overlay.get(key)
            where = f"{name}/{key} ({'upstream' if community else 'overlay'})"
            try:
                tokens, wanted = format_tokens(value), format_tokens(english[key])
                problem = None if tokens == wanted else f"printf tokens {tokens} differ from English {wanted}"
            except ValueError as error:
                problem = str(error)
            if problem:
                report["warnings" if community else "errors"].append(f"{where}: {problem}")
        fallback = [k for k in labels if merged.get(k, english[k]) == english[k]]
        report["languages"][name] = {"overlay_entries": len(overlay), "translated_entries": len(labels) - len(fallback),
                                     "english_fallback_entries": len(fallback),
                                     "recent_fallback_labels": [k for k in labels[-60:] if k in fallback]}
    return report


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--strict", action="store_true", help="fail on unknown overlay labels and unsafe formats")
    parser.add_argument("--json", action="store_true")
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    args = parser.parse_args()
    report = audit(args.root)
    if args.json:
        print(json.dumps(report, indent=2, ensure_ascii=False))
    else:
        print(f"{len(report['languages'])} languages / {report['total_strings']} strings; "
              f"community translations present: {report['upstream_present']}")
        if not report["upstream_present"]:
            print("NOTE: overlay-only estimates; run tools/download_lng.sh for packaged-language totals.")
        for name, c in report["languages"].items():
            print(f"{name:17} overlay={c['overlay_entries']:3} translated={c['translated_entries']:3} "
                  f"English fallback={c['english_fallback_entries']:3} "
                  f"recent fallback={len(c['recent_fallback_labels']):2}/{min(60, report['total_strings'])}")
        for warning in report["warnings"]:
            print("WARNING:", warning)
        for error in report["errors"]:
            print("ERROR:", error)
    return 1 if args.strict and report["errors"] else 0


if __name__ == "__main__":
    raise SystemExit(main())
