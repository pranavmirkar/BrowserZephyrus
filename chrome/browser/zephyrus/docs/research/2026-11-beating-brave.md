# Beating Brave at ad blocking

Research note, 1 November 2026. It answers one question: how does Zephyrus beat
Brave at ad blocking and make Shield much stronger. It builds on
`2026-11-shield-next-level.md`, `2026-11-zep-next-level.md` and
`2026-11-deep-dive.md`. Every number comes from the kit in
`chrome/browser/zephyrus/research/`: run `run_all.sh`, then
`real_traffic/run_real_traffic.sh`.

Evidence labels: [measured] I ran it on the real sources or real pages,
[repo] read in this repository, [sourced] from a linked source, [memory] my own
knowledge, not re-checked here, [unverified] could not confirm.

I could not reach brave.com from this sandbox. Everything about Brave comes from
its open-source engine crate (adblock-rust 0.13.5, which I read and ran) and
from memory. What Brave's browser does on top of that crate is not covered.

## The answer

On the same public lists, Zephyrus and Brave block the same requests. I loaded
32 real pages with nothing blocked, logged 10,787 requests, and asked three
engines for a verdict on each: the engine Zephyrus ships, my ZIndex prototype,
and Brave's engine. All three agreed on all 10,787 [measured]. Any blocker that
loads EasyList, EasyPrivacy and the uBlock Origin lists ties on this test, so
adding more of the same rules cannot win it. I would rather hand you that than a
slogan a reviewer takes apart in an hour.

There are four places to win, and each has a number behind it.

1. **Cost.** Zephyrus loses today. It spends about 19 µs per request where
   Brave's engine spends 7 to 9. The index I prototyped spends about 4.5, which
   is 4× less than what ships and about 1.8× less than Brave. Matching one logged
   page takes 6.1 to 7.0 ms today and 1.4 to 1.6 ms with the index, and nobody
   feels that. What a person can feel is the 52 MB of heap per profile and the
   stalls on the UI thread. The rewrite fixes both for the network rules.
2. **Syntax.** Brave's engine reads 1,531 network rules that ours throws away.
   Ours reads 3,252 that Brave's drops, nearly all `$popup`. Closing the first
   gap is parity, and it is a countable job.
3. **Reach beyond the lists.** After blocking, 253 of the 4,361 requests that
   still load (5.8%) sit on at least one independent blocklist, on 66 hosts. A
   person can review 66 hosts in an afternoon. Nearly a quarter of those requests
   (59) go to one consent-banner library you would not want to block, which is
   the point: the lists cannot tell safe from unsafe, and a test that can is worth
   building. A crude "looks like tracking" test did not work.
4. **Proof and trust.** I found no browser that publishes a breakage number,
   shows which rule broke a page, or ships rules from one signed source
   [unverified, an absence claim]. Zephyrus can do all three. The first one
   decides whether blocking more is safe at all.

My recommendation. Build the shared index first, because it removes our deficit.
Close the cheap syntax gaps. Then put the real effort into a verified rule
pipeline (move 3). It is the one idea here that I found nobody shipping, and it
is where Zep gives Zephyrus something Brave does not have.

One idea I expected to help did not, and I dropped it. Judging `$third-party` and
`domain=` against the tab instead of the issuing frame looked like 65 extra
blocks. Replayed the way a protected visit unfolds, it is one request, and that
one defeats a deliberate carve-out in the list. Zephyrus's frame-relative choice is
right. Details are in the section on what I tested and dropped.

## What I measured

- **Pages.** 50 popular sites went in. 32 loaded properly: 21 global news, tech,
  health and weather sites, and 11 Indian ones. Six timed out and twelve returned a block
  page or an almost empty document. The sample is the set of sites that let a
  headless browser in. It leans toward news, so it is more ad-heavy than the web
  at large.
- **Visit.** Headless Chromium through Playwright, a desktop Chrome user agent,
  en-IN locale, nothing blocked, one visit per page, no login, no consent clicks,
  3.5 s of waiting and three scrolls. The traffic left from this sandbox's
  network, not an Indian home connection. Geo-targeted ad tags may differ from
  what an Indian user sees.
- **Lists.** The 17 files the updater downloads: 224,366 lines, of which the
  shipped parser accepts 126,520 network rules.
- **Engines.** The shipped Zephyrus sources, compiled unmodified with stand-ins
  for `base::`, `GURL` and the public-suffix helpers. ZIndex. Brave's adblock-rust
  0.13.5 as a black box. Each got the same URL, issuing frame and resource type.
- **Timing.** Mean per request after a warm-up pass. Ranges cover every run of
  the final code. Brave's figure leaves out building its `Request` object, which
  lowercases and tokenises the URL. Ours includes that work. So the gap to Brave
  is a little understated [measured, crate source read].
- **Replay.** The log comes from a visit with nothing blocked. A protected visit
  makes fewer requests, because a blocked frame loads nothing inside it.
  `reach.py` replays the log frame by frame. It models that one causal chain only.
  A request started by a script the engine blocks would also never happen, and
  the log does not record which script started which request. So every "still
  loads" figure below is an upper bound.

What a protected visit looks like on this sample [measured]:

| | Per page | Total |
|---|---|---|
| Requests in the unprotected log | 337 | 10,787 |
| Never made, because they sit inside a blocked frame | 52 | 1,670 (15.5%) |
| Made by a protected visit | 285 | 9,117 |
| Blocked, of those made | 149 | 4,756 (52.2%) |
| Still load (upper bound) | 136 | 4,361 |

Declared sizes exist for 4,260 of the 6,426 requests that never load. They add up
to 47.7 MB, or 1.5 MB per page, and that undercounts because many responses
declare no length. A frame verdict is the most valuable decision Shield makes,
since one block removes everything inside the frame. It is also the costliest to
get wrong.

## Scoreboard

Matching speed on the logged requests, all 10,787 of them.

| | Zephyrus as shipped | Brave's engine | ZIndex prototype |
|---|---|---|---|
| Same verdict as the shipped engine | n/a | 10,787 of 10,787 | 10,787 of 10,787 |
| Mean per request | 17.8 to 20.8 µs | 7.4 to 9.0 µs | 4.2 to 4.8 µs |
| p99 per request | 85 to 107 µs | 33 to 48 µs | 29 to 35 µs |
| Matching per logged page, 337 requests | 6.1 to 7.0 ms | 2.5 to 3.1 ms | 1.4 to 1.6 ms |
| Memory | 51.8 MB of heap per profile | 9.7 MB loaded, 13.5 MB when built; includes cosmetic data | 10.5 MB read-only mapping, no heap, shared; network rules only |
| Start | about 350 ms of parsing | 8.3 ms to deserialise | 1.9 ms to map and validate |
| Rules it parses that the other drops | 3,252 | 1,531 | same as shipped |

What the table says:

- The speed gap to Brave is real and small in absolute terms. Both spend a few
  milliseconds per page. Do not sell it.
- Against Brave, memory is a tie. Against ourselves the network index drops from
  33 MB per profile to one 10.5 MB mapping shared by all profiles. The cosmetic
  and scriptlet engines, 19 MB per profile, still need the same treatment.
- The synthetic corpus in the earlier note flattered ZIndex. It measured 1.9 µs
  against 20.3 µs shipped and 7.5 µs for Brave. Real URLs average 171 characters
  against 51 in the synthetic mix, and 58% of real requests match a rule against
  11%. The matcher does more work per real request. I withdraw the earlier
  "3.8× faster than Brave". The honest figure is about 1.8×.
- ZIndex is differentially tested, not drop-in. It returned the shipped
  engine's verdict on 660,219 probes derived from every accepted rule, including
  7,579 where the match sits after 150 other tokens, and on all 10,787 real
  requests. A fuzz run of 2,500 mutated blobs under ASan and UBSan produced 1,990
  rejections at load and 510 blobs that were then used for 400 matches each, with
  no crash or hang.

## Where Zephyrus is held back

These are our own chains. Each has evidence and a fix.

1. **One engine per profile, matched on the UI thread.** The service factory
   builds a full engine for the regular, Guest and off-the-record profiles. Each
   costs 51.8 MB and about 350 ms to parse. The proxy asks the service about every
   request on the UI thread, and a list update frees the old engine there, 60 ms
   for the network index and 94 ms in all [repo, measured]. One immutable shared
   index removes all three costs.
2. **Syntax the parser drops.** 1,531 network rules, listed under move 2.
3. **The scriptlet library is sent whole.** Every document that gets any
   scriptlet receives the full 55 KB runtime and compiles it: 1.2 ms, or 0.1 ms
   with a V8 code cache [measured].
4. **Four hosts ship code into pages.** The updater downloads lists from
   `easylist.to`, `raw.githubusercontent.com`, `secure.fanboy.co.nz` and
   `easylist-downloads.adblockplus.org`. About 9,400 scriptlet rules in this list
   set run as JavaScript in page context [repo, measured].
5. **An out-of-support Chromium base** (Z-01) and a missing updater (Z-02). A
   faster blocker on an unpatched engine is the wrong trade for users.

## What lists cannot do

Three limits hold for every list-based blocker, ours and Brave's.

**They tie.** Same inputs, same outputs. The 10,787 of 10,787 above is the proof.

**They stop where volunteers stopped.** Of the 4,361 requests that still load, 253
(5.8%) sit on at least one of StevenBlack's hosts file, Peter Lowe's list,
AdGuard's DNS filter and Disconnect's tracker list. They sit on 66 hosts, mostly
cookie-sync and ad-exchange endpoints: `ssp.disqus.com` (21),
`zz43629hpr.pub.tappx.com` (17), `pcs.ssp.cadent.com` (13), `sync.pmbmonetize.live`
(11), `dcs.ssp.cadent.com` (11). Per source: Disconnect 138 requests on 38 hosts,
StevenBlack 119 on 29, AdGuard 58 on 25, Peter Lowe 4 on 2. With 32 pages and an
upper bound, read the 5.8% as a magnitude, not a rate [measured].

**They cannot tell safe from unsafe.** The most frequent host in those 253 is
`cdn.cookielaw.org` with 59 requests. It serves OneTrust's consent-banner
library [memory]. Blocking it hides the banner and can break consent-gated
pages. Adding independent lists wholesale would inherit exactly that.

## What I tested and dropped

Three results that did not pan out. I kept them because each one saves someone
from repeating the idea.

**Judging against the tab instead of the issuing frame.** Zephyrus gives the
engine `request_initiator`, the frame that issued the request [repo]. I reran the
shipped engine with the tab's top-level page as the initiator. Judged one request
at a time, 65 more of the 10,787 are blocked and none are newly allowed. It looked
like a free 1.0% more blocks. It is not:

- The engine's own per-rule matcher shows what decides them. 64 of the 65 are
  `||criteo.com^$third-party`-style host rules, hit by an ad frame calling its own
  site, such as `usersync.gumgum.com` from `rtb.gumgum.com` (13), `ms-cookie-sync.presage.io`
  from itself (10), Criteo (9), Outbrain (8) and Taboola (6).
- Those frames are themselves blocked at load by the same engine. In a protected
  visit they never run, so the 64 requests never happen. Replayed frame by frame,
  both readings make 9,117 requests, and block 4,756 and 4,757.
- The one difference is `m.stripe.com/6`, called by Stripe's own frame
  `m.stripe.network`. The rule that fires is `||m.stripe.com^$third-party,
  domain=~stripe.network`. The `domain=~stripe.network` part reads as a carve-out
  so Stripe's own frame can reach Stripe. It only works when `domain=` is judged
  against the frame. Judged against the tab, the carve-out is dead and the call is
  blocked. I believe that
  call is Stripe's fraud signal [memory], the kind of request whose blocking
  breaks checkouts.

Keep the frame-relative reading. The lists are written for it. I believe Brave
judges against the tab host [memory]. I did not verify that, or what it costs there.

**A shape test for unlisted trackers.** Among the 2,028 third-party requests that
still load, I flagged an identifier-looking query value, a tiny image, a POST
beacon or a tracking-style path. It flagged 329 (16%), and 245 of those are on
none of the four lists. The most frequent flagged hosts are
`img-s-msn-com.akamaized.net` (46), `www.bing.com` (30), `www.google.com` (14) and
`m.media-amazon.com` (13), mixed with a few real ad-tech hosts such as
`cs.startappnetwork.com` (9) and `vop.sundayskyads.com` (3). A test like this can
queue candidates for review. It cannot block [measured].

**An India coverage gap.** I expected the Indian pages to be worse covered. They
are not, in this sample. The 11 Indian pages had 54.3% of their made requests
blocked against 50.9% on the 21 global pages, and 4.8% of requests that still
load were on an independent list against 6.4%. IndianList is among the 17 lists.
The sample is small and was fetched from outside India, so this does not settle
the question. A crawl from Indian connections does [measured].

## The moves

### 1. One shared, immutable index

Port ZIndex into the service: 28-byte rule records in a read-only mapped file,
built on a thread-pool task when a list changes, one per browser process.
Allowlist and enable switch stay per profile. Keep the old engine behind a flag
for one release and run the differential test in CI permanently.

The cost of honesty here. Brave's crate already uses this shape, one FlatBuffer
blob with serialize and deserialize. This move catches up with a known design and
adds a little. It removes a deficit. It is not a headline. Chromium also has an
in-tree matcher, `url_pattern_index` [memory]. Check whether it can carry these
semantics before maintaining a matcher of our own.

Gate: probe parity on every build, mean at least 3× lower than the current engine
on the real-traffic corpus, and RSS showing one mapping when the Private Workspace
opens. Not yet in the index: cosmetic and scriptlet data, and the syntax in move 2.

### 2. Close the syntax gap, cheapest first

Brave's parser keeps these rules and ours drops them [measured]. The `badfilter`
rows that show up in a one-line-at-a-time test are an artefact, because
Zephyrus does implement `$badfilter` [repo], so they are not counted.

| Rules | Option | Work |
|---|---|---|
| 198 | `match-case` | one flag on the rule record |
| 144 | `to=` | a second domain list, same code as `domain=` |
| 24 | `method` | a bit mask |
| 241 | `removeparam` | strip the parameter before the request leaves; also gives a built-in tracking-parameter list |
| 216 | regex patterns | RE2, which runs in linear time and is already in Chromium |
| 47 | `csp` | inject the header on the response, which the proxy already sees |
| 595 | `redirect`, `redirect-rule` | surrogate resources; uBlock's are GPLv3 [memory], so write or license our own |
| 66 | other, including 10 `rewrite` | with the response work |

The first three are 366 rules for small changes. Do them first.

Keep the lead we already have. Ours parses 3,066 `$popup`, 171 `$denyallow` and 11
`$popunder` rules that Brave's crate drops. Zephyrus has a real pop-up path,
`ShouldBlockPopup`, that uses them [repo]. What Brave's browser does about
pop-ups beyond the crate is not covered here.

Gate: `real_traffic/parity_report.py` shows zero under "Brave keeps, Zephyrus
drops". Anyone can rerun it.

### 3. Verified rules

This is the idea I would bet the next quarter on. A list maintainer adds a rule
when someone reports an ad or a breakage. No step checks, for each rule, what it
does to the page. Zephyrus can add that step, because it owns a browser that an
agent can drive.

The loop:

1. **Crawl.** Zep or a headless browser loads the top pages, global ones and
   Indian ones from Indian connections, in a clean profile with nothing blocked,
   and records the request graph with the script that started each request.
   `crawl.js` in the kit is a toy version. It covered 32 pages in minutes and
   records frames only.
2. **Propose.** Candidates come from sources that each find different things.
   Independent lists give 66 hosts on this sample. The shape test gives a noisy
   second queue. A request-graph classifier in the AdGraph and WebGraph style can
   run in shadow mode.
3. **Test.** Reload each page with only the candidate rule applied. Reload it
   twice with nothing applied as a control, so you know how much the page changes
   on its own, which for news pages is a lot. For scale, bbc.com made 264 requests
   in my crawl and 419 in a single rerun minutes later. Compare what a person would see:
   viewport screenshots, the accessibility-tree text, console errors and failed
   requests. Where a task exists, such as log in, add to cart, reach the payment
   step or play the video, let Zep run it and compare success.
4. **Accept.** A rule ships only if every comparison stays inside the control's
   noise. Everything else goes to a person with the evidence attached, before and
   after screenshots and the diff, or is marked never-block.
5. **Ship** through the signed bundle (move 5), staged, with a kill switch.

The oracle must prove itself before it may accept anything. Seed it with rules
known to break pages, such as `||cdn.cookielaw.org^` and `||m.stripe.com^`, plus
twenty more taken from the lists' own unbreak files. It has to reject all of them.
An oracle that waves one through is not allowed to auto-accept. Publish its catch
rate with every release.

Why this can beat Brave. Rule lists are written by people and corrected after
users report breakage [memory]. The closest automated effort I found is Brave's
Cookiecrumbler, which generates cookie-notice rules with a model and has humans
review them [sourced]. I found nothing that tests a network rule's effect on the
page before it ships [unverified]. A verified pipeline lets Zephyrus ship the safe
part of those 66 hosts that other blockers avoid, and cover the Indian long tail,
without inheriting the breakage. It also produces a number nobody else publishes:
breakage per release.

Risks. Sites serve bots differently, and 18 of my 50 pages did not load. The
oracle will miss things, so staged rollout, a per-site kill switch and move 4 are
part of the design, not extras. Crawling has terms-of-service exposure. You become
the list maintainer. Rules derived from GPL and CC BY-SA lists carry those
licences. `THIRD_PARTY_DATA.md` covers Disconnect (CC BY-NC-SA, not for a product
without a commercial licence) and not the other independent lists I tried. Check
StevenBlack, AdGuard and Peter Lowe terms before shipping anything derived from them.

### 4. A Shield you can interrogate

When a page looks wrong, one button. Shield reloads with half of the rules that
fired on that page switched off, asks "better?", and repeats. A page in my sample
had 149 requests blocked on average, so the culprit is found in at most 8
reloads. Today the choice is to turn Shield off for the whole site. The result
reads "Rule X from list Y broke this page, allowed for this site". It needs the
matcher to return the rule that fired. ZIndex has the rule id, and a side table
can map it to a source line. The same hook gives a request log with provenance
and, opt-in and local, the breakage reports that feed move 3.

### 5. One signed bundle

A build job fetches the upstream lists, shows a person the diff, compiles the
index plus the cosmetic and scriptlet data, signs it with Ed25519 (the entity
dataset already does this), and publishes to one host. The browser checks the
signature, refuses an older version than it holds, and honours a kill switch.
That ends the four-host trust chain and gives staged rollout. The verified rules
from move 3 ride in the same bundle. The bundle carries the upstream licences.

### 6. Publish the benchmark

The kit is the start. With the lists and the crawl cached, `run_real_traffic.sh`
reproduces every real-traffic number in this note in about 35 seconds. Extend it
with recorded pages (Web Page Replay) so the network is identical for every
browser, uBlock Origin's engine as a fourth contender (its core is on npm
[memory]), a breakage score from a fixed Zep task suite, CPU and RAM per tab, and
every metric where Zephyrus loses. Publishing the losses is what makes the wins
believable.

## Order and gates

| # | Work | Size | Gate |
|---|---|---|---|
| 0 | Z-01 base, Z-02 updater | L | unchanged from the earlier notes |
| 1 | Shared index | L | probe parity; mean 3× lower; one mapping across profiles |
| 2 | Matcher options, `removeparam`, `csp` (366 + 241 + 47) | M | those causes at zero in `parity_report.py` |
| 3 | Signed bundle | M | browser rejects unsigned and older bundles |
| 4 | Oracle with the seeded-fault test | M | rejects every seeded breakage |
| 5 | Pilot on the 66 hosts, then an Indian-connection crawl that records script initiators | M | evidence bundles reviewed; numbers published |
| 6 | Bisect and rule provenance | M | culprit rule found in at most 8 reloads on a test set |
| 7 | RE2 regex, surrogate resources (216 + 595) | L | `parity_report.py` at zero |
| 8 | Public benchmark | M | a third party reproduces it |

## What this will not do

- It will not win the anti-adblock arms race. Sites will keep detecting blockers.
- It will not remove ads that arrive inside the video stream. Server-side insertion
  cannot be separated at the network layer.
- It will not make "blocks more than Brave" a safe claim. Without a breakage number
  next to it, that claim invites a reviewer to find the one site you broke.
- It does not turn on the shape test or the independent lists by default. Both are
  review queues until move 3 exists.

## What I did not do

- Run anything inside Chromium, or with the real `GURL` and public-suffix list.
- Measure breakage. I measured what gets blocked, not what stops working. This is
  the biggest gap, and move 3 exists to close it.
- Model script chains. Blocking a script also stops the requests it would have
  made. The log has no initiators, so "still loads" and the 66-host pool are upper
  bounds. A crawl over the DevTools protocol records them.
- Read Brave's browser code. Its Shields behaviour beyond the crate is unknown to me.
- Test a version of the crate newer than 0.13.5.
- Crawl from India, while logged in, or past a bot wall. 18 of 50 pages were lost.
- Compare against uBlock Origin, AdGuard, Firefox or Safari.

## Sources

- [adblock-rust, Brave's engine](https://github.com/brave/adblock-rust), 0.13.5 read from crates.io
- [Brave: Cookiecrumbler](https://brave.com/privacy-updates/33-cookiecrumbler/)
- [AdGraph](https://arxiv.org/pdf/1805.09155) and [WebGraph](https://usenix.org/conference/usenixsecurity22/presentation/siby)
- [EasyList and EasyPrivacy](https://easylist.to/), [uBlock Origin uAssets](https://github.com/uBlockOrigin/uAssets)
- [StevenBlack hosts](https://github.com/StevenBlack/hosts), [Peter Lowe's ad server list](https://pgl.yoyo.org/adservers/), [AdGuard DNS filter](https://github.com/AdguardTeam/AdGuardSDNSFilter), [Disconnect](https://github.com/disconnectme/disconnect-tracking-protection), used for research only
