#!/bin/sh
# Sprawdza, że programy K1 mają zabezpieczenia wymagane przez SPEC §11 (ADR 0002):
# statyczne PIE (bez interpretera i bibliotek), pełne RELRO (BIND_NOW), stos niewykonywalny
# i brak segmentów jednocześnie zapisywalnych i wykonywalnych. Ochronę stosu (kanarki) sprawdzono
# jednorazowo deasemblacją (ADR 0002) — w binariach bez symboli nie da się jej rzetelnie wykryć.
# Użycie: sprawdz-binaria.sh PLIK...
set -eu
zle=0
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
    if [ -n "$bledy" ]; then echo "  $f:$bledy"; zle=1; else echo "  $f: OK (static-pie, RELRO, BIND_NOW, NX)"; fi
done
[ "$zle" -eq 0 ] || { echo "[k1] BŁĄD: binaria bez wymaganych zabezpieczeń" >&2; exit 1; }
