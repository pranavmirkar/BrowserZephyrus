# Zephyrus — session handoff (written 2026-09-30)

Read this first if you are a new Claude session picking the project up. It is the
state of the world at the moment it was written; where it disagrees with the code
or a newer note, the code wins. No secrets are in this file and none should be
added: keys live only where section 9 says.

## 0. How to get the rest of the context

| What | Where |
|---|---|
| Persistent memory (~100 notes, indexed) | `C:\Users\prana\.claude\projects\D--chromium\memory\MEMORY.md` — read the index, then the notes you need |
| Full transcript of the last long session | `C:\Users\prana\.claude\projects\D--chromium\9ed02d5e-f664-4c2f-b662-e2e988ed6674.jsonl` |
| Design docs | `chrome/browser/zephyrus/docs/` — `agentic-harness-v2.md`, ADRs `0001`–`0006` |
| Vocabulary | `chrome/browser/zephyrus/CONTEXT.md` (terms only) |
| Security triage / production audit | `SECURITY_TRIAGE.md`, `PRODUCTION_AUDIT_2026-09.md` (same folder) |
| Scratch, test scripts, traces | `D:\zeph_perf\` (capture scripts, replay files, fixtures, logs) |

## 1. Who and what

- **User:** Pranav (`pranavmirkar@gmail.com`). Solo founder-engineer. Windows 11, AMD Ryzen 9 6900HS, 16 threads, ~23 GB RAM.
- **Product:** **Zephyrus** browser (company **Lazarus**, not yet registered as of July 2026). A **Chromium 151 fork** at `D:\chromium\src`, India-first.
- **Differentiator being built:** an **agentic browser** — an in-browser agent that operates real web pages, with a mascot that is its visible cursor, plus push-to-talk **voice** commands. Ambition stated by the user: "more powerful than Claude Code and Codex" for the browser, fast, accurate, wide task coverage, and **asking permission only for payments and sign-in**.
- **Role the user asked me to play:** Senior Agent/Harness Developer and startup founder; "this being your own product". Treat it as a real product, not a demo hack.

## 2. The hackathon (goal and seriousness)

- The user has a hackathon **imminent** (notes of 2026-09-28 said "in 2 days", so it is on or about **2026-09-30 to 2026-10-01** — confirm).
- **Deliverable:** browser + agentic harness + mascot + voice, working out of the box for **judges**.
- **Judges must be able to start using the agent and voice commands immediately, with nothing to configure.** That drove the "demo proxy" work (section 5).
- **Seriousness:** very high. The user repeatedly asked for production-class quality ("be a bit more serious and ambitious"), and a passing demo is not the bar: correctness, speed and safety are.
- **Event (confirmed 2026-09-30 from the page):** **AssemblyAI - Voice Agent Hackathon** on lablab.ai, https://lablab.ai/ai-hackathons/assemblyai-voice-agent-hackathon
  - Runs **September 1-30, 2026**, fully online, so **the deadline is the day this handoff was written**. The exact cutoff time and timezone are NOT on the page: ask the user / check the page's countdown.
  - Theme: "the fastest path to a working voice agent". **Everything must be built with AssemblyAI** (our voice input uses its sync API, model `universal-3-5-pro`).
  - Prizes: $10,000 ($5,000 cash + $5,000 AssemblyAI credits). Judging criteria are not published on the page; the lablab rule book is at https://lablab.ai/hackathon-rules.
  - **lablab submission package (from lablab's own guidelines; verify on the submit form):** project title (max 50 chars); short description (max 255); long description (min 100 words); cover image PNG/JPG 16:9; **video presentation MP4, max 5 min, under 300 MB**; **slide deck PDF**; **public GitHub repository**; **demo application URL** (Streamlit / Replit / Vercel) that judges can use online.
  - **Consequence:** judges evaluate online. A Windows installer is not a demo they can open, so we need (a) a demo video, (b) a hosted page they can open, and (c) a public repo. The Vercel proxy project is already a place to host a page.
  - Drafts of the text fields are in `SUBMISSION_DRAFT.md` (same folder).

## 3. Environment and build rules (hard-won)

- Repo: `D:\chromium\src`. Build dir for shipping: **`out\Release`** (official, non-component). `out\Default` is the older dev build. **Test performance on `out\Release`.**
- Every build command must be `cd /d/chromium/src && autoninja -C out/Release <targets>` — the Bash tool's cwd silently resets to `D:\chromium`. **`autoninja` can exit 0 on failure; check the log for "finished successfully" and compare binary vs source timestamps** (stale-binary false pass has happened).
- Targets: `chrome`, `mini_installer`, `zephyrus_agent_unittests`, `zephyrus_privacy_unittests`. Installer packaging: `chrome/installer/zephyrus_setup/tools/package_installer.py` (refuses a build with `zephyrus_dev_switches = true` unless `--allow-dev-switches`; the user wants that switch kept `true` for the tester build).
- Never kill Chrome broadly; kill only PIDs you started (the user's real Chrome is in use). No full-screen captures — use PrintWindow scripts (`D:\zeph_perf\still_capture.ps1`, `popup_capture.ps1`, `agent_task_capture.ps1`).
- The agent's shell is MSIX-redirected for AppData: **never verify an install from it**; the user must launch. Its login state (e.g. `vercel whoami`) is NOT the user's terminal state.
- Bash-tool traps: heredocs with quotes/`\n`/`\0` mangle (use the Write tool or python scripts; a real NUL byte in C++ broke a build); recursive grep over `chrome/` times out (use the Grep tool on a narrow path); `sleep` polling is blocked (use Monitor).
- Dev/verification switches in the browser: `--zephyrus-test-compact=<mode>`, `--zephyrus-test-agent-task="<task>"` (`a|||b` sends messages in sequence), `--zephyrus-agent-trace=<file>`.

## 4. Architecture in one page

- **Agent kernel:** Rust (`chrome/services/zephyrus_agent/kernel`, cxx bridge) running in its own sandboxed utility process (ADR 0001). C++ `TaskLoop` drives it; mojom `AgentKernel`. Contract: `chrome/browser/zephyrus/agent/schemas/tools.v1.json` (v1.4.0, 26 tools).
- **Providers** (ADR 0004): Anthropic / OpenAI-compatible / Gemini adapters live in the **kernel**; the browser is an authenticated pipe (`agent/model_transport.cc`) that adds the key last, fixes the host, allowlists paths/headers, uses the system network context.
- **Policy** (`kernel/src/policy.rs`, ADR 0005): the agent **asks only for payments and sign-in**. Payment verbs always Ask; outbound verbs Ask only if the task did not request them; password/card typing is Deny and points to `task.handoff` (user does it). Approval lifts an Ask, **never a Deny**.
- **Acting on pages:** observe with accessibility (never act with it), act with a synthesized input pipeline: an ordered input queue in `BrowserToolSurface` (`ui/views/frame/zephyrus_agent_tool_surface.*`), human-paced pointer paths (Bezier + min-jerk, Fitts timing), scroll via wheel events, page-identity guards.
- **Mascot:** `MascotRig` (implicit-Euler springs) + `ZephyrusAgentMascotOverlay`, a click-through layer. Home = bottom-right of the page card. It only travels where it can arrive in time; otherwise the action happens from home.
- **Memory** (ADR 0006): working (kernel) / conversation (RAM, per workspace) / long-term (profile file "Zephyrus Agent Memory", per workspace, none for Private). `memory.remember` is judged by provenance in Rust. A reply to the agent's question continues the task instead of starting a new one.
- **Voice:** `agent/voice_input.*`, Windows waveIn 16 kHz mono, AssemblyAI sync API, mic button in the agent panel.
- **Privacy/adblock/workspaces:** separate large subsystems (Privacy Intelligence phases 1–4, cosmetic + network adblock, workspaces as StoragePartitions, Private Workspace as OTR). See the memory index.
- **UI:** Material 3 Expressive overhaul (Nothing OS look retired). Tonal containers behind toolbar buttons are drawn by `ZephyrusGlassPill` in `toolbar_view.cc`.

## 5. What was done in the last session (2026-09-29 → 09-30)

All built and verified unless noted.

1. **Mascot as the agent's cursor** (browser-wide overlay), smaller, default at page-card bottom, richer expressions.
2. **Speed:** ~12.2s → ~4.6s per 5 actions (settle constants, `ObserveQuick`, effort=low, batching up to 8 cloud tool calls, each still policy-judged). Model latency (2–7 s per reply) now dominates.
3. **Permissions** reduced to payment + sign-in, with `task.handoff`; broader tools; starter chips; cost footnote.
4. **Memory architecture** (see ADR 0006) — fixed "agent forgets the previous message".
5. **Settings bubble** shrunk to fit small screens (scrolls).
6. **Appearance toggle:** Settings → Appearance → "Tonal containers behind toolbar buttons" (pref `zephyrus.appearance.m3_containers`, default on, live repaint). Both states captured and checked. The toggle click itself was not driven; the pref was seeded.
7. **Hackathon keys — the demo proxy.** A key inside a binary is always extractable, so the browser holds **no provider key**:
   - `chrome/browser/zephyrus/demo_proxy/` is a small **Vercel** project holding the Anthropic and AssemblyAI keys as sensitive env vars. It only accepts an expiring HMAC token (`zd1.<expiry>.<sig>`), allows a model allowlist, `max_tokens` ≤ 16000, no streaming, 3 MB / 4 MB body caps, ~30 req/min/IP per instance (best effort), fixed upstream hosts.
   - The build embeds only `{proxy_url, demo_token}` from `D:\zephyrus-keys\demo_keys.json` via GN arg `zephyrus_bundled_keys_file` (line is in `out/Release/args.gn`). `agent/tools/gen_bundled_keys.py` refuses provider-key-shaped values.
   - Client: `agent/bundled_keys.*`; `model_settings.cc` (`UsesBundledKey` decides proxy vs provider from where the key came from; a saved key is never sent to the proxy and a failed-to-decrypt saved key never becomes the token); `VoiceInput::StopAndTranscribe(key, endpoint, done)`.
   - Cloud consent is allowed by default in demo builds until the user touches the per-workspace switch; Private stays off.
   - **Deployed:** `https://zephyrus-demo-proxy.vercel.app` (Vercel project under `pranavmirkars-projects`). Live checks passed: no token → 401 from our own function; health 200 with both keys present; a real Claude call returned "proxy ok"; the voice route accepted a synthetic WAV.
   - Setup/teardown scripts: `demo_proxy/scripts/setup_demo.ps1` (asks for both keys hidden, deploys, mints a 14-day token, writes config) and `kill_demo.ps1` (removes the project; then revoke provider keys).
   - Proxy tests: `node --test test/proxy.test.js` in `demo_proxy/` (14 pass).
   - Bugs found and fixed on the way: proxy `max_tokens` cap was 4096 (browser sends 16000); PowerShell 5.1 treated Vercel CLI stderr as an error; the setup script wrote the JSON with a BOM (generator now reads `utf-8-sig`, script writes without BOM).

## 6. Where things stand right now

- **Final build done (2026-09-30 01:53 IST):** `chrome` + `mini_installer` built with the demo config; `chrome.dll` holds the proxy URL but neither the token nor any `sk-ant-` string in plaintext.
- **Verified on it:** a fresh profile with nothing entered ran "Open the More information link" through the LIVE proxy to real Claude: it clicked and finished in 2 steps / 13 s / 19k tokens (one step was "wasted" because the model chose no action). Capture: `D:\zeph_perfinal.png`.
- `out/Release/args.gn` has `zephyrus_bundled_keys_file = "D:/zephyrus-keys/demo_keys.json"` and `zephyrus_dev_switches = true`.
- The provider keys were created in dedicated workspaces with spend caps (verify they exist).
- **Not yet verified:** a real SPOKEN command (only a synthetic WAV reached AssemblyAI), the packaged installer and a clean install by the user, the lablab submission assets (repo, video, slides, cover, demo URL).

## 7. Next steps, in order

**DONE and verified in `out/Release` (2026-09-30, second session): the three voice features.**
- M1 mascot bubble: the agent's status, answer and Allow/Deny question sit above the mascot (`zephyrus_mascot_bubble.*`, wired from the panel). Verified: a spoken task's answer appeared in the bubble.
- M2/M3 "Hey Zep": opt-in, only while the window is active (`zephyrus_hands_free.*`, engine in `agent/hands_free.*`, `voice_dsp.*`, `voice_lock.*`). On-device MFCC + DTW against the user's own enrolled recordings; nothing goes to AssemblyAI until the phrase matches. A small red light on the mascot while the microphone is open. "Hey Zep, stop" cancels a running task. Ctrl+Shift+Space = talk (press again to send), Ctrl+Shift+Comma = mute.
- M4 Voice Lock: named voices (record / rename / delete / test) in "Hey Zep and voices" (`zephyrus_voice_setup.cc`, opened from the agent settings or a chip in the panel's empty state), strictness setting. Voiceprints are features only, OSCrypt-encrypted in `<profile>/Zephyrus Voices`, never uploaded. It is a lightweight matcher, NOT biometric-grade: replay or a close impersonator can pass. Payments/sign-in stay click-only regardless.
- Proved end to end with Windows-SAPI speech through the REAL engine + live proxy: enrolled voice -> wake -> command -> AssemblyAI text -> task ran -> answer in the bubble. Negative control: another voice is refused and nothing is transcribed. Dev hooks: `--zephyrus-test-compact=voice-hands-free|voice-setup --zephyrus-test-voice-dir=<dir>` (see the memory note `zephyrus-voice-handsfree-state`). NOT yet tested with a real microphone and a real human voice: that needs the user.
- Agent benchmark harness: `D:\zeph_perfench\` (local fixture site + request log, real chrome.exe, live proxy). Found and fixed on the way: (1) kernel policy stalled "open A and B" on an ambiguity question (`named_separately`, policy.rs); (2) a `<select>`'s choices were invisible so the model guessed values (observation now carries `options`); (3) a click on a link whose page had not started opening yet was reported as "nothing changed" and the model gave up (the loop now re-looks up to 4 times for free, task_loop.cc). Each has a regression test with a negative control.
- Caveat learned the hard way: a LOCKED or idle display makes the browser stop painting and produces fake agent failures. Benchmarks keep the display awake (SetThreadExecutionState).

**URGENT, deadline is 2026-09-30 (see section 2): the submission package matters more than any further feature work.**

0. Submission package, in this order: (1) public GitHub repo (the overlay repo; **ask the user before any push**, never publish secrets: `demo_keys.json`, `.vercel`, generated headers are excluded), (2) 60-90 s screen recording of the voice-driven agent doing 3 tasks, MP4, under 5 min, (3) 8-10 slide PDF, (4) 16:9 cover image, (5) hosted demo page URL, (6) paste the text from `SUBMISSION_DRAFT.md`.
1. Confirm the build finished; run `zephyrus_agent_unittests`.
2. **Fresh-profile end-to-end on the live proxy:** `powershell -File D:\zeph_perf\agent_task_capture.ps1 -Profile D:\zeph_perf\fresh1 -Out D:\zeph_perf\final.png -Wait 40` (default task: open the "More information" link on example.com). Expect real agent actions, not an error. Then a real multi-step task.
3. **Voice with a real mic** (the user must speak; `mcp` cannot).
4. Package the installer (`package_installer.py --out-dir out/Release --output <path> --allow-dev-switches`), and have the **user** install and launch it (never verify installs from the agent shell). Test as a judge would: first run, no settings.
5. Ask the user for the **hackathon page link**; record deadline, judging criteria and format in section 2; tailor the demo script to the criteria.
6. Prepare a short **demo script** (3–5 tasks that reliably work: search+open, fill a form up to the login/payment handoff, multi-tab research, voice command) and rehearse it on the shipping build. Have a fallback if the network or model is slow.
7. Set/verify **spend caps** on both provider workspaces; note the token expiry date (14 days from setup).
8. After the event: `demo_proxy/scripts/kill_demo.ps1`, then revoke both provider keys.
9. Nothing has been pushed since commit `d9ab5e1`; **ask the user before pushing** (see the GitHub overlay note in memory).

## 8. Known gaps and risks (be honest about these in a demo)

- Model latency (2–7 s/reply) dominates task time.
- Figma-style canvas apps: the agent can flail with repeated clicks. Ambiguity questions render as an approval card.
- Long-term recall is keyword overlap, facts are plaintext in the profile dir; chats are not restored after restart.
- Rate limiting in the proxy is per serverless instance, so the provider-side spend caps are the real ceiling.
- The Phase-1 12-task benchmark gate through all three adapters with the user's keys has not been run.
- Production audit (2026-09) said **not production-ready**: base out of support window, no updater, unsigned, Safe Browsing dead. Fine for a hackathon; do not claim otherwise.
- Prompt-injection posture: policy sits outside the model (measured: model size does not buy injection resistance). Keep it that way.

## 9. Secrets policy (standing)

- **Never** ask the user to paste an API key into chat, and never type one into a field. The user enters keys only in their own terminal (setup script) or dashboards.
- Provider keys exist only as sensitive env vars on the Vercel project. `D:\zephyrus-keys\demo_keys.json` holds only the proxy URL and a demo token. `D:\zephyrus-keys\entity_signing_key.pem` is the privacy-dataset signing key (not in the repo).
- Do not commit the generated `bundled_keys_data.h` (it lives in `out/`) or any key file.

## 10. Standing working rules from the user

- **Security/bug pass after every code change** (bug, error, security, attack surface) — see the ritual note.
- **Same bug twice ⇒ widen:** find the class, prove the test fails first.
- **Do not restructure what wasn't asked;** one structural change per build.
- Keep `zephyrus_dev_switches = true` in `out/Release`.
- **Ask before pushing** to any remote.
- Explain decisions simply: plain words, real numbers, a recommendation, one clear question.
- No AI-writing tells in chat; concise output style is active in this environment.
- Don't stop early over low context; judge by the shape of the remaining work.

## 11. Key files

- Agent (browser side): `chrome/browser/zephyrus/agent/` — `model_settings.*`, `bundled_keys.*`, `model_transport.*`, `tool_executor.*`, `agent_memory.*`, `voice_input.*`, `pointer_path.*`, `mascot_rig.*`, `schemas/tools.v1.json`
- Agent (UI): `chrome/browser/ui/views/frame/` — `zephyrus_agent_panel.*`, `zephyrus_agent_task_controller.*`, `zephyrus_agent_tool_surface.*`, `zephyrus_agent_mascot_overlay.*`, `zephyrus_agent_settings.cc`, `browser_view.cc`
- Kernel: `chrome/services/zephyrus_agent/` — `task_loop.cc`, `public/mojom/agent_kernel.mojom`, `kernel/src/{lib,policy,providers,tests}.rs`
- Toolbar/containers: `chrome/browser/ui/views/toolbar/toolbar_view.cc` (`ZephyrusGlassPill`)
- Demo proxy: `chrome/browser/zephyrus/demo_proxy/`
- Prefs: `chrome/browser/prefs/browser_prefs.cc`; settings allowlist `chrome/browser/extensions/api/settings_private/prefs_util.cc`; Appearance page `chrome/browser/resources/settings/appearance_page/`
