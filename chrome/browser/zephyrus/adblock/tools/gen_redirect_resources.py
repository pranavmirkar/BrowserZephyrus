#!/usr/bin/env python3
# Copyright 2026 Pranav Mirkar. PolyForm Strict 1.0.0 (see the repository's
# LICENSE).
"""Generates adblock_redirect_resources_data.inc from redirect_resources/.

A filter list rule such as `||tracker.example/ga.js$script,redirect=google-analytics.com/ga.js`
names a stand-in the browser serves instead of the blocked file. The stand-ins are
written in redirect_resources/ (scripts) and redirect_resources/media/ (images, audio,
video); this table says which name means which, with which aliases and for which kinds of
request. Resource IDS ARE STORED IN THE COMPILED INDEX: never renumber or reuse one.

Run from this directory:  python gen_redirect_resources.py
"""
import base64
import os

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "redirect_resources")
OUT = os.path.join(HERE, "..", "adblock_redirect_resources_data.inc")

# ResourceType bits (adblock_filter_engine.h) a stand-in can serve.
OTHER, SCRIPT, IMAGE, STYLESHEET, OBJECT, XHR, SUBDOC, FONT, MEDIA, PING = (
    "kTypeOther", "kTypeScript", "kTypeImage", "kTypeStylesheet", "kTypeObject",
    "kTypeXhr", "kTypeSubdocument", "kTypeFont", "kTypeMedia", "kTypePing")
JS = (SCRIPT, XHR, OTHER, OBJECT, PING)
TEXT = (XHR, OTHER, PING, IMAGE, SCRIPT, STYLESHEET, FONT, OBJECT)
PIC = (IMAGE, XHR, OTHER, PING)
EVERYTHING = (OTHER, SCRIPT, IMAGE, STYLESHEET, OBJECT, XHR, SUBDOC, FONT, MEDIA, PING)

# (id, name, mime, types, aliases, file or None, text)   file: under redirect_resources/
# (media/ for binary). `text` is the body when there is no file.
R = [
    (1, "noop.js", "text/javascript", JS, ["noopjs"], None, "(function() {\n\t;\n})();\n"),
    (2, "noop.txt", "text/plain", TEXT, ["nooptext"], None, ""),
    (3, "noop.html", "text/html", (SUBDOC, OBJECT, XHR, OTHER), ["noopframe"], None,
     "<!DOCTYPE html>\n<html><head><meta charset=\"utf-8\"><title></title></head><body></body></html>\n"),
    (4, "noop.json", "application/json", TEXT, ["noopjson"], None, "{}"),
    (5, "noop.css", "text/css", (STYLESHEET, XHR, OTHER), [], None, ""),
    (6, "empty", "text/plain", EVERYTHING, [], None, ""),
    (7, "1x1.gif", "image/gif", PIC, ["1x1-transparent.gif"], "media/1x1.gif", None),
    (8, "2x2.png", "image/png", PIC, ["2x2-transparent.png"], "media/2x2.png", None),
    (9, "3x2.png", "image/png", PIC, ["3x2-transparent.png"], "media/3x2.png", None),
    (10, "32x32.png", "image/png", PIC, ["32x32-transparent.png"], "media/32x32.png", None),
    (11, "noop-0.1s.mp3", "audio/mpeg", (MEDIA, XHR, OTHER), ["noopmp3-0.1s"], "media/noop-0.1s.mp3", None),
    (12, "noop-1s.mp4", "video/mp4", (MEDIA, XHR, OTHER), ["noopmp4-1s"], "media/noop-1s.mp4", None),
    (13, "noop-vast2.xml", "text/xml", TEXT, ["noopvast-2.0"], None,
     "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<VAST version=\"2.0\"></VAST>\n"),
    (14, "noop-vast3.xml", "text/xml", TEXT, ["noopvast-3.0"], None,
     "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<VAST version=\"3.0\"></VAST>\n"),
    (15, "noop-vmap1.0.xml", "text/xml", TEXT, ["noopvmap-1.0"], None,
     "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<vmap:VMAP xmlns:vmap=\"http://www.iab.net/videosuite/vmap\" version=\"1.0\"></vmap:VMAP>\n"),
    (16, "google-ima.js", "text/javascript", JS, ["google-ima3"], "google-ima.js", None),
    (17, "googletagservices_gpt.js", "text/javascript", JS, ["googletagservices.com/gpt.js"], "googletagservices_gpt.js", None),
    (18, "googletagmanager_gtm.js", "text/javascript", JS, ["googletagmanager.com/gtm.js"], "googletagmanager_gtm.js", None),
    (19, "google-analytics_analytics.js", "text/javascript", JS, ["google-analytics.com/analytics.js"], "google-analytics_analytics.js", None),
    (20, "google-analytics_ga.js", "text/javascript", JS, ["google-analytics.com/ga.js"], "google-analytics_ga.js", None),
    (21, "google-analytics_cx_api.js", "text/javascript", JS, ["google-analytics.com/cx/api.js"], "google-analytics_cx_api.js", None),
    (22, "googlesyndication_adsbygoogle.js", "text/javascript", JS, ["googlesyndication.com/adsbygoogle.js"], "googlesyndication_adsbygoogle.js", None),
    (23, "doubleclick_instream_ad_status.js", "text/javascript", JS, [], "doubleclick_instream_ad_status.js", None),
    (24, "amazon_apstag.js", "text/javascript", JS, [], "amazon_apstag.js", None),
    (25, "amazon_ads.js", "text/javascript", JS, ["amazon-adsystem.com/aax2/amzn_ads.js"], "amazon_ads.js", None),
    (26, "scorecardresearch_beacon.js", "text/javascript", JS, ["scorecardresearch.com/beacon.js"], "scorecardresearch_beacon.js", None),
    (27, "chartbeat.js", "text/javascript", JS, [], "chartbeat.js", None),
    (28, "outbrain-widget.js", "text/javascript", JS, [], "outbrain-widget.js", None),
    (29, "fingerprint2.js", "text/javascript", JS, [], "fingerprint2.js", None),
    (30, "fingerprint3.js", "text/javascript", JS, [], "fingerprint3.js", None),
    (31, "prebid-ads.js", "text/javascript", JS, [], "prebid-ads.js", None),
    (32, "fuckadblock.js-3.2.0", "text/javascript", JS, [], "fuckadblock.js-3.2.0.js", None),
    (33, "nobab2.js", "text/javascript", JS, [], "nobab2.js", None),
    (34, "nobab.js", "text/javascript", JS, [], "nobab.js", None),
    (35, "nofab.js", "text/javascript", JS, [], "nofab.js", None),
    (36, "popads.js", "text/javascript", JS, ["popads.net.js", "popads-dummy.js"], "popads.js", None),
    (37, "hd-main.js", "text/javascript", JS, [], "hd-main.js", None),
    (38, "nitropay_ads.js", "text/javascript", JS, [], "nitropay_ads.js", None),
    (39, "adthrive_abd.js", "text/javascript", JS, [], "adthrive_abd.js", None),
    (40, "sensors-analytics.js", "text/javascript", JS, [], "sensors-analytics.js", None),
]

DELIM = "ZRES"


def cpp_text(body):
    assert ")" + DELIM + '"' not in body
    return 'R"%s(%s)%s"' % (DELIM, body, DELIM)


def main():
    ids = [r[0] for r in R]
    names = [r[1] for r in R] + [a for r in R for a in r[4]]
    assert len(ids) == len(set(ids)), "duplicate id"
    assert len(names) == len(set(names)), "duplicate name or alias"
    out = ["// Copyright 2026 Pranav Mirkar. PolyForm Strict 1.0.0 (see the repository's",
           "// LICENSE).",
           "//",
           "// GENERATED by tools/gen_redirect_resources.py from redirect_resources/. Do not edit.",
           "// Included by adblock_redirect_resources.cc.",
           "", "constexpr RedirectResource kResourceTable[] = {"]
    for rid, name, mime, types, aliases, fname, text in R:
        mask = " | ".join(types)
        if fname is None:
            body, kind = cpp_text(text), "text"
        else:
            path = os.path.join(SRC, fname)
            if fname.startswith("media/"):
                body, kind = '"%s"' % base64.b64encode(open(path, "rb").read()).decode(), "base64"
            else:
                body, kind = cpp_text(open(path, encoding="utf-8").read()), "text"
        out.append('    {%d, "%s", "%s", %s,' % (rid, name, mime, mask))
        out.append("     %s, %s}," % (body if kind == "text" else '""', body if kind == "base64" else '""'))
    out.append("};")
    out.append("")
    out.append("constexpr RedirectAlias kAliases[] = {")
    for rid, name, mime, types, aliases, fname, text in R:
        for a in aliases:
            out.append('    {"%s", %d},' % (a, rid))
    out.append("};")
    with open(OUT, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(out) + "\n")
    print("wrote", os.path.normpath(OUT), "-", len(R), "resources,", sum(len(r[4]) for r in R), "aliases")


if __name__ == "__main__":
    main()
