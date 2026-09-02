# Zephyrus glossary

Canonical vocabulary for the Zephyrus fork. Terms only. No implementation
details, no decisions, no plans — those belong in `docs/adr/` and the code.

Add a term when it is resolved, not when it is proposed.

---

## Browser concepts (established, shipping)

**Workspace**
A named grouping of tabs with its own cookies and site data, backed by a
StoragePartition inside the single browser Profile. Identity in Zephyrus is the
workspace, not the profile. Only one workspace is *visible* in a window at a
time.

Not: a Chrome profile. Not a window. Not a tab group.

**Private Workspace**
The off-the-record workspace. Backed by an OTR Profile rather than a partition,
so it is the one workspace that is not a partition. Leaves nothing on disk.

**Pinned tab**
A tab pinned within one workspace. Pinning is per-workspace; a pinned tab is not
visible in other workspaces.

**Shield**
The ad and tracker blocking surface, and its title-bar counter.

**Privacy Intelligence**
The subsystem that records which third parties a page contacted and attributes
them to known entities. It is about *observed page behaviour*. It is not about
AI data handling.

---

## Agent concepts (new, being designed)

These are pinned here because the source PRD used several of them
interchangeably and at least two collide with browser terms above.

**Intent**
What the user asked for, in their words. One utterance. Trusted input.

**Task**
One unit of agent work pursuing an Intent, with a lifecycle, a step budget and a
terminal outcome. A Task is the thing that can be cancelled, resumed and
audited.

Not a "session". A session may contain many Tasks.

**Observation**
A single structured snapshot of browser state, produced by the browser and
addressed to the model. Versioned and budgeted.

Distinct from **Context**, which is the accumulated, compacted state the model
reasons over across a Task. An Observation is an input to Context; they are not
synonyms and the PRD conflated them.

**Tool**
A named, schema-typed browser capability the model may request. Tools are the
*only* way a model can affect the browser.

Not "action" (what happens when a tool runs) and not "capability" (the
authorization to run one). Keep the three separate.

**Risk class**
R0 read-only, R1 reversible, R2 external side effect, R3 high impact. A property
of a Tool, not of a Task.

**Policy**
The rules deciding whether a requested Tool call is allowed to execute, given
the risk class, origin and user approvals. Enforced outside the model.

Not to be confused with adblock filter rules, which the codebase also calls
"rules". Prefer "filter rules" for those.

**Approval**
A user decision authorizing one Tool call, or a class of them, at a stated
scope. Scopes: once, session, site, always.

**Task scope**
The browser state a Task is permitted to observe and act on. Whether this is the
same boundary as a Workspace is an open decision, not settled vocabulary.

**Agent Kernel**
The component owning Task lifecycle, Policy, Context and model routing. It
decides; it does not execute. Chromium executes.

**Model worker**
The isolated process running local inference. Replaceable. Never in the browser
or renderer process.

**Local-only mode**
A mode in which no Task content reaches any model provider over the network.
Ordinary web browsing traffic is unaffected and continues normally — the
distinction must be visible in the UI, because "local AI" does not mean "no
network".

---

## Terms deliberately avoided

**"Agent"** on its own. Ambiguous between the kernel, the model, and the loop.
Say Agent Kernel, model, or Task.

**"Session"** as a synonym for Task. A browsing session is a different thing and
already means something in Chromium.

**"Context"** as a synonym for Observation. See above.
