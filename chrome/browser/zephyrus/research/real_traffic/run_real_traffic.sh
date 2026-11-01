#!/usr/bin/env bash
# Real-traffic evaluation: load pages as an ordinary visitor, then ask each engine what it would have blocked.
#   chrome/browser/zephyrus/research/real_traffic/run_real_traffic.sh [work-dir]
# Needs: run_all.sh already run once with the same work-dir (it downloads the lists), node + playwright (npm i playwright),
# g++, cargo. The crawl is skipped if <work-dir>/crawl/out already exists. Pages are fetched once each, like a visitor.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd); KIT=$(cd "$HERE/.." && pwd); ROOT=$(cd "$KIT/../../../.." && pwd); ADB="$ROOT/chrome/browser/zephyrus/adblock"
OUT=${1:-/tmp/zephyrus-research}; L="$OUT/lists"; C="$OUT/crawl"; B="$OUT/build"; I="$OUT/indep"; mkdir -p "$C" "$B" "$I"
CXX="g++ -std=c++20 -O2 -I$KIT/shim -I$ROOT -I$KIT"
LISTS="$L/easylist.txt $L/easyprivacy.txt $L/filters-general.txt $L/filters-2020.txt $L/filters-2021.txt $L/filters-2022.txt $L/filters-2023.txt $L/filters-2024.txt $L/filters-2025.txt $L/filters-2026.txt $L/privacy.txt $L/quick-fixes.txt $L/badware.txt $L/unbreak.txt $L/annoyances-cookies.txt $L/cookiemonster.txt $L/indianlist.txt"
get() { [ -s "$2" ] || curl -sSfL -m 90 -o "$2" "$1"; }
[ -d "$C/out" ] || node "$HERE/crawl.js" "$HERE/sites.txt" "$C/out" 4
python3 -I "$HERE/make_tsv.py" "$C/out" "$C/real.tsv"
echo; echo "== shipped engine vs ZIndex on real traffic"
$CXX -o $B/real_zeph $HERE/real_zeph.cc $ADB/adblock_filter_engine.cc $ADB/adblock_list_util.cc && (cd $B && ./real_zeph "$C/real.tsv" $LISTS && mv real_zeph_decisions.txt "$C/zeph_decisions.txt")
echo; echo "== does the initiator definition matter? the frame that issued the request vs the tab's top-level page"
awk -F'\t' 'BEGIN{OFS="\t"} {$3=$12; print}' "$C/real.tsv" > "$C/real_top.tsv"
(cd $B && ./real_zeph "$C/real_top.tsv" $LISTS > /dev/null && mv real_zeph_decisions.txt "$C/zeph_top_decisions.txt")
python3 -I "$HERE/initiator_compare.py" "$C/real.tsv" "$C/zeph_decisions.txt" "$C/zeph_top_decisions.txt"
$CXX -o $B/explain_initiator $HERE/explain_initiator.cc $ADB/adblock_filter_engine.cc $ADB/adblock_list_util.cc && $B/explain_initiator "$C/real.tsv" "$C/zeph_decisions.txt" "$C/zeph_top_decisions.txt" $LISTS
echo; echo "== Brave's engine on the same requests"
python3 -I - $LISTS > $B/all_lists.txt <<'PY'
import sys
for f in sys.argv[1:]:
    print("\n! ===== " + f + " =====\n" + open(f, encoding="utf-8", errors="replace").read() + "\n")
PY
CARGO_TARGET_DIR=$B/cargo_target cargo build --release --manifest-path $KIT/brave_compare/Cargo.toml -q
$B/cargo_target/release/real $B/all_lists.txt "$C/real.tsv" "$C/brave_decisions.txt"
echo; echo "== independent blocklists (research use only; licences differ: Disconnect is CC BY-NC-SA, see THIRD_PARTY_DATA.md; check the rest before shipping anything derived from them)"
get https://raw.githubusercontent.com/StevenBlack/hosts/master/hosts $I/stevenblack.txt
get "https://pgl.yoyo.org/adservers/serverlist.php?hostformat=nohtml&showintro=0&mimetype=plaintext" $I/peterlowe.txt
get https://adguardteam.github.io/AdGuardSDNSFilter/Filters/filter.txt $I/adguard_dns.txt
get https://raw.githubusercontent.com/disconnectme/disconnect-tracking-protection/master/services.json $I/disconnect.txt
python3 -I $HERE/analyze.py "$C/real.tsv" "$C/zeph_decisions.txt" "$C/brave_decisions.txt" $I
echo; echo "== a crude 'looks like tracking' shape test on what the lists allow (a negative result, kept for the record)"
python3 -I $HERE/shape_heuristic.py "$C/real.tsv" "$C/zeph_decisions.txt" $I
echo; echo "== rule-by-rule parser parity (Zephyrus vs Brave)"
python3 -I - $L > $B/lines.tsv <<'PY'
import sys
L = sys.argv[1] + '/'; seen = set()
for n in ['easylist','easyprivacy','filters-general','filters-2020','filters-2021','filters-2022','filters-2023','filters-2024','filters-2025','filters-2026','privacy','quick-fixes','badware','unbreak','annoyances-cookies','cookiemonster','indianlist']:
    for l in open(L + n + '.txt', encoding='utf-8', errors='replace'):
        s = l.rstrip('\n').strip()
        if not s or s[0] in '![' or s.startswith('!#') or s in seen: continue
        seen.add(s); print(n + '\t' + s)
PY
$CXX -o $B/parity_zeph $HERE/parity_zeph.cc $ADB/adblock_filter_engine.cc $ADB/adblock_list_util.cc && $B/parity_zeph $B/lines.tsv > $B/zeph_parity.txt
(cd $B && mkdir -p parity && cp lines.tsv parity/lines.tsv && ./cargo_target/release/parity && cp parity/brave.txt brave_parity.txt)
python3 -I $HERE/parity_report.py $B/lines.tsv $B/zeph_parity.txt $B/brave_parity.txt
