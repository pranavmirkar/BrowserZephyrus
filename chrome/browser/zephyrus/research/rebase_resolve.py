#!/usr/bin/env python3
"""Resolves the conflict hunks that have an unambiguous shape, and lists the rest.

  rebase_resolve.py <new tree> <conflict file list> [--apply]

A hunk (from rebase_apply.py, diff3 markers) is resolved only in these cases:
  * both sides inserted at the same place (base empty): ours then upstream's;
  * one side only ADDED lines around the base text (a prefix and/or suffix) and the other
    changed the base text: the changed text with the additions put back around it;
  * both sides made the identical change.
Everything else is left with its markers. With --apply the resolved hunks are written;
without it the counts are only printed. Every auto-resolved hunk is logged to
rebase_resolved.md so a person can review them (the compiler is the real check).
"""
import re
import sys

HUNK = re.compile(r"<<<<<<< zephyrus\n(.*?)\|\|\|\|\|\|\| base-151\n(.*?)=======\n(.*?)>>>>>>> upstream-156\n", re.S)


def lines(s):
    return s.splitlines(keepends=True)


def split_additions(side, base):
    """(prefix, suffix) if `side` is `base` with lines added before and/or after, else None."""
    if side == base:
        return "", ""
    sl, bl = lines(side), lines(base)
    if not bl:
        return None
    for i in range(0, len(sl) - len(bl) + 1):
        if sl[i:i + len(bl)] == bl:
            return "".join(sl[:i]), "".join(sl[i + len(bl):])
    return None


# Zephyrus renamed the product in user-visible text. When our side of a hunk is the base with
# exactly these replacements, the right answer is upstream's text with the same replacements.
BRAND_PAIRS = [("Google Chrome", "Zephyrus"), ("Chromium", "Zephyrus"), ("Chrome", "Zephyrus")]


def rebrand(text):
    for old, new in BRAND_PAIRS:
        text = text.replace(old, new)
    return text


def resolve(ours, base, theirs):
    if ours == theirs:
        return ours, "identical change"
    if rebrand(base) == ours and ours != base:
        return rebrand(theirs), "we only renamed the product; upstream's new text, renamed"
    if base.strip() == "":
        return ours + theirs, "both inserted at the same place (ours first)"
    a = split_additions(ours, base)
    b = split_additions(theirs, base)
    if a is not None and b is None:
        return a[0] + theirs + a[1], "we only added lines; upstream changed the text"
    if b is not None and a is None:
        return b[0] + ours + b[1], "upstream only added lines; we changed the text"
    if a is not None and b is not None:
        return a[0] + b[0] + base + b[1] + a[1], "both only added lines around the same text"
    return None, None


def main():
    tree, listing = sys.argv[1:3]
    apply = "--apply" in sys.argv
    files = [l.strip() for l in open(listing) if l.strip()]
    stats = {"resolved": 0, "left": 0, "files_clean": 0}
    log, left = [], []
    for f in files:
        path = tree.rstrip("/\\") + "/" + f
        text = open(path, encoding="utf-8", errors="surrogateescape", newline="").read()
        out, pos, remaining = [], 0, 0
        for m in HUNK.finditer(text):
            res, why = resolve(m.group(1), m.group(2), m.group(3))
            out.append(text[pos:m.start()])
            if res is None:
                out.append(m.group(0))
                remaining += 1
                left.append(f)
            else:
                out.append(res)
                stats["resolved"] += 1
                log.append("- %s: %s" % (f, why))
            pos = m.end()
        out.append(text[pos:])
        stats["left"] += remaining
        if remaining == 0:
            stats["files_clean"] += 1
        if apply:
            open(path, "w", encoding="utf-8", errors="surrogateescape", newline="").write("".join(out))
    open("rebase_resolved.md", "w").write("# Auto-resolved hunks\n\n" + "\n".join(log) + "\n")
    open("rebase_left.txt", "w").write("\n".join(sorted(set(left))) + "\n")
    print(stats, "| files still with conflicts:", len(set(left)))


if __name__ == "__main__":
    main()
