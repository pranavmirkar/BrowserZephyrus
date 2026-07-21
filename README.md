# Zephyrus Browser

India-first browser fork of Chromium, by **Lazarus** ([thelazarus.in](https://thelazarus.in)).

**Version:** 0.1.0
**Base:** Chromium `151.0.7913.0` — upstream commit `f201b394daec46187f5efd093415e538a0414f34`

## What this repo is

This is the Zephyrus **overlay** — only the files changed or added on top of
upstream Chromium (not the full Chromium tree). Highlights: Workspaces with real
tab isolation, the Private Workspace feature (Windows Hello lock), a redesigned
UI/NTP, and a built-in ad/tracker shield.

## Building

1. Check out upstream Chromium at the base commit above (via `depot_tools`).
2. Copy this overlay over the Chromium `src/` tree.
3. Build:
   ```
   autoninja -C out/Default chrome
   ```
