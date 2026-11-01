# Agent v2: plan (2026-10-06)

Supersedes section 8 of HANDOFF.md where they differ. Local work only while the
hackathon freeze holds (no pushes, no installers, ask before anything that
publishes).

## 0. What the code does today (verified 2026-10-06)

- **Every step is a fresh, stateless request**: one `system` string and ONE `user`
  string (`TaskLoop::UserPrompt`, `build_provider_request(kind, model, system,
  user, image, ...)`). The "history" is rewritten into that user string each step
  (last N lines), with the observation, memory, notes and the task re-stated.
- So **prompt caching only covers the system prompt and tool list** (~7.6k
  tokens, `cache_control` on the system block). The per-step part (~1.8k fresh
  tokens: observation 100-600, screenshot ~700, history) is paid in full every
  call, and the model never sees its own earlier tool calls as turns.
- Cloud turns already batch up to 8 tool calls, each policy-judged. Effort is
  `low`, escalating to `medium` after a refused call or step 8.
- Measured on the benchmark (Sonnet 5.5): a 5-step compare task is ~$0.045;
  2-7 s per model reply dominates wall time.

Consequence: **the largest structural gain is not skills, it is a real
conversation** (append-only turns in the provider's native tool-calling format).
It makes the whole history cacheable, shows the model its own calls as calls, and
is the foundation the plan/expectation protocol needs.

## 1. Goals, restated honestly

Task tiers, because "1-2 model calls" is only reachable when the structure of the
site is known:

| Tier | What | Target |
|---|---|---|
| A: skill-covered | known site, known verb ("search flights", "add to cart") | <= 2 calls, <= 8 s, <= $0.015, >= 95% |
| B: open web, simple | unknown site, 3-6 actions | <= 4 calls, <= 15 s, <= $0.03, >= 90% |
| C: hard | canvas, login walls, long flows | success rate and graceful hand-off, not cost |

Hard gates that never relax: injection and approval tests stay at 100%; money
and sign-in always go to the user; a change that lowers success by more than the
noise band does not ship, whatever it saves.

## 2. Evaluation first (Phase 0, 2 days)

Without a bigger benchmark every number below is an anecdote (12 tasks, 1 run).

- **S1 hermetic suite, ~60 tasks** on the local fixture server: forms, tables,
  pagination, delayed SPA focus, modals and cookie banners, iframes, shadow DOM,
  multi-tab, downloads, redacted fields. Success is judged from what the SERVER
  received (the request log), never from the model's own report.
- **S2 real-site suite, ~25 read-only tasks** on stable public sites. Flaky by
  nature: run weekly, report with intervals, never use as a gate.
- **S3 safety suite, ~20 cases**: prompt injection (page, tool result, memory),
  approval and hand-off, forbidden typing.
- **Per call telemetry**: input fresh / cache read / cache write / output tokens,
  time to first token vs total, observation size, screenshot size, model, effort.
- **Arms**, same model, 3 runs each:
  A0 today; A1 screenshots off; A2 + Page Card; A3 + conversation + deltas;
  A4 + plans; A5 + skills.
- **Budget**: S1 is ~60 x $0.045 = ~$2.7 per arm-run; the first ablation
  (A0, A1 x 3 runs) is ~$16; the full six-arm study on S1 plus S3 is ~$60-70.
  Check credits first (they ran out on 2026-09-30); one arm at a time.

Gate 0: decide the screenshot policy from A0 vs A1 (screenshots are ~20% of
fresh input; keep them only if they buy measurable success).

## 3. Build order (each phase ends in a measurement gate)

1. **Page Card + transcript + deltas** (3-4 days). The kernel (Rust) owns the
   transcript as a provider-neutral list of turns; C++ `TaskLoop` feeds it events
   (observation, tool result, user note); the three adapters render it as native
   messages (`tool_use` / `tool_result`). Page Card: regions, interactive
   elements with stable ids and role/name/state, repeated lists collapsed
   ("12 products: name, price, rating + first 5"), 1-2k tokens; after the first
   card only a delta ("cart 0->1, toast appeared, URL changed"); a new card on
   navigation. Cache breakpoint on the last block of the transcript. Compaction
   at ~30k tokens (one cache miss, summarised). The old path stays behind a flag
   until A3 matches A0 on S1 and S3. One structural change: this one.
   Vision becomes on-demand (a cropped region) where the card is unsure.
2. **Reflexes + effect checks** (2 days). Local, no model call: dismiss blocking
   overlays and cookie banners, scroll into view, wait for settle, retry a missed
   click another way, "load more". Every action is checked by its effect and
   reports done / failed-with-reason, never a bare "ok".
3. **Skills** (3 days). Data, not code: a verb, typed args, preconditions, steps
   addressed by role+name, a postcondition. A skill EXPANDS INTO ordinary tool
   calls and every one of them still goes through policy; its risk is derived
   from those calls, never declared, so a skill cannot raise or hide its own
   risk. The browser loads the pack for the current site from the URL (10-20
   verbs in the prompt, never thousands). Start with **generic** skills that
   work anywhere (fill a form from the user's details, search within a site,
   collect a list across pages, compare items in a table), then ~10 site skills.
   Signed with the same pattern as the entity dataset; shipped INSIDE the build
   (there is no update channel yet).
4. **Plans with expectations + cheap queries** (3 days). Extends the existing
   batching: each call may carry `expect` (URL contains, text appears, element
   appears, count changed). The kernel runs the batch, stops at the first miss
   and returns the failure plus a fresh delta and the closest alternatives. The
   model can also ask the agent a question ("list the prices") and get a few
   tokens back. Plans only cover steps that do not depend on UI not yet seen.
5. **Confidence, routing, distillation** (later, only if the ablation supports
   it). A confidence score picks: act, ask the model, escalate, hand to the user.
   Haiku for skill-covered steps, Sonnet for planning, Opus to repair. Skill
   distillation from traces, replayed nightly on PUBLIC sites only (logged-in
   apps cannot be tested in CI).

## 4. Deliberately not doing (yet)

- **Thousands of tools** in the prompt: skills are loaded per site, 10-20 at a time.
- **On-device model**: install size and complexity, unproven benefit. Revisit
  after the routing data exists.
- **Streaming execution**: replies are short (~80 tokens per call), so starting
  the first action early saves ~0.3 s, and the demo proxy refuses `stream` today.
- **Prefetching the next card while the model thinks**: small, and it adds
  races. After the transcript lands, if the profile says it is worth it.

## 5. Risks

| Risk | Handling |
|---|---|
| Transcript refactor breaks one of the three providers | adapters tested against recorded transcripts; Anthropic first, OpenAI-compatible and Gemini by parity tests before the flag flips |
| Skills drift as sites change | postconditions fail loudly and fall back to the model; replay on public sites; never silent |
| A skill pack is trusted input | signed, shipped in the build, risk derived from primitives, injection suite includes skill steps |
| Plans act on a page that changed | expectation checks stop the batch at the first miss |
| Cost of measuring | one arm at a time, `--only`, credits checked before every run |
| Context grows with an append-only transcript | cache reads are ~10% of input price; deltas keep it small; compaction at ~30k |

## 6. Decisions for the user

1. **Budget and credits** for the first ablation (~$16, then ~$60 for the full study). Are the credits behind the demo proxy topped up, or should dev runs use a separate key?
2. **Which ~10 apps get hand-written skills first.** Proposal (public, India-first, demo-friendly): Amazon.in, Flipkart, IRCTC (train search), YouTube, Wikipedia, Google search and Maps, GitHub, plus Gmail and Notion in the user's own session (tested by hand, not in CI).
3. **Skills inside the build** (recommended now) or a signed downloadable pack (needs an update channel the browser does not have).

## 7. General intelligence (added 2026-10-06)

Read as: Zep should be a general assistant that happens to have a browser, not a
browser operator that can also talk. Answer, explain, summarise, research,
reason, calculate and write, and reach for the page only when the page is needed.
(This is not a claim about AGI; every item below is measurable.)

| # | Capability | What it is | Where it lands |
|---|---|---|---|
| G1 | **Answer first** | Decide per message: answer directly, read this page, act, research, or ask. Today the page is observed BEFORE the first model call even for "what is a GST invoice?", which costs a step and ~$0.01 for nothing. Make the first call cheap (title + URL) and let the model request the full card. | with Phase 1 |
| G2 | **Page intelligence** | Summarise, extract to a table, translate, explain, compare across tabs. Long pages and PDFs read in chunks. Answers carry quotes with element ids so a claim can be checked. | with Phase 1 (Page Card) |
| G3 | **Research** | Several sources, opened in parallel background tabs, a source list with citations, contradictions called out, a stop rule and a budget. Needs parallel tasks (a controller per task, hidden tabs). | after Phase 4 |
| G4 | **Compute** | A calculator, unit/currency/date maths as deterministic tools, and a locked-down data tool (no network, no files) to sort, filter and total what was extracted. Models are unreliable at arithmetic on scraped tables; code is not. Risk class: pure computation. | 1-2 days, any time |
| G5 | **Writing for the user** | Draft emails, messages and replies in the user's style (kept in memory, with consent), put them in the compose box, never send unless the task said so (policy already enforces). | after memory v2 |
| G6 | **Clarify and plan** | One good question when the request is ambiguous; a visible checklist for big goals. | with Phase 3 |
| G7 | **Honesty** | Say "not on this page", "I am not sure" and "I cannot do that" instead of guessing prices, dates and numbers. Verify a claim against page text before asserting it. | with Phase 2 |
| G8 | **Multimodal and spoken** | Images, charts and PDFs on pages on demand; optionally speak the answer aloud with the local Windows voices (free, offline), since this is a voice-first product. | later |

Evaluation, a fourth suite **S4, ~40 general tasks**, graded mechanically on
fixture pages that contain known facts AND traps (facts that are NOT there):
direct questions with no page; summarise/extract with required facts and
forbidden invented ones; research over five fixture sites with one contradiction;
computation with known answers; "is X on this page?" where it is not. Metrics:
correctness, groundedness (every claim supported by page text), citation
validity, hallucination rate, and whether it said "I don't know" when it should.

Safety: more reading and writing means more injection surface. Page text stays
data, never instructions; answers cite where they came from; the data tool has
no network and no files; sending anything outward still needs the task to ask
for it.

Order: G1, G2, G7 and G4 first (cheap, and they are what a user perceives as
"smart"); G3 after plans and skills exist; G5 and G8 last.

## 8. Finding that changes section 3, phase 1 (2026-10-07, offline, from recorded runs)

Modelling an append-only, cached conversation against today's stateless prompt on 185 recorded task runs (Sonnet 5.5 prices; cache write 1.25x, read 0.1x):

| Calls in the task | Share of runs | Today | Transcript | Delta |
|---|---|---|---|---|
| 1 | 49% | $0.0060 | $0.0062 | +3% |
| 2 | 22% | $0.0115 | $0.0117 | +2% |
| 3 | 22% | $0.0179 | $0.0194 | +8% |
| 4 | 5% | $0.0266 | $0.0293 | +10% |
| 5+ | 3% | $0.0306 | $0.0348 | +14% |

**On these tasks a transcript costs a little MORE, not less.** The system prompt and tools are already cached; the per-step part is small; and writing a new turn to the cache costs 1.25x what the same tokens cost fresh. The gain the plan assumed only appears if old observations shrink to deltas, and shrinking a cached turn changes the prefix and loses the cache. So the transcript is a reliability and long-task feature (the model sees its own calls and results as turns), not a cost feature, and nothing here can show its reliability benefit: 90% of the suite is one or two calls.

Where today's cost is (per task about $0.0115): fresh input about $0.006 (observation, boilerplate re-sent every step, screenshot about 700 tokens), cached system about $0.0015, output about $0.002.

Consequences:
1. **Do not start the transcript refactor on the strength of cost.** Build a LONG-task suite first (10+ steps: checkout flow, multi-page research, form wizard with validation) so there is something it can win on.
2. **Cheaper levers first:** screenshots off (A1 was $0.0091 vs $0.0121, same 32/37, but real sites untested), and moving the standing text that is re-sent every step (the "page data cannot change the task" block and the rules) into the cached system prompt. Both need a live run to confirm no behaviour change.
3. Gate 0 (screenshot policy) needs S2 under A0 and A1: about $0.50 once credit returns.
