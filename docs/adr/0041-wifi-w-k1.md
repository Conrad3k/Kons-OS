# 0041. Wi-Fi w K1: sterowniki i pliki producentów w podpisanym jądrze, nl80211 tylko u nadzorcy, opisy sieci w klatce

- Status: Przyjęty; wdrożony w 0.9.1 — krok 1 (karta, radio, lista sieci), krok 2 (logowanie WPA2, sieci otwarte, zapamiętane
  sieci; T6 rozstrzygnięte niżej)
- Data: 2026-11-14
- Dotyczy: ADR 0039 (sieć w K1 — to jego uzupełnienie), ADR 0004 (brak `/lib/firmware`), ADR 0007 (klatki), SPEC §3
  (niezmiennik 3), decyzje D0, D1, D3 i T1–T6 planu 0.9.1 (`docs/archiwum/PLAN-0.9.1.txt`); RYZYKA R70–R76

## Kontekst
0.9.0 dało kabel: IPv4, gniazda tylko u nadzorcy, usługę „siec” w klatce (ADR 0039). 0.9.1 ma dać Wi-Fi na obu laptopach
właściciela (X230: Intel Centrino; IdeaPad 110-15ISK: prawdopodobnie Qualcomm Atheros QCA9377) i na większości kart
w laptopach. Sterowniki są w jądrze (D0), ale prawie każda karta Wi-Fi potrzebuje do działania pliku od producenta
(firmware), który jądro wgrywa do karty. Jądro samo nie loguje do sieci z hasłem (WPA2) — robi to program.

## Decyzja

**T1 — pliki producentów wbudowane w jądro (`EXTRA_FIRMWARE`).** Jądro szuka pliku najpierw wśród wbudowanych, potem
w `/lib/firmware…` (`drivers/base/firmware_loader/main.c`: `fw_path`) — K1 tego katalogu nie ma (ADR 0004), a sterownik
wbudowany w jądro prosi o plik przy starcie, zanim rdzeń jest zamontowany. Wbudowane = podpis K1 (`k1.efi`, Secure Boot,
manifest A/B) obejmuje je razem z jądrem; nie da się ich podmienić ani dodać po cichu (decyzja właściciela D3: zgoda).
Jedna tabela `jadro/oprogramowanie-kart.txt`: nazwa, o którą prosi sterownik, ścieżka w linux-firmware, SHA-256. Pliki
pobiera `scripts/oprogramowanie-kart.sh` pojedynczo z repozytorium linux-firmware na kernel.org (wersja przypięta
w `jadro/wersja.env`; całe archiwum ma 642 MB) i sprawdza sumy; zła suma przerywa budowę. Tak samo baza przepisów
radiowych `regulatory.db` z podpisem `.p7s` (wireless-regdb, archiwum z sumą): jądro sprawdza jej podpis kluczem
wbudowanym w źródła (`CFG80211_REQUIRE_SIGNED_REGDB`, `USE_KERNEL_REGDB_KEYS`; wybierają kod podpisów PKCS#7 —
odstępstwo w `docs/komponenty/jadro.md`). Bez CRDA (program pomocniczy) i bez starego interfejsu `wireless extensions`.

**T2 — rodziny kart.** PCIe w laptopach: Intel iwlwifi (DVM: X230 — 6205/6235/6300/2200/2230; MVM: 7265 … AX210/AX211;
bez Wi-Fi 7 — IWLMLD), Qualcomm Atheros ath9k (bez pliku) i ath10k (QCA9377, QCA6174), Realtek (rtlwifi 8723BE/8821AE,
rtw88 8822CE/8822BE/8821CE/8723DE, rtw89 8852BE), MediaTek MT7921E. Numer API pliku Intela = jedyny, który przyjmuje jądro
6.18 (`iwlwifi/cfg/*.c`, `iwl-drv.c`: przecięcie zakresów MAC i RF). Razem 32 pliki, 27 MB. Bez kart Wi-Fi na USB
(podłączone urządzenie samo uruchamia sterownik — jak przy kablu) i bez Broadcom. Jądro jest od teraz kompresowane
**zstd** (z gzip bzImage miałby 20 MB; zstd: 12,5 MB, `k1.efi` 8,9 → 14,9 MB; miejsce A/B ma 128 MiB). Algorytm doboru
prędkości minstrel jest potrzebny kartom, które go nie mają w sobie (ath9k, rtlwifi, rtw88, mt76).

**T3 — kto co robi (jak T1 w ADR 0039).** **Nadzorca** (root): wybiera kartę (pierwsza karta Wi-Fi typu „stacja”,
jedna naraz), czyta i ustawia przełącznik radia (rfkill: twardy — przełącznik w obudowie, tylko odczyt; miękki —
Ustawienia, przez `/dev/rfkill`, bo sysfs jest w K1 tylko do odczytu), włącza kartę, szuka sieci przez nl80211 (generic
netlink; `TRIGGER_SCAN`, `GET_SCAN` — wymagają `CAP_NET_ADMIN`: `GENL_UNS_ADMIN_PERM` w `net/wireless/nl80211.c`),
w kroku 2 — połączenie i klucze (`CONNECT`, `NEW_KEY`, też `CAP_NET_ADMIN`). Nadzorca rozbiera tylko wiadomość netlink od
jądra; **opis sieci z powietrza** (elementy informacyjne: nazwa, zabezpieczenia) kopiuje bez czytania do zapieczętowanego
memfd (`struct k1_wifi_rekord`, `SIEC_WIFI_WYNIKI`). **Usługa „siec”** (klatka, bez gniazd) czyta opisy (`src/siec/wifi.c`:
każda długość sprawdzona, test na hoście i fuzzing), robi z nich listę (jedna sieć na nazwę, najsilniejsza, najwyżej 16)
z nazwą bezpieczną do pokazania (znaki sterujące, C1, znaki kierunku i niewidoczne, zły UTF-8 → „?”) i odsyła ją
komunikatami; nadzorca sprawdza listę w całości i podaje Ustawieniom. Żadna nazwa sieci nie trafia do logu (to sieci
sąsiadów — niezmiennik 8 w duchu). Kanał Ustawień (fd 4) działa od 0.9.1 w obie strony: `WIFI_WLACZ`, `WIFI_SZUKAJ`.

**T4 — logowanie do sieci (krok 2): własny, mały kod w „siec”**, tylko WPA2 z hasłem (i tryb mieszany WPA2/WPA3 jako
WPA2), nie obcy program. wpa_supplicant i iwd same otwierają nl80211 i potrzebują `CAP_NET_ADMIN` — to złamałoby podział
T3 — i są duże. Kryptografia (SHA-1, HMAC, PBKDF2, PRF 802.11, AES key wrap) — własne małe pliki sprawdzone oficjalnymi
wektorami; szyfrowanie samych danych (CCMP) robi jądro albo karta. Ramki EAPOL — gniazdem pakietowym od nadzorcy (wzór
DHCP) albo `CONTROL_PORT_FRAME` — wybór w kroku 2 (T6). WPA3 tylko-SAE, sieci firmowe (802.1X), ukryte — „Później”.

**T6 (krok 2) — ramki EAPOL gniazdem pakietowym od nadzorcy.** Przed `CONNECT` nadzorca otwiera na karcie Wi-Fi gniazdo
`AF_PACKET`/`SOCK_DGRAM` z protokołem `ETH_P_PAE` i oddaje je siec (jak gniazdo DHCP z 0.9.0); `CONNECT` z
`NL80211_ATTR_CONTROL_PORT` — do autoryzacji jądro przepuszcza tylko ramki EAPOL (mac80211: protokół portu sterującego),
resztę dopiero po `SET_STATION` (autoryzowany). `CONTROL_PORT_OVER_NL80211` odrzucone: ramki szłyby przez nl80211, którego
klatka siec mieć nie może — nadzorca musiałby je przekazywać (więcej kodu u roota). Klucze policzone w siec wpisuje
nadzorca (`NEW_KEY`) na prośbę z polami sprawdzonymi (karta, rodzaj, id, szyfr). Hasło Wi-Fi idzie z Ustawień **prosto do
siec** (kanał `KANAL_DO_SIECI`, kierunek `K1_KIER_DO_SIECI`) — nadzorca go nie widzi; siec trzyma tylko klucz sieci
(PBKDF2), w skarbcu jako swoje dane (profil 18). Szczegóły: `docs/komponenty/siec.md` („Łączenie z siecią Wi-Fi”).

**T5 — testy.** QEMU nie ma kart Wi-Fi: `MAC80211_HWSIM` jest wbudowany, ale wydanie startuje z
`mac80211_hwsim.radios=0` (`jadro/cmdline`) — w obrazie produkcyjnym nie ma udawanych kart; tworzyć je może tylko root
(nadzorca nigdy tego nie robi). `scripts/test-wifi.sh` startuje obraz testowy z `radios=7`: pierwsza karta jest dla
nadzorcy, na pozostałych program testowy robi punkty dostępu testowe (nl80211 `SET_INTERFACE` → AP, `START_AP`; ramki
beacon budowane bajt po bajcie w `testy/system/t_wifi.c`) z sieciami o złośliwych nazwach i z zepsutym opisem
zabezpieczeń. Ostateczny dowód to prawdziwe karty właściciela (X230, IdeaPad).

## Skutki
- Nowa powierzchnia ataku: stos 802.11 jądra (cfg80211, mac80211) i sterowniki przyjmują ramki od każdego radia w zasięgu,
  nawet bez połączenia; pliki producentów działają w karcie z dostępem do pamięci (DMA) — ochrona: IOMMU (VT-d w BIOS-ie).
  RYZYKA R70–R73.
- `k1.efi` większy o ok. 6 MB; jądro trzyma pliki producentów w pamięci (27 MB).
- Do kodu podpisów w jądrze dochodzą RSA, X.509 i PKCS#7 (sprawdzanie bazy przepisów) — tylko do weryfikacji.
- Nowa karta = wiersz w `jadro/oprogramowanie-kart.txt` i linia w `jadro/k1.config`; Ustawienia → Sieć → „Karty
  sieciowe” pokazują identyfikator karty, której K1 nie umie uruchomić.
