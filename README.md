# Zephyrus Browser

India-first browser fork of Chromium, by **Lazarus** ([thelazarus.in](https://thelazarus.in)).

**Version:** 0.1.0
**Base:** Chromium `151.0.7922.171` - upstream commit `cc5584af0df9786f00efdb71f666c6664f836c2d`

**The browser you talk to.** An India-first Chromium 151 fork by **Lazarus** with a built-in voice-driven agent. Built for the AssemblyAI Voice Agent Hackathon.

Say "Hey Zep" and a task. AssemblyAI transcribes it, and an agent does it on the real page in front of you, with a mascot as its cursor. It asks you only before spending money and at sign-in.

## What is in this repo

An **overlay**: only the files changed or added on top of upstream Chromium, not the full Chromium tree (which cannot be pushed to GitHub). See "Building".

### The agent
- `chrome/services/zephyrus_agent/` – the agent **kernel**, in Rust, running in its own sandboxed utility process. It decides what the agent may do (`kernel/src/policy.rs`), speaks to the model providers (`providers.rs`) and drives the task loop (`task_loop.cc`). Policy sits **outside the model**: approval lifts an Ask and never a Deny.
- `chrome/browser/zephyrus/agent/` – the browser side: observation (accessibility tree + labelled screenshot), tool executor, model transport (keys added last, host fixed), memory, the mascot's physics and pointer paths.
- `chrome/browser/ui/views/frame/zephyrus_agent_*.cc` – the panel, the tool surface (acts through the real input pipeline), the mascot overlay and its speech bubble.
- Tool contract: `chrome/browser/zephyrus/agent/schemas/tools.v1.json`. Decisions: `chrome/browser/zephyrus/docs/adr/`.

### Voice
- `agent/hands_free.*`, `voice_dsp.*`, `voice_lock.*`, `phrase_recorder.*` – "Hey Zep": on-device wake detection (MFCC + DTW against the user's own recordings) and **Voice Lock**. Nothing is sent to AssemblyAI until the phrase matches an enrolled voice; then only the command that follows is transcribed (`voice_input.*`).
- `agent/voice_library.*`, `voice_profile_store.*` – named voices, stored as features (never audio), encrypted with the OS keystore, on this computer only.
- `ui/views/frame/zephyrus_hands_free.*`, `zephyrus_voice_setup.*` – the listener for a window and the "Hey Zep and voices" dialog.
- Ctrl+Shift+Space talks without the phrase; Ctrl+Shift+Comma mutes.

### The rest of the browser
Workspaces with real storage isolation, a Private Workspace, built-in ad and tracker blocking, a Material 3 Expressive UI, and a Privacy Intelligence panel.

## Measured

Twelve real tasks through the shipped browser with a live model, checked against what the page actually received (a form by the POST the server got, a prompt injection by whether an address was ever requested): **12 of 12 passed, 14 s per task on average**, including a Hinglish command. The harness is in `chrome/browser/zephyrus/agent/benchmark/`. Running it found and fixed three real bugs, each with a regression test.

Unit tests: `zephyrus_agent_unittests` (230), `zephyrus_agent_service_unittests` (72), Rust `kernel_unittests` (140).

## Honest limits
- Voice Lock is a lightweight matcher, not biometric security. A replay or a close impersonator can pass it, which is why payments and sign-in never depend on it. It was tuned on synthetic voices and needs more real-voice testing.
- Not code-signed, no updater yet, and the Chromium base is out of its support window. Fine for a hackathon, not for production.
- The hosted demo key service (`chrome/browser/zephyrus/demo_proxy/`) exists so judges need no setup: no provider key is inside the browser, only a proxy address and an expiring token.

## Building
1. Check out upstream Chromium at the base commit above (via `depot_tools`).
2. Copy this overlay over the Chromium `src/` tree.
3. `autoninja -C out/Release chrome`

Secrets policy: no API key is in this repository. Provider keys live only as environment variables on the demo proxy.
