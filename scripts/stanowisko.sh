#!/bin/sh
# Stanowisko na hoście (0.10.1 krok 5; tak krok 1 znalazł regułę 28): kompozytor strefy zbudowany na tym komputerze
# z ASan/UBSan i prawdziwy Firefox z obrazu strefy (build/strefa-przegladarka) w chroot — bez maszyny wirtualnej, w kilka
# minut. Wejście kompozytora to kolejka FIFO jako /dev/input/event0 (jak virtio-input z VMM), ekran w pamięci
# (testy/stanowisko/ekran.c). Firefox loguje protokół Wayland (WAYLAND_DEBUG) — widać, co dostaje i co wysyła.
# Użycie: scripts/stanowisko.sh [scenariusz] — domyślnie testy/stanowisko/palce.py (przewijanie dwoma palcami po nowej
# karcie, plan 0.10.1 krok 5). Wynik: build/stanowisko/{firefox,kompozytor}.log, zrzuty *.ppm. Wymaga: root (unshare -m,
# chroot), scripts/zbuduj.sh (obraz strefy). Narzędzie do szukania błędów — nie wchodzi do scripts/testy.sh (Firefox
# na hoście zależy od komputera budowy; od 0.10.1 przeglądarkę sprawdza tylko właściciel na sprzęcie — AGENTS.md).
. "$(dirname "$0")/wspolne.sh"
SCEN=${1:-$K1_REPO/testy/stanowisko/palce.py}
WYNIK=$K1_BUDOWA/stanowisko
OBRAZ=$K1_BUDOWA/strefa-przegladarka
[ -x "$OBRAZ/usr/bin/firefox-esr" ] || { echo "[k1] brak obrazu strefy: najpierw scripts/zbuduj.sh"; exit 1; }
rm -rf "$WYNIK"; mkdir -p "$WYNIK"
gcc -std=c11 -g -O1 -D_GNU_SOURCE -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I"$K1_REPO/src/strefa/kompozytor" -I"$K1_REPO/src/strefa/gosc" -I"$K1_REPO/src/wspolne" -I"$K1_BUDOWA/naglowki/include" \
    $(ls "$K1_REPO"/src/strefa/kompozytor/*.c | grep -v '/drm\.c$') "$K1_REPO/testy/stanowisko/ekran.c" \
    -o "$WYNIK/kompozytor"
echo "[k1] stanowisko: kompozytor strefy na hoście + Firefox z obrazu strefy ($(basename "$SCEN"))"
r=0
K1_KOMPOZYTOR="$WYNIK/kompozytor" timeout --kill-after=10 600 unshare -m --propagation private \
    python3 -I "$K1_REPO/testy/stanowisko/stanowisko.py" "$OBRAZ" "$WYNIK" "$SCEN" || r=1
if [ $r -eq 0 ]; then info "stanowisko: wszystko zaliczone"; else echo "[k1] stanowisko: BŁĄD (logi: $WYNIK)"; fi
exit $r
