#!/bin/sh
# Test etapów M1–M2 (powtarzalny): testy na hoście + uruchomienia K1 w QEMU, także negatywne
# (start, testy w systemie, błędy startu). Ekran i wejście z zewnątrz: scripts/test-m2.sh.
# Wymaga zbudowanych obrazów (scripts/zbuduj.sh). Logi: build/testy/<scenariusz>.log
# Kod wyjścia 0 = wszystko zaliczone. Opis scenariuszy: docs/TESTY.md.
. "$(dirname "$0")/wspolne.sh"

TESTY=$K1_BUDOWA/testy
CZAS=${K1_TEST_CZAS:-240}          # limit na jedno uruchomienie (emulacja TCG bywa wolna)
CMDLINE=$(cat "$K1_REPO/jadro/cmdline")
BAZA=${CMDLINE% k1.rdzen=*}        # linia poleceń bez k1.rdzen=
mkdir -p "$TESTY"
zaliczone=0; niezaliczone=0

wynik() { # $1 = nazwa, $2 = 0/1, $3 = opis
    if [ "$2" -eq 0 ]; then zaliczone=$((zaliczone + 1)); echo "  [OK]    $1"
    else niezaliczone=$((niezaliczone + 1)); echo "  [BŁĄD]  $1 — $3"; fi
}

# scenariusz NAZWA TRYB OBRAZ CMDLINE OCZEKIWANE...
#   TRYB=wylaczenie: maszyna ma się sama wyłączyć (kontrolowane wyłączenie) przed upływem limitu
#   TRYB=czekaj:     po pojawieniu się wszystkich oczekiwanych napisów test zamyka QEMU
#   OBRAZ=-        : bez dysku
# Zawsze niedozwolone: "Kernel panic", "[BŁĄD]" z testów w systemie.
scenariusz() {
    nazwa=$1 tryb=$2 obraz=$3 cmd=$4; shift 4
    log=$TESTY/$nazwa.log mon=$TESTY/$nazwa.sock
    if [ "$obraz" = - ]; then dysk=--bez-dysku; else dysk="--obraz $obraz"; fi
    # Stary log z poprzedniego przebiegu musi zniknąć PRZED startem QEMU w tle — inaczej pętla
    # niżej mogłaby go przeczytać, zanim uruchom-qemu.sh zdąży go usunąć.
    rm -f "$log" "$mon" "$TESTY/$nazwa.png"
    # shellcheck disable=SC2086
    "$K1_REPO/scripts/uruchom-qemu.sh" $dysk --cmdline "$cmd" --log "$log" --monitor "$mon" --czas "$CZAS" &
    qpid=$!
    sam=0; t=0
    while [ $t -lt "$CZAS" ]; do
        if ! kill -0 $qpid 2>/dev/null; then sam=1; break; fi
        if [ "$tryb" = czekaj ]; then
            brak=0; for w in "$@"; do grep -qF -- "$w" "$log" 2>/dev/null || brak=1; done
            [ $brak -eq 0 ] && break
        fi
        sleep 1; t=$((t + 1))
    done
    if [ $sam -eq 0 ]; then
        # Maszyna nadal działa: przy niepowodzeniu zachowaj zrzut ekranu (diagnostyka), potem zamknij QEMU.
        brak=0; for w in "$@"; do grep -qF -- "$w" "$log" 2>/dev/null || brak=1; done
        [ $brak -eq 0 ] || "$K1_REPO/scripts/zrzut-ekranu.sh" "$TESTY/$nazwa.png" "$mon" >/dev/null 2>&1 || true
        python3 -I -c 'import socket,sys; s=socket.socket(socket.AF_UNIX); s.settimeout(5); s.connect(sys.argv[1]); s.sendall(b"quit\n"); s.recv(64)' "$mon" 2>/dev/null \
            || kill $qpid 2>/dev/null || true
    fi
    kod=0; wait $qpid 2>/dev/null || kod=$?
    problem=""
    for w in "$@"; do grep -qF -- "$w" "$log" || problem="$problem brak:'$w'"; done
    grep -q 'Kernel panic' "$log" && problem="$problem kernel-panic"
    grep -qF 'K1-TEST: [BŁĄD]' "$log" && problem="$problem nieudany-test-w-systemie"
    [ "$tryb" = wylaczenie ] && [ $sam -eq 0 ] && problem="$problem brak-wyłączenia-przed-limitem"
    if [ -z "$problem" ]; then wynik "qemu: $nazwa (${t} s)" 0 ""
    else wynik "qemu: $nazwa" 1 "$problem; czas ${t} s, QEMU zakończyło się samo: $sam, kod: $kod (log: $log)"; fi
}

echo "[k1] testy na hoście (ASan/UBSan, fuzzing)"
krok() { # $1 = nazwa, $2 = plik logu, reszta = polecenie
    n=$1 l=$2; shift 2
    r=0; "$@" >"$l" 2>&1 || r=$?
    wynik "$n" "$r" "log: $l"
}
krok "jednostkowe: linia poleceń, protokół zdarzeń, renderer (ASan/UBSan)" "$TESTY/host.log" \
    make -s -C "$K1_REPO" BUDOWA="$K1_BUDOWA" testy-host
krok "fuzzing: linia poleceń i protokół zdarzeń (${K1_FUZZ_CZAS:-20} s każdy, libFuzzer)" "$TESTY/fuzz.log" \
    make -s -C "$K1_REPO" BUDOWA="$K1_BUDOWA" fuzz FUZZ_CZAS="${K1_FUZZ_CZAS:-20}"
krok "zabezpieczenia binariów (static-pie, RELRO, NX)" "$TESTY/binaria.log" \
    "$K1_REPO/scripts/sprawdz-binaria.sh" "$K1_BUDOWA"/bin/*
krok "konfiguracja jądra zgodna z jadro/k1.config" "$TESTY/jadro.log" \
    "$K1_REPO/scripts/sprawdz-konfiguracje-jadra.sh" "$K1_REPO/jadro/k1.config" "$K1_WYNIK/jadro.config"

echo "[k1] obrazy do testów negatywnych"
dd if=/dev/zero of="$TESTY/zera.img" bs=1M count=4 status=none
rm -rf "$TESTY/bez-init"; mkdir -p "$TESTY/bez-init/jadro/dev" "$TESTY/bez-init/jadro/proc" "$TESTY/bez-init/jadro/sys"
mksquashfs "$TESTY/bez-init" "$TESTY/bez-init.squashfs" -quiet -no-progress -noappend -comp zstd -all-root -no-xattrs >/dev/null

echo "[k1] uruchomienia w QEMU ($( [ -w /dev/kvm ] && echo KVM || echo 'emulacja TCG'))"
scenariusz start czekaj "$K1_WYNIK/rdzen.squashfs" "$CMDLINE" \
    "K1-INITRAMFS: start" "K1: init OK" "uruchomiono nadzorcę" "K1-EKRAN: pierwsza klatka"
scenariusz testy-w-systemie wylaczenie "$K1_WYNIK/rdzen-test.squashfs" "$CMDLINE" \
    "K1: init OK" "K1-TEST: WYNIK: OK" "odrzucone 4)" "ekran zakończony sygnałem 11" "K1: wyłączanie zasilania" "reboot: Power down"
scenariusz neg-brak-dysku wylaczenie - "$CMDLINE" \
    "nie znaleziono urządzenia z obrazem rdzenia: /dev/vda" "system zostanie wyłączony" "reboot: Power down"
scenariusz neg-dysk-bez-obrazu wylaczenie "$TESTY/zera.img" "$CMDLINE" \
    "nie udało się zamontować obrazu rdzenia z /dev/vda" "reboot: Power down"
scenariusz neg-obraz-bez-init wylaczenie "$TESTY/bez-init.squashfs" "$CMDLINE" \
    "obraz rdzenia nie zawiera programu init" "reboot: Power down"
scenariusz neg-brak-parametru wylaczenie "$K1_WYNIK/rdzen.squashfs" "$BAZA" \
    "brak parametru k1.rdzen" "reboot: Power down"
scenariusz neg-zly-parametr wylaczenie "$K1_WYNIK/rdzen.squashfs" "$BAZA k1.rdzen=/dev/../vda" \
    "niepoprawny parametr k1.rdzen: dozwolone tylko" "reboot: Power down"
scenariusz neg-powtorzony-parametr wylaczenie "$K1_WYNIK/rdzen.squashfs" "$CMDLINE k1.rdzen=/dev/vdb" \
    "niepoprawny parametr k1.rdzen (za długi, powtórzony" "reboot: Power down"

echo "[k1] wynik: zaliczone $zaliczone, niezaliczone $niezaliczone"
[ "$niezaliczone" -eq 0 ]
