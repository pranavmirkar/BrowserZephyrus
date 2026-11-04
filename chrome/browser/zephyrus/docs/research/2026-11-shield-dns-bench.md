# Shield DNS: what it catches, what it misses, how we know

Status: UPDATED 2026-11-04 (see "Update" below). First-sight recall and false positives are settled; the
credential-trap comparison is a tie so far and the sampling rounds are still adding to it. Numbers marked (n=...) carry their sample size.
Everything here is reproducible with `research/shield_bench.py`, `shield_bench_content.py`,
`shield_bench_rounds.py` and `shield_bench_final.py`.

## Update 2026-11-04: 1,857 links, four sampling rounds

Same method, more data (`shield_bench_final.py`, 95% Wilson intervals, a lead is claimed only when they do not
overlap):

| feed | n | Brave (Google) | Cloudflare | Quad9 | **Shield DNS** |
|---|---|---|---|---|---|
| OpenPhish | 626 | 43.3% (39.5-47.2) | 62.5% | 39.8% | **85.8% (82.8-88.3)** |
| PhishTank | 87 | 66.7% (56.2-75.7) | 58.6% | 88.5% | **92.0% (84.3-96.0)** |
| urlscan.io | 1,144 | 6.0% | 3.8% | 2.8% | 5.3% (no clear difference) |
| **credential traps**, OpenPhish + PhishTank | 74 | 67.6% (56.3-77.1) | 62.2% | 31.1% | 71.6% (60.5-80.6) |
| **credential traps**, every feed | 118 | 46.6% (37.9-55.6) | 45.8% | 22.0% | 51.7% (42.8-60.5) |

What this says, in the order it should be trusted:

* **Independent ground truth is a tie.** On pages that really ask for a password the point estimate favours
  Shield DNS (71.6% vs 67.6%) but the intervals overlap by a wide margin. Do not claim a lead here.
* **First-sight recall on the two phishing feeds is a clear lead** (85.8% vs 43.3%, 92.0% vs 66.7%). The caveat
  stands: Cloudflare reads OpenPhish and Quad9 reads PhishTank, and Google reads neither, so these rows
  favour us by construction. They show we catch fresh links that Google's lists have not yet, not that we are
  "safer".
* Brave's first-sight recall on OpenPhish fell from 58.5% (n=395) to 43.3% (n=626) as newer links came in: its
  lists lag the newest campaigns. That is a property of the lists, not of the method.
* Quad9 alone is a weak complement to Cloudflare on the independent traps (31.1%); on those it adds about nine
  points over Cloudflare alone (71.6% vs 62.2%).

Claim wording that survives this data: "Shield DNS (opt-in) blocks about 86% of fresh OpenPhish links and 92% of
PhishTank links at first sight, where Safe Browsing's lists blocked 43% and 67%, and on pages that really ask for
a password the two are level." Never: "safer than Google or Brave".

## What Shield DNS is now

Two free security resolvers are asked about the host name of a navigation (only when the user turned it on,
only for hosts outside the top 100,000 sites, cached for minutes). The page is blocked if EITHER says no.

| resolver | how it says "bad" | transport |
|---|---|---|
| Cloudflare security | answers `0.0.0.0` | DoH JSON |
| Quad9 | `NXDOMAIN` with Extended DNS Error 17 "Filtered" (measured on the wire, 2026-11-03) | RFC 8484 DoH (HTTP/2) |

Quad9 was added because the first benchmark showed Cloudflare alone is weak on PhishTank-listed hosts
(59.6%) and Quad9 alone is weak on OpenPhish-listed hosts (24.1%): they read different intelligence.
A bare `NXDOMAIN` without the error code is *not* a block (the name may simply not exist).

## How it was measured

* Fresh phishing links from three independent feeds: OpenPhish, PhishTank, urlscan.io (tag `phishing`).
  At most two per registrable domain per feed, so one campaign cannot dominate.
* Only links that still resolve are counted (a dead host proves nothing).
* "Brave" = Google Safe Browsing lists as Brave's own profile holds them (isolated Brave profile kept
  running so its lists stay fresh), looked up by hash prefix offline. Nothing malicious is ever loaded.
* Resolvers are probed over plain DNS against their unfiltered twin; "blocked" means the filtered one
  refuses while the twin resolves.
* Ground truth that does not come from any blocklist: a **credential trap** is a live page with a password
  field on a non-popular host. A list that merely reports a link cannot make it one.
* Intervals are 95% Wilson intervals. A lead is claimed only when the intervals do not overlap.

## Results so far

### First sight (507 live hosts)

| feed | n | Brave (Google) | Cloudflare | Quad9 | **Shield DNS (CF or Quad9)** |
|---|---|---|---|---|---|
| OpenPhish | 212 | 56.1% | 75.9% | 24.1% | **83.0%** (77-87) |
| PhishTank | 89 | 66.3% | 59.6% | 88.8% | **92.1%** (85-96) |
| urlscan.io | 206 | 4.9% | 1.9% | 0.5% | 1.9% |
| all | 507 | 37.1% | 43.0% | 25.8% | 51.7% |

Read with care:
* Resolvers appear to ingest OpenPhish (Cloudflare) and PhishTank (Quad9). A feed that a service reads
  flatters that service on that feed. Brave/Google read neither, so the comparison tilts toward us on these
  two rows. This is why the credential-trap row below matters more.
* urlscan.io's `phishing` tag is mostly not phishing (it includes `accounts.google.com`). Everyone scores
  near zero. It is shown, not counted.

### Credential traps (password field, independent of any list)

| subset | n | Brave | Cloudflare | Quad9 | **Shield DNS** |
|---|---|---|---|---|---|
| OpenPhish + PhishTank + urlscan | 39 | 41.0% (27-57) | 38.5% | 30.8% | **53.8%** (39-68) |
| OpenPhish + PhishTank only | 28 | 57.1% | 50.0% | 39.3% | **71.4%** |

The intervals overlap: **on independent ground truth the lead is not yet proven**. More rounds are running
to shrink the interval.

### False positives (benign popular domains)

Majestic Million sample, resolvers asked about domains that are in the top 100k / 100k-1M:

| set | Cloudflare | Quad9 | CleanBrowsing |
|---|---|---|---|
| top 100k (n~2,790) | 2 blocked (0.07%) | 0 (0.00%) | 15 (0.54%) |
| 100k-1M (n~2,770) | 1 (0.04%) | 1 (0.04%) | 0 of 710 |

An upper bound (a few long-tail domains really are bad). Shield DNS asks only about hosts outside the top
100k, and the combined rate is a fraction of a percent.

## Where DNS cannot help (a structural limit, not a tuning issue)

Of the credential traps that **nobody** caught, most live on shared hosts: `*.pages.dev`, `*.netlify.app`,
`*.cloudfront.net`, S3-style object stores, `*.elementor.cloud`. A resolver cannot refuse a name that
millions of good sites share. Those need URL-level knowledge: Google's lists have it; our local index
(`zephyrus_threats.bin`) has it only as fresh as its last refresh. **Freshness of the local index is the real
gap**, and it is solved by the signed rule bundle (plan item 4), not by more resolvers.

## What we can honestly claim today

* Ahead of Brave/Google on OpenPhish-listed phishing at first sight (83% vs 56%, intervals do not overlap),
  with the caveat that Cloudflare reads that feed.
* Not worse on PhishTank (92% vs 66%) with the same ingestion caveat for Quad9.
* On list-independent ground truth: ahead in the point estimate (54% vs 41%), not significant at n=39.
* False-positive rate on popular sites below 0.2% combined.
* NOT claimed: "safer than Google". Google's lists catch URL-level cases DNS cannot, and
  Google does not send host names anywhere. Shield DNS is opt-in and sends the host name to two services.

## Lag: nothing catches up

Of the hosts a service did NOT block at first sight, how many it blocked later (same 507 links re-probed):

| after | Brave (Google) | Cloudflare | Quad9 |
|---|---|---|---|
| +1 h | 0.6% | 0.0% | 1.6% |
| +3 h | 1.0% | 2.1% | 1.6% |

What a service misses at first sight it mostly never catches within hours. So coverage at first sight is what
counts, and a "check again later" feature would add almost nothing.

## Verified in the browser (Chromium 156 build)

`research/ab_threat.py`, 11 of 11 cases: local index hits and misses; with Shield DNS OFF a Cloudflare-blocked host
and a Quad9-only-blocked host both load and nothing is sent; with it ON both show the warning page, a made-up host
loads, a locally listed host is blocked without a lookup.

## Still to come

* Lag curves (+1h, +3h, +6h, +24h): how quickly each service catches what it missed at first sight.
* Rounds over several days to grow the credential-trap sample (`shield_bench_rounds.py`).
* A claim sentence we will publish only if it survives all of the above.
