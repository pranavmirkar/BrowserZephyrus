#!/usr/bin/env python3
"""The Shield ruler: the same pages, loaded cold, in every browser, measured the same way.

  shield_ruler.py [rounds=1] [sites=30] [out dir=D:\\zeph_perf\\ruler]
  shield_ruler.py summary <rows.jsonl> [more.jsonl ...]

Arms: Zephyrus with Shield on, Zephyrus with Shield off, Brave (default shields), uBlock Origin
Lite (default mode, loaded into the Zephyrus engine with Shield off), Chrome with no blocker. All speak the DevTools protocol, so
every number comes from the same instrument:

  load_s      navigation start to the load event
  settle_s    navigation start to the network going quiet (2.5 s without a request), capped at 30 s
  requests    requests the page made
  blocked     requests the blocker kept off the network: refused (net::ERR_BLOCKED_BY_CLIENT)
              or answered locally with a stand-in
  kbytes      bytes that crossed the network
  cpu_s       main-thread task time of the page's renderer (Performance.getMetrics TaskDuration)
  script_s    of which script
  errors      uncaught exceptions on the page
  text        characters of visible text, images that loaded, frames (the breakage signals)

Per site the browsers are visited back to back in a rotating order, so a slow minute on the
network hits each of them equally. Profiles are kept between runs (blockers download their lists
into the profile, and a fresh one would be measured half-armed); the HTTP cache and cookies are
cleared before every page instead.

It runs in the background. Every browser is started on a separate, hidden Windows desktop, so
none of its windows can be seen on, come to the front of, or take the keyboard of the desktop
someone is working on. Each browser keeps ONE tab and navigates it. Between pages the tab visits chrome://version, which forces the
next page into a new renderer process, so cpu_s starts from zero for every page. Occlusion
tracking is switched off in every arm alike so a page nobody is looking at still renders and runs timers.
`focus_taken` in the summary counts the times one of these windows was found in front anyway.

What this is not: a recording. The pages are live, so two loads of one page differ. Read medians
over many sites and several rounds, and never a single row.
"""
import collections
import ctypes
import ctypes.wintypes as wt
import json
import os
import statistics
import subprocess
import sys
import time
import urllib.request

import websocket

sys.path.insert(0, r"D:\zeph_perf")
from cap import seed_profile  # noqa: E402

user32 = ctypes.WinDLL("user32", use_last_error=True)

ZEPH = os.environ.get("ZEPH_EXE", r"D:\chromium156\src\out\Release\chrome.exe")
BRAVE = r"C:\Program Files\BraveSoftware\Brave-Browser\Application\brave.exe"
CHROME = r"C:\Program Files\Google\Chrome\Application\chrome.exe"
UBOL = os.environ.get("UBOL_DIR", r"D:\zeph_perf\ruler\ubol_2026_1103")

SITES = """https://www.cnn.com/ https://www.bbc.com/news https://www.nytimes.com/ https://www.theguardian.com/international
https://www.reuters.com/ https://www.forbes.com/ https://www.buzzfeed.com/ https://www.espn.com/ https://www.imdb.com/
https://www.reddit.com/ https://www.yahoo.com/ https://www.msn.com/ https://timesofindia.indiatimes.com/
https://www.hindustantimes.com/ https://www.ndtv.com/ https://www.cricbuzz.com/ https://www.moneycontrol.com/
https://www.flipkart.com/ https://www.amazon.in/ https://www.ebay.com/ https://www.walmart.com/ https://www.target.com/
https://www.washingtonpost.com/ https://www.foxnews.com/ https://www.dailymail.co.uk/ https://www.indiatoday.in/
https://economictimes.indiatimes.com/ https://www.news18.com/ https://www.thehindu.com/ https://www.zomato.com/""".split()

ARMS = [
    dict(name="zephyrus", exe=ZEPH, port=9411, zeph=True, prefs=None),
    dict(name="zephyrus_off", exe=ZEPH, port=9412, zeph=True,
         prefs={"zephyrus.adblock.enabled": False}),
    dict(name="brave", exe=BRAVE, port=9413, zeph=False, prefs=None),
    # uBlock Origin Lite, in its default mode, on the SAME engine build as
    # Zephyrus with Shield switched off: only the blocker differs. (Branded
    # Chrome no longer accepts an unpacked extension from the command line.)
    dict(name="ubol", exe=ZEPH, port=9415, zeph=True,
         prefs={"zephyrus.adblock.enabled": False}, args=["--load-extension=" + UBOL]),
    dict(name="chrome", exe=CHROME, port=9414, zeph=False, prefs=None),
]
# The arm compared against every other in the summary.
MAIN = os.environ.get("RULER_MAIN", "zephyrus")

# Zephyrus with Chromium's own loading optimisations switched on, to try them
# without a rebuild: RULER_FAST_FEATURES lists them.
FAST_FEATURES = os.environ.get("RULER_FAST_FEATURES", "")
if FAST_FEATURES:
    ARMS.insert(1, dict(name="zephyrus_fast", exe=ZEPH, port=9416, zeph=True, prefs=None,
                        args=["--enable-features=" + FAST_FEATURES]))
# Zephyrus with named features switched OFF, to measure one change on its own:
# RULER_ALT_DISABLE="ZephyrusAutoLazyFrames" adds the arm "zephyrus_alt".
ALT_DISABLE = os.environ.get("RULER_ALT_DISABLE", "")
if ALT_DISABLE:
    ARMS.insert(1, dict(name="zephyrus_alt", exe=ZEPH, port=9417, zeph=True, prefs=None,
                        no_features=ALT_DISABLE.split(",")))
if os.environ.get("RULER_ARMS"):
    ARMS = [a for a in ARMS if a["name"] in os.environ["RULER_ARMS"].split(",")]

# The blocker in each arm, and the same engine without one.
BLOCKERS = (("zephyrus", "zephyrus_off"), ("zephyrus_fast", "zephyrus_off"), ("zephyrus_alt", "zephyrus_off"), ("brave", "chrome"),
            ("ubol", "zephyrus_off"))

NEUTRAL = "chrome://version/"

PROBE = """(async () => {
  const imgs = [...document.images];
  const nav = performance.getEntriesByType('navigation')[0] || {};
  const fcp = (performance.getEntriesByName('first-contentful-paint')[0] || {}).startTime;
  // Largest contentful paint is only given to an observer; buffered entries arrive at once.
  const lcp = await new Promise((res) => {
    let last = null;
    try {
      new PerformanceObserver((list) => { for (const e of list.getEntries()) last = e.startTime; })
          .observe({type: 'largest-contentful-paint', buffered: true});
    } catch (e) {}
    setTimeout(() => res(last), 300);
  });
  return JSON.stringify({
    ttfb_ms: nav.responseStart || null,
    fcp_ms: fcp || null,
    lcp_ms: lcp,
    text: (document.body ? document.body.innerText : '').length,
    images: imgs.filter(i => i.complete && i.naturalWidth > 1).length,
    frames: document.querySelectorAll('iframe').length,
    height: document.documentElement ? document.documentElement.scrollHeight : 0,
    visible: document.visibilityState,
  });
})()"""


# ---- a desktop of their own ----
#
# Every browser is started on a separate Windows desktop. A window there cannot
# be seen on the user's desktop, cannot come to the front of it and cannot take
# its keyboard, whatever the browser does; and to the browser it is an ordinary
# shown window, so pages render and run as usual.

kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
DESKTOP = "zephyrus_ruler"
WIN_W, WIN_H = 1366, 900


class STARTUPINFOW(ctypes.Structure):
    _fields_ = [("cb", wt.DWORD), ("lpReserved", wt.LPWSTR), ("lpDesktop", wt.LPWSTR),
                ("lpTitle", wt.LPWSTR), ("dwX", wt.DWORD), ("dwY", wt.DWORD),
                ("dwXSize", wt.DWORD), ("dwYSize", wt.DWORD), ("dwXCountChars", wt.DWORD),
                ("dwYCountChars", wt.DWORD), ("dwFillAttribute", wt.DWORD), ("dwFlags", wt.DWORD),
                ("wShowWindow", wt.WORD), ("cbReserved2", wt.WORD), ("lpReserved2", ctypes.c_void_p),
                ("hStdInput", wt.HANDLE), ("hStdOutput", wt.HANDLE), ("hStdError", wt.HANDLE)]


class PROCESS_INFORMATION(ctypes.Structure):
    _fields_ = [("hProcess", wt.HANDLE), ("hThread", wt.HANDLE),
                ("dwProcessId", wt.DWORD), ("dwThreadId", wt.DWORD)]


class Proc:
    def __init__(self, pid):
        self.pid = pid


_desktop_handle = None


def the_desktop():
    global _desktop_handle
    if _desktop_handle is None:
        user32.CreateDesktopW.restype = wt.HANDLE
        user32.CreateDesktopW.argtypes = [wt.LPCWSTR, wt.LPCWSTR, ctypes.c_void_p, wt.DWORD,
                                          wt.DWORD, ctypes.c_void_p]
        _desktop_handle = user32.CreateDesktopW(DESKTOP, None, None, 0, 0x10000000, None)  # GENERIC_ALL
        if not _desktop_handle:
            raise OSError("could not create the hidden desktop: %d" % ctypes.get_last_error())
    return _desktop_handle


def start_hidden(cmd):
    the_desktop()
    si = STARTUPINFOW()
    si.cb = ctypes.sizeof(si)
    si.lpDesktop = "WinSta0\\" + DESKTOP
    pi = PROCESS_INFORMATION()
    line = ctypes.create_unicode_buffer(subprocess.list2cmdline(cmd))
    kernel32.CreateProcessW.argtypes = [wt.LPCWSTR, wt.LPWSTR, ctypes.c_void_p, ctypes.c_void_p, wt.BOOL,
                                        wt.DWORD, ctypes.c_void_p, wt.LPCWSTR, ctypes.c_void_p, ctypes.c_void_p]
    if not kernel32.CreateProcessW(None, line, None, None, False, 0, None, None,
                                   ctypes.byref(si), ctypes.byref(pi)):
        raise OSError("could not start %s: %d" % (cmd[0], ctypes.get_last_error()))
    kernel32.CloseHandle(pi.hThread)
    kernel32.CloseHandle(pi.hProcess)
    return Proc(pi.dwProcessId)


def park(pid):
    """Nothing to do: the windows are on their own desktop."""


def foreground_pid():
    q = wt.DWORD()
    user32.GetWindowThreadProcessId(user32.GetForegroundWindow(), ctypes.byref(q))
    return q.value


def launch(arm, work):
    prof = os.path.join(work, "prof_" + arm["name"])
    first = not os.path.exists(os.path.join(prof, "Default", "Preferences"))
    if first and arm["zeph"]:
        seed_profile(prof, workspaces=False, bookmarks=False, extra_prefs=arm["prefs"])
    os.makedirs(prof, exist_ok=True)
    no_features = ["CalculateNativeWinOcclusion"] + arm.get("no_features", [])
    cmd = [arm["exe"], f"--user-data-dir={prof}", f"--remote-debugging-port={arm['port']}",
           "--no-first-run", "--no-default-browser-check",
           "--window-position=0,0", f"--window-size={WIN_W},{WIN_H}",
           "--disable-session-crashed-bubble", "--hide-crash-restore-bubble",
           "--disable-backgrounding-occluded-windows", "--disable-renderer-backgrounding",
           "--disable-background-timer-throttling",
           "--disable-features=" + ",".join(no_features)] + arm.get("args", []) + [NEUTRAL]
    return start_hidden(cmd), first


# ---- the instrument ----

class Cdp:
    def __init__(self, port):
        info = json.load(urllib.request.urlopen(f"http://127.0.0.1:{port}/json/version", timeout=5))
        self.ws = websocket.create_connection(info["webSocketDebuggerUrl"], timeout=1,
                                              suppress_origin=True)
        self.nid = 0
        self.events = []
        self.session = None

    def send(self, method, session=None, **params):
        self.nid += 1
        msg = {"id": self.nid, "method": method, "params": params}
        if session:
            msg["sessionId"] = session
        self.ws.send(json.dumps(msg))
        return self.nid

    def read(self):
        try:
            return json.loads(self.ws.recv())
        except websocket.WebSocketTimeoutException:
            return None

    def call(self, method, session=None, wait=10, **params):
        """Sends and waits for the reply; events that arrive meanwhile are kept."""
        want = self.send(method, session, **params)
        end = time.time() + wait
        while time.time() < end:
            msg = self.read()
            if msg is None:
                continue
            if msg.get("id") == want:
                return msg.get("result", {})
            if "method" in msg:
                self.events.append(msg)
        return {}

    def the_tab(self):
        """Attaches to the one tab this browser keeps; never opens another."""
        if self.session:
            return self.session
        pages = [t for t in self.call("Target.getTargets").get("targetInfos", [])
                 if t.get("type") == "page" and not t.get("url", "").startswith("chrome-extension:")]
        if pages:
            target = pages[0]["targetId"]
            # A profile kept between runs brings its old tabs back. Keep one,
            # in front, so the page being measured is the page being shown.
            for extra in pages[1:]:
                self.call("Target.closeTarget", targetId=extra["targetId"])
        else:
            target = self.call("Target.createTarget", url=NEUTRAL).get("targetId")
            if not target:
                return None
        self.call("Target.activateTarget", targetId=target)
        self.session = self.call("Target.attachToTarget", targetId=target,
                                 flatten=True).get("sessionId")
        for domain in ("Page", "Network", "Runtime", "Performance"):
            self.call(domain + ".enable", self.session)
        return self.session

    def go(self, url, wait):
        """Navigates the tab and waits for its load event (or `wait` seconds)."""
        self.events.clear()
        self.send("Page.navigate", self.session, url=url)
        end = time.time() + wait
        while time.time() < end:
            msg = self.read()
            if msg and msg.get("sessionId") == self.session and msg.get("method") == "Page.loadEventFired":
                return True
        return False


def measure(cdp, url, pid=None):
    session = cdp.the_tab()
    if not session:
        return None
    # A page of the browser's own: the next site cannot share its process.
    cdp.go(NEUTRAL, 8)
    # A hidden page is throttled and may never finish; make sure this one is shown.
    for _ in range(6):
        seen = cdp.call("Runtime.evaluate", session, expression="document.visibilityState",
                        returnByValue=True).get("result", {}).get("value")
        if seen == "visible" or pid is None:
            break
        park(pid)
        time.sleep(1)
    cdp.call("Network.clearBrowserCache", session, wait=20)
    cdp.call("Network.clearBrowserCookies", session, wait=20)
    cdp.events.clear()

    sent, failed = {}, {}
    row = dict(url=url, load_s=None, dcl_s=None, kbytes=0.0, errors=0, answered=0)
    t0 = time.time()
    cdp.send("Page.navigate", session, url=url)
    last_request = t0

    def take(msg):
        nonlocal last_request
        if msg.get("sessionId") != session:
            return
        m, p = msg.get("method"), msg.get("params", {})
        if m == "Network.requestWillBeSent":
            sent[p["requestId"]] = p["request"]["url"].split("?")[0][:120]
            last_request = time.time()
        elif m == "Network.loadingFinished":
            row["kbytes"] += p.get("encodedDataLength", 0) / 1000.0
        elif m == "Network.loadingFailed":
            failed[p["requestId"]] = p.get("errorText", "")
        elif m == "Network.responseReceived":
            # An answer that came from no server, no cache and no service worker
            # was made up by the browser: a blocker's stand-in for what it blocked.
            r = p.get("response", {})
            if (r.get("url", "").startswith("http") and not r.get("remoteIPAddress")
                    and not r.get("fromDiskCache") and not r.get("fromServiceWorker")
                    and not r.get("fromPrefetchCache")):
                row["answered"] += 1
        elif m == "Page.loadEventFired" and row["load_s"] is None:
            row["load_s"] = round(time.time() - t0, 2)
        elif m == "Page.domContentEventFired" and row["dcl_s"] is None:
            row["dcl_s"] = round(time.time() - t0, 2)
        elif m == "Runtime.exceptionThrown":
            row["errors"] += 1

    quiet = False
    while True:
        now = time.time()
        if now - t0 > 30:
            break
        if row["load_s"] is not None and now - last_request > 2.5:
            quiet = True
            break
        msg = cdp.read()
        if msg and "method" in msg:
            take(msg)
    row["settle_s"] = round(time.time() - t0 - (2.5 if quiet else 0), 2)

    perf = {m["name"]: m["value"] for m in cdp.call("Performance.getMetrics", session).get("metrics", [])}
    for msg in cdp.events:
        take(msg)
    cdp.events.clear()
    row["cpu_s"] = round(perf.get("TaskDuration", 0), 2)
    row["script_s"] = round(perf.get("ScriptDuration", 0), 2)
    row["heap_mb"] = round(perf.get("JSHeapUsedSize", 0) / 1e6, 1)
    probe = cdp.call("Runtime.evaluate", session, expression=PROBE, returnByValue=True,
                     awaitPromise=True)
    try:
        row.update(json.loads(probe["result"]["value"]))
    except (KeyError, TypeError, ValueError):
        row.update(text=None, images=None, frames=None, height=None)
    row["requests"] = len(sent)
    blocked = collections.Counter(sent.get(rid, "?") for rid, e in failed.items() if "BLOCKED_BY_CLIENT" in e)
    # Refused outright, plus answered with a stand-in instead of being sent.
    row["refused"] = sum(blocked.values())
    row["blocked"] = row["refused"] + row["answered"]
    # A page that asks for one blocked address over and over is a retry storm: it
    # burns CPU for nothing, and it is the blocker's problem to solve.
    if blocked:
        url_most, most = blocked.most_common(1)[0]
        row["most_repeated_blocked"] = [most, url_most]
    row["kbytes"] = round(row["kbytes"], 1)
    return row


# ---- reading the rows ----

def med(rows, key):
    vals = [r[key] for r in rows if r.get(key) is not None]
    return statistics.median(vals) if vals else float("nan")


def sign_p(wins, n):
    """Two-sided sign test: how likely a split this uneven is if neither side is better."""
    if n == 0:
        return 1.0
    k = max(wins, n - wins)
    tail = sum(math_comb(n, i) for i in range(k, n + 1)) / 2.0 ** n
    return min(1.0, 2 * tail)


def math_comb(n, k):
    import math
    return math.comb(n, k)


def summarise(rows):
    names = [n for n in dict.fromkeys(r["arm"] for r in rows)]
    sites = len({r["url"] for r in rows})
    rounds = len({(r.get("file", ""), r.get("round", 0)) for r in rows})
    print("\nmedian over %d sites, %d round(s)" % (sites, rounds))
    cols = ["fcp_ms", "lcp_ms", "load_s", "settle_s", "cpu_s", "kbytes", "requests", "blocked", "errors", "text"]
    print("%-13s" % "arm" + "".join("%10s" % c for c in cols) + "   failed")
    for n in names:
        mine = [r for r in rows if r["arm"] == n]
        ok = [r for r in mine if r.get("load_s") is not None]
        print("%-13s" % n + "".join("%10.1f" % med(ok, c) for c in cols) + "   %d/%d" % (len(mine) - len(ok), len(mine)))

    by = {}
    for r in rows:
        by.setdefault((r["url"], r.get("file", ""), r.get("round", 0)), {})[r["arm"]] = r

    def paired(a, b, key):
        """Loads where both finished: how often a is lower than b, and the median ratio a/b."""
        pairs = [(v[a][key], v[b][key]) for v in by.values()
                 if a in v and b in v and v[a].get("load_s") is not None and v[b].get("load_s") is not None
                 and v[a].get(key) and v[b].get(key)]
        if not pairs:
            return "no pairs"
        wins = sum(1 for x, y in pairs if x < y)
        ties = sum(1 for x, y in pairs if x == y)
        n = len(pairs) - ties
        return "lower on %d of %d (p=%.3f), median ratio %.2f" % (
            wins, n, sign_p(wins, n), statistics.median(x / y for x, y in pairs))

    for rival in [n for n in ("zephyrus", "zephyrus_alt", "brave", "ubol", "chrome") if n != MAIN]:
        if rival not in names or MAIN not in names:
            continue
        print("\n%s against %s (paired by site and round)" % (MAIN, rival))
        for key in ("fcp_ms", "lcp_ms", "load_s", "settle_s", "cpu_s", "kbytes", "requests"):
            print("  %-9s %s" % (key, paired(MAIN, rival, key)))

    # Breakage canary, crude on purpose: a page that lost most of its text or
    # stopped loading when the blocker was on, or gained uncaught errors.
    print("\nbreakage suspects (blocker on against the same engine with it off)")
    for on, off in BLOCKERS:
        if on not in names or off not in names:
            continue
        suspects = collections.Counter()
        loads = 0
        for (url, _, _), v in by.items():
            if on not in v or off not in v:
                continue
            a, b = v[on], v[off]
            if b.get("load_s") is None or not b.get("text"):
                continue
            loads += 1
            why = []
            if a.get("load_s") is None:
                why.append("did not finish loading")
            # A page that is blank in the other browsers too is a bot wall or an
            # outage, not something this blocker did.
            others = [r.get("text") or 0 for n, r in v.items() if n != on]
            elsewhere = statistics.median(others) if others else 0
            if a.get("text") is not None and a["text"] < 0.5 * b["text"] and elsewhere >= 0.5 * b["text"]:
                why.append("text %d -> %d" % (b["text"], a["text"]))
            if a.get("errors", 0) >= b.get("errors", 0) + 3:
                why.append("errors +%d" % (a.get("errors", 0) - b.get("errors", 0)))
            for w in why:
                suspects[(url, w.split(" ")[0])] += 1
        flagged = len({u for u, _ in suspects})
        print("  %-12s %d of %d sites flagged in %d loads" % (on, flagged, sites, loads))
        for (url, kind), n in sorted(suspects.items(), key=lambda kv: -kv[1]):
            print("     %dx %-6s %s" % (n, kind, url))

    storms = [r for r in rows if (r.get("most_repeated_blocked") or [0])[0] >= 50]
    print("\nretry storms (one blocked address asked for 50+ times): %d" % len(storms))
    for r in storms:
        print("     %s %s x%d %s" % (r["arm"], r["url"], r["most_repeated_blocked"][0], r["most_repeated_blocked"][1]))
    hidden = sum(1 for r in rows if r.get("visible") not in (None, "visible"))
    print("pages measured while not visible: %d   focus_taken: %d" % (hidden, sum(r.get("focus_taken", 0) for r in rows)))


def main():
    rounds = int(sys.argv[1]) if len(sys.argv) > 1 else 1
    count = int(sys.argv[2]) if len(sys.argv) > 2 else len(SITES)
    work = sys.argv[3] if len(sys.argv) > 3 else r"D:\zeph_perf\ruler"
    os.makedirs(work, exist_ok=True)
    out = os.path.join(work, "ruler_%s.jsonl" % time.strftime("%Y%m%d_%H%M%S"))
    procs, cdps, fresh = {}, {}, False
    try:
        for arm in ARMS:
            procs[arm["name"]], first = launch(arm, work)
            fresh = fresh or first
        ours = {p.pid for p in procs.values()}
        # A profile made a moment ago has not fetched its lists yet.
        end = time.time() + (150 if fresh else 25)
        while time.time() < end:
            for p in procs.values():
                park(p.pid)
            time.sleep(2)
        for arm in ARMS:
            cdps[arm["name"]] = Cdp(arm["port"])
        rows = []
        with open(out, "w", encoding="utf-8") as f:
            for rnd in range(rounds):
                for i, url in enumerate(SITES[:count]):
                    order = ARMS[(i + rnd) % len(ARMS):] + ARMS[:(i + rnd) % len(ARMS)]
                    for arm in order:
                        try:
                            row = measure(cdps[arm["name"]], url, procs[arm["name"]].pid)
                        except (websocket.WebSocketException, OSError) as e:
                            row = dict(url=url, load_s=None, error=str(e)[:80])
                        if row is None:
                            row = dict(url=url, load_s=None)
                        row.update(arm=arm["name"], round=rnd)
                        if foreground_pid() in ours:
                            row["focus_taken"] = 1
                        for p in procs.values():
                            park(p.pid)
                        rows.append(row)
                        f.write(json.dumps(row) + "\n")
                        f.flush()
                        print("%-13s %-44s load %-6s cpu %-6s req %-4s blocked %-4s kB %s" % (
                            arm["name"], url[:44], row.get("load_s"), row.get("cpu_s"),
                            row.get("requests"), row.get("blocked"), row.get("kbytes")), flush=True)
        summarise(rows)
        print("\nrows in", out)
    finally:
        for p in procs.values():
            subprocess.run(["taskkill", "/PID", str(p.pid), "/T", "/F"], capture_output=True)


if __name__ == "__main__":
    if len(sys.argv) > 2 and sys.argv[1] == "summary":
        every = []
        for path in sys.argv[2:]:
            for line in open(path, encoding="utf-8"):
                row = json.loads(line)
                row["file"] = os.path.basename(path)
                every.append(row)
        summarise(every)
    else:
        main()
