# Site tool packs: research and taxonomy (2026-10-06)

Part of Agent v2 (see `agent-v2-plan.md`, section 3 phase 3). The idea: the model
always has ~25 general primitives; when the page is a known kind of site, the
browser adds a small **pack** of verbs for that site, and only that pack.

## 1. What the research found

1. **WebMCP is real and is already in our tree.** A W3C Community Group draft
   (Google and Microsoft) lets a page register tools with
   `navigator.modelContext.registerTool({name, description, inputSchema, execute})`,
   or declare them on a `<form>` (`toolname`, `tooldescription`, ...). Chrome 146
   shipped an early preview (Feb 2026). Our Chromium 151 has the whole thing:
   `third_party/blink/renderer/core/script_tools/` (`ModelContext`,
   `registerTool/getTools/executeTool/ontoolchange`), runtime feature `WebMCP`,
   and a browser-process registry (`content/browser/renderer_host/
   model_context_user_data.*`, mojom `ModelContextHost` with `GetScriptTools` and
   `ExecuteRemoteScriptTool`). Tools carry `annotations.read_only` and
   `untrusted_content`; same-origin by default; submit events carry an
   `agentInvoked` flag. To verify: whether the feature is on by default in our
   build, and which content-side API lists and runs tools from the browser process.
2. **Injecting WebMCP tools into sites that do not have them is an existing
   practice.** Browserbase ships `webmcp-gen` ("author, compile and validate
   site-specific WebMCP init scripts") and `add-webmcp` (generate tools from an
   app's routes, forms and actions, validated before use). That is our "pack":
   an init script that registers tools through the same API.
3. **Other browser agents (Stagehand, browser-use, Playwright MCP skills)** cache
   resolved actions and use compact accessibility snapshots instead of
   screenshots; none ship per-site verb packs as a first-class concept. That is
   room for us, and it matches the Page Card plan.
4. **Canvas apps are driven by commands, not coordinates.** Figma exposes Quick
   Actions (Ctrl+/ or Ctrl+P: every tool, command and plugin by name), single-key
   tools (V, F, R, T, A for auto layout), Shift+A, Ctrl+Alt+K (component), and a
   DOM layers panel and properties panel. Text-addressable commands are
   deterministic where canvas clicks are not. This is exactly where our agent
   flailed in the Figma trace.
5. **What India uses** (Similarweb, Aug 2026, consumer top 40): Google,
   YouTube, Instagram, WhatsApp, ChatGPT, Amazon.in, Facebook, Wikipedia,
   Flipkart, Cricbuzz, Reddit, LinkedIn, Indiatimes, X, Gemini, Hotstar, Bing,
   Cricinfo, Pinterest, Myntra, NDTV, BookMyShow, Netflix, Aaj Tak, Claude, IMDb,
   JustDial. Work and utility sites that matter for an agent but not for traffic
   rank: Gmail, Docs/Sheets, Figma, Notion, GitHub, IRCTC, UPI and bank pages,
   government portals.

Sources: zuplo.com/blog/what-is-webmcp, w3c-cg.github.io/aikr/webMCP, github.com/
browserbase/skills, help.figma.com (shortcuts, Quick Actions), similarweb.com/
top-websites/india.

## 2. How a pack is made, chosen and shown

**A pack** = manifest (id, version, signature, match rules, category, mechanism,
risk) + verbs. A verb = name, one-line description, typed args, preconditions,
steps, postcondition (the effect that proves it worked).

**Four mechanisms**, safest first:

| Mechanism | What it is | Policy |
|---|---|---|
| `native` | the SITE's own WebMCP tools, read from the page | descriptions are page text: treated as `untrusted_content`, never as instructions; `read_only` tools run freely, others judged like any action |
| `macro` | steps over our primitives, addressed by role+name | every step is an ordinary tool call, judged individually; risk is DERIVED, never declared |
| `keys` | keyboard command sequences (Quick Actions, shortcuts) | same: each key sequence is an input call |
| `script` | an init script that registers WebMCP-style tools in the page | READ-ONLY in v1 (extract, query state). Mutations go through macro/keys so the kernel sees them. Signed first-party packs only |

**Choosing a pack**, strongest signal first: the user named it ("in Figma") ->
URL host and path match -> page signals (WebGL canvas, contenteditable root,
`navigator.modelContext` tools present, known app markers) -> a generic category
pack (editor, canvas, table, form, checkout, media, search) -> nothing.

**Showing it, without breaking the prompt cache.** Changing the tool list
invalidates the cache, so the tools array stays fixed: the ~25 primitives plus
one dispatcher `site.do(verb, args)` and `site.list()`. The pack is a
transcript turn appended when the site context is entered (append-only, cache
friendly): "SITE: Figma. Verbs: ...", each verb with a compact signature. At most
~12 verbs (~600-900 tokens); `site.list()` returns the rest on demand. Leaving
the site appends a note; nothing is rewritten.

## 3. Categories

Columns: why plain primitives fail, the verbs worth having (first 3 in bold),
mechanism, risk, priority (P0 now, P1 next, P2 later).

### A. Canvas and design tools: Figma, Canva, Miro, Excalidraw, Google Slides (canvas), Whimsical
- **Fails because:** the content is a WebGL/canvas surface; there is no DOM per object; coordinate clicks drift and the accessibility tree is empty for the canvas.
- **Verbs:** **`quick_action(name)`** (Ctrl+/ then the typed command), **`select_layer(name|path)`** (layers panel is DOM), **`set_property(field, value)`** (properties panel inputs by label: X, Y, W, H, fill, radius), `insert(shape|frame|text, at, size)`, `rename_layer`, `align(dir)`, `distribute`, `group/ungroup`, `component(create|detach)`, `export(format)`, `zoom_to(fit|selection)`.
- **Mechanism:** keys + macro on the DOM panels; canvas pointer drags only for `insert` and `move`, using the human-paced pointer path that already exists. **Risk:** edits a document (undoable); sharing/publishing = ask. **P0** for the demo and the Figma trace.

### B. Rich editors and documents: Notion, Google Docs, Gmail compose, Confluence, Outlook web, WordPress, Medium, Overleaf
- **Fails because:** contenteditable and canvas-rendered text, async focus moves (essay landed in the title once), formatting by shortcuts.
- **Verbs:** **`write(text, at=cursor|end|title|body)`** (line-paced, verifies by reading back), **`format(bold|heading|list|link, selection)`**, **`find_replace(a, b)`**, `insert_block(type)`, `comment`, `outline()` (headings as ids), `read_document()` (chunked).
- **Mechanism:** macro + keys; Docs uses its accessibility mode and menu search (Alt+/). **Risk:** drafting is safe; **send/share/publish = ask unless the task said so**. **P0** (we already hit the Notion bug).

### C. Commerce and marketplaces: Amazon.in, Flipkart, Myntra, Meesho, Nykaa, JioMart, Croma
- **Fails because:** product grids with lazy loading, variant pickers (size/colour/pincode), sponsored noise, price formats.
- **Verbs:** **`search(query, filters)`**, **`list_results(n, fields=title,price,rating,delivery)`** (a collapsed list, not 40 cards), **`open_product(rank|title)`**, `select_variant(size, colour)`, `check_delivery(pincode)`, `compare(items)` (opens 2-4, extracts a table), `add_to_cart`, `read_reviews(summary)`.
- **Mechanism:** macro + script (read-only extraction of structured data / JSON-LD). **Risk:** `add_to_cart` allowed; **checkout, payment, address, OTP = hand-off, always**. **P0** (top Indian sites, great demo).

### D. Travel and ticketing: IRCTC, MakeMyTrip, RedBus, Ixigo, BookMyShow, Cleartrip
- **Fails because:** multi-step date/route widgets, autocompletes that need the suggestion clicked, captchas and logins (IRCTC), seat maps.
- **Verbs:** **`search_trains|buses|flights|shows(from, to, date, class)`**, **`list_options(filters)`**, `select_option(rank)`, `fill_passenger(details)` (typing from the user's saved profile, redacted fields handled), `read_fare()`.
- **Mechanism:** macro; autocomplete handled as a reflex. **Risk:** search is read-only; **booking, payment, captcha, OTP = hand-off**. **P1**.

### E. Food, grocery and local services: Swiggy, Zomato, Blinkit, Zepto, JustDial, Urban Company
- **Verbs:** **`set_location(area)`**, **`search_dishes|items(query)`**, **`list_restaurants(filters)`**, `open_menu`, `add_item(name, qty)`, `read_cart()`.
- **Risk:** cart fine; **placing the order and payment = hand-off**. **P1**.

### F. Social, messaging, communities: WhatsApp Web, Instagram, X, Facebook, LinkedIn, Reddit, Pinterest, Telegram Web
- **Fails because:** infinite feeds, virtualised lists, compose boxes that reject synthetic typing, privacy sensitivity.
- **Verbs:** **`read_feed(n)`**, **`open_thread|profile|post(name)`**, **`read_messages(thread, n)`**, `draft_message(thread, text)` (types, does not send), `search(query)`, `summarise_thread()`.
- **Risk:** reading someone's private messages goes to the model, so say so in the pack and respect the per-workspace cloud switch; **sending, posting, connecting, following = ask unless the task said so**. **P1** (WhatsApp Web and LinkedIn first).

### G. Video, audio and streaming: YouTube, Hotstar, Netflix, JioCinema, Spotify web, IMDb
- **Fails because:** a custom player, ads, autoplay, unlabeled icon buttons, transcripts hidden.
- **Verbs:** **`search(query, filters)`**, **`open_result(rank|title)`**, **`player(play|pause|seek|speed|fullscreen|captions)`**, `skip_ad`, `read_transcript()` (the key to summarising a video cheaply), `read_description/comments`, `add_to_watchlist`.
- **Risk:** low; subscription or purchase = hand-off. **P0** (YouTube is #2 in India; transcript summarisation is a showcase for "general intelligence").

### H. Search, knowledge and AI assistants: Google, Bing, Wikipedia, ChatGPT, Gemini, Claude, Perplexity, StackOverflow, news (NDTV, Aaj Tak, Indiatimes), Cricbuzz/Cricinfo
- **Verbs:** **`search(query, site=, time=)`**, **`read_results(n)`** (titles, snippets, sources, dates), **`open_result(rank)`**, `read_article()` (main text without chrome, with byline and date), `live_score(match)` (Cricbuzz), `ask(assistant, prompt)` (type into another AI site and read the reply: delegation).
- **Mechanism:** macro + script (read-only extraction). **Risk:** low. **P0** (the backbone of research mode).

### I. Productivity suites and data: Gmail, Google Calendar, Drive, Sheets, Excel web, Zoho, Airtable, Trello
- **Verbs:** Gmail **`search(query)`**, **`open_thread`**, **`draft_reply(text)`**, `label/archive`; Calendar `create_event(title, when, who)`, `find_free_slot`; Sheets `read_range(A1:C20)`, `write_range`, `add_formula`; Drive `find_file(name)`, `open`.
- **Mechanism:** macro + keys (Sheets name box, Gmail shortcuts); Sheets via its accessibility/grid layer. **Risk:** drafts and events low; **sending mail, sharing, deleting = ask**. **P1**.

### J. Developer tools: GitHub, GitLab, Stack Overflow, Colab, Vercel, npm, cloud consoles
- **Verbs:** **`search_repos|code|issues`**, **`open_file(path)`**, **`read_file()`** (raw), `list_prs`, `read_pr_diff`, `create_issue(draft)`, `read_ci_status`.
- **Risk:** reading is safe; **merge, deploy, delete, secrets and tokens = ask or refuse**. **P1**.

### K. Government, banking and payments (India): IRCTC login, Income Tax, DigiLocker, UIDAI, Parivahan, EPFO, bank sites, Paytm/PhonePe/Google Pay web
- **Fails because:** captchas, OTPs, session timeouts, strict forms.
- **Verbs:** `find_service(name)`, `open_form(name)`, `fill_known_fields(profile)`, `read_status(reference)`. **Never** OTP, password, PIN, card, UPI PIN.
- **Risk:** highest. The pack's job is to get the user to the right form and fill what is safe; everything sensitive is a hand-off by policy, not by the pack. **P2**, after the policy tests for this class exist.

### L. Generic fallback packs (these cover most sites nobody wrote a pack for)
- **form** (`fill(fields)`, `submit`, validation errors read back), **table** (`read_table`, `sort`, `filter`, `paginate_collect(n)`), **list/feed** (`collect(n, fields)`, `load_more`), **modal/overlay** (`dismiss`, `accept_cookies(minimal)`), **checkout** (always hand-off), **login wall** (always hand-off), **search box** (`search_here(query)`), **media** (the player verbs of G), **article** (`read_article`). **P0:** these six or seven give breadth; the named packs give depth.

## 4. Verification of a pack

- Each verb has a **postcondition** that is checked on every run; failure is reported with the reason and the model falls back to primitives. Never a silent miss.
- **Fixture tests:** DOM-addressed verbs are tested against recorded snapshots of the real pages (golden, offline) and our hermetic fixture site; keys/canvas verbs against the fixture canvas app.
- **Live smoke list** per pack, run by hand before a release; **nightly replay** only for public, logged-out flows.
- **Safety suite additions:** an injected site-pack description, a malicious native WebMCP tool description, a `script` verb trying to mutate, and a verb whose steps would reach a payment control (must hand off).

## 5. Build order for packs

1. **Mechanism and dispatcher** (`site.do`, `site.list`, the context turn, pack signing and loading, risk derived from steps, postcondition checks). No named packs yet.
2. **Generic packs** (L) and **native WebMCP consumption** (read the page's own tools, labelled untrusted).
3. **Named P0 packs:** Figma (A), Notion/Docs/Gmail compose (B), Amazon.in + Flipkart (C), YouTube (G), Google + Wikipedia (H).
4. **P1 packs:** WhatsApp Web, LinkedIn, Gmail, Calendar, Sheets, GitHub, IRCTC/BookMyShow search, Swiggy/Zomato.
5. **Pack Studio** (later): record a successful run, draft a pack from the trace, validate by replay, human review before signing.

## 6. Decisions for the user

1. **P0 named packs:** Figma, Notion/Docs/Gmail-compose, Amazon.in + Flipkart, YouTube, Google + Wikipedia. Change the list?
2. **Mechanism order:** I recommend native WebMCP + macro + keys now and `script` read-only; no mutating scripts in v1. OK?
3. **Private-content sites (WhatsApp, Gmail, banking):** read them by default with the cloud switch the user already controls, or require an extra per-site opt-in? I recommend an extra per-site opt-in.
