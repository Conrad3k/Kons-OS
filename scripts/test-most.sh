#!/bin/sh
# Test Raptor Bridge — mostu między strefą a światem przez K1 (0.10.5 krok 5, T15; powtarzalny, działa też w emulacji
# bez KVM): obraz testowy z małym dyskiem o numerze seryjnym „k1-test-mostu” i kartą w sieci QEMU (-netdev user: router
# 10.0.2.2 = ten komputer). Na „routerze” serwer testowy w Pythonie (port 8790): „D n” — n bajtów wzoru i koniec, „E” —
# echo do końca klienta, „H n” — n bajtów, półzamknięcie serwera, potem liczy bajty klienta, „C n” — połowa i reset.
# Program testowy w K1 uruchamia strefę próbną w trybach mostu (gosc/most.c), a ta jest własnym klientem: przekaźnik
# w strefie → vsock → łącznik w VMM → nadzorca (zasada D3, gniazdo) → serwer.
#   1. pojedyncze połączenia: 1 bajt, 3 MiB w dół, echo 1 MiB (przepustowość jednego połączenia, w logu)
#   2. 48 połączeń naraz: od 1 B do 1 MiB i echa — każdy bajt zgodny ze wzorem
#   3. półzamknięcia w obie strony: klient kończy wysyłanie i czyta echo; serwer kończy pierwszy, klient wysyła dalej
#   4. przerwane w połowie: klient zamyka po 256 KiB z 4 MiB; serwer resetuje w połowie; klient łączy się z przekaźnikiem
#      i odchodzi bez bajtu (jego miejsce w VMM wraca — poprawka 0.10.5 krok 5)
#   5. odmowy nadzorcy (D3: sieć domowa, pętla zwrotna) obok udanych; nazwa, której nie ma, i zła nazwa
#   6. NEGATYWNE: zły SOCKS5 wprost do VMM (bez przekaźnika): SOCKS4, BIND, IPv6, port 0, zła nazwa, bajt zarezerwowany
#   7. NEGATYWNE: więcej połączeń niż miejsc (140 > 128) — dokładnie 128 dostaje odpowiedź, reszta odmowę, żadne nie wisi
#   8. liczniki zgodne z obu stron: połączenia przyjęte przez serwer = udane u gościa; odmowy u nadzorcy = odmowy D3
#      u gościa; VMM na końcu: zajęte 0
#   9. NEGATYWNE: złośliwy gość vsock (sam steruje urządzeniem): 200 połączeń, pakiety bez połączenia i nieznane — VMM
#      odmawia i żyje; potem ponad kredyt — koniec tylko tej maszyny; pakiet z cudzym adresem — koniec tylko tej maszyny
#  10. po złośliwych most działa jak przedtem (drugi pełny przebieg); w logu K1 żadnego adresu ani nazwy (niezmiennik 8)
# Dowody: build/testy/most.log (czasy i MiB/s — liczby do docs/komponenty/raptor-bridge.md). Wymaga: scripts/zbuduj.sh.
. "$(dirname "$0")/wspolne.sh"
TESTY=$K1_BUDOWA/testy
CZAS=${K1_TEST_CZAS:-2400}
mkdir -p "$TESTY"; rm -f "$TESTY"/most*
CMDLINE="$(cat "$K1_REPO/jadro/cmdline")"
ZNAK=$TESTY/most-znak.img
dd if=/dev/zero of="$ZNAK" bs=1M count=1 status=none
EMULACJA=
if [ -w /dev/kvm ]; then
    grep -qsE '^(1|Y)$' /sys/module/kvm_intel/parameters/nested /sys/module/kvm_amd/parameters/nested || EMULACJA=--emulacja
fi

cat > "$K1_BUDOWA/most.py" <<'PY'
import os, re, socket, socketserver, struct, subprocess, sys, threading
sys.path.insert(0, os.environ["K1_TESTY_PY"])
from k1qemu import Maszyna
repo, out, limit, cmdline, znak, emulacja = sys.argv[1], sys.argv[2], float(sys.argv[3]), sys.argv[4], sys.argv[5], sys.argv[6:]
PORT = 8790  # MOST_PORT (src/strefa/gosc/most.h)
WZOR = bytes(((i * 7 + (i >> 9) + 13) & 255) for i in range((4 << 20) + 65536))  # most_wzor()
stan = {"przyjete": 0, "d": 0, "e": 0, "h": 0, "c": 0, "h_dobre": 0, "h_zle": 0, "przerwane": 0, "wyslane": 0,
        "odebrane": 0}
zamek = threading.Lock()


def licz(k, n=1):
    with zamek:
        stan[k] += n


class Obsluga(socketserver.BaseRequestHandler):
    def handle(self):
        s = self.request
        licz("przyjete")
        linia = b""
        try:
            while not linia.endswith(b"\n") and len(linia) < 32:
                b = s.recv(1)
                if not b:
                    return
                linia += b
            czesci = linia.decode().split()
            op, n = czesci[0], int(czesci[1]) if len(czesci) > 1 else 0
            if op == "D":
                licz("d"); s.sendall(WZOR[:n]); licz("wyslane", n)
            elif op == "E":
                licz("e")
                while True:
                    b = s.recv(65536)
                    if not b:
                        break
                    s.sendall(b); licz("odebrane", len(b)); licz("wyslane", len(b))
            elif op == "H":  # the server ends its side first; the client goes on sending
                licz("h"); s.sendall(WZOR[:n]); licz("wyslane", n)
                s.shutdown(socket.SHUT_WR)
                dane = b""
                while True:
                    b = s.recv(65536)
                    if not b:
                        break
                    dane += b
                licz("odebrane", len(dane))
                licz("h_dobre" if dane == WZOR[:len(dane)] and len(dane) > 0 else "h_zle")
            elif op == "C":  # half, then a reset (SO_LINGER 0)
                licz("c"); s.sendall(WZOR[:n // 2]); licz("wyslane", n // 2)
                s.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack("ii", 1, 0))
        except (BrokenPipeError, ConnectionResetError):
            licz("przerwane")  # the client closed in the middle
        finally:
            s.close()


class Serwer(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True
    request_queue_size = 256


serwer = Serwer(("127.0.0.1", PORT), Obsluga)
threading.Thread(target=serwer.serve_forever, daemon=True).start()

SIEC = "-netdev user,id=n0 -device e1000e,netdev=n0"
ZNAK = (f"-drive if=none,id=znak,file={znak},format=raw,readonly=on -device virtio-blk-pci,drive=znak,"
        f"serial=k1-test-mostu,addr=0x1e")
log, mon = f"{out}/most.log", f"{out}/most.sock"
for f in (log, mon):
    if os.path.exists(f):
        os.remove(f)
qemu = subprocess.Popen([f"{repo}/scripts/uruchom-qemu.sh", "--obraz", f"{repo}/build/wynik/rdzen-test.squashfs",
                         "--cmdline", cmdline, "--log", log, "--monitor", mon, "--czas", str(int(limit)),
                         "--pamiec", "1024", "--qemu", SIEC + " " + ZNAK] + emulacja)
q = Maszyna(log, mon, out, limit, "most")
czekaj, sprawdz = q.czekaj, q.sprawdz
FAZY = ["jeden bajt", "3 MiB w dół", "echo 1 MiB", "48 naraz", "półzamknięcia", "przerwane w połowie",
        "odmowy i nazwy", "po złych"]


VMM = r"K1-STREFA: połączenia przeglądarki: (\d+), nazwy (\d+), odmowy (\d+) .*zajęte na końcu (\d+)"
NADZ = r"K1-NADZORCA: strefa 1: połączenia (\d+), odmowy (\d+)"


def przebieg(nr):
    """One run of the honest bridge test (gosc TRYB_MOST); its numbers for the comparison."""
    od_vmm, od_nadz = q.ile(VMM), q.ile(NADZ)
    sprawdz(f"[{nr}] strefa próbna w trybie mostu: przekaźnik w strefie gotowy, klient startuje",
            czekaj(r"K1-TEST: strefy: strefa próbna uruchomiona \(tryb 13\)", od=nr - 1, czas=limit * 0.3) and
            czekaj(r"K1-GOSC: most: start", od=nr - 1, czas=300))
    for f in FAZY:
        m = czekaj(rf"K1-GOSC: most: {re.escape(f)}: ok (\d+)/(\d+), ([\d.]+) s, ([\d.]+) MiB/s", od=nr - 1,
                   czas=900)
        sprawdz(f"[{nr}] {f}: wszystkie połączenia jak trzeba", m and m.group(1) == m.group(2),
                m.group(0) if m else "brak")
    if nr == 1:
        m = czekaj(r"K1-GOSC: most: zły SOCKS5 wprost do VMM: ok (\d+)/(\d+)", czas=300)
        sprawdz("NEGATYWNY: zły SOCKS5 wprost do VMM (SOCKS4, BIND, IPv6, port 0, zła nazwa, bajt zarezerwowany) — "
                "łącznik odpowiada odmową i zamyka", m and m.group(1) == m.group(2) == "6", m.group(0) if m else "brak")
        m = czekaj(r"K1-GOSC: most: więcej połączeń niż miejsc: odpowiedź (\d+), odmowa (\d+), inne (\d+)", czas=300)
        sprawdz("NEGATYWNY: 140 połączeń naraz — 128 miejsc dostaje odpowiedź, 12 odmowę (RST), żadne nie wisi",
                m and m.groups() == ("128", "12", "0"), m.group(0) if m else "brak")
    w = czekaj(r"K1-GOSC: most: wynik (\S+): połączenia do serwera (\d+), odmowy D3 (\d+)", od=nr - 1, czas=600)
    sprawdz(f"[{nr}] wynik gościa OK", w and w.group(1) == "OK", w.group(0) if w else "brak")
    k = czekaj(VMM, od=od_vmm, czas=120)
    sprawdz(f"[{nr}] VMM na końcu strefy: żadne miejsce połączenia nie zostało zajęte", k and k.group(4) == "0",
            k.group(0) if k else "brak")
    n = czekaj(NADZ, od=od_nadz, czas=60)
    sprawdz(f"[{nr}] odmowy u nadzorcy = odmowy D3 u gościa", n and w and n.group(2) == w.group(3),
            f"{n.group(0) if n else 'brak'} / {w.group(0) if w else 'brak'}")
    return int(w.group(2)) if w else -1


tcp = przebieg(1)
sprawdz("liczniki zgodne z obu stron: połączenia przyjęte przez serwer = udane u gościa",
        tcp == stan["przyjete"], f"gość {tcp}, serwer {stan['przyjete']}")
sprawdz("serwer: półzamknięcie serwera — klient wysłał potem swoje bajty, zgodne ze wzorem",
        stan["h_dobre"] == 2 and stan["h_zle"] == 0, str(stan))
# 9. hostile to vsock
sprawdz("NEGATYWNY: gość ponad kredyt vsock (po 200 połączeniach i pakietach bez połączenia) — koniec tylko tej maszyny",
        czekaj(r"K1-GOSC: tryb 14: vsock — 200 połączeń", czas=300) and
        czekaj(r"K1-STREFA: strefa 1: koniec — vsock: gość przekroczył kredyt", czas=120) and
        czekaj(r"K1-TEST: strefy: strefa skończyła się \(tryb 14\)", czas=120))
m = re.findall(r"K1-STREFA: połączenia przeglądarki: (\d+), .*bez wolnego miejsca (\d+)", q.tekst())
sprawdz("…VMM przyjął 128 połączeń, 72 odmówił (bez wolnego miejsca) — i żył do pakietu ponad kredyt",
        len(m) >= 2 and m[1] == ("128", "72"), str(m))
sprawdz("NEGATYWNY: pakiet vsock z cudzym adresem — koniec tylko tej maszyny",
        czekaj(r"K1-STREFA: strefa 1: koniec — vsock: zły adres albo typ pakietu", czas=300) and
        czekaj(r"K1-TEST: strefy: strefa skończyła się \(tryb 15\)", czas=120))
przyjete = stan["przyjete"]
tcp2 = przebieg(2)
sprawdz("po złośliwych gościach most działa jak przedtem; liczniki zgodne", tcp2 == stan["przyjete"] - przyjete,
        f"gość {tcp2}, serwer {stan['przyjete'] - przyjete}")
t_ = q.tekst()
sprawdz("K1 działa dalej: nadzorca, ekran i pulpit bez awarii; bez zabronionych wywołań i paniki",
        czekaj(r"K1-TEST: most: koniec scenariusza", czas=120) and
        not re.search(r"K1-NADZORCA: (ekran|gui|nadzorca).*zakończon", t_) and "zabronione wywołanie" not in t_ and
        "Kernel panic" not in t_)
k1 = "\n".join(l for l in t_.splitlines() if re.match(r"\s*K1-(NADZORCA|STREFA|SIEC)", l))
sprawdz("w logu K1 żadnego adresu ani nazwy z połączeń stref (niezmiennik 8)",
        not re.search(r"10\.0\.2\.(2|77)\b|127\.0\.0\.1|invalid|zla nazwa", k1))
for m in re.finditer(r"K1-GOSC: most: (3 MiB w dół|echo 1 MiB|48 naraz): ok \d+/\d+, ([\d.]+) s, ([\d.]+) MiB/s", t_):
    print(f"  [info]  wynik pomiaru — {m.group(1)}: {m.group(2)} s, {m.group(3)} MiB/s{' (emulacja)' if emulacja else ''}",
          flush=True)  # "wynik": the CI summary shows it
q.monitor("quit")
qemu.wait()
sys.exit(0 if all(q.wyniki) else 1)
PY

echo "[k1] test Raptor Bridge (0.10.5 krok 5): most strefy do serwera na routerze, liczniki, złośliwy gość"
r=0
K1_TESTY_PY="$K1_REPO/testy/qemu" python3 -I "$K1_BUDOWA/most.py" "$K1_REPO" "$TESTY" "$CZAS" "$CMDLINE" "$ZNAK" $EMULACJA || r=1
if [ $r -eq 0 ]; then info "most: wszystko zaliczone"; else echo "[k1] most: BŁĄD (log: $TESTY/most.log)"; fi
exit $r
