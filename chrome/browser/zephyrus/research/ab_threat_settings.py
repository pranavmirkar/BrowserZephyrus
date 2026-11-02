"""The Shield DNS switch in chrome://settings: present, honest, and it really flips the pref.

Opens the Zephyrus settings page, finds the toggle through the shadow DOM, reads its label, clicks
it, and reads the Local State pref back through settingsPrivate (which is what the page itself
uses). Run twice to see both directions.
"""
import json
import os
import sys
import time
import urllib.request

import websocket

sys.path.insert(0, r"D:\zeph_perf")
from cap import Browser, seed_profile  # noqa: E402

CDP = 9339
PROFILE = r"D:\zeph_perf\threat_settings_prof"

FIND = """
(function find(root, id) {
  const stack = [root];
  while (stack.length) {
    const n = stack.pop();
    if (n.id === id) return n;
    if (n.shadowRoot) stack.push(n.shadowRoot);
    for (const c of (n.children || [])) stack.push(c);
  }
  return null;
})
"""


def main():
    seed_profile(PROFILE, workspaces=False, bookmarks=False)
    b = Browser(PROFILE, args=["--remote-debugging-port=%d" % CDP], urls=("chrome://settings/adBlocker",), wait=25)
    time.sleep(6)
    tabs = json.load(urllib.request.urlopen("http://127.0.0.1:%d/json/list" % CDP))
    print("tabs:", [(t.get("type"), t.get("url")) for t in tabs])
    tab = [t for t in tabs if t.get("type") == "page"][0]
    ws = websocket.create_connection(tab["webSocketDebuggerUrl"], timeout=20, suppress_origin=True)
    nid = [0]

    def js(expr):
        nid[0] += 1
        ws.send(json.dumps({"id": nid[0], "method": "Runtime.evaluate",
                            "params": {"expression": expr, "returnByValue": True, "awaitPromise": True}}))
        while True:
            m = json.loads(ws.recv())
            if m.get("id") == nid[0]:
                return (m.get("result", {}).get("result", {}) or {}).get("value")

    js("location.href = 'chrome://settings/adBlocker'; true")
    time.sleep(6)
    ws.close()
    tab = [t for t in json.load(urllib.request.urlopen("http://127.0.0.1:%d/json/list" % CDP))
           if t.get("type") == "page"][0]
    print("now at:", tab.get("url"))
    ws = websocket.create_connection(tab["webSocketDebuggerUrl"], timeout=20, suppress_origin=True)
    find = "(%s)(document, 'shieldDnsToggle')" % FIND.strip()
    state = js("(()=>{const t=%s; return t ? JSON.stringify({label:t.label, sub:t.subLabel, checked:t.checked}) : null})()" % find)
    print("toggle:", state)
    ok = state is not None
    pref = lambda: js("new Promise(r=>chrome.settingsPrivate.getPref('zephyrus.threat.shield_dns', p=>r(p&&p.value)))")
    before = pref()
    print("pref before:", before)
    js("(()=>{const t=%s; t.click(); return true})()" % find)
    time.sleep(1.5)
    after = pref()
    print("pref after one click:", after)
    ok = ok and before is False and after is True
    js("(()=>{const t=%s; t.click(); return true})()" % find)
    time.sleep(1.5)
    back = pref()
    print("pref after a second click:", back)
    ok = ok and back is False
    ws.close()
    b.close()
    print("PASS" if ok else "FAIL")


if __name__ == "__main__":
    main()
