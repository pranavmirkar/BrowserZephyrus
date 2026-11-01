# Shield: how to go past what other browsers ship

Research note, 1 November 2026. Not a decision record. Nothing here is built.

How to read the evidence labels:

- **[repo]** read in this repository today.
- **[measured]** I ran a script on the real files in this session.
- **[sourced]** from the linked source. Many were search snippets, not full reads.
- **[unverified]** a claim I could not confirm. Do not repeat it publicly until checked.

Limits of this research: brave.com was blocked from this session's network, so
every Brave claim comes from search snippets and third-party coverage. I did not
read the full text of the 2025 canvas paper or the 2026 GPU side-channel paper.
"No other browser has this" is a claim about absence. I can say I found no
evidence of it, not that it is false.

## Short answer

Do not try to out-block Brave on lists. Two things are open:

1. **A shield that proves what it did.** Every browser shows counts of blocked
   trackers. None I found shows which *identifier* went to which company, for
   example "a SHA-256 of your email was sent to a data broker, and Shield
   removed it". Zephyrus already has the pieces (request hook, entity
   attribution, CNAME cache, fingerprint-attempt detection, workspaces).
2. **Shield and the agent as one system.** Zep reads the page Shield has already
   cleaned, and the same page is the main prompt-injection surface for any
   agent. No browser I found measures that link.

Before either claim is credible, the engine has parity debt (section 1) and the
Chromium base is out of support (audit item Z-01). Fix those first, because a
world-first claim from a browser that skips `:has-text` rules will be picked apart.

## 1. Where Shield stands today

### Structure

- The matcher is in-process C++ (`adblock/adblock_filter_engine.cc`), with
  token-bucketed rules, a step budget and a wildcard cap **[repo]**.
- Extensions are limited by Manifest V3. uBlock Origin Lite is built entirely
  on declarativeNetRequest and its author says that choice costs features
  beyond what MV3 itself requires **[sourced: uBOL FAQ via search]**. Zephyrus
  has no such ceiling: it can rewrite responses, edit headers and run code
  before page scripts. This is the real advantage, and it is currently
  under-used.

### Coverage, measured against the lists the updater downloads

I downloaded the lists `zephyrus_adblock_updater.cc` fetches and counted what
the parser would drop **[measured, 2026-11-01]**.

| List | Network rules | Unusable | Main reason |
|---|---|---|---|
| EasyPrivacy | 56,416 | about 0% | none |
| uBO `privacy.txt` | 1,458 | **36.8%** | `removeparam`, `urlskip` |
| uBO `filters-general.txt` | 966 | **33.2%** | regex rules |
| uBO `quick-fixes.txt` | 222 | 20.7% | `replace`, regex |
| uBO `filters-2025.txt` | 498 | 14.9% | surrogate `redirect`, `redirect-rule` |
| uBO `unbreak.txt` | 1,947 | 13.3% | `redirect`, `redirect-rule`, `cname` |
| uBO `badware.txt` | 4,695 | 7.0% | `to`, `ipaddress` |

Across all lists, 98.7% of network rules by count are usable. The count is
misleading: the unusable rules are concentrated in the lists that carry the
newest and most privacy-specific syntax. Across lists the dropped syntax is:
`redirect-rule` 453, surrogate `redirect` 305, `removeparam` 274, regex about
500, `urlskip` 221, `replace` 89, `csp` 80.

Other gaps:

- **Procedural cosmetic filters are skipped entirely** ("skipped for now" in
  `adblock_cosmetic_engine.cc`) **[repo]**. Occurrences in the lists:
  `:has-text` 561, `:upward` 170, `:remove` 85, `:matches-css` 25, `:xpath` 22,
  `:matches-attr` 16. That is roughly 900 operator uses. Separately, 521 rules
  use the `#?#` procedural syntax; the two counts overlap.
- **Scriptlets:** 63 implemented, covering 96.1% of 12,101 scriptlet rules
  **[measured]**. Largest gaps: `trusted-replace-argument` 78, `href-sanitizer`
  76, the `json-edit` family, `xml-prune` 19. The `trusted-*` ones are blocked
  on purpose since audit item Z-04.
- **Shipped snapshot (since removed from the repo on main):** EasyList of 3
  July 2026 with 110,310 usable network rules and 26,015 element-hide rules
  **[measured]**. The deep-dive uses freshly downloaded lists instead.

### Fingerprinting

- Per-origin, per-session seed from a CSPRNG, never on disk, derived per
  surface so surfaces cannot be correlated **[repo]**. This design is sound.
- A "fingerprint attempt" detector: 3 or more distinct surfaces within 5
  seconds, or a canvas read of something never painted **[repo]**.
- Open audit items: Inter is forced for every web generic font family, which
  makes Zephyrus identifiable (Z-07); the seed is not delivered to workers
  (Z-08) **[repo]**.

## 2. What the field does

- **Brave.** Randomises ("farbles") canvas, audio and other APIs. Press and
  Brave coverage describe new GPU, WebGL and WebGPU protections starting in
  August 2026, and a decision to drop its separate strict fingerprinting mode
  because it broke sites **[sourced, details unverified]**. Brave's engine
  (`adblock-rust`) implements uBlock syntax extensions, redirects and scriptlets
  **[sourced]**. Brave also built Cookiecrumbler, which uses open LLMs to find
  cookie notices and propose rules, with human review before publication
  **[sourced]**. A late-2025 forum thread says it did not block notices for some
  users **[sourced, unresolved]**.
- **Firefox.** Total Cookie Protection partitions storage per site and is on in
  Standard mode **[sourced: Mozilla]**. Fingerprinting defences started in
  Private Browsing and Strict mode; I could not confirm they are now default.
- **Safari 26.** Reported to add noise to fingerprinting-relevant APIs in all
  browsing modes **[sourced, single source]**.
- **Chrome.** Kept third-party cookies and retired the Privacy Sandbox APIs
  (Topics, Protected Audience, Attribution Reporting and others) in 2025
  **[sourced, dates conflict between sources]**. Bounce-tracking mitigations
  exist for users who block third-party cookies **[sourced]**. Chrome's
  default posture protects less than the others, which is why a fork can lead
  here.
- **Trackers are moving first-party.** CNAME cloaking was measured as growing,
  with cookie leaks to the tracker on 95% of the sites embedding one
  **[sourced: PoPETs 2021]**. A later NDSS paper found authentication cookies
  sent to cloaked third parties **[sourced]**. Google Tag Gateway serves Google
  tags from the advertiser's own domain. Whether it defeats blockers is
  disputed: one hands-on test said no, another analysis says blockers can match
  on request parameters **[sourced, vendor-heavy]**.
- **Video.** YouTube's server-side ad insertion merges ads into the stream.
  AdGuard says extensions have the least room to respond and there is no
  reliable fix **[sourced: AdGuard, TechRadar]**.

## 3. What research shows is possible but not shipped (as far as I found)

| Work | Claim | Caveat |
|---|---|---|
| AdGraph (S&P 2020) | Graph of HTML, requests and JS behaviour; 95.33% agreement with human-curated filter labels. Under obfuscation that fully bypassed list rules, precision/recall fell by at most 7% / 19% | Research prototype; accuracy is relative to list labels |
| WebGraph (USENIX Sec 2022) | Classifies by information flow (stores an ID, shares an ID); adversary success cut from near-perfect to about 8% | Built on forked OpenWPM; not a shipped product |
| FP-Inspector (S&P 2021) | Detects fingerprinting scripts by AST and runtime features; 99.9% accuracy, found 26% more scripts than lists, halved breakage | Same |
| PERCIVAL (USENIX ATC 2020) | Image classifier inside the render pipeline; 96.76% agreement with EasyList, 4.55% render overhead in Chromium | Visual only; a complement, per its authors |
| Canvas defences (WWW 2025) | Authors demonstrate attacks on randomisation-based canvas defences, including Brave's, and state no fully deployable defence exists | I read only the abstract |
| WebGPU-SPY (2024), TIMESLICE-SANDWICH (USENIX Sec 2026 preprint) | A page can infer *which other sites you visit* from GPU cache or scheduling behaviour; 92.84% top-1 on Chrome in the newer paper | WebGPU-SPY is Intel iGPU only; I did not read the newer paper |

I found no evidence that any of the machine-learning blockers ships on by
default in a mainstream browser. Brave hosts the AdGraph and PERCIVAL research
pages, but I could not read them.

## 4. Bets, ranked

### Bet 1. Leak ledger and identifier-flow protection (strongest, build this)

**What.** Per page and per Workspace, show which *identifiers* reached which
entity, not how many requests were blocked. Examples: a first-party cookie value
copied into a third-party request; a device ID; a hash of the user's email. In
block mode, strip the identifier from requests to advertising and tracking
entities.

**Why it is plausible.** Email hashing is the cookieless identifier. LiveRamp
and UID2 documentation lists SHA-256, MD5 and SHA-1 of the lowercased,
trimmed address as accepted inputs **[sourced: vendor docs]**. I found no
independent measurement of how often hashed emails appear in browser traffic,
so the prevalence is **[unverified]**, and measuring it is the first deliverable.
Detection is cheap and exact: hash the addresses the browser knows about (as
SHA-256, MD5, SHA-1, and base64/hex/URL-encoded forms) and scan outgoing query
strings and bounded bodies.

**Fits the codebase.** The request hook exists
(`zephyrus_adblock_proxying_url_loader_factory.cc`), entity attribution exists
(`privacy/entity_resolver.cc`), the event pipeline exists
(`privacy_event_sink`). New parts: a local token set, a scanner, and a report row.

**Rules for doing it safely.** Store only hashes of the user's identifiers, on
this machine, never uploaded. Default to *report*. Block only toward entities
the resolver classes as advertising or tracking, because the same hash goes to
legitimate sign-in providers.

**Effort.** Medium. **Risk.** Breakage in sign-in and checkout flows; false
sense of completeness (the ledger only sees what it can decode).

### Bet 2. Close parity first, in an order that also feeds Bet 1

1. `$removeparam` and `urlskip` (495 rules, and most of `privacy.txt`'s loss).
   Do it before the request leaves, plus a built-in tracking-parameter list.
2. Regex rules through RE2, which is linear-time and already in Chromium
   (about 500 rules). The existing wildcard guard shows the team cares about
   DoS, and RE2 removes that concern for this class.
3. Surrogate resources for `redirect` and `redirect-rule` (758 rules) so pages
   do not break when a script is blocked.
4. A procedural cosmetic engine for `:has-text`, `:upward`, `:xpath`, `:remove`,
   `:matches-css`, `:matches-attr` (about 900 rules).
5. `$replace` and `$csp`, which is where an in-engine design beats any
   extension (169 rules).

**Proof, not claims.** The `privacy/tools/breakage_corpus` harness already runs
a corpus with and without protections. Extend it to publish usable-rule
coverage per list and breakage per release. That is the number to put in front
of users.

### Bet 3. Behaviour classifier, shipped first in shadow mode

**What.** Train offline on request-graph and runtime features in the
AdGraph/WebGraph/FP-Inspector style, labelled with the filter lists, and ship a
very small model that scores *unlisted* third-party requests. Start with no
blocking at all: log where the model and the lists disagree.

**Why start in shadow.** The research reports accuracy against list labels, not
against users' breakage. A false positive on a payment script is expensive.
Shadow mode also produces the thing you can use at once: a ranked list of
probable tracker domains to review and add to a Zephyrus-maintained list.

**Effort.** Large (crawler, labels, model, on-device inference). **Risk.** The
labels are only as good as the lists, so the model inherits their gaps.

### Bet 4. A fingerprint defence that survives the published attacks

1. Fix Z-07 and Z-08 first. Forcing Inter makes Zephyrus users a distinct group.
2. Get the 2025 canvas paper, replay its attacks against
   `fingerprint_randomization_browsertest.cc`, and keep them as regression
   tests. Randomisation that is predictable is worse than none, because it also
   announces itself.
3. Where randomisation can be detected, add *uniformity*: report one of a few
   common values (screen size buckets, a fixed font set) so users blend.
4. Treat the GPU side channel as a cross-site history leak. Cheap steps:
   disable WebGPU in the Private Workspace and in cross-origin frames, and
   coarsen GPU timing. Whether that stops the attacks is open until the papers
   are read.

**Effort.** Medium. **Risk.** Uniformity and API removal break some sites and
games; make them per-Workspace.

### Bet 5. Payload-signature rules for first-party tracking paths

Domain lists miss `/metrics/` and `/gtm/` paths served from the site's own
origin. Add rules that match the *shape* of a request (parameter names and
value formats of the common analytics and conversion endpoints) on same-site
paths, using the `removeparam`/regex machinery from Bet 2. Small and cheap once
Bet 2 lands. It is the practical answer to CNAME cloaking and tag gateways.

### Bet 6. Per-Workspace Shield contracts

Workspaces are real storage partitions, which no mainstream browser pairs with
an in-engine blocker. A Banking Workspace could allow only first-party scripts
and an allowlist; a Reading Workspace could be maximal on ads and consent
walls. This is more product than breakthrough, but it is cheap and unique.

### Bet 7. Agent-assisted list maintenance (Cookiecrumbler-style)

Zep drives a real browser and sees the page. Let it crawl popular Indian sites
from Indian connections, flag unblocked ads, banners and breakage, and propose
rules for a human to review. This is India-first coverage that global lists
thin out. Brave's tool shows the approach works with review; its reported
failures show the review step is not optional.

### Bet 8. Video ads, as an experiment

Server-side insertion cannot be separated at the network layer. The engine can
still manage the player: detect the ad state through the media element and
player events, then mute, speed up or skip. Expect a continuing race, and
check terms-of-service exposure before shipping. Put it behind a switch and do
not market it.

## 5. Suggested order

1. **Foundations.** Z-01 (rebase), Z-07, Z-08, then Bet 2 items 1 to 3.
2. **Differentiator.** Bet 1, with a prevalence study of hashed emails first.
3. **Credibility.** Published coverage and breakage numbers per release.
4. **Zep link.** Section 6 and the agent report.
5. **Then** Bets 3, 4, 5 and 6, in whichever order the study results point to.
6. **Experiments.** Bets 7 and 8.

## 6. Where Shield and Zep meet

- Observation already records when an element is covered by something drawn
  on top, and names cookie banners as the commonest cause of a failed click
  **[repo: observation.h]**. Shield removing the banner removes the failure.
  Test it: run the 12-task benchmark and a set of noisy pages with Shield on
  and off, and report the difference.
- Ads are third-party content the model should not trust. Third-party iframe
  text should reach the model labelled by origin and, where Shield judged it an
  ad, not at all.
- The accessibility tree drops AX-invisible subtrees, but text hidden by
  position, clipping or colour is not necessarily AX-invisible **[repo, not
  tested]**. Hidden text has been used against shipping AI browsers
  **[sourced: Brave's Comet disclosure]**.

## 7. Risks to state plainly

- Z-01: the base is out of support. Tracker protection on an unpatched engine is
  a poor trade for users.
- A large part of this is a maintenance commitment (lists, surrogates, models),
  not a one-off build.
- Lists, models and rules are only as good as their breakage handling. The
  value is a browser that does not break pages.

## Sources

- [Percival: Making In-Browser Perceptual Ad Blocking Practical With Deep Learning (USENIX ATC 2020)](https://www.usenix.org/conference/atc20/presentation/din)
- [AdGraph: A Graph-Based Approach to Ad and Tracker Blocking](https://arxiv.org/pdf/1805.09155)
- [WebGraph: Capturing Advertising and Tracking Information Flows for Robust Blocking (USENIX Security 2022)](https://usenix.org/conference/usenixsecurity22/presentation/siby)
- [Fingerprinting the Fingerprinters (FP-Inspector)](https://arxiv.org/pdf/2008.04480)
- [The CNAME of the Game (PoPETs 2021)](https://petsymposium.org/popets/2021/popets-2021-0053.php)
- [WebGPU-SPY: Finding Fingerprints in the Sandbox through GPU Cache Attacks](https://arxiv.org/pdf/2401.04349)
- [TIMESLICE-SANDWICH, USENIX Security 2026 preprint](https://www.usenix.org/system/files/conference/usenixsecurity26/sec26_prepub_kim-hodong.pdf)
- [Canvas fingerprinting defences, WWW 2025 abstract](https://chatpaper.com/paper/129963)
- [Detecting Anti Ad-blockers in the Wild (PoPETs 2017)](https://www.petsymposium.org/popets/2017/popets-2017-0032.php)
- [Brave: Fingerprinting Protections wiki](https://github.com/brave/brave-browser/wiki/Fingerprinting-Protections)
- [Brave: Cookiecrumbler](https://brave.com/privacy-updates/33-cookiecrumbler/)
- [Brave adblock-rust](https://github.com/brave/adblock-rust)
- [Mozilla: Firefox fingerprint protections](https://blog.mozilla.org/en/firefox/fingerprinting-protections/)
- [Mozilla: Total Cookie Protection](https://www.firefox.com/features/total-cookie-protection/)
- [Google Privacy Sandbox shutdown coverage (Search Engine Land)](https://searchengineland.com/google-officially-shuts-down-privacy-sandbox-463561)
- [Chrome bounce tracking mitigations](https://privacysandbox.google.com/protections/bounce-tracking-mitigations)
- [uBlock Origin Lite FAQ](https://github.com/uBlockOrigin/uBOL-home/wiki/Frequently-asked-questions-%28FAQ%29)
- [AdGuard: YouTube server-side ad insertion](https://adguard.com/en/blog/youtube-server-side-ad-insertion.html)
- [TechRadar: YouTube SSAI complicates ad blocking](https://www.techradar.com/pro/youtubes-server-side-ad-insertion-complicates-ad-blocking-efforts)
- [Google Tag Gateway explainer](https://ppc.land/google-tag-gateway/)
- [LiveRamp: hashing identifiers](https://docs.liveramp.com/connect/en/hashing-identifiers.html)
- [UID2: LiveRamp integration tips](https://unifiedid.com/docs/guides/integration-liveramp-tips)
- [Brave: indirect prompt injection in Perplexity Comet](https://brave.com/blog/comet-prompt-injection/)
