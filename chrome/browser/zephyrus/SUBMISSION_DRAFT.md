# lablab.ai submission — AssemblyAI Voice Agent Hackathon

Updated 2026-09-30 (second session). Text fields below are the length-checked
versions; the same text is in `D:\zeph_perf\submission\lablab_text.md`.
Limits are lablab's published ones; verify on the submit form.

## Project title (max 50 chars)
Zephyrus: the browser you talk to

## Short description (50 to 255)
Zephyrus is a browser you talk to. Say "Hey Zep" and a task: AssemblyAI transcribes it and an agent does it on the real page, with a mascot as its cursor. Voice Lock answers only your voice. It asks only before money and sign-in.

## Long description (600 to 2000)
Zephyrus is a Chromium-based browser with a voice-driven agent built in. Say "Hey Zep, open the pricing page and compare the two cheapest plans" and it happens on the real web page in front of you. A small mascot is the agent's cursor: it moves, clicks and types like a person, and what it is doing, what it found and any question it has appear in a bubble above it, so you can watch every step and stop it any time ("Hey Zep, stop").

Voice: the wake phrase is matched on your computer against voices you recorded, and nothing is sent to AssemblyAI until it matches. Only the command that follows is transcribed (AssemblyAI universal-3-5-pro). Voice Lock keeps other voices, and the video playing beside you, from steering the browser. It stores features of the phrase, never audio, encrypted by Windows. It is a convenience lock, not biometric security, so payments and sign-in never depend on it.

Safety: a policy layer outside the model decides what is allowed. The agent asks you only before spending money and at sign-in, and hands you the keyboard for passwords and card numbers. Page text is treated as data, never as instructions.

How it was tested: 12 real tasks through the shipped browser with a live model, checked against what the page received (a form by the POST the server got, a prompt injection by whether an address was ever requested): 12 of 12 passed, 14 seconds per task on average, including a Hinglish command. That benchmark found and fixed three real harness bugs.

India first: works in Hindi and English, separate workspaces with their own logins, built-in ad and tracker blocking. Judges can try it with nothing to set up: the build ships a hosted key service, so no provider key is inside the browser.

## Technologies used
AssemblyAI (speech-to-text, universal-3-5-pro), Claude (agent reasoning), Chromium 151, C++, Rust, Vercel (key-holding proxy).


## Assets (all in `D:\zeph_perf\submission\`)

| Field | File | Status |
|---|---|---|
| Cover image 16:9 PNG | `Zephyrus_cover_16x9.png` | done |
| Slide deck PDF | `Zephyrus_deck.pdf` (8 slides) | done |
| Demo video MP4 (<= 5 min, < 300 MB) | not made | needs one live model run once credits are restored; `record.py` + ffmpeg are ready. The user recording their OWN voice is stronger than the synthetic-voice footage |
| Public GitHub repo | overlay repo `D:\zephyrus_repo`, README text in `README_repo.md` | ask the user before pushing |
| Demo application URL | `demo_proxy/public/index.html` | user runs `vercel --prod` in `demo_proxy`, after adding `demo.mp4` to `public/` |

## What was demonstrated (safe to claim)

- Spoken command -> on-device wake + Voice Lock -> AssemblyAI text -> task -> answer in the bubble above the mascot, end to end, with Windows-SAPI speech through the REAL engine and the live proxy. A different voice is refused and nothing is transcribed.
- 12 real tasks, live Claude Opus 5.5, real browser: 12/12 passed, 164 s total. Checked against what the page received.
- Hinglish task passed.

## Not demonstrated - do not claim

- Voice Lock or "Hey Zep" with a real microphone and a real human voice (only synthetic voices so far).
- Hindi speech recognition quality (the Hinglish test was typed text).
- Any benchmark against other agent products.
- That Voice Lock is secure: it is a convenience lock.
