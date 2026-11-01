# Zep: how to go past what other agentic browsers ship

Research note, 1 November 2026. Not a decision record. Nothing here is built.
Written to sit beside `docs/agentic-harness-v2.md`, whose seven pillars (P1 to
P7) it refers to by number.

Evidence labels: **[repo]** read in this repository; **[sourced]** from the
linked source, often a search snippet; **[unverified]** could not be confirmed.

Limits: the agentic-browser market moves monthly and most comparison pages I
found were aggregator blogs with conflicting dates. I rely on primary sources
(Google's Chrome security post, Anthropic's and Brave's own posts, arXiv) for
anything that carries weight, and say so when I could not. "No other browser
has this" means I found no evidence of it.

## Short answer

The market is racing on capability. The open ground is **trust you can
measure**. Three things, in order:

1. **Enforce where data came from, not only which tool was called.** The
   evidence from 2025 and 2026 says defences that depend on the model noticing
   an attack fall to adaptive attackers, and the designs that hold are
   architectural. Zep already has the right base (policy outside the model).
   Provenance at the sinks is the next layer, and it is the one claim that can
   be backed with a published adaptive-attack result.
2. **Bind approvals to what was actually approved, and be ready for payment
   rails that sign intent.** This is the India-first angle: UPI is being opened
   to agents, and Zep's "asks only about money and sign-in" rule is a strong
   fit if the ask carries a verified cart.
3. **Learn a site once, replay it without the model.** Latency is the
   biggest user-visible cost (2 to 7 seconds per model reply). Replaying
   verified steps removes most model calls on known sites.

## 1. What Zep has today

- Policy outside the model, in a sandboxed Rust process. Approval lifts an Ask
  and never a Deny. Money and sign-in are the only routine asks **[repo]**.
- 26 tools, a frozen contract, accessibility-tree plus labelled-screenshot
  observation, acting through the real input pipeline **[repo]**.
- Measured: Claude Opus 5.5 12 of 12 on the 12-task suite at 14 seconds per task
  on average, Sonnet 5.5 10 of 12; graded by what the page received, not by a
  judge model **[repo: README]**.
- Voice: hold-to-talk, on-device "Hey Zep" matcher, Voice Lock (stated as not
  biometric-grade) **[repo]**.
- Not built yet from the v2 plan: the provenance monitor (P4), parallel and
  durable tasks (P5), skills (P6), public benchmarks (P7). Long-term recall is
  keyword overlap **[repo: HANDOFF.md]**.
- Things the audit pass in this repo found that matter here: a navigation to
  the page's own origin with data in the query counts as "expected"; a
  Devanagari task skips the wrong-recipient rule; approvals are not tied to a
  snapshot of what was approved **[repo, PR #1]**.

## 2. What the field does

- **Chrome (Gemini).** Google's December 2025 post describes layers around its
  agent: a separate "User Alignment Critic" model that sees only metadata about
  a proposed action, "Agent Origin Sets" that split origins into read-only and
  read-write sets for the agent, an injection check on every page it sees,
  confirmations before sensitive actions, and no direct access to stored
  passwords. A bounty up to $20,000 is offered **[sourced: Google, primary]**.
  The agentic feature launched 28 January 2026 and was reported as limited to
  paid Google AI tiers and the US **[sourced: aggregators]**.
- **Anthropic's Claude in Chrome.** Launch testing: 23.6% attack success without
  mitigations, 11.2% with, on 123 cases. A later post reports about 1% for a
  newer model; the setups are not directly comparable **[sourced: Anthropic]**.
  Anthropic's own advice is to avoid financial, legal and medical sites.
- **Perplexity Comet.** Brave disclosed indirect prompt injection in it,
  including payloads hidden in text and images, with a demonstration that
  reached email and a banking portal **[sourced: Brave]**.
- **ChatGPT Atlas.** One set of sources says OpenAI ended the browser on
  9 August 2026 and moved the features into the ChatGPT app and a Chrome
  extension. I could not find the primary statement **[unverified]**.
- **Dia, Edge Copilot Mode, Opera Neon.** Coverage was thin or conflicting.
  I would not build positioning on any claim about them.

## 3. What the evidence says about the hard problem

- **Detection and prompting do not survive an attacker who adapts.** A 2025
  study by authors from OpenAI, Anthropic and Google DeepMind, among others,
  took 12 published defences that reported near-zero attack success and broke
  most of them with adaptive attacks; success rates ranged from 71% to 100%
  **[sourced: arXiv 2510.09023, USENIX Security 2026]**. Conclusion for Zep: do
  not rest a safety claim on a classifier or a prompt, and do not evaluate with
  a fixed list of attack strings.
- **Architecture holds, at a utility cost.** CaMeL (Google DeepMind) separates
  a planner that never sees untrusted data from a model that does, and tracks
  data provenance in an interpreter. On AgentDojo it solved 77% of tasks with
  provable security against 84% undefended (an earlier version of the paper
  reported 67%) **[sourced: arXiv 2503.18813]**. Zephyrus's own harness doc
  already argues for constraining the data flow to sinks and keeping the full
  page view, to avoid that utility loss.
- **A simple rule to design against.** Meta's "Agents Rule of Two": in one
  session an agent should have no more than two of (a) untrusted input,
  (b) access to sensitive data, (c) ability to change state or communicate
  outward **[sourced: secondary summaries; Meta's post not retrieved]**. A
  browsing agent with a logged-in session has all three the moment it reads a
  web page and can click. Zep's approvals are how the third leg is held back.
  Making that explicit, per task, is cheap.

## 4. Bets, ranked

### Bet 1. Provenance at the sinks, with a published adaptive evaluation

**What.** Label every span the model sees as user, browser or page(origin).
At each sink (navigation URL, typed text, form submit, download,
`memory.remember`) the kernel checks that the value traces to the user or the
browser, or to the same origin it is going to. Add per-task read and write
origin sets, the same idea as Chrome's Agent Origin Sets, but computed from the
Workspace the task is bound to (ADR 0003). Count Rule-of-Two legs per task and
require approval when a task would hold all three.

**Why now.** It is P4 in the harness doc; the evidence in section 3 says move it
ahead of skills. It also closes a gap found in the audit pass: a navigation
to the current page's own origin with data in the query is allowed today
because the origin matches. Provenance can allow ordinary on-site filtering
(the data is the user's) and refuse a page-supplied string.

**Proof.** Run an adaptive attacker, not a string list, against it, publish the
method and numbers, and include the weakest model you support. The harness doc
already states this as target T2.

**Limits.** Matching is not proof; a model can paraphrase page text past a
substring check (the doc says so too). Keep a strict mode for high-risk tasks.
**Effort.** Large. **Risk.** Utility loss from false refusals; measure it.

### Bet 2. Approvals that bind to what was approved; payment-ready by design

**What.** When Zep asks about a payment, record a snapshot: merchant origin,
amount and currency as read from the page, the cart contents, the click target.
Re-check it immediately before executing, and void the approval if any of it
changed (a price bump between ask and click is the realistic attack). Show the
snapshot in the card.

**Why it fits India-first.** NPCI is reported to be preparing a "Unified Agent
Protocol" that would let registered agents make small UPI payments, with a
figure of 15,000 rupees and an NPCI-run agent registry mentioned. A 2025 pilot
with OpenAI and Razorpay used UPI Circle and Reserve Pay with consent kept in
the user's own UPI app. Pine Labs announced a protocol that skips the PIN step
**[sourced: Inc42, Outlook Business, Paypers; launch status unconfirmed, sources
disagree on event dates]**. Elsewhere, Google's AP2 uses signed intent, cart and
payment "mandates" **[sourced: secondary; check the spec for who signs what]**.

**Design.** Introduce a `PaymentIntent` object in the kernel now (origin, amount,
currency, cart hash, expiry, approval record). Today it backs the card; later it
can back a mandate or a UPI delegation without changing the policy. Keep the
rule that Zep never types card details.

**Effort.** Medium. **Risk.** Page scraping of amounts is fragile; fall back to
asking when the amount cannot be read.

### Bet 3. Skills: learn a site once, replay it without the model

**What.** P6. Save a completed task as a verified step list with slots, replay
it deterministically, and call the model only when a step's expectation fails.

**Evidence.** Browserbase's Autobrowse and Anchor describe the same pattern:
one expensive run, then cached steps with self-repair **[sourced: vendor blogs;
all performance figures are unverified]**. One guide gives 3 seconds against 20
seconds for a deterministic run against an agent run, from a single author.

**Why Zephyrus can do it better.** The engine sees navigation commits, DOM
mutations and request origins, so each replayed step can be *verified*, which a
screenshot or CDP agent has to guess. Anchor steps to what Observation already
provides (role, name, nearby labels), not CSS selectors, and keep every replayed
call inside kernel policy.

**Measure.** Seconds, tokens and model calls per success on repeated tasks, with
and without a skill. **Effort.** Large. **Risk.** Stale steps; replaying a
skill on a changed checkout page is a payment risk, so payments always
re-verify (Bet 2).

### Bet 4. Use pages' own tools, and treat their descriptions as hostile

**What.** WebMCP lets a page register tools for agents through
`navigator.modelContext`. It is an early W3C Community Group draft co-authored
by Google and Microsoft; Chrome announced an early preview on 10 February 2026
behind a flag in Chrome 146 Canary, and one source reports an origin trial in
149 **[sourced; the 149 claim unverified; API expected to change]**. HTTP pages
are excluded and workers cannot register tools.

**Fit.** The fork is on a newer base than 146, so the flag may already exist.
Add a bridge tool pair (`page.tools.list`, `page.tools.call`) behind the kernel.
Where a site offers a tool, the agent stops guessing at pixels.

**Security point.** A tool name and description is text written by the page.
Treat it as page content in the provenance model (Bet 1), never as an
instruction, and infer risk from the schema and the kernel's rules, not from
what the description claims. This is the same class as MCP tool poisoning.

**Effort.** Small to medium. **Risk.** Standard still moving; ship it as
experimental.

### Bet 5. Cut latency with a cheap grounding model and a verified cascade

Model latency (2 to 7 seconds per reply) dominates task time **[repo]**. Two
levers:

- **Cascade.** Try a faster, cheaper model first; keep the expectation checks
  from P3; escalate to the strong model only on failure. The suite shows the
  gap to close: 12 of 12 (Opus) against 10 of 12 (Sonnet).
- **On-device grounding for hard surfaces.** Small GUI-grounding models exist:
  GoClick is 230 million parameters (April 2026), and Ferret-UI Lite is 3
  billion with reported scores of 91.6% on ScreenSpot-V2 **[sourced: arXiv
  2604.23941, 2509.26539; I did not find real-hardware latency]**. These suit
  canvas apps and unlabeled controls, where the HANDOFF notes Zep flails. They
  run next to the existing local vision client and keep the screenshot on the
  machine.

**Effort.** Medium for the cascade, large for local grounding. **Risk.** Small
models disagree with the accessibility tree; let the tree win when it has a
label.

### Bet 6. A cleaner, safer page for the agent (the Shield link)

- Remove ads, banners and consent walls before observing; Observation already
  tracks covered elements for exactly this reason **[repo]**. Test with and
  without Shield and report the success difference.
- Show the model only text a person could see. AX-invisible subtrees are already
  dropped; off-screen, clipped and low-contrast text may not be **[repo, not
  tested]**, and hidden text was used against Comet.
- Label third-party iframe content as untrusted by origin.

**Effort.** Small to medium. **Why it is distinctive.** The same engine that
blocks the ad prepares the agent's view. I found no browser that claims or
measures this.

### Bet 7. Make Zep work the way Indian users talk

- **Script-awareness.** The audit pass showed a Devanagari task skips the
  wrong-recipient rule, and Hindi send and delete buttons run without asking
  (Hindi payment buttons were fixed). Finish this in the policy, with a test per
  script.
- **Speech.** Models lose 30 to 50% relative accuracy on Hindi-English
  code-switched speech compared with single-language speech **[sourced: HiACC
  corpus paper, 2025]**. One 2026 comparison (written by a competing model's
  author) put Sarvam Saaras-v3 at 11.25 word error rate on conversational
  Hinglish and ElevenLabs Scribe-v2 at 12.40, with the order reversed on read
  speech **[sourced, single source]**. I found no AssemblyAI figures. Build a
  small Hinglish *command* test set from real users ("Flipkart pe iPhone
  dhundo"), measure word error and task success per provider, and keep ASR
  swappable.
- **Output script.** The transcript may arrive in Devanagari or Latin script.
  Policy and observation matching must not care which.

**Effort.** Medium. **Risk.** Cross-script matching (Hindi task, English page)
needs transliteration; say so rather than pretend.

### Bet 8. Evaluation that holds up

- WebVoyager-style scores near 90% fell sharply under human evaluation on
  Online-Mind2Web, partly because model judges disagree with people
  **[sourced: arXiv 2504.01382]**. Zephyrus already grades by what the page
  received. Keep that and add live tasks on Indian sites.
- Some 2026 leaderboard entries I saw were implausible or unverifiable, so I
  quote none. Treat any outside number as unconfirmed until reproduced.
- Add the adaptive attacker from Bet 1 to every kernel change.

### Bet 9. Durable and background tasks, after Bets 1 and 2

Scheduled or unattended tasks remove the human who would catch a bad action.
By the Rule of Two they should not run until provenance and bound approvals are
in. Then P5 is mostly plumbing.

## 5. Suggested order

1. Bet 1 design and an adaptive-attack harness (the harness first, so the
   baseline exists before the defence).
2. Bet 2 (snapshot-bound approvals, `PaymentIntent`).
3. Bet 7 policy fixes (small, and they close real gaps).
4. Bet 6 A/B measurement (cheap, and it can headline the Shield story).
5. Bet 3 skills; Bet 4 WebMCP behind a flag.
6. Bet 5 cascade, then local grounding.
7. Bet 9.

## 6. Risks to state plainly

- Z-01: the browser base is out of support. An agent that logs into banks on an
  unpatched engine is the worst version of that risk.
- The agent's safety claim is only as good as the weakest model's adaptive
  result. Publish the method as well as the number.
- Provider and payment-rail facts above are moving. Re-check the NPCI, AP2 and
  WebMCP status before building against any of them.

## Sources

- [Google: Architecting security for agentic capabilities in Chrome](https://blog.google/security/architecting-security-for-agentic/)
- [BleepingComputer: Chrome adds a security layer for Gemini agentic browsing](https://bleepingcomputer.com/news/security/google-chrome-adds-new-security-layer-for-gemini-ai-agentic-browsing)
- [Anthropic: Claude for Chrome](https://www.anthropic.com/news/claude-for-chrome)
- [Anthropic: prompt injection defenses](https://www.anthropic.com/research/prompt-injection-defenses)
- [Brave: indirect prompt injection in Perplexity Comet](https://brave.com/blog/comet-prompt-injection/)
- [The Attacker Moves Second (arXiv 2510.09023)](https://arxiv.org/abs/2510.09023)
- [Defeating Prompt Injections by Design, CaMeL (arXiv 2503.18813)](https://arxiv.org/abs/2503.18813)
- [Simon Willison: Agents Rule of Two and The Attacker Moves Second](https://simonw.substack.com/p/new-prompt-injection-papers-agents)
- [An Illusion of Progress? Assessing the Current State of Web Agents (arXiv 2504.01382)](https://arxiv.org/pdf/2504.01382)
- [WebMCP early preview coverage (eWeek)](https://www.eweek.com/news/google-webmcp-chrome-ai-web-standard-preview/)
- [WebMCP explainer (Zuplo)](https://zuplo.com/blog/what-is-webmcp)
- [AP2 explainer (Analytics Vidhya)](https://www.analyticsvidhya.com/blog/2025/09/agent-payments-protocol-ap2/)
- [Inc42: NPCI to launch agentic payments on UPI](https://inc42.com/buzz/npci-to-launch-agentic-payments-on-upi-report/)
- [Outlook Business: Unified Agent Protocol](https://www.outlookbusiness.com/news/india-plans-ai-powered-upi-payments-framework-through-unified-agent-protocol)
- [The Paypers: Pine Labs P3P](https://thepaypers.com/payments/news/pine-labs-launches-p3p-agentic-payment-protocol-on-upi)
- [Browserbase: Autobrowse](https://browserbase.com/blog/autobrowse)
- [Anchor: browser agent amnesia and skill caching](https://anchorbrowser.io/blog/browser-agent-amnesia-skill-caching-anchor)
- [GoClick (arXiv 2604.23941)](https://arxiv.org/pdf/2604.23941)
- [Ferret-UI Lite (arXiv 2509.26539)](https://arxiv.org/abs/2509.26539)
- [HiACC: Hinglish adult and children code-switched corpus](https://www.ncbi.nlm.nih.gov/pmc/articles/PMC12329218/)
- [Tara Hindi transcription model card (comparison with Sarvam, ElevenLabs)](https://huggingface.co/Trelis/tara)
