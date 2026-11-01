#!/usr/bin/env bash
# Reproduces every number in docs/research/2026-11-deep-dive.md.
#   chrome/browser/zephyrus/research/run_all.sh [work-dir]      (needs g++ 13, python3, node, libssl-dev, network)
# Nothing here is built by GN. The real engine sources are compiled unmodified against small stand-ins in ./shim.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd); ROOT=$(cd "$HERE/../../../.." && pwd); ADB="$ROOT/chrome/browser/zephyrus/adblock"
OUT=${1:-/tmp/zephyrus-research}; L="$OUT/lists"; T="$OUT/threat"; B="$OUT/build"; mkdir -p "$L" "$T" "$B"
CXX="g++ -std=c++20 -O2 -I$HERE/shim -I$ROOT -I$HERE"
get() { [ -s "$2" ] || curl -sSfL -m 90 -o "$2" "$1"; }
echo "== fetching the lists the updater fetches (third-party data; kept in $OUT, never in the repo)"
U=https://raw.githubusercontent.com/uBlockOrigin/uAssets/master/filters
get https://easylist.to/easylist/easylist.txt $L/easylist.txt   # no longer committed to the repo: fetched, as the updater does
get https://easylist.to/easylist/easyprivacy.txt $L/easyprivacy.txt
for f in filters-general filters-2020 filters-2021 filters-2022 filters-2023 filters-2024 filters-2025 filters-2026 privacy quick-fixes badware unbreak annoyances-cookies; do get $U/$f.txt $L/$f.txt; done
get https://secure.fanboy.co.nz/fanboy-cookiemonster.txt $L/cookiemonster.txt
get https://easylist-downloads.adblockplus.org/indianlist.txt $L/indianlist.txt
get https://raw.githubusercontent.com/mitchellkrogza/Phishing.Database/master/phishing-domains-ACTIVE.txt $T/phishdb_domains.txt
get https://raw.githubusercontent.com/mitchellkrogza/Phishing.Database/master/phishing-links-ACTIVE.txt $T/phishdb_links.txt
LISTS="$L/easylist.txt $L/easyprivacy.txt $L/filters-general.txt $L/filters-2020.txt $L/filters-2021.txt $L/filters-2022.txt $L/filters-2023.txt $L/filters-2024.txt $L/filters-2025.txt $L/filters-2026.txt $L/privacy.txt $L/quick-fixes.txt $L/badware.txt $L/unbreak.txt $L/annoyances-cookies.txt $L/cookiemonster.txt $L/indianlist.txt"
echo; echo "== 1. what the engine can and cannot use of those lists"; python3 -I "$HERE/list_audit.py" $LISTS | head -30
echo; echo "== 2. the engine as shipped: parse time, heap, per-request cost, destructor"
$CXX -o $B/baseline_engine $HERE/baseline_engine.cc $ADB/adblock_filter_engine.cc $ADB/adblock_cosmetic_engine.cc $ADB/adblock_scriptlet_engine.cc $ADB/adblock_list_util.cc && $B/baseline_engine $LISTS
echo; echo "== 3. where a request's time goes"
$CXX -o $B/cost_split $HERE/cost_split.cc $ADB/adblock_filter_engine.cc $ADB/adblock_list_util.cc && $B/cost_split $LISTS
$CXX -o $B/token_hotspots $HERE/token_hotspots.cc $ADB/adblock_filter_engine.cc $ADB/adblock_list_util.cc && $B/token_hotspots $LISTS
echo; echo "== 4. ZIndex: build, mmap, differential test against the real engine, speed"
$CXX -o $B/ztest $HERE/ztest.cc $ADB/adblock_filter_engine.cc $ADB/adblock_list_util.cc && (cd $B && ./ztest $LISTS)
echo; echo "== 5. ZIndex blob fuzzing under ASan+UBSan (small index so thousands of mutations are cheap)"
SMALL="$L/quick-fixes.txt $L/unbreak.txt $L/privacy.txt $L/annoyances-cookies.txt"
(cd $B && ./ztest $SMALL >/dev/null && mv zidx.bin zidx_small.bin)
g++ -std=c++20 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined -I$HERE/shim -I$ROOT -I$HERE -o $B/zfuzz $HERE/zfuzz.cc $ADB/adblock_list_util.cc
(cd $B && ZIDX_BLOB=zidx_small.bin ZIDX_N=${ZIDX_N:-2500} ASAN_OPTIONS=detect_leaks=0 ./zfuzz $SMALL)
echo; echo "== 6. ThreatIndex (offline, no per-URL network query)"
g++ -std=c++20 -O2 -I$HERE -o $B/threat_test $HERE/threat_test.cc && $B/threat_test $T
echo; echo "== 7. IdLeakScanner"
g++ -std=c++20 -O2 -I$HERE -o $B/idleak_test $HERE/idleak_test.cc -lcrypto && $B/idleak_test
echo; echo "== 8. scriptlet runtime: V8 compile cost per document"; node "$HERE/scriptlet_compile_time.js" "$ADB/adblock_scriptlet_engine.cc"

echo; echo "== 9. Brave's engine on the same lists and requests (needs cargo)"
if command -v cargo >/dev/null; then
  g++ -std=c++20 -O2 -I$HERE/shim -I$ROOT -I$HERE -o $B/corpus_dump $HERE/brave_compare/corpus_dump.cc && $B/corpus_dump $LISTS > $B/corpus.tsv
  g++ -std=c++20 -O2 -I$HERE/shim -I$ROOT -I$HERE -o $B/probe_dump $HERE/brave_compare/probe_dump.cc $ADB/adblock_filter_engine.cc $ADB/adblock_list_util.cc && $B/probe_dump $LISTS > $B/probes.tsv
  python3 -I - $LISTS > $B/all_lists.txt <<'PY'
import sys
for f in sys.argv[1:]:
    print("\n! ===== " + f + " =====\n" + open(f, encoding="utf-8", errors="replace").read() + "\n")
PY
  (cd $B && CARGO_TARGET_DIR=$B/cargo_target cargo build --release --manifest-path $HERE/brave_compare/Cargo.toml -q && ./cargo_target/release/brave_cmp && ./cargo_target/release/cross)
else echo "cargo not found; skipped"; fi
