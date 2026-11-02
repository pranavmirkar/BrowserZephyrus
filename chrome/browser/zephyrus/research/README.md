# Research kit: measured prototypes for Shield, threat blocking and ID-leak detection

Nothing in this directory is built by GN or shipped. It is a bench. The real
ad-block engine sources are compiled **unmodified** against small stand-ins for
`base::`, `GURL` and the public-suffix helpers (`shim/`), so the engine can be
measured and attacked outside a Chromium checkout. Findings are written up in
`../docs/research/2026-11-deep-dive.md` and `../docs/research/2026-11-beating-brave.md`.

```
chrome/browser/zephyrus/research/run_all.sh [work-dir]
chrome/browser/zephyrus/research/real_traffic/run_real_traffic.sh [work-dir]   # run run_all.sh first, with the same work-dir
```

`run_all.sh` needs g++ 13, python3, node, libssl-dev, cargo and a network
connection. It downloads the third-party lists the updater downloads into the
work directory (never into the repo) and runs everything in the first table.
`run_real_traffic.sh` also needs Playwright (`npm i playwright`) for the crawl.
With the lists and a crawl already cached it takes about 35 seconds.

| File | What it is |
|---|---|
| `list_audit.py` | Counts how much of each list the engine's parser can use, and why rules are dropped |
| `baseline_engine.cc` | The engine as shipped: parse time, heap, per-request latency, destructor time |
| `cost_split.cc`, `token_hotspots.cc` | Where a request's time goes, and which tokens put hundreds of rules in front of every request |
| `zindex.h` | **ZIndex**: compact, immutable, memory-mappable index of network rules, same matching semantics |
| `ztest.cc` | Builds ZIndex from the real parser's output, then **differential-tests** it against the real engine on about 660,000 probes derived from every rule (including URLs with the match after 150 other tokens), and benchmarks both |
| `zfuzz.cc` | Mutates the blob and, if `Load()` accepts it, *uses* it, under ASan + UBSan with a hang watchdog |
| `brave_compare/` | Brave's `adblock-rust` 0.13.5 as a black box: speed, memory, and the rule-by-rule parser comparison |
| `h2h_crawl.py`, `brave_compare/src/bin_h2h.rs` | The head-to-head on ONE machine: record real traffic with Zephyrus's NetLog, then run Brave's engine and `AdblockZIndexTest.HeadToHeadCorpus` on the same list and requests. Results: `../docs/research/2026-11-head-to-head.md` |
| `threat_index.h`, `threat_test.cc` | **ThreatIndex**: an offline list of known-bad hosts and URLs. No per-URL network query |
| `idleak.h`, `idleak_test.cc` | **IdLeakScanner**: finds the user's own identifiers (plain, percent-encoded, or as MD5/SHA-1/SHA-256 in hex/base64) in an outgoing request |
| `scriptlet_compile_time.js` | V8 cost of the scriptlet runtime library per document, with and without a code cache |
| `shim/` | The stand-ins. Behaviourally faithful for the call shapes the engine uses; the `GURL` and public-suffix ones are simplified |

`real_traffic/` holds the recorded-traffic evaluation.

| File | What it is |
|---|---|
| `crawl.js`, `sites.txt`, `make_tsv.py` | Visit 50 sites as an ordinary visitor with nothing blocked, and turn the request log into `real.tsv` |
| `real_zeph.cc` | Shipped engine and ZIndex on every request: verdicts and per-request cost |
| `../brave_compare/src/bin_real.rs` | Brave's engine on the same requests |
| `reach.py` | Replays the log as a *protected* visit: a blocked frame loads nothing inside it |
| `analyze.py` | Agreement between engines, what a protected visit makes, and what four independent blocklists would add |
| `initiator_compare.py`, `explain_initiator.cc` | Frame-relative versus tab-relative `$third-party` and `domain=`, and the rule behind each difference |
| `shape_heuristic.py` | A crude "looks like tracking" test. A recorded negative result |
| `parity_zeph.cc`, `../brave_compare/src/bin_parity.rs`, `parity_report.py` | Which rules each parser keeps, by cause |

## Read this before trusting a number

- Timings are from one Linux machine, with the stand-in `GURL`. They compare
  the shipped engine, ZIndex and Brave's engine **in the same process on the same
  requests**, which is the part that carries over. Absolute microseconds in the
  browser will differ. Re-run `adblock_matcher_perftest.cc` in the tree before
  quoting any figure. Brave's timing leaves out building its `Request` object
  (lowercasing and tokenising), which the other two include.
- The synthetic request mix in `run_all.sh` is a unit-test workload. Its URLs
  average 51 characters and 11% match a rule. Real URLs average 171 characters and
  58% match, and ZIndex's lead over the shipped engine is smaller there (4× instead
  of 10×). Quote the recorded-traffic figures.
- The recorded traffic is 32 pages that let a headless browser in, 18 of 50 did
  not. It leans toward news, was fetched from outside India, visited once each,
  with no login and no consent clicks. Treat results as magnitudes.
- The log has frames but no script initiators. `reach.py` models the frame chain
  only, so "still loads" figures are upper bounds.
- Agreement between engines says nothing about breakage. Nothing here measures
  what stops working when a request is blocked.
- ZIndex is **not** drop-in. It is proven equivalent to the shipped matcher on
  the probes above, for the rule types the shipped parser accepts, and on every
  recorded request. Porting it into the service (shared across profiles, built
  off-thread, signed) is the next step and needs a Chromium build to verify.
- ThreatIndex data is the community Phishing.Database feed. That demonstrates
  size and speed, not coverage against Google's Safe Browsing.
- The independent blocklists the evaluation reads (StevenBlack, Peter Lowe, AdGuard
  DNS, Disconnect) are used for research only. Their licences differ and Disconnect's
  is non-commercial. Nothing derived from them belongs in a product without a check.
