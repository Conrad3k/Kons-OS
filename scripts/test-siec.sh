#!/bin/sh
# Test sieci (0.9.0, ADR 0039; powtarzalny): K1 z kartą sieciową (e1000e — jak X230) podłączoną kablem do „sieci
# testowej” (testy/qemu/k1siec.py: drugi koniec kabla w Pythonie, QEMU -netdev dgram). Obraz produkcyjny.
#   krok 5: jądro widzi kartę; z zewnątrz nic w K1 nie odpowiada — zapytania ARP, ping (także rozgłoszeniowy),
#           ICMP „znacznik czasu”, SYN na porty TCP 1–1024 i popularne wyższe, datagramy UDP: K1 nie wysyła ani jednej
#           ramki (karta nie jest włączona — nikt w K1 nie pobiera jeszcze adresu)
# Dowody: build/testy/siec.log (log K1), build/testy/siec-ramki.txt (ramki od K1). Wymaga: scripts/zbuduj.sh.
. "$(dirname "$0")/wspolne.sh"
TESTY=$K1_BUDOWA/testy
CZAS=${K1_TEST_CZAS:-240}
mkdir -p "$TESTY"; rm -f "$TESTY"/siec*

cat > "$K1_BUDOWA/siec.py" <<'PY'
import os, subprocess, sys, time
sys.path.insert(0, os.environ["K1_TESTY_PY"])
from k1qemu import Maszyna
import k1siec as s
repo, out, limit = sys.argv[1], sys.argv[2], float(sys.argv[3])
log, mon = f"{out}/siec.log", f"{out}/siec.sock"
siec = s.SiecTestowa(out)
qemu = subprocess.Popen([f"{repo}/scripts/uruchom-qemu.sh", "--log", log, "--monitor", mon, "--czas", str(int(limit)),
                         "--qemu", siec.qemu("e1000e")])
q = Maszyna(log, mon, out, limit, "siec")
czekaj, sprawdz = q.czekaj, q.sprawdz


def zapisz_ramki(ramki):
    with open(f"{out}/siec-ramki.txt", "w", encoding="utf-8") as f:
        f.writelines(s.opis(r) + "\n" for r in ramki)


g = czekaj(r"K1-GUI: pulpit gotowy", czas=limit * 0.7)
sprawdz("pulpit gotowy (obraz produkcyjny z kartą sieciową)", g)
k = czekaj(r"e1000e [0-9a-f:.]+ (eth\d+): Intel\(R\) PRO/1000 Network Connection", czas=5)
sprawdz("jądro widzi kartę sieciową (e1000e)", k, "brak wiersza sterownika w logu")

# --- krok 5: nothing answers ---
t0 = time.time()
TCP = list(range(1, 1025)) + [1900, 3389, 5353, 5900, 8000, 8080, 8443, 9100, 49152, 62078]
UDP = [7, 53, 67, 68, 69, 123, 137, 138, 161, 500, 1900, 4500, 5353, 5355, 33434]
wyslane = [s.arp_zapytanie(s.IP_K1), s.arp_zapytanie(s.IP_K1, s.MAC_INNY, s.IP_INNY),
           s.ping(s.IP_K1), s.ping(s.IP_K1, s.ROZGLOSZENIE), s.ping(bytes([255] * 4), s.ROZGLOSZENIE),
           s.icmp(13, 0, s.IP_K1), s.icmp(17, 0, s.IP_K1)]
wyslane += [s.tcp_syn(p) for p in TCP] + [s.tcp_syn(p, cel_mac=s.ROZGLOSZENIE) for p in (22, 80, 443)]
wyslane += [s.udp(p) for p in UDP] + [s.udp(p, cel_ip=bytes([255] * 4), cel_mac=s.ROZGLOSZENIE) for p in (67, 68, 137)]
ile = siec.wyslij_wszystkie(wyslane)
# A card nobody switched on does not receive: QEMU takes only the first few frames (its queue) and stops reading
sprawdz(f"sieć testowa wysyła do K1 ARP, ping, ICMP, SYN na {len(TCP)} portów TCP, {len(UDP)} UDP ({len(wyslane)} ramek): "
        f"karta wyłączona — QEMU przyjęło {ile}", ile > 0, "żadna ramka nie doszła do QEMU (kabel?)")
# "nothing happens" for 8 s after the last frame: a slow machine could only make this pass too early, never fail it —
# the same scan runs again in step 6 with a card that has an address
q.czekaj_na(lambda: siec.ramki_od(0), 8)
od_k1 = siec.ramki_od(0)
zapisz_ramki(od_k1)
sprawdz("z zewnątrz nic w K1 nie odpowiada: od startu ani jednej ramki od K1 (karta wyłączona, nikt nie nasłuchuje)",
        not od_k1, f"{len(od_k1)} ramek, pierwsza: {s.opis(od_k1[0]) if od_k1 else ''}")

t_ = q.tekst()
sprawdz("brak 'Kernel panic', błędów, zablokowanych wywołań i odrzuconych komunikatów", "Kernel panic" not in t_ and
        "BŁĄD" not in t_ and "zabronione wywołanie" not in t_ and "odrzucono" not in t_)
q.monitor("quit")
qemu.wait()
siec.zamknij()
sys.exit(0 if all(q.wyniki) else 1)
PY

echo "[k1] test sieci (0.9.0): karta sieciowa, z zewnątrz nic nie odpowiada"
r=0
K1_TESTY_PY="$K1_REPO/testy/qemu" python3 -I "$K1_BUDOWA/siec.py" "$K1_REPO" "$TESTY" "$CZAS" || r=1
if [ $r -eq 0 ]; then info "sieć: wszystko zaliczone"; else echo "[k1] sieć: BŁĄD (log: $TESTY/siec.log, ramki: $TESTY/siec-ramki.txt)"; fi
exit $r
