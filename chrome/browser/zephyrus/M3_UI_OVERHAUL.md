# Zephyrus — Material 3 UI Overhaul

Status: **plan, not yet started.** Written 2026-09-15 against
<https://m3.material.io> as it stands after Google I/O 2026.

Scope, as set by the request: **the UI changes, the theming mechanism does
not.** The ref-colour ramp, `PublishThemePalette()`, the `PaletteFrom(provider)`
snapshot rule, and Customize Chrome all keep working exactly as they do today.
What changes is what those colours are *applied to*: shape, spacing, type,
state, motion, and component anatomy — across Zephyrus's own surfaces and
Chromium's.

> **Revised 2026-09-15, after the first draft.** The original plan kept the
> Nothing OS design language and adopted M3 only as structure ("M3 is the
> grammar, Nothing OS is the vocabulary"). **That is no longer the plan.**
> Nothing OS is retired. Zephyrus adopts Material 3 Expressive *whole* —
> its colour system, its full shape scale, its elevation model, its typeface,
> its components. Sections 0, 2 and 5 are rewritten accordingly; anything in
> this document that still reads as a compromise between two languages is a
> leftover and should be treated as wrong.

---

## 0. Hard rules

These are not recommendations. They hold in every phase, on every surface,
Zephyrus's and Chromium's alike, and a change that breaks one is wrong even if
it looks fine.

### Rule 1 — The M3 shape scale, all ten steps, with M3's own component mappings

```
0  ·  4  ·  8  ·  12  ·  16  ·  20  ·  28  ·  32  ·  48  ·  full
```

A component's radius is **not chosen by us**. It is whatever M3 specifies for
that component, and where M3 is silent, the nearest step that matches the
component's emphasis. The five-step Zephyrus scale from the first draft is
withdrawn along with the rest of the old language.

Known mappings to hold to:

| Surface | Radius |
| --- | --- |
| Dialogs | 28 |
| Menus, sheets | 12 (baseline) / 28 (expressive vertical menu) |
| Cards | 12 |
| Buttons, chips, FAB, omnibox field | full |
| Button pressed state (size XS/S → M → L/XL) | 8 → 12 → 16 |
| Square button (XS/S → M → L/XL) | 12 → 16 → 28 |
| Connected group inner corners (XS/S/M/L/XL) | 4 / 8 / 8 / 16 / 20 |
| Text fields | 4 top corners (filled) / 4 (outlined) |

The old note in `zephyrus_bubble_style.h` — *"pill or card, nothing in between;
12 or 16 is always one of these two, chosen wrong"* — is **deleted, not
amended**. The measurement behind it was a measurement of Nothing OS, which is
no longer what we are building.

### Rule 2 — Corner concentricity, always

**When a rounded shape sits inside another rounded shape, their corners share a
centre.** The inner radius is not chosen from the scale; it is derived:

```
inner radius = outer radius − padding between them
```

This is Apple's concentricity rule, and it is also M3's own — the spec states
the identical formula under the name "optical roundness". The difference is
force: M3 treats it as guidance, we enforce it.

**Precedence, now that we are fully M3:** where a component spec names a corner,
that number wins (Rule 1). Rule 2 governs everywhere the spec is silent, which
is every *nesting* relationship — a panel inside a window, a WebView inside a
panel, a row inside a popup, a thumbnail inside a card. M3 has no table for
those, which is exactly why they are where we kept getting it wrong.

So the two rules no longer collide the way they did in the first draft. The
connected-button-group inner corners, which were the one real conflict, are now
taken from M3's table (4 / 8 / 8 / 16 / 20) rather than derived.

A nested radius is a *computed* value and may land off the ten-step scale.
That is correct and expected.

**Where Rule 2 applies:** rounded container inside rounded container, corners
aligned. Web contents inside the window frame. A WebView inside the customize
panel. Rows inside a popup. The tab switcher's inner tiles inside its outer
card. A thumbnail inside a card. An avatar inside its disc.

**Two exemptions, and only two:**

1. **Capsules.** A pill's radius is height ÷ 2 — it is a function of the
   control's size, not a value anyone chose, so there is nothing to make
   concentric. A pill nested in a corner stays a pill. This is also what Apple
   ships: capsule buttons sit inside rounded sheets without being flattened.
   Without this exemption the rule would turn every button in the browser into
   a 4px rectangle and destroy "pill means pressable".
2. **Segments of one connected control.** The touching corners inside a
   connected button group, a split button, or a segmented control come from
   that component's own spec, not from the container behind them. They are not
   nested inside anything — they are interior seams.

**Enforcement.** Phase 1 adds `zephyrus::ConcentricInner(outer, padding)` and a
debug-only check that fires when a rounded container paints a rounded child
whose radius violates the rule. A hard rule that is only written down is the
kind we have already broken twice — the customize panel corner leak and the tab
switcher's mismatched inner tiles were both this rule, caught by eye instead of
by the build.

---

## 1. What M3 actually is in 2026

M3 is now **M3 Expressive**. This is not a coat of paint on the 2021 spec; four
of the subsystems below are new or materially rewritten. Everything in this
section was read off the spec, not recalled.

### 1.1 Colour — 26 roles in six groups

| Group | Roles |
| --- | --- |
| Primary | primary, on-primary, primary-container, on-primary-container |
| Secondary | secondary, on-secondary, secondary-container, on-secondary-container |
| Tertiary | tertiary, on-tertiary, tertiary-container, on-tertiary-container |
| Error | error, on-error, error-container, on-error-container |
| Surface | surface, on-surface, on-surface-variant, + five containers: lowest / low / **container** / high / highest |
| Outline | outline, outline-variant |

Plus inverse-surface / inverse-on-surface / inverse-primary, and optional
"fixed" accents that hold tone across light and dark.

Load-bearing rules:

- **Pairs are normative.** Every container role has exactly one `on-` role and
  the pair is guaranteed at least 3:1. Mixing across pairs is how contrast
  breaks when the user raises contrast or switches theme.
- **Error is static** — it does not follow dynamic colour, only light/dark.
  (This is the spec's own version of the rule we just applied to the delete
  dialog. We arrived at it independently; M3 agrees.)
- **Container is never ink.** Container roles are fills only.
- Default component mappings: menus and navigation to `surface-container`,
  dialogs to `surface-container-high`, elevated buttons to
  `surface-container-low`.

### 1.2 Shape — a ten-step scale, and asymmetry is first-class

`0 / 4 / 8 / 12 / 16 / 20 / 28 / 32 / 48 / full`
(none, xs, s, m, l, l-increased, xl, xl-increased, xxl, full)

- Corners can be **asymmetric**; M3 calls the squared-off ones *inner corners*
  and uses them wherever items are closely grouped — menus, split buttons,
  connected button groups.
- **Optical roundness:** `inner radius = outer radius - padding`. Nesting two
  shapes at the same radius is called out explicitly as wrong. This is the same
  formula as Apple's concentricity — but M3 states it as guidance and then
  contradicts it in component tables, whereas for us it is **Rule 2** and
  binding. See section 0.
- Shape is remappable per component *and* per style. Customisation here is
  sanctioned, not merely tolerated.

### 1.3 State layers — fixed opacities, content-derived colour

| State | Opacity |
| --- | --- |
| Hover | 8% |
| Focus | 10% |
| Press | 10% |
| Drag | 16% |
| Disabled | 38% |

The layer's **colour is the content's colour** (the `on-` role), not the
container's. One layer at a time. Layer 40dp, target 48dp.

> Our buttons currently use 8 / 14 / 20%. The 20% press is off-spec and will
> read heavier than the rest of the UI once this lands.

### 1.4 Elevation — tonal first, shadows as the exception

Six levels (0 to +5); **0 to +3 are resting, +4 and +5 are interaction-only.**
The default way to show separation is a **tonal difference between surface
roles**, not a shadow. Shadows are reserved for busy backgrounds and
lift-on-interaction. Scrim is the `scrim` role at **32%**.

### 1.5 Motion — springs, with a published curve fallback

The legacy easing/duration tokens are demoted to "fallback". The system is now
spring physics (damping + stiffness composite tokens). For platforms without
springs, M3 publishes the exact conversion — directly usable with
`gfx::CubicBezier`:

| Spring | Cubic-bezier | Duration |
| --- | --- | --- |
| Expressive fast spatial | 0.42, 1.67, 0.21, 0.90 | 350ms |
| Expressive default spatial | 0.38, 1.21, 0.22, 1.00 | 500ms |
| Expressive slow spatial | 0.39, 1.29, 0.35, 0.98 | 650ms |
| Expressive fast effects | 0.31, 0.94, 0.34, 1.00 | 150ms |
| Expressive default effects | 0.34, 0.80, 0.34, 1.00 | 200ms |
| Expressive slow effects | 0.34, 0.88, 0.34, 1.00 | 300ms |
| Standard fast spatial | 0.27, 1.06, 0.18, 1.00 | 350ms |
| Standard default spatial | 0.27, 1.06, 0.18, 1.00 | 500ms |
| Standard slow spatial | 0.27, 1.06, 0.18, 1.00 | 750ms |
| Standard fast effects | 0.31, 0.94, 0.34, 1.00 | 150ms |
| Standard default effects | 0.34, 0.80, 0.34, 1.00 | 200ms |
| Standard slow effects | 0.34, 0.88, 0.34, 1.00 | 300ms |

Note the **y2 greater than 1** on every spatial curve: these overshoot. That is
the expressive feel, and it is the thing that will make our animations read as
M3 rather than as a generic ease-out.

Split: **spatial** curves move things, **effects** curves change colour and
opacity.

### 1.6 Type — five roles by three sizes, two sets

Roles: display, headline, title, body, label. Sizes: large, medium, small.
15 baseline styles plus **15 "emphasized"** (heavier, added in Expressive) for
selection, primary actions, and headlines.

Standard scale (sizes in sp; confirm each against the spec's token table when
implementing):

| | Large | Medium | Small |
| --- | --- | --- | --- |
| Display | 57 | 45 | 36 |
| Headline | 32 | 28 | 24 |
| Title | 22 | 16 | 14 |
| Body | 16 | 14 | 12 |
| Label | 14 | 12 | 11 |

Buttons use **label-large**. Dialog headlines use **headline-small**.
The scale supports a **brand typeface** (display/headline) distinct from a
**plain typeface** (body/label).

### 1.7 Components — what changed that we care about

**Buttons.** Five sizes (XS/S/M/L/XL), five colour styles (elevated, filled,
tonal, outlined, text), round *or* square, plus toggle variants.
**Shape morph on press** is the signature: the corner squares up while held.

| | XS | S | M | L | XL |
| --- | --- | --- | --- | --- | --- |
| Round | full | full | full | full | full |
| Square | 12 | 12 | 16 | 28 | 28 |
| **Pressed** | 8 | 8 | 12 | 16 | 16 |

Small-button padding moved from 24dp to **16dp**.

**Button groups.** Two variants. *Standard* groups let a pressed button bump its
neighbours' width. *Connected* groups (what the delete dialog now uses) specify
**2dp inner padding** and inner corners of XS 4 / S 8 / M 8 / L 16 / XL 20.

> Our dialog pair uses a 4dp gap and 4dp inner corners, derived from optical
> roundness (28 - 24). The connected-group spec overrides that with 2dp / 8dp.
> Worth reconciling; see section 5.

**Toolbars** (new). 64dp tall, docked or floating, standard
(`surface-container`) or vibrant (`primary-container`). Explicitly a slot
container — icon buttons, buttons, text fields.

**Menus.** New *vertical menu* variant: rounded corners, standard
(surface-based) or vibrant (tertiary-based), selected item on
`tertiary-container`, and **shape morph as focus moves into a submenu**.
Baseline menus (square corners) still work but are no longer the recommendation.

**Dialogs.** 28dp container, `surface-container-high`, 24dp padding all round,
min 280 / max 560 wide, 16dp title to body, 24dp body to actions, 8dp between
buttons.

**Navigation rail.** Collapsed and expanded variants; the baseline rail is
deprecated. Active destination is a `secondary-container` pill with
`on-secondary-container` ink.

Also new: split buttons, FAB menu, loading indicator, carousel, and reworked
progress indicators (waveform, configurable thickness).

---

## 2. Retiring Nothing OS

**Decided: Zephyrus adopts Material 3 whole. The Nothing OS language is
removed, not adapted.**

The rest of this section records what that costs and what has to be deleted, so
nobody re-derives the old rules from leftover comments six months from now.

### What is being removed

| Removed | Replaced by |
| --- | --- |
| 8-role `Palette` as the source of truth | The full 26-role M3 scheme, generated from a seed |
| Binary pill/card shape rule | M3's ten-step scale + component mappings (Rule 1) |
| **Hairline separation** (`rule`, 1px) | **Tonal separation** — surface-container levels |
| One accent, no secondary/tertiary | Real `secondary` and `tertiary` tonal palettes |
| "Separation is a line, not a shadow" | M3 elevation: tonal first, shadows where specified |
| Ad-hoc Segoe UI strings | Roboto + the M3 type scale |

`zephyrus_bubble_style.h` does not survive this in its current form. `Palette`
becomes a thin compatibility shim over the M3 tokens so the ~60 existing
`Ground()` / `Ink()` / `Accent()` call sites keep compiling through Phases 0–3,
and is deleted in Phase 4 as each surface is converted.

### The one thing kept: the brand colour as seed

M3 is designed to be seeded from a brand colour — that is the normal way to use
it, not a deviation. **Pomegranate `#C6102E` becomes the seed** of the primary
tonal palette, with secondary and tertiary generated from it by the standard HCT
derivation rather than picked by hand.

This is the single assumption in this rewrite. The alternative reading of
"fully Material" is baseline M3 (the default purple), which would discard the
brand outright. If that is what was meant, say so and the seed changes — it is
a one-line change in `ref_color_mixer.cc` and nothing else in this plan moves.

### What this costs, stated plainly

**The browser will look less distinctive.** The greyscale-plus-one-red palette
and the hairline rule were the two things that made Zephyrus recognisable at a
glance. Replacing them with tonal surfaces and a generated three-accent scheme
means Zephyrus will read as a well-built Material browser rather than as its own
thing. That is a real trade and it is being made deliberately.

**Two prior decisions are reversed.** Decision 4 in the first draft kept
hairlines; decision 3 kept Segoe UI. Both flip. Hairlines become
`outline-variant` used only where M3 uses it (dividers inside lists), not as the
general separation mechanism.

### Superseded notes

`zephyrus-permanent-theme` and `zephyrus-pomegranate-design-language` describe
the retired language. They are superseded by this document for everything except
the brand seed value.

---

## 2b. The conflict this replaces *(historical)*

`zephyrus_bubble_style.h` is not a loose collection of colours. It is a written
design language — Nothing OS — and it contradicts M3 on three axes. Pretending
otherwise would produce a UI that is neither.

| | Zephyrus today | M3 |
| --- | --- | --- |
| Colour roles | **8** (ground, surface, rule, ink, muted, faint, accent, accent-ink) | **26** |
| Shape | **Binary by rule.** The header says: pill or card, *"nothing in between. A 12px or 16px radius is always one of these two, chosen wrong."* | **Ten steps**, whose centre of mass is exactly 12 / 16 / 20 |
| Separation | **1px hairline** (`rule`) — *"separation is a line, not a shadow"* | **Tonal difference** between surface containers |
| Accent | **One**, deliberately | primary + secondary + tertiary |

The shape rule is the sharp one. It is stated as a measured finding (144
elements at full pill, 16 at 8px, 14 at 6px) and it forbids precisely the range
M3 lives in.

*(Superseded. The recommendation here was "M3 as the grammar, Nothing OS as the
vocabulary" — keep the greyscale ramp, the single accent, and hairlines, and
take only M3's structure. It was rejected in favour of adopting M3 whole. Kept
so the reasoning is on record, not as guidance.)*

---

## 3. What Chromium already gives us

This is the part that makes the job tractable, and it is why the *mechanism*
genuinely can stay put.

| Need | Already in tree |
| --- | --- |
| M3 colour roles | **113 `kColorSys*` tokens** in `ui/color/color_id.h` — Chromium's `ui/color` is already an M3 port |
| Themed ramp | `ui/color/ref_color_mixer.cc` — **we already own this** (Pomegranate) |
| Shape scale | `views::LayoutProvider`, `ShapeContextTokens` to `ShapeSysTokens` (6 steps today; M3 needs 10) |
| Type scale | `views::TypographyProvider` |
| Spring curves | `gfx::CubicBezier` (`ui/gfx/geometry/cubic_bezier.h`) |
| State layers | `views::InkDrop` — already wired into most upstream buttons |

Four of the six subsystems have a provider seam. **That seam is the strategy for
COLOUR** — change `ColorProvider` and essentially every surface follows, which
Phases 0 and 0b proved.

> **CORRECTION, 2026-09-15, measured after Phase 1's first build.** The same
> claim does NOT hold for shape, and assuming it did made Phase 1 land on almost
> nothing. In `chrome/browser/ui/views`:
>
> | | Files |
> | --- | --- |
> | Use `ShapeContextTokens` (the seam) | **19** |
> | Use `GetCornerRadiusMetric` at all | 43 |
> | Draw rounded rects with **hardcoded** values | **103** |
>
> So the shape provider reaches under a third of the files that draw rounded
> shapes, and WebUI does not read it at all — its radii are CSS. Shape is
> therefore a **call-site rewrite**, not a provider change. The token scale is
> still worth having as the single definition of the values, but it does not
> propagate on its own.
>
> Practical consequence: Zephyrus's own surfaces are where the visible change
> lives, because they are what the browser looks like. Do those first and do not
> attempt all 103 upstream files — take the ones actually on screen (menus, the
> omnibox dropdown, bubbles) and leave the rest.

---

## 4. Scale of the work

Measured, not estimated.

**Zephyrus surfaces** — 25 UI `.cc` files, 18,370 lines:

| File | Lines |
| --- | --- |
| `toolbar_view.cc` | 5,306 |
| `zephyrus_sidebar_view.cc` | 2,265 |
| `zephyrus_workspace_manager.cc` | 1,723 |
| `zephyrus_agent_tool_surface.cc` | 1,388 |
| `zephyrus_search_overlay.cc` | 1,211 |
| `zephyrus_privacy_popup.cc` | 960 |
| `zephyrus_settings_popup.cc` | 671 |
| `zephyrus_workspace_icons.cc` | 584 |
| `zephyrus_tab_switcher.cc` | 543 |
| 16 more | under 500 each |

Within those: **91** rounded-rect call sites, **508** `gfx::Insets` literals,
**14** ad-hoc font derivations.

**Chromium surfaces** — 1,928 views `.cc` files, 129 dialog delegates,
60 bubble dialogs.

**Two conclusions follow.**

1. The 1,928 files cannot be hand-edited and must not be. They move through
   `LayoutProvider` / `TypographyProvider` / `ui::ColorProvider` or they don't
   move.
2. Zephyrus's 508 Insets literals and 91 radius sites *are* hand work, but they
   are bounded — one file at a time, in the order of section 6.

**A defect worth fixing on the way through:** fonts are currently ad-hoc string
literals — `"Segoe UI Semibold, 15px"`, `"Segoe UI, 11px"`,
`"Inter, Segoe UI, 13px"`. There is no Inter in the tree, so the search overlay
has been silently falling back to Segoe UI this whole time. There is no type
scale at all today.

---

## 5. Decisions needed before Phase 1

These change what gets built. Recommendations given; all are the user's call.

**1. Shape scale** — **DECIDED: M3's ten steps**, with M3's component mappings.
See Rule 1. The five-step Zephyrus scale is withdrawn.

**2. `secondary` and `tertiary`** — **DECIDED: real tonal palettes**, generated
from the seed by the standard HCT derivation. Not greys.

**3. Typeface** — **DECIDED: Roboto**, bundled. Reversed from the first draft.
The M3 type scale's sizes, line heights and tracking are specified against
Roboto's metrics; running it on Segoe UI means every value is approximately
wrong. Consequences to handle in Phase 2: add Roboto (and Roboto Mono) to the
tree, licence review (Apache 2.0, should be clean), and a fallback chain of
Roboto → Segoe UI → system for scripts Roboto lacks.
*Open sub-question:* Roboto Flex as the brand typeface for display/headline is
optional and can wait.

**4. Hairlines or tonal separation?** — **DECIDED: tonal.** Reversed from the
first draft. `outline-variant` narrows to M3's own use (dividers inside lists
and menus); it stops being the general separation mechanism. Expect this to be
the **single most visible change** in the whole overhaul — every panel edge in
the browser currently reads as a line and will start reading as a tone step.

**5. Shape morph on press** — **DECIDED: adopt.** Unchanged from the first
draft, and now mandatory rather than optional: it is part of M3's button spec,
not a flourish. Replace the existing 0.97 scale transform with a corner-radius
morph to 8 / 12 / 16 by button size.

**7. Vibrant variants.** M3 offers "vibrant" colour schemes for menus
(tertiary-based) and toolbars (primary-container-based) alongside the standard
surface-based ones.
*Recommend:* standard everywhere for now; revisit once the scheme is live. They
are a per-component swap, not an architectural choice, so deferring costs
nothing.

**6. Reconcile the delete dialog's connected group.**
It currently uses a 4dp gap and 4dp inner corners, which I justified as
concentricity (28 − 24). That justification was wrong: those corners are
**interior seams between two segments of one control**, which is exemption 2
under Rule 2 — they are not nested inside the dialog, so concentricity never
governed them. The connected-group spec does: 2dp gap, 8dp inner.

The buttons' *outer* corners are capsules, exemption 1, so they stay pills.

*Recommend:* move to 2dp / 8dp once the shape tokens exist. Adopting Rule 2 as
a hard rule does not change this case — it clarifies that it was never a
Rule 2 case.

---

## 5b. Progress

- **Phase 0 — DONE.** 26-role M3 scheme in `zephyrus_color_mixer.cc`, built from
  ref tone ramps (not `kColorSys*` — Chromium has no surface-container tokens;
  the odd neutral stops 4/6/12/17/22/92/94/96/98 exist precisely for them).
  Legacy 8 renamed `kColorZephyrusLegacy*` and left byte-identical. Verified
  visually unchanged.
- **Phase 0b — DONE.** Legacy ids repointed at M3 roles. Turned on tonal
  separation, which light mode had never had (ground and surface were both
  `neutral100`) and which dark mode had backwards (ground *lighter* than the
  panel raised off it).
- **Unplanned, found on the way:**
  - `ThemeService::GetBrowserColorScheme()` was hard-pinned to `kDark`, so the
    OS setting was never read and Customize Chrome's light/dark control wrote a
    pref nothing read back. Now pref-driven, `kSystem` coerced to dark, "Device"
    chip removed from the selector as it could not do what it said.
  - The chrome band had **four** independent painters. See
    `[[zephyrus-chrome-plane-sources]]`; all now on `surface-container`.
- **Phase 1 — IN PROGRESS.** Shape scale landed in `layout_provider`. Still to
  do: `ConcentricInner` adoption, the debug Rule-2 check, retiring
  `kRadiusCard`/`kRadiusPopup`, and the 91 Zephyrus rounded-rect sites.

**Phase 5 must move earlier.** It was scheduled last on the assumption that
Chromium's surfaces are a separate concern. They are not — they interleave with
ours on the same screen, and every mismatch so far has been a Zephyrus surface
sitting next to an upstream one on a different system. Do it right after
Phase 1.

---

## 6. Phases

Each phase is independently shippable and independently revertable. **One
structural change per build** — the standing rule applies throughout.

### Phase 0 — Token foundations *(no intended visual change)*

The riskiest phase to get wrong and the cheapest to verify: if anything looks
different at the end of it, something is wrong.

- **Generate the real M3 scheme** from the Pomegranate seed: full tonal palettes
  for primary, secondary, tertiary, neutral and neutral-variant, resolved into
  all 26 roles for light and dark. `ref_color_mixer.cc` already carries the
  ramp; this extends it rather than replacing it.
- Add `zephyrus_tokens.h` exposing those roles, mapped onto Chromium's existing
  `kColorSys*` so our surfaces and Chromium's read one graph.
- **Turn `Palette` into a shim.** Its 8 accessors start answering from the M3
  roles (`Ground()` → surface, `Ink()` → on-surface, `Accent()` → primary,
  `Rule()` → outline-variant) so all ~60 call sites keep compiling untouched.
  It is deleted surface-by-surface in Phase 4.
- Add state-layer helpers at spec opacities (8 / 10 / 10 / 16 / 38).
- **Verify:** screenshot every surface before and after. The shim means they
  should be *near* identical — small tone shifts are expected where a hand-picked
  grey is replaced by its generated equivalent. Anything larger than a tone step
  is a mapping bug, not a design change.

### Phase 0b — Apply the scheme

Separated from 0 deliberately, because this is where the browser starts looking
different and it must be revertable on its own.

- Switch separation from hairlines to **surface-container levels**. Narrow
  `outline-variant` to dividers inside lists and menus.
- Apply M3's default component surface mappings: dialogs to
  `surface-container-high`, menus and navigation to `surface-container`,
  elevated buttons to `surface-container-low`.
- Introduce `secondary` and `tertiary` where M3 specifies them — selected
  navigation items, filter chips, selected menu items.
- **This is the phase that retires the look.** Expect it to feel wrong for a
  day; the browser will not look like itself, by design.

### Phase 1 — Shape

- Extend `ShapeSysTokens` to M3's ten steps and wire `ShapeContextTokens` to
  M3's component mappings (Rule 1).
- Add per-corner / asymmetric support and `zephyrus::ConcentricInner(outer,
  padding)` (Rule 2).
- **Add the concentricity check.** Debug-only: when a rounded container paints
  a rounded child, assert the child's radius equals
  `ConcentricInner(parent, padding)` unless it is a capsule or a connected-group
  segment. This is what turns Rule 2 from a note into a rule.
- Retire `kRadiusCard` / `kRadiusPopup` / `kCornerRadius` in favour of tokens;
  rewrite the 91 rounded-rect sites.
- Rewrite the stale shape comment in `zephyrus_bubble_style.h`.
- **Audit every nesting pair against Rule 2**, starting with the ones we have
  already fixed by hand and can regress: web contents inside the window frame,
  the WebView inside the customize panel, the tab switcher's inner tiles,
  sidebar rows, search-overlay suggestion rows, privacy-popup rows.
- **Per the DPI memo:** every layer-rounded radius must land on whole device
  pixels. At 1.5x that constrains the scale — verify 8 and 16 render crisp
  before committing. Note that Rule 2 produces *computed* radii, so this check
  has to cover derived values too, not just the five steps.

### Phase 2 — Type

- **Bundle Roboto and Roboto Mono**, with a Roboto → Segoe UI → system fallback
  chain. Licence review first (Apache 2.0, expected clean).
- Add the M3 type scale (15 baseline + 15 emphasized) on `TypographyProvider`,
  at M3's specified sizes/line-heights/tracking — which only hold against
  Roboto's metrics, hence the bundling above.
- Delete every ad-hoc font string, including the dead Inter reference.
- Apply: dialog headlines to headline-small, buttons to label-large, body to
  body-medium.

### Phase 3 — State and motion

- Replace every hand-rolled hover/press alpha with the state-layer helper.
- Add spring tokens as `gfx::CubicBezier` constants from section 1.5, split
  spatial and effects.
- Adopt press shape-morph (decision 5).
- Honour `gfx::Animation::ShouldRenderRichAnimation()` everywhere, as the
  existing press feedback already does.

### Phase 4 — Zephyrus components

In this order — highest visibility and highest existing debt first:

1. `toolbar_view.cc` (5,306 lines; the workspace strip, dialogs, popups)
2. `zephyrus_sidebar_view.cc` to an M3 **navigation rail**, active destination
   on `secondary-container`
3. `zephyrus_search_overlay.cc` to M3 **search** plus list
4. `zephyrus_tab_switcher.cc` to cards and carousel behaviour
5. `zephyrus_privacy_popup.cc`, `zephyrus_settings_popup.cc` to **menus** and
   side sheets
6. `zephyrus_agent_panel.cc`, `zephyrus_agent_tool_surface.cc` to side sheet
   plus progress
7. `zephyrus_customize_panel.cc`, `zephyrus_profile_dialog.cc`,
   `zephyrus_search_engine_picker.cc`, `zephyrus_workspace_image.cc`

### Phase 5 — Chromium surfaces, through the providers

No per-file edits. Omnibox dropdown, tab strip, bookmarks bar, all 60 bubbles
and 129 dialogs, context menus, find bar, downloads, permissions.
Then a **manual sweep for surfaces that bypass the providers** with literals —
that list is the actual Phase 5 work, and it cannot be written until Phases 0
and 1 land.

### Phase 6 — WebUI

Settings, history, downloads, the privacy dashboard, the adblock page. Emit the
token set as CSS custom properties from the same source, so WebUI and native
cannot drift. Note the standing Release trap: page JS 404s unless the file is
listed in `optimize_webui_in_files`.

---

## 7. Risks

- **Phase 0 is a silent-breakage phase.** A wrong role mapping shows up three
  phases later as "that popup is the wrong grey". Screenshot diffs, not
  eyeballs.
- **`View::PropagateThemeChanged()` walks children before parents.** Any new
  token read in `OnThemeChanged` hits the same one-frame-stale bug that
  `PublishThemePalette()`'s return value exists to work around.
- **Upstream flag flips.** A rebase can swap which implementation runs and
  orphan provider overrides without any test going red.
- **Removing focus rings.** Already done once, on the dialog buttons. If Phase 3
  adopts state layers as the focus indicator system-wide, keyboard focus must be
  re-checked on every surface — M3's answer is a 3dp indicator *outside* the
  component, and we currently have nothing like it.
- **Losing hairlines removes contrast we were relying on.** Tonal separation is
  a smaller visual delta than a 1px line, especially on the dark scheme where
  adjacent surface-container steps are a few points apart. Check every panel
  edge on a real display, not a screenshot — and check it at the dark end,
  which is where tone steps collapse first.
- **The generated scheme can produce a worse red than the hand-picked one.**
  `#C6102E` was chosen; its HCT-derived tonal siblings are not. Verify primary
  at tone 40/80 against both grounds before accepting the generated ramp, and
  pin the value if the derivation comes out muddy.
- **The capsule exemption is load-bearing.** Drop it and strict concentricity
  flattens every nested button into a small rounded rectangle. If a future
  change makes pills look wrong inside a container, the answer is to change the
  padding, never to make the pill concentric.
- **DCHECKs are off in `out/Release`.** Paint DCHECKs (ALIGN_TO_HEAD, unnamed
  focusable views, subpixel-on-transparent) will not fire during this work.
  Run `out/ASAN` at the end of each phase.

---

## 8. Not in scope

The theming *mechanism*: `ref_color_mixer.cc`'s ramp, `PublishThemePalette()`,
the `PaletteFrom(provider)` snapshot rule, Customize Chrome integration, the
private-workspace grey palette, and the `Current()` single-slot limitation.
All keep working as they do today.
