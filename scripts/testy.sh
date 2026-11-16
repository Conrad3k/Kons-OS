#!/bin/sh
# Wszystkie testy K1 (to uruchamia CI). Kod wyjścia 0 = wszystko zaliczone.
# Najpierw test-m1 (host: ASan/UBSan, fuzzing; obraz testowy w QEMU), potem testy pulpitu, sprzętu
# i instalatora. Te ostatnie są od siebie niezależne (osobne logi, gniazda i obrazy dysków): z KVM
# (CI) idą równolegle, bez KVM (emulacja zajmuje cały procesor) — po kolei. Ręcznie:
#   K1_TESTY_ROWNOLEGLE=1 scripts/testy.sh    (wymuś równolegle)   =0 (wymuś po kolei)
#   K1_TESTY_TYLKO="test-przegladarka" scripts/testy.sh   (tylko te, bez test-m1 — szukanie błędu; CI: etykieta
#   „testy-przegladarka”; przed scaleniem zawsze pełny komplet)
. "$(dirname "$0")/wspolne.sh"
r=0
[ -n "${K1_TESTY_TYLKO:-}" ] || "$K1_REPO/scripts/test-m1.sh" || r=1

ROWNOLEGLE=${K1_TESTY_ROWNOLEGLE:-}
if [ -z "$ROWNOLEGLE" ]; then
    if [ -w /dev/kvm ]; then ROWNOLEGLE=1; else ROWNOLEGLE=0; fi
fi
TESTY="test-m4 test-pliki test-pendrive test-skarbiec test-siec test-wifi test-strefa test-przegladarka test-m5 test-m5f test-m6"
[ -z "${K1_TESTY_TYLKO:-}" ] || TESTY=$K1_TESTY_TYLKO
WYJ=$K1_BUDOWA/testy/rownolegle
mkdir -p "$WYJ"
if [ "$ROWNOLEGLE" = 1 ]; then
    info "testy pulpitu, sprzętu i instalatora — równolegle ($TESTY)"
    pidy=
    for t in $TESTY; do
        "$K1_REPO/scripts/$t.sh" > "$WYJ/$t.txt" 2>&1 &
        pidy="$pidy $!"
    done
    # A sign of life every minute (live on the CI run's page): which tests still run and for how long. The output of
    # each test comes only when it ends, so without this a slow test looks like a hang (CI run 125).
    (
        od=$(date +%s)
        while sleep 60; do
            trwa=
            i=0
            for t in $TESTY; do
                i=$((i + 1))
                p=$(echo $pidy | cut -d' ' -f$i)
                # running: not gone and not a zombie (one that ended waits to be collected in the order below)
                s=$(cut -d' ' -f3 "/proc/$p/stat" 2>/dev/null) && [ "$s" != Z ] && trwa="$trwa $t"
            done
            [ -n "$trwa" ] || break
            echo "[k1] trwa ($(( ($(date +%s) - od) / 60 )) min):$trwa"
        done
    ) &
    zycie=$!
    i=0
    for t in $TESTY; do
        i=$((i + 1))
        p=$(echo $pidy | cut -d' ' -f$i)
        wait "$p" || r=1
        cat "$WYJ/$t.txt"
    done
    kill "$zycie" 2>/dev/null || true
    wait "$zycie" 2>/dev/null || true
else
    for t in $TESTY; do
        "$K1_REPO/scripts/$t.sh" || r=1
    done
fi
[ $r -eq 0 ] && info "wszystkie testy zaliczone" || echo "[k1] BŁĄD: część testów nie przeszła"
exit $r
