# Zephyrus third-party data manifest

Every externally-sourced **dataset** Zephyrus ships or downloads, with its licence, how it reaches
the user, and what that licence obliges us to do. Required by the Privacy Intelligence spec's §4 as
amended on 2026-08-11.

This covers *data*, not code. Third-party source code obligations are tracked by Chromium's own
`//third_party` README/LICENSE mechanism and are out of scope here.

**Why a manifest at all:** filter lists and tracker datasets are overwhelmingly copyleft or
non-commercial. Which obligations apply depends on *how the artifact is distributed* — downloaded at
runtime, shipped beside the binary, or compiled into it — and that distinction is invisible in the
code. Recording it here keeps a later refactor from quietly changing the answer.

## Status legend

- **VERIFIED** — licence text read at the source, on the date shown.
- **UNVERIFIED** — believed correct, not yet confirmed. Must not be relied on for a release.

---

## 1. Filter lists (ad/tracker blocking)

| Dataset | Source | Licence | Status |
|---|---|---|---|
| EasyList | easylist.to | GPLv3 **or** CC BY-SA 3.0 (dual) | UNVERIFIED |
| EasyPrivacy | easylist.to | GPLv3 **or** CC BY-SA 3.0 (dual) | UNVERIFIED |
| Fanboy Cookiemonster | secure.fanboy.co.nz | EasyList project terms (dual, as above) | UNVERIFIED |
| uBlock Origin uAssets filters | github.com/uBlockOrigin/uAssets | GPLv3 | UNVERIFIED |

**Distribution: downloaded at runtime as data.** `ZephyrusAdblockUpdater` fetches these over HTTPS
into the profile directory; they are parsed at runtime and never linked, embedded, or compiled into
`chrome.dll`.

**This is the load-bearing fact.** GPLv3 obligations attach to distributing a *combined work*.
Fetching a list at runtime and interpreting it as data does not create one, in the same way a
browser rendering a GPL page is not a derivative of it. **Vendoring any of these into the build, or
generating a compiled artifact from them, changes the analysis and requires re-review before it
ships.**

`lists/zephyrus_filters.txt` is copied next to the binary at build time by the `filter_list` GN
target. **ACTION REQUIRED:** confirm its provenance. If it is a snapshot of EasyList/EasyPrivacy
rather than Zephyrus-authored rules, it *is* shipped alongside the binary and the analysis above
does not cover it.

## 2. Entity attribution (Privacy Intelligence)

| Dataset | Source | Licence | Status |
|---|---|---|---|
| DuckDuckGo Tracker Radar | github.com/duckduckgo/tracker-radar | CC BY-NC-SA 4.0 | **VERIFIED 2026-08-11** |
| Disconnect entity list (`entities.json`, `services.json`) | github.com/disconnectme/disconnect-tracking-protection | CC BY-NC-SA 4.0 | **VERIFIED 2026-08-11** |
| Ghostery TrackerDB (alternative) | github.com/ghostery/trackerdb | Reported CC BY-NC-SA 4.0 | UNVERIFIED |

**Both verified options carry an explicit commercial-licensing offer**, which changes this from a
dead end into a negotiation:

- Tracker Radar: *"The Tracker Radar data is licensed under the Creative Commons
  Attribution-NonCommercial-ShareAlike 4.0 International License. If you'd like to license the list
  for commercial use, please reach out."*
- Disconnect: *"Please email support@disconnect.me if you'd like to license the lists for commercial
  use."*

So the NonCommercial clause is not a permanent blocker on shipping Zephyrus commercially — it is a
mail to the dataset owner. It IS a blocker on doing so silently.

Not legal advice: this records the licence text as published by each project. A commercial release
should have the actual LICENSE file reviewed by whoever signs off on Lazarus's legal position.

Entity-attribution datasets are **systematically non-commercial**, and EasyPrivacy carries no
ownership data at all — so "just use a permissive one" is not an available option. Three obligations
follow, and the architecture already reflects them:

1. **NonCommercial.** CC BY-NC-SA 4.0 forbids commercial distribution. Acceptable for a college
   project; a blocker the moment Lazarus distributes Zephyrus commercially. **This must be resolved
   before any paid or commercially-sponsored release.**
2. **ShareAlike.** A build-time lookup table is an *adaptation* of the data. This is why the table
   ships as a **separate signed data file, mmap'd at runtime, never compiled into the executable** —
   fusing an adaptation into the binary makes the boundary between the dataset and our code
   unarguable in the wrong direction. The separation costs nothing in performance.
3. **Attribution.** Requires visible credit. **ACTION REQUIRED:** add dataset name, source and
   licence to the About screen before the dashboard ships.

`EntityResolver` is an interface over a documented on-disk artifact format with one converter per
source dataset, and **the feature is required to work with no dataset at all** (bare domains, every
other capability intact). Swapping datasets is a converter change, not a rewrite.

## 3. Commercial distribution — planned, not immediate

**Answered 2026-08-11 (Appendix B.2): Zephyrus is a college project today, with commercial
distribution intended later.** That makes the licence split below a schedule, not a hypothetical,
and the two dataset families behave completely differently:

| | Commercially distributable? | Obligation |
|---|---|---|
| Filter lists (EasyList family, uBO) | **Yes** — GPLv3 and CC BY-SA 3.0 both permit commercial use | Attribution + licence text. Applies **now**, see below. |
| Entity datasets (Tracker Radar, Disconnect, Ghostery) | **No** — CC BY-NC-SA 4.0 forbids it | Must obtain a commercial licence, or ship without entity names |

**The filter lists are not a commercial blocker.** Copyleft is not non-commercial; GPL and CC BY-SA
both allow selling. What they require is attribution and licence text, and for GPL, availability of
the corresponding source of the work distributed.

**The entity datasets are the blocker**, and only for the commercial release. Two exits, both
already supported by the architecture: obtain a commercial licence (DuckDuckGo explicitly invites
the request), or ship the commercial build with no dataset — bare domains, every other capability
intact, because `EntityResolver` treats "no dataset" as a first-class implementation rather than an
error path. Start the licensing conversation **before** the release, not at it: the timeline is
someone else's to control.

**Present-tense obligation, independent of all that:** `adblock/lists/zephyrus_filters.txt` is a
verbatim EasyList snapshot that already ships next to the binary in tester builds. CC BY-SA 3.0
attribution is owed **now**, not at commercial release. The cheapest correct fix is to copy
EasyList's licence text alongside the list in the `filter_list` GN target and credit it in About.

## 4. Obligations checklist

- [x] Verify Tracker Radar's licence text at source. CC BY-NC-SA 4.0, verified 2026-08-11.
- [x] Verify Disconnect's entity list terms. Also CC BY-NC-SA 4.0, verified 2026-08-11.
- [ ] **Before the commercial release** (now scheduled, not hypothetical): obtain a commercial
      entity-dataset licence, or confirm the build ships with no dataset.
- [ ] **Now:** ship EasyList's licence text beside `zephyrus_filters.txt` and credit it in About —
      the tester builds already redistribute it.
- [ ] Confirm the provenance of `adblock/lists/zephyrus_filters.txt`.
- [ ] Add dataset attribution to About before the dashboard ships.
- [ ] Keep the entity table out of the binary — a separate signed file, always.
- [ ] Re-review this manifest before any commercial distribution.

Last updated: 2026-08-11.

## Entity dataset: fetched at build time, never committed

**Nothing about the dataset is in this repository, and nothing about it may be**
(spec 4.2). The source data and the converted artifact are both CC BY-NC-SA
works; this repo is published publicly under the project's own licence, and
committing either would place NC-licensed, ShareAlike-encumbered data inside a
tree distributed under a different one. Deleting it afterwards does not help —
git history keeps it.

What lives here (all original work, project licence):

| File | Role |
|---|---|
| `privacy/tools/fetch_entity_dataset.py` | Downloads a dataset pinned by URL **and SHA-256**, then converts and signs it. |
| `privacy/tools/build_entity_artifact.py` | Converter: source dataset → artifact format. One per dataset. |
| `privacy/entity_artifact.h` | The format specification. |
| `privacy/entity_signing_key.h` | The **public** half of the signing key. |
| This file | The licence manifest. |

Gitignored: `zephyrus_entities.dat` and any source dataset.

### Pinned entity dataset

| Field | Value |
|---|---|
| Dataset | DuckDuckGo Tracker Radar |
| Source | https://github.com/duckduckgo/tracker-radar |
| Pinned file | `build-data/generated/domain_map.json` at commit `6253f5a053513120c61ad8221dc30a0e2cdbfeb9` |
| SHA-256 | `e5fa4c4d…dd08` (full value in the fetch script) |
| Upstream date | 2026-07-27 |
| Licence | CC BY-NC-SA 4.0, Copyright 2020 Duck Duck Go, Inc. |
| Obligations | Attribution (shown from the artifact header), ShareAlike, **NonCommercial** |
| Produces | 38,365 domains / 19,130 entities / ~780 KB artifact |

**Why a raw file at a commit rather than a release archive.** The repository is
~12 GB, so a build step cannot clone it — but it can fetch one 10 MB file.
GitHub's auto-generated `/archive/` tarballs are also not guaranteed
byte-stable and have changed compression before, which would break a SHA-256
pin for reasons unrelated to the data; `raw.githubusercontent.com` at an
explicit commit is content-addressed and genuinely stable.

**Known limitation: no categories.** `domain_map.json` carries owners but not
categories — Tracker Radar only records those in the 38k per-domain files, and
publishes no consolidated equivalent. Every entry therefore resolves with
`category = UNKNOWN`. Company attribution ("Google", "Meta") works; the
Advertising/Analytics grouping does not. Recovering it needs a shallow clone of
the full repo in CI, which is a build-infrastructure decision, not a converter
change.

**NonCommercial is the live constraint.** Fine while Zephyrus is distributed
free. A paid tier, bundled search deal, or commercial distribution requires a
licence conversation with DuckDuckGo first — they explicitly invite those. The
fallback if it cannot be agreed is shipping with no dataset: bare domains, every
other capability intact (spec 4.1).

### Building it

```
python3 chrome/browser/zephyrus/privacy/tools/fetch_entity_dataset.py     --out-dir out/Release --signing-key "$ENTITY_SIGNING_KEY" --strict
```

Release and CI pass `--strict`, so a silent loss of attribution cannot ship.

### Verifying the release payload (do this before shipping an installer)

```
python3 chrome/browser/zephyrus/privacy/tools/verify_release_payload.py --out-dir out/Release
```

`--strict` only guards the FETCH. It cannot guard the step after it: if the
fetch is never run at all, `create_installer_archive.py` silently skips the
missing file (it only mentions it in verbose mode) and the installer is built
without a dataset, reporting success. The browser then loads the null resolver
and every tracker in the UI reads as unattributed -- with no error anywhere.

The verifier closes that gap. It checks the three shipped data files exist and
verifies the artifact's real Ed25519 signature against the public key parsed out
of `entity_signing_key.h` -- parsed rather than copied, so it cannot drift from
the key the browser actually trusts. An artifact signed with the WRONG key is
the worst case: it looks correct everywhere and the browser refuses it on every
load.
Developer builds omit it: the script exits 0 on any failure and the browser
starts with the null resolver showing bare domains. **A licensing-encumbered
download must never be able to break a contributor's build.**

The pin (`PIN` in the fetch script) and the manifest row below are updated
together, in one change.

### Signing

The artifact is Ed25519-signed and **verification runs on every load**,
including a bundled artifact (spec 4.4.2). That is deliberate: the path is
exercised from day one rather than first exercised the day remote delivery
starts depending on it. An unsigned, tampered, or wrong-key artifact is
rejected and the browser falls back to bare domains.

The private key is **not in this repository**. It currently lives outside the
tree; a release pipeline must hold it in secret storage and pass it as
`--signing-key`. Rotating it means shipping one release carrying both public
keys before dropping the old one, or every installed artifact fails at once.

### Staleness

Trackers rotate domains, so an old dataset does not merely lose coverage — it
reports "no trackers detected" on pages that are tracked, which is a false §2
claim. The artifact header therefore carries the **source publication date**
(not the build date), and `EntityResolver::Freshness()` buckets it:

| Age | Behaviour |
|---|---|
| < 60 days | Normal |
| 60–180 days | Show the dataset date; suppress unqualified coverage language |
| > 180 days | "no trackers detected" must become "no known trackers detected — tracker data last updated {date}" |

Thresholds live in one place, `kDatasetFreshDays` / `kDatasetStaleDays` in
`privacy/entity_artifact.h`. An artifact with no publication date counts as
stale, never as fresh. `chrome://privacy-internals` always shows the date and
computed age.

### Attribution

The dataset name, version, source and licence are read out of the artifact's own
header and displayed by `chrome://privacy-internals`; the dashboard footer and
About must do the same. Sourcing the credit from the shipped bytes rather than a
hardcoded string is what stops it drifting from what actually shipped.

### Runtime lookup

Datasets are keyed by registrable domain, so a request to
`securepubads.g.doubleclick.net` resolves via `doubleclick.net`. Without that
fold almost nothing attributes, and a page full of trackers reads as clean.

### Install locations

First that exists wins:

| Location | Purpose |
|---|---|
| `<user-data-dir>/ZephyrusEntities/zephyrus_entities.dat` | Where an updater would install. |
| `<dir containing chrome.exe>/ZephyrusEntities/zephyrus_entities.dat` | Shipped alongside the build. |
| `--zephyrus-entity-dataset=<path>` | Explicit override, for testing. |
