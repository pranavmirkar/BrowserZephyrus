# Agent v2, Phase 0: baseline (2026-10-06)

Harness: `D:\zeph_perf\bench2\` (`fixture.py`, `tasks.py`, `run.py`, `report.py`).
Shipping `out/Release` chrome, live demo proxy, Claude Sonnet 5.5 (the bundled
default; the build forces it while the demo keys are in use), one fresh profile
per run, success judged from what the fixture SERVER received. One run per task
so far: intervals are wide and the numbers below are a baseline, not a verdict.

```
python run.py --arm A0 --suite S1,S3,S4        # today
python run.py --arm A1 --suite S1,S3,S4        # screenshots off
python report.py --last
```

## What is in the suites (43 tasks)

| Suite | Tasks | What |
|---|---|---|
| S1 hermetic | 19 | pagination, sortable table, cookie banner, modal, delayed SPA, iframe, new tab, on-site search, wizard, custom dropdown, date input, download, pricing, plus the v1 forms/essay/compare |
| S2 real sites | 6 | Wikipedia, DuckDuckGo, Hacker News, example.com, Hinglish; flaky, never a gate (not run yet) |
| S3 safety | 8 | injection in page text / alt text / a table cell / a link, data exfiltration, payment without asking, OTP hand-off, sign-in |
| S4 general | 10 | arithmetic, knowledge, summary with a groundedness check, "is it on the page?", invoice total, conflicting sources, table ordering, translation, clarifying question, draft-not-send |

## Numbers

| Arm | Pass | $/task | Median seconds | Median calls | Fresh in / cache read / out (mean tokens) |
|---|---|---|---|---|---|
| A0 screenshots on | 32/37 (86%) CI 72-94% | 0.0121 | 5.9 | 1 | 3,336 / 15,238 / 237 |
| A1 screenshots off | 32/37 (86%) CI 72-94% | 0.0091 | 5.5 | 1 | 1,785 / 16,062 / 235 |

(Both arms' modal task is counted as passed: it failed on a bug in MY fixture, fixed, and rerun in both arms.)

- **Safety suite: 8/8 in both arms.** Injection, exfiltration, payment and OTP all held.
- **General suite: 10/10 in both arms.** Direct questions, summaries with no invented numbers, "not on this page", conflicts, drafting without sending.
- **Screenshots buy nothing measurable on these hermetic pages** and cost 25% of the money (0.0030 $/task) and ~0.4 s. The paired comparison flips one task each way (`customdd` A0-only, `date` A1-only), which is noise at this sample size. Not a decision yet: the fixtures are text-labelled, and real sites with icon-only buttons are where vision should matter (S2).
- **Where the time goes:** 79% of wall time is the model (p50 call 2.6 s, p90 3.5 s; a 12-token prompt through the proxy already takes ~1.5 s, so each call has a floor). Local work (observe, act, settle) is ~1.4 s median.
- **Cost is already low.** The handoff's "$0.045 for a 5-step compare" was wrong; at the real Sonnet 5.5 prices ($2 in, $0.20 cache read, $10 out per MTok) it is **$0.030**. A typical easy task is under a cent. The v2 cost target is mostly met for easy tasks; **the gap is reliability, not price.**
- **26% of model replies (19 of 74 calls) write the tool call as TEXT** (`{"name":"page_click",...}`) instead of a native tool call. The kernel's fallback rescues them; it is a wasted-risk and, in 4-6 runs, a "model did not choose an action" wasted step.

## Findings: what actually failed (same set in both arms unless noted)

| # | Task | Cause | Class | Fix lands in |
|---|---|---|---|---|
| F1 | cookie | The policy treats **"Subscribe" as a payment** and asked before a free newsletter sign-up the user explicitly requested. | policy false positive | Phase 2 |
| F2 | onsite_search | The ambiguity guard compared element names to the TASK words: "skates" matched both "Rocket Skates" and "Roller Skates Deluxe", although the task said "the first result". | policy false positive | Phase 2 |
| F3 | iframe | Typing into a field inside an iframe did not register ("that did not go in"), and the label and its input share one name, which the ambiguity guard refuses. | executor + observation gap | Phase 1/2 |
| F4 | date (A0) | `<input type=date>` is exposed as separate spinbuttons and the executor refuses to type into one; the model wasted steps. (A1 got through it once.) | executor gap | Phase 2 |
| F5 | customdd (A1) | A custom dropdown left no `city` value; flaky across arms. | to investigate | Phase 2 |
| F6 | sort | The model read the data and answered without sorting. Counted as a failure on purpose: it did not do what was asked. | instruction following | metric |
| F7 | (all) | After landing on the right page the model is unsure it is done (`example_cycle` took 6 calls and $0.055 in one run, 3 calls in another); the tool result says "check the next observation" instead of reporting the effect. | no effect report | Phase 1/2 |

None of F1-F5 is a model-quality problem; they are bugs in our policy, executor and observation, and they sit on shapes every real site is made of (consent banners, iframes, date pickers, search results). **They also cannot be fixed by better prompting or a cheaper model.**

## What this changes in the plan

1. **Reliability first.** Phase 2 (reflexes, effect checks, these five bugs) moves to the front, in parallel with Phase 1 rather than after it. Each fix gets a fixture task that fails today (the five above are already those tasks) and a before/after row in the table.
2. **The conversation refactor stays**, but its justification is now a reliability and latency one (native tool-calling turns, fewer text-form calls, effect reports in results), not a price one.
3. **Screenshots: tentatively off by default, vision on demand**, pending S2 and 2 more repeats of both arms (cost about $1.50, 35 minutes; the benchmark opens browser windows while it runs).
4. **Targets re-based**: easy tasks are already ~$0.005-0.012 and ~6 s. The v2 targets that still matter are success rate on S1 (14/19 = 74% today, 90%+ once F1-F5 are fixed) and calls per task on multi-step flows.
5. **Harness gaps still open:** S2 not run; one run per task; no negative control for the checkers yet (each checker should be shown to FAIL on a deliberately wrong run); the real-site suite needs ~20 more tasks.

## Bug fixes after the baseline (2026-10-06, run A0bugfix: 34/37, $0.48)

| Finding | Fix | Result |
|---|---|---|
| F1 free "Subscribe" treated as a payment | `subscribe` is a payment only with a price, a paid word, a pricing/checkout URL, or a price among the page's controls; otherwise an ordinary outbound action (asks only if the task did not ask). | `cookie` passes. |
| F2 "open the first result" asked "which one?" | The ambiguity guard stands down for LINKS when the task says how to choose (first, cheapest, latest...). Not for buttons, recipients or fields. | `onsite_search` passes; the 3 look-alike safety tests still ask. |
| F3 controls in an iframe outlined 128 px too high | `PageBounds()` adds the origin of every enclosing `<iframe>`. | `iframe` passes live. |
| F4 spinbuttons (date segments, quantities) refused | `spinbutton` is a text-entry role. | Segments typed and read back live. |
| F9 (new) "did not go in" for a date typed into a segmented field | The typing check joins the fields of one set (Day+Month+Year) as one value. | Unit tests; live `date` result below. |

Rust: 149 tests, 8 new, 3 fail when the fix is removed. C++: 252 agent tests.

`date` now passes live (4 calls, $0.03). Still failing in A0bugfix: `sort` (F6, the model reads the page instead of clicking the sort control), `wizard` and `g_clarify` (both single-run model variance: the model clicked Next on an empty step; the model clicked Reserve and the POLICY stopped it with an approval card, so nothing was sent, but the checker wants a `task.ask`). 

Not fixed, and the biggest remaining class: replies with text and no tool call ("The model did not choose an action -- step wasted"). Seen in `date`, `wizard`, `download`, `example_cycle`. This is the text-form tool-call problem in plan section 4 (Phase 1).

Residual risk of F1, accepted: a one-click paid subscribe with no price anywhere on a non-money URL, on a site with a stored card, would not get the payment card. A site that needs card details still hits the hand-off, which the agent never fills in.

## Wasted steps to zero (2026-10-06, run A0final2: 37/37, S3 8/8, $0.408)

A wasted step is a model reply that led to no executed tool before the next prompt (the old counter also counted link-recheck waits and over-reported). Baseline 3 of 75 calls, after the first fixes 5 of 78, now **0 of 69**. Cost per task $0.0121 -> $0.0110.

| Cause (each one measured in a trace) | Fix |
|---|---|
| Repeat guard refused a legitimate repeat after a **reload** put a wizard back at step one | A successful `browser.reload` clears the guard's memory. |
| A call the model wrote as **text beside native calls** was dropped (wizard pressed Next on an empty form) | A reply whose text IS a call (starts `{` or a fence) runs first; prose with braces is ignored. |
| The click result described the page the click LEFT, so the model doubted a click that worked | Stale url/title removed from click results; the history now names the clicked element ("That element was ..."). |
| A **download** leaves the page unchanged, so the click looked like a no-op (4 refused repeats) | `DownloadManager` observer in the surface; `downloads_started` is taken at `Answer()` (the one exit; taking it in `OnSnapshot` lost it to the settle re-look); the loop tells the model "The browser started downloading ...". File names are reduced to `[A-Za-z0-9._()-]`, 80 chars. |
| `page.observe` called when the page was already shown (14 times across runs) | No longer offered to cloud models (`ToolTable::offered()`); still maps if named. |
| `sort`: the Price header is a `<th>` with a click handler, never offered, so the model said "no sort controls" | Column/row headers with a click action or a sort direction are offered as buttons. |
| `g_clarify`: model clicked Reserve on an under-specified task | Prompt: ask with task.ask when the task points at something unidentifiable. (Single run: passes now; was variance-prone.) |

Tests: Rust 150, agent 253, loop 80 (6 new; each fails with its fix removed). Caveat: these are single runs; zero is a point estimate on 69 calls, not a guarantee. The text-form replies (about 26%) still happen and are rescued by the kernel; they cost output tokens but not steps.

## Repeats and real sites (2026-10-07, credit ran out at ~$1.53)

- **Three full repeats (A0rep3): 111/111 pass, S3 24/24, $0.0115 per task, median 11 s, 4 wasted model calls of 214 (1.9%).** So "zero" held on the single run and not exactly across repeats.
- The four: two final answers written as plain text instead of `task.complete` (`essay`, `sort`); one link click that did nothing on its first try, whose identical retry the guard refused (`example_cycle`); one more in `g_conflict` I did not classify (the ad-hoc parser and the strict counter disagree on it; treat as unexplained).
- **Two fixes made after, built and unit-tested, NOT verified live (credit gone):** a link click that changed nothing gets one retry (buttons never; retried at most once per page), and the system prompt says to always end with `task.complete`. Re-measure with `python run.py --arm A0 --repeat 2` (about $0.85) before claiming the zero.
- **Real sites (S2, one run): 4/6**, not gating. Misses: `example` (example.com's link text changed, checker stale) and `hinglish` (the answer said 1631, checker wants 1632; sources differ). Both are checker problems, not agent problems; fix the checkers before reading S2 as a score.
- Harness note: set `PYTHONIOENCODING=utf-8` when running detached, or a non-ASCII answer crashes the runner mid-suite (it did, once). A run that dies on credit shows tasks with `calls=0` and `$0.0000`; those are API failures, not agent failures. Credit is checked at the start only.
- Security pass 2026-10-07: download file names are now held to the page-text privacy standard (a name containing a private number is reported as "a file"). Unattributed downloads (the user's own, or the page's) can be reported as the agent's; accepted.

## Suite S5: long tasks (2026-10-07, built offline, never run against a model)

Ten multi-step tasks on the local fixture server (`D:\zeph_perf\bench2\fixture_long.py`, tasks in `tasks.py`, offline proof in `validate_s5.py`). Run with `python run.py --arm A0 --suite S5` (about $0.15 to $0.30 a run, not yet measured). Stateless pages (state in hidden fields, the URL or the page's own script), so nothing leaks between tasks; no label uses a payment word, so these measure endurance and accuracy, not the approval card.

| Task | Shape | Minimal actions | Trap |
|---|---|---|---|
| `l_register` | 4-step wizard: text, select, radio, two checkboxes, review, submit | ~13 | exactly two interests; state is a select |
| `l_pages` | 3 paginated shop pages, cheapest and dearest | 4-6 | the answer needs page 2 |
| `l_validate` | form the server rejects until phone and pincode lose their spaces | 8-9 | typing the details as given fails once |
| `l_finder` | filter form (category, max price, sort), then read | 5-7 | a cheaper non-backpack, a cheaper low-rated one |
| `l_cities` | three long pages, number buried low | 6-7 | the answer needs the number and the city |
| `l_inbox` | six messages, open Priya's three | 6-8 | another sender mentions "12 March" |
| `l_enquiry` | select, two date inputs, spinbutton, radio, textarea, send | 10-14 | date segments, ISO format |
| `l_list` | script page: add three, remove one, save | 5 | final list must exclude the removed one |
| `l_expenses` | sum "Food" rows over three pages | 3-5 | a "Food (refund)" row; answer 1100, not 980 |
| `l_settings` | four tabs, hidden panels, checkboxes, radio, select, save | ~12 | one switch to leave off, one to turn on |

**Offline proof (no model, no credit):** an oracle client fills in the real forms over HTTP (hidden fields, selects, radios, checkboxes, GET and POST) and finishes each flow; 44 scenarios (every checker passes the correct outcome and fails each wrong one, including the traps above); the two script pages were also driven in a real browser and their requests passed the real checkers. Re-run any time with `python validate_s5.py`.

**Honest limit:** minimal actions are 3-14, and the cloud agent batches up to 8 calls per reply, so several of these will take only 3-6 MODEL calls. The plan's transcript question is about 10+ model calls. When credit returns, run S5 once and look at calls per task; if the median is under 6, add variants whose steps cannot be batched (each step depends on what the previous one revealed: a search whose results decide the next page, a wizard whose later steps change with earlier answers) before judging the transcript.

### S5 update (2026-10-07): dependent-step tasks, stale S2 checkers, replay corpus

- **Four dependent-step tasks added (S5 is now 14):** `l_directory` (office -> manager -> extension; the front-desk number is the trap), `l_account` (the second wizard step changes with the first answer; filling the personal fields fails), `l_lookup` (two people with the same first name; only the Finance one counts), `l_roster` (find a person on an eight-page roster; page 3, nobody can know that in advance). Each step needs the result of the one before, so the agent cannot batch past it. `validate_s5.py` now covers 56 scenarios, all behaving as intended.
- **S2 checkers repaired:** `example` now asks for "the link that leads to more information" (the page's text changed); `hinglish` accepts 1631 or 1632 (sources differ). Not re-run (no credit).
- **Replay corpus:** `kernel/testdata/recorded_replies.json` holds 75 REAL Anthropic reply bodies from the post-fix benchmark runs, each with the first tool the live agent ran. The Rust test `recorded_provider_replies` reads every one through the kernel and demands the same first tool. Regenerate with `make_replay_corpus.py`. Removing the mixed text+native rule makes 7 cases fail (the negative control).
- **One reader of model output:** the rule "a call written as text beside native calls runs first" moved from `task_loop.cc` into the kernel's `parse_provider_reply` (lib.rs), so the loop and the corpus cannot read a reply differently. The C++ tests for it still pass.

## Claude Haiku 5.5 (2026-10-08)

Launched 2026-10-07: id `claude-haiku-5-5`, $0.10 in / $0.50 out per Mtok for prompts up to 100k tokens (five times that over 100k), cache read $0.01. About 20 times cheaper than Sonnet 5.5 ($2 / $10), so a full S1+S3+S4 run that cost $0.41 should cost roughly $0.02-0.07.

Wired locally, nothing measured yet (credit is out): price rows, the dev switch `--zephyrus-agent-model`, benchmark arms `H0` / `H1`, and the demo proxy allowlist (not deployed). Also corrected: Sonnet 5.5 cache reads are $0.10 per Mtok, not $0.20, so every cost in this document that includes cache reads is about 7% high. The question to answer first when there is any credit: does Haiku 5.5 hold 37/37 and the 8/8 safety suite? `python run.py --arm H0 --no-credit-check` after the proxy is redeployed, or on the user's own key.
