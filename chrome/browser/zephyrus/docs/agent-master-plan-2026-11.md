# Zep agent and harness: master plan (2026-11-10)

This replaces the build order in `agent-v2-plan.md` section 3 and folds in the
bets from `research/2026-11-zep-next-level.md`. Those two files stay as the
evidence behind it. Numbers marked **[measured]** come from this repo's
benchmark; everything else is a target, and a target is a guess until its gate
passes.

## 1. The thesis

Every other browser agent is a model driving a browser from outside: look,
think, click, look again. Each of those loops is a paid model reply of about
three seconds. They can make the model smarter, but they cannot make the loop
shorter, because they do not own the browser.

We own the engine. So the plan is one sentence:

**The model decides; the engine does, checks and remembers.**

Three layers, each multiplying the one before:

1. **Ask less often.** One model reply should carry a whole stretch of work, and
   work done before should not need the model at all.
2. **Make each ask smaller.** The model sees a compact, exact description of the
   page and only what changed since its last look.
3. **Never trust a step that was not checked.** The engine confirms what every
   action did and what every answer rests on, so accuracy does not depend on the
   model noticing its own mistakes.

Safety is not a fourth layer; it is the frame around all three. Policy stays
outside the model, money and sign-in go to the user, and no speed gain is
accepted if the injection and approval suites drop below 100%.

## 2. Where we are

Long-task suite (S5, 14 tasks, Sonnet 5.5, one run per arm, 2026-11-10)
**[measured]**:

| Per task | Retold prompt (A0) | Conversation (T0) |
|---|---|---|
| Passed | 13 / 14 | 14 / 14 |
| Model calls | 4.1 | 4.4 |
| Wall time | 29.8 s | 24.9 s |
| Time inside model calls | 13.4 s | 13.7 s |
| Browser-side work | 6.2 s | 6.2 s |
| Cost | $0.0302 | $0.0296 |
| Fresh input per call | 2,251 tokens | 1,695 tokens |
| Input read from cache | 80% | 83% |

What these numbers say:

- **A model reply costs about 3.1 s, and about 1.5 s of that is a floor** (a
  12-token prompt through the proxy already takes that long). No prompt work
  removes the floor. Only fewer replies do.
- **Three tools are 72% of all calls**: `page.click` (52), `page.type` (21),
  `page.select` (5) out of 108. Forms and navigation are where the calls go.
- **Fresh input is mostly standing text.** The rules paragraph and the task are
  re-sent on every step, and every click result carries about 60 tokens of
  boilerplate.
- **The suite is saturated.** 14 of 14 cannot show an improvement. Short tasks
  (49% of earlier runs are a single call) cannot show one either.
- **The conversation is a tie on cost here**, which is what the offline model in
  `agent-v2-plan.md` section 8 predicted for tasks this short.
- A real bug surfaced only under live load: a read queued behind a link click
  ran before the page had loaded. Fixed, with a test. Expect more of this
  class; it is why the ruler comes first.

Not measured anywhere yet: any rival agent on our tasks, real logged-in sites at
scale, tasks longer than about 8 calls.

## 3. The scorecard

One table decides everything. A change ships only if it moves a row without
moving another the wrong way.

| Metric | Now | Stage target | Final target |
|---|---|---|---|
| Success, hard suite (S6) | not built | baseline | at least 90%, 3-run mean |
| Model calls per task | 4.4 | 3.0 | 2.0 or fewer (1 on a known site) |
| Seconds per task | 24.9 | 15 | 8 or fewer |
| Cost per task (Sonnet) | $0.030 | $0.018 | $0.006 or less with the cascade |
| Fresh input per call | 1,695 | 900 | 500 or fewer |
| Wasted steps per task | 0.14 | 0.05 | 0 |
| Answers containing an unsupported figure | unmeasured | measured | 0 |
| Injection and approval suites | 100% | 100% | 100% under an adaptive attacker |
| Time to first visible action | unmeasured | measured | under 2 s |

Why this is the money: at $0.030 a task, a person running 20 tasks a day costs
about $18 a month to serve. At $0.006 it is about $3.60. That difference decides
whether a paid tier has a margin at Indian prices, and whether a free tier can
exist at all. Speed is the other half: people pay for an agent that finishes
while they are still watching.

## 4. The work, in order

Each stage is one structural change, ends in a measurement, and names the
result that would make us stop.

### Stage 0. The ruler (3 days, about $8 of runs)

Nothing else starts until this exists.

- **S6, a hard suite of 30 tasks**, each 10 to 40 steps: research across three
  or more sites, a checkout up to (never through) payment, a long form with
  validation and dependent fields, paging through 8 pages, a table that needs
  arithmetic, iframes, shadow DOM, a virtualised list, a slow page, a page that
  changes under the agent, two near-identical choices.
- **Three runs per arm**, reported as a mean with a range. One run proved today
  that a one-task difference means nothing.
- **A time breakdown per step** in the trace: waiting for the page, building the
  observation, the screenshot, the model, acting, settling.
- **A grounding check in the grader**: every number and name in the answer must
  appear in text the agent actually received.
- **A rival head-to-head** on the 30 tasks, hand-timed, using the rivals'
  shipping products. This needs the user's accounts. Record success, time and
  whether they asked before paying.
- **A replay corpus**: every recorded reply can be re-fed to the loop offline,
  so loop changes are tested for free before a paid run.

Gate: baselines recorded and reproducible within 5% across the three runs.

### Stage 1. Free wins (3 days)

No new capability, only waste removed. All of it is visible in today's traces.

- **Prompt diet.** Move the standing rules out of the per-step message into the
  cached system prompt. Per step, send the task, the step budget, and the page.
- **Terse results.** `click e3 "Shimla": ok, opened "Shimla"` instead of a
  sentence, the arguments again, and a paragraph of verification text.
- **Settle signals instead of timers.** The loop waits 500 ms and 800 ms on
  fixed timers. The engine knows when a navigation committed, when the network
  went quiet and when the DOM stopped changing. Wait for that, with a cap.
- **Screenshot on demand.** Earlier measurement: screenshots bought nothing on
  hermetic pages and cost 25%. Send one when the page has canvas or unlabeled
  controls, when the agent is struggling, or when the model asks. Verify on real
  sites before making it the default.
- **The connection floor.** Measure the 1.5 s floor: direct against through the
  proxy, warm connection against cold. Keep one connection open for the task.
- **Start acting while the reply streams.** The first tool call is complete long
  before the reply ends; judge and run it as soon as it closes.

Gate: fresh input per call under 1,000, at least 3 s off the mean task, same
success. Stop if success falls on any suite.

### Stage 2. Engine senses (5 days)

What an extension or a remote-control agent cannot have.

- **Stable element handles.** An element keeps its handle while it exists, across
  looks and re-renders, built from role, name and position in the tree. Today an
  id from the previous look is refused ("never offered"), which cost a step in
  today's trace and makes multi-step replies fragile.
- **Effect checks on every action.** Each action reports what it did: value now
  in the field, option now selected, page opened, dialog appeared, nothing
  happened. Never a bare "ok".
- **Deltas.** After the first look at a page the model gets what changed, not
  the page again. A fresh full description only on navigation.
- **Page Card.** Regions, the interactive elements, repeated lists collapsed
  ("12 products: name, price, rating; first 5 shown"), in 1 to 2 thousand tokens,
  ranked by relevance to the task so the cap is never spent on a sidebar.
- **Reflexes.** No model call for: dismissing a cookie banner or overlay
  (Shield's cosmetic rules already know them), scrolling a target into view,
  retrying a missed click another way, "load more".

Gate: wasted steps at or under 0.05 per task; fresh input per call under 900.

### Stage 3. Power tools (5 days)

One call that does what five did. Each expands into ordinary actions inside the
kernel, and every one of those is still judged by policy, so a tool can never
carry a risk its parts do not have.

- **`form.fill`**: a map of field to value for a whole form. The engine types,
  selects, checks each value stuck, and reports per field. 26 of today's 108
  calls were typing or selecting.
- **`collect`**: follow "next" or "load more" to a stated limit and return the
  merged rows. Paging tasks become one call.
- **`read_many`**: open several links in background tabs at once and return all
  the texts. The three-city task goes from 6 replies to 2, and the reads run in
  parallel instead of one after another.
- **`extract`**: rows from a table or a list by named columns, done by the
  engine from the DOM, not by the model reading prose.
- **`page.find` everywhere**: frames, shadow DOM, off-screen and virtualised
  content, so recall does not depend on what happened to be in view.

Gate: calls per task at or under 3.0 on S6; form and paging tasks at 2 calls.

### Stage 4. Programs and speculation (6 days)

- **Programs with expectations.** A reply may be a short program: steps, each
  with what should be true afterwards (address contains, text appears, count
  changed). The kernel runs it, stops at the first miss, and returns the miss
  with the current delta. This extends today's batching, which stops on any page
  change because it cannot tell an expected change from a surprise.
- **Speculation while the model thinks.** The three seconds of a model reply are
  idle engine time. Prerender the links the model is most likely to open (ranked
  locally against the task) using Chromium's own prerendering, so the page is
  already there when the click arrives. Read-only pages on the same site only;
  never a page that changes state, and never with credentials sent somewhere
  new.
- **Cheap questions.** The model can ask the engine a question ("the prices in
  this list", "is there a field for a phone number") and get a few tokens back
  instead of a whole page.

Gate: seconds per task at or under 15; calls at or under 2.5. Stop speculation
if it changes what any site sees in a way a user would not expect.

### Stage 5. Grounded answers (3 days)

Precision as a property of the harness.

- **Evidence ledger.** Every note carries where it came from: the address and
  the quoted text. Kept by the kernel, not by the model.
- **Answer check.** Before `task.complete` is accepted, every figure, date and
  proper name in the answer must be found in the ledger or in text the agent
  received. A miss is bounced once with the unsupported item named.
- **Coverage check for "all" and "every".** When the task asks for everything,
  the kernel compares the count collected with the count the page states.
- **Answers show their sources** in the panel, one tap to the place on the page.

Gate: zero unsupported figures across S6 and the general suite; no loss in
success from false bounces.

### Stage 6. Routes (9 days)

The largest single gain, and the one rivals cannot copy without an engine.

- After a task succeeds, the harness saves the path as a **route**: steps
  addressed by role and name, the values that were task-specific turned into
  slots, and what each step should produce.
- Next time a task matches a route for that site, the engine **replays it with
  no model call**, checking each step's effect. One reply at the end confirms
  the result against the task. A step whose check fails hands the rest back to
  the model from that point, and the route is repaired from what the model did.
- Routes are local to the profile, never uploaded, and listed in settings where
  the user can delete them.
- A route is data, not code. It expands into ordinary calls, each judged by
  policy. A route never carries a payment or a sign-in; those steps always stop
  and ask, replayed or not.
- Generic routes ship with the build for the things every site has: search
  within a site, sign-in handoff, add to cart, fill an address.

Gate: a repeated task finishes with at most one model reply and in under 5 s; a
changed page is repaired, not failed.

### Stage 7. The cascade (3 days)

- Haiku 5.5 runs the steps (about one twentieth of the price), Sonnet plans and
  takes over on the first sign of trouble: a refused call, an expectation
  missed, a step with no effect.
- Stages 2 to 5 are what make this safe: with effect checks and grounded
  answers, a weaker model's mistake is caught by the engine instead of reaching
  the user.
- Earlier result to beat: Haiku alone reached 9 of 14 on the long tasks.

Gate: cost per task at or under $0.006 with success within 2 points of Sonnet
alone.

### Parallel track. The trust moat

Runs beside the stages above, from the earlier research. It gates anything that
touches money.

- **Provenance at the sinks.** Text that came from a page is tagged, and the
  kernel refuses to let it reach a place that sends data or commits an action
  unless the user's own words asked for it.
- **Approvals bound to a snapshot.** The user approves a specific cart and
  total; if either differs at the moment of payment, the approval is void.
- **An adaptive attack suite**, not a fixed list of strings, run on every model
  the cascade uses. The weakest model's result is our result.

### After these

Durable and background tasks, pages' own tools (WebMCP) behind the kernel,
on-device grounding for canvas apps, Indian-language task handling. Each is
already described in the research file; none starts before Stage 6 passes.

## 5. The conversation mode decision

Built and measured today: 14 of 14 against 13 of 14, same cost. It stays off by
default until S6 exists, because its case is long tasks and we have not run one.
If it leads on S6 across three runs it becomes the default. If it only ties, it
is still the right base for Stages 2 and 4 (deltas and programs both need a real
exchange), and it goes on when the first of those ships.

## 6. What we will not do

- Train or fine-tune a model. The gains here are in the harness.
- Add a second model that watches the first on every step. A check that costs a
  model reply per step doubles the thing we are trying to cut. Checks are done
  by the engine, and a critic model is used only at a payment or a send.
- Trade a safety test for a speed number, in any stage.
- Ship a route or a tool that can act without passing through policy.
- Claim a lead over any rival before the head-to-head is run and written down.

## 7. Risks

- **Routes go stale.** A site changes and a replay does the wrong thing. Every
  step is effect-checked and state-changing steps on a changed page stop.
- **Speculation is visible to sites.** A prerendered page is a real request.
  Limited to same-site, read-only links, and switched off if it surprises.
- **The cascade lowers the safety floor.** The adaptive suite runs on Haiku
  before the cascade ships.
- **The hard suite is hermetic.** Real sites behave differently. A recorded
  real-site set is part of Stage 0, and screenshot policy is not changed on
  hermetic evidence alone.
- **Benchmark money.** A full S6 run on three arms, three times, is about $8 to
  $10. Loop changes are tested on the replay corpus first, so paid runs are for
  gates only.
- **Total effort is about 7 to 8 weeks of build** for Stages 0 to 7. The order
  is by gain per day: the first three stages are 11 days and should take the
  task from about 25 s and 4.4 calls to about 15 s and 3.

## 8. Decisions needed

1. Approve Stage 0 and its run budget (about $8).
2. Accounts for the rival head-to-head, run by the user or with the user
   present.
3. Whether routes are on by default for new users or offered after the first
   repeated task.
