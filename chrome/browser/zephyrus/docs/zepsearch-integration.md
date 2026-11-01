# ZepSearch integration

The omnibox and new-tab search box use Chromium's `TemplateURLService`. ZepSearch
is a prepopulated provider with the template `chrome://zepsearch/?q={searchTerms}`
and is the fallback default for profiles without a user-selected provider.
Existing user and policy selections continue through `DefaultSearchManager`.
URLs remain under Chromium's normal omnibox URL-versus-query classification.

`chrome://zepsearch/` is a browser-owned WebUI. Its JavaScript sends a
`zepSearch` WebUI message to the browser process; the handler uses the profile's
browser-process network service to GET `/api/search`. The response is parsed and
limited before crossing back to the page. The page renders backend values with
`textContent`, and only HTTP(S) result URLs become links. A newer request cancels
the prior loader; its request ID also stops stale UI updates. Query, page number,
and optional filters live in the page URL and browser history.

The development API base URL is `http://192.168.137.1:8000/`, defined only in
`zepsearch_config.cc`. To use a deployed service, launch with
`--zepsearch-api-base-url=https://search.example/`. The override must be an
HTTP(S) origin without credentials, query, or fragment; invalid overrides fail
closed. The page makes no cross-origin frontend requests, so the API needs no
CORS allowance for this integration.

Build from `D:\chromium\src` with `autoninja -C out/Release chrome -j 8`.
On a clean profile, type a query in the omnibox or the new-tab search box.
Direct testing: `chrome://zepsearch/?q=hello&pageno=1`. Supported URL filters
are `categories`, `engines`, `language`, `safesearch` (0–2), and `time_range`
(`day`, `month`, `year`). A temporary unreachable override can be used to check
the offline and retry UI without changing the configured development address.
