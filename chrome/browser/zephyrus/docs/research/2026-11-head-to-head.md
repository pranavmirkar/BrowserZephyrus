# Zephyrus against Brave's ad-block engine, measured on one machine

2 November 2026. One filter list, one set of real requests, three matchers, run
one after another on the same Windows machine. Nothing here is estimated, and the
Linux and stand-in `GURL` caveats of the earlier notes do not apply: the Zephyrus
side runs inside the Chromium tree with the real `GURL` and public-suffix list.

## Method

- **List.** The combined list the browser itself downloaded and loaded: 8.96 MB
  after `PreprocessFilterList`, 128,603 rules (EasyList, EasyPrivacy, the uBlock
  Origin lists, Fanboy, the Indian list). Both engines parse the same text.
- **Requests.** 9,591 requests recorded from 30 news, shopping and Indian sites
  (91 distinct initiators including frames) with Zephyrus's own NetLog and the
  blocker switched off, so nothing was missing. 67% are third-party. The resource
  type is inferred from the file extension (NetLog does not record it).
  `research/h2h_crawl.py` records them; they are not committed.
- **Zephyrus.** `AdblockZIndexTest.HeadToHeadCorpus` times the shipped engine and
  the compiled index on those requests (one warm-up pass, three timed passes,
  `base::TimeTicks`). The `GURL`s are parsed beforehand, as they already are by
  the time a request reaches the blocker in the browser.
- **Brave.** `research/brave_compare/src/bin_h2h.rs`, adblock-rust 0.13.5 (the
  engine in Brave), release build, same list and requests, same warm-up and
  passes. Reported twice: matching only (the `Request` already built) and
  including `Request::new` from strings, which is the work Brave's integration
  must do for every request.

## Results

| | mean | p50 | p99 | p99.9 |
|---|---|---|---|---|
| Zephyrus engine, before | 27.86 µs | 22.0 | 76.0 | 155.0 |
| **Zephyrus compiled index** | **4.12 µs** | **3.0** | **23.0** | **58.0** |
| Brave, matching only | 6.56 µs | 5.8 | 22.4 | 56.4 |
| Brave, `Request` + matching | 9.62 µs | 8.3 | 29.9 | 69.7 |

- **Decisions.** All three blocked the same 4,467 of 9,591 requests (46.6%), and
  **there is not one request on which any two of them differ.** On this traffic
  the blocking is identical; what differs is the cost of getting there.
- **Speed.** The index is **6.8× faster than the engine it replaces**, **1.6×
  faster than Brave's engine at matching alone, and 2.3× faster against the work
  Brave's integration actually does per request**. At the tail the two are level
  (p99 23.0 against 22.4 µs; p99.9 58.0 against 56.4): the average is better, the
  worst case is not.
- **Memory.** The index is one 10.65 MB blob per process, shared by every profile.
  Brave's engine is +13.8 MB of working set, and serialises to 10.89 MB; the old
  Zephyrus engine was 17.7 MB *per profile*. Brave's serialised form could be
  shared the same way, so the saving over Brave is small, and the saving over what
  Zephyrus shipped is the large one.
- **Build time: Brave wins.** Brave parses and builds in 149 ms. The Zephyrus
  index takes about 470 ms to build (and 60 to 100 ms to parse first), which the
  disk cache keeps off the start-up path but not off a list update.

## What this does and does not show

It shows that on matching, the cost that decides whether blocking is felt, a
Zephyrus request is now cheaper than a Brave request in the mean, and that it
blocks identically on real traffic. It does **not** show that Zephyrus is a better
browser than Brave. This is one component, one list, 30 sites visited once from one
network, with the resource type guessed. Brave also does things this does not
measure: its Shields also take on cookies, fingerprinting, HTTPS upgrades and
tracking-parameter removal, and it ships Chromium security fixes within days.

Reproduce: `research/h2h_crawl.py` (needs Zephyrus), then run the test with
`ZEPHYRUS_H2H_LIST`, `ZEPHYRUS_H2H_CORPUS` and `ZEPHYRUS_H2H_OUT` set, then
`cargo run --release --bin h2h -- <out>/preprocessed.txt <corpus> <out>` in
`research/brave_compare`.

## Addendum: tracking-parameter removal costs less than it looks

Brave's engine reports a rewritten URL from the same call (that is how it supports
`$removeparam`), and the list Brave was given contains the same rules, so a fair
total for Zephyrus has to add the cost of doing the same. (I did not measure how
many of the rules Brave applies, only its whole call.) Zephyrus now removes
tracking parameters (a built-in set of about 25 plus the 251 usable `$removeparam`
rules of the lists; 20 more are skipped because they need a regex lookahead, which
RE2 does not have, or are shapes that would be unsafe), by answering the request
with a redirect to the same address without them.

On the same 9,591 recorded requests: 4,469 carry a query string. Run on all of
them the removal costs a mean of 2.08 µs, but the proxy only asks for a request
that was **not blocked**, and for those it costs **0.54 µs on average, 0.29 µs
averaged over all requests**. Together with matching that is about 4.2 µs per
request against Brave's 6.56 µs for its whole call on the same list. A crawl of home
pages removes nothing from the requests that are not blocked, because a tracker
URL that carries `gclid` is blocked first; the feature matters on the link you click
or paste, which a crawl by typed address does not exercise. That is measured live
instead (`research/ab_params.py`): a navigation and a fetch and an
image all arrive cleaned, `id`, `state`, `code`, `ref` and `source` are untouched,
the fragment survives, history keeps one entry, and
`--disable-features=ZephyrusAdblockStripTrackingParams` restores the original.
