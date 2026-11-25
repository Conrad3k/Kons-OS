# Komponent: nadzorca (`/rdzen/system/nadzorca`)

| | |
|---|---|
| **Status** | PRZETESTOWANE (M3, M4) — testy w systemie (`scripts/test-m1.sh`, scenariusz `testy-w-systemie`), `scripts/test-m4.sh` (uruchamianie aplikacji, wyłączanie) |
| **Kod** | od 0.6.0 moduły (ADR 0019): `main.c` (pętla: tablica kanałów, sygnały, zdarzenia), `uslugi.c` (usługi z tabel `K1_SYSTEM` i `K1_APLIKACJE`: uruchamianie w klatkach, kanały, restarty, prośby pulpitu), `kanaly.h` (nazwy kanałów `KANAL_*`, 0.9.0), `logi.c` (logi usług), `pendrive.c` (prośby Plików, pendrive i czytnik, 0.8.0), `dekoder.c` (dekoder obrazów dla Plików, 0.8.0, i Podglądu, 0.8.2), `powieksz.c` (K1-DANE do końca pendrive'a: pusta przy starcie, 0.8.0; z danymi na prośbę z Ustawień, 0.8.1), `profile.c` (klatki: `KLATKA_<NAZWA>` dla każdej usługi, aplikacje — jedna dla wszystkich), `urzadzenia.c` (karta, wejścia, partycja K1-DANE, uevent), `skarb.c` (kanały skarbca, **etapy startu**), `instalacja.c` (instalator; od 0.10.5 krok 1 dwa okna: Instalator i Ustawienia → Aktualizacje — co które może, mówi czysty `inst_prawa.c`; od kroku 2 także zadania aktualizacji przez sieć — `inst_siec`), `od_nowa.c` (0.10.10 krok 1: instalacja od nowa na dysku z otwartymi danymi — strefy kończone, obrazy stref puszczone, skarbiec oddaje partycję `SKARB_ZWOLNIJ`, potem instalacja; `docs/komponenty/instalator.md`), `aktualizacja.c` + `aktualizacja_ust.c` (aktualizacja przez sieć, 0.10.5 krok 2, ADR 0046: kiedy sprawdzać, strefa pobierania i program zapisujący, stan dla Ustawień i gui, „Sprawdzaj sam” w skarbcu — `docs/komponenty/aktualizacja.md`), `siec.c` (karty sieciowe, kabel, gniazdo pakietowe dla usługi siec, ustawianie adresu — 0.9.0, ADR 0039), `siec_jadro.c` (zapytania rtnetlink), `siec_stan.c` (stan sieci i lista kart dla pulpitu i Ustawień), `siec_zegar.c` (zegar z internetu), `wifi.c` (karta Wi-Fi, przełącznik radia, szukanie sieci, lista dla Ustawień — 0.9.1, ADR 0041), `wifi_nl.c` (nl80211), `wifi_polacz.c` (łączenie z siecią na prośbę siec, klucze, gniazdo EAPOL — 0.9.1 krok 2), `strefy.c` (strefy: VMM strefy na prośbę gui albo programu testowego, `/dev/kvm`, jądro i obraz strefy, kanał do ekranu; brak wirtualizacji, pamięci, obrazu → `STREFA_STAN` do gui — 0.10.0, ADR 0042; powód końca strefy z kodu VMM i zdarzeń jej klatki `memory.events` — czysta część `strefy_powod.c`, 0.10.1 krok 1; górna granica pamięci strefy — `pamiec_stref.c`, czyste, 0.10.1 krok 2), `obraz_strefy.c` (0.10.9 krok 4, ADR 0050: obraz przeglądarki poza rdzeniem — szuka go w miejscu A/B tego wydania na K1-EFI dysków wewnętrznych, potem pendrive'ów, w testach QEMU na dysku virtio jego wielkości; dm-verity ze skrótem z rdzenia `/rdzen/strefy/<obraz>.skrot`, wspólny kod z initramfs `src/wspolne/k1_verity.c`; urządzenie trzyma i daje VMM-om jego kopię, przed każdym startem strefy czyta pierwszy blok — wyjęty nośnik → szuka od nowa; raz po pulpicie log „obraz strefy …: miejsce a na sda1”), `strefy_balon.c` (pamięć stref od VMM — `STREFA_PAMIEC`, cel balonu — `STREFA_BALON`; ADR 0044; od 0.10.1 krok 3 **rozdzielacz** — pamięć komputera dla K1 i stref, zasada w czystym `pamiec_stref.c`, opis niżej), `strefy_zamyk.c` (0.10.2 krok 1: strefy zamykające się w tle — `STREFA_ZAMYKA` od VMM, „zamyka się…” w `STREFY_STAN` e; prośba o taką strefę → otwarcie, gdy stara maszyna skończy (D2); wyłączanie i ponowne uruchamianie K1 na prośbę pulpitu — najpierw `STREFA_ZAMKNIJ` do każdej działającej strefy i czekanie najwyżej 30 s, potem init), `strefy_do.c` (0.11.0 krok 3: plik z Plików do strefy — `SKARB_DO_STREFY` od skarbca, zamknięta sesja otwierana (D4: A), plik do VMM kawałkami czytanymi kanałem `K1_WL_DO_STREF`, powiązania sesja + numer → plik dla zmienionych wersji), `strefy_pliki.c` (0.11.0 krok 2, ADR 0052: plik ze strefy do Plików — `STREFA_PLIK` od VMM, okienko gui `POBRANIE` → `POBRANIE_DECYZJA`, zapis kawałkami w Pobranych własnym kanałem skarbca `K1_WL_POBRANIA`, jeden plik naraz, koniec strefy przerywa; w logu tylko numery), `strefy_siec.c` (połączenia przeglądarki w strefie: kanał każdej strefy — fd 7 VMM, `STREFA_POLACZ` → reguła D3 `k1_siec_dla_strefy`, limit, gniazdo TCP albo UDP do serwera DNS z DHCP → `STREFA_GNIAZDO` / `STREFA_ODMOWA`; w logu tylko liczniki — 0.10.0 krok 2, ADR 0043; część Raptor Bridge — `docs/komponenty/raptor-bridge.md`), `zasoby.c` (bateria, historia baterii w skarbcu, Zasoby), `dzwiek.c` (0.12.0 krok 1, ADR 0054: pierwsza karta dźwiękowa z odtwarzaniem z sysfs i zdarzeń jądra „sound” → jej węzły tylko dla usługi dźwięku, `DZWIEK_BEZ_KARTY` 5 s po starcie; kanały klientów z tabeli `K1_DZW_KLIENCI` — gui na fd 5, program testowy `TEST_DZWIEK` — `docs/komponenty/dzwiek.md`), `filmy.c` (0.10.9 krok 2, ADR 0049: dekoder filmów — węzeł render
karty i915 tylko dla niego; pytanie przy starcie, co karta rozpakowuje → Ustawienia `FILMY_KARTA`; dekoder strefy na
prośbę — `filmy_dla_strefy`, koniec z VMM-em; jego pamięć w rozdzielaczu — `docs/komponenty/filmy.md`), `posrednik.c` (0.10.9
krok 5, ADR 0049 pkt 7: pośrednik grafiki — pytanie przy starcie, czy karta obsługuje szybką grafikę → Ustawienia
`POSR_KARTA`; pośrednik strefy na prośbę — `posrednik_dla_strefy`, koniec z VMM-em, powód końca w logu; jego pamięć
w rozdzielaczu — `docs/komponenty/posrednik.md`), `karta.c` (0.10.9 krok 5: co dekoder filmów i pośrednik grafiki dzielą —
węzeł render tylko sterownika i915, uruchomienie usługi karty z kanałem i węzłem, `oom_score_adj`, pamięć cgroup),
`test.c` (kanał testowy, od 0.10.9 krok 2 też `TEST_FILMY`, od kroku 5 `TEST_POSREDNIK`); czyste części z testami na hoście: `bateria.c`, `dyski.c`, `uevent.c`; wspólne: `nadzorca.h`. Klatka `src/wspolne/k1_klatka.c`, kanały `k1_kanal.c`, odbiór `k1_odbior.c`, protokół `k1_komunikat.c` |
| **Decyzje** | ADR 0006 (wersja M2), ADR 0007 (M3, klatki), ADR 0008 (pulpit i aplikacje), ADR 0013 (dyski i USB), ADR 0019 (moduły, zdarzenia jądra), ADR 0022 (A/B, Secure Boot), ADR 0030–0033 (pliki, pendrive, dekoder, K1-DANE do końca pendrive'a), ADR 0036 (potwierdzony start), ADR 0037 (Podgląd), ADR 0039 (sieć) |

## Etapy startu (0.6.0)
`dane` (czeka na partycję K1-DANE: do 3 s, z pendrive'em bez dysku do 15 s) → `stan magazynu` (skarbiec
działa; do 20 s na jego stan) → `hasło` (magazyn zablokowany: ekran hasła zamiast pulpitu) →
`przełączanie` (po haśle: nowy ekran z ustawieniami) → `pulpit`. Ponowny start skarbca wraca do
`stan magazynu`. Każde przejście: wiersz `start: A → B (powód)` w logu.

## Zdarzenia jądra (0.6.0)
Gniazdo netlink `NETLINK_KOBJECT_UEVENT` (grupa jądra; tylko nadawca o porcie 0): nowe/usunięte
`input/eventN` → wejscie z nowym zestawem; dysk albo partycja (dodane, zmienione, od 0.8.6 także usunięte —
Pliki od razu wiedzą, że pendrive wyjęto) → K1-DANE (start, późne wykrycie) i pendrive dla Plików; `power_supply`
→ odczyt baterii od razu. Bez gniazda albo po przepełnieniu kolejki — pełne sprawdzenie (co 10 s
zawsze jako zapas). Parser: `uevent.c` (test + fuzzing).
Od 0.9.0 drugie gniazdo: rtnetlink (`NETLINK_ROUTE`, grupa `RTMGRP_LINK`; tylko nadawca o porcie 0) — nowa karta
sieciowa, karta usunięta, kabel włożony albo wyjęty. Wiadomość tylko budzi nadzorcę; co się zmieniło, czyta z sysfs
(`class/net/*/carrier`) — `siec.c`, bez parsowania wiadomości.

## Historia baterii (0.6.0)
Nadzorca jest klientem skarbca jako właściciel `K1_WL_SYSTEM` (obiekt `K1_OB_BATERIA`, format
`k1_bat_arch.h`): odczyt raz po otwarciu magazynu, zapis tego uruchomienia od razu, co 5 min i przy
wyłączaniu; Ustawienia dostają archiwum jako zapieczętowany memfd (`BATERIA_ARCH`). Zapis przy wyłączaniu działa
od 0.8.6: init zatrzymuje najpierw samego nadzorcę (`docs/komponenty/init.md`, „Wyłączanie”), więc skarbiec jeszcze
odpowiada — log „historia baterii zapisana przed wyłączeniem” (test: `scripts/test-m5.sh`). Do 0.8.5 skarbiec
dostawał `SIGTERM` w tej samej chwili i ten zapis przepadał (ostatnie minuty wykresu).

## Cel
Uruchamia usługi świata natywnego, każdą w osobnej klatce, łączy je kanałami, przekazuje ich logi na
konsolę i uruchamia ponownie po awarii. W obrazie testowym uruchamia program testowy i kończy się jego
kodem wyjścia (init wyłącza wtedy maszynę).

## Granica zaufania
Uruchamiany przez init jako root, **bez klatki** (musi zmieniać UID, tworzyć cgroups, otwierać
urządzenia) — część bazy zaufania. Od usług przyjmuje tylko: wiersze logu (oczyszczane, z limitem),
statusy zakończenia i komunikaty 32 B na kanałach z tabeli niżej — od `gui` (`URUCHOM`, `WYLACZ`), skarbca
(`SKARB_*`, `POWIEKSZ_DANE`), okna Instalatora (`INST_*`, `SB_KLUCZ`, `OTWORZ_AKTUALIZACJE`), Ustawień → Aktualizacje (od 0.10.5 krok 1: `INST_DYSKI`, `INST_START` tylko z aktualizacją, `WYLACZ` a=1 po udanej aktualizacji), Plików (`PENDRIVE_OTWORZ`, `DEKODER`), Podglądu
(`DEKODER`), usługi sieci (`SIEC_ADRES`, `SIEC_ZWOLNIJ` — adres sprawdzany pole po polu, tylko dla karty, którą siec
dostała; od 0.9.0 krok 8 `SIEC_CZAS_GNIAZDO`, `SIEC_ZEGAR` — gniazdo do serwera czasu i zegar w granicach) i programu
zapisującego; nadawca każdego sprawdzany `SO_PASSCRED`. Od 0.9.0 nadzorca jest jedynym procesem,
który dotyka sieci: włącza karty, otwiera gniazdo pakietowe dla usługi siec, ustawia i zabiera adres (rtnetlink) —
`docs/komponenty/siec.md`; mówi pulpitowi i Ustawieniom, jak wygląda sieć (`siec_stan.c`: ikona, lista kart ze
szczegółami — tylko liczby), otwiera gniazda UDP do serwerów czasu i ustawia zegar systemowy i sprzętowy
(`siec_zegar.c`, krok 8; CAP_SYS_TIME, `/dev/rtc0`), a skarbcowi mówi, o ile zegar się przesunął. Od 0.9.1 (ADR 0041) także Wi-Fi:
wybiera kartę, przełącza radio (rfkill przez `/dev/rfkill`), szuka sieci przez nl80211 (`CAP_NET_ADMIN`) i oddaje
usłudze siec opisy z powietrza bez czytania (`SIEC_WIFI_WYNIKI`, memfd); listę od siec sprawdza w całości i podaje
Ustawieniom, a od nich przyjmuje `WIFI_WLACZ` i `WIFI_SZUKAJ` (kanał Ustawień działa od 0.9.1 w obie strony). Od 0.9.1 krok 2
(`wifi_polacz.c`): na prośbę siec łączy z siecią (nl80211 `CONNECT`), daje siec gniazdo pakietowe na ramki EAPOL, wpisuje
klucze, które siec policzyła (`NEW_KEY` — sekret: nie w logu, czyszczony), wpuszcza dane (`SET_STATION`), rozłącza;
ogłoszenia jądra (grupa „mlme”) podaje siec; gdy dane płyną, karta Wi-Fi dostaje adres jak karta z kablem (kabel ma
pierwszeństwo); Ustawieniom daje kanał prosto do siec (`KANAL_DO_SIECI`, `KN_SIEC`) — hasło Wi-Fi nie przechodzi przez
nadzorcę. Lista usług i profili jest stała,
w kodzie (`K1_SYSTEM`, `K1_APLIKACJE`): od 0.9.0 jeden wiersz opisuje usługę całą — kiedy startuje (`PRZY_STARCIE`
albo `NA_PROSBE`), rolę przy ekranie i kanały od nadzorcy po kolei; z niego wynikają `uslugi[]`, nazwa i UID klatki,
Zasoby i spis obrazu. Klatka (wywołania i limity) — `KLATKA_<NAZWA>` w `profile.c`; bez niej nadzorca się nie
buduje. Tablica właścicieli naszych końców kanałów ma miejsce na każdy kanał każdego wiersza — nie może się zapełnić.

## Uprawnienia
root, wszystkie capabilities (RYZYKA R23). Usługi — patrz tabela.

Tabela poniżej to stan z `src/nadzorca/profile.c` (klatki) i kolumny kanałów w `src/wspolne/k1_uslugi.h` (deskryptory);
źródłem prawdy jest kod. Każda usługa ma 1 proces w swojej cgroup (`pids.max = 1`).

| Usługa | UID | Przekazane deskryptory | seccomp (poza bazą) | cgroup | `RLIMIT_FSIZE` |
|---|---|---|---|---|---|
| `skarbiec` | 103 | 3: kanał sterujący, 4: partycja K1-DANE | `pread64`, `pwrite64`, `fdatasync`, `getrandom`, `memfd_create`, `sysinfo` | 320 MiB | 24 MiB |
| `ekran` | 101 | 3: kanał sterujący, 4: karta graficzna (DRM master), 5: skarbiec | `memfd_create`, `ioctl` typu `'d'` | 176 MiB | 2 MiB |
| `wejscie` | 100 | 3: kanał do ekranu, 4…: urządzenia `event*` | `ioctl` typu `'E'` | 8 MiB | 0 |
| `gui` | 102 | 3: ekran, 4: prośby do nadzorcy, 5: bateria (od nadzorcy), 6: skarbiec | `memfd_create`, `ftruncate` | 128 MiB | 72 MiB |
| `logowanie` (zamiast gui, gdy jest hasło) | 104 | 3: ekran, 4: prośby do nadzorcy (tylko `WYLACZ`), 5: bateria, 6: skarbiec | `memfd_create`, `ftruncate` | 96 MiB | 72 MiB |
| `zegar`, `kalkulator` | 200, 205 | 3: ekran | `memfd_create`, `ftruncate` | 32 MiB | 16 MiB |
| `notatki` | 201 | 3: ekran, 4: skarbiec, 5: plik do otwarcia (od nadzorcy) | jw. | 32 MiB | 16 MiB |
| `ustawienia` | 202 | 3: ekran, 4: bateria i jej historia (od nadzorcy), 5: skarbiec, 6: siec (hasło Wi-Fi), 7: aktualizacje — dyski, postęp (z nadzorcą w obie strony, 0.10.5) | jw. | 32 MiB | 16 MiB |
| `zasoby` | 203 | 3: ekran, 4: dane od nadzorcy (tylko do odczytu) | jw. | 32 MiB | 16 MiB |
| `instalacja` (okno Instalatora) | 204 | 3: ekran, 4: dyski i postęp (z nadzorcą w obie strony; od 0.10.5 tylko instalacja) | jw. | 32 MiB | 16 MiB |
| `pliki` | 206 | 3: ekran, 4: skarbiec, 5: miejsce do pokazania (od nadzorcy), 6: pendrive i dekoder | jw. | 32 MiB | 16 MiB |
| `wybor` (Wybór pliku) | 207 | 3: ekran, 4: skarbiec | jw. | 32 MiB | 16 MiB |
| `podglad` | 208 | 3: ekran, 4: skarbiec, 5: plik do otwarcia, 6: dekoder | jw. | 96 MiB | 16 MiB |
| `instalator` (na prośbę okna Instalatora albo Ustawień → Aktualizacje) | 105 | 3: kanał sterujący, 4: źródło (K1-EFI pendrive'a), 5: cel (dysk albo jego K1-EFI) | `pread64`, `pwrite64`, `fdatasync`, `getrandom` | 16 MiB | 0 |
| `czytnik` (na prośbę Plików) | 106 | 3: kanał do Plików, 4: pendrive (tylko odczyt) | `pread64`, `memfd_create` | 32 MiB | 17 MiB |
| `dekoder` (na prośbę Plików albo Podglądu) | 107 | 3: kanał do klienta | `memfd_create` | 1216 MiB (mniej przy < 8 GB RAM) | 17 MiB; `RLIMIT_AS` 1152 MiB, `RLIMIT_CPU` 20 s |
| `strefa` (VMM strefy, na prośbę gui; 0.10.0, ADR 0042; wiersz `K1_STREFY`) | 300… (z wiersza) | 3: kanał do ekranu (rola `STREFA`), 4: `/dev/kvm`, 5: jądro strefy, 6: obraz strefy (tylko odczyt) | `ioctl` typu KVM (0xAE), `memfd_create`, `ftruncate`, `eventfd2`, `futex`, `getrandom`, `pread64`, `clone` tylko `CLONE_THREAD`; `pids.max` 1 + procesory + 1 | pamięć gościa + 160 MiB | pamięć gościa |
| `filmy` (0.10.9 krok 2, ADR 0049): pytanie przy starcie (wiersz `FILMY`) i dekoder filmów każdej strefy (z wiersza strefy, UID 400 + nr, cgroup `filmy-<nr>`) | 109 / 400 + nr | 3: kanał do klienta (nadzorca, VMM strefy, program testowy), 4: węzeł render karty i915 (dekoder testowy: `/dev/null`) | `getuid`, `geteuid`, `ioctl` typu DRM (`'d'`) | 608 MiB (512 MiB buforów karty — `K1_FILMY_PAMIEC` + kod i okno), 16 plików | 0 |
| `posrednik` (0.10.9 krok 5, ADR 0049 pkt 7): pytanie przy starcie (wiersz `POSREDNIK`) i pośrednik grafiki każdej strefy (z wiersza strefy, UID 410 + nr, cgroup `posrednik-<nr>`) | 110 / 410 + nr | 3: kanał do klienta (nadzorca, VMM strefy, program testowy), 4: węzeł render karty i915 (silnik testowy: `/dev/null`) | `ioctl` typu DRM (`'d'`) — nic poza bazą | 800 MiB (768 MiB buforów karty — `K1_POSR_PAMIEC` + kod), 16 + 256 plików | 0 |
| `dzwiek` (0.12.0 krok 1, ADR 0054) | 111 | 3: kanał od nadzorcy (jednokierunkowy); węzły karty dźwiękowej `pcmC<k>D<u>p` i `controlC<k>` (w `DZWIEK_KARTA`), końce kanałów klientów (w `DZWIEK_KLIENT`) | `ioctl` tylko 13 numerów ALSA (`docs/komponenty/dzwiek.md`) | 16 MiB, 24 pliki | 0 |
| `siec` (0.9.0) | 108 | 3: kanał do nadzorcy; gniazdo pakietowe na karcie z kablem (w komunikacie `SIEC_KABEL`); do 6 gniazd UDP do serwerów czasu (`SIEC_GNIAZDO`); memfd z wynikami szukania sieci Wi-Fi (`SIEC_WIFI_WYNIKI`, 0.9.1) | `getrandom` | 8 MiB, 16 plików | 0 |

Landlock (ABI ≥ 6, inaczej usługa nie startuje): jedyna reguła to własny program (uruchomienie i odczyt — tylko
do `execve` przy starcie), poza tym żadnej ścieżki; zablokowane też TCP oraz sygnały i abstrakcyjne gniazda UNIX
poza własną klatką. Aplikacje dostają kanał do skarbca (`KN_SKARB`) tylko,
gdy skarbiec działa. `RLIMIT_FSIZE` ogranicza też memfd (powierzchnie okien, dane dla skarbca); na urządzenie
blokowe (skarbiec, instalator) nie działa.

Wspólne dla usług: fd 0 i 1 = `/dev/null`, fd 2 = pipe logu do nadzorcy; brak capabilities (także
zbiór graniczny), `NO_NEW_PRIVS`, securebits zablokowane, `RLIMIT_CORE=0`, niezrzucalne
(`PR_SET_DUMPABLE=0`), `PR_SET_PDEATHSIG=SIGKILL`, drugi filtr seccomp bez `execve`.
Baza seccomp: `read write close exit exit_group rt_sig* brk munmap mremap madvise clock_gettime
clock_nanosleep nanosleep poll ppoll fcntl fstat newfstatat lseek getpid gettid sendmsg recvmsg sendto
recvfrom seccomp set_tid_address restart_syscall execve` + `mmap`/`mprotect` bez `PROT_EXEC`,
`arch_prctl(ARCH_SET_FS)`, `prctl(PR_SET_DUMPABLE)`.

## Wejścia i wyjścia
Wejście: statusy procesów (`signalfd`), logi usług, prośby usług (wyżej), zdarzenia jądra (uevent), sysfs (dyski,
bateria), kanał testowy (obraz testowy). Wyjście: procesy
usług, kanały (`socketpair`, strona odbiorcy z `SO_PASSCRED`), komunikaty `KLIENT` do ekranu, log.

## Kanał testowy (tylko obraz testowy)
Program testowy dostaje od nadzorcy fd 3 (`K1_KIER_DO_NADZORCY` / `OD_NADZORCY`, nadawca sprawdzany):
`TEST_POLACZ` (rola, aplikacja, oczekiwany UID) → `TEST_GNIAZDO` z końcem nowego kanału do ekranu;
`TEST_SKARB` (właściciel — numer profilu; 0.9.0) → `TEST_GNIAZDO` z końcem nowego kanału do skarbca, nadawcą jest program
testowy (UID 0);
`TEST_PROBA` (profil) → program testowy uruchomiony w klatce tej usługi, po końcu `TEST_WYNIK` (kod
wyjścia albo 128 + sygnał); `URUCHOM` jak od gui. W obrazie produkcyjnym tego kanału nie ma.

## Zachowanie
- Start: skarbiec, ekran (karta `card0`…`card7`;
  gdy brak — ponowna próba za 4 s), potem jego klienci (wejscie, gui).
- Prośby od `gui` (fd 4, nadawca UID 102): `URUCHOM` — aplikacja, jeśli jeszcze nie działa
  (aplikacje nie są restartowane); `WYLACZ` — najpierw `WYLACZANIE` do ekranu (ekran „Wyłączanie” od razu, 0.10.7
  krok 6), potem strefy (`strefy_zamyk.c`: najwyżej 30 s, co 5 s log „wyłączanie: czekam na strefy (n), najwyżej jeszcze
  x s”) i co najmniej jeden obieg kropek ekranu „Wyłączanie” (1,5 s, także bez stref — 0.10.9), potem `SIGTERM`
  (wyłączenie) albo `SIGINT` (restart) do init;
  `WYLACZ` a=2 (0.6.3, tylko od UID 102) — zablokowanie („Zablokuj”, do 0.11.4 „Wyloguj”) `zamknij_sesje()`: aplikacje
  i pulpit cicho (`SIGTERM`), potem skarbiec (zapomina klucz); nowy skarbiec zgłasza magazyn zablokowany → ekran
  hasła. Od 0.11.5 tylko, gdy magazyn ma hasło (`blokada_mozliwa()`: `SKARB_STAN` b=1, skarbiec zgłasza stan też po
  ustawieniu albo wyłączeniu hasła) — inaczej odrzucone („zablokowanie odrzucone: magazyn danych bez hasła”). Ekran
  logowania i Instalator a=2 nie wyślą.
- Prośby skarbca (0.8.0, ADR 0030, kanał sterujący, nadawca UID 103): `SKARB_URUCHOM` — uruchom aplikację, a gdy
  ma plik (obiekt ≠ 0; skarbiec dał jej dostęp po „Otwórz za pomocą” albo podwójnym kliknięciu) — wyślij go na jej fd 5 (`PLIK_OTWORZ`) i poproś
  pulpit (`POKAZ_APL`, fd 5 gui) o jej okno na wierzch; obiekt = 0 — okno wyboru pliku („Wybór pliku”).
  Kanał „plik do otwarcia” (fd 5) mają Notatki i Pliki (Pliki: `PLIKI_POKAZ`), tablica `plik_kanal[]`.
- Pendrive (0.8.0 krok 3, ADR 0031, `pendrive.c`): Pliki dostają fd 6 (`PENDRIVE`, `PENDRIVE_KANAL`; prośba
  `PENDRIVE_OTWORZ` tylko od UID 206). Pendrive = pierwszy cały dysk USB z nośnikiem bez partycji K1
  (`k1_pendrive_znajdz`, sysfs — nadzorca nie czyta pendrive'a). Na prośbę: cały pendrive `O_RDONLY` → usługa `czytnik`
  (fd 4), kanał czytnik ↔ Pliki (fd 3 czytnika). Wyjęty pendrive (zdarzenie dysku albo skan co 10 s) → `SIGKILL`
  czytnika i `PENDRIVE` a = 0. Szczegóły: `komponenty/czytnik.md`.
- K1-DANE do końca pendrive'a (0.8.0 krok 5, ADR 0033, `powieksz.c`): przed oddaniem partycji skarbcowi — gdy jest
  na pendrivie, pusta (pierwszy MiB zer) i za nią jest ≥ 16 MiB — nowa tablica GPT (`k1_gpt_powieksz`, sprawdza starą)
  i `BLKPG_RESIZE_PARTITION`; log „K1-DANE powiększona do końca pendrive'a: 64 → N MiB”. Od 0.8.1 (krok 3) także
  z danymi — tylko na `POWIEKSZ_DANE` od skarbca (kanał sterujący, nadawca UID 103), który wysyła je po kliknięciu
  „Użyj całego pendrive'a na dane” w Ustawieniach (gest użytkownika, decyzja D4) i przy otwartym magazynie; te same
  zapisy tablicy, skarbiec zostaje przy otwartej partycji, odpowiedź `POWIEKSZONO` (a = 0 / `K1_SKARB_ARG` /
  `K1_SKARB_IO`). `SKARB_KLIENT` d = ile MiB pendrive ma jeszcze za K1-DANE (0: nie pendrive albo < 16 MiB).
- Potwierdzony start (`rozruch.c`): po sprawdzeniu A/B (15 s pulpitu albo ekranu hasła — od 0.10.9 krok 2, S3, liczone od
  `EKRAN_OBRAZ`: ekran mówi na swoim kanale sterującym, że jego pierwsza klatka jest na ekranie; bez tego wersja z i915
  i czarnym ekranem zostałaby zatwierdzona). Do 0.9.1 nadzorca mówił wtedy
  skarbcowi `START_POTWIERDZONY` (stary magazyn formatu 1 mógł przejść na format 2, ADR 0036) — usunięte w 0.10.0 razem z
  formatem 1; gdy na którymś dysku czeka próba A/B innej wersji, log „A/B: próba innej wersji czeka”.
- Sesje stref (0.10.0 krok 5, `strefy_ust.c`, format `sesje.c`): która sesja przeglądarki pamięta i jak się nazywa —
  w skarbcu (właściciel `K1_WL_SYSTEM`, obiekt 4); do gui, ekranu i Ustawień `STREFA_NAZWA` i `STREFY_STAN`, od Ustawień
  `STREFA_TRWALOSC`, `STREFA_NAZWA`, `STREFA_WYCZYSC` (do skarbca `SKARB_WYCZYSC`). Strefa, która pamięta, dostaje od
  `strefy.c` fd 8 (kanał do skarbca, właściciel = jej profil) i fd 9 (pusty dysk strefy) — `komponenty/strefa.md`.
  Od 0.10.9 krok 6 każda sesja ma też przełącznik „Szybka grafika” (format `K1Z2`: pole cech, `K1Z1` czytany —
  domyślne z wiersza `K1_STREFY`: Ogólna włączona, D5): `STREFA_GRAFIKA` od Ustawień, `STREFY_GRAFIKA` do gui, ekranu,
  Ustawień i Zasobów (a — przełączniki, b — strefy działające teraz ze swoim pośrednikiem: znak na ramce). Gdy przełącznik
  jest włączony, a pytanie przy starcie mówi, że karta obsługuje szybką grafikę (`posrednik_karta_umie`), `strefy.c`
  uruchamia z VMM-em pośrednika grafiki tej strefy (`posrednik_dla_strefy`) i daje VMM-owi jego kanał na fd 10 (fd 8, 9
  — `/dev/null`, gdy strefa zapomina) oraz `K1_POSR_PAMIEC` więcej pamięci (bufory karty zmapowane w VMM-ie);
  program testowy — strefa próbna w trybach 16 i 17 z silnikiem testowym („pamięć karty”: rzadki memfd od nadzorcy).
- Dekoder (0.8.0 krok 4, ADR 0032, `dekoder.c`): na `DEKODER` od klienta z tabeli klientów (0.8.2: Pliki — ten sam
  fd 6 co pendrive; Podgląd — jego fd 6, `podglad_kanal`, tylko `DEKODER`, nadawca UID 208; ADR 0037) — nowy proces `dekoder` w klatce (fd 3: kanał do klienta, obie strony z `SO_PASSCRED`; argument: numer
  aplikacji klienta), drugi koniec klientowi w `DEKODER_KANAL`. Jeden obraz na proces, jeden proces naraz, najwyżej
  jedna prośba w kolejce na klienta (prośba tego samego klienta w trakcie: `SIGKILL` starego, nowy po jego końcu;
  innego — czeka). Limity pamięci klatki z RAM: najwyżej 1/6 pamięci komputera, nie mniej niż 384 MiB (0.8.2).
  Szczegóły: `komponenty/dekoder.md`.
- Kanał do skarbca dla klienta (`SKARB_KLIENT` c, 0.6.3): 1 = partycja K1-DANE jest na USB; skarbiec
  oddaje to klientom w `SKARB_WYNIK` d (bit 1) — Notatki i Ustawienia piszą „dysk komputera” albo
  „pendrive K1”.
- Urządzenia wejścia (0.5.1): sprawdza (co 10 s, bez gniazda zdarzeń jądra co 2 s), czy pojawiło się nowe `event*` (szybki start może
  wyprzedzić rejestrację myszy przez jądro; później — urządzenia USB); jeśli tak, uruchamia `wejscie`
  ponownie z pełnym zestawem (bez liczenia jako awaria). Od 0.5.2 (M5a) zapomina urządzenia, których
  węzeł zniknął (odłączone USB) — ponownie podłączone pod tym samym numerem jest zauważane. Od 0.6.0 nowe
  i odłączone urządzenia zgłasza jądro od razu (sekcja „Zdarzenia jądra”); okresowe sprawdzanie zostało jako zapas.
- Hasło (0.5.7, ADR 0017): skarbiec zgłasza na kanale sterującym `SKARB_STAN`; „zablokowany” →
  uruchamia `logowanie` zamiast `gui` (fd 4 prośby — tylko `WYLACZ`, fd 5 bateria, fd 6 skarbiec);
  „odblokowany” → zatrzymuje logowanie, restartuje ekran, potem pulpit. Ustawienia dostają fd 5 do skarbca.
- Skarbiec (0.5.6, ADR 0015/0016): uruchamiany **przed** ekranem; partycja danych = dokładnie jedna
  partycja GPT o nazwie „K1-DANE” (sysfs `PARTNAME`), otwierana `O_RDWR|O_EXCL` i przekazywana jako
  fd 4; czeka na nią do 3 s po starcie, a gdy usb-storage ma urządzenie bez dysku — do 15 s (pendrive
  bez K1-DANE, np. z plikiem .iso, nie wstrzymuje startu). Od 0.5.8 szuka jej też przez 2 min po starcie
  (co 2 s, gdy skarbiec działa bez partycji): znaleziona → restart skarbca, z nim ekranu i pulpitu
  (pendrive zgłoszony później — wolny albo przełączony z EHCI na xHCI w X230). Kanały klientów
  (`kanal_do_skarbca`: ekran fd 5, Notatki fd 4) zgłasza skarbcowi przez `SKARB_KLIENT` z właścicielem;
  oba końce mają `SO_PASSCRED`. Koniec skarbca → restart ekranu (klienci dostają nowe kanały).
- Instalator (0.5.9, ADR 0018): lista dysków z sysfs (`dyski.c`), na prośbę okna Instalatora (UID 204,
  fd 4 w obie strony) otwiera źródło (K1-EFI pendrive'a, odczyt) i cel (cały dysk albo jego K1-EFI,
  `O_EXCL`) i uruchamia program `instalator` (UID 105) z nimi jako fd 4 i 5; postęp i wynik przekazuje
  oknu; `WYLACZ` od okna tylko po udanej instalacji. Dwie partycje K1-DANE: wybiera tę z dysku
  wewnętrznego (nie USB).
- Aktualizacje (0.10.5 krok 1, plan T1): ten sam zapis ma drugie okno — Ustawienia → Aktualizacje, osobnym kanałem
  (`KANAL_AKTUALIZACJI`, fd 7 Ustawień, nadawca UID 202). Kto o co może prosić, rozstrzyga czysta funkcja
  `inst_dozwolone` (`inst_prawa.c`, test `test_inst_prawa`): Instalator — tylko instalacja (`INST_START` z `K1_INST_INSTALUJ`),
  Secure Boot, link do Ustawień; Ustawienia — tylko aktualizacja (`K1_INST_AKTUALIZUJ`, nigdy wyczyszczenie dysku),
  ponowne uruchomienie po udanej aktualizacji. Każde okno ma swoją listę dysków; postęp i wynik idą do okna, które
  zadanie zaczęło. Zapis naraz najwyżej jeden: prośba, gdy program zapisujący działa → `INST_WYNIK` z
  `K1_INST_BLAD_TRWA` („Trwa już zapis K1 na dysk w drugim oknie”). `OTWORZ_AKTUALIZACJE` (od Instalatora albo pulpitu):
  nadzorca uruchamia Ustawienia, jeśli nie działają, wysyła im `AKT_POKAZ` (pokaż sekcję) i pulpitowi `POKAZ_APL`
  (okno na wierzch). `INST_AB` e = 1: manifest działającego miejsca jest podpisany (kafel wersji: „Zweryfikowano”).
- Bateria (0.5.3, ADR 0014): co 15 s `src/nadzorca/bateria.c` czyta `sys/class/power_supply` (baterie
  systemowe: procent z energii/ładunku/`capacity`, stan, zasilacz) i przy zmianie wysyła `BATERIA` do
  gui kanałem fd 5 (jednokierunkowo, `SHUT_RD`); test na hoście `test_bateria` (także śmieciowe dane).
  Od 0.11.8 krok 3 także `BATERIA_POMIAR` (pobór mocy, uśredniony jak dla Zasobów, i minuty do pustej / pełnej) — po
  zmianie, tylko do gui (UID 102), dla okienka baterii na górnym pasku. Gui mówi `BATERIA_OKIENKO` (fd 4), gdy okienko się
  otwiera i zamyka: póki jest otwarte, odczyt co 5 s (`BATERIA_CO_ZASOBY`, jak przy otwartych Zasobach) i raz od razu przy
  otwarciu; log „okienko baterii otwarte: odczyt baterii co 5 s” / „zamknięte: … co 15 s”. a = 2 („Ustawienia baterii”):
  Ustawienia, `BATERIA_POKAZ` do nich (pokaż sekcję Bateria), `POKAZ_APL` do gui; log „otwieram Ustawienia → Bateria”.
  Nowy kanał do gui: okienko uznane za zamknięte, pomiar idzie od nowa.
  Od 0.5.5: historia od uruchomienia — próbka co 30 s (procent + bit zasilacza), do 1440 próbek; po
  zapełnieniu łączy pary (odstęp ×2), więc zawsze obejmuje cały czas od startu. Ustawienia dostają stan
  i historię kanałem fd 4 (jednokierunkowo); przy pełnym buforze gniazda reszta idzie w następnej pętli.
- Zasoby (M4.1, ADR 0012): gdy aplikacja działa, co sekundę `ZASOBY_SYSTEM` (procesor ‰ z
  `cpu.stat` korzenia cgroup, pamięć: `sysinfo` minus odzyskiwalna pamięć podręczna z `memory.stat`,
  liczba CPU, czas pracy) i `ZASOBY_USLUGA` dla każdej usługi (`cpu.stat`, `memory.current` jej
  cgroup). Kanał jednokierunkowy: po stronie nadzorcy `shutdown(SHUT_RD)` — nic od aplikacji nie czyta.
  Od 0.10.10 (krok 2, `karta_pomiar.c`) także `ZASOBY_KARTA`: karta i915 (`drm/cardN`, sterownik i915) —
  czas snu karty (→ obciążenie, czyste `k1_karta_pomiar.c`), taktowanie teraz i najwyższe — od 0.10.11 (krok 3) z tabeli
  `K1_KARTA_PLIKI`: najpierw `gt/gt0/` (część graficzna; Meteor / Arrow Lake mają też `gt1` — media — a pliki samej karty
  łączą obie), potem `power/rc6_residency_ms`, `gt_act_freq_mhz`, `gt_RP0_freq_mhz`;
  RAPL (`powercap/intel-rapl:0:K` o nazwie „uncore”) — `energy_uj` z zawinięciem przy `max_energy_range_uj` (plik tylko dla
  root — nadzorca daje Zasobom średnią z sekundy); sesje z działającym pośrednikiem grafiki. Raz w logu: „zasoby: karta
  graficzna — cardN; pobór mocy karty: RAPL uncore”.
- Logi usług: wiersz oczyszczany przez `k1_log_oczysc` (znaki sterujące C0/DEL/C1, błędne UTF-8 → `?`),
  sprawdzany prefiks usługi, limit 20 wierszy/s.
- Zakończenie usługi → wpis w logu (kod 159 = zabronione wywołanie systemowe) → restart po
  0,2 / 1 / 2 / 4 / 4 s; 6. zakończenie w ciągu 60 s → „wyłączony”.
- Zakończenie ekranu → klienci zatrzymani (bez liczenia jako awarię) i uruchomieni ponownie.
- `SIGTERM`/`SIGINT` (od init przy wyłączaniu; od 0.8.6 nadzorca dostaje go pierwszy i sam) → ostatni zapis
  historii baterii (najwyżej 1,5 s), zatrzymanie usług i koniec.
- Nieudane uruchomienie skarbca albo ekranu (`fork`, kanał) → ponowna próba po 1 s (od 0.8.6; wcześniej usługa
  zostawała zatrzymana — klienci ekranu zawsze próbowali ponownie).

## Wywołania systemowe
`signalfd`, `poll`, `pipe2`, `socketpair`, `setsockopt`, `sendmsg`/`recvmsg`, `fork`, `dup2`, `fcntl`,
`open` (karta, `event*`, `/dev/null`, pliki cgroup), `mkdir` (cgroup), `kill`, `wait4`, `execve`;
w dziecku: `prctl`, `setgroups`, `setres[ug]id`, `capget`, `setrlimit`, `landlock_*`, `seccomp`.

## Obsługa błędów
Brak `/dev/null`, `signalfd` albo kontrolerów cgroup → `k1_koniec()` (init wyłącza system). Nieudane
nałożenie klatki w dziecku → wpis w logu i kod 126 (usługa **nie** startuje bez klatki — fail closed),
potem zwykłe reguły restartu.

## Powierzchnia ataku
Wiersze logu od usług (bufor 400 B, znaki sterujące zamieniane, limit 20/s), komunikaty 32 B
(`k1_komunikat_sprawdz`, testy + fuzzing), statusy procesów.

## Testy
| Test | Rodzaj |
|---|---|
| klatki skarbca, ekranu, wejścia, sieci (0.9.0), gui i każdej aplikacji z `K1_APLIKACJE` (`t_klatki`): własny UID/GID, brak grup i capabilities, `NoNewPrivs`, 2 filtry seccomp, własna cgroup z limitami, tylko przekazane deskryptory (`/proc/<pid>/status`, `fd`, `cgroup`) | funkcjonalny |
| próby w klatce **każdego** profilu z `k1_uslugi.h` (`t_proby`, program testowy uruchomiony w cudzej klatce): 27 zabronionych działań (inny program, `open`, `setuid`, `socket` — od 0.9.0 także `AF_INET`, `AF_PACKET`, `AF_NETLINK`,
`socketpair(AF_INET)`, `ioctl(SIOCSIFADDR)`, `setsockopt(SO_DETACH_FILTER)` —, `fork`, `clone`, `kill`, `ptrace`, `mount`, `unshare`, `chroot`, `perf_event_open`, `keyctl`, `memfd_create` z `MFD_EXEC`, pamięć wykonywalna, obcy `ioctl`, `execve`…) | negatywny |
| awaria ekranu → nowy ekran i nowe wejscie | funkcjonalny |
| 6 zabić wejscie → usługa wyłączona | negatywny |

## A/B i Secure Boot (0.7.0, ADR 0022)
- `rozruch.c`: przy starcie czyta `/rdzen/system/wydanie` (identyfikator wydania); 15 s po pulpicie albo
  ekranie hasła sprawdza K1-EFI każdego dysku wewnętrznego (`O_EXCL`): próba, która wystartowała i ma to
  samo wydanie → `k1_slot`; próba, z której GRUB wrócił → wyczyszczona i zgłoszona w Ustawieniach → Aktualizacje
  (`INST_AB` d) i raz pulpitowi (`AB_NIEUDANA`, fd 5 gui, nie ekranowi hasła: wersja, która nie wystartowała, i ta, która
  działa — okienko „Nowa wersja K1 nie uruchomiła się”, 0.10.5 krok 1). Pisze tylko 1024 B grubenv. Pendrive'ów nie rusza.
- `efi.c`: efivarfs montowany na chwilę pod `/rdzen/jadro/sys/firmware/efi/efivars`; stan Secure Boot
  (`SecureBoot`), klucz K1 w `MokListRT*`, prośba czekająca (`MokNew`); na `SB_KLUCZ` (tylko klucz wydania,
  `K1_SB_PRAWDZIWY`) zapisuje `MokNew` i `MokAuth` (SHA-256, `k1_sha256.c`). Od 0.8.5 `MokNew` czekający z wcześniejszego
  startu też pozwala na ponowne uruchomienie z okna Instalatora (`sb_czeka` ustawiane przy odczycie stanu). Po zapisie okno Instalatora może
  poprosić o restart (`WYLACZ` a=1).

## Rozdzielacz — pamięć komputera dla wszystkich (0.10.1 krok 3, ADR 0044 T6, decyzja właściciela D3)
Raz na sekundę, gdy działa strefa, którą rządzi rozdzielacz (każda uruchomiona przez gui; z programu testowego tylko
w trybie 10), `strefy_balon.c` liczy, ile K1 ma wolnej pamięci (wolna + pamięć podręczna plików, którą jądro może
zrzucić; tmpfs i pamięć stref — zajęta; to samo co Zasoby: `pamiec_uzyta`), i daje to czystej funkcji
`pamiec_rozdziel` (`pamiec_stref.c`, test na hoście `test_pamiec_stref`) razem z tym, co każda strefa powiedziała
o sobie (`STREFA_PAMIEC` — podpowiedź, nie prawda; twarda granica to `memory.max` klatki VMM). Progi w jednej tabeli
(`PROGI`): **rezerwa K1** (768 MiB do 6 GiB pamięci, 1 GiB od 6 GiB — tabela `REZERWA`), zapas wolnej pamięci gościa
(256 MiB, najwyżej ćwierć jego granicy — na nagły skok), krok balonu (256 MiB na sekundę), histereza (128 MiB).
- K1 poniżej rezerwy → balony rosną: najpierw sesje w tle (ta, która może oddać najwięcej, pierwsza), sesja z fokusem
  na końcu i nigdy poniżej swojej pamięci na start (D3: „do jej minimum”); żadna nigdy w zapas gościa. Balon daje K1
  tylko pamięć, którą gość trzyma, a której nie potrzebuje (jego pamięć podręczna) — wolne strony gość oddał już sam
  (zgłaszanie), więc cel balonu to „gość tak duży, ile K1 ma go trzymać”: granica − (pamięć w K1 − to, co ma wrócić).
  To, o co już poproszono, a gość jeszcze nie oddał, się liczy; gość zawsze zostaje z co najmniej dwoma zapasami (sufit
  celu — statystyki gościa przychodzą co kilka sekund, VMM odejmuje od nich to, co balon wziął od tamtej pory). K1 ma rezerwę i 128 MiB więcej → balony maleją (najpierw
  sesja z fokusem).
- Żaden balon nie może wziąć więcej, a K1 dalej poniżej rezerwy (2 s z rzędu) → gui dostaje przydział każdej sesji
  (`STREFA_PRZYDZIAL`) i `MALO_PAMIECI`: okienko „Mało pamięci” (D3 (2); najwyżej raz na 2 min), log „mało pamięci: K1 ma
  N MiB (rezerwa R), balony nie mogą wziąć więcej — okienko”.
- Do tego K1 ma mniej niż ćwierć rezerwy (3 s z rzędu) → nadzorca zamyka sesję w tle z największą pamięcią (SIGKILL VMM;
  D3 (3), nigdy sesji z fokusem) i gui mówi „<tytuł> zamknęła się — Komputerowi zabrakło pamięci…” (`STREFA_STAN`
  `_PAMIEC_K1`); log „brak pamięci: K1 ma N MiB — zamykam strefę S (w tle, M MiB)”.
- Fokus: gui wysyła `STREFA_FOKUS`, gdy zmienia się aktywne okno (log „fokus: strefa N” / „żadna strefa”).
- `oom_score_adj` VMM: 800 w tle, 500 z fokusem (usługi K1: 0) — gdy cały komputer naprawdę nie ma pamięci, jądro
  wybiera VMM, nie usługę K1.
- Start sesji (`jest_pamiec`, S5): wolna pamięć K1 + to, co balony działających sesji mogą jeszcze oddać ≥ pamięć na
  start + 64 MiB.
- Zasoby dostają co sekundę przydział każdej działającej strefy (`STREFA_PRZYDZIAL`) i nazwy sesji (`STREFA_NAZWA`).
- `STREFA_PAMIEC` częściej niż co 0,5 s — nadmiar odrzucony (raz w logu), ostatnie liczby zostają. Na końcu strefy
  log „pamięć na końcu — w K1 …, wolne w gościu … (−1: statystyki nie doszły), balon …, oddane …”; przy okienku „Mało
  pamięci” liczby każdej strefy (w K1, wolne, cel).
- W logu jedna linia na sekundę najwyżej, gdy cel się zmienił: „rozdzielacz: K1 ma N MiB (rezerwa R); cel balonu:
  strefa S M MiB, …” — same liczby.

