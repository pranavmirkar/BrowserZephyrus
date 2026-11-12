#!/usr/bin/env python3
"""Which frames load at once and which wait for the reader: the automatic lazy-frame rule.

  lazyframe_check.py [arms=zephyrus,zephyrus_nolazy]

A page on 127.0.0.1 embeds frames from localhost (another origin) and from itself; the server
records which frame URLs were asked for, before the page is scrolled and after. Hidden desktop.
"""
import http.server
import json
import os
import subprocess
import sys
import threading
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import shield_ruler as R  # noqa: E402

PORT = 8795
asked = []
CROSS = "http://localhost:%d" % PORT

PAGE = """<!doctype html><meta charset=utf-8><title>frames</title>
<p>top of the page</p>
<iframe src="CROSS/f?near" width=300 height=150></iframe>
<iframe src="CROSS/f?hidden_none" style="display:none"></iframe>
<iframe src="CROSS/f?tiny" width=1 height=1 style="border:0"></iframe>
<iframe src="CROSS/f?offscreen" width=300 height=150 style="position:absolute;left:-9999px;top:-9999px"></iframe>
<div style="height:9000px"></div>
<iframe src="CROSS/f?far" width=300 height=150></iframe>
<iframe src="CROSS/f?far_eager" loading=eager width=300 height=150></iframe>
<iframe src="/f?far_same_origin" width=300 height=150></iframe>
<p id=end>end</p>
""".replace("CROSS", CROSS)


class Handler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path.startswith("/f?"):
            asked.append(self.path[3:])
            body = b"<!doctype html><p>frame"
        else:
            body = PAGE.encode()
        self.send_response(200)
        self.send_header("Content-Type", "text/html")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, *a):
        pass


EXPECT_AT_LOAD = {"near", "hidden_none", "tiny", "offscreen", "far_eager", "far_same_origin"}


def main():
    arms = (sys.argv[1] if len(sys.argv) > 1 else "zephyrus,zephyrus_nolazy").split(",")
    srv = http.server.ThreadingHTTPServer(("127.0.0.1", PORT), Handler)
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    ok = True
    for name in arms:
        base = "zephyrus" if name.startswith("zephyrus") else name
        arm = dict([a for a in R.ARMS if a["name"] == base][0])
        arm["name"] = name
        arm["port"] += 400
        if name == "zephyrus_nolazy":
            arm["no_features"] = ["ZephyrusAutoLazyFrames"]
        p, _ = R.launch(arm, r"D:\zeph_perf\lazyframe")
        time.sleep(15)
        try:
            c = R.Cdp(arm["port"])
            s = c.the_tab()
            asked.clear()
            c.go("http://127.0.0.1:%d/" % PORT, 15)
            time.sleep(3)
            at_load = set(asked)
            c.call("Runtime.evaluate", s, expression="document.getElementById('end').scrollIntoView()")
            time.sleep(3)
            after = set(asked)
        finally:
            subprocess.run(["taskkill", "/PID", str(p.pid), "/T", "/F"], capture_output=True)
        print("%-16s at load: %s" % (name, sorted(at_load)))
        print("%-16s after scrolling down, newly: %s" % ("", sorted(after - at_load)))
        if name == "zephyrus":
            good = at_load == EXPECT_AT_LOAD and "far" in after
            ok &= good
            print("   %s: the far cross-site frame waits, everything else loads at once" % ("PASS" if good else "FAIL"))
        if name == "zephyrus_nolazy":
            good = "far" in at_load
            ok &= good
            print("   %s: kill switch loads everything at once" % ("PASS" if good else "FAIL"))
    print("ALL PASS" if ok else "FAILED")


if __name__ == "__main__":
    main()
