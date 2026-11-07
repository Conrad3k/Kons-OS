#!/bin/sh
# Sprawdza powtarzalność builda: buduje wszystko drugi raz w osobnym katalogu (z tym samym
# SOURCE_DATE_EPOCH) i porównuje sumy SHA-256 wyników. Trwa tyle co pełny build (~3–8 min).
. "$(dirname "$0")/wspolne.sh"
DRUGI=${1:-$K1_BUDOWA/powtorka}
[ -f "$K1_WYNIK/SHA256SUMS" ] || blad "najpierw scripts/zbuduj.sh"
mkdir -p "$DRUGI/pobrane"
cp "$K1_BUDOWA"/pobrane/linux-"$K1_JADRO_WERSJA".tar.* "$DRUGI/pobrane/" 2>/dev/null || true
cp -r "$K1_BUDOWA/pobrane/efi" "$DRUGI/pobrane/" 2>/dev/null || true
K1_BUDOWA=$DRUGI "$K1_REPO/scripts/zbuduj.sh" > "$DRUGI.log" 2>&1 || blad "drugi build nie powiódł się ($DRUGI.log)"
obrazy() { (cd "$1" && sha256sum "k1-$K1_WERSJA.iso" "k1-$K1_WERSJA-usb.img"); }
obrazy "$K1_WYNIK" > "$DRUGI.obrazy1"; obrazy "$DRUGI/wynik" > "$DRUGI.obrazy2"
if diff "$K1_WYNIK/SHA256SUMS" "$DRUGI/wynik/SHA256SUMS" && diff "$DRUGI.obrazy1" "$DRUGI.obrazy2"; then
    info "powtarzalny: oba buildy dały identyczne pliki (także całe ISO i obraz USB)"
else
    blad "wyniki różnią się (powyżej)"
fi
