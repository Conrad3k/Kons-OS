# Aktualizacja przez sieć (0.10.5 krok 2, ADR 0046)

**Cel:** K1 zainstalowany na dysku sam sprawdza, czy w najnowszym wydaniu na GitHubie jest nowsza wersja, pobiera ją
i czeka z nią na „Zainstaluj”. Bez tej zgody nic się nie zmienia w tym, co startuje. Skąd, w jakiej postaci i dlaczego:
ADR 0046; od 0.13.0 łańcuch wydań — ADR 0056 (zwłoka wydania, ogłoszenie formatu 2 z terminem, czas z HTTPS). Pojęcia:
**ogłoszenie wydania**, **paczka aktualizacji**, **poczekalnia**, **strefa pobierania**, **opis wersji**, **zwłoka wydania**,
**ogłoszenie dnia**, **stopka strefy pobierania** (`docs/SLOWNIK.md`).

## Granica zaufania
Wszystko, co przychodzi z sieci, jest wrogie — także od GitHuba. K1 ufa tylko podpisowi kluczem K1 (manifesty
i ogłoszenie, EdDSA). Bajty z sieci czyta tylko **program zapisujący** (`instalator`, UID 105, klatka bez ścieżek).
Nadzorca podaje je dalej, nie czytając. Pobiera **strefa pobierania** (mikro-maszyna, curl z Debiana). Przejęta strefa
może najwyżej nie podać wersji albo podać śmieci — śmieci odrzuca program zapisujący. Od 0.13.0 (ADR 0056): przejęte
konto GitHub albo CI wyda podpisaną wersję — ale laptop zainstaluje ją najwcześniej **20 minut** po pobraniu i tylko,
jeśli wydanie wtedy wciąż jest na GitHubie (właściciel ma czas je usunąć); stare ogłoszenie podsunięte przez łącze
działa najwyżej 7 dni (termin ważności); czas do terminów — z nagłówka `Date` za TLS-em, nie z SNTP.

## Części
| Część | Plik | Co robi |
|---|---|---|
| ogłoszenie wydania | `src/wspolne/k1_ogloszenie.{h,c}`, `k1_ogloszenie_podpis.c` | format (do 8 KiB): wersja, skrót paczki, opisy wersji, podpis; zasady tekstu (T18); od 0.13.0 format 2 z czasem podpisu (`k1-ogloszenie2.bin`), świeże 7 dni (`k1_ogl_swieze`) |
| paczka, poczekalnia | `src/wspolne/k1_paczka.{h,c}` | nagłówek paczki, przód i tył K1-EFI, znak poczekalni (od 0.13.0 format 2: widziane, potwierdzone; `k1_pocz_zwloka`) |
| stopka strefy pobierania | `src/wspolne/k1_stopka.{h,c}` | 0.13.0: 16 B za każdym całym plikiem — czas z nagłówka `Date`; `k1_data_http` |
| narzędzie wydania | `narzedzia/wydanie.c`, `scripts/opisy-wersji.py` | paczka i oba ogłoszenia przy budowie (`scripts/zbuduj-usb.sh`); `odswiez` — „Ogłoszenie dnia” |
| „Ogłoszenie dnia” | `.github/workflows/ogloszenie.yml` | 0.13.0: co dzień `k1-ogloszenie2.bin` najnowszego wydania podpisane od nowa (`docs/komponenty/robot.md`) |
| program zapisujący | `src/instalator/siec.c`, `siec_instaluj.c` | tryby `STAN`, `POBIERZ`, `ZAINSTALUJ`, `WROC` (`docs/komponenty/instalator.md`) |
| nadzorca | `src/nadzorca/aktualizacja.c`, `aktualizacja_ust.c` | kiedy sprawdzać, strefa i program zapisujący, stan dla okien, „Sprawdzaj sam” w skarbcu |
| strefa pobierania | wiersz `POBIERANIE` w `K1_STREFY` (bez okna), `src/strefa/gosc/pobieranie.c` | curl przez przekaźnik; bajty do K1 połączeniem `pobrane.k1` |
| łącznik | `src/strefa/lacznik.c` (`lacznik_cel`, `K1_HOSTY_POBIERANIA`) | strefa pobierania łączy się tylko z hostami wydań GitHuba |
| Ustawienia | `src/aplikacje/ustawienia/aktualizacje_siec.c` | stan, „Sprawdź teraz”, „Sprawdzaj sam”, opis, „Zainstaluj…”, „Wróć…” |
| gui | `src/gui/aktualizacja.c` | okienko „Jest nowa wersja K1”, kropka przy Start |

## Droga danych
1. **Kiedy** (D4): 30 s po tym, jak K1 ma sieć, potem co 6 godzin — gdy „Sprawdzaj sam” jest włączone (domyślnie tak;
   nadzorca trzyma to w skarbcu, obiekt `K1_OB_AKTUALIZACJE` właściciela `K1_WL_SYSTEM`). Dopóki ustawienie nie jest
   wczytane, K1 sam nie sprawdza. „Sprawdź teraz” w Ustawieniach — zawsze. Raz po starcie pulpitu, bez sieci: tryb `STAN`
   (co czeka, co jest poprzednią wersją).
2. **Sprawdzenie:** nadzorca tworzy parę gniazd. Jeden koniec dostaje program zapisujący (tryb `STAN`, fd 4), drugi czeka
   na strefę pobierania. Strefę nadzorca uruchamia z celem `latest/download/k1-ogloszenie2.bin` (0.13.0; do 0.12.x
   `k1-ogloszenie.bin`, który każde wydanie wciąż ma — w formacie 1 — dla starszych K1) (argv[4] VMM →
   `k1.pobierz=` jądra strefy). Gość (`gosc/pobieranie.c`) uruchamia przekaźnik, łączy się przez niego z `pobrane.k1:1` —
   VMM prosi wtedy nadzorcę o gniazdo (`STREFA_POLACZ` c = `K1_STREFA_POBRANE`) i dostaje jego koniec pary — i uruchamia
   curl (`--socks5-hostname`: nazwy rozwiązuje łącznik K1, tylko z `K1_HOSTY_POBIERANIA`), który pisze plik w to
   połączenie, a po całym pliku gość dokleja stopkę z czasem serwera (0.13.0). Program zapisujący sprawdza ogłoszenie
   (format 2, podpis, podpisane najwyżej 7 dni przed czasem serwera — inaczej „Nie mogę sprawdzić, czy masz najnowszą
   wersję”) i mówi `INST_SIEC`; przyjęte ogłoszenie zapisuje do memfd od nadzorcy. Nadzorca robi z niego zapieczętowaną
   kopię dla okien (`AKT_OPIS`).
3. **Pobranie:** gdy jest nowsza wersja, jest miejsce, nie ma próby w toku, a „Sprawdzaj sam” jest włączone (albo kliknięto
   „Sprawdź teraz”): tryb `POBIERZ`. Cel strefy nadzorca składa z samego numeru wersji
   (`download/k1-v<w>/k1-<w>.k1a`). Paczka idzie do poczekalni, potem jeszcze raz `STAN` bez sieci.
3a. **Zwłoka wydania** (0.13.0, ADR 0056, D1: A): pobrana wersja czeka 20 minut od pobrania (znak poczekalni „widziane”,
   czas serwera). `AKT_STAN` mówi ją jako c z `K1_AKT_FL_ZWLOKA` (b = 0: nie ma jeszcze „Zainstaluj”); Ustawienia: „K1 X
   jest pobrana. Za kilka minut będzie gotowa.”. Po 21 minutach nadzorca sprawdza jeszcze raz (także przy wyłączonym
   „Sprawdzaj sam”): świeże ogłoszenie tego samego wydania → „potwierdzone”, `AKT_STAN` b = czeka; ogłoszenie starszej
   wersji (wydanie usunięte z GitHuba) → wersja usunięta z poczekalni.
4. **Pytanie:** gui pokazuje okienko „Jest nowa wersja K1” — raz na start pulpitu i po pobraniu — z najnowszym opisem.
   Kropka przy Start świeci, dopóki wersja czeka. Ustawienia pokazują pełny opis.
5. **„Zainstaluj”** (Ustawienia albo okienko): tryb `ZAINSTALUJ`, potem ponowne uruchomienie. Nowa wersja startuje na
   próbę, potwierdza ją `rozruch.c` — jak po aktualizacji z pendrive'a.
6. **„Wróć do poprzedniej wersji”** (D7; tylko Ustawienia, z pytaniem): tryb `WROC`, potem ponowne uruchomienie.
   Wersji, z której wrócono, K1 nie proponuje znowu — dopiero następną (program zapisujący porównuje z wolnym miejscem).
7. **Pendrive** (D5): K1-EFI pendrive'a nie ma poczekalni. Tryb `STAN` mówi wtedy o nowszej wersji, a sekcja pokazuje
   „Jest nowsza wersja K1 X — nagraj nowy pendrive (README)”.

Limity: jedno zadanie naraz (wspólne z aktualizacją z pendrive'a), jedna strefa pobierania. Zadanie trwa najwyżej 40 min —
potem nadzorca kończy strefę i program zapisujący. curl: tylko `https`, przekierowania tylko na `https`, do 5, odrzuca
plik ponad 600 MB i łącze wolniejsze niż 1 KB/s przez 2 min.

## Komunikaty
| Komunikat | Kierunek | Pola |
|---|---|---|
| `INST_ZADANIE` | nadzorca → program zapisujący | a = `K1_INST_SIEC_*` (3…6), b = 512, c = 1 test aktualizacji |
| `INST_SIEC` | program zapisujący → nadzorca | a = działa, b = czeka, c = nowsza do pobrania, d = `K1_SIEC_FL_*`, e = poprzednia |
| `STREFA_POLACZ` c = `K1_STREFA_POBRANE` | VMM strefy pobierania → nadzorca | a = b = 0: koniec pary do programu zapisującego (raz); odpowiedź `STREFA_GNIAZDO` a = `POBRANE` + fd |
| `AKT_STAN` | nadzorca → Ustawienia (fd 7), gui (fd 5) | a = działa, b = czeka, c = nowsza w wydaniu (z `K1_AKT_FL_ZWLOKA`: pobrana, w zwłoce), d = poprzednia, e = `K1_AKT_FL_*` (nowa linia jądra, pendrive, „Sprawdzaj sam”, próba w toku, K1 na dysku, 0.13.0: zwłoka wydania) |
| `AKT_ETAP` | nadzorca → Ustawienia, gui | a = `K1_AKT_E_*` (sprawdza, pobiera, instaluje, wraca), b = procent, c = `K1_AKT_W_*` ostatnie sprawdzenie (0.13.0: też `STARE` — ogłoszenie podpisane ponad 7 dni temu), d = kiedy (s), e = wynik ostatniego zadania |
| `AKT_OPIS` | nadzorca → Ustawienia, gui | a = bajty; + memfd zapieczętowany z ogłoszeniem (odbiorca czyta je `k1_ogloszenie_czytaj`) |
| `AKT_PROSBA` | Ustawienia (fd 7; gui fd 5 — tylko `ZAINSTALUJ`) → nadzorca | a = `K1_AKT_P_*`: sprawdź, zainstaluj, wróć, „Sprawdzaj sam” wł./wył. |

Kto o co może prosić: `inst_prawa.c` (Ustawienia — `AKT_PROSBA`; Instalator — nie), `obsluz_gui` (gui — tylko
„Zainstaluj”). Złe pola odrzuca `k1_odbierz_komunikat` (`test_komunikat`).

## Test aktualizacji
Obraz testowy z dyskiem o numerze seryjnym `k1-test-aktualizacji` (bez niego obraz testowy nigdy nie sprawdza sam).
- Strefa pobierania łączy się z serwerem testowym na „routerze” QEMU (`https://10.0.2.2:8443/`): adres podany liczbami
  jest dozwolony tylko w tym trybie, a certyfikat nie jest sprawdzany (`-k`).
- Wersja deweloperska przyjmuje pliki bez podpisu. Z kluczem (CI z sekretem) podpis sprawdzany jest zawsze.
- 0.13.0: nadzorca sprawdza wydanie po zwłoce już po 20 s (nie po 21 min), a serwer testowy przesuwa swój nagłówek
  `Date` (po paczce o 25 min, w przebiegu 3 o 8 dni) — zwłoka mija i ogłoszenie starzeje się bez czekania.

Scenariusz: `scripts/test-aktualizacja-siec.sh` (`docs/TESTY.md`).

## Testy
- Na hoście: `test_ogloszenie` (od 0.13.0: format 2, termin, zgodność z zamrożonym czytnikiem 0.12.3), `test_paczka`
  (znak 2, zwłoka), `test_stopka`, `test_siec_instalatora` (cała droga programu zapisującego na obrazach K1-EFI; od
  0.13.0 stopka, za stare / odświeżone ogłoszenie, zwłoka, wycofanie), `test_lacznik` (lista hostów, cel strefy),
  `test_komunikat`, `test_inst_prawa`; `obrazy_paczki.sh` (`wydanie odswiez` i jego odmowy). Fuzzing: `fuzz_ogloszenie`,
  `fuzz_paczka`, `fuzz_stopka`.
- QEMU: `scripts/test-aktualizacja-siec.sh`.
