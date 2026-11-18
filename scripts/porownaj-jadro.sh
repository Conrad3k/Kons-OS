#!/bin/sh
# Porównanie konfiguracji jądra przy nowej linii (0.10.5 krok 3, ADR 0047, T19 — prośba właściciela z 2026-11-17).
# K1 buduje jądro z allnoconfig + krótki fragment, więc opcja, którą nowa linia wydzieli z czegoś, co działało samo
# („domyślnie tak”, jak KVM_IOAPIC w 6.18), nie wejdzie, bo fragmentu nikt nie zmienił. Ten skrypt to wykrywa:
#   A = nowe źródła: allnoconfig + fragment (to, co zbuduje K1);
#   B = nowe źródła: dotychczasowa pełna konfiguracja (.config z budowy) BEZ wierszy „is not set” + olddefconfig —
#       każda opcja dostaje to, co dałoby jej jądro samo (default … if …, przy naszych opcjach);
#   PYTANIE: opcja włączona w B, wyłączona w A, której nie ma we fragmencie ani na liście odrzuconych
#            (jadro/k1-odrzucone.txt, jadro/strefa-odrzucone.txt: „świadomie nie chcemy”, każda z powodem);
#   ZNIKNĘŁA: opcja włączona w dotychczasowej konfiguracji, której nowe źródła już nie znają (zniknęła albo ma nową nazwę).
# Każde pytanie rozstrzyga raz człowiek (fragment albo lista odrzuconych); robot (robot.yml) przy czymkolwiek z tej
# listy zakłada Issue zamiast PR-a. Te same źródła i ta sama konfiguracja po pierwszym wypełnieniu listy: 0 pytań.
#
#   scripts/porownaj-jadro.sh ZRODLA STARY_CONFIG FRAGMENT ODRZUCONE   — kod 0: nic do rozstrzygnięcia; 1: lista na
#                                                                        wyjściu („PYTANIE CONFIG_X — opis”, „ZNIKNĘŁA …”)
#   … --lista                                                          — same nazwy opcji „PYTANIE” (pierwsze wypełnienie)
set -eu
ZRODLA=$(realpath "$1") STARY=$(realpath "$2") FRAGMENT=$(realpath "$3") ODRZUCONE=$(realpath "$4") TRYB=${5:-}
[ -f "$ZRODLA/Makefile" ] && [ -f "$STARY" ] && [ -f "$FRAGMENT" ] || { echo "porownaj-jadro: złe argumenty" >&2; exit 2; }
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
mkdir -p "$T/a" "$T/b"
make -s -C "$ZRODLA" O="$T/a" ARCH=x86_64 KCONFIG_ALLCONFIG="$FRAGMENT" allnoconfig >/dev/null
grep -v ' is not set$' "$STARY" > "$T/b/.config"
make -s -C "$ZRODLA" O="$T/b" ARCH=x86_64 olddefconfig >/dev/null
wlaczone() { sed -n 's/^\(CONFIG_[A-Za-z0-9_]*\)=[ym]$/\1/p' "$1" | sort -u; }
znane() { sed -n 's/^\(CONFIG_[A-Za-z0-9_]*\)=.*/\1/p; s/^# \(CONFIG_[A-Za-z0-9_]*\) is not set$/\1/p' "$1" | sort -u; }
wlaczone "$T/a/.config" > "$T/A"
wlaczone "$T/b/.config" > "$T/B"
{ sed -n 's/^\(CONFIG_[A-Za-z0-9_]*\)=.*/\1/p; s/^# \(CONFIG_[A-Za-z0-9_]*\) is not set.*/\1/p' "$FRAGMENT"
  sed -n 's/^\(CONFIG_[A-Za-z0-9_]*\)[ \t].*/\1/p; s/^\(CONFIG_[A-Za-z0-9_]*\)$/\1/p' "$ODRZUCONE"; } | sort -u > "$T/rozstrzygniete"
# S51: only options with a question (a prompt in Kconfig) — the others are switched on by `select` of one that has one
find "$ZRODLA" -name 'Kconfig*' -exec awk '
    /^[ \t]*(menu)?config[ \t]+[A-Za-z0-9_]+/ { o = $2; next }
    o != "" && /^[ \t]*(bool|tristate|string|int|hex)[ \t]+"/ { print "CONFIG_" o; o = ""; next }
    o != "" && /^[ \t]*prompt[ \t]+"/ { print "CONFIG_" o; o = ""; next }
    /^[ \t]*(if|endif|menu|endmenu|choice|endchoice|source|comment)([ \t]|$)/ { o = "" }' {} + | sort -u > "$T/z_pytaniem"
comm -23 "$T/B" "$T/A" | comm -23 - "$T/rozstrzygniete" | comm -12 - "$T/z_pytaniem" > "$T/pytania"
{ znane "$T/a/.config"; znane "$T/b/.config"; } | sort -u > "$T/nowe"
wlaczone "$STARY" | comm -23 - "$T/nowe" > "$T/zniknely"
if [ "$TRYB" = --lista ]; then
    cat "$T/pytania"
    exit 0
fi
opis() { # the prompt of option $1 in the sources' Kconfig files (its first one)
    n=${1#CONFIG_}
    grep -rh -A3 --include='Kconfig*' -E "^(menu)?config $n\$" "$ZRODLA" 2>/dev/null |
        sed -n 's/^[ \t]*\(bool\|tristate\|prompt\)[ \t]*"\(.*\)".*/\2/p' | head -n 1
}
while read -r o; do echo "PYTANIE $o — $(opis "$o")"; done < "$T/pytania"
while read -r o; do echo "ZNIKNĘŁA $o (włączona dotąd; nowe źródła jej nie znają)"; done < "$T/zniknely"
if [ -s "$T/pytania" ] || [ -s "$T/zniknely" ]; then exit 1; fi
echo "porownaj-jadro: nic do rozstrzygnięcia ($(wc -l < "$T/A") opcji włączonych)"
