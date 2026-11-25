#!/bin/sh
# Test etapów M1–M4 (powtarzalny): testy na hoście + uruchomienia K1 w QEMU, także negatywne
# (start, testy w systemie, błędy startu). Ekran i wejście z zewnątrz: scripts/test-m4.sh (początek).
# Wymaga zbudowanych obrazów (scripts/zbuduj.sh). Logi: build/testy/<scenariusz>.log
# Kod wyjścia 0 = wszystko zaliczone. Opis scenariuszy: docs/TESTY.md.
. "$(dirname "$0")/wspolne.sh"
# Whether log $1 contains $2. Kernel messages ("[   51.005212] …") may land in the middle of one of
# our lines on the shared console ("K1: wyłączanie[   51.0…] sd … cache⏎ zasilania" — CI, 0.6.1):
# they are removed first, together with the line break they end with.
zawiera() {
    python3 -I -c 'import re,sys
try: t=open(sys.argv[1],encoding="utf-8",errors="replace").read()
except OSError: sys.exit(1)
sys.exit(0 if sys.argv[2] in t or sys.argv[2] in re.sub(r"\[ *\d+\.\d+\] [^\n]*\n","",t) else 1)' "$1" "$2"
}

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
    # 512 MiB (0.10.0): the in-system test starts the test zone (its 128 MiB and nadzorca's margin must be free)
    # shellcheck disable=SC2086
    "$K1_REPO/scripts/uruchom-qemu.sh" $dysk --cmdline "$cmd" --log "$log" --monitor "$mon" --czas "$CZAS" --pamiec 512 \
        ${SPRZET:+--qemu "$SPRZET"} &
    qpid=$!
    sam=0; t=0
    while [ $t -lt "$CZAS" ]; do
        if ! kill -0 $qpid 2>/dev/null; then sam=1; break; fi
        if [ "$tryb" = czekaj ]; then
            brak=0; for w in "$@"; do zawiera "$log" "$w" || brak=1; done
            [ $brak -eq 0 ] && break
        fi
        sleep 1; t=$((t + 1))
    done
    if [ $sam -eq 0 ]; then
        # Maszyna nadal działa: przy niepowodzeniu zachowaj zrzut ekranu (diagnostyka), potem zamknij QEMU.
        brak=0; for w in "$@"; do zawiera "$log" "$w" || brak=1; done
        [ $brak -eq 0 ] || "$K1_REPO/scripts/zrzut-ekranu.sh" "$TESTY/$nazwa.png" "$mon" >/dev/null 2>&1 || true
        python3 -I "$K1_REPO/testy/qemu/k1qemu.py" zamknij "$mon" \
            || kill $qpid 2>/dev/null || true
    fi
    kod=0; wait $qpid 2>/dev/null || kod=$?
    problem=""
    for w in "$@"; do zawiera "$log" "$w" || problem="$problem brak:'$w'"; done
    grep -q 'Kernel panic' "$log" && problem="$problem kernel-panic"
    grep -qF 'K1-TEST: [BŁĄD]' "$log" && problem="$problem nieudany-test-w-systemie"
    grep -qF 'K1-PROBA: [BŁĄD]' "$log" && problem="$problem nieudana-próba-klatki"
    grep -qF 'zabronione wywołanie systemowe' "$log" && problem="$problem usługa-zatrzymana-przez-seccomp"
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
# 0.7.5 (AGENTS.md, „Czytelność”): file and function size limits, every service, app and message described
krok "czytelność: pliki ≤ 600 wierszy, funkcje ≤ 80, opisy usług, aplikacji i komunikatów" "$TESTY/czytelnosc.log" \
    python3 -I "$K1_REPO/scripts/sprawdz-czytelnosc.py" "$K1_REPO"
krok "jednostkowe: linia poleceń, protokoły, kanały, przybornik GUI, renderer (ASan/UBSan)" "$TESTY/host.log" \
    make -s -j"$(nproc)" -O -C "$K1_REPO" BUDOWA="$K1_BUDOWA" testy-host
# 0.8.6: the fuzzers side by side, as many as there are processors (one after another: 13 × the time of one).
# 10 s each by default (a local run); CI fuzzes longer and keeps the corpus between runs (k1.yml).
krok "fuzzing: linia poleceń, protokoły zdarzeń i usług (${K1_FUZZ_CZAS:-10} s każdy, libFuzzer)" "$TESTY/fuzz.log" \
    make -s -j"$(nproc)" -O -C "$K1_REPO" BUDOWA="$K1_BUDOWA" fuzz FUZZ_CZAS="${K1_FUZZ_CZAS:-10}"
# (binaries and the kernel config are checked by the build itself: zbuduj-obraz.sh, zbuduj-jadro.sh)

echo "[k1] obrazy do testów negatywnych"
dd if=/dev/zero of="$TESTY/zera.img" bs=1M count=4 status=none
rm -rf "$TESTY/bez-init"; mkdir -p "$TESTY/bez-init/jadro/dev" "$TESTY/bez-init/jadro/proc" "$TESTY/bez-init/jadro/sys"
mksquashfs "$TESTY/bez-init" "$TESTY/bez-init.squashfs" -quiet -no-progress -noappend -comp zstd -all-root -no-xattrs >/dev/null
verity "$TESTY/bez-init.squashfs"  # M6b: a correctly verified image — only /rdzen/init is missing
ZERA_SKROT=0000000000000000000000000000000000000000000000000000000000000000
echo "$ZERA_SKROT" > "$TESTY/zera.img.skrot"
# M6b: the image without its root hash; the image with one changed byte (its correct root hash)
cp "$K1_WYNIK/rdzen.squashfs" "$TESTY/bez-skrotu.squashfs"
cp "$K1_WYNIK/rdzen.squashfs" "$TESTY/zmieniony.squashfs"
cp "$K1_WYNIK/rdzen.squashfs.skrot" "$TESTY/zmieniony.squashfs.skrot"
python3 -I -c 'import sys; f=open(sys.argv[1],"r+b"); f.seek(200000); b=f.read(1); f.seek(200000); f.write(bytes([b[0]^1])); f.close()' \
    "$TESTY/zmieniony.squashfs"
# M5a: extra hardware for the in-system test — SATA disk (q35's built-in AHCI) with a GPT partition
# "K1-DANE", NVMe disk with an old MBR table, USB stick on EHCI, USB keyboard on xHCI, mouse on OHCI.
for d in sata:16 nvme:8 usb:4; do
    dd if=/dev/zero of="$TESTY/${d%:*}.img" bs=1M count="${d#*:}" status=none
done
printf 'label: gpt\nstart=2048, size=8192, name="K1-DANE"\n' | sfdisk -q "$TESTY/sata.img" >/dev/null
printf 'label: dos\nstart=2048, size=4096, type=83\n' | sfdisk -q "$TESTY/nvme.img" >/dev/null
bateria_aml 1 0 "$TESTY/bateria-rozladowanie.aml"   # 0.5.3: a laptop battery (73 %, discharging)
SPRZET_TEST="-drive if=none,id=sata,file=$TESTY/sata.img,format=raw -device ide-hd,drive=sata,bus=ide.0
 -drive if=none,id=nvme,file=$TESTY/nvme.img,format=raw -device nvme,drive=nvme,serial=k1test
 -device qemu-xhci,id=xhci -device usb-kbd,bus=xhci.0
 -device usb-ehci,id=ehci -drive if=none,id=usb,file=$TESTY/usb.img,format=raw -device usb-storage,drive=usb,bus=ehci.0
 -device pci-ohci,id=ohci -device usb-mouse,bus=ohci.0
 -acpitable file=$TESTY/bateria-rozladowanie.aml"
SPRZET=

echo "[k1] uruchomienia w QEMU ($( [ -w /dev/kvm ] && echo KVM || echo 'emulacja TCG'))"
scenariusz start czekaj "$K1_WYNIK/rdzen.squashfs" "$CMDLINE" \
    "K1-INITRAMFS: start" "K1-INITRAMFS: obraz rdzenia sprawdzony (dm-verity" "K1: init OK" "uruchomiono nadzorcę" "K1-EKRAN: pierwsza klatka" "K1-GUI: pulpit gotowy" \
    "K1-GUI: brak baterii"
SPRZET=$SPRZET_TEST
scenariusz testy-w-systemie wylaczenie "$K1_WYNIK/rdzen-test.squashfs" "$CMDLINE" \
    "K1: init OK" "K1-TEST: WYNIK: OK" "odrzucone 5)" "komunikat od obcego nadawcy (uid 0, oczekiwano 100)" \
    "bez fokusu (ochrona klawiatury)" "K1-SKARBIEC: utworzono nowy magazyn na pustej partycji" \
    "K1-GUI: bateria 73%, rozładowywanie, zasilacz odłączony" \
    "K1-GUI: pomiar baterii: 73%, rozładowywanie, 9,0 W, zostało 219 min" \
    "przekroczony limit pamięci powierzchni" "K1-ZASOBY: dane od nadzorcy" \
    "deskryptor nie jest memfd" "memfd bez pieczęci" "zły rozmiar memfd" "panele tylko dla powłoki" \
    "pulpit tylko dla powłoki" "rozmiar inny niż ten, o który prosił ekran" \
    "tego ustawienia ten klient nie zmienia" "zarządzanie cudzymi oknami tylko dla powłoki" \
    "ekran zakończony sygnałem 11" "ponowne uruchomienie ekran" "wejscie wyłączony: 6 zakończeń" \
    "K1: wyłączanie zasilania" "reboot: Power down"
SPRZET=
# The negative starts are independent of each other (own logs, sockets and images): with KVM side by side, without it
# (emulation takes the whole processor) one after another — like scripts/testy.sh. Each prints its result to a file.
ROWNOLEGLE=${K1_TESTY_ROWNOLEGLE:-}
if [ -z "$ROWNOLEGLE" ]; then
    if [ -w /dev/kvm ]; then ROWNOLEGLE=1; else ROWNOLEGLE=0; fi
fi
NEG=
neg() { # the arguments of scenariusz
    NEG="$NEG $1"
    # in a subshell either way: the counters are summed from the files below
    if [ "$ROWNOLEGLE" = 1 ]; then (scenariusz "$@") > "$TESTY/$1.wynik" 2>&1 &
    else (scenariusz "$@") > "$TESTY/$1.wynik" 2>&1 || true; fi
}
neg neg-brak-dysku wylaczenie - "$CMDLINE k1.rdzen_skrot=$ZERA_SKROT" \
    "nie znaleziono urządzenia z obrazem rdzenia: /dev/vda" "system zostanie wyłączony" "reboot: Power down"
neg neg-dysk-bez-obrazu wylaczenie "$TESTY/zera.img" "$CMDLINE" \
    "nie udało się zamontować obrazu rdzenia z /dev/vda" "reboot: Power down"
neg neg-obraz-bez-init wylaczenie "$TESTY/bez-init.squashfs" "$CMDLINE" \
    "obraz rdzenia nie zawiera programu init" "reboot: Power down"
# One bad k1.rdzen is enough here (the system shuts down): the parser's cases (missing, "..", repeated, too long)
# are checked on the host (test_cmdline, fuzz_cmdline) — since 0.10.0 not one start each.
neg neg-zly-parametr wylaczenie "$K1_WYNIK/rdzen.squashfs" "$BAZA k1.rdzen=/dev/../vda" \
    "niepoprawny parametr k1.rdzen: dozwolone tylko" "reboot: Power down"
# M6b (ADR 0021): dm-verity — no root hash, a wrong one, one changed byte in the image
neg neg-brak-skrotu wylaczenie "$TESTY/bez-skrotu.squashfs" "$CMDLINE" \
    "brak parametru k1.rdzen_skrot" "reboot: Power down"
neg neg-zly-skrot wylaczenie "$K1_WYNIK/rdzen.squashfs" "$CMDLINE k1.rdzen_skrot=$(echo "$ZERA_SKROT" | tr 0 a)" \
    "obraz rdzenia jest zmieniony albo uszkodzony" "reboot: Power down"
neg neg-zmieniony-obraz wylaczenie "$TESTY/zmieniony.squashfs" "$CMDLINE" \
    "obraz rdzenia jest zmieniony albo uszkodzony" "reboot: Power down"
wait
for n in $NEG; do
    cat "$TESTY/$n.wynik"
    # grep -c prints 0 and exits with 1 when nothing matches (set -e in wspolne.sh)
    zaliczone=$((zaliczone + $(grep -c '^  \[OK\]' "$TESTY/$n.wynik" || true)))
    niezaliczone=$((niezaliczone + $(grep -c '^  \[BŁĄD\]' "$TESTY/$n.wynik" || true)))
    grep -q '^  \[\(OK\|BŁĄD\)\]' "$TESTY/$n.wynik" || { niezaliczone=$((niezaliczone + 1)); echo "  [BŁĄD]  qemu: $n — brak wyniku"; }
done

echo "[k1] wynik: zaliczone $zaliczone, niezaliczone $niezaliczone"
[ "$niezaliczone" -eq 0 ]
