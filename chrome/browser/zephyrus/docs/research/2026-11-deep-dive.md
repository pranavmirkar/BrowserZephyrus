# Zephyrus deep dive: the Shield engine, memory, trust, and what to build

Research note, 1 November 2026. Companion to `2026-11-shield-next-level.md` and
`2026-11-zep-next-level.md`. The prototypes and every script behind the numbers
are in `chrome/browser/zephyrus/research/` (`run_all.sh` reproduces them).

Evidence labels: **[measured]** I ran it on the real sources, **[repo]** read in
this repository, **[sourced]** from the linked source, **[memory]** from my
own knowledge and not re-checked here, **[unverified]**.

## The short version

1. **The Shield matcher is the weakest part of Shield, and it is measurably
   behind Brave's.** On identical lists and identical requests, the shipped
   engine costs about 20 µs per request, Brave's open-source engine about 9 µs.
   The shipped engine also holds 52 MB of heap, builds a complete copy for
   every profile (the Private Workspace and Guest included), parses 224,000
   lines on every start, and frees it all on the UI thread at every list update.
   [measured, [repo]]
2. **A compiled, memory-mapped index fixes all of that.** My prototype
   (`ZIndex`) matches in **2.4 µs**, loads in **1.8 ms** with no heap, is one
   10.6 MB file mapping that every profile shares, and returned **0 mismatches
   in 652,640 differential probes** against the shipped engine. A blob that
   `Load()` accepts after random corruption cannot crash it, under ASan and
   UBSan. It is about 3.8× faster than Brave's engine on this corpus.
   [measured]
3. **The same shape fixes a trust problem.** The browser downloads the rules
   from four third-party hosts and runs the JavaScript they contain on web pages.
   A Zephyrus-built, signed bundle replaces that with one trust root. [repo]
4. **A Safe Browsing replacement that never sends a URL anywhere is feasible.**
   76.8 MB of open phishing data became a 4.9 MB index with 2.8 µs lookups and 0
   false positives in 3 million benign-shaped URLs. [measured]
5. **Hashed-email tracking can be detected and shown to the user.** The
   scanner found every encoding I tested in 0.7 µs per typical request.
   [measured]
6. **Honest limits.** None of this has run inside Chromium. The benchmark uses
   stand-ins for `GURL` and the public-suffix list and a synthetic request mix.
   "Better than the other browsers" is not proven for the product as a whole; it
   is shown only for the matcher, against one competitor's engine.

## 1. Method

The ad-block engine sources (`adblock_filter_engine.cc`, the cosmetic and
scriptlet engines, `adblock_list_util.cc`) are compiled **unmodified** with
small stand-ins for `base::`, `GURL` and the public-suffix helpers
(`research/shim/`). Inputs are the lists the updater downloads, fetched on
1 November 2026: 17 files, namely EasyList, EasyPrivacy, thirteen uBlock Origin
files, Fanboy's cookie list and IndianList. That is 224,366 lines (8.2 MB) of which the
Zephyrus parser accepts 126,520 network rules.

Requests are a deterministic synthetic corpus of 20,000: about 90% ordinary
subresources, 10% shaped like blocked ones. That exercises the code paths; it is
not recorded traffic. Correctness probes are different: 632,640 URLs *derived
from every accepted rule* so that the rule is expected to fire, plus mutations.

Everything is Linux, g++ 13, one machine. Ratios inside one process on one
corpus carry over; absolute microseconds do not.

## 2. What the shipped engine costs

| | Measured | Where |
|---|---|---|
| Parse, network / cosmetic / scriptlet | 183 / 100 / 65 ms | pool thread, every start and every list update |
| Heap per engine instance | 33.0 / 9.7 / 9.1 MB = **51.8 MB** | plus 8 MB of list text in flight |
| Per request, clean traffic | **20 to 23 µs** mean, 69 to 79 µs p99 | browser process, UI thread |
| Destroy on hot-swap | 60 + 20 + 14 ms | UI thread, at each list update |

Facts from the code, not the benchmark **[repo]**:

- The service lives on the UI thread and the URL-loader proxy asks it about
  every request there (`zephyrus_adblock_service.h`).
- `ZephyrusAdblockServiceFactory` gives the regular, Guest and off-the-record
  profiles each their own instance ("Block in incognito too, using its own
  instance"). Opening the Private Workspace therefore builds a second full
  engine: another 52 MB and another 350 ms of parsing.
- The cosmetic and scriptlet engines each re-split the same text.

Where the 20 µs goes **[measured]**:

| Step | µs per request |
|---|---|
| Lowercase, tokenise, bucket lookups | 1.0 |
| Scanning 435 "untokenized" block rules linearly, every request | about 10.5 |
| Evaluating about 210 candidates reached through common tokens | about 9 |

The candidates come from a design fault in keyword choice: a rule is filed
under its *longest* bounded token, so `.com/ad/` is filed under `com`. That
token is in 87% of URLs and its bucket puts about 98 rules in front of nearly
every request. `www`, `static`, `images` and `api` do the same.

## 3. Against Brave's engine, on the same input

Brave's `adblock-rust` 0.13.5 (crates.io) built from the same text and run on
the same 20,000 requests **[measured]**:

| | Zephyrus as shipped | Brave adblock-rust | ZIndex prototype |
|---|---|---|---|
| Mean per request | 20 to 23 µs | 8.9 µs | **2.4 µs** |
| p99 | 69 to 79 µs | 31.7 µs | **5.7 µs** |
| Memory | 52 MB heap (33 MB network only) | +13.5 MB built; 10.2 MB serialised; +9.7 MB after loading it | **10.6 MB mapped, 0 heap** |
| Start-up | 348 ms parse | 195 ms build, 8.7 ms deserialise | **1.8 ms** |

Reading it fairly:

- Brave's engine also holds cosmetic data; ZIndex covers network rules only.
  The memory comparison favours ZIndex slightly more than it should.
- Brave's crate keeps its rules in one FlatBuffer with `serialize()` and
  `deserialize()` **[sourced: crate source]**. It is the same architecture as
  ZIndex. **This is catching up with a known design, not an invention.** The
  measured edge over it comes from key selection, an initiator-domain index and
  a bitmap in front of the n-gram tables, on this corpus.
- Its documentation says a file produced and read by the same crate version
  skips FlatBuffer verification after a checksum, and treats local rewriting of
  the file as out of scope **[sourced]**. ZIndex validates every offset on every
  load and was fuzzed (section 4), which suits a browser whose profile
  directory other software can write to.
- **The two engines agree.** All 20,000 corpus decisions match. Across 246,912
  rule-derived probes they agree on 99.87%; 255 are blocked only by Zephyrus and
  78 only by Brave **[measured]**.

What the 333 disagreements were, from reading rule text:

- Syntax Zephyrus does not implement: regex rules and `redirect=` surrogate
  resources, where Brave blocks or redirects and Zephyrus allows.
- `domain=nishankhatri.*` entity domains and the `$doc` alias, where Zephyrus
  follows uBlock Origin and Brave differs. I triaged only a handful and did not
  establish which engine is right.
- **Three cosmetic lines the network parser wrongly admitted as network
  rules** (`#@?#` and `#%#` spellings). That is a real bug and is fixed in this
  branch with a test.

## 4. ZIndex

### How it works

- Rules are 28-byte records. Patterns live in one string arena. There are no
  per-rule heap objects. (The shipped `FilterRule` is 128 bytes plus its
  strings and vectors.)
- Each rule is placed in exactly one index, in this order:
  1. its **rarest** whole-token key, never one from a list of ubiquitous tokens
     (`com`, `www`, `static` ...);
  2. else its rarest literal 3, 4 or 5-gram, behind a 8 KB bitmap per length so
     almost every probe is one L1 load;
  3. else the initiator domain, for rules like `*$script,3p,domain=a.com|b.*`
     (311 rules; most of what was left of the linear scan);
  4. else a residual list. **One rule** lands there.
- A 64-bit signature of the whole tokens a rule requires is a free early reject.
- Matching is a faithful port of the shipped matcher, including its step budget.
- The blob is built once when a list changes, not at start-up.

### What each idea bought (mean µs per request, interactive runs)

Shipped 29 → n-gram index for untokenized rules 19 → initiator-domain index and
signatures 12 → rarest-token keys 6.9 → entity domains and bitmap 2.4.
Those figures are from development runs on the earlier list set; the final
head-to-head is 19.7 µs against 2.4 µs in one process **[measured]**.

### Evidence it is correct

- **652,640 probes, 0 mismatches**, against the real engine: 20,000 from the
  corpus and 632,640 derived from every accepted rule (exact, first-party
  initiator, other resource type, one-character mutation, added query), 483,415
  of them blocked. Document-exception lookups: 623 probes, 0 mismatches.
- **Hostile input.** `Load()` checks every offset a record or table can reach,
  alignment, and that each probing table has an empty slot (without that check a
  corrupt table makes a lookup loop forever; I found and fixed this while
  writing the fuzzer). 2,500 mutated blobs, covering bit flips, header flips,
  garbage runs and truncation: 1,860 rejected, 640 accepted and then *used* for
  400 matches each. No crash, no undefined behaviour, no hang, under ASan and
  UBSan.
- A request cannot influence the index, so hash flooding is not a concern.

### What is not done

- It is not wired into the service. The port needs a Chromium build.
- Only the rule types the shipped parser accepts are indexed. `$removeparam`,
  regex, redirect surrogates and procedural cosmetics are still to be written
  (section 8).
- Chromium has an in-tree n-gram flat-buffer matcher (`url_pattern_index`, used
  for the subresource filter and declarativeNetRequest) **[memory]**. Check
  whether it can carry these semantics before maintaining a matcher of our own.

### Integration plan

1. Port to Chromium idioms (`base::span`, `CHECK`, `base::MemoryMappedFile`).
2. Build in a thread-pool task when a list updates and write it next to the list.
3. One immutable index per browser process, shared as `scoped_refptr<const ...>`.
   Allowlist and enable switch stay per profile. That removes the per-profile
   copies.
4. Keep the old engine behind a flag for one release and run the differential
   test in CI forever.
5. Gate: `adblock_matcher_perftest.cc` at least 5× faster and probe parity.

## 5. Trust: stop running other people's JavaScript

The updater downloads 10 list URLs (plus the files uBlock's list includes) from
`easylist.to`, `raw.githubusercontent.com`,
`secure.fanboy.co.nz` and `easylist-downloads.adblockplus.org`, checks only that
the result looks like a filter list, and merges it. Scriptlet rules (`+js(...)`)
from those files run as JavaScript in the page's main world. Audit item Z-04
restricted the powerful `trusted-*` scriptlets to uBlock's own sections, which
was the right fix. The remaining ones (`set`, `aopr` and others; about 9,400 scriptlet rules
in this list set) are still page-altering code from four hosts **[repo, measured]**.

**Proposal:** a build pipeline (CI) fetches the upstream lists, shows the diff
for review, compiles the index and the cosmetic/scriptlet bundle, signs it
with Ed25519 (the entity dataset already does this), and publishes to one host.
The browser verifies the signature, refuses an older version than it holds
(rollback protection), and honours a kill switch. Staged rollout then becomes
possible. A side effect: one update host to contact instead of four.

Cost: you become the list maintainer, answerable for breakage. Licensing: the
lists are GPLv3 or CC BY-SA, and a compiled index is an adaptation of them, so
the bundle carries those licences and attribution while the browser code stays
under its own. `THIRD_PARTY_DATA.md` covers this; get it checked before
shipping a bundle **[repo]**.

## 6. Local threat blocking (`ThreatIndex`)

Audit item Z-05: Safe Browsing is inactive (no API key). Chrome's standard mode
asks Google about hash prefixes of URLs **[memory]**; a fork without the key has
nothing.

The prototype keeps known-bad hosts and URLs in a sorted array of 48-bit hashes
with a directory, tested with Safe Browsing-style expressions (host suffixes ×
path prefixes). The hash is **SipHash-2-4 with a random key chosen when the blob
is built**, so nobody can submit a URL to a feed that collides with a legitimate
site before the key exists.

Built from the Phishing.Database community feed (MIT licence) **[measured]**:

| | |
|---|---|
| Input | 392,178 domains + 789,054 URLs, 76.8 MB of text |
| Index | **4.88 MB**, 4.2 bytes per entry, built in 0.33 s |
| Lookup | 2.8 µs for a clean URL (parse plus about 30 expressions), 0.8 µs for a hit |
| Recall | 112,722 of 112,722 sampled listed URLs; 392,075 of 392,178 listed domains found with an unseen path |
| False positives | 0 in 3,000,000 benign-shaped URLs; calculated rate about 1e-7 per URL |
| Popular sites | 0 of 10 flagged |

The 103 domains not found are malformed feed rows (`%20mandrillapp.com`, an IP
with a query string glued on), not index misses.

What this is **not**: a replacement for Google's coverage or its live
classification. It shows the size, speed and privacy shape. Real coverage needs
several feeds, a daily delta, and a warning page with a way through.

## 7. Seeing identifier leaks (`IdLeakScanner`)

The idea, from the previous report: show the user which *identifier* reached
which company, and optionally strip it. The prototype finds the user's own
addresses in an outgoing request: plain, percent-encoded, and as MD5, SHA-1 or
SHA-256 in hex or base64 (upper and lower case, padded or not), including when
glued to a longer token, and the Gmail-normalised form
**[measured]**:

- 15 of 15 cases pass, including four negative controls (one hex digit changed,
  a random 64-hex session id, an unrelated address, a SHA-1 of someone else).
- 0.68 µs for a typical analytics URL; 84 MB/s on hex-heavy random data; 0
  false positives there.
- Setup hashes the identifiers once; scanning does no hashing and no allocation.

Design rules for the real feature: store only hashes, on this machine; scan only
requests to third parties the entity resolver classes as advertising or
tracking; default to *report*; block as an opt-in per Workspace, because the
same hash also goes to legitimate sign-in providers. How often hashed emails
actually appear in browser traffic is **unmeasured**; the first deliverable
should be a prevalence study on the top 1,000 sites.

## 8. Other findings from reading the code

1. **Scriptlet runtime.** Every document that gets any scriptlet is sent the
   full 55 KB library and compiles it: 1.2 ms (2.5 ms with first run) per
   document; with a V8 code cache 0.1 ms **[measured]**. A page with ten frames
   pays it ten times. Sending only the scriptlets a document uses (typically a
   few of about 50) is simpler and also shrinks the Mojo message.
2. **Unsupported syntax.** In `privacy.txt` 37% of network rules are unusable
   (`removeparam`, `urlskip`), in `filters-general.txt` 33%; about 900 procedural
   cosmetic selectors are skipped **[measured, previous report]**.
3. **A parser bug, fixed here.** Three cosmetic lines were being indexed as
   network rules. Ten separator spellings are now all recognised, with a test.
4. **Already well done.** Memory settings are tuned (spare renderer off, Memory
   Saver on, a two-hour discard delay). Per-origin fingerprint seeds are
   ephemeral and unlinkable. The privacy database encrypts domain columns and
   uses an HMAC for lookup columns, which is stronger than Chromium's History
   file **[repo]**.
5. **Preloading is off.** That is a privacy win and a speed loss. A local,
   same-origin-only speculation (prerender the next page of the site the user is
   already on, never cross-site) would recover some speed without telling a new
   party anything. Unmeasured idea.
6. **One question to verify.** Chrome's App-Bound Encryption protects cookies
   from same-user malware through a system service; per-user installs may not
   get it **[memory, unverified]**. The installer is per-user. Check what protects
   Zephyrus's cookie key.

## 9. How to prove "better" in public

A claim that holds up is one anyone can rerun:

- Record 50 to 100 pages (global and Indian) with Web Page Replay so the
  network is identical for every browser.
- Run Zephyrus, Chrome, Brave, Firefox and Edge on one machine. Measure time to
  largest paint, CPU time, private working set per tab, bytes and requests,
  energy where Windows exposes it.
- Measure *breakage* with Zep: a fixed task suite that passes or fails on each
  browser. A blocker that blocks everything and breaks pages should not score.
- Publish method, archives and raw numbers, including every metric where
  Zephyrus loses. That is what makes the others believable.

## 10. Order of work

| # | Item | Size | Gate |
|---|---|---|---|
| 1 | Port ZIndex; one shared index per process | L | perftest ≥5× faster, probe parity |
| 2 | Scriptlets per document, with code cache | S | compile cost per document under 0.2 ms |
| 3 | `removeparam`, `urlskip`, RE2 regex, redirect surrogates, procedural cosmetics | L | usable-rule share in `privacy.txt` above 95% |
| 4 | Signed bundle pipeline | M | browser accepts only signed, newer bundles |
| 5 | ThreatIndex with several feeds and a warning page | M | measured recall and false-positive rate on a held-out set |
| 6 | Prevalence study, then the leak report | M | numbers for the top 1,000 sites |
| 7 | Public benchmark | M | reproducible by a third party |

Z-01 (an out-of-support Chromium base) and the missing updater (Z-02) come
before all of it. A faster blocker on an unpatched engine is the wrong trade for
users.

## 11. What I did not do

- Run anything inside Chromium, or measure real traffic, real `GURL` or the real
  public-suffix list.
- Evaluate Chromium's own `url_pattern_index` as an alternative matcher.
- Read the 2025 canvas-fingerprinting paper in full, or benchmark against uBlock
  Origin. Brave's engine was the only other engine tested.
- Establish which engine is right in the 333 disagreements beyond the handful
  named in section 3.

## Sources

- [adblock-rust (Brave's engine)](https://github.com/brave/adblock-rust), version 0.13.5 read from crates.io
- [Phishing.Database (MIT)](https://github.com/Phishing-Database/Phishing.Database)
- [uBlock Origin uAssets](https://github.com/uBlockOrigin/uAssets), [EasyList](https://easylist.to/)
- The two companion reports in this folder carry the competitor and research sources.
