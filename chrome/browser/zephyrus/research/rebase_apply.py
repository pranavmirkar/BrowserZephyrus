#!/usr/bin/env python3
"""Moves the Zephyrus changes from the current tree onto a clean checkout of a newer Chromium.

  rebase_apply.py <old tree> <new tree> <base rev> [--dry-run]

  old tree   the Zephyrus tree (its working tree is "ours", its git history knows the base)
  new tree   a clean checkout of the target release (its working tree is "theirs")
  base rev   the upstream commit the Zephyrus work started from (git rev-parse c50805ee18^)

For every file the Zephyrus work added, changed or deleted:
  added      copied over (reported if the new release has the same path already)
  changed    upstream did not touch it between base and target  -> ours is copied
             upstream touched it                                  -> three-way merge
                                                                    (git merge-file, diff3 markers);
                                                                    conflicts are left in the file
  deleted    deleted in the new tree if upstream left it alone
  missing    changed by us but gone from the new release (moved or deleted upstream): not touched,
             listed for a person
Writes rebase_report.json and rebase_report.md next to this script's working directory and, unless
--dry-run, changes the new tree. Never touches the old tree.
"""
import json
import os
import subprocess
import sys
import tempfile

SKIP_EXACT = {
    "chrome/VERSION",  # the fork's version is upstream's own on a rebased tree
}
SKIP_PREFIXES = ("out/", ".git/")
SKIP_SUFFIXES = (".log", ".pyc")


def git(tree, *args, text=True, check=False):
    r = subprocess.run(["git", "-C", tree] + list(args), capture_output=True)
    if check and r.returncode != 0:
        raise RuntimeError("git %s failed: %s" % (" ".join(args), r.stderr.decode("utf-8", "replace")))
    return r.stdout.decode("utf-8", "replace") if text else r.stdout


def blob_at(tree, rev, path):
    r = subprocess.run(["git", "-C", tree, "show", "%s:%s" % (rev, path)], capture_output=True)
    return r.stdout if r.returncode == 0 else None


def is_binary(data):
    return b"\0" in data[:8000]


def main():
    old, new, base = sys.argv[1:4]
    dry = "--dry-run" in sys.argv
    status = {}
    for line in git(old, "diff", "--name-status", base).splitlines():
        parts = line.split("\t")
        status[parts[1]] = parts[0][0]
    for p in git(old, "ls-files", "--others", "--exclude-standard").splitlines():
        status.setdefault(p, "A")
    status = {p: s for p, s in status.items()
              if p not in SKIP_EXACT and not p.startswith(SKIP_PREFIXES) and not p.endswith(SKIP_SUFFIXES)}

    rep = {"copied_added": [], "added_but_exists_upstream": [], "copied_unchanged_upstream": [],
           "merged_clean": [], "merged_conflicts": [], "binary_conflicts": [], "missing_upstream": [],
           "deleted": [], "delete_skipped": [], "errors": []}
    for path in sorted(status):
        st = status[path]
        ours_path = os.path.join(old, path)
        theirs_path = os.path.join(new, path)
        try:
            if st == "D":
                if os.path.exists(theirs_path):
                    base_blob = blob_at(old, base, path)
                    if base_blob == open(theirs_path, "rb").read():
                        if not dry:
                            os.remove(theirs_path)
                        rep["deleted"].append(path)
                    else:
                        rep["delete_skipped"].append(path)
                continue
            if not os.path.isfile(ours_path):
                rep["errors"].append(path + " (not in the old working tree)")
                continue
            ours = open(ours_path, "rb").read()
            if st == "A":
                if os.path.exists(theirs_path):
                    rep["added_but_exists_upstream"].append(path)
                    if open(theirs_path, "rb").read() == ours:
                        continue
                if not dry:
                    os.makedirs(os.path.dirname(theirs_path), exist_ok=True)
                    open(theirs_path, "wb").write(ours)
                rep["copied_added"].append(path)
                continue
            # changed
            if not os.path.exists(theirs_path):
                rep["missing_upstream"].append(path)
                continue
            theirs = open(theirs_path, "rb").read()
            base_blob = blob_at(old, base, path)
            if base_blob is None:
                rep["errors"].append(path + " (no base version)")
                continue
            if base_blob == theirs:
                if not dry:
                    open(theirs_path, "wb").write(ours)
                rep["copied_unchanged_upstream"].append(path)
                continue
            if ours == base_blob:
                continue  # we did not really change it
            if is_binary(ours) or is_binary(theirs) or is_binary(base_blob):
                rep["binary_conflicts"].append(path)
                continue
            with tempfile.TemporaryDirectory() as tmp:
                fo, fb, ft = (os.path.join(tmp, n) for n in ("ours", "base", "theirs"))
                open(fo, "wb").write(ours)
                open(fb, "wb").write(base_blob)
                open(ft, "wb").write(theirs)
                r = subprocess.run(["git", "merge-file", "-p", "--diff3", "-L", "zephyrus", "-L", "base-151",
                                    "-L", "upstream-156", fo, fb, ft], capture_output=True)
                if r.returncode < 0 or r.returncode > 127:
                    rep["errors"].append(path + " (merge-file failed)")
                    continue
                if not dry:
                    open(theirs_path, "wb").write(r.stdout)
                if r.returncode == 0:
                    rep["merged_clean"].append(path)
                else:
                    rep["merged_conflicts"].append([path, r.returncode])
        except Exception as e:  # keep going: one odd file must not stop the pass
            rep["errors"].append("%s (%s)" % (path, e))

    json.dump(rep, open("rebase_report.json", "w"), indent=1)
    lines = ["# Rebase pass: %s -> %s\n" % (base[:10], os.path.basename(new)), "| bucket | files |", "|---|---|"]
    for k, v in rep.items():
        lines.append("| %s | %d |" % (k.replace("_", " "), len(v)))
    for k in ("merged_conflicts", "binary_conflicts", "missing_upstream", "added_but_exists_upstream",
              "delete_skipped", "errors"):
        if rep[k]:
            lines.append("\n## %s\n" % k.replace("_", " "))
            lines += ["- %s" % (x if isinstance(x, str) else "%s (%d conflict regions)" % tuple(x)) for x in rep[k]]
    open("rebase_report.md", "w").write("\n".join(lines) + "\n")
    print("\n".join(lines[:14]))
    if dry:
        print("(dry run: the new tree was not changed)")


if __name__ == "__main__":
    main()
