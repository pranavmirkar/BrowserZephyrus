# Same lists, same requests: Zephyrus's engine against Brave's

Four small programs around Brave's `adblock-rust` 0.13 (crates.io), used as a black box.

- `brave_cmp` builds the engine from the lists, times it on the 20,000-request
  synthetic corpus the C++ benchmark uses, and reports serialised size, load time
  and memory.
- `cross` replays about 247,000 probes derived from every rule the Zephyrus parser
  accepts and counts decisions that differ.
- `real` times the engine on recorded page traffic and writes its verdicts
  (`../real_traffic/run_real_traffic.sh`).
- `parity` reports, line by line, which rules its parser keeps
  (`../real_traffic/parity_report.py` reads the result).

Differences are not automatically Zephyrus bugs. The ones found so far were
(1) syntax Zephyrus does not support (regex, `redirect=` surrogates, `removeparam`),
(2) `domain=x.*` entity and `$doc` alias handling, where Zephyrus follows
uBlock Origin and Brave differs, and (3) three cosmetic lines the network
parser wrongly admitted, fixed in `adblock_filter_engine.cc`. Probes whose
resource type has no equivalent in the other engine (popup) are skipped.
Brave's crate does not parse `$popup`, `$denyallow` or `$popunder` rules;
Zephyrus does. On `parity`, one `$badfilter` rule tested alone looks dropped
by Zephyrus. That is an artefact: `$badfilter` cancels another line, so it
only means something inside a whole list.
