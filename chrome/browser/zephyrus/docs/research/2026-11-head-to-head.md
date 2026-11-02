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

## Addendum: `$to=` rules, and a place where Brave's engine is behind

`$to=host|~host` limits a rule to requests whose destination host is (a subdomain
of) one of the first and none of the second. 183 plain rules and 259 regex rules in
the lists use it (mostly the malware list: `||name.$doc,to=~edu|~gov`). Both the
engine and the compiled index now read it, so 158 more rules are usable (128,603 to
128,761), the index grew from 10.65 to 10.70 MB, and the index format is version 3.

What was checked, and how each check was shown to be able to fail:

- Engine and index unit tests for include, exclude, exclude-only, `$to=` with
  `$domain=`, and an exception rule. With the index's `$to=` test switched off,
  exactly the new test fails.
- The all-rules differential on the full downloaded list: 651,308 probes over
  128,682 rules, 0 mismatches. With the index's `$to=` test switched off the same
  differential reports 82. (On the shipped seed list it reported 0 even then,
  because that list has no `$to=` rules; the check is now run on the full list too.)
- Real traffic, 9,591 requests: engine, index and Brave still agree on every one.
- Brave as an oracle, on 268 requests built from 112 real `$to=` rules (one request
  at a host the rule admits, one at a host it does not): Zephyrus's engine and
  index agree with each other on all of them, and block 173. Brave blocks 2. On six
  hand-made cases Brave allows `||tonly.example^$doc,to=example` for a page at
  `tonly.example`, which the rule says to block, so **adblock-rust 0.13.5 ignores
  `$to=` rules**. 172 of the 173 differences are those. The other is a regex rule
  that Zephyrus does not read yet (next).

## Addendum: `/regex/` rules (RE2), and where Brave's engine is behind again

Until now the engine and the compiled index dropped every `/regex/` rule: 608 lines in
the lists. 393 of those have options this browser reads. They are matched by a new `AdblockRegexMatcher`, kept apart
from the keyword buckets and the index, and its three answers (important, block,
exception) are ORed into the verdict of whichever of the two is in use.

How it stays cheap: RE2 (linear time, so a hostile list or URL cannot make it
backtrack) behind three cheaper questions. (1) A **gate**: `re2::FilteredRE2` says
which literals a regex needs; the smallest, rarest sufficient set becomes the rule's
gate, and a four-byte-prefix scan of the URL wakes only the rules whose gate occurs.
(2) The resource **type**, from 12 bytes per rule. (3) Every other option, by
`RuleOptionsMatch` -- the same function the engine uses for its own rules, so the two
cannot read an option differently. Long `$domain=` / `$to=` lists (some rules name a
hundred sites) are now sorted at parse time and searched by host suffix.

What does not run, and what stays safe: 39 of the 393 use lookahead or back-references,
which RE2 (and Brave's Rust `regex`) cannot express; the next addendum shows how those
are now read. A rule neither engine reads is counted and left out, and never blocks
anything. A URL over 16 KB is allowed through. At most 4,096 regexes are compiled, each
with at most 2 MB of RE2 memory.

Measured, same machine, same list:

| | mean per request | notes |
|---|---|---|
| compiled index | ~3.8 us | before |
| regex step alone | ~2.7 us | 354 compiled, 70 tried for every request, builds in ~40 ms |
| index + regex | ~6.4 us | |
| Brave 0.13.5, match only / with `Request::new` | ~6.5 / ~8.7-9.6 us | includes its own regex rules |

- Real traffic, 9,591 requests: engine, index, engine + regex and index + regex still
  block the same 4,467, and none differs from Brave on any request. The regex rules
  add no block on this sample: they name rare malicious URL shapes (generated phishing
  domains, one-off ad-malware paths), which a crawl of 30 popular sites does not hit.
- Independent oracle (`research/rx_oracle.py`: Python `re` plus the option semantics),
  on 4,152 requests generated from the real regexes (the URL a regex matches, the
  same with the wrong type, and a one-character near miss): 4,135 agree. Of the
  other 17, 7 are the oracle testing a URL before GURL rewrites it (`\`, `^`, an
  invalid IPv4 address) and 10 are `redirect=noopjs` rules, which the oracle did not
  read and the engine blocks as an empty response, as it does for plain rules.
  Zephyrus blocked nothing the oracle did not, apart from those 10.
- Brave as a second oracle, same 4,152 requests: Zephyrus blocks 1,382 and Brave 686;
  664 are common. Of Zephyrus's 718: 617 are explained by a rule using `$to=` (Brave
  ignores `$to=`, shown earlier), 49 by `$popup`/`$doc` rules, and 52 by others,
  30 of which carry a `{400,1000}`-style repeat. Of Brave's 22 that Zephyrus does not
  block: 14 involve `$popup`/`$doc` rules, 5 other rules, 3 `$to=` rules. We have not
  established why Brave and Zephyrus differ on the 52 + 49 that are not `$to=`.
- A mutated matcher (regex verdict inverted) failed 13 of the 20 regex tests then
  written (22 now).
- Live, in the browser (`research/ab_rx.py`, DevTools): a page requests 700 URLs built
  from the real regex rules, each as an image and a script (1,400 requests), with the
  shared index loaded from its disk cache and the downloaded 129,115-rule list. The
  browser blocked 133: the 132 the unit-test binary predicts, plus
  `ads.host-cdn.net/mcdZjs?2` as an image, which the built-in starter rule
  `||ads.*^$third-party` blocks (the unit test does not load the starter list). All
  81 requests that only the regex rules block were blocked.

## Addendum: lookahead and back-references (ICU), and a second place Brave stops

The 39 regex rules RE2 cannot read target generated-looking ad-malware and phishing
paths, such as `^https:\/\/[a-z]{2}\.[a-z]{7,14}\.com\/[a-z](?=[a-z]*[0-9A-Z])...`.
They were 11% of the generated requests in the corpus above, and uBlock Origin reads
them (its regexes are JavaScript's). They are read now.

How: each such regex is **widened** into one RE2 can run, by removing every lookahead /
lookbehind group (an extra condition can only narrow a match) and turning every
back-reference into `.*`. That regex can only match more than the original, so it gates
the rule (its required literals) and is the cheap first test. Only a URL it accepts
reaches **ICU's regex engine** (already in the browser process, used by autofill), which
checks the real pattern under a step limit (200,000 match steps) and a 256 KB backtrack
stack; hitting either means "no match", so a request is allowed and never blocked wrongly.
At most 512 rules may use ICU; RE2's memory cap is applied before ICU compiles anything.

What was checked, and how each check was shown able to fail:

- **ICU reads a regex the way RE2 does**: all 354 real regexes both can compile, run in
  both on 4,146 generated and 9,591 real-traffic URLs: 0 disagreements, 6,039 true
  matches (`IcuReadsTheRealRegexesLikeRE2Does`). Without that, trusting ICU for the
  rules RE2 cannot read would prove nothing.
- Unit tests for negative and positive lookahead, case-insensitive vs `$match-case`
  lookahead, back-references, lookarounds nested in groups and classes, and a lookahead
  rule inside exceptions and options. In each the widened regex alone gives the wrong
  answer, so each fails if ICU is not asked.
- **The step limit is what stops a hang**: `/(x+x+)+y(?=z)/` on a URL of 60 `x` returns in
  3 ms. With the limit removed the same test did not finish in 60 s.
- Independent oracle (Python `re`, which reads lookahead natively), now on all 393 rules
  and the same 4,152 generated requests: 4,145 agree and Zephyrus blocks nothing the
  oracle does not; the other 7 are URLs GURL rewrites or rejects before matching.
- Real traffic, 9,591 requests: engine, index, with and without regex rules, and Brave
  still agree on every request (4,467 blocked). Engine/index all-rules differential on the
  full list: 651,419 probes, 0 mismatches.
- Corpus built from the real regexes: Zephyrus now blocks 1,617 of 4,152 (it was 1,382
  before the lookahead rules), Brave 686. The 235 added are requests only a lookahead
  rule blocks.
- Live, in the browser (DevTools, shared index from its disk cache): 1,400 requests from
  the generated URLs. The browser blocked 163 = the 162 predicted + the starter-rule
  block seen before; all 111 requests only the regex rules block (30 of them only the
  lookahead rules) were blocked.

Cost: 393 rules compile in ~50 ms (39 of them also in ICU); 82 are tried for every
request (70 before). The regex step is about 3.0 us mean on real traffic (2.7 before), so
index + regex is ~6.9 us against Brave's 6.5 us match-only (8.7-9.6 us with its Request
object).
