# Shield: ad blocker and Shield DNS master plan (2026-11-10)

This orders the remaining work from `research/2026-11-beating-brave.md`,
`2026-11-head-to-head.md`, `2026-11-shield-next-level.md` and
`2026-11-shield-dns-bench.md`, and adds what those left out. Numbers marked
**[measured]** come from those files and their harnesses. Everything else is a
target until its gate passes. The standing rule applies to every row: a tie
with a rival is not done.

## 1. The thesis

Every rival blocker sits in one of two places. Extensions (uBlock Origin Lite,
AdGuard) are held under Manifest V3: a fixed rule budget, no response
rewriting, no code before the page's own. Built-in blockers (Brave, Edge,
Opera) match requests against lists and stop there. Safe Browsing and its
copies match addresses against lists that are hours behind the newest attack.

All of them answer one question: **is this address on a list?**

We own the network stack, the renderer and the profile. So Shield answers
three questions, and each is a layer:

1. **Is it on a list?** Answered faster, in less memory, reading more of the
   list syntax than anyone on Chromium.
2. **What is it doing?** Answered from behaviour the engine can see and a list
   cannot: what a response contains, what a script reads, where an identifier
   goes, whether a page nobody has seen before is asking for a password.
3. **Did we break anything?** Answered by measuring, per site, on the user's own
   machine, and backing off without being asked.

Nothing leaves the machine to answer any of them, except the opt-in Shield DNS
question, and that is being cut down too (section 5).

## 2. Where we are

### Ad blocker **[measured]**

| | Zephyrus | Brave (adblock-rust 0.13.5) |
|---|---|---|
| Decisions on 9,591 real requests, same list | 4,467 blocked | 4,467 blocked, no request differs |
| Match time per request | **4.1 us** | 6.6 us (9.6 with request setup) |
| Matcher memory | **10.65 MB, shared by all profiles** | +13.8 MB |
| `$to=` rules | read | ignored |
| Lookahead regex rules | read (39 via ICU) | cannot |
| `$urlskip` (226 rules) | read | no support |
| Procedural cosmetic rules kept | **1,234 of 1,235** | 1,226 |
| `$csp` rules kept | **96 of 97** | 85 |
| Generated regex corpus blocked | **1,617** | 686 |
| Damaged or rewritten cache file | 0 crashes in 100,000 | 5% panic on rewrite |
| Cold build of the engine | 470 ms | **149 ms** |

Open, and the reason this plan exists:

- **Memory:** the cosmetic and scriptlet engines still cost about 19 MB **per
  profile**. Only the request matcher is shared.
- **Speed is only proven for the matcher.** Nobody has measured what a user
  feels: page load time, CPU and bytes on real pages against rivals.
- **Precision is unmeasured.** There is no number for how many sites Shield
  breaks, for us or for anyone else.
- **Syntax nobody on Chromium reads:** `$replace` 87, `$header` 54,
  `$ipaddress` 53, `$cname` 29, `$uritransform` 20, `##^` HTML filtering,
  `$method` 5. uBlock Origin on Firefox reads most of these; Brave reads none.
- No signed rule bundle, no update faster than daily, no public benchmark.

### Shield DNS **[measured, 1,857 links]**

| | Shield DNS | Brave (Google's lists) |
|---|---|---|
| Fresh OpenPhish links blocked at first sight | **85.8%** | 43.3% |
| Fresh PhishTank links | **92.0%** | 66.7% |
| Pages that really ask for a password (n=74) | 71.6% | 67.6%, a tie |
| False positives, top 100k sites | 0.07% | not measured |

Open:

- **The only independent test is a tie.** The first two rows favour us by
  construction (our resolvers read those feeds).
- **Misses are on shared hosts** (pages.dev, netlify.app, cloudfront). A host
  name cannot be blocked there without blocking everyone on it. More resolvers
  will not fix this.
- **It costs privacy to ask.** The host name goes to two companies. Google's
  design sends nothing by name. We may not say "safer than Google".
- **Added delay per navigation is unmeasured.**
- The sampling job stopped after 2 of 8 rounds, so n is still 74.

## 3. The scoreboard

| Axis | Metric | Now | Target |
|---|---|---|---|
| Faster | Match per request | 4.1 us | hold under 5 us with every new rule type |
| Faster | Page load, 30 ad-heavy sites, against Brave and Chrome+uBOL | unmeasured | fastest of the three, 3-run median |
| Faster | Delay Shield DNS adds to a navigation | unmeasured | 0 ms at the median, under 30 ms at p95 |
| Lighter | Blocker memory per extra profile | about 19 MB | under 1 MB |
| Lighter | CPU added per page by scriptlets and cosmetic work | unmeasured | measured, then halved |
| Recall | List rules read, all 16 lists | 98.1% and rising | over 99.8%; every rule type uBO reads |
| Recall | Requests blocked that Brave lets through, real crawl | 0 | over 0 with no extra breakage |
| Recall | Password-asking phishing pages stopped | 71.6% (tie) | over 90%, intervals clear of Google's |
| Precision | Sites visibly broken, top 500 | unmeasured | under 1%, and lower than Brave's |
| Precision | Shield DNS false positives, top 1M | 0.04 to 0.07% | under 0.02% |
| Private | Host names sent off the machine by Shield DNS | every unpopular host | only never-seen hosts, split so no one party sees both the user and the name |
| Trust | A rule file nobody signed can change what is blocked | yes | no |

## 4. Ad blocker, in order

One structural change per build. Each stage ends in a number and names what
would stop it.

### A0. The ruler (3 days)

- **Page benchmark.** 30 ad-heavy sites, replayed from a recording so the
  network is identical for everyone: load time, main-thread CPU, bytes,
  requests. Zephyrus, Brave, Chrome with uBlock Origin Lite, Chrome with
  nothing.
- **Breakage canary.** 500 popular sites, Shield on against off: main content
  present, page scripts' uncaught errors, the largest visible element, a
  screenshot difference. Run the same set through Brave.
- **One command** that prints the whole scoreboard, so every later stage is a
  before and after.

Gate: three runs agree within 5%.

### A1. One engine for every profile (4 days)

- Compile the cosmetic rules and the scriptlet table into the same kind of
  read-only, memory-mapped index the request matcher already uses, shared
  process-wide, checked on load, with the per-profile parts (allowlist,
  toggles) kept outside it.
- Build it off-thread when a list updates and cache it next to the list.

Gate: under 1 MB per extra profile; identical CSS and scriptlets on a
differential over every rule and 10,000 real page addresses. Stop if the cold
start gets slower.

### A2. Nothing computed twice (3 days)

- **Scriptlets per document:** build the injected script once per site and
  rule-set, keep it, and hand V8 its code cache so the second visit compiles
  nothing.
- **Cosmetic CSS per site:** the same, as a single stylesheet.
- **Survey less:** the generic-rule survey runs four times per page on timers.
  Drive it from DOM changes with a budget instead.

Gate: CPU per page from Shield halved on the page benchmark.

### A3. The response layer (8 days)

The layer extensions lost and built-in blockers never built. A streaming
rewriter in the request path, for text responses only, with a size cap and a
time budget, off unless a rule for that address exists.

- `$replace`: edit a response body (strip an ad block from a JSON feed, remove
  an anti-adblock check from a script).
- `##^` HTML filtering: remove elements before the parser sees them, so inline
  scripts never run.
- `$header`, `$uritransform`, `$method`, `$ipaddress`, `$cname` (the CNAME
  harvester already exists).
- Only from the trusted list sections, never from a user-added list, because a
  rule that can rewrite a response can do anything.

Gate: every rule of these types in the 16 lists read; a real-crawl block count
above Brave's; no measurable cost on pages with no such rule; fuzzed like every
other parser. Stop on any breakage rise in the canary.

### A4. Shield that corrects itself (5 days)

- **Breakage sentinel**, local only: after a block, watch for the signs the
  canary uses (the page's own scripts failing on a blocked name, main content
  missing, a reload within seconds). When they appear, relax the narrowest
  thing that fixes it, for that site, and say so in the Shield popup.
- **The same detector in reverse:** an ad that still rendered (a frame from a
  known ad host with a cosmetic rule that did not match) is logged for the
  rule report.
- Nothing is uploaded. A user can export the report.

Gate: breakage on the top 500 under 1% and below Brave's.

### A5. Behaviour, in shadow first (8 days)

- A small local classifier over what the engine already sees: who started a
  request, what the script read first, whether a stored identifier went out.
  It runs in shadow, logging what it would have blocked, until its precision is
  measured against the lists on real crawls.
- **Leak ledger:** when an identifier (a hashed email, a stored ID) leaves for
  a third party, record which and to whom, show it, and strip it. This is the
  feature no rival has.

Gate: shadow precision over 99% before it blocks anything.

### A6. Trust and proof (4 days)

- **Signed rule bundle**, with small differences between versions and an hourly
  channel for urgent fixes. Compiled from list text by a sandboxed compiler;
  the browser loads only the checked result.
- **Public benchmark:** the scoreboard command, the recordings and the rival
  versions, published so anyone can rerun it.

## 5. Shield DNS and phishing, in order

### D0. The ruler (2 days)

- Restart the sampling rounds until the password-page set passes n=300.
- Measure the delay Shield DNS adds, per navigation, cold and cached.
- Add the shared-host split to the report: how much of each side's misses are
  on hosts that cannot be blocked by name.

### D1. Ask without waiting (3 days)

- Start the DNS question at the same moment as the navigation and hold only the
  commit, never the request. A page that answers before the verdict waits; a
  verdict that arrives first costs nothing.
- Remember answers, good and bad, with the resolver's own lifetime.

Gate: 0 ms added at the median.

### D2. The address-level index (5 days)

- A local index of full phishing addresses from open feeds, in the same checked
  binary format as the ad index, updated hourly through the signed bundle.
- This is what covers shared hosts: `evil.pages.dev/login` can be blocked while
  `pages.dev` stays open.
- Looked up locally. Nothing is sent.

Gate: shared-host misses cut by more than half.

### D3. Password tripwire (6 days)

The check no list can do, because it needs no list.

- At the moment a password field is focused, on a site outside the popular
  set, with no history and no saved login for it: score what is on the page.
  A well-known brand's name in the host or the title on a site that is not that
  brand's, a certificate days old, a form that posts to another site, a host
  made of look-alike characters.
- Over the threshold: a warning in the field itself before a key is typed, with
  the reason in plain words and a way through.
- Entirely local. It also covers every new campaign from the first victim, not
  the first report.

Gate: password-page recall over 90% with the interval clear of Google's, and
under 1 warning per 1,000 legitimate sign-in pages. Stop if the false warning
rate cannot be held; a warning people learn to dismiss is worse than none.

### D4. Ask less, and reveal less (4 days)

- Ask only for hosts this profile has never loaded and that are not in the
  local good set.
- Send each question through a relay so the resolver sees the name without the
  user's address and the relay sees the address without the name, where the
  resolver supports it. Verify support before building; if neither resolver
  does, say so and keep the wording honest.
- Report, in the Shield popup, how many names were asked this week.

Gate: names sent off the machine cut by 90% with no loss of recall.

## 6. What we will not do

- Accept "blocks more" without the breakage number beside it.
- Let a rule that rewrites responses come from anywhere but a signed, trusted
  section.
- Turn on a classifier that has not run in shadow.
- Say "safer than Google" while Shield DNS sends a host name anywhere.
- Upload browsing data to improve any of this. Reports are local and exported
  by the user.

## 7. Risks

- **The response layer is the most dangerous code here.** It parses hostile
  bytes in the request path. Size caps, time budgets, fuzzing and a kill switch
  are part of the stage, not follow-ups.
- **A self-correcting blocker can be gamed** by a site that fakes breakage. The
  sentinel relaxes cosmetic and stand-in rules first, and never unblocks a
  tracker that carries an identifier.
- **The tripwire's false warnings.** Measured on real sign-in pages before it
  ships; ships in log-only mode first.
- **Rival numbers move.** Every comparison names the rival's version and date.
- **Maintenance.** Lists, stand-ins and feeds are a standing cost. The signed
  bundle (A6) is what makes that cost a pipeline instead of a rebuild.

## 8. Order across both, and effort

A0 and D0 first (the rulers), then A1, D1, A2, D2, A3, D3, A4, D4, A5, A6.
About 11 weeks of build. The first four weeks (rulers, shared engine, DNS
without waiting, nothing computed twice, the address index) cover every row of
the scoreboard marked "unmeasured" and the two cheapest leads.
