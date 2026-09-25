# Production readiness audit — 25 September 2026

Scope: the 819 files Zephyrus changes on top of Chromium, the `out/Release`
configuration, and a 3-minute net-log of a fresh profile on the Release binary.
Inherited Chromium mechanisms were checked for regressions, not re-audited.
Full write-up (private artifact): https://claude.ai/artifact/5vux3x6uBrzWJk2NBY2JXt

**Release status: NOT READY — blockers remain** (Z-01, Z-02, Z-03, Z-05).

| ID | Sev | Status | Finding |
|---|---|---|---|
| Z-01 | P0 | open | Base 151.0.7922.171 is out of support. Stable was 155.0.8059.12; 5 later M151 releases and 30 across M152–M155 are missing. |
| Z-02 | P0 | open | No browser auto-updater. Only the filter lists update; fixes cannot reach installed users. |
| Z-03 | P1 | open | Binaries and installer unsigned (no signing step in `package_installer.py`). |
| Z-04 | P1 | **fixed** | Any third-party filter list could use uBO `trusted-*` scriptlets (response rewrite, cookies, clicks) on any named site. Now only uAssets sections and built-in rules may. |
| Z-05 | P1 | open (probable) | Safe Browsing inactive: no Google API key; zero `safebrowsing.googleapis.com` requests in 3 min. |
| Z-06 | P3 | open | Installer unpacks to predictable `%TEMP%\zephyrus_setup_<pid>`, ignores a pre-existing dir, writes then executes by path. |
| Z-07 | P3 | open | Inter forced for every web generic font family: trivially fingerprints Zephyrus. |
| Z-08 | P3 | open | Fingerprint seed not delivered to dedicated/shared workers. |
| Z-09 | P3 | **fixed** | Dead `OpenPrivateTabIn` path put OTR tabs in a regular window. Deleted. |
| Z-10 | P4 | **fixed** | Dev switches shipped in release. Now compiled only with `zephyrus_dev_switches` (GN arg, default off in official builds); packager refuses a build with them unless `--allow-dev-switches`. |
| Z-11 | P4 | open | Filter-list fetch triggers a NEL report to `a.nel.cloudflare.com`. |
| Z-12 | P4 | open | Fork commit message names 151.0.7913.0; real base is 151.0.7922.171. |

## Verified not vulnerable

- Renderer→browser Mojo (`PrivacyReporter`, `FingerprintSeedHost`,
  `ScriptletHost`): origin from the RenderFrameHost, inputs bounded,
  `DocumentService` lifetime, reports deduplicated per site/surface.
- No sandbox or site-isolation changes; agent kernel is `kService`-sandboxed.
- Ad-block URL-loader proxy decides at request start, never reads bodies.
- Agent model/vision endpoints are loopback-only (`net::IsLocalhost`).
- Entity dataset: Ed25519 verified before parse; unsigned path `CHECK_IS_TEST`.
- `chrome://privacy` pages escape every runtime string.
- Private Workspace: primary OTR profile in its own `Browser`.
- No telemetry: a fresh install contacts only the filter-list hosts and
  Chromium's component updater; `X-Client-Data` removed; no variations fetch.

## Not present

VPN, UPI, DigiLocker, AI autofill, native messaging host: none exist in the
codebase. Each needs its own threat model before it is built.

## Unverified

Runtime process integrity levels; fuzzing/sanitizer runs; 100-tab and 12-hour
soak; screen-reader and high-contrast passes; multi-monitor/mixed DPI;
macOS/Linux.
