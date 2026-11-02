#!/usr/bin/env python3
"""Which FREE signals catch fresh phishing that Google's lists (as Brave holds them) miss?

For phishing URLs submitted to PhishTank in the last 3 days (verified) plus OpenPhish's
feed (last hours), per URL:
  brave     its Safe Browsing expressions have a prefix in Brave's local lists
  quad9     Quad9 (9.9.9.9) refuses a host that its unfiltered twin (9.9.9.10) resolves
  cf        Cloudflare's security resolver (1.1.1.2) blocks a host that 1.1.1.1 resolves
  dns0      DNS0.eu Zero (193.110.81.9) against its open resolver (193.110.81.0)
  clean     CleanBrowsing Security (185.228.168.9) against 1.1.1.1
  alive     1.1.1.1 resolves the host at all (a dead host cannot hurt anyone)
  password  the page, fetched with a plain GET, has a password field
  login     a password, e-mail, phone or card-style field

Only hostnames go to the public resolvers (plain DNS, the same answers DNS-over-HTTPS would
give); pages are fetched with curl and only parsed, nothing runs. No account, no key.

Usage: sb_free_signals.py <phishtank csv> <openphish feed> <brave Safe Browsing dir> <out json> [limit]
"""
import concurrent.futures
import csv
import datetime
import json
import random
import re
import socket
import struct
import subprocess
import sys

import sb_coverage as sb

UA = ("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) "
      "Chrome/150.0.0.0 Safari/537.36")


def dns_a(server, name, timeout=3.0):
    """(rcode, [ipv4 strings]) or (None, []) on failure. Plain UDP, one retry."""
    qn = b"".join(bytes([len(p)]) + p.encode("idna") for p in name.split(".") if p) + b"\0"
    for attempt in range(2):
        tid = random.randint(0, 0xFFFF)
        pkt = struct.pack(">HHHHHH", tid, 0x0100, 1, 0, 0, 0) + qn + struct.pack(">HH", 1, 1)
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.settimeout(timeout)
        try:
            s.sendto(pkt, (server, 53))
            d, _ = s.recvfrom(4096)
        except Exception:
            continue
        finally:
            s.close()
        if len(d) < 12 or struct.unpack(">H", d[:2])[0] != tid:
            continue
        rcode = d[3] & 0xF
        an = struct.unpack(">H", d[6:8])[0]
        pos = 12
        # skip the question
        while pos < len(d) and d[pos] != 0:
            pos += d[pos] + 1 if d[pos] < 0xC0 else 2
        pos += 5
        ips = []
        for _ in range(an):
            if pos >= len(d):
                break
            pos += 2 if d[pos] >= 0xC0 else 0
            if pos > len(d) - 10:
                break
            if d[pos - 2] < 0xC0 and d[pos] != 0:
                while pos < len(d) and d[pos] != 0:
                    pos += d[pos] + 1
                pos += 1
            typ, _cls, _ttl, rdlen = struct.unpack(">HHIH", d[pos:pos + 10])
            pos += 10
            if typ == 1 and rdlen == 4:
                ips.append(".".join(str(b) for b in d[pos:pos + 4]))
            pos += rdlen
        return rcode, ips
    return None, []


def blocked_by(filtered, unfiltered, host):
    """(kind, alive). kind is None when not blocked: 'nx' the filter says the name does not exist
    while the open resolver resolves it, 'zero' it answers 0.0.0.0, 'sink' it answers an address
    that differs from the open answer (checked afterwards: a real sinkhole repeats across hosts).
    A server failure or a timeout is never counted as a block."""
    f = dns_a(filtered, host)
    u = dns_a(unfiltered, host)
    if f[0] is None or u[0] is None:
        return None, False
    alive = u[0] == 0 and any(ip != "0.0.0.0" for ip in u[1])
    if not alive or f[0] not in (0, 3):
        return None, alive
    if f[0] == 3:
        return "nx", alive
    if "0.0.0.0" in f[1]:
        return "zero", alive
    if f[1] and not set(f[1]) & set(u[1]):
        return "sink:" + ",".join(sorted(f[1])), alive
    return None, alive


def fetch_page(url):
    try:
        out = subprocess.run(
            ["curl", "-s", "-m", "10", "-L", "-k", "--max-filesize", "1500000", "-A", UA,
             "-H", "Accept: text/html", "-w", "\n__CT__%{content_type}", url],
            capture_output=True, timeout=15).stdout.decode("utf-8", "replace")
    except Exception:
        return {"fetched": False}
    if "__CT__" not in out:
        return {"fetched": False}
    body, ctype = out.rsplit("__CT__", 1)
    if "html" not in ctype.lower() or not body.strip():
        return {"fetched": bool(body.strip()), "html": False}
    low = body.lower()
    return {
        "fetched": True,
        "html": True,
        "password": bool(re.search(r"<input[^>]+type\s*=\s*[\"']?password", low)),
        "login": bool(re.search(r"<input[^>]+type\s*=\s*[\"']?(password|email|tel)", low))
        or bool(re.search(r"(card.?number|cvv|cvc|\botp\b|one.?time)", low)),
        "title": (re.search(r"<title[^>]*>(.*?)</title>", body, re.S | re.I) or [None, ""])[1].strip()[:80],
    }


def one(item):
    url, source = item
    c = sb.canonical(url)
    if not c:
        return None
    host = c[0]
    q9, alive = blocked_by("9.9.9.9", "9.9.9.10", host)
    cf, _ = blocked_by("1.1.1.2", "1.1.1.1", host)
    d0, _ = blocked_by("193.110.81.9", "193.110.81.0", host)
    cb, _ = blocked_by("185.228.168.9", "1.1.1.1", host)
    page = fetch_page(url) if alive else {"fetched": False}
    return {"url": url, "host": host, "source": source, "quad9": q9, "cf": cf, "dns0": d0,
            "clean": cb, "alive": bool(alive), **page}


def main():
    phishtank, openphish, sbdir, out = sys.argv[1:5]
    limit = int(sys.argv[5]) if len(sys.argv) > 5 else 10 ** 9
    random.seed(3)
    now = datetime.datetime.now(datetime.timezone.utc)
    pt = []
    for r in csv.DictReader(open(phishtank, encoding="utf-8", errors="replace")):
        try:
            t = datetime.datetime.fromisoformat(r["submission_time"])
        except ValueError:
            continue
        if (now - t).total_seconds() < 3 * 86400:
            pt.append((r["url"], "phishtank<3d"))
    pt = random.sample(pt, min(len(pt), 250))
    op = [(u, "openphish") for u in sb.read_lines(openphish)]
    items = (pt + op)
    random.shuffle(items)
    items = items[:limit]
    prefixes, _ = sb.load_brave(sbdir)
    brave = dict(zip([u for u, _ in items], sb.brave_listed(prefixes, [u for u, _ in items])))
    print("sample:", len(items), "URLs", flush=True)

    results = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=24) as ex:
        for i, res in enumerate(ex.map(one, items)):
            if res:
                res["brave"] = brave[res["url"]]
                results.append(res)
            if (i + 1) % 50 == 0:
                print("  done", i + 1, flush=True)
    # A "sink" answer counts only if the same address is given for at least three different hosts
    # (a block page's address repeats; a CDN's rotating answers do not).
    for name in ("quad9", "cf", "dns0", "clean"):
        seen = {}
        for r in results:
            k = r.get(name)
            if isinstance(k, str) and k.startswith("sink:"):
                seen.setdefault(k, set()).add(r["host"])
        for r in results:
            k = r.get(name)
            if isinstance(k, str):
                r[name] = k in ("nx", "zero") or len(seen.get(k, ())) >= 3
    json.dump(results, open(out, "w"), indent=0)

    def pct(n, d):
        return "%5.1f%%" % (100.0 * n / max(d, 1))

    for label, rows in [("all", results)] + [(s, [r for r in results if r["source"] == s])
                                            for s in ("phishtank<3d", "openphish")]:
        n = len(rows)
        alive = [r for r in rows if r["alive"]]
        print("\n== %s: %d URLs, %d still resolve (%s)" % (label, n, len(alive), pct(len(alive), n)))
        names = ("brave", "quad9", "cf", "dns0", "clean")
        for name in names:
            hit = sum(1 for r in alive if r.get(name))
            print("  %-6s flags %4d of %4d live  %s" % (name, hit, len(alive), pct(hit, len(alive))))
        dns = lambda r: r.get("quad9") or r.get("cf") or r.get("dns0") or r.get("clean")
        print("  any free DNS filter         %s" % pct(sum(1 for r in alive if dns(r)), len(alive)))
        print("  Brave or any DNS filter     %s" % pct(sum(1 for r in alive if r.get("brave") or dns(r)), len(alive)))
        miss = [r for r in alive if not (r.get("brave") or dns(r))]
        fetched = [r for r in miss if r.get("html")]
        print("  caught by none: %d live; page fetched as HTML for %d" % (len(miss), len(fetched)))
        if fetched:
            pw = sum(1 for r in fetched if r.get("password"))
            lg = sum(1 for r in fetched if r.get("login"))
            print("    of those: password field %s, any credential field %s" %
                  (pct(pw, len(fetched)), pct(lg, len(fetched))))


if __name__ == "__main__":
    main()
