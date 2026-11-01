# Zephyrus — session handoff (updated 2026-10-01)

Read this first if you are a new Claude session picking the project up. It is the
state of the world at the moment it was written; where it disagrees with the code
or a newer note, the code wins. No secrets are in this file and none should be
added: keys live only where section 10 says.

**Right now:** the hackathon is SUBMITTED and the project is under a **freeze until
the results are out (~2026-10-14): no GitHub pushes, no new installers, ask before
any publish (including `vercel --prod`)**. Local edits and local builds are fine.
The next body of work is **Agent v2** (section 8).

## 0. How to get the rest of the context

| What | Where |
|---|---|
| Persistent memory (~100 notes, indexed) | `C:\Users\prana\.claude\projects\D--chromium\memory\MEMORY.md` — read the index, then the notes you need. Start with `zephyrus-hackathon-freeze`, `zephyrus-voice-handsfree-state`. |
| Transcript of the 2026-09-30 sessions | `C:\Users\prana\.claude\projects\D--chromium\bb6effea-3782-416f-8f39-bca80601935a.jsonl` |
| Older transcript | `C:\Users\prana\.claude\projects\D--chromium\9ed02d5e-f664-4c2f-b662-e2e988ed6674.jsonl` |
| Design docs | `chrome/browser/zephyrus/docs/` — `agentic-harness-v2.md`, ADRs `0001`–`0006` |
| Vocabulary | `chrome/browser/zephyrus/CONTEXT.md` (terms only) |
| Security triage / production audit | `SECURITY_TRIAGE.md`, `PRODUCTION_AUDIT_2026-09.md` (same folder) |
| Scratch, test scripts, traces | `D:\zeph_perf\` (capture scripts, benchmark, recordings, submission assets) |

## 1. Who and what

- **User:** Pranav. Solo founder-engineer. Windows 11, AMD Ryzen 9 6900HS, ~23 GB RAM. **Screen 2560x1440 at 150% (144 dpi), taskbar auto-hidden at the bottom.**
- **Product:** **Zephyrus** browser (company **Lazarus**). A **Chromium 151 fork** at `D:\chromium\src`, India-first.
- **Differentiator:** an **agentic browser**: an in-browser agent that operates real pages, a mascot (**Zep**) that is its visible cursor, and voice ("Hey Zep", hold-to-talk). Asks permission only for payments and sign-in.
- **Role the user wants:** Senior Agent/Harness Developer and co-founder; treat it as a real product.

## 2. The hackathon — SUBMITTED

- **Event:** AssemblyAI Voice Agent Hackathon on lablab.ai (Sep 1–30, 2026). Submitted 2026-09-30 ~20:15 IST.
- **What judges see (do not change until results):**
  - Repo: https://github.com/pranavmirkar/BrowserZephyrus at **`86b278d`** (overlay; previous `479e6f0`, `6aec336`).
  - Release: **v0.1.0-hackathon** (pre-release), asset `Zephyrus-Setup-hackathon.exe`, 125,235,200 bytes, SHA-256 `DE19732C691BDFCB4102B38B4786F80EADA31A403FC3AEEBD8FF02B6E8C3DEB2`. Same file on the user's Google Drive (link on the demo page).
  - Demo page + key proxy: https://zephyrus-demo-proxy.vercel.app (Download button → Drive, GitHub release as alternate; plays the user's real 3:52 recording).
  - Video, deck (`Zephyrus_deck_v2.pdf`, 8 slides, M3 style), cover, form texts: `D:\zeph_perf\submission\FINAL\` and `D:\zeph_perf\submission\{form_text.md, additional_info.md, lablab_text.md}`.
- **Numbers we claimed (keep claims honest):** benchmark of 12 real tasks — Claude Opus 5.5 12/12 (14 s/task avg), Claude Sonnet 5.5 (demo default) 10/12; unit tests 247 / 74 / Rust 141.
- **After the results:** `demo_proxy/scripts/kill_demo.ps1`, then revoke both provider keys; the demo token expires ~14 days from setup anyway.

## 3. Environment and build rules (hard-won)

- Repo: `D:\chromium\src`. Shipping build dir: **`out\Release`** (official, non-component, DCHECKs OFF). Test performance there.
- Build: `cd /d/chromium/src && autoninja -C out/Release <targets>`; the Bash tool's cwd resets to `D:\chromium`. **autoninja can exit 0 on failure**: check the log and binary timestamps. A negative-control mutation that fails to COMPILE leaves the OLD test exe behind and gives a false pass: keep every variable used in a mutation.
- Targets: `chrome`, `mini_installer`, `chrome/installer/zephyrus_setup:zephyrus_setup`, `zephyrus_agent_unittests`, `zephyrus_agent_service_unittests`, `zephyrus_agent_kernel_unittests`, `kernel_unittests` (Rust).
- Installer: `python chrome/installer/zephyrus_setup/tools/package_installer.py --out-dir out/Release --output D:/<name>.exe --allow-dev-switches` (refuses to overwrite; move the old one aside). **Frozen until results.**
- Keep `zephyrus_dev_switches = true` in `out/Release/args.gn`.
- Never kill Chrome broadly; only PIDs you started. A stray test chrome holding a profile makes a benchmark "fail" in 22 s with calls=0.
- **Screen captures:** the screen is 2560x1440; size capture bitmaps from the screen (a 1920x1200 bitmap silently captured only the top-left for hours).
- The agent's shell is MSIX-redirected for AppData: never verify an install from it.
- **Bash-tool trap (hit many times):** `\n` and backslashes inside heredocs / inline python arrive mangled (a `\n` in a C++ string literal becomes a real newline). Write patch scripts with the Write tool, or build escapes from `chr(92)`.
- Dev switches: `--zephyrus-test-compact=<mode>` (`panel`, `mascot-drag`, `voice-setup`, `voice-hands-free`, `voice-light`, `task`), `--zephyrus-test-agent-task="<task>"`, `--zephyrus-test-voice-dir=<dir>`, `--zephyrus-agent-trace=<file>`.
- **Real-input test scripts (use after touching overlay / hit-testing / keys):** `D:\zeph_perf\realclick.ps1` (OS clicks must reach the page: pixel (900,600) white afterwards), `realdrag.ps1` (OS drag picks Zep up), `realptt.ps1` (OS key hold/tap), `winrect.ps1` (window vs client rects), `edgecheck.ps1`.
- Benchmark: `D:\zeph_perf\bench\run_bench.py --model claude-sonnet-5-5 [--only a,b]` (fixture site `site.py`, real chrome, live proxy; costs credits — budget runs).
- Real-voice diagnostic: `ZEPH_REAL_DIR=D:\zeph_perf\real ZEPH_REAL_SKIP=1 ./out/Release/zephyrus_agent_unittests.exe --single-process-tests --gtest_filter=VoiceReal.* --enable-logging=stderr` (recordings of the user on 3 mics; `D:\zeph_perf\rec3.py` records new ones).

## 4. Architecture in one page

- **Agent kernel:** Rust (`chrome/services/zephyrus_agent/kernel`, cxx bridge) in a sandboxed utility process (ADR 0001). C++ `TaskLoop` (`task_loop.cc`) drives it. Tool contract `agent/schemas/tools.v1.json` (26 tools).
- **Providers** (ADR 0004): adapters in the kernel; the browser is an authenticated pipe (`model_transport.cc`). Cloud turns batch up to 8 tool calls, each policy-judged. Effort `low`, escalates to `medium` after a refused call or step 8.
- **Policy** (`policy.rs`, ADR 0005): asks only for payments and sign-in; password/card typing is Deny → `task.handoff`. Approval lifts an Ask, never a Deny. An invented tool name is refused with the list of real tools.
- **Observation:** accessibility tree + labelled screenshot (redacted: phone/email/card numbers masked in text AND pixels; `raw_value` stays in the browser for typing verification). Act through a synthesized input pipeline (`zephyrus_agent_tool_surface.*`), never through accessibility actions. Multi-line typing goes line by line with a pause after each Enter (rich editors move focus asynchronously).
- **Mascot (Zep):** `MascotRig` springs + `ZephyrusAgentMascotOverlay` (pass-through layer). Grabbing it uses an **aura pre-target mouse handler** (`MouseWatcher`); a view targeter on the full-window overlay made the WHOLE page unclickable once — never again. Moods include `kHeld` (hangs from the cursor) and `kRelieved`. Where it is dropped becomes home (`parked_`). Typed/spoken "move to the left / get out of the way / come back" → `agent/mascot_commands.cc`.
- **Voice:** `voice_input.*` (waveIn 16 kHz, mic chosen by pref `zephyrus.voice.mic`, list from `audio_devices.cc`), AssemblyAI sync API via the proxy. **Hold Ctrl+Shift+Space to talk** (`ZephyrusAgentPanel::KeyWatcher`, aura pre-target key handler, timed by event timestamps, taps <350 ms cancelled). "Hey Zep": `hands_free.*`, `voice_dsp.*` (shared `HopVad`, noise-aware `SpeechSpan`), `voice_lock.*`, `phrase_recorder.*`; enrolment = **3 takes** in the popup anchored to the mic-and-gear header button (`zephyrus_voice_setup.cc`).
- **Memory** (ADR 0006): working / conversation / long-term.
- **Demo key proxy:** `chrome/browser/zephyrus/demo_proxy/` (Vercel). Browser holds no provider key, only `{proxy_url, expiring token}`.
- Other subsystems (privacy intelligence, adblock, workspaces, M3 UI): see the memory index.

## 5. What was done on 2026-09-30 (three sessions)

All built, unit-tested and checked on the real build unless noted.

1. **Voice M1–M4:** mascot bubble, "Hey Zep" on-device wake, Voice Lock, setup UI. Proven end to end with SAPI speech + live proxy; negative control (another voice) refused.
2. **Default model** Claude Sonnet 5.5 for the bundled demo keys (forced while bundled keys are in use).
3. **Final audit fixes:** page text / `<select>` options / screenshot text were unredacted; glued numbers missed; typing into a redacted field could never verify; mascot repainted the whole window (24% → 2% idle CPU); mic light hidden with the mascot off; a "two mascots on startup" regression.
4. **Voice setup popup** rewritten small and friendly; microphone selector; "not hearing anything" hint; **3 takes**.
5. **Real-microphone enrolment:** gates were tuned on clean synthetic voices; real takes sit 0.33–0.40 apart. Loosened, plus a length gate. **Laptop array mic** (constant −25 dB hiss): new shared `HopVad` (smoothing, adaptive margin, re-anchors a floor poisoned by the device's leading digital silence), noise-aware `SpeechSpan`. Real takes: 3/3 accepted, wake 4/4, no false wakes. Two regression tests, each proven to fail without its fix.
6. **Hold-to-talk** (replaces the old press/press toggle). Proven with real OS keys; speech-through-mic to transcript NOT proven by me (TTS wasn't picked up) — the user's own recording shows it working.
7. **Zep can be picked up and moved** (hold + relief animation; move by voice/chat). Proven with a real OS drag.
8. **Agent complex-task fixes:** Notion essay landing in the title (line-paced typing), WRITING IN EDITORS + PERSISTENCE prompt sections, invented-tool refusal names real tools, effort escalation, Notion-like `essay` task in the benchmark. Sonnet 4/4 on essay/contact/form/compare.
9. **Submission package** (section 2), including the GitHub release made via the API with the cached git login.
10. **After submission (LOCAL ONLY, unpushed):** maximised window showed 2 px of desktop above the title bar. Cause: Windows 11 answers `ABM_GETAUTOHIDEBAR` with the bottom taskbar for the TOP edge too. Fix in `chrome/browser/ui/views/chrome_views_delegate_win.cc` (trust it only for the taskbar's real edge via `ABM_GETTASKBARPOS`). The bottom 2 px is by design (lets the auto-hide taskbar pop).

## 6. Where things stand right now

- `out/Release` contains everything above including the unpushed taskbar fix. The shipped installer does NOT have the taskbar fix.
- Unpushed local changes: the taskbar fix; `demo_proxy/public/demo.mp4` (the user's real recording, deployed but not in the repo).
- Tests: `zephyrus_agent_unittests` 247, `zephyrus_agent_service_unittests` 74, Rust `kernel_unittests` 141 — all pass.
- Known model-quality misses on Sonnet: the example.com "Learn more" loop; an off-by-one year on the Hinglish Taj Mahal task.

## 7. Known gaps and risks

- Model latency (2–7 s per reply) dominates task time; every step is a full round trip.
- The laptop mic recorder still misses ~1 in 6 phrases; Voice Lock is a convenience lock, not biometric.
- Canvas apps (Figma-like) and image-only buttons are weak; login walls and CAPTCHAs on real sites need the user.
- Not production-ready (base out of support window, no updater, unsigned, Safe Browsing dead).
- Prompt-injection posture: policy sits outside the model. Keep it that way.

## 8. THE NEW PLAN — Agent v2 (decided direction, not started)

Goal: a more general, more accurate, cheaper and faster agent; Claude as strategist, the agent as a capable tactician, the Rust kernel as the safety layer.

### 8.1 What the numbers say today (measured on our benchmark, Sonnet 5.5)

- A 5-step compare task costs about **$0.045**: fresh input 9.3k tokens, cache reads 38k (the ~7.6k-token system prompt + tool list is already cached each call), output 390.
- Fresh input per call is ~1.8k tokens: observation (100–600 tokens), screenshot (~700 tokens, estimated from a 692x792 JPEG), history.
- So dropping screenshots alone saves ~20%. The big levers are **fewer model calls per task** and **cheaper models for easy steps**.

### 8.2 Seven components

1. **Skills, not thousands of tools.** ~25 primitives always on (cached). App knowledge lives in **skills**: signed data files, a typed verb with args, preconditions, steps addressed by role+name, a postcondition, and a risk class the kernel enforces (a skill cannot raise its own risk). The **browser** loads the skill pack for the current site from the URL (10–20 verbs), so the model never sees thousands. Sources: hand-written for ~50 India-first apps (IRCTC, Flipkart, Swiggy, Zomato, UPI apps, WhatsApp Web, Gmail, Notion, YouTube, Amazon…); distilled from successful traces; replayed nightly, broken ones flagged.
2. **Page Card (text-first perception).** A compact text rendering, 1–2k tokens: regions, interactive elements with stable ids, role/name/state, repeated lists collapsed ("12 products: name, price, rating + first 5"), a small layout sketch. Vision only on demand, as a cropped region, when the card is unsure (canvas, charts, CAPTCHA, icon-only buttons). Text redacts more cleanly than pixels.
3. **A new exchange protocol with Claude.**
   - **Append-only conversation** so the prompt cache covers the whole history, not just the prefix (today parts of the history are rewritten, which breaks caching).
   - **Deltas, not snapshots** after the first card ("cart 0→1, toast appeared, URL changed").
   - **Plans with expectations:** Claude sends a short program, each step with an expected outcome; the agent runs it locally and reports once, or only when an expectation fails (with the closest alternatives).
   - **Cheap queries:** Claude asks the agent ("list prices", "does Checkout exist?") and gets a few tokens back.
   - **Streaming execution** (start the first action while Claude is still writing) and **prefetch** (build the next card while Claude thinks).
4. **Reflexes** handled locally without Claude: cookie banners/pop-ups, SPA settling, scroll into view, "load more", retry a missed click with another method. (The existing link-click and loading rechecks become a system.)
5. **Self-verification and confidence.** Every action is checked by its effect; the agent reports done / failed-with-reason, never a bare "ok". A confidence score decides: act, ask Claude, escalate to a bigger model, or hand to the user (money and sign-in always the user).
6. **Memory that compounds:** what worked per site becomes skills; user preferences carry over.
7. **Model routing:** Haiku for skill-covered steps, Sonnet for planning, Opus only to repair a broken skill. Optional small on-device model for page-type classification and routing.

### 8.3 Targets (measured, published as an ablation)

- ≥90% success, ≤$0.02 and ≤8 s median per task; 1–2 Claude calls where today it takes ~5.
- Same model, three arms: screenshots (today) vs text-only vs text + skills + plans. Extend the 12-task harness toward ~200 tasks on ~40 real sites, including injection and approval checks.

### 8.4 Order of work (local only while the freeze holds)

1. **Baseline + Page Card v1 + append-only history + deltas.** Measure the cost breakdown per step; run the text-only vs screenshot ablation. (~2–3 days.) ← recommended first step, awaiting the user's go-ahead.
2. **Reflexes and self-checking.**
3. **Skill format, URL loader, first 10 skills,** kernel risk mapping.
4. **Plans with expectations + cheap queries + streaming execution.**
5. **Confidence-based escalation, skill distiller + nightly replay, model routing.**

Open decisions for the user: which 10 apps first; whether an on-device model is worth its install size; whether skills ship inside the build or as a signed downloadable pack (the latter needs an update channel, which the browser does not have yet).

## 8b. Agent v2 Phase 0 DONE (2026-10-06): see docs/agent-v2-baseline.md

Benchmark v2 is in `D:\zeph_perfench2\` (43 tasks in 4 suites, per-call telemetry, report with intervals). Baseline on Sonnet 5.5: A0 (screenshots on) and A1 (off) both 32/37, safety 8/8, general 10/10, $0.012 vs $0.009 per task, ~6 s median. The failures are our bugs (policy treats "Subscribe" as a payment; ambiguity guard false positive; iframe typing; date inputs), not model quality. Plan docs: `docs/agent-v2-plan.md` (phases, general intelligence), `docs/site-tool-packs.md` (site packs + WebMCP), `docs/agent-v2-baseline.md` (numbers and findings). Reliability fixes now come first.

**Bug fixes after Phase 0 (same day):** policy F1/F2, iframe bounds, spinbutton typing, segmented-field typing check; A0 now 34/37, S3 8/8. Left: sort, wizard/g_clarify variance, and replies with text and no tool call (Phase 1). Unpushed, local only. See the end of docs/agent-v2-baseline.md.

**Wasted steps now 0 of 69 (37/37, S3 8/8):** reload resets the repeat guard, text-form call beside native runs first, download + click-effect reporting, page.observe not offered, clickable table headers offered. Single runs; repeats still pending.

## 9. Next steps, in order

1. Wait for the user's go-ahead on Agent v2 step 1 (8.4). Work locally; do not push or package.
2. When the freeze lifts: rebuild the installer with the taskbar fix, push the overlay (ask first), consider pushing the real demo video, run `kill_demo.ps1` and revoke keys if the proxy is no longer needed.

## 10. Secrets policy (standing)

- **Never** ask the user to paste an API key into chat, and never type one into a field.
- Provider keys exist only as sensitive env vars on the Vercel project. `D:\zephyrus-keys\demo_keys.json` holds only the proxy URL and a demo token. `D:\zephyrus-keys\entity_signing_key.pem` is the privacy-dataset signing key (not in the repo).
- Do not commit the generated `bundled_keys_data.h` (in `out/`) or any key file. Run `D:\zeph_perf\secret_scan.py` and `crlf_scan.py` before any push (procedure: memory note `zephyrus-github-overlay`).

## 11. Standing working rules from the user

- **Freeze until hackathon results:** no pushes, no installers, ask before any publish.
- **Security/bug pass after every code change.**
- **Same bug twice ⇒ widen:** find the class, prove the test fails first.
- **Do not restructure what wasn't asked;** one structural change per build.
- Keep `zephyrus_dev_switches = true` in `out/Release`.
- **Ask before pushing** to any remote.
- Explain decisions simply: plain words, real numbers, a recommendation, one clear question. Concise output, no AI-writing tells.
- Verify UI changes with REAL OS input (click/drag/keys), not only the agent's input path.

## 12. Key files

- Agent (browser side): `chrome/browser/zephyrus/agent/` — `tool_executor.*`, `observation.*`, `sanitizer.*`, `model_settings.*`, `bundled_keys.*`, `model_transport.*`, `agent_memory.*`, `voice_input.*`, `audio_devices.*`, `hands_free.*`, `voice_dsp.*`, `voice_lock.*`, `phrase_recorder.*`, `voice_library.*`, `mascot_rig.*`, `mascot_commands.*`, `pointer_path.*`, `schemas/tools.v1.json`
- Agent (UI): `chrome/browser/ui/views/frame/` — `zephyrus_agent_panel.*`, `zephyrus_agent_task_controller.*`, `zephyrus_agent_tool_surface.*`, `zephyrus_agent_mascot_overlay.*`, `zephyrus_mascot_bubble.*`, `zephyrus_hands_free.*`, `zephyrus_voice_setup.*`, `zephyrus_agent_settings.cc`, `browser_view.cc` (dev hooks)
- Kernel: `chrome/services/zephyrus_agent/` — `task_loop.cc`, `public/mojom/agent_kernel.mojom`, `kernel/src/{lib,policy,providers,extraction,tests}.rs`
- Windows frame: `chrome/browser/ui/views/frame/browser_desktop_window_tree_host_win.cc`, `chrome/browser/ui/views/chrome_views_delegate_win.cc`
- Demo proxy + hosted page: `chrome/browser/zephyrus/demo_proxy/`
- Benchmark: `D:\zeph_perf\bench\` (`site.py`, `run_bench.py`, `traces/`)
