#!/bin/sh
# Składa zamknięte jądro K1 (M6d, ADR 0022): build/wynik/k1.efi — jądro z wbudowaną linią poleceń i initramfs
# z obrazem rdzenia, podpisane do Secure Boot kluczem K1 (narzedzia/klucz_sb.py: z sekretu K1_KLUCZ_PODPISU,
# bez niego — publiczny klucz deweloperski). Ten jeden plik uruchamia GRUB z pendrive'a, z dysku i z ISO.
# Od 0.13.0 krok 4 (ADR 0059) także build/wynik/k1-test.efi — to samo z logiem na porcie szeregowym, tylko do obrazów
# testów (k1-<wersja>-test.iso i -test-usb.img: scripts/zbuduj-iso.sh, zbuduj-usb.sh --test); wydanie go nie zawiera.
# Buduje w tym samym katalogu co jądro deweloperskie (scripts/zbuduj-jadro.sh): zmienia się kilka plików.
# Wymaga: scripts/zbuduj-jadro.sh i scripts/zbuduj-obraz.sh.
. "$(dirname "$0")/wspolne.sh"

SB=$K1_BUDOWA/gen/sb
[ -f "$K1_WYNIK/rdzen.squashfs.skrot" ] && [ -f "$K1_BUDOWA/initramfs.lista" ] && [ -f "$SB/k1-sb.crt" ] \
    || blad "najpierw zbuduj system: scripts/zbuduj.sh"
for n in sbsign sbverify faketime; do command -v $n >/dev/null || blad "brak $n (scripts/przygotuj-srodowisko.sh)"; done

LISTA=$K1_BUDOWA/k1efi.lista
{ cat "$K1_BUDOWA/initramfs.lista"; printf 'file /rdzen.squashfs %s 0400 0 0\n' "$K1_WYNIK/rdzen.squashfs"; } > "$LISTA"
# 0.13.0 step 4 (ADR 0059): the release's line (jadro/cmdline-wydanie: console=ttynull — nothing on the serial port) plus
# noinitrd (see jadro/k1-zamkniete.config); the test copy k1-test.efi (only in the tests' ISO and USB images, never
# released) adds console=ttyS0 panic=-1 at the end — the later ones win: the log on the serial port, no wait on a panic.
LINIA="$(cat "$K1_REPO/jadro/cmdline-wydanie") k1.rdzen_skrot=$(cat "$K1_WYNIK/rdzen.squashfs.skrot") noinitrd quiet loglevel=7"
case " $LINIA " in *" console=ttyS"*) blad "jadro/cmdline-wydanie: wydanie bez konsoli na porcie szeregowym (ADR 0059)" ;; esac
# SBAT (0.13.0 step 2, ADR 0057): the generation of K1 — the same number nadzorca keeps in the TPM
GENERACJA=$(sed -n 's/^#define K1_GENERACJA \([0-9][0-9]*\)$/\1/p' "$K1_REPO/src/wspolne/k1_tpm.h")
[ -n "$GENERACJA" ] || blad "brak K1_GENERACJA w src/wspolne/k1_tpm.h"
SBAT=$K1_BUDOWA/k1.sbat.csv
{ echo 'sbat,1,SBAT Version,sbat,1,https://github.com/rhboot/shim/blob/main/SBAT.md'
  echo "k1,$GENERACJA,Kons K1,k1,$K1_WERSJA,https://github.com/Conrad3k/Kons-OS"; } > "$SBAT"
FRAG=$K1_BUDOWA/k1efi.config
# k1.config with the vendor files (zbuduj-jadro.sh) without the options this fragment sets, on or off (both must hold
# exactly what is checked below)
ZMIENIANE=$(sed -n 's/^\(CONFIG_[A-Z0-9_]*\)=.*/\1/p; s/^# \(CONFIG_[A-Z0-9_]*\) is not set$/\1/p' \
    "$K1_REPO/jadro/k1-zamkniete.config" | paste -sd'|')
[ -f "$SB/k1-sb.key" ] || blad "brak klucza Secure Boot (narzedzia/klucz_sb.py — make)"

# zamkniete LINIA WYJSCIE — one closed kernel with this line, signed for Secure Boot (shim → GRUB → k1.efi): Authenticode,
# RSA-2048. A frozen clock makes the signature (its signing time) the same in every build of the same sources (-f: the
# clock stands still; without it, it starts at that time and runs — a second later in a slower build, 0.10.5 step 7).
# Only the line differs between the two: the second build recompiles what reads CONFIG_CMDLINE and links again.
zamkniete() {
    info "$(basename "$2"): zamknięte jądro (linia poleceń i rdzeń w środku)"
    { grep -Ev "^($ZMIENIANE)=|^# ($ZMIENIANE) is not set" "$K1_BUDOWA/jadro-fragment.config"
      sed "s|@LINIA@|$1|; s|@LISTA@|$LISTA|; s|@SBAT@|$SBAT|" "$K1_REPO/jadro/k1-zamkniete.config"; } > "$FRAG"
    make_jadra KCONFIG_ALLCONFIG="$FRAG" allnoconfig >/dev/null
    "$K1_REPO/scripts/sprawdz-konfiguracje-jadra.sh" "$FRAG" "$K1_JADRO_OBJ/.config"
    make_jadra -j"$K1_CPU" bzImage
    cp "$K1_JADRO_OBJ/arch/x86/boot/bzImage" "$2.niepodpisany"
    faketime -f "$(date -u -d "@$SOURCE_DATE_EPOCH" '+%Y-%m-%d %H:%M:%S')" \
        sbsign --key "$SB/k1-sb.key" --cert "$SB/k1-sb.crt" --output "$2" "$2.niepodpisany" \
        >/dev/null 2>&1 || blad "sbsign $(basename "$2")"
    sbverify --cert "$SB/k1-sb.crt" "$2" >/dev/null 2>&1 || blad "podpis $(basename "$2") nie przechodzi sprawdzenia"
}
# the test copy first: the kernel's build directory ends with the release's configuration (the CI cache keeps it)
zamkniete "$LINIA console=ttyS0 panic=-1" "$K1_WYNIK/k1-test.efi"
zamkniete "$LINIA" "$K1_WYNIK/k1.efi"
rm -f "$SB/k1-sb.key" # never left lying around (regenerated from the secret by the next build)
cp "$SB/k1-sb.cer" "$K1_WYNIK/k1-sb.cer"
(cd "$K1_WYNIK" && sed -i '/  k1\.efi$\|  k1-sb\.cer$/d' SHA256SUMS && sha256sum k1.efi k1-sb.cer >> SHA256SUMS)
info "gotowe: $K1_WYNIK/k1.efi i k1-test.efi ($(du -k "$K1_WYNIK/k1.efi" | cut -f1) KiB, $(grep -q 'K1_SB_PRAWDZIWY 1' "$SB/k1_sb_cert.h" && echo 'klucz wydania' || echo 'klucz deweloperski'))"
