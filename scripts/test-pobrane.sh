#!/bin/sh
# Test plików ze strefy do Plików (0.11.0 krok 2, ADR 0052; powtarzalny, działa też w emulacji bez KVM): obraz testowy
# z małym dyskiem o numerze seryjnym „k1-test-pobranych”. Program testowy w K1 uruchamia strefę próbną w trybach
# pobierania (src/strefa/gosc/pobrane.c), a ona — jak agent przeglądarki — zgłasza pliki na porcie plików VMM:
#   1. uczciwa: 4 pliki naraz — okienka K1 po kolei („Strefa próbna pobrała plik”), ten skrypt klika: „Zapisz
#      w Pobranych” dla trzech, „Zostaw w przeglądarce” dla „zostaw.bin”; zrzut okienka; gość dostaje wyniki
#   2. zapisane w Pliki → Pobrane (program testowy czyta jak Pliki): „raport (test).txt” 3 kawałki, bajt w bajt;
#      „zły/plik\x01.pdf” jako „zły_plik_.pdf” (zasady nazw K1), typ PDF z treści; „pusty.txt” 0 B; „zostaw.bin” nie ma;
#      pochodzenie strefy próbnej („ze strefy próbnej”), twórca = strefa
#   3. NEGATYWNE (program testowy mówi „Zapisz” każdemu plikowi): bajty bez zgody, więcej bajtów niż rozmiar, ramka bez
#      typu — VMM zamyka tylko port plików, strefa żyje; zalew 20 plików — 4 przyjęte, reszta „odmowa”; plik 5 GB —
#      „za duży”; BRAK w połowie bajtów i koniec strefy w połowie — nic połowicznego w Plikach
#   4. K1 działa dalej; w logu K1 żadnej nazwy pliku (niezmiennik 8)
# Dowody: build/testy/pobrane.log, build/testy/pobrane-okienko.png. Wymaga: scripts/zbuduj.sh.
. "$(dirname "$0")/wspolne.sh"
TESTY=$K1_BUDOWA/testy
CZAS=${K1_TEST_CZAS:-1800}
mkdir -p "$TESTY"; rm -f "$TESTY"/pobrane*
CMDLINE="$(cat "$K1_REPO/jadro/cmdline")"
ZNAK=$TESTY/pobrane-znak.img
dd if=/dev/zero of="$ZNAK" bs=1M count=1 status=none
EMULACJA=
if [ -w /dev/kvm ]; then
    grep -qsE '^(1|Y)$' /sys/module/kvm_intel/parameters/nested /sys/module/kvm_amd/parameters/nested || EMULACJA=--emulacja
fi

cat > "$K1_BUDOWA/pobrane.py" <<'PY'
import os, re, subprocess, sys
sys.path.insert(0, os.environ["K1_TESTY_PY"])
from k1qemu import Maszyna
repo, out, limit, cmdline, znak, emulacja = sys.argv[1], sys.argv[2], float(sys.argv[3]), sys.argv[4], sys.argv[5], sys.argv[6:]
ZNAK = (f"-drive if=none,id=znak,file={znak},format=raw,readonly=on -device virtio-blk-pci,drive=znak,"
        f"serial=k1-test-pobranych,addr=0x1e")
log, mon = f"{out}/pobrane.log", f"{out}/pobrane.sock"
for f in (log, mon):
    if os.path.exists(f):
        os.remove(f)
qemu = subprocess.Popen([f"{repo}/scripts/uruchom-qemu.sh", "--obraz", f"{repo}/build/wynik/rdzen-test.squashfs",
                         "--cmdline", cmdline, "--log", log, "--monitor", mon, "--czas", str(int(limit)),
                         "--pamiec", "1024", "--qemu", ZNAK] + emulacja)
q = Maszyna(log, mon, out, limit, "pobrane")
czekaj, sprawdz = q.czekaj, q.sprawdz
OKIENKO = (r"K1-GUI: okienko \(pobranie\): sesja 1, plik (\d+); Zapisz w Pobranych (\d+),(\d+); "
           r"Zostaw w przeglądarce (\d+),(\d+)")

# 1. the honest zone: four files, the windows one after another
sprawdz("strefa próbna zgłasza 4 pliki na porcie plików VMM",
        czekaj(r"K1-TEST: pobrane: tryb testu pobranych", czas=600) and
        czekaj(r"K1-GOSC: pobrane: zgłoszone 4 pliki", czas=600) and
        czekaj(r"K1-NADZORCA: strefa 1: plik 1 \(2101473 B\) — okienko pulpitu", czas=60))
for i in range(4):
    m = czekaj(OKIENKO, od=i, czas=300)
    if not m:
        sprawdz(f"okienko pobrania nr {i + 1}", False, "brak")
        break
    plik = int(m.group(1))
    if i == 0:
        q.zrzut("pobrane-okienko")
    zostaw = plik == 3  # "zostaw.bin": the third file the zone told of (the VMM's numbers 1..4)
    x, y = (int(m.group(4)), int(m.group(5))) if zostaw else (int(m.group(2)), int(m.group(3)))
    q.klik(x, y)
    sprawdz(f"okienko pliku {plik}: „{'Zostaw w przeglądarce' if zostaw else 'Zapisz w Pobranych'}”",
            czekaj(rf"K1-GUI: okienko \(pobranie\): sesja 1, plik {plik}: "
                   rf"{'Zostaw w przeglądarce' if zostaw else 'Zapisz w Pobranych'}", czas=60))
for nr, wynik in ((11, 0), (12, 0), (13, 1), (14, 0)):
    sprawdz(f"gość: plik {nr} — wynik {wynik} ({'zostaje w przeglądarce' if wynik else 'zapisany w Plikach'})",
            czekaj(rf"K1-GOSC: pobrane: plik {nr} — wynik {wynik}\b", czas=600))
sprawdz("nadzorca: 3 pliki zapisane w Pobranych, 1 zostaje w strefie",
        czekaj(r"K1-NADZORCA: strefa 1: plik 1 — zapisany w Pobranych \(2101473 B\)", czas=60) and
        czekaj(r"K1-NADZORCA: strefa 1: plik 3 — zostaje w strefie", czas=60) and
        len(re.findall(r"K1-NADZORCA: strefa 1: plik \d — zapisany w Pobranych", q.tekst())) == 3)

# 3. hostile
ZLY = r"K1-GOSC: pobrane zły "
for co in ("bajty bez zgody", "więcej niż rozmiar", "zła ramka"):
    sprawdz(f"NEGATYWNY: {co} — VMM zamyka tylko port plików, strefa żyje", czekaj(ZLY + co + ": port zamknięty", czas=600))
z = czekaj(ZLY + r"zalew: wyniki (\d+), odmowy (\d+)", czas=600)
sprawdz("NEGATYWNY: zalew 20 plików — najwyżej 4 czekają, reszta dostaje „odmowa”",
        z and z.group(1) == "20" and int(z.group(2)) >= 16, z.group(0) if z else "brak")
sprawdz("NEGATYWNY: plik 5 GB — „za duży dla Plików”", czekaj(ZLY + r"za duży: wynik 3\b", czas=300))
sprawdz("NEGATYWNY: BRAK w połowie bajtów — plik przerwany", czekaj(ZLY + r"brak w trakcie: wynik 1\b", czas=300))
sprawdz("NEGATYWNY: koniec strefy w połowie bajtów — nadzorca porzuca zapis",
        czekaj(ZLY + r"koniec strefy w trakcie: wyłączam", czas=300) and
        czekaj(r"K1-NADZORCA: strefa 1: plik \d+ — przerwany", czas=120))
sprawdz("VMM: 3 złamania zasad portu plików (połączenie zamknięte), maszyna żyła do końca",
        len(re.findall(r"K1-STREFA: pliki: strefa złamała zasady portu plików", q.tekst())) == 3)

# 2. what is in Pobrane
s = czekaj(r"K1-TEST: pobrane: sprawdzenie (\S+)", czas=300)
sprawdz("Pliki → Pobrane: trzy pliki ze strefy (nazwa naprawiona, PDF z treści, pusty, duży bajt w bajt), bez „zostaw.bin” "
        "i bez niczego od złośliwej strefy", s and s.group(1) == "OK",
        (czekaj(r"K1-TEST: pobrane: w Pobranych .*", czas=5) or s).group(0) if s else "brak")

t_ = q.tekst()
sprawdz("K1 działa dalej: nadzorca, ekran i pulpit bez awarii; bez zabronionych wywołań i paniki",
        czekaj(r"K1-TEST: pobrane: koniec scenariusza", czas=120) and
        not re.search(r"K1-NADZORCA: (ekran|gui|skarbiec|nadzorca).*zakończon", t_) and "zabronione wywołanie" not in t_ and
        "Kernel panic" not in t_)
k1 = "\n".join(l for l in t_.splitlines() if re.match(r"\s*K1-(NADZORCA|STREFA|GUI|SKARBIEC|EKRAN)", l))
sprawdz("w logu K1 żadnej nazwy pliku ze strefy (niezmiennik 8)",
        not re.search(r"raport|zostaw\.bin|pusty\.txt|plik_\.pdf|zalew\.bin|duzy\.iso|przerwany\.bin", k1))
q.monitor("quit")
qemu.wait()
sys.exit(0 if all(q.wyniki) else 1)
PY

echo "[k1] test plików ze strefy (0.11.0 krok 2): okienko K1, zapis w Pobranych, pochodzenie, złośliwa strefa"
r=0
K1_TESTY_PY="$K1_REPO/testy/qemu" python3 -I "$K1_BUDOWA/pobrane.py" "$K1_REPO" "$TESTY" "$CZAS" "$CMDLINE" "$ZNAK" $EMULACJA || r=1
if [ $r -eq 0 ]; then info "pobrane: wszystko zaliczone"; else echo "[k1] pobrane: BŁĄD (log: $TESTY/pobrane.log)"; fi
exit $r
