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


def arp_odpowiedz(mac_nad, ip_nad, mac_cel, ip_cel):
    return eth(mac_cel, mac_nad, ETH_ARP, arp(2, mac_nad, ip_nad, mac_cel, ip_cel))


# --- DHCP (0.9.0 step 6): a home router in a few lines ---
DHCP_CIASTKO = 0x63825363


def dhcp_z_ramki(r):
    """A DHCP message K1 sent (UDP 68 → 67): {xid, typ, ciaddr, opcje, ip_zrodlo, ip_cel, eth_cel} or None."""
    if len(r) < 42 or struct.unpack("!H", r[12:14])[0] != ETH_IP or r[23] != 17:
        return None
    l4 = 14 + (r[14] & 15) * 4
    if struct.unpack("!HH", r[l4:l4 + 4]) != (68, 67):
        return None
    d = r[l4 + 8:]
    if len(d) < 240 or struct.unpack("!I", d[236:240])[0] != DHCP_CIASTKO:
        return None
    o, i = {}, 240
    while i < len(d) and d[i] != 255:
        if d[i] == 0:
            i += 1
            continue
        o[d[i]] = d[i + 2:i + 2 + d[i + 1]]
        i += 2 + d[i + 1]
    return {"op": d[0], "xid": struct.unpack("!I", d[4:8])[0], "ciaddr": d[12:16], "chaddr": d[28:34],
            "typ": o.get(53, b"\0")[0], "opcje": o, "ip_zrodlo": r[26:30], "ip_cel": r[30:34], "eth_cel": r[0:6]}


def dhcp(typ, xid, yiaddr=IP_K1, opcje=None, mac=MAC_K1, maska=bytes([255, 255, 255, 0]), router=IP_SIEC, dzierzawa=30,
         serwer=IP_SIEC, suma_udp=True, czas=None):
    """A reply of the router to K1: OFFER (2), ACK (5), NAK (6). `opcje`: the whole options instead of the usual ones;
    `czas`: the time servers (option 42, 0.9.0 step 8)."""
    if opcje is None:
        opcje = bytes([53, 1, typ, 54, 4]) + serwer
        if typ != 6:
            opcje += bytes([1, 4]) + maska + bytes([3, 4]) + router + bytes([51, 4]) + struct.pack("!I", dzierzawa)
            if czas:
                opcje += bytes([42, 4 * len(czas)]) + b"".join(czas)
        opcje += b"\xff"
    d = bytes([2, 1, 6, 0]) + struct.pack("!IHH", xid, 0, 0) + bytes(4) + yiaddr + IP_SIEC + bytes(4) + mac + bytes(202)
    d += struct.pack("!I", DHCP_CIASTKO) + opcje
    u = struct.pack("!HHHH", 67, 68, 8 + len(d), 0) + d
    cel = yiaddr if yiaddr != bytes(4) else bytes([255] * 4)
    if suma_udp:
        u = u[:6] + struct.pack("!H", _pseudo(IP_SIEC, cel, 17, u) or 0xFFFF) + u[8:]
    return eth(MAC_K1, MAC_SIEC, ETH_IP, ip(IP_SIEC, cel, 17, u))


# --- the time (0.9.0 step 8): SNTP answers of "servers on the internet" — the router answers for every address ---
NTP_OD_1900 = 2208988800


def ntp_z_ramki(r):
    """A question of K1 to a time server (UDP → 123): {serwer, port, znacznik (the transmit timestamp), bajt0} or None."""
    if len(r) < 42 or struct.unpack("!H", r[12:14])[0] != ETH_IP or r[23] != 17:
        return None
    l4 = 14 + (r[14] & 15) * 4
    zp, cp = struct.unpack("!HH", r[l4:l4 + 4])
    d = r[l4 + 8:]
    if cp != 123 or len(d) < 48:
        return None
    return {"serwer": r[30:34], "port": zp, "znacznik": d[40:48], "bajt0": d[0], "dlugosc": len(d)}


def ntp(pytanie, czas, znacznik=None, warstwa=2, skok=0):
    """The server's answer to `pytanie` (ntp_z_ramki) with time `czas` (seconds since 1970)."""
    t = int((czas + NTP_OD_1900) * 2 ** 32) & (2 ** 64 - 1)
    d = bytes([skok << 6 | 4 << 3 | 4, warstwa, 4, 0xE9]) + struct.pack("!II", 0x10, 0x20) + b"K1TS"
    d += struct.pack("!Q", t) + (znacznik if znacznik is not None else pytanie["znacznik"]) + struct.pack("!QQ", t, t)
    u = struct.pack("!HHHH", 123, pytanie["port"], 8 + len(d), 0) + d
    u = u[:6] + struct.pack("!H", _pseudo(pytanie["serwer"], IP_K1, 17, u) or 0xFFFF) + u[8:]
    return eth(MAC_K1, MAC_SIEC, ETH_IP, ip(pytanie["serwer"], IP_K1, 17, u))


class Router:
    """The test network's router: answers ARP for its own address and — once `wlaczony` — DHCP (OFFER to DISCOVER,
    ACK to REQUEST; NAK when `odmowa`). Every DHCP message from K1 is kept in `dhcp` (time, message).
    Time servers (0.9.0 step 8): the router names itself in option 42 and answers every question to port 123, whatever
    the address, as `czas` says: None — silence; "dobry" — the time now; "zle" — every answer wrong in turn (another
    timestamp, a time from 2020, stratum 0, the alarm); "niezgodne" — every server another time (100 s apart);
    a number — that time from every server. Every question is kept in `ntp` (time, question)."""

    def __init__(self, siec, dzierzawa=30):
        self.siec, self.dzierzawa = siec, dzierzawa
        self.wlaczony, self.odmowa = False, False
        self.dhcp, self.ntp, self.czas = [], [], None
        siec.odpowiadacze.append(self)

    def __call__(self, r):
        if len(r) >= 42 and struct.unpack("!H", r[12:14])[0] == ETH_ARP and r[21] == 1 and r[38:42] in (IP_SIEC, IP_INNY):
            mac = MAC_SIEC if r[38:42] == IP_SIEC else MAC_INNY
            return [arp_odpowiedz(mac, r[38:42], r[22:28], r[28:32])]
        p = ntp_z_ramki(r)
        if p is not None and r[26:30] == IP_K1:
            self.ntp.append((time.time(), p))
            return self.czas_odpowiedz(p, len(self.ntp))
        m = dhcp_z_ramki(r)
        if m is None or m["op"] != 1:
            return []
        self.dhcp.append((time.time(), m))
        return self.odpowiedz(m) if self.wlaczony else []

    def odpowiedz(self, m):
        if m["typ"] == 1:
            return [dhcp(2, m["xid"], dzierzawa=self.dzierzawa, czas=[IP_SIEC])]
        if m["typ"] == 3:
            return [dhcp(6 if self.odmowa else 5, m["xid"], yiaddr=bytes(4) if self.odmowa else IP_K1,
                         dzierzawa=self.dzierzawa, czas=[IP_SIEC])]
        return []

    def czas_odpowiedz(self, p, nr):
        teraz = time.time()
        if self.czas is None:
            return []
        if self.czas == "dobry":
            return [ntp(p, teraz)]
        if self.czas == "niezgodne":
            return [ntp(p, teraz + 100 * p["serwer"][3])]
        if self.czas == "zle":
            zle = [ntp(p, teraz, znacznik=bytes(a ^ 1 for a in p["znacznik"])), ntp(p, 1577836800.0),
                   ntp(p, teraz, warstwa=0), ntp(p, teraz, skok=3)]
            return [zle[nr % len(zle)]]
        return [ntp(p, float(self.czas))]

    def ostatni(self, typ):
        return next((m for t, m in reversed(self.dhcp) if m["typ"] == typ), None)


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
        """QEMU options: the card (id "karta": monitor `set_link karta off` pulls the cable out) and its cable."""
        m = ":".join(f"{b:02x}" for b in mac)
        return (f"-netdev dgram,id=siec,local.type=unix,local.path={self.k1},remote.type=unix,remote.path={self.host} "
                f"-device {karta},netdev=siec,mac={m},id=karta")

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
