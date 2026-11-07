#!/bin/sh
# Wszystkie testy K1 po kolei (to uruchamia CI). Kod wyjścia 0 = wszystko zaliczone.
. "$(dirname "$0")/wspolne.sh"
r=0
"$K1_REPO/scripts/test-m1.sh" || r=1
echo "[k1] test M2: ekran i wejście"
"$K1_REPO/scripts/test-m2.sh" || r=1
"$K1_REPO/scripts/test-m4.sh" || r=1
"$K1_REPO/scripts/test-m5.sh" || r=1
"$K1_REPO/scripts/test-m5f.sh" || r=1
[ $r -eq 0 ] && info "wszystkie testy zaliczone" || echo "[k1] BŁĄD: część testów nie przeszła"
exit $r
