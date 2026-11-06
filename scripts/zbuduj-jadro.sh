#!/bin/sh
# Pobiera (z weryfikacją), konfiguruje i buduje jądro Linux dla K1.
# Wynik: build/wynik/bzImage, build/wynik/jadro.config, build/naglowki (nagłówki UAPI dla programów K1),
#        build/jadro-obj/usr/gen_init_cpio (narzędzie do budowy initramfs).
. "$(dirname "$0")/wspolne.sh"

PLIK=linux-$K1_JADRO_WERSJA.tar.xz
POBRANE=$K1_BUDOWA/pobrane
mkdir -p "$POBRANE" "$K1_WYNIK" "$K1_BUDOWA/zrodla"

if [ ! -d "$K1_JADRO_ZRODLA" ]; then
    if [ ! -f "$POBRANE/$PLIK" ]; then
        info "pobieram $PLIK"
        curl -fsSL --retry 3 -o "$POBRANE/$PLIK.tmp" "$K1_JADRO_URL/$PLIK"
        curl -fsSL --retry 3 -o "$POBRANE/linux-$K1_JADRO_WERSJA.tar.sign" "$K1_JADRO_URL/linux-$K1_JADRO_WERSJA.tar.sign"
        mv "$POBRANE/$PLIK.tmp" "$POBRANE/$PLIK"
    fi
    info "sprawdzam sumę SHA-256 (przypiętą w jadro/wersja.env)"
    echo "$K1_JADRO_SHA256  $POBRANE/$PLIK" | sha256sum -c --quiet - || blad "zła suma kontrolna $PLIK"
    if [ -f "$POBRANE/linux-$K1_JADRO_WERSJA.tar.sign" ] && command -v gpg >/dev/null; then
        info "sprawdzam podpis PGP wydania (klucz w jadro/klucze/)"
        GNUPGHOME=$(mktemp -d); export GNUPGHOME
        gpg -q --import "$K1_REPO"/jadro/klucze/*.asc 2>/dev/null
        xz -dc "$POBRANE/$PLIK" | gpg -q --status-fd 1 --verify "$POBRANE/linux-$K1_JADRO_WERSJA.tar.sign" - 2>/dev/null \
            | grep -q '^\[GNUPG:\] VALIDSIG 647F28654894E3BD457199BE38DBBDC86092693E ' || blad "zły podpis PGP"
        rm -rf "$GNUPGHOME"; unset GNUPGHOME
    fi
    info "rozpakowuję źródła"
    tar -C "$K1_BUDOWA/zrodla" -xf "$POBRANE/$PLIK"
fi

export KBUILD_BUILD_USER=k1 KBUILD_BUILD_HOST=k1-budowa KBUILD_BUILD_VERSION=1
KBUILD_BUILD_TIMESTAMP=$(date -u -d "@$SOURCE_DATE_EPOCH" '+%Y-%m-%d %H:%M:%S UTC'); export KBUILD_BUILD_TIMESTAMP
# Wtyczka latent_entropy bez -frandom-seed losuje stałe przy każdym buildzie
# (scripts/gcc-plugins/latent_entropy_plugin.c: get_random_seed) — stałe ziarno = powtarzalny build.
M="make -s -C $K1_JADRO_ZRODLA O=$K1_JADRO_OBJ ARCH=x86_64 KCFLAGS=-frandom-seed=k1-$K1_JADRO_WERSJA"

info "konfiguracja: allnoconfig + jadro/k1.config"
mkdir -p "$K1_JADRO_OBJ"
$M KCONFIG_ALLCONFIG="$K1_REPO/jadro/k1.config" allnoconfig >/dev/null
"$K1_REPO/scripts/sprawdz-konfiguracje-jadra.sh" "$K1_REPO/jadro/k1.config" "$K1_JADRO_OBJ/.config"

info "kompilacja jądra ($K1_CPU wątków)"
$M -j"$K1_CPU" bzImage
$M headers_install INSTALL_HDR_PATH="$K1_NAGLOWKI" >/dev/null
[ -x "$K1_JADRO_OBJ/usr/gen_init_cpio" ] || $M usr/gen_init_cpio >/dev/null

cp "$K1_JADRO_OBJ/arch/x86/boot/bzImage" "$K1_WYNIK/bzImage"
cp "$K1_JADRO_OBJ/.config" "$K1_WYNIK/jadro.config"
info "gotowe: $K1_WYNIK/bzImage ($(du -k "$K1_WYNIK/bzImage" | cut -f1) KiB)"
