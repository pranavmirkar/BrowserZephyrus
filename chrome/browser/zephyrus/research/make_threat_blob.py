#!/usr/bin/env python3
"""Builds the two blobs the threat check reads (layout: zephyrus_threat_index.h).

  make_threat_blob.py threats <out.bin> <feed file>...
      Known-bad addresses. A line that is a URL becomes its exact expression
      (host/path?query); a bare host becomes "host/" (which matches the host and every
      subdomain). Feeds: Phishing.Database links and domains (MIT), the Blocklist Project
      phishing list. Check each feed's licence before adding one (THIRD_PARTY_DATA.md).
  make_threat_blob.py popular <out.bin> <majestic_million.csv> <public_suffix_list.dat> [count]
      The popular sites Shield DNS never asks about (default: Majestic Million top 100,000, by
      GlobalRank), as "domain/". Majestic Million is CC BY 3.0 (commercial use allowed with
      attribution; see THIRD_PARTY_DATA.md). Tranco is NOT used for anything shipped: it
      aggregates sources that include a non-commercial one.
      Domains that are public suffixes in the PSL's PRIVATE section (blogspot.com, github.io,
      vercel.app, ...) and a few other user-content hosts are left out: a page there is
      whoever's page it is, so the host name says nothing about it.

The hash key is random per build (os.urandom), so nobody can grind a URL that collides with a
site ahead of time. SipHash-2-4 and the Safe Browsing expression rules are the same as the
browser's; the SipHash test vector is checked every run, and ZephyrusThreatIndexTest checks the
C++ side against the same vector.
"""
import os
import struct
import sys

import sb_coverage as sb

MAGIC = 0x3149545A  # the bytes "ZTI1"
VERSION = 1
MASK64 = (1 << 64) - 1


def rotl(x, b):
    return ((x << b) | (x >> (64 - b))) & MASK64


def siphash24(key: bytes, data: bytes) -> int:
    k0, k1 = struct.unpack("<QQ", key)
    v0 = 0x736F6D6570736575 ^ k0
    v1 = 0x646F72616E646F6D ^ k1
    v2 = 0x6C7967656E657261 ^ k0
    v3 = 0x7465646279746573 ^ k1

    def rnd():
        nonlocal v0, v1, v2, v3
        v0 = (v0 + v1) & MASK64
        v1 = rotl(v1, 13)
        v1 ^= v0
        v0 = rotl(v0, 32)
        v2 = (v2 + v3) & MASK64
        v3 = rotl(v3, 16)
        v3 ^= v2
        v0 = (v0 + v3) & MASK64
        v3 = rotl(v3, 21)
        v3 ^= v0
        v2 = (v2 + v1) & MASK64
        v1 = rotl(v1, 17)
        v1 ^= v2
        v2 = rotl(v2, 32)

    n = len(data)
    i = 0
    while i + 8 <= n:
        m = struct.unpack_from("<Q", data, i)[0]
        v3 ^= m
        rnd()
        rnd()
        v0 ^= m
        i += 8
    last = (n & 0xFF) << 56
    for j in range(n - i):
        last |= data[i + j] << (8 * j)
    v3 ^= last
    rnd()
    rnd()
    v0 ^= last
    v2 ^= 0xFF
    for _ in range(4):
        rnd()
    return (v0 ^ v1 ^ v2 ^ v3) & MASK64


# The reference vector from the SipHash paper; the C++ test asserts the same one.
assert siphash24(bytes(range(16)), bytes(range(15))) == 0xA129CA6149BE45E5
assert siphash24(bytes(range(16)), b"") == 0x726FDB47DD0E0E31


def build_blob(expressions, key=None) -> bytes:
    key = key or os.urandom(16)
    keys = sorted({siphash24(key, e.encode("utf-8", "replace")) & 0xFFFFFFFFFFFF for e in expressions})
    count = len(keys)
    directory = []
    nxt = 0
    for b in range(65537):
        while nxt < count and (keys[nxt] >> 32) < b:
            nxt += 1
        directory.append(nxt)
    out = bytearray()
    out += struct.pack("<II", MAGIC, VERSION) + key + struct.pack("<I", count)
    out += struct.pack("<%dI" % 65537, *directory)
    out += struct.pack("<%dI" % count, *[k & 0xFFFFFFFF for k in keys])
    return bytes(out)


def feed_expressions(paths):
    exprs = set()
    for path in paths:
        n_url = n_host = 0
        for line in sb.read_lines(path):
            token = line.split()[-1] if line.split() else ""
            if not token:
                continue
            if "/" in token or "?" in token or token.startswith(("http:", "https:", "ftp:")):
                ex = sb.expressions(token)
                if ex:
                    exprs.add(ex[0])
                    n_url += 1
            else:
                c = sb.canonical(token)
                if c and "." in c[0]:
                    exprs.add(c[0] + "/")
                    n_host += 1
        print("  %-62s %8d URLs %8d hosts" % (os.path.basename(path)[:62], n_url, n_host))
    return exprs


USER_CONTENT_HOSTS = {
    "wordpress.com", "weebly.com", "wixsite.com", "wix.com", "squarespace.com", "tumblr.com",
    "medium.com", "notion.site", "carrd.co", "webflow.io", "000webhostapp.com", "yolasite.com",
    "jimdofree.com", "mystrikingly.com", "godaddysites.com", "site123.me", "blogger.com",
    "livejournal.com", "typepad.com", "over-blog.com", "sites.google.com", "docs.google.com",
    "forms.gle", "bit.ly", "tinyurl.com", "t.co", "goo.gl", "ow.ly", "is.gd", "cutt.ly",
    "rebrand.ly", "linktr.ee", "canva.com", "sharepoint.com", "dropbox.com", "drive.google.com",
}


def private_suffixes(psl_path):
    private = set()
    in_private = False
    for line in open(psl_path, encoding="utf-8"):
        line = line.strip()
        if "===BEGIN PRIVATE DOMAINS===" in line:
            in_private = True
        elif "===END PRIVATE DOMAINS===" in line:
            in_private = False
        elif in_private and line and not line.startswith("//"):
            private.add(line.lstrip("*.!"))
    return private


def popular_expressions(majestic, psl_path, count):
    private = private_suffixes(psl_path)
    out = set()
    skipped = 0
    with open(majestic, encoding="utf-8") as f:
        header = f.readline().strip().split(",")
        rank_col, domain_col = header.index("GlobalRank"), header.index("Domain")
        for line in f:
            cols = line.split(",")
            if int(cols[rank_col]) > count:
                break
            dom = cols[domain_col].strip().lower()
            if not dom or dom in private or dom in USER_CONTENT_HOSTS or any(
                    dom.endswith("." + p) for p in USER_CONTENT_HOSTS):
                skipped += 1
                continue
            out.add(dom + "/")
    print("  popular: %d domains kept, %d user-content hosts left out" % (len(out), skipped))
    return out


def main():
    cmd, out = sys.argv[1], sys.argv[2]
    if cmd == "threats":
        exprs = feed_expressions(sys.argv[3:])
    elif cmd == "popular":
        exprs = popular_expressions(sys.argv[3], sys.argv[4], int(sys.argv[5]) if len(sys.argv) > 5 else 100000)
    else:
        raise SystemExit(__doc__)
    blob = build_blob(exprs)
    with open(out, "wb") as f:
        f.write(blob)
    print("wrote %s: %d expressions, %.2f MB" % (out, len(exprs), len(blob) / 1e6))


if __name__ == "__main__":
    main()
