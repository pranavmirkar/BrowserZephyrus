#!/usr/bin/env python3
"""Find phishing BEFORE anyone reports it, from free public data, and measure how late the
big lists are.

  sb_ct_discovery.py discover <out.json> [entries per log]
      Reads the newest certificates from public Certificate Transparency logs and keeps the
      hostnames that borrow a well-known brand's name without being that brand's site.
  sb_ct_discovery.py probe <in.json> <out.json> <brave Safe Browsing dir>
      For each candidate: does it resolve, what does its page look like (a password or other
      credential field? which brand does it name?), and do Google's lists (as Brave holds them)
      or the free filtering resolvers already flag it?
  sb_ct_discovery.py recheck <probed.json> <brave Safe Browsing dir>
      Later: the same candidates against the lists again, to see how long the lists took.

Everything here is read-only and free: public logs over HTTPS, public DNS, plain page GETs
that are parsed and never run.
"""
import base64
import concurrent.futures
import datetime
import json
import re
import struct
import subprocess
import time
import sys

import sb_coverage as sb
import sb_free_signals as fs

LOGS = [
    "https://ct.cloudflare.com/logs/nimbus2026/",
    "https://elephant2026h2.ct.sectigo.com/",
]

BRANDS = (
    "paypal microsoft office365 outlook apple icloud google gmail amazon netflix facebook instagram "
    "whatsapp linkedin dhl fedex usps chase wellsfargo bankofamerica citibank hsbc barclays santander "
    "coinbase binance metamask trustwallet docusign adobe dropbox steam spotify allegro bradesco itau "
    "hdfc icici paytm phonepe irctc flipkart aadhaar airtel yahoo twitter telegram ebay walmart "
    "att verizon comcast dpd royalmail"
).split()


def curl_json(url, timeout=25):
    out = subprocess.run(["curl", "-s", "-m", str(timeout), url], capture_output=True).stdout
    return json.loads(out.decode("utf-8", "replace"))


def parse_names(entry):
    """DNS names and issuance time of a CT entry, or None."""
    from cryptography import x509
    leaf = base64.b64decode(entry["leaf_input"])
    extra = base64.b64decode(entry["extra_data"])
    if len(leaf) < 15:
        return None
    entry_type = struct.unpack(">H", leaf[10:12])[0]
    try:
        if entry_type == 0:
            n = int.from_bytes(leaf[12:15], "big")
            der = leaf[15:15 + n]
        else:
            n = int.from_bytes(extra[0:3], "big")
            der = extra[3:3 + n]
        cert = x509.load_der_x509_certificate(der)
    except Exception:
        return None
    names = set()
    try:
        san = cert.extensions.get_extension_for_class(x509.SubjectAlternativeName).value
        names.update(san.get_values_for_type(x509.DNSName))
    except Exception:
        pass
    return {"names": sorted(n.lower().lstrip("*.") for n in names),
            "not_before": cert.not_valid_before_utc.isoformat()}


def discover(out, per_log):
    from sb_heuristics import Psl
    psl = Psl("D:/zeph_perf/sb/psl.dat")
    popular = {}
    for line in open("D:/zeph_perf/sb/top-1m.csv", encoding="utf-8"):
        rank, dom = line.strip().split(",", 1)
        popular[dom] = int(rank)
    top100k = {d for d, r in popular.items() if r <= 100000}
    legit = {k: {d for d in top100k if k in d.split(".")[0]} for k in BRANDS}

    def lookalike(host):
        reg = psl.reg_domain(host)
        if reg in top100k:
            return None
        tokens = re.split(r"[.\-_]|(?<=[a-z])(?=\d)|(?<=\d)(?=[a-z])", host)
        for k in BRANDS:
            if reg in legit[k]:
                continue
            for tok in tokens:
                if tok == k or (len(k) >= 6 and k in tok):
                    return k
        return None

    cands, seen_reg, total = {}, set(), 0
    for base in LOGS:
        try:
            first = curl_json(base + "ct/v1/get-sth")
            time.sleep(12)
            second = curl_json(base + "ct/v1/get-sth")
        except Exception as e:
            print("skip", base, e)
            continue
        tree = second["tree_size"]
        rate = max(1.0, (second["tree_size"] - first["tree_size"]) /
                   max(1.0, (second["timestamp"] - first["timestamp"]) / 1000.0))
        print("%s: %.0f entries/s" % (base, rate), flush=True)
        got = 0
        # windows of per_log entries, ending 5 min, 1 h, 3 h and 6 h ago
        for back_s in (300, 3600, 3 * 3600, 6 * 3600):
            pos = max(0, tree - int(rate * back_s) - per_log)
            stop = pos + per_log
            while pos < stop:
                try:
                    j = curl_json("%sct/v1/get-entries?start=%d&end=%d" % (base, pos, min(pos + 1023, stop - 1)))
                except Exception:
                    break
                entries = j.get("entries", [])
                if not entries:
                    break
                for e in entries:
                    p = parse_names(e)
                    if not p:
                        continue
                    total += 1
                    for name in p["names"]:
                        k = lookalike(name)
                        if k:
                            reg = psl.reg_domain(name)
                            if reg not in seen_reg:
                                seen_reg.add(reg)
                                cands[reg] = {"host": name, "brand": k, "not_before": p["not_before"],
                                              "log": base, "window_hours_ago": back_s / 3600.0}
                pos += len(entries)
                got += len(entries)
        print("%s: read %d entries" % (base, got), flush=True)
    print("parsed %d certificates -> %d lookalike candidate registrable domains" % (total, len(cands)))
    json.dump({"found_at": datetime.datetime.now(datetime.timezone.utc).isoformat(),
               "candidates": list(cands.values())}, open(out, "w"), indent=1)


def probe_one(c):
    host = c["host"]
    _, alive = fs.blocked_by("9.9.9.9", "9.9.9.10", host)
    q9, _ = fs.blocked_by("9.9.9.9", "9.9.9.10", host)
    cf, _ = fs.blocked_by("1.1.1.2", "1.1.1.1", host)
    page = fs.fetch_page("https://" + host + "/") if alive else {}
    if alive and not page.get("fetched"):
        page = fs.fetch_page("http://" + host + "/")
    return {**c, "alive": bool(alive), "quad9": q9, "cf": cf, **page}


def probe(inp, out, sbdir):
    data = json.load(open(inp))
    prefixes, _ = sb.load_brave(sbdir)
    rows = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=24) as ex:
        for r in ex.map(probe_one, data["candidates"]):
            r["brave"] = sb.brave_listed(prefixes, [r["host"] + "/"])[0]
            rows.append(r)
    for name in ("quad9", "cf"):
        for r in rows:
            if isinstance(r.get(name), str):
                r[name] = r[name] in ("nx", "zero")
    json.dump({**data, "probed_at": datetime.datetime.now(datetime.timezone.utc).isoformat(),
               "rows": rows}, open(out, "w"), indent=1)
    report(rows)


def report(rows):
    alive = [r for r in rows if r["alive"]]
    html = [r for r in alive if r.get("html")]
    trap = [r for r in html if r.get("password")]
    cred = [r for r in html if r.get("login")]
    print("\ncandidates %d | resolve %d | HTML page %d | password field %d | any credential field %d" %
          (len(rows), len(alive), len(html), len(trap), len(cred)))
    for label, sel in [("password-field pages", trap), ("credential-field pages", cred)]:
        if not sel:
            continue
        n = len(sel)
        b = sum(1 for r in sel if r["brave"])
        q = sum(1 for r in sel if r["quad9"])
        c = sum(1 for r in sel if r["cf"])
        either = sum(1 for r in sel if r["brave"] or r["quad9"] or r["cf"])
        print("  %-24s %4d: Google lists %2d (%4.1f%%) | Quad9 %2d | Cloudflare %2d | any of the three %2d (%4.1f%%) | NONE %d" %
              (label, n, b, 100.0 * b / n, q, c, either, 100.0 * either / n, n - either))
    print("  examples of password pages nobody lists yet:")
    for r in trap:
        if not (r["brave"] or r["quad9"] or r["cf"]):
            print("    %-46s brand=%-10s title=%s" % (r["host"][:46], r["brand"], r.get("title", "")[:40]))
            if sum(1 for _ in trap) and False:
                break


def recheck(inp, sbdir):
    data = json.load(open(inp))
    prefixes, _ = sb.load_brave(sbdir)
    rows = data["rows"]
    for r in rows:
        r["brave_later"] = sb.brave_listed(prefixes, [r["host"] + "/"])[0]
        _, _ = None, None
        r["quad9_later"] = (fs.blocked_by("9.9.9.9", "9.9.9.10", r["host"])[0] in ("nx", "zero"))
        r["cf_later"] = (fs.blocked_by("1.1.1.2", "1.1.1.1", r["host"])[0] in ("nx", "zero"))
    trap = [r for r in rows if r.get("password")]
    n = len(trap)
    print("password-field pages %d. Listed at first look -> now:" % n)
    for name in ("brave", "quad9", "cf"):
        print("  %-6s %3d -> %3d" % (name, sum(1 for r in trap if r[name]), sum(1 for r in trap if r[name + "_later"])))
    print("  any of the three: %d -> %d" % (
        sum(1 for r in trap if r["brave"] or r["quad9"] or r["cf"]),
        sum(1 for r in trap if r["brave_later"] or r["quad9_later"] or r["cf_later"])))


if __name__ == "__main__":
    cmd = sys.argv[1]
    if cmd == "discover":
        discover(sys.argv[2], int(sys.argv[3]) if len(sys.argv) > 3 else 60000)
    elif cmd == "probe":
        probe(sys.argv[2], sys.argv[3], sys.argv[4])
    elif cmd == "recheck":
        recheck(sys.argv[2], sys.argv[3])
