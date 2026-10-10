# Glance: look at a link without opening it

Zen's feature, rebuilt for Chromium. Hold Alt and click a link: the page opens
in a card over the current tab. Close it, or promote it to a real tab, or put it
beside the page in split view.

## What Zen does (read from its source, 2026-10)

- **Trigger.** Alt+click on a link in a normal tab. Setting: on/off, and the key
  (Alt, Ctrl or Shift). Pinned and Essential tabs glance on their own for links
  that leave the site (off by default in the source, on in the docs).
- **Also from** the link context menu ("Open link in Glance") and bookmarks.
- **The card.** 80% of the page width, full height, centred. It grows out of the
  link you clicked (the link's rectangle scales up along a short arc), over about
  a third of a second with an ease-in-out curve.
- **The page behind.** Shrinks to 97% and fades to 30% while the card is open.
- **Buttons.** Three round buttons sit outside the card's side edge: Close,
  Expand (becomes a real tab next to the parent), Split (expand, then split view
  with the parent). They fade and slide in 0.2 s after the card lands.
- **Closing.** Click outside the card, or Close. The card shrinks back; the page
  behind springs back to full size.
- **It is a real tab underneath**, owned by the parent. Switch away from the
  parent and the card is dismissed. Close the parent and the glance goes too.
- **One glance at a time.**

## What we build, and where it differs

Chromium has no link preview left in the tree (the friend declaration in
`tab_helpers.h` is all that remains), so this is built from the pieces below.

| Piece | How |
|---|---|
| Know which link was clicked | `Browser::UpdateTargetURL` already reports the link under the pointer (it feeds the status bubble). One line forwards it to the glance controller. |
| Catch the click | `RenderWidgetHost::AddMouseEventCallback` on the page's widget. It runs before Blink, and returning true swallows the event, so Alt+click never reaches Blink's download rule. No Blink or content patch. |
| The page | A loose `WebContents` in a `views::WebView`, **not** a tab-strip tab. Zen's hidden real tab would show up in our sidebar, the session file and tab search, and each of those would need a filter. |
| Same cookies | The glance is created in the parent's workspace StoragePartition (`SiteInstanceForWorkspace`), or the default one. A glance that was logged out would be useless for "look at this link". |
| Same protection | Tab helpers are attached so the adblocker and the privacy features see the glance like any other page. One friend line in `tab_helpers.h`. |
| Expand | `TabStripModel::AddWebContents` after the parent. The tab strip adopts the same `WebContents`, so the page does not reload. |
| Split | Expand, then `AddToNewSplit` with the parent. |
| The card | A view over the page area, under the mascot. Animated with the same layer helpers as the omnibox overlay. |

**Not in the first version:** bookmarks trigger, auto-glance for pinned tabs,
glances opened from inside a glance. Each is small once the rest stands.

## Settings

- `zephyrus.glance.enabled` (default true)
- `zephyrus.glance.trigger` = `alt` | `ctrl` | `shift` (default `alt`)

## Risks to check by running, not by reasoning

1. **Tab helpers on a tab that is not in a tab strip.** Some helpers look for the
   tab's `TabInterface`. Attach them, open a few real sites, watch for a
   DCHECK/CHECK dump (use `--log-file`).
2. **OOPIF links.** The mouse callback is per widget, so a link inside a
   cross-process iframe needs its own registration.
3. **Swallowed clicks.** Only swallow a click that is Alt + left + over a link we
   can glance. Anything else must pass through untouched.
4. **Popups from the glance** (`window.open`, target=_blank) must go to the tab
   strip, never to a second hidden window.
5. **Lifetime.** The glance owns its `WebContents` until Expand hands it over.
   A close during navigation, a parent that goes away mid-animation, and a
   window closing with a glance open each need a test.

## Milestones

1. **Pure logic** (`chrome/browser/zephyrus/glance`): trigger rule, link filter,
   frame geometry, animation curve. Unit tests. *This is the part that can be
   wrong without anything crashing, so it is tested first.*
2. **Hover + click interception** with a log line only (no UI): prove the right
   URL is known at click time and the click is swallowed only when it should be.
3. **The card**: WebContents, view, open/close animation, Escape, click outside.
4. **Expand and Split.**
5. **Settings, context menu, dev switch (`--zephyrus-test-glance=URL`)**, docs.

Security/bug pass after each, per the standing rule.
