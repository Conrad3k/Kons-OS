#!/bin/sh
# Sprawdza, że programy K1 mają zabezpieczenia wymagane przez SPEC §11 (ADR 0002):
# statyczne PIE (bez interpretera i bibliotek), pełne RELRO (BIND_NOW), stos niewykonywalny
# i brak segmentów jednocześnie zapisywalnych i wykonywalnych. Ochronę stosu (kanarki) sprawdzono
# jednorazowo deasemblacją (ADR 0002) — w binariach bez symboli nie da się jej rzetelnie wykryć.
# _FORTIFY_SOURCE (0.9.0 krok 4, fortify-headers): w każdym programie są pułapki kontroli przepełnień (instrukcja ud2 —
# bez fortify K1 nie ma ich wcale), a próbka zbudowana z tymi samymi flagami (--fortify PROBA) przy przepełnieniu
# memcpy kończy się sygnałem SIGILL, a w granicach bufora działa.
# Użycie: sprawdz-binaria.sh [--fortify PROBA] PLIK...
set -eu
zle=0
if [ "${1:-}" = --fortify ]; then
    proba=$2; shift 2
    if "$proba" 0 && { ("$proba" 1; exit $?) 2>/dev/null; [ $? -eq 132 ]; }; then
        echo "  $proba: OK (fortify: memcpy w granicach działa, poza bufor — SIGILL)"
    else
        echo "  $proba: fortify nie zatrzymał przepełnienia memcpy"; zle=1
    fi
fi
for f in "$@"; do
    nag=$(readelf -hW "$f"); seg=$(readelf -lW "$f"); dyn=$(readelf -dW "$f")
    bledy=""
    echo "$nag" | grep -q 'Type:.*DYN' || bledy="$bledy nie-PIE"
    echo "$seg" | grep -q 'INTERP' && bledy="$bledy ma-interpreter"
    echo "$dyn" | grep -q 'NEEDED' && bledy="$bledy biblioteki-dynamiczne"
    echo "$seg" | grep -q 'GNU_RELRO' || bledy="$bledy brak-RELRO"
    echo "$dyn" | grep -q 'BIND_NOW\|FLAGS_1.*NOW' || bledy="$bledy brak-BIND_NOW"
    echo "$seg" | grep 'GNU_STACK' | grep -q 'RW ' || bledy="$bledy stos-wykonywalny"
    echo "$seg" | grep 'LOAD' | grep -q 'RWE' && bledy="$bledy segment-RWX"
    pulapki=$(objdump -d "$f" | grep -c 'ud2' || true)
    [ "$pulapki" -gt 0 ] || bledy="$bledy brak-fortify"
    if [ -n "$bledy" ]; then echo "  $f:$bledy"; zle=1; else echo "  $f: OK (static-pie, RELRO, BIND_NOW, NX, fortify: $pulapki pułapek)"; fi
done
[ "$zle" -eq 0 ] || { echo "[k1] BŁĄD: binaria bez wymaganych zabezpieczeń" >&2; exit 1; }
