# Komponent: siec (adres od routera)

| | |
|---|---|
| **Status** | PRZETESTOWANE w QEMU (0.9.0 krok 6: karta e1000e kablem do sieci testowej — `scripts/test-siec.sh`; testy na hoście `test_siec`, fuzzing `fuzz_dhcp`); na X230 i IdeaPadzie z kablem do routera — test właściciela po wydaniu 0.9.0 |
| **Pliki** | `src/siec/main.c` (usługa: kanał do nadzorcy, gniazdo pakietowe, log), `dhcp.{c,h}` (pakiety DHCP: budowa i sprawdzanie), `klient.{c,h}` (klient DHCP: stany i czasy); `src/nadzorca/siec.c` (karty, kabel, gniazdo pakietowe, ustawienie adresu); `src/wspolne/k1_siec.{c,h}` (sprawdzanie adresów, MAC), `k1_kom_siec.c` (komunikaty) |
| **Decyzje** | ADR 0039 (T1: uprawnienia, T2: IPv4, T3: nic nie odpowiada, T6: sterowniki) |

## Cel
Gdy w komputerze jest karta przewodowa i włożony kabel do routera, K1 sam dostaje adres (DHCP) i trasę do internetu;
po wyjęciu kabla adres znika. Nic poza tym: żadna aplikacja nie dostaje w 0.9 sieci, nic w K1 nie nasłuchuje.

## Granica zaufania
Wszystko, co przychodzi z sieci, jest wrogie: każdy w tej samej sieci może wysłać pakiet udający router. Dlatego
podział (ADR 0039, T1):
- **nadzorca** (root) robi tylko to, co wymaga uprawnień: znajduje karty (sysfs `class/net`, tylko przewodowe —
  typ Ethernet, bez Wi-Fi), włącza je, widzi kabel (wiadomości jądra rtnetlink budzą go, prawdą jest `carrier` w sysfs),
  otwiera **gniazdo pakietowe** na karcie z kablem (`CAP_NET_RAW`) z filtrem, który przepuszcza tylko IPv4 UDP na port
  68 (odpowiedzi DHCP), **zablokowanym** (`SO_LOCK_FILTER`), i ustawia adres, o który prosi siec (`CAP_NET_ADMIN`,
  rtnetlink `RTM_NEWADDR`, `RTM_NEWROUTE`) — po sprawdzeniu go pole po polu (`k1_siec_adres_dobry`: nie 0.x, 127.x,
  224.x i wyżej, nie pierwszy i ostatni adres sieci, maska /8…/30, router w tej samej sieci);
- **siec** (UID 108, klatka) czyta i liczy wszystko, co przychodzi z sieci: każdy pakiet sprawdzany jak komunikat
  (`k1_dhcp_czytaj`: nagłówek IP i jego suma, fragmenty, porty i suma UDP, nagłówek DHCP, transakcja i MAC, każda opcja
  z długością, adres) — zły pakiet jest liczony i pomijany, usługa działa dalej. Nie ma żadnego gniazda poza tym jednym
  (seccomp bez `socket`, `setsockopt`, `bind`, `ioctl`) i nie może sama ustawić adresu.
Adresu nie ma w żadnym logu (niezmiennik 8) — tylko „połączono”, maska i czas dzierżawy.

## Uprawnienia
| UID | seccomp (poza bazą) | Landlock | cgroup | deskryptory |
|---|---|---|---|---|
| 108 | `getrandom` (numery transakcji DHCP, bity pytań o czas) | brak ścieżek | 8 MiB, 1 proces; 16 plików | fd 3: kanał do nadzorcy (`KANAL_SIECI`); gniazdo pakietowe z `SIEC_KABEL` (jedno naraz); do 6 gniazd UDP do serwerów czasu z `SIEC_GNIAZDO` (jedna runda naraz) |

## Przebieg
1. Start systemu: nadzorca uruchamia siec (`PRZY_STARCIE`, ponownie po awarii) i otwiera gniazdo rtnetlink
   (`RTMGRP_LINK`). Co zmianę kart (i co 10 s jako zapas) przegląda `class/net`: nową kartę przewodową włącza
   (`RTM_NEWLINK`, `IFF_UP`) — log „sieć: karta przewodowa eth0 (sterownik e1000e, PCI 8086:10d3) włączona”. Włączona karta bez
   adresu niczego nie wysyła (bez IPv6 i multicastu).
2. Kabel włożony (`carrier` = 1) na pierwszej karcie: nadzorca otwiera gniazdo pakietowe i wysyła siec `SIEC_KABEL`
   (MAC karty + gniazdo). Jedna karta naraz; druga dostaje swoją kolej, gdy pierwsza straci kabel.
3. siec (`klient.c`, RFC 2131): DISCOVER do wszystkich → OFFER → REQUEST o ten adres → ACK → `SIEC_ADRES` do nadzorcy
   (adres, maska, router, dzierżawa) → nadzorca ustawia adres i trasę domyślną, odpowiada `SIEC_WYNIK`, log
   „sieć: połączono (eth0, adres od routera, maska /24, z trasą)”. Bez odpowiedzi: powtórki po 4, 8, 16, 32, 64 s
   i dalej co 64 s; po 4 zapytaniach log „router nie odpowiada”. REQUEST bez odpowiedzi 4 razy albo NAK — od nowa.
4. Dzierżawa: po T1 (połowa, albo T1 od routera) REQUEST prosto do routera z adresu K1; po T2 (7/8) do wszystkich;
   ACK — „dzierżawa odnowiona”, inny adres — nowe `SIEC_ADRES`; NAK albo koniec dzierżawy — `SIEC_ZWOLNIJ`
   (nadzorca zabiera adres) i od nowa.
5. Kabel wyjęty albo karta zniknęła: nadzorca zabiera adres (`RTM_DELADDR`; trasy przez nią znikają z nim) i wysyła
   `SIEC_ROZLACZONO`; siec zamyka gniazdo i zapomina dzierżawę. Nowy kanał (siec uruchomiona ponownie) — to samo, potem
   znów `SIEC_KABEL`.

## Komunikaty
| Typ | Kierunek | Pola |
|---|---|---|
| `SIEC_KABEL` | nadzorca → siec | obiekt = karta (numer jądra), a, b = MAC karty (`k1_siec_mac_do_pol`); + 1 fd: gniazdo pakietowe |
| `SIEC_ROZLACZONO` | nadzorca → siec | obiekt = karta, a = 1 kabel wyjęty / 2 karta zniknęła |
| `SIEC_ADRES` | siec → nadzorca | obiekt = karta, a = adres, b = maska /8…/30, c = router (0: brak), d = dzierżawa 10 s…2³¹−1 s |
| `SIEC_ZWOLNIJ` | siec → nadzorca | obiekt = karta — zabierz adres |
| `SIEC_WYNIK` | nadzorca → siec | obiekt = karta, a = prośba (`SIEC_ADRES`, `SIEC_ZWOLNIJ`), b = 0 albo `K1_SIEC_BLAD_*` |

Każde pole sprawdza `k1_kom_siec.c`; prośbę o adres dla innej karty niż ta z `SIEC_KABEL` nadzorca odrzuca
(`K1_SIEC_BLAD_KARTA`).

## Co widzi użytkownik (0.9.0 krok 7)
Nadzorca (`siec_stan.c`) mówi pulpitowi (fd 5) i Ustawieniom (fd 4), jak wygląda sieć — tylko liczby, nic z samej sieci:
stan dla ikony na górnym pasku (brak karty / kabel odłączony / łączę / połączono) i listę kart sieciowych komputera.
Lista: karty przewodowe, których używa nadzorca, potem każde urządzenie PCI klasy 02 (sieć) z `bus/pci/devices` —
także bez sterownika w K1 (np. Wi-Fi), żeby właściciel mógł przeczytać jego identyfikator. Dla karty przewodowej ze
sterownikiem także szczegóły z sysfs: sterownik, prędkość i dupleks łącza (`speed`, `duplex` — tylko z kablem), MAC,
a gdy połączono — adres, maska i router (to, co nadzorca sam ustawił). Nazwy kart (np. „Intel 82579LM”) to tabela
`K1_KARTY_SIECIOWE` w Ustawieniach (`src/aplikacje/ustawienia/siec.c`); karta spoza tabeli — rodzaj i identyfikator.

| Typ | Kierunek | Pola |
|---|---|---|
| `SIEC_STAN` | nadzorca → gui, Ustawienia | a = `K1_SIEC_*` stan 0–3, b = kart na liście 0–8, c = połączono od (CLOCK_MONOTONIC, s; > 0 tylko „połączono”), d = zegar z internetu od (s; 0: jeszcze nie) |
| `SIEC_KARTA` | nadzorca → Ustawienia | obiekt = pozycja 0–7, a = identyfikator (producent << 16 \| urządzenie; PCI albo USB), b = przewodowa / Wi-Fi / inna, c = `K1_SIEC_KF_*` (USB, sterownik, kabel, używana — kabel i używana tylko przewodowa ze sterownikiem) |
| `SIEC_SZCZEGOLY` | nadzorca → Ustawienia | obiekt = pozycja, a = prędkość 0–800000 Mb/s (0: brak łącza), b = dupleks (0 nieznany, 1 pół, 2 pełny), c, d = MAC (`k1_siec_mac_dobry`) |
| `SIEC_STEROWNIK` | nadzorca → Ustawienia | obiekt = pozycja, a…e = nazwa sterownika: 1–20 znaków `a-z 0-9 _ -`, reszta zera (`k1_siec_sterownik_dobry`) |
| `SIEC_ADRESY` | nadzorca → Ustawienia | obiekt = pozycja (tylko „połączono”), a = adres, b = maska, c = router — `k1_siec_adres_dobry` |

Po `SIEC_STAN` (b = n) idą `SIEC_KARTA` 0…n−1, każda ze swoimi szczegółami. Adres widać tylko w oknie Ustawień —
w logu dalej go nie ma (niezmiennik 8).

## Zegar z internetu (0.9.0 krok 8, T5)
Zwykły protokół czasu (SNTP) nie jest podpisany (L17), więc K1 pyta naraz do 6 serwerów i bierze czas tylko, gdy co
najmniej dwie odpowiedzi — i więcej niż połowa wszystkich — zgadzają się co do 2 s (`src/siec/czas.c`). Serwery: te,
które poda router (opcja DHCP 42, najwyżej 2), i stałe adresy dwóch firm na zmianę (`K1_SERWERY_CZASU`: time.google.com
216.239.35.0 i .4, time.cloudflare.com 162.159.200.1 i .123 — adresy anycast; bez nazw, bo K1 nie ma DNS).
1. Adres ustawiony → runda: siec prosi nadzorcę o gniazdo do każdego serwera (`SIEC_CZAS_GNIAZDO`); nadzorca otwiera
   gniazdo UDP i łączy je z tym adresem, port 123 (`connect`: losowy port, odpowiedzi od kogokolwiek innego nie
   docierają) i oddaje je w `SIEC_GNIAZDO`. Najwyżej 18 gniazd na minutę.
2. Pytanie (`sntp.c`): wersja 4, tryb klienta, 64 losowe bity w polu „czas wysłania” — serwer odsyła je w polu
   „origin”; odpowiedź bez nich nie jest do nas. Każde pole sprawdzane: długość 48–1024 B, wersja 3/4, tryb serwera,
   warstwa 1–15 (0 = „kiss of death”), bez alarmu, czasy odbioru i wysłania niezerowe, opóźnienie/2 + rozrzut < 1 s,
   czas 2026–2100 (przejście licznika NTP w 2036 — era 1); droga tam i z powrotem ≤ 4 s.
3. Koniec rundy (wszyscy odpowiedzieli albo 8 s): zgodne → `SIEC_ZEGAR` (sekundy, nanosekundy, ile zgodnych) — połowa
   drogi doliczona; niezgodne albo żadnej dobrej odpowiedzi → zegar bez zmian, następna runda po 15, 30, 60 s … do 1 h.
   Po dobrej rundzie — co 6 h, póki jest adres; kabel wyjęty — koniec rund.
4. nadzorca (`src/nadzorca/siec_zegar.c`): czas nie sprzed budowy tego K1 (`K1_CZAS_BUDOWY`) i najwyżej raz na minutę,
   inaczej `SIEC_WYNIK` z `K1_SIEC_BLAD_CZAS` (siec spróbuje później). Różnica < 1 s — „zegar zgodny”. Inaczej
   `clock_settime` (CAP_SYS_TIME), zegar sprzętowy (`/dev/rtc0`, `RTC_SET_TIME`, UTC — następny start ma dobrą godzinę
   bez sieci) i `ZEGAR_PRZESUNIETY` do skarbca (Kosz zachowuje wiek plików). Log: „zegar ustawiony z internetu
   (przesunięcie +N s, zgodnych źródeł n)”. Adresów serwerów nie ma w logu.

| Typ | Kierunek | Pola |
|---|---|---|
| `SIEC_CZAS_GNIAZDO` | siec → nadzorca | obiekt = karta, a = serwer (`k1_siec_serwer_dobry`: nie 0.x, 127.x, 224.x i wyżej), b = źródło 0–5 |
| `SIEC_GNIAZDO` | nadzorca → siec | obiekt = karta, a = serwer, b = źródło, c = 0 + 1 fd (gniazdo UDP połączone z serwerem:123) albo `K1_SIEC_BLAD_*` bez fd |
| `SIEC_ZEGAR` | siec → nadzorca | obiekt = karta, a, b = sekundy od 1970 (2026–2100), c = nanosekundy, d = zgodnych źródeł 2–6; odpowiedź `SIEC_WYNIK` |

## Karty a pliki od producenta
Karty przewodowe z listy (ADR 0039, T6) działają bez plików od producenta. Realtek r8169 (np. IdeaPad 110-15ISK) prosi
o plik z łatką (`rtl_nic/…`): K1 go nie ma (`EXTRA_FIRMWARE=""`, bez `/lib/firmware`), sterownik pracuje bez niego
(`firmware_request_nowarn` — brak pliku to nie błąd). Czy połączenie na IdeaPadzie jest stabilne — test właściciela.

## Testy
| Test | Rodzaj |
|---|---|
| host `test_siec` (0.9.0 krok 7): `SIEC_STAN`, `SIEC_KARTA`, `SIEC_SZCZEGOLY`, `SIEC_STEROWNIK`, `SIEC_ADRESY` — dobre i każde złe pole (zły kierunek, „od” bez połączenia, Wi-Fi z kablem, używana bez kabla, nazwa sterownika pusta, z wielkimi literami, z bajtem za końcem; ucięcie do 20 bajtów) | jednostkowy + negatywny |
| host `test_siec`: adresy i maski (dobre, każdy rodzaj złego), MAC z sysfs, komunikaty `SIEC_*` w każdą stronę (złe pola, zły kierunek), pakiety wysyłane (nagłówki i sumy liczone niezależnie), odbierane (OFFER, ACK, NAK; każdy skrócony pakiet; obca transakcja i karta; zła suma IP i UDP; fragment; złe porty; opcje za długie, bez końca, bez serwera; zły rodzaj; adres, maska, router nie do przyjęcia; dzierżawa za krótka, „na zawsze”, brak), klient w każdym stanie (powtórki, T1, T2, koniec dzierżawy, NAK, inny serwer, inny adres) | jednostkowy + negatywny |
| `fuzz_dhcp` (korpus `testy/host/korpus-dhcp`): dowolne pakiety po kolei do klienta — pakiet albo odrzucony, albo z adresem do przyjęcia; T1 ≤ T2 ≤ koniec | fuzzing |
| `scripts/test-siec.sh` (QEMU): karta włączona, kabel, DISCOVER; bez adresu nic nie odpowiada; 5 złych odpowiedzi DHCP odrzuconych; adres od routera; z adresem: ARP tylko o swój adres, RST na każdy SYN, żadnego ICMP; trasa domyślna; odnowienie; kabel wyjęty i włożony; brak adresu w logu; ikona na górnym pasku w stanach „łączę”, „połączono”, „kabel odłączony” i Ustawienia → Sieć (kafelek, szczegóły) w jasnym i ciemnym motywie — zrzuty `siec-*.png` | z zewnątrz + negatywny |
| w systemie `t_klatki` (klatka siec), `t_proby` (każda klatka: `socket(AF_INET/AF_PACKET/AF_NETLINK)`, `ioctl(SIOCSIFADDR)`, `setsockopt(SO_DETACH_FILTER)` zablokowane) | funkcjonalny + negatywny |
