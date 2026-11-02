#!/usr/bin/env python3
"""Downloads the open feeds and rebuilds the two blobs the browser ships.

  refresh_threat_data.py [work dir]

Run before building an installer: the shipped list is a snapshot, and phishing feeds go
stale in hours. Writes chrome/browser/zephyrus/threat/data/zephyrus_threats.bin and
zephyrus_popular.bin (generated files, not committed), then checks them.

Feeds (all free, no account): Phishing.Database links and domains (MIT) and the Blocklist
Project phishing list. Add a feed only after checking its licence (THIRD_PARTY_DATA.md).
The Majestic Million (CC BY 3.0, attribution in THIRD_PARTY_DATA.md) only decides which sites
Shield DNS skips: none of its entries is shipped as a verdict, only as "do not ask about
this host". Tranco is deliberately not used: it aggregates a non-commercial source.
"""
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
DATA = os.path.normpath(os.path.join(HERE, "..", "threat", "data"))
PDB = "https://raw.githubusercontent.com/mitchellkrogza/Phishing.Database/master/"
FEEDS = [
    (PDB + "phishing-links-ACTIVE.txt", "pdb_links.txt"),
    (PDB + "phishing-domains-ACTIVE.txt", "pdb_domains.txt"),
    ("https://raw.githubusercontent.com/blocklistproject/Lists/master/phishing.txt", "blp_phishing.txt"),
]
PSL = ("https://publicsuffix.org/list/public_suffix_list.dat", "psl.dat")
MAJESTIC = ("https://downloads.majestic.com/majestic_million.csv", "majestic_million.csv")


def fetch(url, path):
    print("fetching", url)
    subprocess.run(["curl", "-s", "-L", "-m", "300", "--fail", "-o", path, url], check=True)
    if os.path.getsize(path) < 1000:
        raise SystemExit("%s came back almost empty; not touching the shipped data" % url)


def main():
    work = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.environ.get("TEMP", "."), "zephyrus_threat_feeds")
    os.makedirs(work, exist_ok=True)
    os.makedirs(DATA, exist_ok=True)
    for url, name in FEEDS + [PSL, MAJESTIC]:
        fetch(url, os.path.join(work, name))
    maker = os.path.join(HERE, "make_threat_blob.py")
    subprocess.run([sys.executable, maker, "threats", os.path.join(DATA, "zephyrus_threats.bin")] +
                   [os.path.join(work, n) for _, n in FEEDS], check=True, cwd=HERE)
    subprocess.run([sys.executable, maker, "popular", os.path.join(DATA, "zephyrus_popular.bin"),
                    os.path.join(work, "majestic_million.csv"), os.path.join(work, "psl.dat"), "100000"],
                   check=True, cwd=HERE)
    for name in ("zephyrus_threats.bin", "zephyrus_popular.bin"):
        size = os.path.getsize(os.path.join(DATA, name))
        print("%-24s %.2f MB" % (name, size / 1e6))
        if size < 500_000:
            raise SystemExit(name + " is suspiciously small")


if __name__ == "__main__":
    main()
