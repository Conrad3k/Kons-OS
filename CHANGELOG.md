# Historia zmian K1

Format: każda wersja ma sekcje **Dodane**, **Zmienione**, **Naprawione**, **Usunięte** (tylko potrzebne).
Numeracja: [docs/ARCHITEKTURA.md](docs/ARCHITEKTURA.md#wersjonowanie). Tagi wersji K1: `k1-vX.Y.Z`.

## [0.8.1] — 2026-11-11

**Pliki — dopracowanie, część 1** (`docs/PLAN-0.8.1.txt`, kroki 1–4; decyzja właściciela D9: wydanie teraz, reszta —
`docs/PLAN-0.8.2.txt`). Najważniejsze dla danych: nic nie ginie, mieści się dużo więcej, widać, ile miejsca zostało, a stary
pendrive można powiększyć bez utraty danych.

### Dodane
- **Ustawienia → Informacje: miejsce na pliki** (krok 3) — „zajęte X z Y” z paskiem i „Pliki i foldery: n z 16 384”;
  bez K1-DANE: ile zostało w pamięci do wyłączenia.
- **„Użyj całego pendrive'a na dane”** (krok 3, ADR 0033) — pendrive nagrany wersją 0.5.6–0.7.6 (ok. 30 MB na pliki)
  powiększa się jednym kliknięciem w Informacjach; pliki, notatki i ustawienia zostają na miejscu, przerwa prądu w
  trakcie niczego nie psuje (start dokańcza powiększenie).
- **Pliki do 4 GB** (krok 4, ADR 0034) — np. filmy z pendrive'a; plik idzie kawałkami po 1 MB, więc K1 nie potrzebuje
  na to więcej pamięci. Przy dużym pliku stopka pokazuje postęp („120 z 300 MB”); wyjęty w trakcie pendrive nie zostawia
  śmieci.
- Pliki mówią w stopce „Mało miejsca na pliki”, gdy zostało mniej niż 10 %.

### Znane ograniczenie
- Z dużą ilością danych start trwa dłużej (skarbiec sprawdza przy starcie wszystkie dane — ok. 30 s na 1 GB
  z pendrive'a USB 2.0). Pulpit nie czeka: ustawienia i tapeta ze zdjęcia pojawiają się, gdy skarbiec skończy.
  Poprawka formatu skarbca — pierwszy krok 0.8.2 (`docs/PLAN-0.8.2.txt`, RYZYKA R58).

### Zmienione
- **Dużo więcej plików** (krok 2, ADR 0034): do 16 384 plików i folderów (było 1024) i ok. 64 GB plików na dużym
  dysku (było ok. 4 GB).
- **Start nie zwalnia z czasem**: skarbiec porządkuje dziennik (kompaktowanie) przy starcie i w wolnej chwili, zanim
  urośnie.
- **Pełny dysk nie blokuje systemu**: pliki nie zajmą ostatnich 8 MB (albo 2 %) — ustawienia, pulpit, tapeta i historia
  baterii zapisują się dalej („Brak miejsca na pliki”).
- Notatki pokazują do 256 najnowszych notatek (było 64); aplikacja dostaje najwyżej 64 pliki naraz — 65. z komunikatem.
- Plik większy niż 16 MB z pendrive'a nie jest już pomijany; podgląd zdjęcia — dalej do 16 MB.

### Naprawione
- **Uszkodzony wpis pliku kasował plik przy starcie** (krok 1) — teraz skarbiec go naprawia („Odzyskany plik 123”,
  złe znaki nazwy → „_”), a treść zostaje cała; tak samo przy złej generacji treści.
- **Zły zegar komputera opróżniał Kosz** (słaba bateria zegara: rok 2000) albo blokował go na lata — Kosz nic nie usuwa,
  gdy zegar jest wcześniejszy niż czas zbudowania wersji albo najpóźniejszy widziany; daty plików się nie cofają.
- **Notatki mogły zgubić tekst**: notatka nie do odczytu jest tylko do odczytu (nie nadpisze oryginału pustą), a gdy
  jej plik trafi do Kosza w trakcie pisania, tekst zapisuje się jako kopia.
- Folder „Notatki” mnożył się („Notatki (2)”, „(3)”) — K1 pamięta go po numerze.
- Przywrócony plik, którego folder jest w Koszu, ląduje w Dokumentach — Pliki teraz to mówią.

### Testy
- Host: `test_dokumenty` (naprawa wpisów, zegar, 16 384 wpisy, rezerwa, zapis w toku i jego przerwania), nowe
  `test_notatki`, `test_indeks`; `test_magazyn` (65 536 obiektów, długi dziennik, kompaktowanie, **powiększenie z przerwą
  prądu po każdym zapisie**), `test_komunikat`, `test_fat` (odczyt od miejsca); fuzzing `fuzz_dokument`, `fuzz_fat`.
- QEMU: nowy `test-skarbiec.sh` (długi dziennik, pełna K1-DANE, powiększenie pełnego pendrive'a przyciskiem i import
  16 zdjęć 16/16, dokończenie przerwanego powiększenia); `test-pendrive.sh` (plik 17 MB i film 300 MB z pendrive'a).
- Ekran wypisuje linię stanu także po zmianie samej liczby odrzuconych zdarzeń — test złośliwych wiadomości
  (M1) nie zależy już od ruchu kursora.
- CI: instalacja narzędzi nie zależy od jednego serwera z pakietami (11.11 serwery Ubuntu w chmurze GitHuba nie
  odpowiadały i budowa 0.8.1 dwa razy kończyła się po 10 minutach): pomija serwer, który nie odpowiada; gdy nie
  odpowiada żaden serwer Ubuntu — bierze pakiety z ich kopii na kernel.org; gdy i ta zawiedzie — instaluje z kopii
  pakietów zapisanej przez poprzednią udaną budowę (pamięć podręczna GitHuba, nie limit artefaktów).

## [0.8.0] — 2026-11-10

**Pliki** — wspólne pliki i foldery w zaszyfrowanym skarbcu, jak w Windows; zdjęcia z pendrive'a, ich podgląd i tapeta
ze zdjęcia (ADR 0026, 0029–0033). Pięć kroków, jedno wydanie.

### Dodane
- **Aplikacja Pliki** (krok 1): z lewej Pulpit, Dokumenty, Obrazy, Muzyka, Wideo, Pobrane i Kosz; własne foldery
  w środku, „w górę” i ścieżka, „Nowy folder”, „Zmień nazwę” (F2), „Usuń” (Delete, do Kosza); z prawej szczegóły
  zaznaczonego pliku: typ rozpoznany z zawartości, rozmiar, daty, pochodzenie, początek tekstu.
- **Kosz**: „Przywróć”, „Usuń na zawsze”, „Opróżnij Kosz”; pliki znikają same po 30 dniach. Kosz i pliki z folderu
  Pulpit leżą na pulpicie jako ikony.
- **Otwieranie plików** (krok 2): podwójne kliknięcie otwiera plik w jego domyślnej aplikacji, „Otwórz w…” — w wybranej
  (Pliki i pulpit); domyślne aplikacje w Ustawienia → Aplikacje. Aplikacja dostaje dostęp tylko do tego jednego pliku.
- **Okno „Wybór pliku”** rysowane przez K1: aplikacja, która prosi o plik (Notatki: „Otwórz…”, Ctrl+O), dostaje tylko
  ten, który wskażesz — nigdy listę plików.
- **Notatki na plikach**: każda notatka to plik w Dokumenty › Notatki (nazwa z pierwszego wiersza), usuwana do Kosza;
  notatki z poprzednich wersji przenoszą się same przy pierwszym uruchomieniu.
- **Zdjęcia z pendrive'a** (krok 3): włożony pendrive (FAT12/16/32, np. z telefonu albo aparatu) to miejsce „Pendrive”
  w Plikach — tylko do odczytu; „Importuj” / „Importuj wszystkie”: zdjęcia do Obrazów, reszta do Dokumentów,
  pochodzenie „z pendrive'a”. Pendrive czyta mała usługa `czytnik` w klatce — jądro dalej nie dotyka obcego systemu
  plików. exFAT i NTFS — jeszcze nie (Pliki mówią to wprost).
- **Podgląd zdjęć** (krok 4): zaznaczone zdjęcie PNG albo JPEG widać w Plikach po prawej (obrócone tak, jak trzymałeś
  telefon). Zdjęcie odczytuje usługa `dekoder` — osobny, krótko żyjący proces w klatce dla każdego zdjęcia (biblioteka
  Wuffs, ADR 0032); uszkodzony plik: „Podgląd niedostępny”.
- **Tapeta ze zdjęcia**: pod podglądem przycisk „Ustaw jako tapetę” — zdjęcie wypełnia ekran przy każdej rozdzielczości,
  zostaje po wyłączeniu komputera (zaszyfrowane w skarbcu), a w Ustawienia → Wygląd pojawia się kafelek „Zdjęcie”.
- **Cały pendrive na dane** (krok 5, ADR 0033): K1 zapisany na pendrive większym niż obraz powiększa przy pierwszym
  starcie swoją pustą partycję danych do końca pendrive'a (było 64 MB na zawsze). Pendrive z danymi nie jest ruszany.

### Zmienione
- Skarbiec przechowuje do 4096 obiektów; pliki do 16 MB (w kawałkach po 1 MiB), z nazwą, typem, czasami i pochodzeniem.
- Pulpit zna pliki z folderu Pulpit i Kosz („Otwórz”, „Otwórz w: …”); ułożenie pulpitu w nowym formacie (stary dalej
  czytany).
- Ekran: miejsce na 16 klientów, zdjęcie na tapetę (do 2560×1600); klatka ekranu z większym limitem pamięci.
- Klatka usługi może mieć limit czasu procesora i przestrzeni adresowej (SPEC §7 — dekodery).

### Naprawione
- Obrazek nakładany na okno aplikacji (dziś: miniatura w Plikach) był przezroczysty — prześwitywała przez niego tapeta.

### Porządki (zasada czytelności 2)
- Spis usług systemu w budowie i w obrazie wynika z jednej listy `K1_SYSTEM` (`scripts/uslugi-systemu.sh`) — nowa
  usługa to o dwa miejsca mniej do zmiany.
- Podzielone przed rozbudową: nadzorca (`logi.c`), Pliki (`przyciski.c`), skarbiec (`obiekty.c`, `haslo.c`).
- Kanał Plików do nadzorcy (fd 6) obsługuje prośby o pendrive i o dekoder w jednym miejscu; wspólne skalowanie obrazów
  (`k1_skaluj`) dla dekodera i ekranu.
- Słownik: usunięty podwójny wpis „okno wyboru pliku”; nowe pojęcia: dekoder (usługa), podgląd, tapeta ze zdjęcia.
- Przegląd „Tymczasowe”: manifest formatu 1 zostaje do 0.9.0 — oprócz aktualizacji z 0.6.x sprawdza on przy
  instalacji pliki startowe (shim, GRUB), więc najpierw trzeba to sprawdzanie zastąpić. Przeniesienie starych notatek —
  do 0.9.0 (bez zmian).

### Testy
- Host: `test_dokumenty`, `test_fat`, `test_dekoder` (obrazy z Pillow, uszkodzone, EXIF), `test_instalator`
  (powiększanie GPT, sfdisk czyta wynik), `test_komunikat` (wszystkie nowe komunikaty); fuzzing `fuzz_dokument`,
  `fuzz_fat`, `fuzz_pendrive`, `fuzz_dekoder`.
- QEMU: `test-pliki.sh` (foldery, Kosz, Notatki na plikach, otwieranie, okno wyboru), `test-pendrive.sh` (wirtualny
  pendrive USB ze zdjęciami: import, podgląd JPEG/PNG, uszkodzony JPEG, tapeta na zrzucie i po ponownym starcie, wyjęcie,
  NTFS), `test-m5.sh` (obraz na większym pendrivie: K1-DANE 64 → 576 MiB przy pierwszym starcie, potem bez zmian).

## [0.7.6] — 2026-11-09

Mała aktualizacja przed 0.8.0 (prośba właściciela).

### Dodane
- **Nowa tapeta „Las”** — zieleń mchu i szałwii na ciemnej sośnie, ze złotymi krawędziami arkuszy.
  Wybór: Ustawienia → Wygląd → Tapeta (miniatury w dwóch rzędach, po cztery).

### Zmienione
- **Instalator na zainstalowanym K1:** gdy system działa z dysku komputera i nie ma pendrive'a z nowszą
  wersją, okno pisze tylko „Aktualizacje są możliwe poprzez nośnik USB.” — bez instrukcji krok po kroku.
  Z podłączonym pendrive'em K1 wszystko jak dotąd („Zaktualizuj system”).

### Porządki
- Liczba tapet w jednym miejscu (`K1_TAPETY` w `k1_komunikat.h`): z niej wynika sprawdzanie ustawienia;
  kompilator pilnuje, żeby każda tapeta miała paletę i nazwę.

## [0.7.5] — 2026-11-09

Pulpit jak w Windows, w stylu K1 (ADR 0026, 0028; szkic A „Kafelki” wybrany przez właściciela).
Przed tym — porządki czytelności (decyzja właściciela z 2026-11-09, `AGENTS.md`).

### Dodane
- **Ikony aplikacji na pulpicie** — jasne kafelki w kolorze okien (w ciemnym motywie ciemne), pod nimi
  nazwa. Kliknięcie zaznacza, podwójne kliknięcie otwiera, przeciągnięcie przenosi ikonę na inne miejsce
  (jeśli tam leży inna, zamieniają się miejscami). Na start: wszystkie aplikacje oprócz Instalatora.
- **Menu pod prawym przyciskiem myszy** (w kolorach motywu): na pulpicie — „Ułóż ikony”, „Tapeta i
  wygląd…”; na ikonie — „Otwórz”, „Przypnij do paska” / „Odepnij od paska”, „Usuń z pulpitu”; na
  aplikacji na pasku — jej nazwa, przypnij / odepnij, „Zamknij okno”; na aplikacji w menu Start —
  „Otwórz”, przypnij / odepnij, „Dodaj do pulpitu” / „Usuń z pulpitu”.
- **Przypinanie do dolnego paska** i zmiana kolejności przeciąganiem; uruchomiona aplikacja spoza paska
  widoczna na nim (za kreską), dopóki działa. Pasek ma szerokość według liczby aplikacji.
- **Pulpit pamięta ułożenie** — ikony i pasek wracają po wyłączeniu komputera (zaszyfrowane w skarbcu).
- „Zamknij okno” z paska: aplikacja zamyka się sama, jak przyciskiem × — Notatki najpierw zapisują tekst.

### Zmienione
- Pulpit (gui) dostaje kanał do skarbca — widzi w nim tylko swoje rozmieszczenie (ADR 0028, RYZYKA R27).
- Ekran zna trzeci rodzaj powierzchni: warstwę pulpitu (nad tapetą, pod oknami; tylko dla pulpitu).
- Klatka gui i ekranu hasła: większe limity pamięci — duże ekrany (powyżej 1920×1080) mieściły się
  z trudem albo wcale (ekran hasła na 1920×1200).

### Porządki (zasada czytelności 2)
- **Limity wielkości sprawdzane w testach** (`scripts/sprawdz-czytelnosc.py`): plik ≤ 600 wierszy,
  funkcja ≤ 80; każda usługa i aplikacja ma opis, każdy komunikat — wpis w dokumencie swojego kanału;
  każde rozwiązanie tymczasowe — wpis w planie.
- Podzielone: pulpit (9 małych modułów), Ustawienia (Bateria, Hasło osobno), Notatki (zapis, edycja
  tekstu), Instalator — okno (każdy ekran osobno), skarbiec (nagłówek magazynu osobno); 7 długich funkcji
  (sprawdzanie komunikatów, nadzorca, ekran, bateria, initramfs) podzielonych na mniejsze.
- Dokumentacja: tabela komunikatów ekranu, kanał testowy nadzorcy, rozdział każdej aplikacji;
  nieaktualne opisy katalogu notatek usunięte. Kod formatu 1 manifestu oznaczony jako tymczasowy (0.8.0).

### Testy
- `test-m4`: ikony na zrzucie (jasne i ciemne kafelki), podwójne kliknięcie, menu kontekstowe (zamknij
  okno, ułóż ikony, odepnij, usuń i dodaj do pulpitu, przypnij), przeciąganie na pasku i na pulpicie,
  rozmieszczenie po ponownym starcie. Host: `test_rozmieszczenie`, fuzzing `fuzz_rozmieszczenie`.
  W systemie: warstwa pulpitu i zamykanie cudzych okien od aplikacji — odrzucone.

## [0.7.3] — 2026-11-09

Koniec porządków przed M7 (ADR 0023, `docs/PRZED-M7.md` — usunięty, historia w git). Dla użytkownika
system działa tak samo; zmieniły się kod i testy.

### Naprawione
- **Kliknięcie aplikacji tuż po starcie.** Przez pierwsze 5 sekund od włączenia komputera pulpit
  ignorował kliknięcie w aplikację (w menu Start i na pasku) — uznawał, że „już się uruchamia”.
  Widać to było na szybkim komputerze; wykryły to nowe, szybsze testy w CI.

### Zmienione
- Notatki: pod notatką tylko „Zapisano (zaszyfrowane)” — bez informacji, czy na pendrivie, czy na dysku
  (prośba właściciela; miejsce danych pokazują Ustawienia → Informacje).
- Kod: jedna funkcja czasu od startu (`k1_teraz`, było 5 kopii), jedna do czytania małych plików z
  sysfs (`k1_czytaj_plik`, było 5), wysokość górnego paska w jednym miejscu dla ekranu i pulpitu,
  Instalator rysuje przyciski wspólnym `k1_przycisk`, GPT zapisuje liczby przez `k1_bajty.h`;
  38 funkcji i zmiennych widocznych tylko tam, gdzie są używane (`static`); nieaktualne komentarze
  sprzed M5 poprawione.

### Testy
- Testy w QEMU bez stałych pauz: czekają na odpowiedź monitora, potwierdzenie kliknięcia w logu ekranu
  i na stan ekranu na zrzucie (`docs/TESTY.md`) — wolna maszyna nie daje fałszywych błędów, a testy
  są szybsze. Zrzuty bez pośrednich plików i powłoki (`k1qemu.py`); `zamknij_qemu` w `wspolne.sh`.
- Testy na hoście: bajty bloku układu K1-EFI zgodne z opisem formatu, wartości GPT i teksty baterii
  wpisane wprost zamiast liczone tymi samymi wzorami co kod.

## [0.7.2] — 2026-11-09

Poprawki zgłoszone przez właściciela i część porządków przed M7 (`docs/PRZED-M7.md`).

### Naprawione
- **Zainstalowany K1 nie „udaje” pendrive'a.** Instalator uruchomiony z dysku pisał „K1 działa teraz z
  obrazu .iso… uruchom komputer z pendrive'a”. Teraz mówi „Kons K1 jest zainstalowany” i jak
  zaktualizować system (pendrive z nowszą wersją → „Zaktualizuj system”).
- **„Wyloguj” tylko w zainstalowanym K1.** Uruchomiony z pendrive'a albo z .iso K1 nie pokazuje
  „Wyloguj” w menu Start (nadzorca też odrzuci taką prośbę). Zainstalowany = dane (K1-DANE) na dysku
  komputera.

### Zmienione
- Ustawienia i Zasoby opisują stan baterii tymi samymi słowami („Na baterii”, „Ładowanie”…).
- Kod: jedna funkcja do liczb szesnastkowych, jedna do cofania po znakach UTF-8 (pola tekstowe,
  Notatki), jedno źródło tekstów baterii. Pulpit dostaje od nadzorcy nowy komunikat `SESJA`.

### Testy
- `test-m4`, `test-m5f`: „Wyloguj” jest przy dysku, nie ma przy ISO i pendrivie; Instalator na
  zainstalowanym K1 nie prosi o pendrive. Testy na hoście: brak `grub-editenv`/`sfdisk` to błąd;
  zmiana `k1_test_host.h` przebudowuje testy; `test_efi` potwierdza wersję miejsca A przy
  nieczytelnym grubenv; nowe sprawdzenia `SESJA`, `k1_hex_bajty`, `k1_utf8_poczatek`, Backspace.

## [0.7.1] — 2026-11-08

Porządki w kodzie przed etapem M7 (ADR 0023) i nowa aplikacja.

### Dodane
- **Kalkulator** — działania, procenty, zmiana znaku, pamięć (MC, MR, M+, M−); liczby po polsku
  (przecinek, odstępy co trzy cyfry); obsługa myszą i klawiaturą (też numeryczną).

### Zmienione
- Kod uporządkowany: lista aplikacji i usług jest w jednym miejscu (nowa aplikacja = jeden wiersz +
  katalog); odczyt partycji systemu (wersje A/B) — jeden wspólny dla nadzorcy i Instalatora; wspólne
  kolory, ikona baterii, formatowanie liczb i czasu, przyciski i pola tekstowe; skrypty budowania bez
  powtórzeń. Działanie systemu bez zmian.
- Kolor akcentu na znaku K1 i strzałce myszy wyrównany do palety (#D97757 — był minimalnie inny).

### Naprawione
- Aplikacje odświeżały zegar dwa razy na obieg pętli; Zasoby nie mieściły listy po dodaniu aplikacji.

### Usunięte
- Stare szkice ekranu hasła i nieużywany kod.

### Testy
- Testy w QEMU na jednej wspólnej bibliotece (mniej przypadkowych błędów); testy na hoście z jednym
  wspólnym mechanizmem; nowe: `test_kalkulator`, `test_efi`, scenariusz Kalkulatora w `test-m4`.

## [0.7.0] — 2026-11-08

Etap **M6 — bezpieczne aktualizacje** zakończony: kroki M6c (A/B) i M6d (Secure Boot), ADR 0022.

### Dodane
- **Dwa miejsca na system (A/B):** na dysku są teraz dwie kopie systemu. Aktualizacja zapisuje nową
  wersję **obok** działającej i nic w działającej nie zmienia. Po ponownym uruchomieniu komputer
  próbuje nowej wersji **raz**: gdy wystartuje, K1 sam ją zatwierdza; gdy nie (uszkodzona, przerwany
  zapis, zawieszenie) — przy następnym starcie wraca poprzednia i Instalator mówi „Ostatnia
  aktualizacja nie wystartowała”. Lista dysków w Instalatorze pokazuje też wersję zapasową.
- **Secure Boot z kluczem K1:** cały system to teraz **jeden podpisany plik** (`k1.efi`). Z włączonym
  Secure Boot laptop uruchomi tylko K1 podpisany Twoim kluczem — podmieniony albo uszkodzony system się
  nie uruchomi, a na ekranie pojawi się wyjaśnienie, co zrobić.
- **Instalator → „Secure Boot…”:** stan (włączony/wyłączony, klucz wpisany albo nie) i przycisk
  „Przygotuj klucz K1”. Potem restart, niebieski ekran MokManagera (w ciągu 10 s dowolny klawisz) →
  Enroll MOK → Continue → Yes → hasło **kons** → Reboot, a na koniec w BIOS-ie (F1) Secure Boot → Enabled.
  Działa tylko w wersji podpisanej Twoim kluczem (README → „Klucz podpisu”): ten sam sekret podpisuje
  teraz i system, i Secure Boot — nie trzeba niczego więcej.

### Zmienione
- Obraz na pendrive ma 345 MB (było 113) — mieści dwa miejsca na system; plik do pobrania (`.img.xz`)
  dalej ok. 8 MB. Nagrywasz go jak dotąd (Rufus/balenaEtcher).
- **Aktualizacja do 0.7.0 z zainstalowanego 0.6.x:** jak zawsze — start z dysku, pendrive 0.7.0
  podłączony, Instalator → Zaktualizuj. Ten jeden raz zapisuje się cała partycja systemu (stary
  Instalator nie zna A/B); od 0.7.0 aktualizacje są już A/B. Dane zostają.
- Obraz ISO (do maszyn wirtualnych) też startuje z `k1.efi`.

### Bezpieczeństwo
- Podpisane jądro ma wbudowaną linię poleceń i nie przyjmuje initramfs od programu ładującego
  (sprawdzone: GRUB z podsuniętym obcym initramfs — K1 go zignorował).
- Wersja deweloperska (bez Twojego sekretu) jest podpisana publicznym kluczem testowym — K1 nigdy nie
  proponuje jego wpisania (każdy mógłby nim podpisać fałszywy system).

### Testy
- QEMU `test-m6` (nowy): Secure Boot z kluczem K1 → pulpit; bez klucza i ze zmienionym jednym bajtem
  `k1.efi` → odmowa z wyjaśnieniem; z kluczem wydania cała Twoja droga: „Przygotuj klucz K1” → MokManager
  (Enroll MOK, hasło) → Secure Boot włączony → K1 startuje.
- QEMU `test-m5f`: aktualizacja A/B do wolnego miejsca, potwierdzenie po starcie, wycofanie z
  uszkodzonego nowego miejsca, blokada starszej wersji.
- Host: układ partycji i grubenv (te same bajty co `grub-editenv`), SHA-256 (wektory FIPS), manifest
  formatu 2, nowe komunikaty, fuzzing.

## [0.6.3] — 2026-11-07

Etap **M6 — bezpieczne aktualizacje**, krok M6b (ADR 0021), oraz Twoje uwagi do 0.6.2.

### Dodane
- **System sprawdza sam siebie przy każdym starcie (dm-verity):** przed uruchomieniem pulpitu
  odczytuje cały obraz systemu i porównuje go z odciskiem zapisanym przy budowie. Zmieniony choćby
  jeden bajt (ktoś podmienił pliki na dysku albo nośnik się psuje) — system się nie uruchomi i powie
  dlaczego, zamiast wykonać obcy kod. Sprawdzanie trwa ułamek sekundy (obraz ma ok. 1 MB; w pierwszej
  wersji tej notatki pomyłkowo „30 MB”).
- **Wyloguj** w menu Start (obok „Uruchom ponownie” i „Wyłącz”): zamyka aplikacje i magazyn danych;
  z hasłem wraca ekran hasła (dane znów zamknięte), bez hasła pulpit uruchamia się od nowa.
- **Monitor zasobów → Bateria:** procent, aktualny pobór mocy (np. „pobór 9,4 W”) i szacowany czas:
  „zostało ok. 3 h 20 min” na baterii albo „do pełna ok. 1 h 10 min” przy ładowaniu. Pobór jest
  uśredniany z kilkunastu sekund (bez skakania przy każdym kliknięciu); przy otwartym Monitorze
  odczyt co 5 s.

### Naprawione
- **„System myśli, że jest na pendrivie”:** Notatki, Ustawienia → Informacje i Ustawienia → Hasło
  mówią teraz, gdzie naprawdę leżą dane — „na dysku komputera” albo „na pendrivie K1” (nadzorca wie,
  czy partycja K1-DANE jest na USB). Napis bez magazynu danych wymienia obie możliwości (instalacja
  na dysku albo pendrive z pliku `-usb.img.xz`).

### Testy
- QEMU: obraz bez odcisku, ze złym odciskiem i z jednym zmienionym bajtem → system się zatrzymuje
  z komunikatem; poprawny obraz → „obraz rdzenia sprawdzony (dm-verity…)”. Start z hasłem → Wyloguj →
  ekran hasła → hasło → pulpit. Zasoby: wiersz baterii (w QEMU: „brak baterii”).
- Host: pobór mocy i energia z różnych sterowników (moc wprost, prąd × napięcie, ładunek × napięcie),
  szacunek czasu, uśrednianie, bzdury z firmware'u; nowe komunikaty (`ZASOBY_BATERIA`, `WYLACZ` a=2,
  „dane na USB”).

## [0.6.2] — 2026-11-07

Etap **M6 — bezpieczne aktualizacje**, krok M6a (ADR 0021).

### Dodane
- **Manifest systemu:** na pendrivie i na dysku przy systemie leży „metryczka” — wersja, odcisk
  (skrót) całego systemu i podpis klucza K1.
- **Instalator sprawdza, co instaluje:** zanim cokolwiek zapisze, liczy odcisk systemu z pendrive'a;
  zmieniony albo uszkodzony system odrzuca. Gdy K1 jest zbudowany z kluczem — wymaga też ważnego
  **podpisu** (podrobiony system nie przejdzie).
- **Bez powrotu do starszej wersji:** aktualizacja ze starszego pendrive'a jest zablokowana (starsze
  wersje mogą mieć poprawione już dziury). Ta sama wersja — wolno (naprawa).
- Okno Instalatora pokazuje wersje: „Na dysku: 0.6.1 → z pendrive'a: 0.6.2 (podpisana)”.
- Wydanie na GitHubie mówi, czy jest podpisane. **Do podpisów potrzebny jest jednorazowo Twój klucz**
  (README → „Klucz podpisu”) — do tego czasu wydania są „deweloperskie”.

### Zmienione
- System plików systemu na pendrivie ma 47 MB (ostatni MB partycji to manifest).
- Najbezpieczniej aktualizować **z zainstalowanego K1**: start z dysku, pendrive z nową wersją
  podłączony, Instalator → Zaktualizuj (wtedy sprawdza Twój system, nie kod z pendrive'a).

### Naprawione
- Wydanie 0.6.1 się nie zbudowało: w CI test uznał za brakujący napis „K1: wyłączanie zasilania”, bo
  komunikat jądra wpadł w jego środek na wspólnej konsoli (system działał poprawnie, 126/126 testów
  w systemie). Sprawdzanie logów pomija teraz takie wtrącenia.

### Testy
- Host: manifest (podpis tylko z właściwym kluczem i niezmienionymi bajtami, obcy klucz, złe dane),
  fuzzing. QEMU: pendrive ze zmienionym jednym bajtem systemu → odmowa, dysk nietknięty; dysk z
  nowszą wersją → aktualizacja zablokowana.

## [0.6.1] — 2026-11-07

### Naprawione
- **Instalator: „Nie znaleziono systemu do skopiowania”** (Twój test 0.5.9): gdy K1 działa z pliku
  `.iso` nagranego na pendrive, nie ma skąd skopiować systemu — a Instalator mimo to pokazywał przycisk
  „Zainstaluj”. Teraz zamiast przycisku mówi wprost, co zrobić: pobrać `k1-…-usb.img.xz`, nagrać go
  na pendrive i uruchomić komputer z niego. Osobny komunikat, gdy podłączone są dwa pendrive'y K1.

### Testy
- QEMU: start z obrazu `.iso` z dyskiem SATA → Instalator bez przycisku, nic się nie zaczyna.

## [0.6.0] — 2026-11-07

Etap **„Fundament”** (ADR 0019) — zamyka M5. Mniej widać na ekranie, więcej pod spodem.

### Dodane
- **Historia baterii z poprzednich uruchomień** (Twoja prośba z 0.5.5): system zapisuje przebieg
  naładowania w zaszyfrowanym magazynie (od razu po starcie, potem co 5 minut i przy wyłączaniu).
  Ustawienia → Bateria pokazują pod wykresem ostatnie wcześniejsze uruchomienie: kiedy, jak długo,
  od ilu do ilu procent, z małym wykresem. Trzymamy ostatni tydzień (do 16 uruchomień).
- **System reaguje na urządzenia od razu:** podłączenie myszy, klawiatury, pendrive'a z danymi czy
  zasilacza jądro zgłasza nadzorcy natychmiast (zdarzenia „uevent”), zamiast sprawdzania co 2 sekundy.

### Zmienione
- **Nadzorca podzielony na moduły** (najważniejszy program w systemie): zamiast jednego pliku z ponad
  1500 wierszami — 8 części o jasnych zadaniach (usługi, klatki, urządzenia, skarbiec, instalator,
  bateria i zasoby, kanał testowy, pętla główna). Start systemu to teraz **jawne etapy** (dane →
  stan magazynu → hasło → pulpit), każdy zapisywany w logu.
- Jedno miejsce sprawdzania wiadomości między programami (nadawca, poprawność, deskryptory) zamiast
  kilkunastu kopii tego samego kodu.
- Testy: wspólna biblioteka do testów w emulatorze; z KVM (w CI) testy pulpitu, sprzętu i instalatora
  idą równolegle.

### Testy
- Na hoście: format historii baterii (zapis, odczyt, limity, uszkodzone dane), parser zdarzeń jądra,
  wspólny odbiór wiadomości (prawdziwe gniazda); fuzzing obu nowych parserów.
- QEMU: dwa starty z pendrive'a z baterią — pierwszy zapisuje historię, drugi ją odczytuje.

## [0.5.9] — 2026-11-07

Etap **M5f — instalator** (ADR 0018) i szybsze przesuwanie okien.

### Dodane
- **Instalator** (Twój pomysł): menu Start → Instalator → wybierasz dysk → wpisujesz **USUŃ** → K1
  instaluje się na dysku komputera: system + zaszyfrowane miejsce na dane **na całym dysku** (zamiast
  64 MB na pendrivie). Potem startujesz bez pendrive'a — szybciej, bo dysk SATA jest dużo szybszy od USB.
- **Aktualizacja bez utraty danych:** uruchamiasz nowszą wersję z pendrive'a → Instalator →
  *Zaktualizuj system*. Wymieniany jest tylko system; Twoje dane (ustawienia, notatki, hasło) zostają.
- Bezpieczeństwo instalatora: okno nie ma dostępu do dysków; zapisuje osobny, malutki program w
  ciasnej klatce, który dostaje od nadzorcy tylko dwa urządzenia. Zapis kończy się sprawdzeniem
  (odczyt i porównanie), a tablica partycji jest zapisywana na samym końcu — przerwana instalacja nie
  zostawia pół-działającego systemu. Aktualizacja fizycznie nie dostaje partycji z Twoimi danymi.
- Z podłączonym pendrive'em K1 zainstalowany system i tak używa swoich danych z dysku.

### Zmienione
- **Przesuwanie okien ~10× mniej obciąża procesor** (Twoja uwaga): w X230 (grafika z firmware) ekran
  składał klatkę przy każdym ruchu myszy, bez limitu, i liczył cień okna wolną funkcją dla każdego
  piksela. Teraz: najwyżej 60 klatek na sekundę, cień z gotowej tabeli, stare i nowe miejsce okna
  składane raz. W emulatorze: ~19 ms zamiast ~190 ms na klatkę; przeciąganie jest płynne.
- Instalator ma ikonę dysku w menu Start (na dolnym pasku zostają cztery aplikacje).

### Testy
- QEMU: pendrive + pusty dysk → instalacja → start z samego dysku → nowy magazyn → aktualizacja z
  pendrive'a → dane przetrwały; bez wpisania „USUŃ” nic się nie dzieje; tablica partycji sprawdzona
  przez `sfdisk --verify`. Na hoście: tablica GPT dla dysków 4 GB … 3 TB i sektorów 512/4096 B,
  lista dysków na sztucznym sysfs, nowe komunikaty. Próby klatek nowych programów.

## [0.5.8] — 2026-11-07

Poprawki po Twoim teście 0.5.7 na X230.

### Dodane
- **Logo przy starcie** (Twoja prośba): od ekranu ThinkPada do pulpitu widać tylko znak K1 i napis
  „Kons K1” na czarnym tle — **bez żadnego tekstu** (wyciszone komunikaty jądra „EFI stub…”).
- **Wiele notatek:** z lewej lista notatek (tytuł = pierwsza linia, pod nim druga), przycisk
  **„Nowa notatka”**, kliknięcie otwiera notatkę, **„Usuń notatkę”** z potwierdzeniem. Ostatnio
  zmieniona jest na górze; pusta notatka znika sama po przejściu do innej. Skróty: Ctrl+N (nowa),
  Ctrl+↑/↓ (poprzednia / następna). Do 32 notatek, każda zaszyfrowana osobno. Twoja dotychczasowa
  notatka zostaje jako pierwsza.
- **Ustawienia → Informacje:** gdzie są Twoje dane — „zaszyfrowane na nośniku K1” albo „tylko
  w pamięci”.

### Naprawione
- **„Wymaga nośnika K1” — nie dało się zapisać ani ustawić hasła:** system szuka partycji na dane
  także **po starcie** (przez 2 minuty). Pendrive, który pojawi się później (wolny albo — jak w X230 —
  przełączony przez kontroler USB 3 w trakcie startu), jest teraz używany: usługa danych i pulpit
  uruchamiają się ponownie, już z zapisem.
- **Długi start z pendrive'a bez partycji na dane** (np. gdy na pendrive zapisano plik `.iso`):
  system czekał do 15 s na partycję, której nie było. Teraz czeka tylko, aż pendrive się zgłosi.
- W Ustawieniach → Hasło jaśniejszy opis, gdy brak miejsca na dane: hasło i zapis działają z pendrive'a
  z plikiem `k1-…-usb.img.xz` (nie z `.iso`).

### Testy
- QEMU: pendrive z pustą partycją K1-DANE podłączony po starcie → nowy zaszyfrowany magazyn i pulpit
  od nowa; dwie notatki → wyłączenie → obie wracają (ostatnio zmieniona na górze) → otwarcie drugiej
  → usunięcie z potwierdzeniem; logo widoczne od GRUB-a do pulpitu (sprawdzone na zrzutach).

## [0.5.7] — 2026-11-07

Etap **M5e — hasło i ekran logowania** (ADR 0017).

### Dodane
- **Ustawienia → Hasło:** włączenie hasła (nowe + powtórz), zmiana (obecne + nowe) i wyłączenie
  (obecne). Hasło szyfruje klucz do Twoich danych; zapomniane hasło = nowe konto.
- **Ekran hasła przy włączaniu** (Twój wybór — szkic B „Zegar”): duży zegar i data na tapecie
  „Zmierzch”, przezroczyste pole hasła na dole, bateria i wyłączanie w rogu. Po poprawnym haśle
  pulpit startuje z Twoimi ustawieniami.
- Ochrona przed zgadywaniem: od trzeciej złej próby coraz dłuższa przerwa (5 s, 10 s … do 5 min) —
  pilnuje tego usługa danych, nie ekran hasła.
- Hasło nigdy nie trafia do logu.

### Testy
- QEMU: ustawienie hasła w Ustawieniach → ponowne uruchomienie → ekran hasła zamiast pulpitu → złe
  hasło odrzucone → dobre hasło → pulpit z zapisaną tapetą i motywem; zrzuty ekranu.
- Próba klatki nowego programu „logowanie”; nowe komunikaty — testy na hoście.

## [0.5.6] — 2026-11-07

Etap **M5c + M5d — zaszyfrowany zapis danych** (ADR 0015, ADR 0016).

### Dodane
- **Dane przetrwają wyłączenie:** tapeta, motyw, prędkość myszy, sekundy na zegarze, rozdzielczość
  i notatka wracają po ponownym uruchomieniu. Zapisuje je nowa usługa **skarbiec** — jedyna, która
  ma dostęp do partycji z danymi „K1-DANE”; wszystko jest **zaszyfrowane** (XChaCha20-Poly1305,
  biblioteka Monocypher), a zmieniony bajt na dysku jest wykrywany, nie przyjmowany po cichu.
- **Obraz na pendrive** `k1-<wersja>-usb.img.xz` (UEFI): system + pusta partycja na dane; przy
  pierwszym starcie K1 zakłada na niej zaszyfrowany magazyn. Dysk laptopa pozostaje nietknięty.
- Bez partycji danych (ISO, maszyna wirtualna bez drugiego dysku) wszystko działa jak wcześniej —
  dane tylko do wyłączenia.
- **Alarm Windows Defendera** („Trojan:Win32/Sabsik!ml” na ISO 0.5.5 — zgadywanie modelu, nie znany
  wirus): ISO i obraz USB startują teraz przez **podpisane** programy z Ubuntu (shim z podpisem
  Microsoftu, GRUB z podpisem Canonical) zamiast naszego, samodzielnie zbudowanego GRUB-a.
- ISO i obraz USB są **w pełni powtarzalne** — z tego samego kodu powstaje plik identyczny bit w bit
  (można sprawdzić, że wydanie pochodzi z repozytorium). ISO jest mniejsze (14 MB).

### Zmienione
- Notatki nie mają już dostępu do żadnych plików — notatka jest obiektem w skarbcu (ciaśniejsza klatka).
- K1 formatuje tylko pustą partycję „K1-DANE”; partycji z innymi danymi nie nadpisuje (test).
- Testy: na hoście format magazynu (62 przypadki: przerwany zapis, zmieniony bajt, hasło, brak
  miejsca…) i fuzzing; w QEMU wyłączenie i ponowny start z tym samym dyskiem, start z obrazu USB
  przez UEFI (dwa razy), próba klatki skarbca.

## [0.5.5] — 2026-11-07

### Dodane
- **Ustawienia → Bateria** (Twój pomysł): procent i stan baterii oraz minimalistyczny wykres
  naładowania od uruchomienia systemu (linia w kolorze akcentu, pod wykresem pasek — kiedy był
  podłączony zasilacz), godzina startu i najniższy poziom. Pomiar co pół minuty; po wielu godzinach
  wykres sam „zagęszcza” starsze pomiary, więc zawsze pokazuje cały czas od uruchomienia.
  Historia z poprzednich uruchomień — razem z trwałym zapisem danych (M5).
- Ikona baterii (Ustawienia).

## [0.5.4] — 2026-11-07

### Naprawione
- **Zacinanie się całego pulpitu na prawdziwym laptopie** (ThinkPad X230, Twój pierwszy test):
  kursor, zmiana tapety i cały system działały skokowo. Dwie przyczyny: (1) w jądrze brakowało obsługi
  typów pamięci procesora (MTRR/PAT), więc każdy piksel szedł do karty bez buforowania; (2) przy
  obrazie z firmware ekran przy każdej klatce przepisywał cały ekran (ok. 4 MB). Teraz przy obrazie
  z firmware jest jeden bufor, a do karty trafiają tylko zmienione miejsca (np. sam kursor).
  W QEMU tego nie było widać — emulator nie odwzorowuje typów pamięci.

## [0.5.3] — 2026-11-07

### Dodane
- **Wskaźnik baterii** na górnym pasku (Twój pomysł): na laptopie przed ikoną dźwięku procent
  naładowania i ikona baterii, błyskawica przy ładowaniu, przy niskim poziomie (≤ 15 %) wypełnienie
  w kolorze akcentu. Na komputerze bez baterii (i w maszynie wirtualnej) nic się nie pokazuje.
  Stan sprawdzany co 15 s (ADR 0014).
- Jądro: sterownik Embedded Controller (przez niego laptopy, m.in. ThinkPad X230, podają stan
  baterii), baterii i zasilacza ACPI.
- Testy: sztuczna bateria laptopa w QEMU (tablica ACPI) — test w systemie, log powłoki i zrzut
  ekranu z ikoną; test na hoście odczytu stanu baterii (także z błędnymi danymi).

## [0.5.2] — 2026-11-07

Etap **M5a — dyski i USB** (pierwszy krok M5; ADR 0013). Pierwszy laptop: ThinkPad X230.

### Dodane
- Jądro rozpoznaje **dyski SATA** (kontroler AHCI — m.in. ThinkPad X230, VirtualBox) i **NVMe**,
  tablice partycji **GPT** (partycja danych K1 będzie szukana po nazwie „K1-DANE”).
- **USB**: kontrolery USB 3 (xHCI), USB 2 (EHCI) i USB 1.1 (OHCI); **klawiatury i myszy USB**
  działają w pulpicie, **pamięci USB** (pendrive'y) są widoczne. Tylko standardowe urządzenia —
  bez sterowników producentów i bez surowego dostępu do urządzeń.
- README: jak spróbować startu z pendrive'a na ThinkPadzie X230.

### Zmienione
- Odłączona mysz lub klawiatura USB jest „zapominana”, więc ponowne podłączenie (także pod tym samym
  numerem urządzenia) działa.
- Stare tablice partycji MBR nie są czytane przez jądro (mniej kodu wystawionego na obce nośniki).
- Testy: test w systemie z dyskami SATA/NVMe, pendrive'em i USB na trzech kontrolerach (dostęp do
  dysków tylko dla roota, nikt ich nie otwiera); nowy `scripts/test-m5.sh` — maszyna bez PS/2,
  tylko klawiatura i mysz USB, podłączanie i odłączanie myszy w trakcie pracy.

## [0.5.1] — 2026-11-07

### Naprawione
- Mysz mogła nie działać po bardzo szybkim starcie (np. pod KVM): nadzorca szukał urządzeń wejścia,
  zanim jądro zgłosiło mysz, i przestawał szukać, gdy znalazł klawiaturę. Teraz co 2 s sprawdza,
  czy doszło nowe urządzenie, i uruchamia usługę wejścia ponownie z pełnym zestawem. To też pierwszy
  krok do podłączania urządzeń w trakcie pracy (M5).

### Zmienione
- CI: logi testów zawsze dostępne, niezależnie od limitu miejsca na artefakty — w konsoli zadania,
  w podsumowaniu (lista OK/BŁĄD) i w wydaniu (`k1-<wersja>-logi.tar.gz`); artefakty 7 dni, obraz jako
  artefakt tylko z PR-ów.
- Testy pulpitu i wejścia czekają, aż mysz zostanie otwarta; test pulpitu sprawdza położenie kursora
  z logu ekranu po każdym ruchu; sprawdzanie wykresu Zasobów liczy każdą kolumnę.
- Dokumentacja: wstępne rozpoznanie etapu M5 (`docs/M5-ROZPOZNANIE.txt`).

## [0.5.0] — 2026-11-06

Etap **M4.1 — porządki i audyt** (ADR 0011, ADR 0012).

### Dodane
- Aplikacja **Zasoby**: procesor i pamięć całego systemu (liczby i wykresy z ostatniej minuty) oraz
  zużycie każdej usługi i aplikacji. Dane podaje nadzorca (statystyki cgroup) przez kanał, do którego
  aplikacja nie może pisać.
- **Ustawienia → Ekran**: wybór rozdzielczości z listy, którą podaje karta graficzna; zmiana od razu,
  pulpit układa się od nowa.
- Sterownik karty graficznej **VMware SVGA** (VirtualBox „VMSVGA”, VMware) — w maszynach wirtualnych
  więcej rozdzielczości niż 1024×768 z firmware.
- Ikony: Zasoby (puls), Ekran (monitor).

### Naprawione
- Pod dolnym paskiem i pod menu dźwięku „wyjeżdżała” jaśniejsza podstawa: cień panelu miał w środku
  dziurę przesuniętą 4 px w dół. Cień jest teraz liczony tylko poza panelem.
- Bezpieczeństwo (przegląd M4.1): nowe okno innej aplikacji nie przejmuje już klawiatury (ochrona
  przed „kradzieżą fokusu”); limit pamięci okien jednego klienta; ekran przycina obszar odświeżania
  do własnego okna klienta; log usług oczyszczany także ze znaków sterujących C1 i błędnego UTF-8;
  klatka zamyka wszystkie zbędne deskryptory (`close_range`); katalog notatek bez podążania za
  dowiązaniami.

### Zmienione
- Kod: kompozytor podzielony na `main.c`, `scena.c`, `okna.c`, `wskaznik.c`, `drm.c`; program
  testowy na pliki tematyczne (`testy/system/`).
- Testy: zmiana rozdzielczości i Zasoby w `test-m4.sh`; w systemie — ochrona fokusu, limit pamięci
  okien, klatka i próba aplikacji Zasoby; na hoście — `test_log`, nowe komunikaty w `test_komunikat`.


## [0.4.2] — 2026-11-06

### Zmienione
- Nowy znak K1 **„Warstwy”** (wybór właściciela z czterech kierunków): trzy arkusze papieru
  w odcieniach terakoty, jak na tapecie — w przycisku Start i w menu. Tarcza z blankami usunięta.
- README: w VirtualBoxie mysz i klawiatura działają od razu (test właściciela, VirtualBox z EFI);
  obejście dla niskiej rozdzielczości w trybie EFI.

## [0.4.1] — 2026-11-06

### Naprawione
- CI: instalacja narzędzi zawieszała się na serwerze GitHuba (ustawienie „bez pytań” dla apt
  ginęło przy `sudo`); teraz jest przekazywane wprost, instalacja pokazuje postęp w logu i ma limit
  10 minut. Bez zmian w samym systemie — obraz ISO jak w 0.4.0.

## [0.4.0] — 2026-11-06

Etap **M4 — GUI**: pulpit, okna i pierwsze aplikacje; pierwszy obraz ISO do maszyny wirtualnej.

### Dodane
- Pulpit (ustalenia właściciela, `docs/WYGLAD.md`): górny pasek — nazwa aktywnej aplikacji, dźwięk,
  data i godzina (czas polski, letni/zimowy sam); dolny pasek wyśrodkowany jak w Windows 11 — Start
  i aplikacje ze wskaźnikami; menu Start z „Uruchom ponownie” i „Wyłącz”; klawisz Super.
- Okna: zaokrąglone, z cieniem, przeciąganie za pasek tytułu, minimalizacja (też kliknięciem na
  pasku), zamykanie, fokus po kliknięciu.
- Aplikacje: **Zegar** (tarcza analogowa, data po polsku), **Notatki** (polskie znaki przez prawy Alt,
  zapis automatyczny), **Ustawienia** (motyw jasny/ciemny, tapeta, prędkość myszy, sekundy na
  zegarze, informacje; hasło — opis na M5).
- Nowe tapety na wzór zdjęcia właściciela (warstwy wygiętego papieru), 4 warianty: Glina (domyślna,
  kolory systemu), Zmierzch, Noc, Świt; nowy, płaski znak K1 (tarcza z blankami).
- Czcionki IBM Plex Sans i Serif (bitmapy liczone przy budowie, ADR 0009).
- **Obraz ISO** (`k1-<wersja>.iso`, ~16 MB) do VirtualBoxa/VMware/QEMU — start przez UEFI albo BIOS,
  grafika z firmware (simpledrm); CI publikuje go jako wydanie (Releases).
- Bezpieczeństwo (ADR 0008): ekran jako kompozytor sprawdza każdy komunikat okien i nadawcę, przyjmuje
  tylko zapieczętowaną pamięć współdzieloną, egzekwuje role (pulpit, Ustawienia); pulpit i każda
  aplikacja we własnej klatce (Notatki widzą tylko swój katalog, pulpit nie widzi treści okien).
- Testy: `scripts/test-m4.sh` (klikanie, pisanie po polsku, przeciąganie, motyw, tapeta, wyłączenie,
  start z ISO przez UEFI i BIOS — zrzuty sprawdzane automatycznie); w systemie: klatki i próby dla
  pulpitu i aplikacji, złośliwe okna, restart pulpitu (88 sprawdzeń); testy jednostkowe przybornika.

### Zmienione
- Jądro: simpledrm (grafika z firmware), loop (rdzeń wbudowany w ISO), zegar sprzętowy CMOS.
- `ekran` przepisany na kompozytor okien (`src/ekran/main.c` + `drm.c`); tapeta bez dużego logo.
- `/dane_uzytkownika` z prawami `0711` (aplikacja wchodzi tylko do swojego podkatalogu).
- Rozpoczęcie: `scripts/przygotuj-srodowisko.sh` instaluje też czcionki, FreeType i GRUB.

## [0.3.0] — 2026-11-06

Etap **M3 — Trusted Core**: każda usługa systemu działa w osobnej „klatce” i wraca po awarii.

### Dodane
- Nadzorca w pełnej wersji (ADR 0007): każda usługa ma własnego użytkownika, zero uprawnień
  administratora, blokadę podnoszenia uprawnień, białą listę wywołań jądra (seccomp), dostęp do plików
  tylko do własnego programu (Landlock) i limity pamięci oraz procesów (cgroups v2).
- Urządzenia (karta graficzna, klawiatura, mysz) otwiera nadzorca i przekazuje usługom — usługi nie
  widzą żadnych plików ani urządzeń poza przekazanymi.
- Kanały między usługami przez gniazda UNIX z kontrolą nadawcy: ekran odrzuca komunikaty od kogoś
  innego niż przypisana usługa. Nowy wspólny protokół komunikatów (32 bajty, testy + fuzzing).
- Logi usług przechodzą przez nadzorcę: bez znaków sterujących, z limitem, usługa nie podszyje się
  pod inną; usługi nie mają już dostępu do konsoli.
- Restart usług po awarii (ekran wraca razem z klientami), usługa padająca w kółko zostaje wyłączona.
- Testy: sprawdzenie klatek z zewnątrz, „próby” w klatkach (22 zabronione działania), obcy nadawca,
  restart ekranu, limit restartów; testy jednostkowe protokołu i kanałów, fuzzing protokołu.
- `docs/DO_ZROBIENIA.txt` — lista tego, co zostało do zrobienia, i decyzji potrzebnych od właściciela.
- CI: workflow „Sprzątanie artefaktów” (usuwa stare obrazy Kons-OS, które zajmowały miejsce).

### Zmienione
- Jądro: gniazda UNIX (`CONFIG_UNIX`) — tylko do kanałów między usługami.
- Skill `kons-dev` opisuje architekturę K1, niezmienniki bezpieczeństwa i przepisy na typowe zmiany.
- Decyzja: na razie bez `fortify-headers` (ADR 0007).

## [0.2.0] — 2026-11-06

Etap **M2 — Ekran**: K1 pokazuje obraz i reaguje na mysz i klawiaturę.

### Dodane
- Nowa tapeta i logo K1 (ustalenia właściciela, `docs/WYGLAD.md`): ciemne, ciepłe tło z łukiem
  „horyzontu”, logo — tarcza z blankami twierdzy (bezpieczeństwo, niezależność) z monogramem K1.
  Styl nawiązuje do Claude Code (terakota, krem, ciemny brąz).
- `ekran`: jedyny proces z dostępem do karty graficznej (DRM/KMS bez libdrm), własny renderer
  programowy z wygładzaniem krawędzi, podwójne buforowanie, kursor myszy.
- `wejscie`: klawiatura i mysz (PS/2, evdev); kody klawiszy nigdy nie trafiają do logu.
- Protokół zdarzeń wejście → ekran (16 bajtów, wersja, typ, długość, sprawdzanie każdego pola).
- `nadzorca` w wersji minimalnej: uruchamia `wejscie` i `ekran` i łączy je kanałem (pełny — M3).
- Testy: `scripts/test-m2.sh` (mysz i klawiatura przez monitor QEMU, zrzuty ekranu sprawdzane
  automatycznie), `scripts/testy.sh` (wszystko), nowe testy negatywne: przejęcie ekranu przez inny
  proces, złośliwe komunikaty do ekranu, awaria procesu ekran; testy jednostkowe i fuzzing protokołu.
- Jądro: DRM + sterownik `bochs` (QEMU), evdev, i8042, klawiatura AT, mysz PS/2 — bez fbdev i fbcon.

### Zmienione
- CI: tag wersji zakładany zaraz po udanych testach; brak miejsca na artefakty GitHuba nie blokuje już wersji.
- Program testowy w systemie: `/rdzen/system/test-systemu` (uruchamia go nadzorca).

## [0.1.0] — 2026-11-06

Pierwsza wersja Kons K1 Sovereign: etapy **M0** (porządki) i **M1** (start systemu).
Na razie bez ekranu — system startuje w maszynie wirtualnej i zgłasza się na porcie szeregowym.

### Dodane
- Jądro Linux 6.18.60 (LTS) z własną, minimalną konfiguracją: bez modułów, bez swapu i hibernacji,
  bez terminali, lockdown + Landlock + Yama, wzmocnienia według KSPP, sprawdzanie sumy i podpisu źródeł.
- `k1-initramfs`: znajduje obraz systemu, montuje go tylko do odczytu jako `/rdzen` i przekazuje
  sterowanie dalej; przy każdym błędzie czytelny komunikat i bezpieczne wyłączenie maszyny.
- `/rdzen/init` (PID 1): montowania pod `/rdzen/jadro`, 13 ustawień bezpieczeństwa jądra,
  sprzątanie osieroconych procesów, kontrolowane wyłączanie. W korzeniu systemu są tylko
  `/rdzen`, `/aplikacje`, `/dane_uzytkownika`.
- Testy: 33 sprawdzenia wewnątrz systemu (w tym negatywne: nie da się uruchomić programu z danych
  ani z pamięci, nie ma terminali, usunięte niebezpieczne wywołania jądra), 6 scenariuszy błędów
  startu, testy jednostkowe z sanitizerami i fuzzing parsera.
- Skrypty: budowa (powtarzalna), uruchamianie w QEMU z logiem, zrzut ekranu, testy.
- CI: GitHub Actions buduje i testuje `main`, publikuje obrazy i logi jako artefakt, zakłada tag `k1-v0.1.0`.
- Dokumentacja: specyfikacja, ADR 0001–0005, architektura, komponenty, testy, zależności, ryzyka.

### Usunięte
- Kons-OS na bazie Debiana (live-build, XFCE) — przeniesiony do gałęzi `archiwum/debian-0.1.2`.

---

# Archiwum: Kons-OS (Debian, live-build)

Historia poprzedniego systemu; kod w gałęzi `archiwum/debian-0.1.2`, tagi `v0.1.0`–`v0.1.2`.

## [Kons-OS 0.1.2] — 2026-11-02
- Naprawiona tapeta XFCE; firmware wybierane ręcznie (ISO ~1,3 GB); lokalne budowanie ISO.

## [Kons-OS 0.1.1] — 2026-11-02
- System odchudzony do pulpitu XFCE, ustawień i Firefoksa; szablony zgłoszeń; tagi wersji.

## [Kons-OS 0.1.0] — 2026-11-02
- Pierwsza wersja: Debian 13 live, XFCE, polski język, budowanie ISO w GitHub Actions.
