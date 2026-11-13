"""K1 — sieć testowa (0.9.0, ADR 0039): drugi koniec kabla karty sieciowej maszyny QEMU, w Pythonie.

QEMU łączy kartę (np. e1000e) z gniazdem datagramowym UNIX (`-netdev dgram`, od QEMU 7.2): każdy datagram to jedna
ramka Ethernet. Sieć testowa zbiera każdą ramkę, którą K1 wyśle, i wysyła własne — z zewnątrz, tak jak router albo
inny komputer w tej samej sieci. Nic tu nie zależy od sieci hosta.

    s = SiecTestowa(katalog)                 # gniazdo hosta, przed startem QEMU
    qemu(..., "--qemu", s.qemu("e1000e"))    # opcje QEMU: karta + połączenie
    s.wyslij(arp_zapytanie(...)); s.ramki_od(t0)
"""
import os
import socket
import struct
import threading
import time

MAC_K1 = bytes.fromhex("5254004b3101")      # the card of the machine under test (-device …,mac=)
MAC_SIEC = bytes.fromhex("525400ee0002")    # the test network ("router")
MAC_INNY = bytes.fromhex("525400ee0003")    # another computer in the same network
ROZGLOSZENIE = b"\xff" * 6
IP_SIEC = bytes([10, 0, 2, 2])
IP_K1 = bytes([10, 0, 2, 15])
IP_INNY = bytes([10, 0, 2, 3])
ETH_IP, ETH_ARP = 0x0800, 0x0806


def suma(dane):
    """The internet checksum (RFC 1071)."""
    if len(dane) % 2:
        dane += b"\0"
    s = sum(struct.unpack(f"!{len(dane) // 2}H", dane))
    while s >> 16:
        s = (s & 0xFFFF) + (s >> 16)
    return (~s) & 0xFFFF


def eth(cel, zrodlo, typ, tresc):
    return cel + zrodlo + struct.pack("!H", typ) + tresc


def ip(zrodlo, cel, protokol, tresc, ttl=64):
    n = struct.pack("!BBHHHBBH4s4s", 0x45, 0, 20 + len(tresc), 0x4b31, 0x4000, ttl, protokol, 0, zrodlo, cel)
    return n[:10] + struct.pack("!H", suma(n)) + n[12:] + tresc


def _pseudo(zrodlo, cel, protokol, tresc):
    return suma(zrodlo + cel + struct.pack("!BBH", 0, protokol, len(tresc)) + tresc)


def arp(oper, mac_nad, ip_nad, mac_cel, ip_cel):
    return struct.pack("!HHBBH6s4s6s4s", 1, ETH_IP, 6, 4, oper, mac_nad, ip_nad, mac_cel, ip_cel)


def arp_zapytanie(o_ip, mac=MAC_SIEC, ip_nad=IP_SIEC):
    """ARP "who has o_ip?" to everyone."""
    return eth(ROZGLOSZENIE, mac, ETH_ARP, arp(1, mac, ip_nad, b"\0" * 6, o_ip))


def ping(cel_ip, cel_mac=MAC_K1, nr=1, mac=MAC_SIEC, ip_nad=IP_SIEC):
    t = struct.pack("!BBHHH", 8, 0, 0, 0x4b31, nr) + b"K1 test" * 4
    t = t[:2] + struct.pack("!H", suma(t)) + t[4:]
    return eth(cel_mac, mac, ETH_IP, ip(ip_nad, cel_ip, 1, t))


def icmp(typ, kod, cel_ip, cel_mac=MAC_K1, mac=MAC_SIEC, ip_nad=IP_SIEC):
    """Any other ICMP message (timestamp request 13, address mask 17, …)."""
    t = struct.pack("!BBHI", typ, kod, 0, 0x4b310001) + b"\0" * 12
    t = t[:2] + struct.pack("!H", suma(t)) + t[4:]
    return eth(cel_mac, mac, ETH_IP, ip(ip_nad, cel_ip, 1, t))


def tcp_syn(port, cel_ip=IP_K1, cel_mac=MAC_K1, mac=MAC_SIEC, ip_nad=IP_SIEC, zrodlowy=40000):
    t = struct.pack("!HHIIBBHHH", zrodlowy, port, 0x4b310000 + port, 0, 5 << 4, 0x02, 64240, 0, 0)
    t = t[:16] + struct.pack("!H", _pseudo(ip_nad, cel_ip, 6, t)) + t[18:]
    return eth(cel_mac, mac, ETH_IP, ip(ip_nad, cel_ip, 6, t))


def udp(port, tresc=b"K1 test", cel_ip=IP_K1, cel_mac=MAC_K1, mac=MAC_SIEC, ip_nad=IP_SIEC, zrodlowy=40000):
    t = struct.pack("!HHHH", zrodlowy, port, 8 + len(tresc), 0) + tresc
    s = _pseudo(ip_nad, cel_ip, 17, t) or 0xFFFF
    t = t[:6] + struct.pack("!H", s) + t[8:]
    return eth(cel_mac, mac, ETH_IP, ip(ip_nad, cel_ip, 17, t))


def opis(r):
    """A frame in one line, for the log of the test."""
    if len(r) < 14:
        return f"ramka {len(r)} B"
    typ = struct.unpack("!H", r[12:14])[0]
    if typ == ETH_ARP and len(r) >= 42:
        oper = struct.unpack("!H", r[20:22])[0]
        return f"ARP {'zapytanie' if oper == 1 else 'odpowiedź' if oper == 2 else oper} " \
               f"{socket.inet_ntoa(r[28:32])} → {socket.inet_ntoa(r[38:42])}"
    if typ == ETH_IP and len(r) >= 34:
        p = r[23]
        nazwa = {1: "ICMP", 6: "TCP", 17: "UDP", 2: "IGMP"}.get(p, f"IP/{p}")
        x = f"{nazwa} {socket.inet_ntoa(r[26:30])} → {socket.inet_ntoa(r[30:34])}"
        l4 = 14 + (r[14] & 15) * 4
        if p in (6, 17) and len(r) >= l4 + 4:
            x += " porty %d → %d" % struct.unpack("!HH", r[l4:l4 + 4])
        if p == 6 and len(r) >= l4 + 14:
            x += f" flagi {r[l4 + 13]:#04x}"
        if p == 1 and len(r) > l4:
            x += f" typ {r[l4]}"
        return x
    return f"ramka typu {typ:#06x}, {len(r)} B"


class SiecTestowa:
    """One cable: the host end (our socket) and the K1 end (QEMU's socket), both in `katalog`."""

    def __init__(self, katalog, nazwa="siec"):
        self.host = os.path.join(katalog, f"{nazwa}-host.sock")
        self.k1 = os.path.join(katalog, f"{nazwa}-k1.sock")
        for p in (self.host, self.k1):
            try:
                os.unlink(p)
            except OSError:
                pass
        self.s = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
        self.s.bind(self.host)
        self.s.settimeout(0.2)
        self.ramki = []                 # (time, frame) of everything K1 sent
        self.odpowiadacze = []          # f(frame) -> list of frames to send back (a router: DHCP, ARP; step 6)
        self.zamek = threading.Lock()
        self.dziala = True
        self.watek = threading.Thread(target=self._odbior, daemon=True)
        self.watek.start()

    def qemu(self, karta="e1000e", mac=MAC_K1):
        """QEMU options: the card and its cable to this test network."""
        m = ":".join(f"{b:02x}" for b in mac)
        return (f"-netdev dgram,id=siec,local.type=unix,local.path={self.k1},remote.type=unix,remote.path={self.host} "
                f"-device {karta},netdev=siec,mac={m}")

    def _odbior(self):
        while self.dziala:
            try:
                r = self.s.recv(65536)
            except socket.timeout:
                continue
            except OSError:
                return
            with self.zamek:
                self.ramki.append((time.time(), r))
            for f in self.odpowiadacze:
                for odp in f(r) or ():
                    self.wyslij(odp)

    def wyslij(self, ramka, czas=2.0):
        """One frame to K1; False when QEMU did not take it within `czas` s — QEMU reads the cable only while the
        card in the machine receives (a card K1 has not switched on takes nothing: the queue of the socket fills)."""
        koniec = time.time() + czas
        while True:
            try:
                self.s.sendto(ramka, self.k1)
                return True
            except socket.timeout:
                if time.time() >= koniec:
                    return False
            except OSError:
                return False

    def wyslij_wszystkie(self, ramki, czas=2.0):
        """Frames in order until one is not taken: how many went."""
        for i, r in enumerate(ramki):
            if not self.wyslij(r, czas):
                return i
        return len(ramki)

    def ramki_od(self, t0=0.0):
        with self.zamek:
            return [r for t, r in self.ramki if t >= t0]

    def zamknij(self):
        self.dziala = False
        self.watek.join(1)
        self.s.close()
        for p in (self.host, self.k1):
            try:
                os.unlink(p)
            except OSError:
                pass
