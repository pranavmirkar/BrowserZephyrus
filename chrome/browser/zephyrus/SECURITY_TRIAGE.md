# Snyk Code triage — 2026-08-20

First Snyk Code (SAST) run against the published overlay
(`github.com/pranavmirkar/BrowserZephyrus`). **39 findings: 0 HIGH, 33 MEDIUM,
6 LOW.** One fixed, 38 assessed not-exploitable with reasons below.

Written down because a scanner that reports 33 false positives every run stops
being read, and the fix for a false positive is a recorded verdict, not silence.

## Fixed

**SSRF — `fetch_entity_dataset.py:90`** (was MEDIUM).
`urlopen()` is now restricted to `https://`. The URL is developer-supplied (the
built-in `PIN`, or `--pin-file`), so this was never attacker-controlled, and the
SHA-256 pin already stops a substituted URL injecting content. But `urlopen`
also speaks `file://` and `ftp://`, so a pin file naming `file:///etc/passwd`
would have been read and hashed rather than refused. Verified: with `--strict`
it now exits 1; without it, it degrades to no dataset as designed.

## Not exploitable — Chromium self-ownership (33 MEDIUM)

Every "Missing Release of Memory after Effective Lifetime" is the same false
positive: Snyk does not model Chromium's self-owning object idioms, in which
`new Foo(...)` with no stored pointer is correct and required.

| pattern | who deletes it | examples flagged |
|---|---|---|
| `content::DocumentService<T>` | itself, with the document or on mojo disconnect | `ZephyrusPrivacyReporterHost`, `ZephyrusFingerprintSeedHost`, `ZephyrusAdblockScriptletHost` |
| `content::RenderFrameObserver` | itself, in `OnDestruct()` when the frame goes | `FingerprintSeedAgent`, `ZephyrusContentSettingsAgent`, `ScriptletAgent`, and ~20 upstream observers in `chrome_content_renderer_client.cc` |
| `views::WidgetObserver` + `delete this` | itself, in `OnWidgetDestroyed` | `ToggleCloseRecorder` (`zephyrus_bubble_style.cc`) |

Each was checked individually, not waved away: `ToggleCloseRecorder` really does
`delete this` in `OnWidgetDestroyed`, and the two Zephyrus mojo hosts really do
derive from `DocumentService`. Note ~24 of the 33 are in **upstream Chromium
files** the fork merely touches — they are not Zephyrus code at all.

If one of these ever becomes a real leak it will be because someone changed the
base class, so the check worth having is a review rule ("does this still
self-own?"), not a scanner.

## Not exploitable — developer build tooling (5 LOW)

**Path traversal ×3** — `build_entity_artifact.py:410,555`,
`fetch_entity_dataset.py:173`. A command-line argument flows into `open()`.
These are build scripts run by a developer against their own machine; `--out-dir`
writing where the developer said is the feature. There is no privilege boundary
here to cross.

**Insecure XML parser ×3** — `privacy_strings_test.py:145,214,295`.
`xml.etree.ElementTree.parse` on `.grd`/`.xtb` files from the repo itself, i.e.
trusted input. The finding also states it does not apply to Python 3.11+; this
tree runs 3.14.

## Worth knowing about this scan

Snyk Code covers the **386 files in the overlay** — the fork's own changes. It
does not and cannot cover `third_party`, V8 or the network stack, because the
overlay does not contain them. That is where the fork's real CVE exposure lives:
as of this run the base is Chromium `151.0.7913.0` (25 June 2026) against a
current stable of `152.0.7977.54`, with 40+ stable releases and 61+ distinct
CVEs published since. **No scan of this repo will ever show that.** Keeping the
base current is a separate, larger job — see the rebase discussion.

Snyk Open Source finds nothing here at all, and that is correct rather than
broken: the overlay has no dependency manifest, because its only third-party
code comes from the Chromium tree it is applied to.
