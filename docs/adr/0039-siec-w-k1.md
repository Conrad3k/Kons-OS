# 0039. Sieć w K1: IPv4 po kablu, gniazda tylko u nadzorcy, nic nie odpowiada z zewnątrz

- Status: Przyjęty, wdrożony w 0.9.0 (krok 5 planu 0.9.0: jądro i test; krok 6: kabel i adres od routera)
- Data: 2026-11-13
- Dotyczy: SPEC §3 (niezmiennik 3: jądro bez `INET` do 0.9), §4 (usługa sieci), ADR 0007 (klatki), ADR 0003 (LSM);
  decyzje D0 i T1–T6 planu 0.9.0; RYZYKA R66, R67

## Kontekst
Do 0.8.7 jądro K1 miało tylko `CONFIG_NET` (potrzebne filtrowi seccomp) i gniazda UNIX (kanały usług) — bez stosu
TCP/IP i bez sterowników kart sieciowych. Temat 0.9 to sieć: najpierw kabel, adres od routera (DHCP) i zegar z internetu;
Wi-Fi w następnej wersji. Właściciel zdecydował (D0), że sterowniki kart bierzemy z jądra Linux, nie piszemy własnych.

Sieć to nowa powierzchnia ataku, której do tej pory nie było: stos IPv4 jądra i sterowniki kart przyjmują dane od
każdego, kto jest w tej samej sieci. Zanim cokolwiek w K1 pobierze adres, trzeba ustalić, kto może dotknąć sieci, co
jądro odpowiada na pakiety z zewnątrz i jak to sprawdzamy.

## Decyzja

**T1 — uprawnienia.** Gniazdo sieciowe otwiera tylko **nadzorca** (root, bez klatki) — jak urządzenia dyskowe i USB.
Usługa „siec” (krok 6) działa w klatce bez uprawnień: dostaje od nadzorcy deskryptor **gniazda pakietowego** na jednej
karcie (`AF_PACKET`; otwarcie wymaga `CAP_NET_RAW` — `net/packet/af_packet.c: packet_create`, deskryptor po przekazaniu
działa bez niego), czyta i liczy wszystko, co przychodzi z sieci (DHCP, później czas), a o rzeczy wymagające uprawnień —
adres, maska, brama (`CAP_NET_ADMIN`, `net/ipv4/devinet.c`), zegar — prosi nadzorcę komunikatem; nadzorca sprawdza
zakresy i robi to sam. Żadna klatka nie ma wywołania `socket` ani `socketpair` (seccomp, `src/wspolne/k1_klatka.c`) —
także usługa „siec”. Żadna aplikacja nie dostaje w 0.9 dostępu do sieci; to przyjdzie z przeglądarką w strefie (0.10).

**T2 — tylko IPv4.** `CONFIG_INET` bez `IPV6`. Domowe routery dają adres IPv4; IPv6 to drugi stos, autokonfiguracja
z ogłoszeń routera (każdy w sieci może je wysłać) i więcej kodu jądra przyjmującego obce pakiety. IPv6 — „Później”,
własnym ADR-em. Wyłączone także: multicast (`IP_MULTICAST`), routing zaawansowany, IPsec (`XFRM_USER`),
autokonfiguracja z linii poleceń (`IP_PNP`), `INET_DIAG`; włączone `SYN_COOKIES` (zalecenie KSPP/CIS).

**T3 — co znaczy „nic nie odpowiada”.**
1. Nic w K1 nie nasłuchuje: żaden proces nie ma gniazda TCP/UDP (klatki bez `socket` — test `t_proby`; nadzorca otwiera
   tylko gniazdo pakietowe dla usługi „siec” i gniazdo netlink).
2. **Jądro nie wysyła żadnego ICMP**: każdy typ ICMP (0–18) przechodzi przez globalny limit (`icmp_ratemask` = wszystkie
   bity), a limit wynosi 0 na sekundę (`icmp_msgs_per_sec` = 0, `icmp_msgs_burst` = 0) — `net/ipv4/icmp.c:
   icmpv4_mask_allow`, `icmp_global_allow` (licznik zaczyna od 0 i nigdy nie rośnie). Skutek: brak odpowiedzi na ping
   (także `icmp_echo_ignore_all`), na „znacznik czasu”, brak „port niedostępny” na datagram UDP. K1 jako klient tego nie
   potrzebuje (wyjątek jądra: „fragmentacja potrzebna” dotyczy tylko przekazywania pakietów, którego K1 nie robi).
3. **Zostaje odpowiedź jądra TCP RST** na próbę połączenia z zamkniętym portem. Bez zapory nie da się jej wyłączyć
   (żadnego sysctl). Zapory (netfilter) nie włączamy w 0.9: to duża część jądra z własnym językiem reguł, a RST mówi
   tylko „tu jest komputer i nic nie nasłuchuje” — to samo, co odpowiedź ARP, bez której sieć nie działa. Wracamy do
   tego w 0.10, gdy strefy będą potrzebować przekazywania pakietów (RYZYKA R67).
4. **ARP**: odpowiedź tylko na pytanie o własny adres na tej samej karcie (`arp_ignore=1`, `arp_announce=2`).
5. Pozostałe ustawienia (wszystkie wymagane — init zatrzymuje start, gdy któreś się nie uda; niezmiennik 10): bez
   przekazywania pakietów (`ip_forward=0`), bez przekierowań ICMP (`accept_redirects`, `secure_redirects`,
   `send_redirects` = 0), bez trasowania od nadawcy (`accept_source_route=0`), ścisłe sprawdzanie nadawcy (`rp_filter=1`),
   `tcp_syncookies=1`, `tcp_rfc1337=1`, `icmp_ignore_bogus_error_responses=1`. Każde dla `all` i `default` (`default`
   dociera też do kart znalezionych przed initem: `devinet_copy_dflt_conf`). Bez `log_martians` — log z adresami
   (niezmiennik 8).

**T4 — bez przestrzeni nazw użytkownika** (`USER_NS` zostaje wyłączone). T1 ich nie potrzebuje; strefy w 0.10 to
mikro-maszyny KVM, nie przestrzenie nazw.

**T6 — sterowniki kart przewodowych: szeroko, wbudowane w jądro.** K1 nie wczytuje modułów (niezmiennik 3), więc każda
rodzina kart jest wbudowana: Intel (e1000e, e1000, igb, igc, e100), Realtek (r8169, 8139), Broadcom (tg3), Qualcomm
Atheros (alx, atl1c, atl1e, atl1), Marvell (sky2, skge), NVIDIA (forcedeth), JMicron (jme), VIA (via-rhine), maszyny
wirtualne (virtio-net, vmxnet3; VirtualBox — e1000) i popularne przejściówki USB–Ethernet (Realtek RTL8152/8153/8156,
ASIX AX88772/AX88179, CDC ECM i NCM). Sterownik karty, której nie ma w komputerze, nigdy się nie uruchamia — jądro dobiera
go po identyfikatorze PCI. Wyjątek to USB: podłączone urządzenie samo wybiera sterownik, stąd tylko popularne rodziny
(RYZYKA R66). Bez kart serwerowych 10G+ i bardzo starych (ISA, Token Ring…). Koszt: jądro +1,3 MiB (bzImage 5,0 → 6,3
MiB; `k1.efi` 7,1 → 8,4 MiB; miejsce A/B ma 128 MiB). Karta, której K1 nie zna, to jedna linia w `jadro/k1.config`
w następnej wersji (krok 7 pokaże jej identyfikator). Pliki od producenta (firmware): K1 ich nie ma (brak `/lib/firmware`,
`EXTRA_FIRMWARE=""`); karty przewodowe z listy działają bez nich (np. r8169 prosi o plik z łatką, ale bez niego działa
dalej) — pliki wejdą razem z Wi-Fi, osobnym ADR-em.

**Test z zewnątrz.** `scripts/test-siec.sh`: maszyna QEMU z kartą e1000e podłączoną gniazdem datagramowym
(`-netdev dgram`) do „sieci testowej” w Pythonie (`testy/qemu/k1siec.py`), która wysyła ARP, ping (też rozgłoszeniowy),
ICMP, SYN na porty TCP 1–1024 i popularne wyższe, datagramy UDP i zapisuje każdą ramkę od K1. Krok 5 (karta jeszcze
wyłączona): K1 nie wysyła żadnej ramki. Krok 6 (karta z adresem od routera): odpowiedzi dokładnie takie, jak w T3 —
ARP na własny adres, RST na SYN, nic więcej. Testy w systemie: wszystkie klatki — `socket(AF_INET)`, `AF_PACKET`,
`AF_NETLINK`, `socketpair(AF_INET)` zablokowane (`t_proby`); jądro ma IPv4, nie ma IPv6 (`t_syscalls`); sysctl sieci
(`t_sysctl`).

## Konsekwencje
- Niezmiennik 3 (SPEC §3, skill): „bez `INET` (do 0.9)” → „IPv4 bez IPv6 i bez zapory; gniazda sieciowe tylko
  u nadzorcy” [korekta: ADR 0039].
- Nowa powierzchnia ataku: stos IPv4 jądra i sterowniki kart (RYZYKA R67), przejściówki USB–Ethernet (R66).
- Dotychczasowe testy QEMU zostają bez karty sieciowej (`-nic none`); sieć tylko w `test-siec.sh`.
- Do rozważenia w 0.10 (strefy): zapora albo inny sposób, żeby jądro nie odpowiadało RST; IPv6.
