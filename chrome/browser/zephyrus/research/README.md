# Research kit: measured prototypes for Shield, threat blocking and ID-leak detection

Nothing in this directory is built by GN or shipped. It is a bench: the real
ad-block engine sources are compiled **unmodified** against small stand-ins for
`base::`, `GURL` and the public-suffix helpers (`shim/`), so the engine can be
measured and attacked outside a Chromium checkout. Findings are written up in
`../docs/research/2026-11-deep-dive.md`.

```
chrome/browser/zephyrus/research/run_all.sh [work-dir]
```

Needs g++ 13, python3, node, libssl-dev and a network connection. It downloads
the third-party lists the updater downloads into the work directory (never
into the repo) and runs everything below.

| File | What it is |
|---|---|
| `list_audit.py` | Counts how much of each list the engine's parser can use, and why rules are dropped |
| `baseline_engine.cc` | The engine as shipped: parse time, heap, per-request latency, destructor time |
| `cost_split.cc`, `token_hotspots.cc` | Where a request's time goes, and which tokens put hundreds of rules in front of every request |
| `zindex.h` | **ZIndex**: compact, immutable, memory-mappable index of network rules, same matching semantics |
| `ztest.cc` | Builds ZIndex from the real parser's output, then **differential-tests** it against the real engine on about 940,000 probes derived from every rule, and benchmarks both |
| `zfuzz.cc` | Mutates the blob and, if `Load()` accepts it, *uses* it, under ASan + UBSan with a hang watchdog |
| `threat_index.h`, `threat_test.cc` | **ThreatIndex**: an offline list of known-bad hosts and URLs. No per-URL network query |
| `idleak.h`, `idleak_test.cc` | **IdLeakScanner**: finds the user's own identifiers (plain, percent-encoded, or as MD5/SHA-1/SHA-256 in hex/base64) in an outgoing request |
| `scriptlet_compile_time.js` | V8 cost of the scriptlet runtime library per document, with and without a code cache |
| `shim/` | The stand-ins. Behaviourally faithful for the call shapes the engine uses; the `GURL` and public-suffix ones are simplified |

## Read this before trusting a number

- Timings are from one Linux machine, with the stand-in `GURL`. They compare
  the shipped engine and the prototype **in the same process on the same
  corpus**, which is the part that carries over. Absolute microseconds in the
  browser will differ. Re-run `adblock_matcher_perftest.cc` in the tree before
  quoting any figure.
- The request corpus is synthetic (about 90% ordinary subresources, 10% shaped
  like blocked ones). It exercises the code paths; it is not recorded traffic.
- ZIndex is **not** drop-in. It is proven equivalent to the shipped matcher on
  the probes above, for the rule types the shipped parser accepts. Porting it
  into the service (shared across profiles, built off-thread, signed) is the
  next step and needs a Chromium build to verify.
- ThreatIndex data is the community Phishing.Database feed. That demonstrates
  size and speed, not coverage against Google's Safe Browsing.
