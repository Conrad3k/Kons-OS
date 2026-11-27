#!/bin/sh
# Test 0.13.0 krok 2 (ADR 0057): TPM 2.0 — licznik wersji (generacja) i sprawdzanie danych. QEMU z TPM programowym
# (swtpm, urządzenie tpm-tis; jego stan w katalogu przechodzi z jednego startu na następny jak prawdziwy układ), obraz
# testowy z dyskiem-znakiem „k1-test-tpm” (program testowy zapisuje jeden obiekt w skarbcu — pierwszy zapis sesji) i
# pustą partycją K1-DANE na dysku SATA. Pięć startów:
#   1. „pierwszy”: nowy TPM → nadzorca zakłada miejsce K1, skarbiec tworzy magazyn, po pierwszym zapisie TPM pamięta stan
#      danych, po 15 s pulpitu — generację tej wersji; potem kopia dysku danych (na hoście)
#   2. „drugi”: generacja w TPM = tej wersji, magazyn otwarty bez ostrzeżenia, stan danych zapamiętany znowu (nowszy)
#   3. „cofniete”: dysk danych podmieniony na kopię z 1. → „dane na dysku są starsze”, okienko gui (zrzut), dane działają;
#      „To moje dane” → nadzorca zapisuje decyzję
#   4. „generacja”: ta sama kopia — bez ostrzeżenia; program testowy (dysk „k1-test-tpm-gen”) wpisuje do TPM
#      generację o jeden wyższą, jak zostawiłaby ją nowsza wersja K1
#   5. „starsza”: ta wersja jest starsza → skarbiec bez K1-DANE (dane tylko w pamięci), okienko gui (zrzut), bez
#      potwierdzenia A/B; NEGATYWNE: magazyn nie otwarty, na hoście dysk danych nietknięty (ta sama suma)
# Dowody: build/testy/tpm-*.log, zrzuty build/testy/tpm-*.png. Wymaga: scripts/zbuduj.sh, swtpm
# (scripts/przygotuj-srodowisko.sh).
. "$(dirname "$0")/wspolne.sh"
TESTY=$K1_BUDOWA/testy
CZAS=${K1_TEST_CZAS:-300}
command -v swtpm >/dev/null || blad "brak swtpm (scripts/przygotuj-srodowisko.sh)"
mkdir -p "$TESTY"; rm -rf "$TESTY"/tpm-*
STAN=$TESTY/tpm-stan DANE=$TESTY/tpm-dane.img KOPIA=$TESTY/tpm-kopia.img
mkdir -p "$STAN"
r=0

# (A serial of virtio-blk has at most 20 characters — the marks are short: "k1-test-tpm-generacja" was cut and matched
# "k1-test-tpm".) The data disk: GPT with an empty K1-DANE of 64 MiB (skarbiec makes the vault). Two marks: a small virtio disk each.
truncate -s 70M "$DANE"
printf 'label: gpt\nstart=2048, size=131072, name="K1-DANE"\n' | sfdisk -q "$DANE" >/dev/null
for z in tpm tpm-gen; do truncate -s 1M "$TESTY/tpm-znak-$z.img"; done

uruchom() { # NAZWA ZNAK
    n=$1
    rm -f "$TESTY/tpm-$n.log" "$TESTY/tpm-$n.sock" "$TESTY/tpm.ctrl"
    swtpm socket --tpm2 --tpmstate dir="$STAN" --ctrl type=unixio,path="$TESTY/tpm.ctrl" --terminate &
    SPID=$!
    python3 -I - "$TESTY/tpm.ctrl" <<'PY' || blad "swtpm nie wystartował"
import os, sys, time
for _ in range(100):
    if os.path.exists(sys.argv[1]): sys.exit(0)
    time.sleep(0.1)
sys.exit(1)
PY
    SPRZET="-chardev socket,id=chrtpm,path=$TESTY/tpm.ctrl -tpmdev emulator,id=tpm0,chardev=chrtpm -device tpm-tis,tpmdev=tpm0"
    SPRZET="$SPRZET -drive if=none,id=dane,file=$DANE,format=raw -device ide-hd,drive=dane,bus=ide.0"
    SPRZET="$SPRZET -drive if=none,id=znak,file=$TESTY/tpm-znak-$2.img,format=raw,readonly=on"
    SPRZET="$SPRZET -device virtio-blk-pci,drive=znak,serial=k1-test-$2,addr=0x1e"
    "$K1_REPO/scripts/uruchom-qemu.sh" --log "$TESTY/tpm-$n.log" --monitor "$TESTY/tpm-$n.sock" --czas "$CZAS" \
        --obraz "$K1_WYNIK/rdzen-test.squashfs" --obraz-strefy - --qemu "$SPRZET" &
    QPID=$!
}
sprawdz() {
    K1_TESTY_PY="$K1_REPO/testy/qemu" python3 -I "$K1_BUDOWA/tpm.py" "$TESTY/tpm-$1.log" "$TESTY/tpm-$1.sock" "$TESTY" \
        "$CZAS" "$1" || r=1
    zamknij_qemu "$TESTY/tpm-$1.sock" $QPID
    wait $SPID 2>/dev/null || true
}

cat > "$K1_BUDOWA/tpm.py" <<'PY'
import os, re, sys
sys.path.insert(0, os.environ["K1_TESTY_PY"])
from k1qemu import Maszyna
log, mon, out, limit, tryb = sys.argv[1], sys.argv[2], sys.argv[3], float(sys.argv[4]), sys.argv[5]
q = Maszyna(log, mon, out, limit, "tpm")
czekaj, sprawdz, ile = q.czekaj, q.sprawdz, q.ile
N = r"K1-NADZORCA: "
DANE = N + r"TPM: zapamiętano stan danych"
GEN = N + r"TPM: zapisano generację (\d+)"
COFNIETE = N + r"TPM: dane na dysku są starsze"
OKIENKO = r"K1-GUI: okienko \(%s\): (\S+(?: \S+)*) (\d+),(\d+); (\S+(?: \S+)*) (\d+),(\d+)"


def pulpit():
    g = czekaj(r"K1-GUI: pulpit gotowy", czas=limit * 0.7)
    k0 = czekaj(r"K1-EKRAN: kursor (\d+),(\d+) ")
    if k0:
        q.kursor[:] = [int(k0.group(1)), int(k0.group(2))]
    return sprawdz(f"{tryb}: pulpit gotowy", g and k0)


if tryb == "pierwszy":
    sprawdz("pierwszy: nadzorca zakłada miejsce K1 w nowym TPM",
                  czekaj(N + r"TPM 2\.0: założono miejsce K1", czas=limit * 0.5))
    sprawdz("pierwszy: skarbiec tworzy magazyn", czekaj(r"K1-SKARBIEC: utworzono nowy magazyn"))
    sprawdz("pierwszy: po pierwszym zapisie TPM pamięta stan danych", czekaj(DANE, czas=120))
    pulpit()
    m = czekaj(GEN, czas=120)
    sprawdz("pierwszy: po 15 s pulpitu TPM dostaje generację tej wersji", m and m.group(1) == "1",
                  m.group(0) if m else "")
elif tryb == "drugi":
    m = czekaj(N + r"TPM 2\.0: generacja w TPM (\d+), tej wersji K1 (\d+)", czas=limit * 0.5)
    sprawdz("drugi: TPM ma generację tej wersji", m and m.group(1) == m.group(2), m.group(0) if m else "")
    sprawdz("drugi: magazyn otwarty", czekaj(r"K1-SKARBIEC: magazyn otwarty"))
    sprawdz("drugi: stan danych zapamiętany znowu (nowszy)", czekaj(DANE, czas=120))
    sprawdz("drugi: bez ostrzeżenia o cofniętych danych", ile(COFNIETE) == 0)
    sprawdz("drugi: generacja nie zapisywana drugi raz", q.cisza(GEN, 20))
elif tryb == "cofniete":
    sprawdz("cofniete: nadzorca widzi dysk danych starszy niż TPM", czekaj(COFNIETE, czas=limit * 0.5))
    sprawdz("cofniete: dane działają (magazyn otwarty)", ile(r"K1-SKARBIEC: magazyn otwarty") == 1)
    pulpit()
    m = czekaj(OKIENKO % "dane cofnięte", czas=60)
    sprawdz("cofniete: okienko „Dane na dysku są starsze niż ostatnio”", m)
    if m:
        q.zrzut("cofniete")
        sprawdz("cofniete: przyciski „Później” i „To moje dane”", m.group(1) == "Później" and m.group(4) == "To moje dane")
        n = ile(N + r"TPM: właściciel potwierdził")
        q.klik(int(m.group(5)), int(m.group(6)))
        sprawdz("cofniete: „To moje dane” — nadzorca zapisuje decyzję",
                      czekaj(N + r"TPM: właściciel potwierdził starsze dane", od=n, czas=30))
    sprawdz("cofniete: dopóki właściciel nie zdecydował, stan danych nie był nadpisany", ile(DANE) == 0)
elif tryb == "generacja":
    sprawdz("generacja: magazyn otwarty", czekaj(r"K1-SKARBIEC: magazyn otwarty", czas=limit * 0.5))
    sprawdz("generacja: po decyzji bez ostrzeżenia, stan danych zapamiętany", czekaj(DANE, czas=120) and ile(COFNIETE) == 0)
    sprawdz("generacja: program testowy wpisał nowszą generację", czekaj(r"K1-TEST: TPM: generacja 2 zapisana", czas=60))
elif tryb == "starsza":
    sprawdz("starsza: nadzorca widzi starszą wersję K1",
                  czekaj(N + r"TPM: ta wersja K1 jest starsza niż ta, którą miał ten komputer", czas=limit * 0.5))
    sprawdz("starsza: skarbiec nie dostaje K1-DANE", czekaj(N + r"partycja K1-DANE nie dla skarbca"))
    sprawdz("starsza: dane tylko w pamięci", czekaj(r"K1-SKARBIEC: brak partycji K1-DANE"))
    pulpit()
    m = czekaj(OKIENKO % "starsza wersja", czas=60)
    sprawdz("starsza: okienko „Ta wersja K1 jest za stara”", m)
    if m:
        q.zrzut("starsza")
    sprawdz("starsza: bez potwierdzenia A/B", czekaj(N + r"A/B: ta wersja jest starsza niż generacja w TPM", czas=60))
    sprawdz("starsza: NEGATYWNE — magazyn nie otwarty, generacja nie obniżona",
                  ile(r"K1-SKARBIEC: magazyn otwarty") == 0 and ile(GEN) == 0 and ile(r"zapamiętano stan danych") == 0)
q.koniec(wylacz=False)
PY

info "TPM 1/5: nowy TPM, nowy magazyn"
uruchom pierwszy tpm; sprawdz pierwszy
cp --sparse=always "$DANE" "$KOPIA"
info "TPM 2/5: drugi start"
uruchom drugi tpm; sprawdz drugi
info "TPM 3/5: dysk danych cofnięty do kopii z pierwszego startu"
cp --sparse=always "$KOPIA" "$DANE"
uruchom cofniete tpm; sprawdz cofniete
info "TPM 4/5: po decyzji; nowsza generacja w TPM"
uruchom generacja tpm-gen; sprawdz generacja
info "TPM 5/5: starsza wersja K1"
PRZED=$(sha256sum < "$DANE")
uruchom starsza tpm; sprawdz starsza
if [ "$(sha256sum < "$DANE")" = "$PRZED" ]; then
    echo "  [OK]    starsza: dysk danych nietknięty (ta sama suma)"
else
    echo "  [BŁĄD]  starsza: dysk danych zmieniony"; r=1
fi
rm -f "$DANE" "$KOPIA"
[ $r -eq 0 ] && info "test TPM: OK" || info "test TPM: BŁĄD"
exit $r
