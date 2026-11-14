# Komponent: siec (adres od routera, sieci Wi-Fi)

| | |
|---|---|
| **Status** | PRZETESTOWANE w QEMU (0.9.0 krok 6: karta e1000e kablem do sieci testowej — `scripts/test-siec.sh`; testy na hoście `test_siec`, fuzzing `fuzz_dhcp`); na X230 i IdeaPadzie z kablem do routera — test właściciela po wydaniu 0.9.0 |
| **Pliki** | `src/siec/main.c` (usługa: kanał do nadzorcy, gniazdo pakietowe, log), `dhcp.{c,h}` (pakiety DHCP: budowa i sprawdzanie), `klient.{c,h}` (klient DHCP: stany i czasy); `src/nadzorca/siec.c` (karty, kabel, gniazdo pakietowe, ustawienie adresu); `src/wspolne/k1_siec.{c,h}` (sprawdzanie adresów, MAC), `k1_kom_siec.c` (komunikaty) |
| **Decyzje** | ADR 0039 (T1: uprawnienia, T2: IPv4, T3: nic nie odpowiada, T6: sterowniki); ADR 0041 (Wi-Fi: pliki producentów, nl80211 u nadzorcy, opisy sieci w klatce) |
| **Wi-Fi (0.9.1 krok 1)** | PRZETESTOWANE w QEMU z udawanymi kartami (`scripts/test-wifi.sh`; `test_wifi`, `fuzz_wifi`); prawdziwe karty (X230, IdeaPad) — test właściciela po wydaniu 0.9.1. Pliki: `src/siec/wifi.{c,h}` (opisy sieci z powietrza → lista), `lista.c` (wyniki od nadzorcy, lista do nadzorcy); `src/nadzorca/wifi.c` (karta, radio, szukanie, lista do Ustawień), `wifi_nl.c` (nl80211); `src/wspolne/k1_wifi.{c,h}` (nazwy, kreski, format wyników), `k1_kom_wifi.c` (komunikaty), `k1_nl.{c,h}` (generic netlink: nadzorca i program testowy) |

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
| `SIEC_STAN` | nadzorca → gui, Ustawienia | a = `K1_SIEC_*` stan 0–5 (4, 5: Wi-Fi — od 0.9.1), b = kart na liście 0–8, c = połączono od (CLOCK_MONOTONIC, s; > 0 tylko „połączono”), d = zegar z internetu od (s; 0: jeszcze nie) |
| `SIEC_KARTA` | nadzorca → Ustawienia | obiekt = pozycja 0–7, a = identyfikator (producent << 16 \| urządzenie; PCI albo USB), b = przewodowa / Wi-Fi / inna, c = `K1_SIEC_KF_*` (USB, sterownik, kabel, używana — kabel tylko przewodowa ze sterownikiem; używana: przewodowa z kablem albo, od 0.9.1, karta Wi-Fi ze sterownikiem) |
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

## Wi-Fi (0.9.1 krok 1, ADR 0041)
Podział jak przy kablu: **nadzorca** robi to, co wymaga uprawnień (nl80211 — `CAP_NET_ADMIN`), **siec** czyta to, co
przyszło z powietrza.
1. Karta: nadzorca (`wifi.c`, przy każdym przeglądzie kart) bierze pierwszą kartę Wi-Fi (`class/net/*/phy80211`), która
   łączy się z sieciami (nl80211 `GET_INTERFACE`: typ „stacja”) — jedna naraz. Log „wifi: karta Wi-Fi wlan0 (PCI
   8086:0085)”. Radio: przełącznik rfkill jej `phy` — twardy (przełącznik w obudowie, tylko odczyt) i miękki (Ustawienia,
   `WIFI_WLACZ`; zapis przez `/dev/rfkill`, bo sysfs jest w K1 tylko do odczytu). Radio włączone → karta włączona
   (`RTM_NEWLINK`) i od razu szukanie.
2. Szukanie: co minutę i na prośbę Ustawień (`WIFI_SZUKAJ`, najwyżej co 10 s) nl80211 `TRIGGER_SCAN` (aktywne: jedna pusta
   nazwa — odpowiada każda sieć); koniec ogłasza jądro w grupie „scan” (`NEW_SCAN_RESULTS`/`SCAN_ABORTED`; bez ogłoszenia —
   po 15 s). Wyniki (`GET_SCAN`, zrzut): dla każdego punktu dostępu nadzorca kopiuje liczby jądra (BSSID, częstotliwość,
   sygnał, pole możliwości) i **opis z powietrza bez czytania** (elementy informacyjne, ≤ 2048 B) do rekordu
   `struct k1_wifi_rekord`; najwyżej 64 rekordy, w zapieczętowanym memfd → `SIEC_WIFI_WYNIKI`.
3. siec (`wifi.c`, czyste funkcje): rekord po rekordzie — długości sprawdzone (element dłuższy niż reszta, nazwa > 32 B,
   zepsuty element RSN: wersja ≠ 1, liczba szyfrów poza długością, 0 albo > 32 szyfrów, ucięte pole) → rekord odrzucony
   i policzony; częstotliwość 2400–7200 MHz, sygnał −120…0 dBm. Nazwa do pokazania (`k1_wifi_nazwa_czysta`): poprawny
   UTF-8 bez znaków sterujących (C0, DEL, C1), znaków kierunku tekstu, niewidocznych i BOM — każdy zły bajt albo znak to
   „?”; pusta nazwa albo same zera = sieć ukryta (nie pokazywana). Zabezpieczenie: RSN z AKM PSK i SAE → WPA2/WPA3,
   tylko SAE → WPA3, PSK → WPA2, 802.1X → firmowa; bez RSN: bez bitu prywatności → otwarta, z nim (WEP, pierwsze WPA) →
   stare; RSN bez bitu prywatności → inne. Jedna sieć na nazwę i zabezpieczenie (najsilniejszy punkt dostępu), najwyżej
   16, najsilniejsze pierwsze; kreski 1–4 z dBm (`k1_wifi_poziom`: progi −55, −67, −75 — jedna tabela).
4. Lista wraca do nadzorcy (`SIEC_WIFI_SIEC`, dwa `SIEC_WIFI_NAZWA` na sieć, na końcu `SIEC_WIFI_LISTA`); nadzorca bierze ją
   tylko w całości (każda sieć ze wszystkimi częściami, każda nazwa `k1_wifi_nazwa_dobra`) i podaje Ustawieniom
   (`WIFI_STAN`, `WIFI_SIEC`, `WIFI_NAZWA`). Log siec: „sieci Wi-Fi w pobliżu: 5 (punktów dostępu 6, ukryte 0, odrzucone
   opisy 1)” — **bez nazw** (to sieci sąsiadów).
5. Górny pasek: bez kabla, z kartą Wi-Fi stan `SIEC_STAN` to `K1_SIEC_WIFI_WYLACZONE` (4) albo `K1_SIEC_WIFI_NIE_POLACZONO`
   (5) — ikona „Wachlarz” (gui.md). Kabel (łączę, połączono) ma pierwszeństwo.
Logowanie do sieci z hasłem (WPA2), zapamiętane sieci — krok 2 planu 0.9.1.

| Typ | Kierunek | Pola |
|---|---|---|
| `SIEC_WIFI_WYNIKI` | nadzorca → siec | obiekt = karta Wi-Fi, a = rekordów 0–64, b = bajtów (0 bez rekordów); + 1 zapieczętowany memfd b bajtów |
| `SIEC_WIFI_SIEC` | siec → nadzorca | obiekt = sieć 0–15, a = kreski 1–4 (zgodne z c), b = zabezpieczenie `K1_WIFI_*` 1–7, c = dBm −120…0, d = MHz 2400–7200, e = długość nazwy 1–32 |
| `SIEC_WIFI_NAZWA` | siec → nadzorca | obiekt = sieć·2 + część; część 0: bajty 0–19 w a…e, część 1: bajty 20–31 w a…c (d, e = 0); całość sprawdza odbiorca (`k1_wifi_nazwa_dobra`) |
| `SIEC_WIFI_LISTA` | siec → nadzorca | obiekt = karta z `SIEC_WIFI_WYNIKI`, a = sieci 0–16, b = odrzuconych rekordów 0–64 — lista gotowa |
| `WIFI_STAN` | nadzorca → Ustawienia | a = `K1_WIFI_*` (0 brak karty, 1 wyłączone przełącznikiem w komputerze, 2 wyłączone w Ustawieniach, 3 włączone), b = sieci 0–16 (tylko gdy włączone), c = 1 szukam teraz |
| `WIFI_SIEC` | nadzorca → Ustawienia | jak `SIEC_WIFI_SIEC` |
| `WIFI_NAZWA` | nadzorca → Ustawienia | jak `SIEC_WIFI_NAZWA` |
| `WIFI_WLACZ` | Ustawienia → nadzorca | a = 1 włącz / 0 wyłącz radio (miękki rfkill); nadzorca sprawdza UID Ustawień |
| `WIFI_SZUKAJ` | Ustawienia → nadzorca | wszystkie pola 0 — szukaj sieci teraz (Ustawienia pokazują listę) |

## Karty a pliki od producenta
Karty przewodowe z listy (ADR 0039, T6) działają bez plików od producenta. Realtek r8169 (np. IdeaPad 110-15ISK) prosi
o plik z łatką (`rtl_nic/…`): K1 go nie ma (w jądrze są tylko pliki kart Wi-Fi z `jadro/oprogramowanie-kart.txt`, bez
`/lib/firmware`), sterownik pracuje bez niego (`firmware_request_nowarn` — brak pliku to nie błąd). Czy połączenie na
IdeaPadzie jest stabilne — test właściciela. Karty Wi-Fi bez pliku nie działają wcale — ich pliki są wbudowane w jądro
(ADR 0041, T1).

## Testy
| Test | Rodzaj |
|---|---|
| host `test_siec` (0.9.0 krok 7): `SIEC_STAN`, `SIEC_KARTA`, `SIEC_SZCZEGOLY`, `SIEC_STEROWNIK`, `SIEC_ADRESY` — dobre i każde złe pole (zły kierunek, „od” bez połączenia, Wi-Fi z kablem, używana bez kabla, nazwa sterownika pusta, z wielkimi literami, z bajtem za końcem; ucięcie do 20 bajtów) | jednostkowy + negatywny |
| host `test_siec`: adresy i maski (dobre, każdy rodzaj złego), MAC z sysfs, komunikaty `SIEC_*` w każdą stronę (złe pola, zły kierunek), pakiety wysyłane (nagłówki i sumy liczone niezależnie), odbierane (OFFER, ACK, NAK; każdy skrócony pakiet; obca transakcja i karta; zła suma IP i UDP; fragment; złe porty; opcje za długie, bez końca, bez serwera; zły rodzaj; adres, maska, router nie do przyjęcia; dzierżawa za krótka, „na zawsze”, brak), klient w każdym stanie (powtórki, T1, T2, koniec dzierżawy, NAK, inny serwer, inny adres) | jednostkowy + negatywny |
| `fuzz_dhcp` (korpus `testy/host/korpus-dhcp`): dowolne pakiety po kolei do klienta — pakiet albo odrzucony, albo z adresem do przyjęcia; T1 ≤ T2 ≤ koniec | fuzzing |
| `scripts/test-siec.sh` (QEMU): karta włączona, kabel, DISCOVER; bez adresu nic nie odpowiada; 5 złych odpowiedzi DHCP odrzuconych; adres od routera; z adresem: ARP tylko o swój adres, RST na każdy SYN, żadnego ICMP; trasa domyślna; odnowienie; kabel wyjęty i włożony; brak adresu w logu; ikona na górnym pasku w stanach „łączę”, „połączono”, „kabel odłączony” i Ustawienia → Sieć (kafelek, szczegóły) w jasnym i ciemnym motywie — zrzuty `siec-*.png` | z zewnątrz + negatywny |
| host `test_wifi` (0.9.1): nazwy (sterujące, C1, kierunek tekstu, zły i ucięty UTF-8, 32 B, ukryte), kreski, każde zabezpieczenie, każdy zepsuty element i RSN, cały wynik (jedna sieć na nazwę, najsilniejsza, 16 najsilniejszych, odrzucone, ukryte, zły format nadzorcy), komunikaty Wi-Fi (każde złe pole i kierunek) | jednostkowy + negatywny |
| `fuzz_wifi`: dowolny opis i dowolny wynik — odrzucony albo lista, w której każda sieć przechodzi sprawdzenie komunikatu i nazwy | fuzzing |
| `scripts/test-wifi.sh` (QEMU, udawane karty `mac80211_hwsim.radios=7`, punkty dostępu testowe z `t_wifi.c`): baza przepisów radiowych sprawdzona podpisem; karta i radio; 5 sieci (4 z hasłem, 1 otwarta), zepsuty opis odrzucony, siec żyje; nazwy ze znakiem nowej linii, sekwencją sterującą, złym UTF-8 i znakiem kierunku pokazane z „?”; ikona; przełącznik wyłącza i włącza radio; zakładka kart; w logu nie ma nazw sieci — zrzuty `wifi-*.png` | z zewnątrz + negatywny |
| w systemie `t_klatki` (klatka siec), `t_proby` (każda klatka: `socket(AF_INET/AF_PACKET/AF_NETLINK)`, `ioctl(SIOCSIFADDR)`, `setsockopt(SO_DETACH_FILTER)` zablokowane; 0.9.1: `socket(AF_NETLINK, NETLINK_GENERIC)` — nl80211 — też) | funkcjonalny + negatywny |
