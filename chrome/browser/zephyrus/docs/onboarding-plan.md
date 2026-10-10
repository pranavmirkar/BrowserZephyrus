# Onboarding screens: plan (2026-10-09)

## Decisions (user, 2026-10-09)

- Four screens: **Welcome + meet Zep**, **Mascot look + theme**, **Agent + voice setup**, **Privacy + import**.
- Full-window, one step at a time (Back / Next, progress dots, Zep reacts to each step), native like the rest of the browser.
- After the demo: take the time, do it properly.

## What already exists and gets reused (nothing is rebuilt)

| Need | Existing piece |
|---|---|
| Cards, switches, fields, section labels, M3 tokens | `zephyrus_m3.{h,cc}`, `zephyrus_m3_controls`, `zephyrus_m3_switch` |
| Mascot (pixel Zep and lens) and its moods | `ZephyrusAgentMascotOverlay`, prefs `zephyrus.agent.mascot`, `_skin`, `_lens_fill` (`MascotLook`) |
| Agent keys, spending limit, screenshots | `zephyrus_agent_settings.cc` (dialog), `model_settings.{h,cc}` |
| Hey Zep voice recording (three takes) | `zephyrus_voice_setup.{h,cc}` (`ShowVoiceSetup`) |
| Search engine choice | `zephyrus_search_engine_picker` |
| Ad and tracker blocking, privacy | adblock and `zephyrus_privacy_popup`, prefs under `zephyrus.privacy.*` |
| Per-window look, theme and workspace setup card | `zephyrus_customize_panel`, `zephyrus_workspace_setup` |
| Import from Chrome, default browser | upstream importer and `shell_integration` (not yet wired into Zephyrus UI) |

## How it is hosted

One new view, `ZephyrusOnboarding`, added to `BrowserView` the way the mascot overlay is (a child covering the window, above the web contents). It is not a dialog and not a web page, so it matches the rest of the chrome and can use the live mascot. Pref `zephyrus.onboarding.done` (bool, default false): set when the user finishes or skips. A dev switch `--zephyrus-test-onboarding` forces it for screenshots (dev builds only, like the other test hooks). The browser stays usable underneath for nothing: while it shows, tabs do not take input.

Each screen is a small class with `Title()`, `BuildBody()` and `Apply()`; the flow owns Back/Next/Skip and the progress dots. Changes are applied **as you go** (the look and theme change live so the person sees the result), except the API key, which is saved only on Next after a check.

## Screens

1. **Welcome.** Zep waves. One line on what the browser is (private, blocks trackers, has an agent that does tasks for you). Buttons: Get started, Skip all.
2. **Look.** Mascot: Zep, Lens, Lens with flowing colour (live preview, the real mascot changes). Theme: follows the system / light / dark. Show the mascot: on/off.
3. **Agent and voice.** Two cards. Agent: "Use the built-in demo access" (default when the build ships demo keys) or "Use my own key" (provider, key, model, spending limit per task, existing fields). Voice: "Set up Hey Zep" opens the existing three-take recording; skippable.
4. **Privacy and import.** Plain-word switches for ad blocking, tracker blocking, privacy dashboard. Search engine. "Bring my bookmarks from Chrome" (one button, starts the importer). "Make Zephyrus my default browser". Finish: Zep celebrates.

## Safety and honesty rules

- Nothing is sent anywhere by onboarding. A pasted API key is stored the same way as in Settings (system keystore) and never shown again, never logged.
- No account creation, no sign-in, nothing tracked about the person completing it.
- Every choice here can be changed later in Settings; each screen says so once.
- Skip at any point leaves safe defaults (blocking on, demo access if present, Zep default look).

## Milestones

1. Host and flow shell: pref, view, Back/Next/Skip, dots, dev switch, one empty screen. Test: pref logic and flow state are pure and unit-tested.
2. Screens 1 and 2 (live look and theme).
3. Screen 3 (agent and voice).
4. Screen 4 (privacy, search, import, default browser).
5. Zep reactions per step, copy pass, small and large windows, keyboard and accessibility.
6. Live checks (PrintWindow frames per step), security and bug pass, docs and memory.

## Open questions (do not block milestone 1)

- Exact copy for each screen (I will draft it; you edit).
- Whether onboarding re-runs from Settings ("Run setup again"). Proposed: yes.
- Whether the demo build skips onboarding when demo keys are present. Proposed: no, but screen 3 pre-selects demo access.

## Milestone 1: done (2026-10-09)

Built: `agent/onboarding_flow.{h,cc}` (pure flow: Next/Back/GoTo/Skip, the done pref `zephyrus.onboarding.done`, 9 tests), `ui/views/frame/zephyrus_onboarding.{h,cc}` (full-window view: progress dots, title and body per screen, Skip / Back / Next / Get started / Finish), hooked into `BrowserView` under the mascot and decided once at first activation. Four placeholder screens carry the draft copy; the controls come in milestones 2-4.

Checked live with a REAL mouse (`D:\zeph_perf\onb_drive.py`, PrintWindow frames, refuses to click unless the browser is in front): all four screens walk through, Back appears from screen 2, Finish reveals the browser, and the pref is saved (`done: true`) after a graceful close. A normal launch (no switch) does not show it.

DORMANT BY DEFAULT: `kOnboardingEnabled = false`, because the pref defaults to "not done" and every existing profile would otherwise meet a half-built flow on its next launch (including the demo build). Only `--zephyrus-test-onboarding` (dev builds) shows it. Flip it when milestone 6 is done.

Found while building: `SetPreferredSize(w, 0)` on a BoxLayout column squashes every row to nothing; a column of fixed width needs `CalculatePreferredSize` + `GetPreferredHeightForWidth`. A nested view class cannot be added with `AddChildView<T>` without its own `METADATA_HEADER`.

To do before turning it on: skip it in guest and off-the-record windows (their prefs start "not done" every time); decide keyboard handling (Esc, Enter) and focus trapping so Tab cannot reach the page underneath.

## Milestone 2: done (2026-10-09)

Screens 1 and 2 are real. Welcome: three honest lines (blocks ads and trackers; Zep does tasks for you; asks before paying, never types a password) and Zep waves. Make it yours: a row of three cards for the mascot (Pixel Zep, Lens, Lens flowing), a "Show Zep" switch, and a row of three for colours (Match my computer, Light, Dark). Every choice applies LIVE as it is pressed (the real mascot on the screen changes, light/dark flips the whole window) and is saved at once: `mascot_skin`, `mascot_lens_fill`, `mascot` and the browser colour scheme (`ThemeService::SetBrowserColorScheme`). Skip leaves whatever was chosen so far.

Checked with a real mouse (`D:\zeph_perf\onb_choices.py`): Lens, Lens flowing, Light, Show Zep off and on each show on screen at once, and the profile holds `lens`, `fluid`, `mascot true`, scheme Light after a graceful close. Real findings: a flex row gave cards unequal widths (now a fixed width per card); the driver mistook a selected card's pink outline for the Next button (it looks for FILLED areas only).

Reusable now: `OnboardingChoice` (a selectable card with radio accessibility, focus ring, hover) and `AddChoiceRow`, for the privacy and agent screens.

## Milestone 3: done (2026-10-09)

Screen 3, "Set up your assistant". Zep's brain: when the build ships its own access, two cards (Built-in access / My own key) that set `zephyrus.agent.use_provided_keys`; a status line that says what is really true ("Ready: built-in access", "Ready: your own key", "No key yet. Zep can browse with you, but it cannot do tasks until you add one"), kept fresh every second so it follows the dialog; a button that opens the EXISTING Agent model dialog for the key, model and spending limit. Hey Zep: a short honest paragraph and a button that opens the EXISTING voice setup popup. The screen never sees a key or a recording: both are the dialogs' business, so nothing new handles a secret. The rule behind the status wording is pure and tested (`AgentAccessFor`, 2 tests).

Proxy: switched to Sonnet 5.5 only for the demo. `claude-haiku-5-5` was removed from the live allowlist again (redeployed 2026-10-09; Sonnet answers, Haiku is refused, 15 proxy tests pass). Opus and the older Haiku stay allowed, so a submitted build that asks for them keeps working.

Checked with a real mouse (`D:\zeph_perf\onb_agent2.py`): My own key flips the status, "Add or change my key" opens the Agent model dialog with the provided-keys switch showing OFF (in step), "Set up my voice" opens the voice popup, Escape closes each, and the flow carries on to the last screen.

## Redesign + Workspaces screen (2026-10-09, user request)

Five screens now: Welcome, Make it yours, Workspaces (new), Set up your assistant, Privacy and import. The whole flow sits on an animated daytime sky (`zephyrus_onboarding_sky.{h,cc}`): a pre-rendered gradient with sunlight, and clouds in three parallax layers drifting at 30 fps (far small hazy slow, near big bright quicker), only while onboarding shows and only with rich animations on. Per frame it draws one sky image plus about 14 cloud images; nothing is blurred per frame. The headline is big white type with a soft lift, over a deep-blue haze so passing clouds dim instead of swallowing it; the controls sit in an opaque rounded card with a soft shadow; each screen rises and fades in (380 ms). Text on the transparent layer uses greyscale antialiasing (LCD text there had coloured fringes). On a short window the headline shrinks and the card's controls scroll, so the footer is always on screen.

Workspaces screen: the existing workspaces as chips, "Create a workspace..." opens the sidebar's own setup card (workspace id 0), and the list follows it within a second.

The Agent model dialog was too tall when opened from a button low in the window (title off the top of the screen): its body height now comes from the room above or below the ANCHOR (max 420), not the window's height. Measured: 976 px tall before, 558 px from a 900x700 window after, title and buttons on screen.

## Flying Zep, parallax, time-of-day skies (2026-10-09, user request)

- Zep flies a slow loop (52 s a lap, with a bob) round the column of headline and card, fitted to the window each frame so it is never parked behind the card. It is drawn by the sky BETWEEN its cloud layers (in front of the far and middle clouds, behind the near ones), is whichever look the person picked (pixel Zep or the lens, live), reacts to each screen (waves, happy, thinking, listening, happy), and is not there when "Show Zep" is off. The corner mascot steps aside meanwhile (`ZephyrusAgentMascotOverlay::SetSuppressed`). Flight path is pure and tested: on screen, smooth frame to frame, goes round the box it is given.
- The sky leans away from the pointer: each cloud layer slides by its depth (0.6%, 1.4%, 2.8% of the width at full deflection), eased so it reads as depth, not lag.
- Four skies by local time (pure, tested): dawn 05:00-07:59, day 08:00-16:59, sunset 17:00-19:29, night 19:30-04:59 (stars and a moon, dimmer blue clouds). Dev switch `--zephyrus-test-sky=dawn|day|sunset|night`.
- The workspace card had the same off-screen bug as the key dialog. Second time, so the rule is now one shared function, `zephyrus::BubbleBodyRoom(anchor, other_height, cap)` (room on the bigger side of the anchor, inside the window and the work area), used by both; the workspace card's body now scrolls under its hero. Measured from a 900x700 window: 566 px tall, hero, fields and buttons all on screen.
- Build note: a wide rebuild with the default parallelism ran clang out of memory (23 GB machine); `-j 6` to `-j 8` works.

## Workspaces inline + the last screen (2026-10-09)

- The Workspaces screen IS the workspace editor now (user request: show the card as the screen). The card's contents were pulled out of the bubble into one class, `ZephyrusWorkspaceEditor` (zephyrus_workspace_setup.cc), hosted by the bubble AND inline via `CreateWorkspaceEditor` / `CommitWorkspaceEditor`, so the two cannot drift. On that screen Next reads "Create and continue" and creates what the editor shows; "Not now, I'll make one later" moves on without one. Creating switches the window to the new workspace, so its colour shows at once.
- Last screen, "Your privacy, your things": switches for ads-and-trackers blocking and self-opening pop-ups (the adblock service watches both prefs live); one card of action rows for search engine (the address bar's own picker), bookmarks from Chrome, and default browser (Windows' own Settings decides; a check fills the line on entry).
- Chromium's importer cannot read Chrome (Firefox, Edge and bookmarks.html only), so `agent/chrome_bookmarks_reader.{h,cc}` reads Chrome's JSON itself: http and https only (no javascript: bookmarklets, no file: or chrome:), at most 32 folders deep, 20,000 bookmarks, 512-character titles, 32 MB file; read off the UI thread; added into one "From Chrome" folder on the bookmarks bar so nothing existing is touched; the button works once. 4 tests. Checked live: it brought over the one bookmark this computer's Chrome has.
- Zep flies in the right-hand margin when there is room beside the card (a tall loop), and round the card only when there is not, so it is never parked behind the controls.
- Slip worth remembering: a text replacement whose end marker sat far below its start deleted three screen builders between them; caught by the compiler, restored from an earlier read.
