#!/bin/sh
# Zrzuty wzorcowe (0.10.2 krok 3): przyjmuje nowy wygląd — obrazy „po” z ostatniego testu (build/testy/wzorce/*-po.png,
# albo z podanego katalogu, np. rozpakowanego artefaktu z logami CI) stają się wzorcami w testy/wzorce/.
# TYLKO po akceptacji właściciela (decyzja D3): PR, który zmienia wygląd, pokazuje mu obrazy „przed / po”, a ten skrypt
# uruchamia się dopiero po jego „OK”. Użycie: scripts/przyjmij-wzorce.sh [KATALOG] [NAZWA…] (bez nazw: wszystkie).
. "$(dirname "$0")/wspolne.sh"
ZRODLO=$K1_BUDOWA/testy/wzorce
if [ $# -gt 0 ] && [ -d "$1" ]; then ZRODLO=$1; shift; fi
CEL=$K1_REPO/testy/wzorce
mkdir -p "$CEL"
n=0
for f in "$ZRODLO"/*-po.png; do
    [ -f "$f" ] || continue
    nazwa=$(basename "$f" -po.png)
    if [ $# -gt 0 ]; then
        case " $* " in *" $nazwa "*) ;; *) continue ;; esac
    fi
    cp "$f" "$CEL/$nazwa.png"
    info "wzorzec $nazwa przyjęty"
    n=$((n + 1))
done
[ $n -gt 0 ] || { echo "[k1] brak obrazów „po” w $ZRODLO"; exit 1; }
info "przyjęto $n — sprawdź git diff testy/wzorce/ i dopisz zmianę do opisu PR-a"
