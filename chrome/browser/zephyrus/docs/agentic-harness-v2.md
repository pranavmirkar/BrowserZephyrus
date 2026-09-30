# Agentic Harness v2: bring any model, the browser does the rest

Status: **accepted, 28 September 2026.** The four decisions at the end were
taken as recommended; see ADR 0004, which also moves the provider adapters into
the kernel (P1 below describes them in the browser; the ADR supersedes that).
Vocabulary is `CONTEXT.md`'s. Builds on ADR 0001 (kernel in its own process)
and ADR 0003 (a Task is bound to one Workspace), both of which stand.

## 1. What "transcends" has to mean

"More powerful than Claude Code and Codex" is not something a benchmark can
confirm, and those two are coding harnesses, so they are the wrong yardstick
on their own. What we can make true and prove is this:

> **Give Zephyrus any capable model and it completes more real web tasks,
> cheaper and faster, than that same model does in any other harness, and no
> page can make it act against its user, whatever the model.**

Every part of that sentence is measurable. It turns into five targets:

| # | Target | How it is measured |
|---|---|---|
| T1 | Same model, higher success than other harnesses | WebArena (the one web benchmark not saturated) + our recorded real-site suite, same model in Browser Use and in Zephyrus |
| T2 | Page injection moves no action, for any model including 1.5B | Adaptive attack suite, attacker who knows the defence, target 0 successful exfiltrations or unrequested R2/R3 actions |
| T3 | Lower cost and latency per completed task | Tokens and seconds per success, from the benchmark's per-task accounting |
| T4 | Tasks that other harnesses cannot express | Parallel, long-running, resumable, cross-page tasks in the suite |
| T5 | Works with any provider the user picks | The same suite passes through every shipped adapter |

Why these and not a success number: WebVoyager (99%) and Online-Mind2Web (97%)
are saturated, so leading them proves nothing. WebArena tops out near 74%.
Security is where every current harness is weakest. Their approach is
detection or asking the model nicely, and adaptive attacks break those.
The research field has converged on enforcing policy outside the model, which is
what our kernel already is. That is our opening.

## 2. Where we actually are

Built, measured, shipping in `out/Release`:

- Kernel (Rust policy + C++ loop) in a sandboxed `kService` process; 12 lines of
  upstream patch.
- 18 tools, frozen contract, risk classes R0-R3, approval that lifts Ask and
  never Deny, sensitivity labels (passwords and cards never offered).
- Accessibility-tree Observation, acting by coordinates through the input
  pipeline (not AX actions, which were proven not to work).
- Benchmark that drives the SHIPPED loop and kernel (no copies left).
  Claude Opus: 12/12 tasks; qwen2.5:7b: 7/12, 0 trapped.

What limits it:

- **Only a local model can be connected.** `DevModelClient` enforces loopback,
  behind a developer switch. There is no way to add a cloud key.
- **One action per model call, no memory across pages, no plan.** "Compare two
  prices" is impossible whatever the model.
- **Text-JSON tool calls** even when a provider offers native tool calling.
- **Actions are fire-and-hope.** The loop learns what happened only by
  re-observing.
- **Security is call-level, not data-level.** The kernel judges each call, but
  it cannot tell whether the text being typed came from the user or from the
  page.
- **One Task, one tab, foreground only.**

## 3. The design: seven pillars

Each pillar is something a browser-native harness can do and an extension, a
CDP driver or a screenshot agent structurally cannot, or it is the enabler for
one. The kernel stays the brain (ADR 0001): it decides, the browser executes.

### P1. Bring your own model

- **Provider adapters in the browser process** (the kernel has no network, by
  design): Anthropic, OpenAI-compatible (covers OpenAI, OpenRouter, DeepSeek,
  Jev, vLLM, Ollama), Google Gemini. One normalised request/response shape
  crossing a widened `AgentModel` mojom: messages, native tool definitions,
  tool calls, usage, stop reason.
- **Native tool calling** where the provider has it; text extraction (the
  kernel's, already shipped) as the fallback. The kernel validates either way.
- **Keys** encrypted with OSCrypt (DPAPI on Windows), never in a renderer,
  never in the kernel, never logged. Settings page to add, test and remove.
- **Capability probe** on connect: context size, vision, tool calling, speed and
  price. The harness adapts its prompt and observation budget to the model,
  instead of assuming one.
- **Routing**: optionally a stronger planner model and a cheaper actor model,
  plus a money budget per Task alongside the step budget.
- **Prompt caching** of the stable prefix (rules, tool listing) where the
  provider supports it.

### P2. Context engine v2: plan, memory, compaction

- **Plan** owned by the kernel: the model writes and revises a short step list;
  the kernel tracks it and shows it in the panel. Planning is explicit, not a
  paragraph of hidden reasoning.
- **Working memory** (`notes.write`, `notes.read`): facts the Task collected, kept
  across pages. This alone unlocks compare, collect and summarise-across-sites
  tasks.
- **Compaction**: old steps become a summary, not dropped; the budget is tokens,
  not a line count.
- **Observation levels used adaptively**: L0/L1/L2 and a screenshot only when
  the model can use it and the AX tree is ambiguous (canvas, maps, charts).

### P3. Verified actions

Every acting tool call carries an **expectation** the browser checks: navigation
committed, a field now holds the text, a dialog opened, a network request went
to a named origin. The browser reports "did / did not / did something else",
which is the web's equivalent of a coding agent running its tests. R2/R3 actions
get a **preview**: the form fields and the destination that would be submitted,
shown in the approval, before anything leaves the machine.

This is where being the engine pays: we see DOM mutations, navigations and the
tab's network requests directly. An extension or a screenshot agent has to guess
at them.

### P4. Provenance-enforced security

The kernel becomes a reference monitor over **where data came from**, not just
which tool is called:

- Every span the model is shown carries a label: the user's words, the browser's
  own state, or page content from origin X.
- At a **sink** (a navigation URL, typed text, a send/submit, a download) the
  kernel checks the value's provenance. Text that traces only to page content
  from another origin cannot flow out; text from the user's Intent can.
- The model still sees the page (full utility), unlike plan-first designs such
  as CaMeL, which keep only up to 57% of a frontier model's success. We
  constrain the **data flow to sinks**, not the model's view.
- **Strict mode** for high-risk Tasks: plan fixed from the Intent before any
  page is read, page content only fills pre-approved slots. It costs utility,
  so it is a choice, not the default.

Claim to prove (T2): with provenance on, an adaptive attacker achieves zero
exfiltrations and zero unrequested R2/R3 actions, with the weakest model we
support. The benchmark already showed models of every size obey injections, so
this cannot rest on the model.

### P5. Parallel, durable, background Tasks

- **Sub-Tasks in background tabs** of the same Workspace (ADR 0003 holds), run
  concurrently, each with its own budget, results returned to the parent's
  memory. "Check these five sites" runs in the time of one.
- **Durable**: Task state checkpointed and encrypted, so a Task survives a
  browser restart and resumes. Resume after approval already carries history.
- **Background and scheduled**: a Task can run while the user browses, and on
  a schedule ("every morning, check…"), with a per-run budget and a report.

### P6. Skills: learn a site once, replay without the model

A completed Task can be saved as a **skill**: the verified sequence of actions
with the slots that varied (a search term, a date). Next time the kernel replays
it deterministically, calling the model only where a step's expectation fails
or a slot needs judgement. On a known site, this makes Zephyrus faster and
cheaper than any model-in-the-loop harness, and it is where T3 is won.
User-written skills and an MCP client come later, and every call they make still
goes through the kernel's policy.

### P7. Evaluation is part of the product

- **Record real pages** (the open harness item 8): capture Observations and
  outcomes from real browsing, replay them deterministically.
- **Model report card**: when a user connects a model, run a short local suite
  and show what it is good at, what it costs, and where the harness will ask
  more often.
- **Adaptive injection suite** that knows the defence, run on every kernel
  change.
- **WebArena subset** for a number the outside world recognises.

## 4. Sequence

Each phase ships something usable and has a gate that must pass before the next.

1. **BYO model (P1).** Adapters, keys, native tool calling, capability probe,
   money budget, per-workspace consent. Gate: the 12-task suite passes through
   all three adapters in the real browser, with keys never visible outside the
   browser process (tested).
2. **Measure first (P7 core).** Real-page recorder, adaptive injection suite,
   WebArena subset, baselines for Claude/GPT/Gemini and a local model *before*
   the next phases change anything. Gate: baselines recorded, reproducible.
3. **Memory, plan, compaction (P2).** Gate: cross-page tasks (compare, collect)
   added to the suite and passing with a mid-size model.
4. **Verified actions (P3).** Gate: wasted steps down measurably against the
   phase-2 baseline; every R2/R3 approval shows a preview.
5. **Provenance security (P4).** Gate: T2, zero adaptive-attack successes across
   all adapters, utility within 5% of the phase-4 number.
6. **Parallel and durable Tasks (P5).** Gate: a 5-site task finishes in under
   twice the time of a 1-site task; a Task survives a browser kill.
7. **Skills (P6).** Gate: a replayed skill completes with at most one model
   call, and self-heals a changed page in the suite.
8. **Head-to-head (T1).** Same models in Browser Use and Zephyrus, same tasks,
   published numbers.

### Phase 1 status (2026-09-28)

Built: kernel adapters for all three providers (`kernel/src/providers.rs`);
the loop's cloud path (`TaskLoop::StartCloud`, `mojom::ModelTransport`,
`mojom::CloudModel`) with retries that cost no step and a spending limit the
kernel enforces from the usage it parses; the browser transport
(`agent/model_transport.cc`: configured host only, one path per provider,
header allowlist, key added last, no credentials, size caps); settings
(`agent/model_settings.cc`: OSCrypt-encrypted keys, per-Workspace consent,
price table, clamped limit); the controller choosing cloud or local and
carrying the budget across approval resumes; the Model settings bubble in the
agent panel; the benchmark driving the same cloud path through the loop probe.

Gate still open: the 12-task suite through all three adapters in the real
browser, with the user's own keys.

## 5. Risks, stated plainly

- **Privacy promise vs cloud.** Zephyrus is a privacy browser; sending page
  content to a cloud model is the opposite of that unless the user chose it,
  per Workspace, knowingly. Local-only mode stays first class.
- **Cost surprises.** A runaway loop on a frontier model spends real money.
  Money budgets are not optional.
- **Provenance is matching, not proof.** A model can paraphrase page text past
  a substring check. P4's claim must be tested by an adaptive attacker who knows
  this, and the strict mode exists for when matching is not enough.
- **Sites fight automation.** Bot detection, CAPTCHAs (which we will not solve)
  and terms of service. The agent acts through the real input pipeline in the
  user's own session, which helps, but it will not win every site.
- **Upstream patch budget (400 lines).** Settings and network code tempt
  patches into upstream files. Everything new lives under `zephyrus/`.
- **Scale of work.** This is months, not weeks, for a small team. The phases are
  ordered so every one ships something usable on its own.

## 6. Decisions needed before Phase 1

1. **Cloud default.** Off until the user turns it on per Workspace, with
   sensitive fields always withheld (recommended), or on for all Workspaces
   once a key is added.
2. **First providers.** Anthropic, OpenAI-compatible, Gemini (recommended: all
   three; OpenAI-compatible already covers OpenRouter, DeepSeek, Jev and local
   servers).
3. **Security default.** Provenance monitor with full page view (recommended),
   or plan-first strict mode for everything.
4. **Where to start.** Phase 1 then 2 as ordered (recommended: everything after
   depends on connecting real models), or measurement first.
