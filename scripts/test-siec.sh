#!/bin/sh
# Test sieci (0.9.0, ADR 0039; powtarzalny): K1 z kartą sieciową (e1000e — jak X230) podłączoną kablem do „sieci
# testowej” (testy/qemu/k1siec.py: drugi koniec kabla w Pythonie, QEMU -netdev dgram, router z DHCP). Obraz produkcyjny.
#   1. jądro widzi kartę, nadzorca ją włącza i widzi kabel, usługa siec pyta router o adres (DHCP DISCOVER)
#   2. router milczy: z zewnątrz nic w K1 nie odpowiada — zapytania ARP, ping (także rozgłoszeniowy), ICMP, SYN na porty
#      TCP 1–1024 i popularne wyższe, datagramy UDP: od K1 przychodzą tylko jego zapytania DHCP
#   3. router odpowiada — najpierw złymi pakietami DHCP (za krótki, obca transakcja, złe opcje, zła suma, adres 127.0.0.1):
#      odrzucone, usługa działa dalej; potem dobrze: OFFER → REQUEST → ACK → nadzorca ustawia adres („połączono”)
#   4. z adresem (T3 w ADR 0039): ARP tylko o własny adres; ping, ICMP, UDP — cisza; SYN na każdy port → tylko TCP RST
#      (żadnego SYN-ACK: nic nie nasłuchuje); SYN spoza sieci → RST przez router (trasa domyślna)
#   5. odnowienie dzierżawy (30 s): REQUEST prosto do routera, ACK — „dzierżawa odnowiona”
#   6. kabel wyjęty (monitor QEMU set_link) → „kabel odłączony”, bez adresu; włożony → znów adres
#   W logu nie ma adresu (niezmiennik 8).
# Dowody: build/testy/siec.log (log K1), build/testy/siec-ramki.txt (ramki od K1). Wymaga: scripts/zbuduj.sh.
. "$(dirname "$0")/wspolne.sh"
TESTY=$K1_BUDOWA/testy
CZAS=${K1_TEST_CZAS:-360}
mkdir -p "$TESTY"; rm -f "$TESTY"/siec*

cat > "$K1_BUDOWA/siec.py" <<'PY'
import os, re, struct, subprocess, sys, time
sys.path.insert(0, os.environ["K1_TESTY_PY"])
from k1qemu import Maszyna
import k1siec as s
repo, out, limit = sys.argv[1], sys.argv[2], float(sys.argv[3])
log, mon = f"{out}/siec.log", f"{out}/siec.sock"
siec = s.SiecTestowa(out)
router = s.Router(siec, dzierzawa=30)
qemu = subprocess.Popen([f"{repo}/scripts/uruchom-qemu.sh", "--log", log, "--monitor", mon, "--czas", str(int(limit)),
                         "--qemu", siec.qemu("e1000e")])
q = Maszyna(log, mon, out, limit, "siec")
czekaj, sprawdz = q.czekaj, q.sprawdz
TCP = list(range(1, 1025)) + [1900, 3389, 5353, 5900, 8000, 8080, 8443, 9100, 49152, 62078]
UDP = [7, 53, 67, 68, 69, 123, 137, 138, 161, 500, 1900, 4500, 5353, 5355, 33434]
ROZGL = bytes([255] * 4)


def skan():
    """ARP, ping, ICMP, SYN on every port of TCP, UDP — from another computer of the network (10.0.2.2)."""
    r = [s.arp_zapytanie(s.IP_K1), s.arp_zapytanie(s.IP_K1, s.MAC_INNY, s.IP_INNY), s.arp_zapytanie(bytes([10, 0, 2, 99])),
         s.ping(s.IP_K1), s.ping(s.IP_K1, s.ROZGLOSZENIE), s.ping(ROZGL, s.ROZGLOSZENIE), s.icmp(13, 0, s.IP_K1),
         s.icmp(17, 0, s.IP_K1)]
    r += [s.tcp_syn(p) for p in TCP] + [s.tcp_syn(p, cel_mac=s.ROZGLOSZENIE) for p in (22, 80, 443)]
    r += [s.udp(p) for p in UDP] + [s.udp(p, cel_ip=ROZGL, cel_mac=s.ROZGLOSZENIE) for p in (67, 68, 137)]
    return r


def rodzaj(r):
    """What K1 sent, in one word: dhcp, arp-pytanie, arp-odpowiedz, rst, syn-ack, tcp, icmp, inne."""
    if s.dhcp_z_ramki(r):
        return "dhcp"
    typ = struct.unpack("!H", r[12:14])[0]
    if typ == s.ETH_ARP:
        return "arp-pytanie" if r[21] == 1 else "arp-odpowiedz"
    if typ == s.ETH_IP and r[23] == 6:
        f = r[14 + (r[14] & 15) * 4 + 13]
        return "rst" if f & 0x04 else "syn-ack" if f & 0x12 == 0x12 else "tcp"
    return "icmp" if typ == s.ETH_IP and r[23] == 1 else "inne"


def od_k1(t0):
    return [r for r in siec.ramki_od(t0) if r[6:12] == s.MAC_K1]


def odrzucone():
    return sum(int(x) for x in re.findall(r"K1-SIEC: odrzucone pakiety z sieci: (\d+)", q.tekst()))


def polaczono(n=0, czas=60):
    return czekaj(r"K1-NADZORCA: sieć: połączono \(eth0, adres od routera, maska /24, z trasą\)", od=n, czas=czas) and \
        czekaj(r"K1-SIEC: połączono: adres od routera ustawiony \(dzierżawa 30 s\)", od=n, czas=10)


g = czekaj(r"K1-GUI: pulpit gotowy", czas=limit * 0.6)
sprawdz("pulpit gotowy (obraz produkcyjny z kartą sieciową)", g)
sprawdz("jądro widzi kartę (e1000e), nadzorca ją włącza",
        czekaj(r"e1000e [0-9a-f:.]+ eth0: Intel\(R\) PRO/1000 Network Connection", czas=5) and
        czekaj(r"K1-NADZORCA: sieć: karta przewodowa eth0 \(sterownik e1000e\) włączona", czas=30))
sprawdz("kabel podłączony: nadzorca daje usłudze sieci kartę, usługa pyta router (DHCP)",
        czekaj(r"K1-NADZORCA: sieć: kabel podłączony \(eth0\)", czas=60) and
        czekaj(r"K1-SIEC: kabel podłączony \(karta \d+\) — pytam router o adres \(DHCP\)", czas=10))
d = router.ostatni(1) if q.czekaj_na(lambda: router.ostatni(1), 30) else None
sprawdz("DHCP DISCOVER od K1: do wszystkich, z MAC karty, od portu 68 do 67",
        d and d["chaddr"] == s.MAC_K1 and d["eth_cel"] == s.ROZGLOSZENIE and d["ip_cel"] == ROZGL)

# --- 2. the router is silent: nothing in K1 answers ---
t0 = time.time()
wyslane = skan()
ile = siec.wyslij_wszystkie(wyslane)
sprawdz(f"sieć testowa wysłała {ile} z {len(wyslane)} ramek (ARP, ping, ICMP, SYN na {len(TCP)} portów, {len(UDP)} UDP)",
        ile == len(wyslane))
q.czekaj_na(lambda: [r for r in od_k1(t0) if rodzaj(r) != "dhcp"], 8)  # 8 s of nothing but DHCP
inne = [r for r in od_k1(t0) if rodzaj(r) != "dhcp"]
sprawdz("bez adresu nic w K1 nie odpowiada: od K1 tylko zapytania DHCP", not inne,
        f"{len(inne)} ramek, pierwsza: {s.opis(inne[0]) if inne else ''}")

# --- 3. the router answers: bad DHCP packets first, then a good offer ---
xid = router.ostatni(1)["xid"]
zla_suma = bytearray(s.dhcp(2, xid))
zla_suma[14 + 20 + 8 + 44] ^= 1  # a byte of the message (sname): the UDP checksum no longer fits
zle = [s.dhcp(2, xid)[:200],                                                # cut short
       s.dhcp(2, xid ^ 1),                                                  # another transaction
       s.dhcp(2, xid, opcje=bytes([53, 1, 2, 54, 4, 10, 0, 2, 2, 3, 200])),  # an option past the end
       bytes(zla_suma),
       s.dhcp(2, xid, yiaddr=bytes([127, 0, 0, 1]))]                         # an address that cannot be ours
o0 = odrzucone()  # the scan's two datagrams to port 68 got through the filter too (and were rejected)
siec.wyslij_wszystkie(zle)
q.czekaj_na(lambda: odrzucone() - o0 >= len(zle), 20)
sprawdz("NEGATYWNE: 5 złych odpowiedzi DHCP odrzuconych (za krótka, obca transakcja, złe opcje, zła suma UDP, adres "
        "127.0.0.1) — usługa działa dalej, adresu nie ma",
        odrzucone() - o0 == len(zle) and "siec zakończył" not in q.tekst() and "połączono" not in q.tekst(),
        f"odrzucone {odrzucone() - o0}")
n = q.ile(r"K1-NADZORCA: sieć: połączono")
router.wlaczony = True
siec.wyslij(s.dhcp(2, xid, dzierzawa=30))  # the offer to the last DISCOVER (the next one could be a minute away)
r3 = None
sprawdz("router: OFFER → K1: REQUEST o ten adres → ACK → nadzorca ustawia adres i trasę („połączono”)",
        polaczono(n) and (r3 := router.ostatni(3)) and r3["opcje"].get(50) == s.IP_K1 and r3["opcje"].get(54) == s.IP_SIEC)
t_adres = time.time()

# --- 4. with the address: ARP for our address only, RST for every SYN, nothing else ---
t0 = time.time()
siec.wyslij_wszystkie(skan())
q.czekaj_na(lambda: sum(1 for r in od_k1(t0) if rodzaj(r) == "rst") >= len(TCP), 90)
q.czekaj_na(lambda: False, 3)  # whatever comes late
rodzaje = {}
for r in od_k1(t0):
    rodzaje[rodzaj(r)] = rodzaje.get(rodzaj(r), 0) + 1
arp = [r for r in od_k1(t0) if rodzaj(r) == "arp-odpowiedz"]
sprawdz(f"z adresem: ARP tylko o własny adres (od K1: {rodzaje})",
        len(arp) == 2 and all(r[28:32] == s.IP_K1 and r[22:28] == s.MAC_K1 for r in arp))
sprawdz(f"z adresem: SYN na {len(TCP)} portów → {rodzaje.get('rst', 0)} × TCP RST, żadnego SYN-ACK (nic nie nasłuchuje)",
        rodzaje.get("rst", 0) == len(TCP) and "syn-ack" not in rodzaje and "tcp" not in rodzaje)
sprawdz("z adresem: ping, ICMP, UDP — cisza (jądro nie wysyła ICMP: ani „echo”, ani „port niedostępny”)",
        "icmp" not in rodzaje and "inne" not in rodzaje)
t0 = time.time()
siec.wyslij(s.tcp_syn(443, ip_nad=bytes([93, 184, 216, 34])))
q.czekaj_na(lambda: [r for r in od_k1(t0) if rodzaj(r) == "rst"], 20)
rst = [r for r in od_k1(t0) if rodzaj(r) == "rst"]
sprawdz("trasa domyślna: SYN spoza sieci → RST przez router (MAC routera)",
        rst and rst[0][0:6] == s.MAC_SIEC and rst[0][30:34] == bytes([93, 184, 216, 34]))

# --- 5. renewing the lease (30 s: T1 = 15 s) ---
sprawdz("odnowienie dzierżawy: REQUEST prosto do routera (od adresu K1), ACK — „dzierżawa odnowiona (30 s)”",
        czekaj(r"K1-SIEC: dzierżawa odnowiona \(30 s\)", czas=60) and
        any(m["typ"] == 3 and m["ciaddr"] == s.IP_K1 and m["ip_zrodlo"] == s.IP_K1 and m["ip_cel"] == s.IP_SIEC and
            m["eth_cel"] == s.MAC_SIEC for t, m in router.dhcp if t > t_adres))

# --- 6. the cable out and in ---
n = q.ile(r"K1-NADZORCA: sieć: kabel odłączony")
q.monitor("set_link karta off")
sprawdz("kabel wyjęty: nadzorca zabiera adres, usługa sieci zapomina dzierżawę",
        czekaj(r"K1-NADZORCA: sieć: kabel odłączony \(eth0\)", od=n, czas=30) and
        czekaj(r"K1-SIEC: kabel odłączony — bez adresu", czas=10))
n = q.ile(r"K1-NADZORCA: sieć: połączono")
q.monitor("set_link karta on")
sprawdz("kabel włożony: znów DHCP i adres", polaczono(n, czas=90))
t0 = time.time()
siec.wyslij(s.arp_zapytanie(s.IP_K1))
sprawdz("…i K1 znów odpowiada na ARP o swój adres",
        q.czekaj_na(lambda: [r for r in od_k1(t0) if rodzaj(r) == "arp-odpowiedz"], 20))

with open(f"{out}/siec-ramki.txt", "w", encoding="utf-8") as f:
    f.writelines(s.opis(r) + "\n" for r in siec.ramki_od(0))
t_ = q.tekst()
sprawdz("w logu nie ma adresu K1 ani routera (niezmiennik 8)", "10.0.2.15" not in t_ and "10.0.2.2" not in t_)
sprawdz("brak 'Kernel panic', błędów, zablokowanych wywołań, odrzuconych komunikatów i restartu usługi sieci",
        "Kernel panic" not in t_ and "BŁĄD" not in t_ and "zabronione wywołanie" not in t_ and "odrzucono" not in t_ and
        "siec zakończył" not in t_)
q.monitor("quit")
qemu.wait()
siec.zamknij()
sys.exit(0 if all(q.wyniki) else 1)
PY

echo "[k1] test sieci (0.9.0): karta, z zewnątrz nic nie odpowiada, adres od routera (DHCP), kabel"
r=0
K1_TESTY_PY="$K1_REPO/testy/qemu" python3 -I "$K1_BUDOWA/siec.py" "$K1_REPO" "$TESTY" "$CZAS" || r=1
if [ $r -eq 0 ]; then info "sieć: wszystko zaliczone"; else echo "[k1] sieć: BŁĄD (log: $TESTY/siec.log, ramki: $TESTY/siec-ramki.txt)"; fi
exit $r
