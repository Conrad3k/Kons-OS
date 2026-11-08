#!/bin/sh
# Składa zamknięte jądro K1 (M6d, ADR 0022): build/wynik/k1.efi — jądro z wbudowaną linią poleceń i initramfs
# z obrazem rdzenia, podpisane do Secure Boot kluczem K1 (narzedzia/klucz_sb.py: z sekretu K1_KLUCZ_PODPISU,
# bez niego — publiczny klucz deweloperski). Ten jeden plik uruchamia GRUB z pendrive'a, z dysku i z ISO.
# Buduje w tym samym katalogu co jądro deweloperskie (scripts/zbuduj-jadro.sh): zmienia się kilka plików.
# Wymaga: scripts/zbuduj-jadro.sh i scripts/zbuduj-obraz.sh.
. "$(dirname "$0")/wspolne.sh"

SB=$K1_BUDOWA/gen/sb
[ -f "$K1_WYNIK/rdzen.squashfs.skrot" ] && [ -f "$K1_BUDOWA/initramfs.lista" ] && [ -f "$SB/k1-sb.crt" ] \
    || blad "najpierw zbuduj system: scripts/zbuduj.sh"
for n in sbsign sbverify faketime; do command -v $n >/dev/null || blad "brak $n (scripts/przygotuj-srodowisko.sh)"; done

info "k1.efi: zamknięte jądro (linia poleceń i rdzeń w środku)"
LISTA=$K1_BUDOWA/k1efi.lista
{ cat "$K1_BUDOWA/initramfs.lista"; printf 'file /rdzen.squashfs %s 0400 0 0\n' "$K1_WYNIK/rdzen.squashfs"; } > "$LISTA"
# the same line as before on the stick and in the ISO, plus noinitrd (see jadro/k1-zamkniete.config)
LINIA="$(sed 's/k1\.rdzen=[^ ]*/k1.rdzen=wbudowany/' "$K1_REPO/jadro/cmdline") k1.rdzen_skrot=$(cat "$K1_WYNIK/rdzen.squashfs.skrot") noinitrd quiet loglevel=7"
FRAG=$K1_BUDOWA/k1efi.config
# k1.config without the options this fragment sets (both must hold exactly what is checked below)
ZMIENIANE=$(sed -n 's/^\(CONFIG_[A-Z0-9_]*\)=.*/\1/p' "$K1_REPO/jadro/k1-zamkniete.config" | paste -sd'|')
{ grep -Ev "^($ZMIENIANE)=|^# ($ZMIENIANE) is not set" "$K1_REPO/jadro/k1.config"
  sed "s|@LINIA@|$LINIA|; s|@LISTA@|$LISTA|" "$K1_REPO/jadro/k1-zamkniete.config"; } > "$FRAG"

export KBUILD_BUILD_USER=k1 KBUILD_BUILD_HOST=k1-budowa KBUILD_BUILD_VERSION=1
KBUILD_BUILD_TIMESTAMP=$(date -u -d "@$SOURCE_DATE_EPOCH" '+%Y-%m-%d %H:%M:%S UTC'); export KBUILD_BUILD_TIMESTAMP
M="make -s -C $K1_JADRO_ZRODLA O=$K1_JADRO_OBJ ARCH=x86_64 KCFLAGS=-frandom-seed=k1-$K1_JADRO_WERSJA"
$M KCONFIG_ALLCONFIG="$FRAG" allnoconfig >/dev/null
"$K1_REPO/scripts/sprawdz-konfiguracje-jadra.sh" "$FRAG" "$K1_JADRO_OBJ/.config"
$M -j"$K1_CPU" bzImage
cp "$K1_JADRO_OBJ/arch/x86/boot/bzImage" "$K1_WYNIK/k1.efi.niepodpisany"

# Signature for Secure Boot (shim → GRUB → k1.efi): Authenticode, RSA-2048. A frozen clock makes the
# signature (its signing time) the same in every build of the same sources.
[ -f "$SB/k1-sb.key" ] || blad "brak klucza Secure Boot (narzedzia/klucz_sb.py — make)"
faketime "$(date -u -d "@$SOURCE_DATE_EPOCH" '+%Y-%m-%d %H:%M:%S')" \
    sbsign --key "$SB/k1-sb.key" --cert "$SB/k1-sb.crt" --output "$K1_WYNIK/k1.efi" "$K1_WYNIK/k1.efi.niepodpisany" \
    >/dev/null 2>&1 || blad "sbsign k1.efi"
rm -f "$SB/k1-sb.key" # never left lying around (regenerated from the secret by the next build)
sbverify --cert "$SB/k1-sb.crt" "$K1_WYNIK/k1.efi" >/dev/null 2>&1 || blad "podpis k1.efi nie przechodzi sprawdzenia"
cp "$SB/k1-sb.cer" "$K1_WYNIK/k1-sb.cer"
(cd "$K1_WYNIK" && sed -i '/  k1\.efi$\|  k1-sb\.cer$/d' SHA256SUMS && sha256sum k1.efi k1-sb.cer >> SHA256SUMS)
info "gotowe: $K1_WYNIK/k1.efi ($(du -k "$K1_WYNIK/k1.efi" | cut -f1) KiB, $(grep -q 'K1_SB_PRAWDZIWY 1' "$SB/k1_sb_cert.h" && echo 'klucz wydania' || echo 'klucz deweloperski'))"
