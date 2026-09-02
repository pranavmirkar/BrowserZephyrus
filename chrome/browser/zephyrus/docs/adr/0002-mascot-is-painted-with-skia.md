# The mascot is painted with Skia, not hosted in WebUI

The mascot's eighteen states were authored as CSS keyframes over inline SVG. CSS
animation does not run in Views, so the states are reimplemented as Skia painting
and layer animation inside a Views class rather than hosting the original file in
a WebUI surface.

The alternative was a `WebContents` living in the browser chrome to run the
animation file close to as written. That is a heavy, permanent commitment for a
decorative surface: another renderer process, another thing to keep alive across
window and workspace changes, and another surface to reason about for privacy.
The mascot does not justify it.

## Consequences

The animation file is a **specification, not an asset**. It stops being the
source of truth once the Views implementation exists, and changes to it must be
ported by hand. Keep it in the repo as the reference for what each state means.

Eighteen states is real work, but it is work this codebase already does. The
title bar hand-paints Breeze caption glyphs, workspace cells, the popup nub and
the separator rule. The mascot's animations are transforms and opacity on simple
rectangles, which is the same problem again.

`prefers-reduced-motion` has to be honoured explicitly. The CSS file gets it from
a media query; a Skia implementation gets nothing for free.
