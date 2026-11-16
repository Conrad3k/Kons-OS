#!/bin/sh
# Pliki od producentów kart Wi-Fi i baza przepisów radiowych dla jądra K1 (0.9.1, ADR 0041; decyzja D3).
# Czyta jadro/oprogramowanie-kart.txt (nazwa, ścieżka w linux-firmware, SHA-256), pobiera każdy plik raz do
# build/pobrane/pliki-kart/<wersja>/, sprawdza sumę i składa katalog build/pliki-kart/ (EXTRA_FIRMWARE_DIR) oraz fragment
# konfiguracji jądra build/pliki-kart.config (CONFIG_EXTRA_FIRMWARE, CONFIG_EXTRA_FIRMWARE_DIR). Zła suma = przerwanie.
# Wywołuje go scripts/zbuduj-jadro.sh.
. "$(dirname "$0")/wspolne.sh"

TABELA=$K1_REPO/jadro/oprogramowanie-kart.txt
POBRANE=$K1_BUDOWA/pobrane/pliki-kart/$K1_PLIKI_KART_WERSJA
KATALOG=$K1_BUDOWA/pliki-kart
FRAGMENT=$K1_BUDOWA/pliki-kart.config
mkdir -p "$POBRANE"
rm -rf "$KATALOG"
mkdir -p "$KATALOG"

# One file: downloaded once, checked every time. When kernel.org fails once, the rest come from the mirror straight
# away: run 108 (PR #123) spent over 40 minutes trying an unreachable git.kernel.org four times for each of 33 files.
GLOWNE_PADLO=
pobierz() { # <name> <path in linux-firmware> <sha256>
    cel=$POBRANE/$1
    if [ ! -f "$cel" ]; then
        mkdir -p "$(dirname "$cel")"
        if [ -n "$GLOWNE_PADLO" ] ||
            ! curl -fsSL --retry 3 --connect-timeout 30 --speed-limit 2048 --speed-time 60 --max-time 300 -o "$cel.tmp" "$K1_PLIKI_KART_URL/$2?h=$K1_PLIKI_KART_WERSJA"; then
            [ -n "$GLOWNE_PADLO" ] || info "git.kernel.org nie odpowiada — pliki kart z zapasu (gitlab.com)"
            GLOWNE_PADLO=1
            curl -fsSL --retry 3 --connect-timeout 30 --speed-limit 2048 --speed-time 60 --max-time 300 -o "$cel.tmp" "$K1_PLIKI_KART_ZAPAS/$K1_PLIKI_KART_WERSJA/$2" || blad "nie pobrano $2"
        fi
        mv "$cel.tmp" "$cel"
    fi
    echo "$3  $cel" | sha256sum -c --quiet - || { rm -f "$cel"; blad "zła suma kontrolna $1"; }
    mkdir -p "$(dirname "$KATALOG/$1")"
    cp "$cel" "$KATALOG/$1"
}

NAZWY=
n=0
while read -r nazwa sciezka suma reszta; do
    case $nazwa in '' | '#'*) continue ;; esac
    [ -n "$suma" ] && [ -z "$reszta" ] || blad "jadro/oprogramowanie-kart.txt: zły wiersz „$nazwa”"
    pobierz "$nazwa" "$sciezka" "$suma"
    NAZWY="$NAZWY $nazwa"
    n=$((n + 1))
done <"$TABELA"

ARCH=wireless-regdb-$K1_REGDB_WERSJA.tar.xz
if [ ! -f "$POBRANE/$ARCH" ]; then
    curl -fsSL --retry 3 --connect-timeout 30 --speed-limit 2048 --speed-time 60 --max-time 300 -o "$POBRANE/$ARCH.tmp" "$K1_REGDB_URL/$ARCH" || blad "nie pobrano $ARCH"
    mv "$POBRANE/$ARCH.tmp" "$POBRANE/$ARCH"
fi
echo "$K1_REGDB_SHA256  $POBRANE/$ARCH" | sha256sum -c --quiet - || { rm -f "$POBRANE/$ARCH"; blad "zła suma $ARCH"; }
tar -C "$KATALOG" -xJf "$POBRANE/$ARCH" --strip-components=1 \
    "wireless-regdb-$K1_REGDB_WERSJA/regulatory.db" "wireless-regdb-$K1_REGDB_WERSJA/regulatory.db.p7s"
touch -d "@$SOURCE_DATE_EPOCH" "$KATALOG/regulatory.db" "$KATALOG/regulatory.db.p7s"
NAZWY="${NAZWY# } regulatory.db regulatory.db.p7s"

printf 'CONFIG_EXTRA_FIRMWARE="%s"\nCONFIG_EXTRA_FIRMWARE_DIR="%s"\n' "$NAZWY" "$KATALOG" >"$FRAGMENT"
info "pliki od producentów kart: $n z linux-firmware $K1_PLIKI_KART_WERSJA + wireless-regdb $K1_REGDB_WERSJA ($(du -sk "$KATALOG" | cut -f1) KiB)"
