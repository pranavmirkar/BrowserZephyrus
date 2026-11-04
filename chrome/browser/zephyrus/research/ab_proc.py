"""Live check of procedural cosmetic filters (:has-text, :upward, :xpath, :matches-css ...).

The rules are appended to a profile's downloaded list for a made-up host that this run maps to a local
server. The page reports, after the document and a late insertion have settled, which of its elements
are hidden, removed or restyled. Three runs:
  * default            -> every case with a rule behaves as expected, every decoy is untouched;
  * kill switch        -> --disable-features=ZephyrusAdblockProceduralCosmetic: nothing is hidden;
  * exception          -> `#@?#` on the same host turns one rule off.
ZEPH_EXE selects the browser (default: out/Check).
"""
import http.server, json, os, shutil, sys, threading, time

sys.path.insert(0, r"D:\zeph_perf")
from cap import Browser, seed_profile

PORT = 8913
HOST = "proc.zephtest.com"

RULES = [
    "div.card:has-text(Sponsored)",
    "div.card:has-text(/^Promoted/)",
    "span.tag:upward(2)",
    "span.tag:upward(.wrap)",
    "p:xpath(following-sibling::aside)",
    "div.m:matches-css(display: flex)",
    "div:matches-attr(\"data-ad\"=\"1\")",
    "div:matches-attr(/^data-slot/)",
    "li.row:not(:has-text(keep))",
    "section.s:has(:has-text(trap))",
    "div.t:min-text-length(30)",
    "div.rm:has-text(gone):remove()",
    "div.cmb:has-text(top) > span",
    "div:has-text(/(a+)+$/)",
    ":xpath(//aside[@id=\"x2\"])",
    "div#rc:remove-class(/^ad-/)",
]
EXCEPT = "div.card:has-text(Sponsored)"
HIDDEN = ["c1", "c2", "u_outer", "u_wrap", "x_aside", "m_flex", "a_yes", "a_slot", "n1", "h_s1",
          "t_long", "cmb_span", "x2", "c_dyn"]
REMOVED = ["rm1"]
DECOYS = ["c_keep", "c2_keep", "m_keep", "a_no", "n2", "h_s2", "t_short", "cmb_keep", "rm_keep", "redos"]

PAGE = """<!doctype html><meta charset=utf-8><title>proc</title>
<style>.m{display:block}.m.flex{display:flex}</style><body>
<div class=card id=c1>Sponsored post here</div><div class=card id=c_keep>Regular post</div>
<div class=card id=c2>Promoted: buy now</div><div class=card id=c2_keep>Not Promoted</div>
<div id=u_outer><div class=mid><span class=tag>x</span></div></div>
<div class=wrap id=u_wrap><div><span class=tag>y</span></div></div>
<div><p>x</p><aside id=x_aside>aside</aside></div>
<div class="m flex" id=m_flex>f</div><div class="m" id=m_keep>k</div>
<div data-ad="1" id=a_yes>ad</div><div data-ad="2" id=a_no>no</div>
<div data-slot-top="x" id=a_slot>slot</div>
<ul><li class=row id=n1>drop</li><li class=row id=n2>keep this</li></ul>
<section class=s id=h_s1><b>trap</b></section><section class=s id=h_s2><b>fine</b></section>
<div class=t id=t_long>0123456789012345678901234567890123456789</div><div class=t id=t_short>short</div>
<div class=rm id=rm1>gone</div><div class=rm id=rm_keep>stays</div>
<div class=cmb>top<span id=cmb_span>s</span></div><div class=cmb>other<span id=cmb_keep>s</span></div>
<div id=redos>aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa!</div>
<aside id=x2>second</aside><div id=rc class="ad-x keep ad-y">rc</div>
<script>
setTimeout(function(){var d=document.createElement("div");d.className="card";d.id="c_dyn";
  d.textContent="Sponsored later";document.body.appendChild(d)},400);
setTimeout(function(){
  var ids=%s, out={};
  ids.forEach(function(i){var e=document.getElementById(i);
    out[i]=e?getComputedStyle(e).display:"REMOVED";});
  out.rc_class=document.getElementById("rc").className;
  fetch("/report",{method:"POST",body:JSON.stringify(out)});
},4000);
</script></body>"""
IDS = HIDDEN + REMOVED + DECOYS


class H(http.server.BaseHTTPRequestHandler):
    report = None

    def do_GET(self):
        body = (PAGE % json.dumps(IDS)).encode()
        self.send_response(200); self.send_header("Content-Type", "text/html")
        self.send_header("Content-Length", str(len(body))); self.end_headers(); self.wfile.write(body)

    def do_POST(self):
        n = int(self.headers.get("Content-Length", 0))
        H.report = json.loads(self.rfile.read(n))
        self.send_response(200); self.send_header("Content-Length", "0"); self.end_headers()

    def log_message(self, *a):
        pass


srv = http.server.ThreadingHTTPServer(("127.0.0.1", PORT), H)
threading.Thread(target=srv.serve_forever, daemon=True).start()


def run(label, extra, extra_rules=()):
    H.report = None
    prof = r"D:\zeph_perf\proc_prof"
    seed_profile(prof, workspaces=False, bookmarks=False)
    dst = os.path.join(prof, "ZephyrusAdBlock")
    shutil.rmtree(dst, ignore_errors=True)
    shutil.copytree(r"D:\zeph_perf\abc_prof_on\ZephyrusAdBlock", dst)
    for f in ("filters.zidx",):
        if os.path.exists(os.path.join(dst, f)):
            os.remove(os.path.join(dst, f))
    with open(os.path.join(dst, "filters.txt"), "a", encoding="utf-8", newline="\n") as f:
        f.write("\n! ===== https://raw.githubusercontent.com/uBlockOrigin/uAssets/master/filters/test.txt =====\n")
        for r in RULES:
            f.write(f"{HOST}#?#{r}\n")
        for r in extra_rules:
            f.write(r + "\n")
    b = Browser(prof, args=["--host-resolver-rules=MAP %s 127.0.0.1" % HOST,
                            "--enable-logging", "--log-file=" + r"D:\zeph_perf\proc.log", "--v=1"] + extra,
                urls=(f"http://{HOST}:{PORT}/",), wait=30)
    deadline = time.time() + 60
    while H.report is None and time.time() < deadline:
        time.sleep(1)
    b.close()
    print("==", label, "report received" if H.report is not None else "NO REPORT")
    return H.report or {}


def judge(rep, expect_hidden, expect_removed, label):
    bad = []
    for i in HIDDEN + REMOVED + DECOYS:
        got = rep.get(i, "missing")
        if i in expect_removed:
            want_ok = got == "REMOVED"
        elif i in expect_hidden:
            want_ok = got == "none"
        else:
            want_ok = got not in ("none", "REMOVED", "missing")
        if not want_ok:
            bad.append((i, got))
    want_cls = "keep" if expect_removed else "ad-x keep ad-y"
    if rep.get("rc_class") != want_cls:
        bad.append(("rc_class", rep.get("rc_class")))
    print("%-34s %s" % (label, "PASS" if not bad else "FAIL %s" % bad))
    return not bad


on = run("default", [])
off = run("kill switch", ["--disable-features=ZephyrusAdblockProceduralCosmetic"])
exc = run("exception", [], [f"{HOST}#@?#{EXCEPT}"])
ok = [
    judge(on, set(HIDDEN), set(REMOVED), "default: rules apply"),
    judge(off, set(), set(), "kill switch: nothing hidden"),
    judge(exc, set(HIDDEN) - {"c1", "c_dyn"}, set(REMOVED), "exception: one rule off"),
]
print("PASS" if all(ok) else "FAIL")
