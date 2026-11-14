#!/bin/sh
# Pobiera (z weryfikacją), konfiguruje i buduje jądro Linux dla K1.
# Wynik: build/wynik/bzImage, build/wynik/jadro.config, build/wynik/strefa-jadro i strefa-jadro.config (jądro
#        strefy, 0.10.0, ADR 0042: te same źródła, fragment jadro/strefa.config), build/naglowki (nagłówki UAPI dla programów K1),
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


# Wi-Fi (0.9.1, ADR 0041): the vendor files and the radio rules built into the kernel — the fragment gets
# CONFIG_EXTRA_FIRMWARE and its directory from the table jadro/oprogramowanie-kart.txt.
"$K1_REPO/scripts/oprogramowanie-kart.sh"
FRAGMENT=$K1_BUDOWA/jadro-fragment.config
cat "$K1_REPO/jadro/k1.config" "$K1_BUDOWA/pliki-kart.config" >"$FRAGMENT"

info "konfiguracja: allnoconfig + jadro/k1.config + pliki od producentów kart"
mkdir -p "$K1_JADRO_OBJ"
make_jadra KCONFIG_ALLCONFIG="$FRAGMENT" allnoconfig >/dev/null
"$K1_REPO/scripts/sprawdz-konfiguracje-jadra.sh" "$FRAGMENT" "$K1_JADRO_OBJ/.config"

info "kompilacja jądra ($K1_CPU wątków)"
make_jadra -j"$K1_CPU" bzImage
make_jadra headers_install INSTALL_HDR_PATH="$K1_NAGLOWKI" >/dev/null
[ -x "$K1_JADRO_OBJ/usr/gen_init_cpio" ] || make_jadra usr/gen_init_cpio >/dev/null

cp "$K1_JADRO_OBJ/arch/x86/boot/bzImage" "$K1_WYNIK/bzImage"
cp "$K1_JADRO_OBJ/.config" "$K1_WYNIK/jadro.config"
info "gotowe: $K1_WYNIK/bzImage ($(du -k "$K1_WYNIK/bzImage" | cut -f1) KiB)"

# 0.10.0 (ADR 0042): the zone kernel — runs inside a micro machine, started by the VMM strefa; the same sources
info "jądro strefy: allnoconfig + jadro/strefa.config"
mkdir -p "$K1_STREFA_OBJ"
make_jadra_w "$K1_STREFA_OBJ" KCONFIG_ALLCONFIG="$K1_REPO/jadro/strefa.config" allnoconfig >/dev/null
"$K1_REPO/scripts/sprawdz-konfiguracje-jadra.sh" "$K1_REPO/jadro/strefa.config" "$K1_STREFA_OBJ/.config"
info "kompilacja jądra strefy ($K1_CPU wątków)"
make_jadra_w "$K1_STREFA_OBJ" -j"$K1_CPU" bzImage
cp "$K1_STREFA_OBJ/arch/x86/boot/bzImage" "$K1_WYNIK/strefa-jadro"
cp "$K1_STREFA_OBJ/.config" "$K1_WYNIK/strefa-jadro.config"
info "gotowe: $K1_WYNIK/strefa-jadro ($(du -k "$K1_WYNIK/strefa-jadro" | cut -f1) KiB)"
