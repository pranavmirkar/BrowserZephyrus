# Rebase onto Chromium 156.0.8078.80: state and how to resume

Started 2026-11-03 (overnight, unattended). The old tree (`D:\chromium\src`, base 151.0.7922.171) is
untouched and still builds the installer. The new tree is a separate checkout.

## Where things are

| | |
|---|---|
| New tree | `C:\chromium156\src`, branch `zephyrus-156`, local commits only (not pushed) |
| Base | shallow clone of tag `156.0.8078.80` (the old tree is also shallow, so `--reference` does not work) |
| gclient | `C:\chromium156\.gclient` has `"managed": False` for `src`, so a sync fetches dependencies and never moves `src` off our branch |
| Dependencies | `gclient sync --nohooks --no-history -D` and `gclient runhooks` both finished (exit 0), including both PGO profiles |
| Build files | `gn gen out/Release` **succeeds** (36,251 targets) with the same `args.gn` as the old tree |
| Compile | **not started** |

## What the mechanical pass did (`research/rebase_apply.py`)

279 upstream files were modified by Zephyrus; upstream also changed 209 of them. 909 files are new.
909 new files copied; 70 modified files upstream never touched were copied; 130 merged automatically;
74 had conflicts (159 hunks). `research/rebase_resolve.py` settled 73 hunks that had an unambiguous
shape (both inserted at the same place; one side only added lines) and 11 that were only the product
rename. The remaining hunks were resolved by hand; **the tree now has no merge markers**.
`first_pass_report.md` lists every file; `auto_resolved_hunks.md` lists each automatic resolution so a
person can review them.

## What is NOT done (in priority order)

1. **Re-port the edits in `port_todo.md`.** Where upstream moved or removed code we had edited, the
   resolution took upstream's version and logged our edit there. Until re-ported these Zephyrus
   behaviours are missing in the 156 tree: the window-closing flag in `Browser` (empty tab strip must
   close the window), the clean search bar and engine pill in the location bar, the popup-safe
   history-clusters handler, "never use the search provider's new-tab page", overlay scrollbars on, and
   a settings string block. `extensions_menu_coordinator.cc` has an unused `arrow` variable to delete.
2. **Four files we changed no longer exist upstream** and were not applied:
   `settings/appearance_page/appearance_page.html`, `settings/settings_page/settings_section.html`,
   `ui/views/location_bar/ai_mode_page_action_icon_view.cc`,
   `ui/views/page_action/page_action_icon_controller.cc`.
3. **Windows SDK.** 156 pins SDK 10.0.28000.0; this machine has 10.0.26100.0. The two pin lines
   (`build/vs_toolchain.py`, `build/toolchain/win/setup_toolchain.py`) were pointed at the installed
   SDK to get `gn gen` through. If the compile needs the newer one: `winget install
   Microsoft.WindowsSDK.10.0.28000`, then revert those two lines.
4. **Compile and fix API drift** across ~1,100 Zephyrus files. Already known: `profile()` ->
   `GetProfile()`, `Browser::Create(CreateParams)` -> `CreateBrowserWindow(BrowserWindowCreateParams)`,
   `is_tab_modal_popup_deprecated_` -> `is_tab_modal_popup_`, `SetCornerRadii` ->
   `SetNativeViewCornerRadii`, `IDC_EDIT_MENU`/`IDC_ZOOM_MENU` -> `AppMenuModel::k*Placeholder`,
   `ScrimView(...)` is constructed with a colour id upstream (ours passes a colour; check), `V4LocalDatabaseManager` -> `SBLocalDatabaseManager` (done in
   the threat code). Suggested: a component, no-LTO build in a second output directory with `-k 0` to
   collect every error in one run.
5. The Zephyrus **installer** (`chrome/installer/zephyrus_setup`) and the **updater** (Z-02) need the
   same pass.

## Traps met

- `grep -r` over the half-million-file tree hangs the shell; use `git grep`.
- Do not run two Chromium builds at once (memory); the 156 build waits for the old one.
- Python helper text that contains `\n` inside a bash heredoc is mangled; write helpers to files.
