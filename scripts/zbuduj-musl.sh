#!/bin/sh
# Pobiera (z weryfikacją), łata i buduje bibliotekę C musl dla programów K1 (0.9.0 krok 4, korekta ADR 0007).
# Wynik: build/musl/ — include/, lib/ (libc.a, crt), lib/musl-gcc.specs i bin/musl-gcc (kompilator programów K1:
# CC_K1 w Makefile). Wersja, suma i źródła: musl/wersja.env; łatki: musl/latki/ (musl/README.md).
# Przebudowuje tylko, gdy zmieniła się wersja, łatka albo ten skrypt (znacznik build/musl/.k1-wejscie).
. "$(dirname "$0")/wspolne.sh"
. "$K1_REPO/musl/wersja.env"

POBRANE=$K1_BUDOWA/pobrane
PLIK=musl-$K1_MUSL_WERSJA.tar.gz
ZRODLA=$K1_BUDOWA/zrodla/musl-$K1_MUSL_WERSJA
CEL=$K1_BUDOWA/musl
mkdir -p "$POBRANE" "$K1_BUDOWA/zrodla"

wejscie=$(cat "$K1_REPO/musl/wersja.env" "$K1_REPO"/musl/latki/*.patch "$0" | sha256sum | cut -c1-64)
if [ -x "$CEL/bin/musl-gcc" ] && [ "$(cat "$CEL/.k1-wejscie" 2>/dev/null)" = "$wejscie" ]; then
    touch "$CEL/bin/musl-gcc" # make: up to date with musl/ (a fresh checkout has newer files)
    exit 0
fi

if [ ! -f "$POBRANE/$PLIK" ] || ! echo "$K1_MUSL_SHA256  $POBRANE/$PLIK" | sha256sum -c --quiet - 2>/dev/null; then
    rm -f "$POBRANE/$PLIK" "$POBRANE/$PLIK.asc"
    for url in $K1_MUSL_ZRODLA; do
        info "pobieram musl $K1_MUSL_WERSJA: $url"
        if curl -fsSL --retry 3 --connect-timeout 30 --speed-limit 2048 --speed-time 60 -o "$POBRANE/$PLIK.tmp" "$url" && curl -fsSL --retry 3 --connect-timeout 30 --speed-limit 2048 --speed-time 60 -o "$POBRANE/$PLIK.asc" "$url.asc" &&
            echo "$K1_MUSL_SHA256  $POBRANE/$PLIK.tmp" | sha256sum -c --quiet -; then
            mv "$POBRANE/$PLIK.tmp" "$POBRANE/$PLIK"
            break
        fi
        echo "[uwaga] $url: nie pobrano albo zła suma — następne źródło"
        rm -f "$POBRANE/$PLIK.tmp" "$POBRANE/$PLIK.asc"
    done
fi
[ -f "$POBRANE/$PLIK" ] || blad "nie udało się pobrać $PLIK (musl/wersja.env: K1_MUSL_ZRODLA)"
info "sprawdzam sumę SHA-256 (przypiętą w musl/wersja.env) i podpis PGP wydania (klucz w musl/klucze/)"
echo "$K1_MUSL_SHA256  $POBRANE/$PLIK" | sha256sum -c --quiet - || blad "zła suma kontrolna $PLIK"
command -v gpg >/dev/null || blad "brak gpg (scripts/przygotuj-srodowisko.sh)"
GNUPGHOME=$(mktemp -d); export GNUPGHOME
gpg -q --import "$K1_REPO"/musl/klucze/*.asc 2>/dev/null
gpg -q --status-fd 1 --verify "$POBRANE/$PLIK.asc" "$POBRANE/$PLIK" 2>/dev/null \
    | grep -q "^\[GNUPG:\] VALIDSIG $K1_MUSL_ODCISK " || blad "zły podpis PGP $PLIK"
rm -rf "$GNUPGHOME"; unset GNUPGHOME

info "musl $K1_MUSL_WERSJA: rozpakowuję i nakładam łatki ($(ls "$K1_REPO"/musl/latki/*.patch | wc -l))"
rm -rf "$ZRODLA" "$CEL"
tar -C "$K1_BUDOWA/zrodla" -xzf "$POBRANE/$PLIK"
for p in "$K1_REPO"/musl/latki/*.patch; do
    patch -s -d "$ZRODLA" -p1 < "$p" || blad "łatka $(basename "$p") nie pasuje"
done

info "musl $K1_MUSL_WERSJA: kompilacja ($K1_CPU wątków)"
# Only the static library: K1 has no shared libraries (SPEC §11). The paths of the build do not go into the objects.
(cd "$ZRODLA" && ./configure --prefix="$CEL" --disable-shared --enable-wrapper=gcc CC=gcc \
    CFLAGS="-O2 -ffile-prefix-map=$ZRODLA=musl-$K1_MUSL_WERSJA" > "$K1_BUDOWA/musl-konfiguracja.log" 2>&1) \
    || { cat "$K1_BUDOWA/musl-konfiguracja.log"; blad "musl: configure"; }
make -s -C "$ZRODLA" -j"$K1_CPU" > "$K1_BUDOWA/musl-kompilacja.log" 2>&1 || { tail -30 "$K1_BUDOWA/musl-kompilacja.log"; blad "musl: make"; }
make -s -C "$ZRODLA" install > /dev/null || blad "musl: make install"
[ -x "$CEL/bin/musl-gcc" ] && [ -f "$CEL/lib/libc.a" ] && [ -f "$CEL/lib/rcrt1.o" ] || blad "musl: brak musl-gcc, libc.a albo rcrt1.o"
echo "$wejscie" > "$CEL/.k1-wejscie"
info "gotowe: musl $K1_MUSL_WERSJA w $CEL"
