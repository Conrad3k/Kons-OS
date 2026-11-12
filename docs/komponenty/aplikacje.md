# Komponent: aplikacje natywne (`/rdzen/aplikacje/<program>`)

| | |
|---|---|
| **Status** | PRZETESTOWANE (M4, Zasoby i Ekran: M4.1) — `scripts/test-m4.sh`, testy w systemie (klatki, próby) |
| **Kod** | `src/aplikacje/*/main.c`, szkielet `src/przybornik/k1_aplikacja.c` |
| **Decyzje** | ADR 0008; do 0.12 aplikacje leżą w obrazie rdzenia (`/rdzen/aplikacje`), potem podpisane obrazy w `/aplikacje` |

## Cel
Każda aplikacja to jeden wiersz w `K1_APLIKACJE` (`src/wspolne/k1_uslugi.h`) i katalog
`src/aplikacje/<program>/`; każda ma tu swój rozdział (sprawdza `scripts/sprawdz-czytelnosc.py`).

### Zegar
Tarcza analogowa (sekundnik w kolorze akcentu), godzina, data po polsku, czas
letni/zimowy.

### Notatki
Od 0.5.8 wiele notatek: z lewej lista (tytuł = pierwsza niepusta linia, pod nim druga; ostatnio zmieniona na
górze, strzałki przewijania gdy się nie mieszczą), „Nowa notatka” (Ctrl+N), obok przycisk z folderem —
**„Otwórz…”** (Ctrl+O, 0.8.0): okno wyboru pliku K1, Ctrl+↑/↓ — poprzednia/następna, „Usuń notatkę” w stopce
z potwierdzeniem. Pisanie z polskimi znakami (AltGr), strzałki, Home/End, Backspace, Delete, Enter, kliknięcie
ustawia kursor, zawijanie wierszy, zapis automatyczny (0,8 s po zmianie; napis w stopce mówi, gdzie zapisano).

**Od 0.8.0 każda notatka to plik** (ADR 0030) w **Dokumenty › Notatki** (do 256 najnowszych notatek po 16 KB — przy
większej liczbie komunikat „Pokazano 256 najnowszych notatek”, starsze są w Plikach; 0.8.1): nazwa pliku
idzie za pierwszym wierszem („Lista: zakupy?” → „Lista zakupy”; bez znaków, których nazwa nie może mieć), dopóki
użytkownik nie zmieni jej w Plikach; ta sama nazwa w folderze — „ (2)”. Nowa notatka staje się plikiem przy
pierwszym zapisie; pusta znika po przejściu do innej (plik — do Kosza). „Usuń notatkę” przenosi plik do Kosza
(przywrócisz go w Plikach). Plik tekstowy otwarty w Notatkach z Plików, z pulpitu („Otwórz w…”) albo przez
„Otwórz…” dochodzi do listy; zamiast „Usuń notatkę” ma „Zamknij plik” (plik zostaje na swoim miejscu); większy
niż 16 KB — komunikat w stopce. Przy pierwszym uruchomieniu 0.8.0 stare notatki (obiekty 1–33 z 0.5.6–0.7.6)
stają się plikami; stary obiekt znika dopiero, gdy skarbiec potwierdzi plik (`stare.c`, tymczasowe — do 0.9.0).
**Notatki nie gubią tekstu** (0.8.1): notatka, której treści skarbiec nie oddał, jest tylko do odczytu („Nie udało
się wczytać tej notatki — spróbuję ponownie.”) — klawisz nie zapisze pustej notatki na plik; Notatki próbują znowu
przy następnej zmianie plików. Gdy plik notatki trafi do Kosza (w Plikach), a w notatce jest niezapisany tekst,
Notatki zapisują go jako nowy plik w Dokumenty › Notatki („Plik tej notatki trafił do Kosza — zapisano jej kopię.”);
notatka bez nowego tekstu po prostu znika z listy (jej plik jest w Koszu). Folder Notatek skarbiec pamięta po
numerze — zmiana jego nazwy, przeniesienie albo plik o nazwie „Notatki” nie tworzą „Notatki (2)”.
Moduły: `main.c` (okno), `spis.c` (lista, nazwy), `pliki.c` (skarbiec, nadzorca), `tekst.c`, `stare.c`.

### Wybór pliku
(0.8.0, ADR 0030, `src/aplikacje/wybor/`, UID 207) — **okno wyboru pliku K1**: gdy aplikacja prosi o plik
(`DOK_WYBIERZ`), skarbiec prosi nadzorcę o to okno i mówi mu, kto prosi i o jaki typ albo rodzinę typów
(`DOK_PROSBA`; rodzina od 0.8.2 — Podgląd prosi o „obrazy”, ADR 0037). Na górze
„Notatki prosi o plik (Dokument tekstowy). Dostanie tylko ten, który wybierzesz.”; z lewej stałe foldery, nad
listą „w górę” i ścieżka; lista: foldery, potem pliki (innego typu — wyszarzone); „Anuluj” i „Otwórz”
(też podwójne kliknięcie i Enter; Esc i × = „Anuluj”). Okno widzi tylko nazwy widocznych plików (bez treści,
bez Kosza); wybór idzie do skarbca (`DOK_WYBRANO`), który daje aplikacji dostęp do tego jednego pliku. Nie ma
go w menu Start ani na pulpicie (kolumna -1 w `K1_APLIKACJE`); otwarte ręcznie bez prośby — „Żadna aplikacja
nie prosi teraz o plik.” Moduły: `main.c` (okno), `dane.c` (skarbiec).

### Ustawienia
Wygląd (motyw jasny/ciemny, tapeta: 5 wariantów z miniaturami, po 4 w rzędzie; od 0.8.0 szósty kafelek „Zdjęcie”, gdy
ekran ma zdjęcie ustawione w Plikach — bez niego podpowiedź, jak je ustawić; samego zdjęcia Ustawienia nie widzą), Ekran
(rozdzielczość z listy od ekranu — do 18 trybów w trzech kolumnach, „zalecana” = preferowana przez monitor/kartę;
zmiana w locie, ADR 0012), Bateria (0.5.5: procent, stan, wykres naładowania od uruchomienia — linia
w kolorze akcentu z delikatnym wypełnieniem, pasek pod wykresem = zasilacz podłączony, godzina startu,
najniższy poziom; od 0.6.0 pod wykresem ostatnie wcześniejsze uruchomienie z małym wykresem — archiwum
od nadzorcy jako zapieczętowany memfd `BATERIA_ARCH`; bez baterii — napis), Hasło (0.5.7: włącz / zmień / wyłącz, pola z kropkami, Tab/Enter/Esc;
przez skarbiec — fd 5), Mysz (prędkość),
Data i czas (sekundy na pasku), Aplikacje (0.8.0, `aplikacje.c`: domyślna aplikacja dla typu pliku, który otwiera
więcej niż jedna — dziś „Dokument tekstowy”: Notatki albo Pliki (szczegóły), i od 0.8.2 „Obrazy (PNG, JPEG, GIF, BMP,
WebP)”: Podgląd albo Pliki — typy jednej rodziny w jednym wierszu, wybór ustawia je wszystkie; ustawienie ekranu
`K1_UST_OTWIERANIE` + typ, zapisane w skarbcu), Informacje (`informacje.c`: wersja, ekran, jądro, gdzie są dane; od
0.8.1 — `SKARB_MIEJSCE` od skarbca — „Miejsce na pliki: zajęte X z Y” z paskiem (Y od 0.8.2 to ok. 90 % K1-DANE —
format 2 skarbca, ADR 0036; w 0.8.1 połowa, bo druga była na kompaktowanie), „Pliki i foldery: n z 16 384”, bez
K1-DANE „w pamięci do wyłączenia: zostało X”; na pendrivie K1 z wolnym miejscem za K1-DANE przycisk **„Użyj całego
pendrive'a na dane”** — kliknięcie = zgoda, `SKARB_POWIEKSZ`, w trakcie „Powiększam… Nie wyjmuj pendrive'a.”, potem
„Gotowe — cały pendrive jest teraz na dane.”; ADR 0033, `komponenty/skarbiec.md`).

### Instalator
(0.5.9, ADR 0018, `src/aplikacje/instalacja/`, UID 204) — lista dysków od nadzorcy,
instalacja po wpisaniu „USUŃ”, aktualizacja (dane zostają), postęp, „Wyłącz komputer”. Bez dostępu do
dysków — szczegóły: `komponenty/instalator.md`.

### Kalkulator
(0.7.1, `src/aplikacje/kalkulator/`, UID 205) — cztery działania liczone od lewej do
prawej (jak w zwykłym kalkulatorze: 12 + 7 × 3 = 57), procent (200 + 10 % = 220), zmiana znaku, pamięć
(MC, MR, M+, M−), C / CE / cofnij; liczby po polsku (przecinek, odstęp co trzy cyfry, do 15 cyfr, potem
zapis „1,5e20”); dzielenie przez zero — „Nie można dzielić przez zero”. Klawiatura: cyfry, + − * /,
Enter albo =, Backspace, Esc, % i klawiatura numeryczna. Silnik bez rysowania (`liczydlo.c`, test na
hoście `test_kalkulator`), okno w `main.c`. Bez dostępu do plików i sieci, bez kanałów od nadzorcy.

### Zasoby
(M4.1, ADR 0012) — procesor (procent wszystkich rdzeni) i pamięć (użyta z całości)
całego systemu: liczba + wykres z ostatniej minuty (jedna seria, kolor akcentu, linia 2 px);
tabela usług i aplikacji: procesor i pamięć każdej (z ich cgroup); czas pracy systemu. Dane od
nadzorcy co sekundę (fd 4), sprawdzane: nadawca UID 0, bez deskryptorów, każde pole.
0.6.3: wiersz **Bateria** (`ZASOBY_BATERIA`): procent, pobór mocy w W (`power_now` albo prąd ×
napięcie, uśredniony przez nadzorcę: 30 % nowego odczytu, od nowa przy zmianie ładowanie ↔
rozładowanie; odczyt co 5 s, gdy Zasoby działają) i czas do rozładowania / naładowania (energia ÷
pobór; brak szacunku poniżej 0,1 W i powyżej 99 h).

### Pliki
(0.8.0, ADR 0029, `src/aplikacje/pliki/`, UID 206) — pliki i foldery użytkownika, jak w Windows: z lewej
stałe foldery (Pulpit, Dokumenty, Obrazy, Muzyka, Wideo, Pobrane) i Kosz (pełny — z kulką papieru); nad listą
„w górę” i ścieżka („Dokumenty › Projekt”); przyciski miejsca: „Nowy folder” (od razu zmiana nazwy, jak
w Windows), „Zmień nazwę”, „Usuń” (do Kosza); w Koszu: „Przywróć”, „Usuń na zawsze”, „Opróżnij Kosz” (oba
ostatnie z pytaniem w stopce). Przywrócony plik, którego folder jest w Koszu (albo już nie istnieje), trafia do
Dokumentów — stopka to mówi („Folder „Projekt” jest w Koszu — przywrócono do Dokumentów.”, 0.8.1). Gdy zostało mniej
niż 10 % miejsca na pliki, stopka mówi „Mało miejsca na pliki: zostało X z Y” (`SKARB_MIEJSCE` po każdej liście, 0.8.1). Lista: najpierw foldery, potem pliki po nazwie; kolumny nazwa, data zmiany
(w Koszu: usunięcia), rozmiar; podwójne kliknięcie (i Enter) otwiera folder, a plik — w domyślnej aplikacji
jego typu (Ustawienia → Aplikacje; Pliki — tylko szczegóły; zdjęcie — od 0.8.2 w **Podglądzie**); **„Otwórz w…”** (0.8.0, ADR 0030) zamienia przyciski
na aplikacje, które otwierają ten typ (`K1_OTWIERANIE`) — wybrana dostaje ten jeden plik (`DOK_OTWORZ`). Z prawej **szczegóły** zaznaczonego (do 0.8.2 „podgląd” — decyzja D1): ikona,
nazwa, typ (rozpoznany z zawartości), rozmiar, daty, pochodzenie („utworzony w K1”), w Koszu — za ile dni
zniknie; plik tekstowy — początek treści (do 64 KiB, `DOK_CZYTAJ`); **zdjęcie** (0.8.0 krok 4, ADR 0032; PNG i JPEG,
od 0.8.2 także GIF, BMP, WebP — rodzina „obrazy”) — zmniejszone do 200×150 przez usługę `dekoder`
(`komponenty/dekoder.md`; plik idzie do niego kawałkami prosto od skarbca — **miniatura**; uszkodzone: „Miniatura
niedostępna”, ponad 100 Mpx: „Zdjęcie ma 150 Mpx — / K1 pokazuje do 100 Mpx”, za mało pamięci: „Za mało pamięci na
miniaturę”), a pod nim
przycisk **„Ustaw jako tapetę”** (poza Koszem): zdjęcie do 2560×1600 idzie od dekodera prosto do ekranu, postęp i wynik
w stopce. Klawisze: Enter (otwórz folder),
Backspace (w górę), F2 (zmień nazwę), Delete (do Kosza), strzałki, Esc. Złe nazwy (`\ / : * ? " < > |`,
kropka na końcu) i zajęte — komunikat w stopce. **Pendrive** (0.8.0 krok 3, ADR 0031): gdy włożony jest pendrive
(nie pendrive K1), pod Koszem pojawia się miejsce „Pendrive” — jego foldery i pliki (tylko do odczytu: bez zmiany nazwy
i usuwania) czyta usługa `czytnik` (`komponenty/czytnik.md`); przyciski „Importuj” (zaznaczony plik) i „Importuj
wszystkie” (pliki folderu): zdjęcia (rodzina „obrazy”) do Obrazów, reszta do Dokumentów, pochodzenie „z pendrive'a”; od 0.8.1
(krok 4) pliki do 4 GB, kawałkami po 1 MiB prosto z czytnika do skarbca (Pliki nie czytają treści; przy dużym pliku
stopka: „Importowanie: 1 z 3… 120 z 300 MB”), wyjęty pendrive w trakcie — skarbiec usuwa już zapisane kawałki;
miniatura zdjęcia do 128 MB i 100 Mpx (limity dekodera, od 0.8.2; wcześniej 16 MB i 24 Mpx); postęp i wynik w stopce; wyjęty pendrive — powrót do Dokumentów.
Od 0.8.2 (krok 2): folder z pendrive'a przychodzi w kolejnych listach (po 1024; stopka „Czytanie folderu… n
elementów”), widać i można zaimportować do 16 384 elementów jednego folderu („Importuj wszystkie” — gdy cały folder
jest już wczytany); plik dostaje czas „zmieniono” z pendrive'a (`DOK_CZAS`), a ponowny import pomija plik, który już
jest w Obrazach / Dokumentach z pendrive'a z tą samą nazwą, rozmiarem i czasem — stopka „Pominięto n — już
zaimportowane” (decyzja D3); brak miejsca i limit wpisów przerywają import z powodem: „Brak miejsca na pliki —
zaimportowano n z m.”, „Brak miejsca w pamięci (K1 działa bez nośnika danych) — …”, „Za dużo plików i folderów (K1
mieści 16 384) — …”. Lista sortowana `qsort` (16 384 elementy). Moduły: `main.c` (okno,
mysz, klawisze), `przyciski.c` (przyciski miejsca), `rysuj.c` (lista, szczegóły, stopka), `dane.c` (skarbiec i nadzorca),
`pendrive.c` (pendrive i import), `obrazy.c` (miniatura i tapeta przez dekoder — od 0.8.2 przez wspólny
`k1_dekodowanie` z przybornika; kanał do dekodera = 5. kanał aplikacji, `K1_APL_KANALY`).

### Podgląd
(0.8.2 krok 4, ADR 0037, `src/aplikacje/podglad/`, UID 208) — **zdjęcie w osobnym oknie**, jak w Zdjęciach Windows.
Podwójne kliknięcie zdjęcia (rodzina „obrazy”: PNG, JPEG, GIF, BMP, WebP) w Plikach albo na pulpicie otwiera je tu —
Podgląd jest domyślną aplikacją zdjęć (`K1_OTWIERANIE`; inną można wybrać w Ustawienia → Aplikacje). Wygląd: szkic C
(decyzja właściciela D5, `docs/WYGLAD.md`) — okno 960 × 640, zdjęcie na całym ciemnym obszarze, dopasowane do okna
(nigdy powiększone ponad 100 %); w lewym górnym rogu etykieta: nazwa, wymiary zdjęcia, rozmiar, data zmiany
i pochodzenie („Zachód.jpg · 4000 × 2667 · 2,4 MB · 10.11.2026 18:42 · z pendrive'a”); na dole pływający pasek:
**‹** „3 z 27” **›** (kolejność jak w Plikach — po nazwie; po ostatnim pierwsze), **−** **+** (przybliżenie: 1,5×, 2×,
3×, 4×… dopasowanego zdjęcia, z krótką animacją, najwyżej do 4 pikseli ekranu na piksel obrazu; przybliżone zdjęcie
przesuwa się przeciągnięciem myszą), **„Otwórz…”**, **„Obróć”**, **„Ustaw jako tapetę”**, **„Do Kosza”**.
Klawisze: ← / → (poprzednie / następne), Home / End, **+** / **−** (przybliż / oddal), **0** (dopasuj), **R** (obrót widoku o 90° w prawo — pliku nie zmienia), **Delete**
(do Kosza bez pytania — decyzja D2; potem następne zdjęcie), **Ctrl+O** („Otwórz…”: okno wyboru pliku z samymi
zdjęciami). Uruchomiony z menu Start pokazuje „Otwórz zdjęcie z Plików albo przyciskiem „Otwórz…” (Ctrl+O).”
Zdjęcia nie da się odczytać: „Tego zdjęcia nie da się odczytać.”, ponad 100 Mpx: „Zdjęcie ma 150 Mpx — K1 pokazuje do
100 Mpx.”, plik ponad 128 MB: „Plik jest za duży — K1 pokazuje zdjęcia do 128 MB.” Podgląd **widzi tylko zdjęcia
folderów, z których użytkownik w nim coś otworzył** (prawa w skarbcu: `Z_OBRAZY_FOLDERU`), tylko do odczytu i do
Kosza; nie dekoduje zdjęć sam — robi to `dekoder` w osobnej klatce (`k1_dekodowanie`, jak w Plikach; obraz najwyżej
1600 × 1600, obrót, dopasowanie i przybliżenie u siebie: `k1_skaluj`). Moduły: `main.c` (okno, pływający pasek, etykieta, klawisze, mysz), `dane.c`
(skarbiec, plik do otwarcia, Kosz, okno wyboru), `obraz.c` (dekoder, tapeta), `widok.c` (obrót, dopasowanie, przybliżenie z animacją, przeciąganie), `kolejnosc.c` (kolejność zdjęć —
czyste funkcje, test na hoście).

## Granica zaufania
Klienci ekranu w roli **aplikacja**: tylko własne okna. Ustawienia jako jedyna mogą zmieniać
ustawienia (ekran sprawdza rolę i UID). Treść notatki nigdy nie trafia do logu (tylko długość).

## Uprawnienia
| Aplikacja | UID | seccomp (poza bazą) | Landlock | cgroup |
|---|---|---|---|---|
| Zegar | 200 | `memfd_create`, `ftruncate` | brak ścieżek | 32 MiB, 1 proces |
| Notatki | 201 | `memfd_create`, `ftruncate` (od 0.5.6 bez plików) | brak ścieżek | 32 MiB, 1 proces |
| Ustawienia | 202 | `memfd_create`, `ftruncate` | brak ścieżek | 32 MiB, 1 proces |
| Zasoby | 203 | `memfd_create`, `ftruncate` | brak ścieżek | 32 MiB, 1 proces |
| Instalator | 204 | `memfd_create`, `ftruncate` | brak ścieżek | 32 MiB, 1 proces |
| Kalkulator | 205 | `memfd_create`, `ftruncate` | brak ścieżek | 32 MiB, 1 proces |
| Pliki | 206 | `memfd_create`, `ftruncate` | brak ścieżek | 32 MiB, 1 proces |
| Wybór pliku | 207 | `memfd_create`, `ftruncate` | brak ścieżek | 32 MiB, 1 proces |
| Podgląd | 208 | `memfd_create`, `ftruncate` | brak ścieżek | 96 MiB (obraz do przybliżania: 1600 × 1600 i jego obrót), 1 proces |

## Wejścia i wyjścia
fd 3: kanał do ekranu. Okno: memfd (ADR 0008). Notatki: fd 4 — kanał do skarbca: od 0.8.0 `DOK_*` — tylko
własne pliki i pliki dane przez użytkownika (tabela praw w skarbcu), `DOK_WYBIERZ` → `DOK_WYBRANY`; stare obiekty
1–33 tylko do przeniesienia (`SKARB_CZYTAJ`, `SKARB_USUN`); fd 5 — od nadzorcy, jednokierunkowo: `PLIK_OTWORZ`
(obiekt = plik otwarty w Notatkach z Plików albo z pulpitu). Wybór pliku: fd 4 — kanał do skarbca (lista nazw,
`DOK_PROSBA`, `DOK_WYBRANO`). Zasoby: fd 4 —
kanał od nadzorcy (`ZASOBY_SYSTEM`, `ZASOBY_USLUGA`, od 0.6.3 `ZASOBY_BATERIA`), aplikacja nie może do niego pisać.
Ustawienia: lista trybów ekranu (`K1_KOM_TRYB`) dostaje tylko ona; od 0.5.5 fd 4 — kanał od nadzorcy
(`BATERIA`, `BATERIA_HIST`: do 16 próbek po 1 bajcie w komunikacie), jednokierunkowy, nadawca UID 0.
Bez kanału (zamknięty) reszta Ustawień działa.
Pliki (0.8.0): fd 4 — kanał do skarbca z prawami do wszystkich plików użytkownika (i do niczego więcej: obiekty
innych aplikacji są dla Plików niewidoczne), komunikaty `DOK_*` (`komponenty/skarbiec.md`); fd 5 — od nadzorcy,
jednokierunkowo: `PLIKI_POKAZ` (a = plik albo folder do pokazania, b = 1 Kosz), gdy pulpit otwiera Pliki przy
pliku albo Koszu (`URUCHOM` z miejscem — tylko dla Plików). Lista ze skarbca sprawdzana wpis po wpisie
(`k1_dok_lista_czytaj`); nazwy i treść nigdy nie trafiają do logu (tylko liczby). Od 0.8.0 krok 3: fd 6 — od
nadzorcy `PENDRIVE` (jest / nie ma), `PENDRIVE_KANAL` (kanał do czytnika); do nadzorcy `PENDRIVE_OTWORZ`. Kanał do
czytnika: `PEN_*` (`komponenty/czytnik.md`); plik z pendrive'a idzie do skarbca w memfd od czytnika, bez czytania
(`DOK_NOWY` z pochodzeniem — prawo tylko Plików).
Podgląd (0.8.2): fd 4 — kanał do skarbca (`DOK_SPIS`, `DOK_CZYTAJ` kawałkami dla dekodera, `DOK_KOSZ`, `DOK_WYBIERZ` z
rodziną „obrazy”; tylko zdjęcia folderów otwartych w nim); fd 5 — od nadzorcy, jednokierunkowo: `PLIK_OTWORZ`; fd 6 —
do nadzorcy `DEKODER`, od nadzorcy `DEKODER_KANAL` (kanał do nowego dekodera: `DEK_*`, `komponenty/dekoder.md`).
Zdjęcie na tapetę: `TAPETA_ZDJECIE` do ekranu (memfd od dekodera, bez czytania), jak z Plików.

## Obsługa błędów
Zamknięcie okna (×) albo kanału → koniec procesu; aplikacje nie są restartowane (test).
Błąd zapisu notatki → komunikat w oknie i w logu.

## Testy
| Test | Rodzaj |
|---|---|
| `test-m4.sh`: Notatki — „Zażółć K1” (AltGr), zapis 9 znaków, tekst na zrzucie, przeciąganie okna | funkcjonalny |
| `test-m4.sh` (0.5.8): druga notatka, po ponownym starcie obie (ostatnio zmieniona na górze), otwarcie drugiej, usunięcie z potwierdzeniem | funkcjonalny |
| `test-m4.sh`: Zegar — tarcza na zrzucie; Ustawienia — ciemny motyw i tapeta na zrzucie | funkcjonalny |
| `test-m5.sh`: Ustawienia → Bateria — historia od nadzorcy (≥ 2 próbki), linia wykresu na całą szerokość na zrzucie `m5-bateria.png` | funkcjonalny |
| `test-m4.sh`: Ustawienia → Ekran — zmiana rozdzielczości (zrzut w nowym rozmiarze, pulpit ułożony od nowa); Zasoby — dane od nadzorcy, wykres pamięci na zrzucie | funkcjonalny |
| w systemie: klatki aplikacji (od 0.8.0 także Pliki: UID, brak plików i urządzeń) | funkcjonalny |
| w systemie: próby w klatkach (także Notatki: `open` zablokowane, dysk niedostępny) | negatywny |
| `test-m4.sh`: notatka wraca po wyłączeniu i ponownym starcie z tym samym dyskiem danych | funkcjonalny |
| w systemie: zabita aplikacja nie wraca sama | negatywny |
| `test-m4.sh` (0.8.0): Pliki — nowy folder, zmiana nazwy na klawiaturze, zajęta nazwa, wejście do folderu i „w górę”, Kosz (usuń, przywróć, usuń na zawsze z pytaniem, opróżnij), pliki po ponownym starcie; zrzut `m4-pliki.png` | funkcjonalny |
| host: `test_dokumenty` (skarbiec), `test_komunikat` (`DOK_*`, `PLIKI_POKAZ`), `fuzz_dokument` (lista) | jednostkowy / negatywny / fuzzing |
| host (0.8.0, ADR 0030): `test_dokumenty` — prawa Notatek (tylko własne pliki, nowy plik tylko w swoim folderze, Kosz tylko dla własnych), „Otwórz w…” (pulpit: tylko folder Pulpit; zły typ — odmowa), okno wyboru (jedna prośba naraz, tylko nazwy, folder i zły typ odrzucone, zamknięte okno = anulowanie, dostęp znika z aplikacją); `test_komunikat` — `DOK_OTWORZ`, `DOK_WYBIERZ`, `DOK_WYBRANO`, `DOK_WYBRANY`, `DOK_PROSBA`, `SKARB_URUCHOM`, `PLIK_OTWORZ`, `POKAZ_APL`, domyślne aplikacje | jednostkowy / negatywny |
| `test-pliki.sh` (0.8.0, krok 2): stare notatki przeniesione do plików, nazwa za pierwszym wierszem, „Otwórz w…” z Plików i z pulpitu, okno wyboru pliku (wybór i „Anuluj”), Ustawienia → Aplikacje | funkcjonalny |
| host: `test_przybornik` — czas polski (zmiany czasu), układ PL, UTF-8, rysowanie na krawędziach | jednostkowy |
| host (0.8.2 krok 4, ADR 0037): `test_podglad` — kolejność zdjęć, ← / →, zdjęcie po Koszu, „3 z 27”; `test_dokumenty` — prawa Podglądu (bez gestu nic, tylko zdjęcia folderu, tylko odczyt i Kosz, okno wyboru z rodziną „obrazy”, po zamknięciu nic) | jednostkowy / negatywny |
| `test-pendrive.sh` (0.8.2 krok 4): Podgląd — dwuklik zdjęcia w Plikach, „zdjęcie 4 z 10” w klatce UID 208, kolor na zrzucie, R (widok 60×80), ← uszkodzone (komunikat), ‹ PNG, Delete → Kosz i następne zdjęcie, Ctrl+O → okno wyboru (obrazy), „Ustaw jako tapetę”, × ; + i − (150 % → 100 %, kolor na zrzucie) | funkcjonalny / negatywny |
