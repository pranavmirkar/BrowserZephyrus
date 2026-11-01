# Same lists, same requests: Zephyrus's engine against Brave's

`brave_cmp` builds Brave's `adblock-rust` engine from the lists, times it on the
20,000-request corpus the C++ benchmark uses, and reports serialised size, load
time and memory. `cross` replays ~363,000 probes derived from every rule the
Zephyrus parser accepts and counts decisions that differ.

Differences are not automatically Zephyrus bugs. The ones found so far were
(1) syntax Zephyrus does not support (regex, `redirect=` surrogates),
(2) `domain=x.*` entity and `$doc` alias handling, where Zephyrus follows
uBlock Origin and Brave differs, and (3) three cosmetic lines the network
parser wrongly admitted, fixed in `adblock_filter_engine.cc`. Probes whose
resource type has no equivalent in the other engine (popup) are skipped.
