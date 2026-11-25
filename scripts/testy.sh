#!/bin/sh
# Wszystkie testy K1 (to uruchamia CI). Kod wyjścia 0 = wszystko zaliczone.
# Najpierw test-m1 (host: ASan/UBSan, fuzzing; obraz testowy w QEMU), potem testy pulpitu, sprzętu
# i instalatora. Te ostatnie są od siebie niezależne (osobne logi, gniazda i obrazy dysków): z KVM
# (CI) idą równolegle, po tyle naraz, ile procesorów (0.11.1), bez KVM (emulacja zajmuje cały procesor) — po kolei. Ręcznie:
#   K1_TESTY_ROWNOLEGLE=1 scripts/testy.sh    (wymuś równolegle)   =0 (wymuś po kolei)   K1_TESTY_NARAZ=N (ile naraz)
#   K1_TESTY_TYLKO="test-strefa" scripts/testy.sh   (tylko te, bez test-m1 — szukanie błędu; przed scaleniem zawsze
#   pełny komplet)
# Przeglądarki (Firefox w strefie) nie testuje żaden test QEMU — od 0.10.1 sprawdza ją właściciel na sprzęcie (jego
# decyzja z 2026-11-17, AGENTS.md); jej części K1 — test-strefa i testy na hoście, Firefox na hoście — scripts/stanowisko.sh.
. "$(dirname "$0")/wspolne.sh"
r=0
[ -n "${K1_TESTY_TYLKO:-}" ] || "$K1_REPO/scripts/test-m1.sh" || r=1

ROWNOLEGLE=${K1_TESTY_ROWNOLEGLE:-}
if [ -z "$ROWNOLEGLE" ]; then
    if [ -w /dev/kvm ]; then ROWNOLEGLE=1; else ROWNOLEGLE=0; fi
fi
# The longest first (CI run 310: test-aktualizacja-siec 16 min, test-most, test-strefa 8 min…): with a few at a time the
# whole set ends soonest that way.
TESTY="test-aktualizacja-siec test-most test-strefa test-pobrane test-pliki test-m4 test-wzorce test-dzwiek test-pendrive test-m5f test-m6 test-wifi test-skarbiec test-siec test-m5"
[ -z "${K1_TESTY_TYLKO:-}" ] || TESTY=$K1_TESTY_TYLKO
# How many at once: as many as the processors (each QEMU has 2 of them, zones run nested inside): all 14 at once on
# the 4 processors of a GitHub runner (0.11.1, PR #207) slowed everything down sevenfold — zones missed their times,
# and twice the runner itself went down without a log (exit 143). K1_TESTY_NARAZ=N to choose.
NARAZ=${K1_TESTY_NARAZ:-$(nproc 2>/dev/null || echo 2)}
[ "$NARAZ" -ge 1 ] 2>/dev/null || NARAZ=1
WYJ=$K1_BUDOWA/testy/rownolegle
mkdir -p "$WYJ"
zyje() { # the test's process still runs (not gone, not a zombie waiting to be collected)
    s=$(cut -d' ' -f3 "/proc/$1/stat" 2>/dev/null) && [ "$s" != Z ]
}
if [ "$ROWNOLEGLE" = 1 ]; then
    info "testy pulpitu, sprzętu i instalatora — po $NARAZ naraz ($TESTY)"
    czeka=$TESTY biegnie= od=$(date +%s) znak=$(date +%s)
    while [ -n "$czeka" ] || [ -n "$biegnie" ]; do
        # start new ones while there is room
        while [ -n "$czeka" ] && [ $(echo $biegnie | wc -w) -lt "$NARAZ" ]; do
            t=${czeka%% *}
            [ "$t" = "$czeka" ] && czeka= || czeka=${czeka#* }
            "$K1_REPO/scripts/$t.sh" > "$WYJ/$t.txt" 2>&1 &
            biegnie="$biegnie $t:$!"
        done
        sleep 2
        # collect the ended ones: their result and output now
        dalej=
        for tp in $biegnie; do
            t=${tp%%:*} p=${tp#*:}
            if zyje "$p"; then
                dalej="$dalej $tp"
            else
                wait "$p" || r=1
                cat "$WYJ/$t.txt"
            fi
        done
        biegnie=$dalej
        # A sign of life every minute (live on the CI run's page): which tests run and which wait (CI run 125)
        if [ $(( $(date +%s) - znak )) -ge 60 ] && [ -n "$biegnie" ]; then
            znak=$(date +%s)
            trwa=
            for tp in $biegnie; do trwa="$trwa ${tp%%:*}"; done
            echo "[k1] trwa ($(( (znak - od) / 60 )) min):$trwa${czeka:+ (czekają: $(echo $czeka | wc -w))}"
        fi
    done
else
    for t in $TESTY; do
        "$K1_REPO/scripts/$t.sh" || r=1
    done
fi
[ $r -eq 0 ] && info "wszystkie testy zaliczone" || echo "[k1] BŁĄD: część testów nie przeszła"
exit $r
