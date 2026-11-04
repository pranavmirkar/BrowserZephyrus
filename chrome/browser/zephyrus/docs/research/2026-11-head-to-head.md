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

## Addendum: `$urlskip=` (link-redirector bypass), a rule type Brave's engine does not read

A link like `https://safelinks.protection.outlook.com/?url=https%3A%2F%2Fnews.example`
asks a tracking service to log the click and send you on. The destination is in the
link, so the detour can be skipped. uBlock Origin has 242 such rules in the lists used
here (217 in its privacy list, 25 in its main list); the string `urlskip` does not appear
anywhere in adblock-rust 0.13.5's source, so Brave's engine cannot apply them.

How: `AdblockUrlSkipFilter` reads each rule's steps (`?name`, `&N`, `#`, `/regex/`,
`+https`, `-base64`, `-safebase64`, `-uricomponent`, `-blocked`) and matches its request
pattern with the same engine as `$removeparam`. A page load that matches is answered with
a redirect to the destination before the network is touched, by the same mechanism as
tracking-parameter removal, which now also does what a real redirect does to a request
(cookie site, referrer, isolation info) because a skip crosses sites. The destination
comes back through the block lists, and at most 8 redirects of this kind can chain.

Safe by construction: only rules from uBlock Origin's own list sections are read (the
test used for trusted scriptlets), because a redirect the list author controls sends you
somewhere; the result must be an http(s) address other than the one asked for, with no
credentials and no control characters; any step that cannot be done means no skip; a
request the block lists block is blocked, not skipped, unless the rule says `-blocked`;
only GET page loads are considered; the allowlist and a kill switch
(`--disable-features=ZephyrusAdblockUrlSkip`) apply.

What was checked, and how each check was shown able to fail:

- All 242 `urlskip` lines of the list load (0 unusable, 0 from untrusted sections).
- 23 unit tests (one runs the whole shipped list): every step, failure modes, trust, options before and after the value,
  regex patterns, `-blocked`, earliest rule wins. With the credentials check and the
  `-blocked` guard removed, exactly the two tests about them fail.
- Independent corpus (`research/h2h_urlskip_corpus.py`): addresses built in Python from
  the real rules by running their steps backwards, with the destination each must skip
  to, plus the same addresses with the parameter missing and on another host. 524 URLs:
  179 skip to exactly the expected destination (166 plain-step rules, 13 regex-step
  rules), 345 are correctly left alone, 0 wrong. Not covered by the generator: 18
  rules whose pattern is a regex and 32 whose regex step it cannot invert (the unit
  tests cover both kinds).
- Real traffic, 9,591 recorded URLs offered as page loads: none skipped (a crawl of home
  pages has no tracker links), at 3.6 us per call, and only page loads ask.
- Live, in the browser (`research/ab_urlskip.py`, a local server records the headers of
  every request): a typed navigation to a real tracker URL, a base64 tracker (storz.ma)
  and a clicked tracker link all reach the destination, the tracker host is never asked,
  and history grows by one entry. The request the server gets has the same Sec-Fetch-*,
  Referer and Cookie as a direct navigation, including a click to a different site
  (`cross-site`, origin-only referrer). For a same-site destination the referrer is the
  origin only, where a direct click sends the full URL: the redirect applies the page's
  referrer policy as it would have to the tracker, so it is never fuller. The kill switch
  restores the plain navigation. Tracking-parameter removal was re-run after the redirect
  change and still passes all 8 of its checks.

## Addendum: `$redirect=` stand-ins, the biggest coverage hole where Brave was ahead

A `redirect=` rule does not only refuse a file: it names a harmless substitute with the
same shape, so a page that expected `googletag`, a `ga()` that runs its callbacks or an ad
manager that answers at once keeps working. The lists carry 1,378 such rules (959
`redirect=`, 419 `redirect-rule=`, plus 19 exceptions for them) naming 49 resources. Until
now the engine did one of two bad things with them: 665 (the "empty" stand-ins) were plain
blocks, which breaks pages, and **294 were dropped, so the real ad or tracker script
loaded**; the 419 `redirect-rule=` lines and the exceptions were ignored. Brave's engine
supports redirect resources.

What was built:

- **40 stand-ins and 24 aliases, written from scratch** from the public documentation of
  each API (`redirect_resources/`; uBlock Origin's are GPL and the lists only name them).
  Scripts: an IMA ad manager that answers at once and tells the player to resume, GPT with
  its chained calls and command queue, GTM/analytics that run `eventCallback`/`hitCallback`,
  `adsbygoogle`, Amazon APS, fingerprinting libraries that return one constant, anti-adblock
  detectors that never detect, and so on; plus transparent images, silent audio, a 1 s video,
  empty documents and VAST/VMAP stubs. `tools/gen_redirect_resources.py` builds the table;
  resource ids are stored in the compiled index and are never renumbered.
- **Semantics as uBlock Origin documents them:** `redirect=` is a block plus a directive,
  `redirect-rule=` only a directive for requests something else blocks, `name:N` priority
  (highest wins, then lowest id so the engine and the index choose alike), a named
  `@@...$redirect-rule=name` cancels that stand-in and a nameless one all of them, none of
  which ever unblocks; an unknown resource name drops the rule; `redirect=none` is a block.
- **Index format 4:** two more rule groups and the stand-in id and priority in the rule
  record's spare bytes. The stand-in is chosen only for a request that is blocked.
- **The response is given under the original address** by a loader in the browser process
  (`ZephyrusAdblockStandIn`): the page's Content-Security-Policy, which allowed that file,
  still holds; `response_type` and the CORS headers are what the network stack would have
  produced, so `fetch()` and credentialed XHR of a stand-in resolve; `no-store`; never for a
  page load; a stand-in only answers the kinds of request it can (a picture is not handed to
  a `<script>`). Kill switch: `--disable-features=ZephyrusAdblockStandIns`.
- This Chromium build has no H.264 (`canPlayType` says so), so the video stand-in is
  VP9-in-MP4, which every Chromium build plays.

What was checked, and how each check was shown able to fail:

- Every stand-in executed in `node` and used the way a page does (39 checks:
  `tools/test_redirect_stubs.cjs`); 22 unit tests for the table and rules; **all 1,397
  redirect options of the shipped list resolve to a stand-in** (0 unknown names).
  With the index made to ignore redirect exceptions, exactly the three tests about
  exceptions fail.
- The all-rules differential on the full list now also compares the stand-in chosen:
  660,428 probes, 0 mismatches, and the 8,421 probes served a stand-in agree between the
  engine and the compiled index.
- Real traffic, 9,591 requests: engine, index and Brave still agree on every one (4,467
  blocked); the index is unchanged at about 3.9 us. Choosing the stand-in costs 3.3 us on a
  blocked request and nothing on an allowed one; 106 of the 4,467 blocked real requests are
  given one.
- Brave as an oracle on 921 requests built from the real redirect rules: 859 blocked by
  both. Zephyrus alone blocks 4: all four rules use `domain=site.*`, an entity wildcard
  Brave's engine has never matched (hand-made case confirms). Brave alone blocks 1: a
  `redirect-rule=` marked `important`, which Brave treats as a block and uBlock Origin's
  documentation says is only a directive (hand-made case confirms).
- **Live, in the browser** (`research/ab_standins.py`): a page requests, at hosts that do not
  exist, one URL per stand-in. All 40 test rules and all 25 real rules the page can reach are
  served: scripts fire `load` (not `error`) and the API the page expects exists, images
  decode, stylesheets load, audio and video report their duration, and `fetch` and a
  credentialed `XMLHttpRequest` get status 200 and the right body. With the kill switch none
  is served. Tracking-parameter removal, link skipping and the regex rules were re-run on the
  new proxy code and still pass.

Not shown: that a real video player continues past our IMA stand-in (it is tested against the
documented API, not against a live player), and iframes are only shown to load, because a
blocked frame fires `load` too. Five rules need `method=` and three are malformed, so they
still do nothing; a regex-pattern rule with `redirect=` still blocks but has no stand-in.

## Addendum: fuzzing every parser, and the one crash it found

Every place the blocker reads text or bytes it did not write now has a fuzz target: filter-list
text into network rules and the compiled index (the engine and the index must agree on every
probe), the compiled index itself as raw bytes, `/regex/` rules, `$removeparam`, `$urlskip`,
cosmetic rules and `:style()`, `##+js()` scriptlet rules, and the `!#if` / section / header
helpers. Each target runs one input through the parser and through whatever consumes its output,
and checks the promises the parser makes: a hiding selector or style declaration cannot close its
own rule, open a comment or leave a quote open; a scriptlet call line contains only string
literals; a skip target is a plain web address different from the page; an index built from a list
answers exactly as the engine it came from.

How it was run:

- **Deterministic mutation test** in the ordinary unit tests (`adblock_fuzz_regression_unittest.cc`):
  every seed, then a seeded stream of mutations (bit flips, interesting bytes, inserts, deletes,
  duplicated runs, spliced seeds, dictionary tokens, long repeated runs). 40,000 inputs per
  target, with 80 windows of the real shipped list among the seeds, and 1,500 per target on every
  ordinary test run (about 4 seconds).
- **libFuzzer under AddressSanitizer**, one binary per target, with a dictionary of the syntax
  tokens. Two rounds, all eight in parallel: 5 minutes, then 10 minutes on the grown corpora.
  About 4.4 million executions in the second round, most of them the cheap helper target; the
  parser-heavy targets run at 25 to 85 inputs a second under ASAN, so those got 15 to 50
  thousand each. That is a modest amount of fuzzing, not a lot. A third round, after the
  index fix, ran 30 minutes per target with windows of the real shipped list as extra seeds
  (the first two used only hand-written seeds): 11.6 million executions, no findings, and
  coverage grew on every parser target (list parser 3,619 to 4,455 edges, regex 7,807 to 9,039,
  `removeparam` 5,091 to 5,773, `urlskip` 5,644 to 7,008). The heavy targets still run at 14 to
  71 inputs a second under ASAN, so depth on them is limited by speed, not by time.

Do the checks work? Each invariant was handed known-bad values and refuses them (4 tests), and a
bug planted in the compiled index (ignore `$important`) was caught by the engine-against-index
check within 789 inputs, with the failing input saved. A check that cannot fail would have proved
nothing here.

**One real finding.** A compiled index whose table directory was all zero (a valid header, then
zeros) was accepted by `Load()`, and the first lookup stopped the browser on an out-of-range
array index. The cause: slot lookup hashes with `key >> (32 - log2)`, which for `log2 == 0` is a
shift by the word size, and `Load()` allowed it although the writer never makes a table smaller
than 16 slots. The index is a file in the profile. Random damage to it was always caught by the
cache file's checksum before `Load()` ran, so this could only be reached by a file someone wrote
by hand with a valid checksum (an earlier version of this note said a damaged file could crash the
browser on every start; that was wrong, the checksum stops damage). `Load()` is the second line of
defence for exactly that case, and it had a hole. It now refuses tables outside 16 slots to 2^24;
this rejects only blobs the writer could never have produced. The test fails without the fix and
passes with it. Nothing else was found by either method. The standing result is therefore one
denial-of-service, bounded by the hardened `span` check, not memory corruption.

**Against Brave, on the parsers.** Not better, only closer. Brave's engine is Rust, so this whole
class is a safe panic or a rejected input by construction; ours reads a binary file in place from
C++ and had to be checked. Their repository has fuzz targets for filter parsing and request
parsing (at least `parse_filter`, `parse_filter_hosts` and `parse_request`; the listing I could
read may be partial); I did not run them. The next
addendum measures the stored-engine file, where the two designs differ most.

Found along the way: the sanitizer and plugin build refuses a raw `span` or pointer member, which
the Release build never checks. The index's view members are now marked as deliberate exclusions
(they are hot, read-only views into the index's own blob), so the fuzz and ASAN builds compile.

## Addendum: a damaged or rewritten engine file, Brave against Zephyrus

Both engines can store their compiled rules in a file and reload it at the next start. What each
does with a file that has been damaged is a hard, countable test, so I ran the same one against
both (`research/hostile_driver.py`; Brave's side is `research/brave_compare/src/bin_corrupt.rs`,
Zephyrus's is `AdblockZIndexTest.HostileBlobCampaign`). Each case takes the engine's stored file
for the shipped list (about 10.9 MB for both), mutates one copy (a bit flip, one to three aligned
32-bit overwrites with 0, 1, 0xFFFFFFFF, 0x7FFFFFFF, 0x80000000 or sizes, half of them in the first
1% where the directories live, or a zeroed run of 16 to 4,096 bytes), reloads it, and puts 300
recorded requests through it. 50,000 cases per engine and mode. A mutation that rewrites a byte
with the value already there is counted as a no-op and left out.

Two modes:

- **Damage**: anywhere in the file, the checksum not repaired. This is bit rot or a half-written
  file.
- **Rewrite**: the payload only, with the checksum repaired so it passes. This is somebody who can
  write the profile directory. Brave's own documentation puts this outside its threat model and
  skips verification for speed when the file's version matches, so this is not a fault in what they
  promise; it is measured because the compiled index defends against it.

| 50,000 cases each | Brave (adblock-rust 0.13.5) | Zephyrus |
|---|---|---|
| **Damage:** refused at load | 49,987 of 49,988 (the other changed a byte of the crate-version field, which the checksum does not cover; it loaded and behaved the same) | **49,539 of 49,539** |
| **Damage:** loaded and used | 1 | 0 |
| **Rewrite:** refused at load | 0 (0.00%) | 11,698 (23.61%) |
| **Rewrite:** loaded, same decisions | 47,353 (94.73%) | 37,576 (75.85%) |
| **Rewrite:** loaded, decisions silently changed | 121 (0.24%) | 267 (0.54%) |
| **Rewrite:** Rust panic / crash / hang | **2,512 panics (5.03%)**, 0 crashes, 0 hangs | **0 / 0 / 0** |
| Load of the intact file | 3.0 to 3.2 ms | **2.0 to 2.4 ms** |

Reading it honestly:

- **Damage is a tie.** Both reject essentially all of it. Zephyrus's cache checksum was 32-bit; it
  is now 64-bit and read eight bytes at a time over four lanes, which is stronger and faster than
  what it replaced (4.5 ms to 2.0 ms for the whole load), and the cache version was bumped so old
  files rebuild once.
- **Rewrite is where Brave is behind.** It trusts a same-version file without verifying it, and
  5% of rewritten files made it panic. In a Chromium build a Rust panic ends the process, so each
  of those is a browser that fails to start on a bad file. Zephyrus validates every offset before
  trusting a file and had no panic, crash or hang in 49,541 cases. Neither engine can tell a
  malicious rewrite that changes what is blocked from the real thing without a signature; the
  0.2% and 0.5% "changed" rows are exactly that, and are the same on both sides in kind.
- **Loading is also faster, with more checking**, because the checksum no longer reads the file a
  byte at a time.
- What this does not show: Brave's panic is a bounded failure (Rust bounds-checks slices), not
  memory corruption, so this is a robustness difference, not a code-execution one. It also covers
  one file format each; I did not fuzz Brave's list parser.

## Addendum: Safe Browsing, and where open data does not catch up

Audit item Z-05: this build has no Google API key, so Chromium's Safe Browsing does nothing.
Brave (desktop) uses Google's lists, kept locally as sorted 4-byte SHA-256 prefixes, and asks
Google (through a Brave-operated proxy, which hides the IP address but not the prefix) for
the full hash whenever a prefix matches. This addendum measures what a replacement built from
open feeds would hold, against what Brave holds, without loading a single bad page. The method:
read the prefix files in Brave's profile (`User Data/Safe Browsing/*.store.4_*`; refreshed the
moment I launched it, 4,384,053 distinct prefixes, 4.22 million of them in the phishing list),
hash each test URL's Safe Browsing expressions (host suffixes times path prefixes, at most 30)
and look them up. A URL that is not listed still hits by chance: measured on 7,512 real benign
URLs, **2.90%** of them match a prefix (the open index: 0.01%), and results below are corrected
for that. Scripts: `research/sb_coverage.py`, `sb_leave_one_out.py`, `sb_heuristics.py`.

Test sets are held out from the index being tested: PhishTank's verified, still-online phishing,
and OpenPhish's community feed (a few hundred URLs from the last hours). The open index is
Phishing.Database (789,057 URLs) plus its domain list and the Blocklist Project phishing list
(582,396 domains between them), 1,340,066 distinct entries.

| Fresh phishing | URLs | Brave (Google lists) | open-feed index | either |
|---|---|---|---|---|
| PhishTank, submitted < 30 days ago | 2,339 | **53.8%** | about 1% | about 54.5% |
| PhishTank, < 7 days | 729 | **50.6%** | 1.1% to 2.6% | 51.4% |
| OpenPhish, last hours | 300 | **59.0%** | 6.7% to 13.3% | 63.7% |

(The other 95% of PhishTank's "online" file is more than 30 days old and mostly dead links; there
the open feeds hold 26% and Google 11.6%, which says little about protection today.)

**Result: open data alone does not match Google's coverage. Brave holds 4 to 50 times more of
fresh phishing than an index built from the free feeds I could use.** The feeds mostly report
different URLs, so adding more of them helps little: all of them together hold 13% of OpenPhish's
fresh URLs and 3% of PhishTank's. Brave-or-ours is only 1 to 5 points above Brave alone.

A purely local check of the hostname (it borrows a well-known brand's name but is not that
brand's site) was tried as a way to catch what the lists lag on. It adds 15 points over Google
on the 30-day PhishTank sample, but 560 of its 650 hits are one campaign (look-alikes of
a Polish marketplace), and it flags 418 of 900,000 mid-tail domains (0.046%) and 4 of 888 hosts
of real browsing. That is too many false alarms to put behind a blocking warning; it is a lead,
not a feature.

**Where a local index does win, by construction:** it sends nothing. Brave sent a hash prefix to
Google (through its proxy) for about 3 in every 100 URLs it checked, and waits for the answer
before showing the page; the local index answers in about 3 microseconds (the prototype's
figure, outside the browser) and never leaves the machine. That is a real privacy and latency lead and a coverage loss of the size above. Neither
a tie nor a win overall.

**Using Google's data is possible but not free.** The Safe Browsing API v4 is licensed for
non-commercial use only (commercial use goes through the paid Web Risk API), requires clear
attribution to Google and only trusts a listing if the client updated within the last 30
minutes. Brave presumably has its own agreement with Google; I did not verify that.

## Addendum: threat protection that costs nothing, built and measured

The previous addendum found that open feeds alone do not match Google's coverage. This is what was
built instead, and what it measured. Everything below costs nothing to run: no Google key, no paid
API, no server.

**What was built** (`chrome/browser/zephyrus/threat/`, wired in through one factory line in
`services_delegate_desktop.cc`):

1. **A local index** of 1,340,107 known-bad addresses (Phishing.Database, Blocklist Project) as keyed
   48-bit hashes: 5.6 MB, instant, nothing leaves the machine. Always on.
2. **Shield DNS**, an opt-in switch in Settings (off by default): before a site outside the 100,000
   most popular (Majestic Million, 0.66 MB list) opens, the browser asks Cloudflare's free security
   DNS resolver whether the host name is blocked. Only the host name is sent: no path, query, cookies
   or identifiers. At most 8 requests in flight and 120 a minute, answers cached for minutes, and any
   failure means "allow".
3. Both feed Chromium's own Safe Browsing path, so the stock warning page, "proceed anyway" and the
   per-site bypass work unchanged. A build that later gains Google's lists keeps all three.

**Result**, on the same 419 live phishing hosts as before (PhishTank verified in the last 3 days and
OpenPhish's last hours, still resolving):

| | Fresh phishing caught |
|---|---|
| Brave (Google's lists) | **60.1%** (252 of 419) |
| Zephyrus as shipped (Shield DNS off) | 5.0% (21) |
| **Zephyrus with Shield DNS on** | **72.8%** (305) |
| Brave and Zephyrus together | 89.5% (375) |

With Shield DNS on, Zephyrus caught 123 hosts Brave's list did not, and Brave caught 70 that Zephyrus
did not. Skipping the popular sites cost nothing here (11 of the 419 are on a popular site; Cloudflare
flagged none of them).

**Did it beat Brave and Google? Honestly: only with the switch on, and only on this measure.**

- *Coverage of fresh phishing:* ahead by 12.7 points with Shield DNS on; far behind (5.0% against
  60.1%) with it off. The shipped default is off because turning it on sends host names to a third
  party, and a default cannot be taken back from a public installer.
- *Privacy:* with it off, nothing is sent, which is better than Brave (which sends a 4-byte hash
  prefix to Google, through its own proxy, for about 3 in 100 URLs it checks). With it on, Zephyrus
  sends the full host name of unfamiliar sites to Cloudflare. How many that is in ordinary use I
  measured only once: 0 lookups across 25 popular news pages and 3 small sites in the real browser
  (the end-to-end test shows exactly the two intended lookups). I did not measure Brave's rate on
  the same pages (its 3% is per URL, across every URL in a recorded crawl), nor the rate for
  someone who mostly visits unpopular sites, who would send many more. Each lookup also reveals
  more than a 4-byte prefix does. That trade is why it is a switch.
- *Latency:* the local index adds nothing. With Shield DNS on, the first visit in ten minutes to an
  unpopular host waits for one HTTPS round trip to Cloudflare.
- *Google:* the comparison is against Google's Safe Browsing lists as Brave holds them. Chrome also
  does real-time URL checks and page analysis that this build cannot reproduce, so this does not
  show Zephyrus beats Chrome.

**Caveats that matter.** The sample is one afternoon and 419 hosts. Some of the free resolvers
probably ingest PhishTank and OpenPhish, which inflates their score on those feeds; an independent
test (finding phishing from certificate logs myself) did not work at the scale I tried. Only
Cloudflare is used because it is the one resolver that answers a clean, unambiguous 0.0.0.0 for a
block and has a JSON interface; Quad9 scored 52.5% and could be added. Cloudflare's terms for use
inside a distributed product are unconfirmed (THIRD_PARTY_DATA.md), so keep it opt-in and replaceable
until they are.

**Tests:** 19 unit tests (index round trip, strict loading, 3,000 mutated blobs, the SipHash
reference vector, Shield DNS parsing and the request cap, the shipped blob finding 4,000 of 4,000
sampled feed URLs through Chromium's own canonicaliser); the blob loader fuzzed 213,000 runs under
ASan; and a real-browser test of 9 cases (`research/ab_threat.py`: listed host, subdomain, exact
address, other path, clean host, Shield on and off) that all pass.

## Update 2026-11-04: rules Brave's parser keeps and ours keeps (156 build)

Measured with `research/brave_compare` (bin `parity`: adblock-rust 0.13.5's own `parse_filter`) against this
engine, on the list the updater downloads (240,965 lines). "Keeps" means the line is understood, not that its
effect has been compared; where an effect was compared it is stated.

| rule family | lines | Brave's parser keeps | Zephyrus keeps | effect checked |
|---|---|---|---|---|
| procedural cosmetic (`#?#`, `:has-text`, `:upward`, `:xpath`, `:matches-css`, `:matches-attr`, `:not`/`:has`, `:remove`, `:style` ...; HTML filtering `##^` excluded) | 1,235 | 1,226 | **1,234** | live, 3 runs incl. kill switch and exception: `research/ab_proc.py`, PASS on the Release build |
| `$csp=` | 97 | 85 (12 refused as "csp with content type") | **96** (1 refused: a `report-uri` policy) | live, page and frame, kill switch, exception: `research/ab_csp.py`, PASS on Release |

How the two families work here: procedural selectors are evaluated by a page-side runner (no `eval`, regex
patterns refused when they could backtrack catastrophically, a 30 ms budget per pass, never hides `<html>` or
`<body>`); `$csp` adds a Content-Security-Policy header to a page or frame load a rule names, refusing any policy
with a reporting directive. Both have kill switches (`ZephyrusAdblockProceduralCosmetic`, `ZephyrusAdblockCsp`).

Not done, said plainly: HTML filtering (`##^`, 329 lines) is unsupported by both engines; `$replace` (87),
`$header` (54), `$ipaddress` (53) and `$uritransform` (20) are still unsupported here; Brave's parser refuses all four
too ("unrecognised option", checked line by line).
`$csp` on a redirect target is not applied (the loader follows redirects without asking the factory again).
Nothing here is compared for speed yet.
