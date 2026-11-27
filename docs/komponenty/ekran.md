# Komponent: ekran — obraz i okna (`/rdzen/system/ekran`)

| | |
|---|---|
| **Status** | PRZETESTOWANE (M2–M4) — `scripts/test-m4.sh` (zrzuty), testy w systemie (klatka, złośliwe IPC, restart) |
| **Kod** | `src/ekran/main.c` (start, kanał sterujący, pętla, zmiana trybu), `scena.c` (składanie, wyjście), `okna.c` (klienci, okna, fokus, komunikaty), `pelny.c` (pełny ekran okna strefy, 0.11.0), `powierzchnie.c` (pamięć, nowa powierzchnia, zatwierdzanie; 0.9.0; od 0.10.5 krok 4 zamiana powierzchni okna strefy w nowym rozmiarze), `rozmiar.c` (0.10.5 krok 4: zmiana rozmiaru i maksymalizacja okna strefy, obszar roboczy, zapamiętany rozmiar sesji), `wskaznik.c` (mysz, klawiatura), `zabezpieczenia.c` (Ctrl+Alt+Del i ekran zabezpieczeń, 0.13.0 krok 6), `drm.c` (KMS, lista trybów), renderer `src/grafika/k1_grafika.c`, protokoły `src/wspolne/k1_zdarzenie.c`, `k1_komunikat.c` |
| **Decyzje** | ADR 0006, ADR 0007 (klatka, kanały), ADR 0008 (okna), ADR 0010 (simpledrm), ADR 0012 (przegląd, tryby); wygląd: `docs/WYGLAD.md` |

## Cel
Jedyny proces z dostępem do karty graficznej (DRM/KMS) i kompozytor okien: ustawia tryb, rysuje
tapetę (jeden z 5 wariantów), składa okna aplikacji i panele powłoki z ich pamięci współdzielonej,
dodaje cienie okien i kursor, robi page flip. Kieruje wejście (mysz do powierzchni pod kursorem,
klawiatura do okna z fokusem, Super i Alt+Tab do powłoki), zarządza fokusem, kolejnością, przeciąganiem
i minimalizacją okien, przechowuje ustawienia i rozsyła je klientom.

## Granica zaufania
Ufa jądru (DRM) i nadzorcy (kanał sterujący, UID 0 sprawdzany). **Nie ufa** klientom: każdy
komunikat sprawdzany pole po polu, UID nadawcy każdego pakietu porównywany z tożsamością od
nadzorcy, role egzekwowane (panele i cudze okna — tylko powłoka; ustawienia — tylko Ustawienia),
powierzchnia przyjmowana tylko jako memfd z pieczęciami SHRINK/GROW/SEAL i dokładnym rozmiarem,
mapowana tylko do odczytu, łącznie najwyżej 2 pełne ekrany na klienta (aplikacja i powłoka — od 0.7.5 z warstwą pulpitu; ekran hasła: 1); obszar
zatwierdzenia przycinany do własnej powierzchni; po 50 błędach klient jest rozłączany. Treść pikseli klientów jest
tylko kopiowana i mieszana (bez interpretacji).

## Uprawnienia
UID/GID 101, bez capabilities, `NO_NEW_PRIVS`, seccomp (baza + `ioctl` typu `'d'` = DRM + `memfd_create` —
ustawienia i zdjęcie tapety dla skarbca),
Landlock bez żadnej ścieżki, cgroup `ekran` (176 MiB — z miejscem na zdjęcie na tapetę, 0.8.0; 1 proces). Kartę otwiera nadzorca (to
otwarcie jest DRM masterem) i przekazuje jako fd 4.

## Wejścia i wyjścia
- fd 3: kanał sterujący od nadzorcy — `KLIENT(rola, aplikacja, uid)` + koniec kanału klienta.
- Rola `LOGOWANIE` (0.5.7, ADR 0017): panele jak powłoka; dopóki taki klient jest połączony, dostaje
  każdy klawisz (także Super i Alt+Tab).
- **Alt+Tab** (0.10.5 krok 6, T16; `wskaznik.c`): od pierwszego Tab przy wciśniętym lewym Alt aż do puszczenia Alt
  (`s.przelaczanie`) Tab (z Shift — wstecz), Esc i puszczenie Alt idą do powłoki — nigdy do okna z fokusem; Shift w tym
  czasie nie idzie do strefy. Strefa z fokusem dostaje najpierw puszczenie Alt (jej gość nie zostaje z „wciśniętym” Alt —
  S43; przy zmianie fokusu VMM i tak puszcza wszystkie klawisze gościa: `FOKUS` 0 → `wejscie_pusc`). Strefa nie ma
  komunikatu, którym przełączyłaby okna, i nie zablokuje skrótu (klawiatura idzie przez ekran). Log „Alt+Tab: przełączanie
  okien w powłoce” (przy strefie z fokusem: „(strefa z fokusem: puszczony Alt)”) — raz na przytrzymanie Alt.
- fd 5 (0.5.6, opcjonalny): kanał do skarbca — przy starcie odczyt `K1_OB_USTAWIENIA` (≤ 1,5 s; tapeta,
  motyw, mysz, sekundy, rozdzielczość jako szer.×wys.; każda wartość sprawdzana jak `USTAW`, zła →
  domyślna), po każdej zmianie zapis nowej wersji (`src/ekran/zapis.c`). Rozdzielczość przywracana po
  pierwszej klatce, jeśli karta ją ma. Format zapisu: `K1U3` (0.8.2: miejsce na domyślne aplikacje 32 typów plików —
  GIF, BMP i WebP dały 9 typów, a `K1U2` mieścił 8); ekran czyta też `K1U2` (0.8.0–0.8.1) i `K1U1` (0.5.6–0.7.6) —
  typy, których stary zapis nie ma, zostają domyślne. Od 0.8.2 krok 4 zapis `K1U4`: w `K1U1`–`K1U3` aplikacją zdjęć
  mogły być tylko Pliki (nie wybór użytkownika), więc dla typów rodziny „obrazy” ekran bierze wtedy domyślną —
  Podgląd (ADR 0037).
- Zdjęcie na tapetę (0.8.0, ADR 0032, `src/ekran/zdjecie.c`): przyjmuje je tylko od aplikacji z tabeli nadawców —
  Plików i (od 0.8.2, ADR 0037) Podglądu (`TAPETA_ZDJECIE`:
  piksele z dekodera, do 2560×1600, zapieczętowany memfd dokładnie tego rozmiaru — pliku zdjęcia ekran nie widzi).
  Rysuje je „wypełniając” ekran (wyśrodkowane, przycięte po bokach albo u góry i dołu; `k1_skaluj`) przy każdej
  rozdzielczości. W skarbcu jako własne obiekty: `K1_OB_ZDJECIE` („K1Z1”, szerokość, wysokość — zapisywany na końcu)
  i do 12 kawałków po 1 MiB (3 bajty na piksel); przy starcie wczytane tylko w całości. `K1_UST_ZDJECIE` (0/1) mówi
  klientom, czy zdjęcie jest — ustawia go tylko ekran; `USTAW` tapety = zdjęcie bez zdjęcia jest odrzucany.
- Kanały klientów: wejście (`k1_zdarzenie` 16 B, do 4 źródeł), powłoka i aplikacje (`k1_komunikat`
  32 B, ADR 0008), razem do 12.
- Karta: dwa bufory „dumb” XRGB8888 w trybie preferowanym (QEMU `bochs` 1280×800; laptopy właściciela od 0.10.9
  krok 2 — `i915`, page flip jak bochs; simpledrm — tryb z firmware; vmwgfx — VirtualBox VMSVGA/VMware), scena i tapeta w RAM. Przy `simpledrm` (0.5.4,
  rozpoznany przez `DRM_IOCTL_VERSION`): jeden bufor i `DIRTYFB` tylko ze zmienionymi prostokątami
  (≤ 17) — page flip w simpledrm kopiuje cały ekran do pamięci firmware, co na prawdziwym sprzęcie
  (ThinkPad X230) zacinało pulpit. Lista trybów (różne
  rozmiary 800×600…1920×1200 + preferowany, do 18) → Ustawienia (`K1_KOM_TRYB`); `K1_UST_ROZDZIELCZOSC` przełącza
  tryb między klatkami (nowe bufory → `SETCRTC` → zwolnienie starych; błąd = poprzedni tryb), po
  czym klienci dostają `WITAJ` z nowym rozmiarem.
- Log: tryb, pierwsza klatka, okna (powstanie, przesunięcie, minimalizacja, zamknięcie), ustawienia,
  odrzucone komunikaty (z powodem, ≤ 8 na klienta), stan kursora ≤ 2×/s. **Bez kodów klawiszy.**

## Komunikaty (kanały klientów, `k1_komunikat` 32 B, ADR 0008)
Pola: `obiekt` = numer powierzchni klienta 1…8 (o ile nie podano inaczej), `a`…`e` jak w `k1_komunikat.h`.

| Komunikat | Kierunek | Kto może | Znaczenie |
|---|---|---|---|
| `POWIERZCHNIA` | klient → ekran | okno: aplikacja; panel: powłoka i logowanie; pulpit: tylko powłoka, jeden; ekran zabezpieczeń (0.13.0 krok 6, `K1_POW_ZABEZPIECZENIA`): tylko powłoka, jeden | nowe okno, panel, warstwa pulpitu (0.7.5) albo ekran zabezpieczeń (jak panel, z położeniem; poza limitem pamięci): szerokość, wysokość, rodzaj, położenie panelu; + memfd z pieczęciami. Okno z `e` = 1 (0.10.5 krok 4): zamienia powierzchnię okna `obiekt` — tylko w rozmiarze z ostatniego `ROZMIAR` |
| `ZATWIERDZ` | klient → ekran | właściciel powierzchni | gotowa połowa bufora i zmieniony prostokąt; okno strefy wraca nim do własnych pikseli (0.10.11) |
| `STREFA_BUFOR` (0.10.11 krok 2, ADR 0051) | strefa (VMM) → ekran | rola `STREFA`, jej okno | bufor karty przeglądarki: `a` = numer 1…`K1_STREFA_BUFORY` (4), `b`, `c` = szerokość, wysokość, `d` = bajty wiersza (≥ 4·`b`, wielokrotność 4), `e` = początek obrazu w buforze (wielokrotność 4); + dma-buf (w teście memfd silnika testowego pośrednika). Ekran sprawdza rozmiar bufora (`lseek`), mapuje tylko do odczytu część z obrazem; numer zastępuje wcześniejszy bufor o tym numerze |
| `STREFA_OBRAZ` (0.10.11 krok 2) | strefa (VMM) → ekran | rola `STREFA`, jej okno | okno pokazuje bufor `a` (wysłany wcześniej, nie większy niż okno), `b`…`e` = zmieniony prostokąt (`d` = `e` = 0: całość); `RAMKA` jak po `ZATWIERDZ` |
| `PELNY_EKRAN` (0.11.0 krok 1, D1) | strefa (VMM) → ekran | rola `STREFA`, jej okno | `a` = 1: gość prosi o **pełny ekran** (F11, film „na cały ekran”), `a` = 0: wyszedł z niego sam (Esc); reszta 0. Ekran zgadza się tylko dla okna z fokusem i tylko w `GEST_S` (10 s) po geście użytkownika w tym oknie (wciśnięcie przycisku albo klawisz, które ekran temu oknu dał); po wejściu gest się zużywa. Odmowa — tylko wpis w logu (strefa nie dostaje odpowiedzi); zgoda — `ROZMIAR` z `c` = `K1_ROZMIAR_PELNY` |
| `PRZECIAGANIE` (0.11.0 krok 4, ADR 0052) | klient → ekran | Pliki (okno), powłoka (pulpit); tylko powierzchnia, która trzyma wskaźnik z wciśniętym lewym przyciskiem | co jest przeciągane: `a` = źródło `K1_PZ_*`, `b` = ile (1…16 384), `c` = ile z nich folderów, `d` = pierwszy element (numer skarbca; plik z pulpitu), `e` = folder, w którym leżą — żadnych nazw. Ważne do puszczenia przycisku |
| `UPUSZCZANIE_CO` (0.11.0 krok 4) | ekran → cel | Pliki, pulpit | to samo co `PRZECIAGANIE` (a…e) — zanim kursor z cudzym przeciąganiem pierwszy raz przyjdzie nad tę powierzchnię |
| `UPUSZCZANIE` (0.11.0 krok 4) | ekran → cel | Pliki, pulpit | kursor z cudzym przeciąganiem nad powierzchnią: `a`, `b` = x, y, `c` = `K1_UP_RUCH` / `WYJSCIE` / `UPUSC` (puszczony), `d` = klawisze `K1_MOD_*` (Ctrl: kopia, D6) |
| `PRZECIAGANIE_CEL` (0.11.0 krok 4) | pulpit → ekran | powłoka; tylko odpowiedź na `UPUSZCZANIE UPUSC`, raz, w ciągu 2 s | gdzie trafia upuszczenie z Plików: `a` = `K1_PC_FOLDER` (`b` = folder: Pulpit albo folder na pulpicie) / `K1_PC_KOSZ` / 0 (nic). Ekran przekazuje to Plikom (`PRZECIAGANIE_KONIEC`) |
| `PRZECIAGANIE_GDZIE` (0.11.0 krok 4) | ekran → źródło | Pliki, pulpit | `a` = 1: kursor jest nad innym oknem albo pulpitem (tam rysuje się znak — źródło chowa swoją kartę), 0: wrócił nad źródło |
| `PRZECIAGANIE_KONIEC` (0.11.0 krok 4) | ekran → źródło | Pliki, pulpit | upuszczone gdzie indziej: `a` = `K1_PC_FOLDER` / `K1_PC_KOSZ` (`b` = folder) albo `K1_PC_STREFA` (`b` = numer strefy), `c` = klawisze przy puszczeniu; robi to źródło (Pliki pytają, przenoszą, kopiują; do strefy — `DOK_DO_STREFY` „upuść”) |
| `UPUSC_STREFA` (0.11.0 krok 4) | ekran → strefa (VMM) | ekran | upuszczono pliki na okno strefy: `a`, `b` = punkt w obrazie strefy, `c` = ile plików (1…8); kopie przychodzą potem przez nadzorcę. Póki kursor tylko jest nad oknem, strefa nic nie dostaje |
| `SCHOWEK_USTAW` (0.11.0 krok 5, ADR 0052, D5: A) | klient → ekran | klient okna z fokusem, tylko do 2 s (strefa: 10 s) po klawiszu albo wciśnięciu użytkownika w tym oknie | skopiowano: `a` = bajty 1…1 MiB, reszta 0; + memfd z pieczęciami z tekstem (UTF-8 bez NUL i znaków sterujących — `k1_schowek_dobry`). Ekran trzyma tekst w pamięci (nigdy w skarbcu ani w logu) i skąd jest (K1 albo numer strefy); kopia bez gestu albo zły tekst — odrzucone |
| `SCHOWEK_DAJ` (0.11.0 krok 5) | aplikacja → ekran | aplikacja okna z fokusem, do 2 s po geście (Ctrl+V, „Wklej”) | prośba o tekst schowka; odpowiedź `SCHOWEK` (pusty schowek: nic). Strefa nigdy nie pyta |
| `SCHOWEK` (0.11.0 krok 5) | ekran → klient | aplikacja (odpowiedź na `SCHOWEK_DAJ`); VMM strefy — tuż przed Ctrl+V do jej okna | `a` = bajty, `b` = skąd (0 K1, numer strefy); + memfd z tekstem. Tekst z innej sesji: na ramce okna na 3 s „Wklejono z: <sesja>” |
| `POKAZ` | klient → ekran | powłoka, logowanie | pokaż / ukryj panel |
| `PRZESUN` | klient → ekran | powłoka, logowanie | nowe położenie panelu |
| `PRZECIAGNIJ` | klient → ekran | właściciel okna | zacznij przesuwać okno wciśniętym przyciskiem myszy |
| `MINIMALIZUJ` | klient → ekran | właściciel okna | schowaj okno (wraca przez powłokę) |
| `USUN` | klient → ekran | właściciel powierzchni | usuń powierzchnię |
| `AKTYWUJ_OKNO` | powłoka → ekran | tylko powłoka | `obiekt` = numer okna: pokaż, na wierzch, fokus |
| `MINIMALIZUJ_OKNO` | powłoka → ekran | tylko powłoka | `obiekt` = numer okna: schowaj |
| `ZAMKNIJ_OKNO` | powłoka → ekran | tylko powłoka | `obiekt` = numer okna: ekran prosi aplikację o zamknięcie (0.7.5, „Zamknij okno” na dolnym pasku) |
| `STREFA_DZWIEK` (0.12.0 krok 3, D6: A) | powłoka → ekran | tylko powłoka | `obiekt` = 0, `a` = strefa 1…`K1_STREFY_MAKS`, `b` = 1 jej sesja gra, `c` = 1 wyciszona; ekran rysuje głośnik na pasku okna tej strefy, póki `b` = 1 (`strefy_dzwiek.c`) |
| `STREFA_WYCISZ` (0.12.0 krok 3) | ekran → powłoka | ekran | `obiekt` = 0, `a` = strefa — użytkownik kliknął głośnik na pasku okna sesji: powłoka wycisza sesję albo pozwala jej znów grać (usługa dźwięku, `DZWIEK_GLOSNOSC` d = strefa) |
| `GLOSNOSC_POKAZ` (0.12.0 krok 2, D4: A) | powłoka → ekran | tylko powłoka | po klawiszu głośności: `obiekt` = 0, `a` = głośność 0…100, `b` = 1 wyciszone, `c` = wyjście `K1_DZW_WYJSCIE_*` (BRAK: komputer bez karty — wtedy `a` = `b` = 0). Ekran pokazuje na 1,5 s **pigułkę głośności** (`glosnosc.c`) |
| `USTAW` | Ustawienia → ekran | Ustawienia; klucze `K1_UST_OTWIERANIE` + typ także Pliki i powłoka (0.8.2, „Zawsze otwieraj tą aplikacją”; `ustawia()` w `okna.c`) | klucz `K1_UST_*` i wartość (sprawdzana); nie `K1_UST_ZDJECIE` |
| `TAPETA_ZDJECIE` | Pliki, Podgląd → ekran | tabela nadawców (`zdjecie.c`) | zdjęcie na tapetę (0.8.0): szerokość, wysokość; + memfd z pikselami od dekodera |
| `WITAJ` | ekran → klient | — | rozmiar ekranu, rola, aplikacja; ponownie po zmianie rozdzielczości |
| `WSKAZNIK` | ekran → klient | — | mysz nad powierzchnią klienta (0 = poza nią): x, y, ruch / przycisk, który przycisk, e = wciśnięte Shift / Ctrl / Alt / AltGr (`K1_MOD_*`, 0.8.2 krok 6: Ctrl+klik i Shift+klik w Plikach) |
| `PRZEWIN` | ekran → klient | — | **przewijanie** (0.10.0 krok 4): kółko myszy albo dwa palce na touchpadzie nad powierzchnią — `a`, `b` = x, y; `c`, `d` = w bok i w dół w 1/120 ząbka kółka (`K1_PRZEWIN_ZABEK`; `d` > 0 = dalej w dół, treść jedzie w górę; `c` > 0 = w prawo; najwyżej `K1_PRZEWIN_MAKS`, nie oba 0); `e` = skąd (`K1_PRZEWIN_KOLKO` / `K1_PRZEWIN_TOUCHPAD`). Idzie do okna **pod kursorem**, także bez fokusu (jak w innych systemach); przy wciśniętym przycisku — do okna, które trzyma wskaźnik; nigdy do strefy przez ramkę K1 |
| `KLAWISZ` | ekran → klient | — | klawisz dla okna z fokusem (kod, w dół / w górę / powtórzenie, modyfikatory); nigdy w logu. Same klawisze Shift, Ctrl, Alt i AltGr idą jako `KLAWISZ` tylko do okna strefy (gość ma własną klawiaturę) — aplikacje K1 czytają je z pola modyfikatorów |
| `FOKUS` | ekran → klient | — | okno dostało / straciło klawiaturę |
| `RAMKA` | ekran → klient | — | ostatnie zatwierdzenie jest na ekranie, można rysować drugą połowę |
| `ZAMKNIJ` | ekran → aplikacja | — | zamknij to okno — jak przycisk × (0.7.5; Notatki najpierw zapisują) |
| `ROZMIAR` (0.10.5 krok 4) | ekran → strefa (VMM) | — | nowy rozmiar obrazu okna strefy po geście użytkownika na ramce K1: `a` = szerokość `K1_OKNO_MIN_SZER`…`K1_POW_MAKS_BOK` (do 0.11.0 wielokrotność 8; od 0.11.0 dowolna — tryb gościa z EDID od VMM), `b` = wysokość `K1_OKNO_MIN_WYS`…`K1_POW_MAKS_BOK`, `c` = `K1_ROZMIAR_*`: 0 zwykłe, 1 zmaksymalizowane, 2 pełny ekran (0.11.0: cały ekran, bez ramki K1), reszta 0; strefa odpowiada `POWIERZCHNIA` z `e` = 1 dokładnie w tym rozmiarze. VMM, którego gość jest w pełnym ekranie, a dostaje `c` ≠ 2, mówi gościowi „pelny 0” (K1 zakończył pełny ekran) |
| `OKNO` | ekran → powłoka | — | okno aplikacji powstało / zmieniło stan / zniknęło (dolny pasek); okno strefy (0.10.0): `a` = 0, `c` = numer strefy |
| `USTAWIENIE` | ekran → wszyscy | — | nowa wartość ustawienia (motyw, tapeta, mysz, sekundy, rozdzielczość, czy jest zdjęcie na tapetę, domyślne aplikacje) |
| `TRYB` | ekran → Ustawienia | — | jeden z trybów ekranu (rozmiar, Hz, czy zalecany) |
| `OPIS` (0.10.2 krok 2, ADR 0045) | klient → ekran, tylko gdy `K1_UST_OPIS` = 1 | memfd (`k1_opis.h`) | elementy jednej powierzchni (obiekt), `a` = liczba rekordów 0…`K1_OPIS_MAKS`; sprawdzane pole po polu (rodzaj, stan, prostokąt w powierzchni, nazwa UTF-8 bez znaków sterujących); bez włączonego opisu — odrzucone |
| `OPIS_PYTANIE` | klient roli `TEST` → ekran | — | co jest na ekranie? Od innej roli — odrzucone |
| `OPIS_EKRANU` | ekran → klient roli `TEST` | memfd | widoczne powierzchnie od dołu (okno albo panel, potem jego elementy) we współrzędnych ekranu; `a` = rekordy, `b`, `c` = rozmiar ekranu |

**Opis ekranu dla testów (0.10.2 krok 2, ADR 0045; `opis.c`).** Rola `TEST` (`K1_ROLA_TEST`) — tylko program testowy
obrazu testowego (nadzorca daje ją przez `TEST_POLACZ`): bez powierzchni, wolno jej tylko pytać. Gdy taki klient jest
połączony, ekran wysyła wszystkim `USTAWIENIE` `K1_UST_OPIS` = 1 (nie zapisywane; nikt go nie ustawia) i trzyma tabele
elementów od klientów; gdy odejdzie — 0 i tabele znikają. W obrazie produkcyjnym nikt tej roli nie dostanie: opis jest
wyłączony, klienci nic nie wysyłają. Pełny kanał klienta testowego przy odpowiedzi nie jest błędem — klient zapyta znowu.

**Rola `STREFA` (0.10.0, ADR 0042; `strefy.c`).** VMM strefy jest klientem z jednym oknem (bez paneli, ustawień,
tapety). Ekran rysuje mu **ramkę** (0.10.0 krok 3, zmiana D1: szkic A — `docs/WYGLAD.md`): **pasek tytułu** 40 px
(`K1_STREFA_TYTUL`, `k1_okno.h`) nad obrazem strefy — ikona, „<tytuł> · ● <sesja> · osobna maszyna”, minimalizuj,
maksymalizuj, zamknij — i obwódkę 3 px wokół paska i obrazu. Od 0.10.7 (krok 4, ADR 0048, D1: szkic A) ramka jest
w kolorach okna K1 (tło, obrys, napis w kolorze tekstu), a kolor strefy z `K1_STREFY` jest tylko w kropce przed nazwą
sesji i w linii 2 px pod paskiem; do 0.10.5 pasek i obwódka były całe w kolorze strefy. Cała ramka leży **na zewnątrz** prostokąta
strefy i jest rysowana przed jej obrazem, więc strefa nie może jej zasłonić ani narysować (R26, R80); cień obejmuje okno
z ramką. Wskaźnik na ramce nie trafia do strefy: chwycenie paska przesuwa okno (pasek nie wyżej niż górny pasek K1),
minimalizuj chowa okno, zamknij wysyła VMM `ZAMKNIJ`; kursor nad przyciskiem go podświetla. VMM odejmuje wysokość paska
od wysokości obrazu (`src/strefa/okno.c`). Log, gdy kursor wchodzi na pasek okna: `strefa: pasek okna N (minimalizuj X,Y,
maksymalizuj X,Y, zamknij X,Y)` (testy klikają według niego). Do 0.10.0 krok 2 ramką była obwódka ze znaczkiem i plakietką (szkic B2).

**Rozmiar okna strefy (0.10.5 krok 4, T12–T14, D8 — `rozmiar.c`).** Zmienia go tylko ekran, na gest użytkownika na
ramce K1: **chwyty** brzegów (6 px: 3 w obwódce, 3 na zewnątrz) i rogów (16 px wzdłuż brzegu od rogu) — kursor zmienia się
w podwójną strzałkę (↔, ↕, dwie ukośne; `k1_rysuj_kursor_rozmiar`, gorący punkt na środku); w trakcie ciągnięcia **zarys**
w kolorze strefy z wymiarami „W × H” (rysowany na wierzchu sceny), okno się nie zmienia; po puszczeniu ekran wysyła strefie
**`ROZMIAR`** (obraz od `K1_OKNO_MIN_SZER` × `K1_OKNO_MIN_WYS` = 640 × 480 do obszaru roboczego; do 0.11.0 szerokość co 8 —
jądro strefy liczyło tryb CVT po 8 pikseli; od 0.11.0 krok 1 VMM daje gościowi EDID z dokładnym rozmiarem, T9), okno od razu staje w nowym miejscu (ciągnięcie lewego / górnego brzegu), a strefa
odpowiada `POWIERZCHNIA` z `e` = 1 — **zamianą** powierzchni okna dokładnie w tym rozmiarze (numer, fokus, kolejność
zostają; nowa pokazana od pierwszego zatwierdzenia, do tego czasu stara). Powierzchnia innego rozmiaru albo bez prośby
ekranu — odrzucona („rozmiar inny niż ten, o który prosił ekran”), okno zostaje. Strefa nie ma komunikatu „zmień mój
rozmiar” ani „przesuń mnie”. **Maksymalizacja:** przycisk między „minimalizuj” a „zamknij” (kwadrat; zmaksymalizowane —
dwa kwadraty „przywróć”) albo podwójne kliknięcie paska (< 0,45 s): okno na cały **obszar roboczy** — ekran bez górnego
i dolnego paska, liczony z paneli powłoki, które ekran składa (T13: górny pasek — panel na całą szerokość od góry; dolny —
panel, który sięga dołu ekranu; nie z liczb od gui; od 0.11.0 krok 1 do **widocznej** krawędzi dolnego paska — powierzchnia
panelu ma margines cienia `K1_PANEL_MARGINES` = 16 px, wspólny z gui; szerokość co do piksela, np. 1366), bez obwódki, pasek tytułu tuż pod górnym paskiem; nie da się go
przesunąć ani ciągnąć za brzegi. „Przywróć” i podwójne kliknięcie — poprzedni prostokąt. Zmiana obszaru roboczego
(rozdzielczość, paski) — zmaksymalizowane okna idą za nią. **Pamięć:** rozmiar i maksymalizacja każdej sesji w skarbcu
(obiekt ekranu `K1_OB_OKNA_STREF`, „K1O1”, wiersz na strefę, każdy sprawdzany przy odczycie); nowe okno strefy dostaje
rozmiar swojej sesji (log `okno N: rozmiar sesji W×H`). Log: `okno N: nowy rozmiar W×H[ (zmaksymalizowane)]`,
`obszar roboczy X,Y W×H`, `okno N: powierzchnia W×H w X,Y` (zamiana pokazana).

**Pełny ekran okna strefy (0.11.0 krok 1, D1 właściciela: szkic A — `pelny.c`).** Przeglądarka w strefie prosi o pełny
ekran (F11, film „na cały ekran”): kompozytor strefy → `gosc` → VMM (linia „pelny 1” kanału sterującego) → ekran
(`PELNY_EKRAN` a = 1). Ekran zgadza się tylko dla okna z fokusem, niezminimalizowanego, i tylko w `GEST_S` = 10 s po
**geście użytkownika** w tym oknie (wciśnięcie przycisku albo klawisz, które ekran temu oknu dał — `pelny_gest`); gest się
zużywa przy wejściu, więc strefa sama, bez nowego gestu, nie wróci do pełnego ekranu, gdy K1 go zakończył (odmowa: log
`pełny ekran: okno N — odmowa (bez gestu użytkownika)`, strefa nie dostaje odpowiedzi). Po zgodzie: okno w (0, 0),
`ROZMIAR` z `c` = `K1_ROZMIAR_PELNY` i rozmiarem ekranu, okno w warstwie **nad panelami** (`scena.c`: górny i dolny pasek
schowane pod nim), bez ramki K1, cienia i chwytów; ekran przyjmuje wtedy zamianę powierzchni na cały ekran (zwykle okno
najwyżej do górnego paska). Co rysuje K1 nad nim (strefa tego nie zasłoni — rysowane w scenie po oknie; warstwa karty
wyłączona, póki to widać): **pigułka** u góry na środku przez `PIGULKA_S` = 4 s — kropka w kolorze sesji, „<tytuł> · <sesja>
· pełny ekran: Esc, aby wyjść”, obwódka 2 px w kolorze sesji; **pasek K1** (40 px, tło okna, linia w kolorze sesji, przycisk
„Zakończ pełny ekran”), gdy kursor dotknie górnej krawędzi (chowa się, gdy zejdzie niżej niż 64 px); kursor na nim nic nie
daje strefie. **Koniec:** ten przycisk, strefa sama (Esc w przeglądarce — `PELNY_EKRAN` a = 0), Alt+Tab i klawisz Start
(przełącznik i menu to panele — pod pełnym ekranem byłyby niewidoczne), fokus w innym oknie, panel powłoki pokazany
(`POKAZ` — np. „Mało pamięci”), minimalizacja, zamknięcie okna. Potem okno wraca do prostokąta sprzed (zwykłe albo
zmaksymalizowane); pełny ekran nigdy nie trafia do skarbca. Zmiana rozdzielczości w pełnym ekranie — nowy `ROZMIAR`
ekranu. Log: `pełny ekran: okno N — wejście (W×H)`, `… — koniec (powód)`, `pigułka schowana po S s`, `pasek K1 (zakończ
X,Y)` (testy klikają według niego).
Od 0.10.0 krok 5 nazwa sesji na pasku to nazwa od nadzorcy (`STREFA_NAZWA` — użytkownik może ją zmienić; do pierwszej
wiadomości — z `K1_STREFY`), a „· zapomina po zamknięciu” i jasny pasek — według tego, czy strefa dostała dysk, gdy
ruszyła (`STREFY_STAN` a w chwili `KLIENT`; zmiana w Ustawieniach działa od następnego otwarcia, więc otwarte okno jej nie
pokazuje). Od 0.10.9 krok 6 (D7 właściciela: szkic „układ scalony”) tuż za nazwą sesji mały znak — ikona `K1_IK_KARTA`,
16 px, w kolorze drugiego tekstu — póki strefa działa ze swoim pośrednikiem grafiki (`STREFY_GRAFIKA` b: szybka grafika,
słabsza izolacja — ADR 0020 p. 3); gdy pośrednik się skończy (zawieszenie karty), znak znika.

Kanał sterujący (fd 3) od nadzorcy: `KLIENT` (rola `STREFA`: `b` = numer strefy); od 0.10.0 krok 5 także `STREFY_STAN` i
`STREFA_NAZWA` (bez deskryptora; sesje — `komponenty/strefa.md`), od 0.10.9 krok 6 `STREFY_GRAFIKA`; od 0.10.7 krok 6 `WYLACZANIE` (a = 0 wyłączanie,
1 ponowne uruchamianie; reszta pól 0) — niżej „Ekran wyłączania”. Złe pole, zła rola albo obcy UID → komunikat odrzucony
z wpisem w logu (≤ 8 na klienta), po 50 — klient rozłączony.

**Przeciąganie plików między oknami (0.11.0 krok 4, ADR 0052 T6, D6 właściciela: A — `przeciaganie.c`).** Ekran wie, gdzie są
okna, więc prowadzi przeciąganie, które wychodzi poza okno, w którym się zaczęło. Źródło (Pliki albo pulpit) mówi, co
przeciąga (`PRZECIAGANIE` — rodzaj, liczba, foldery, pierwszy element; nigdy nazwy). Gdy kursor jest nad czymś innym,
źródło słyszy `PRZECIAGANIE_GDZIE` 1 (chowa kartę, ikona pulpitu wraca na swoje miejsce), a cel — `UPUSZCZANIE_CO`
i `UPUSZCZANIE`. **Nad Plikami** (z pulpitu): Pliki rysują kartę i znak i same to robią (widzą każdy plik). **Nad pulpitem**
(z Plików): pulpit rysuje znak i obwódkę celu, a po puszczeniu mówi, gdzie (`PRZECIAGANIE_CEL`: Pulpit, folder na pulpicie,
Kosz) — ekran przekazuje to Plikom (`PRZECIAGANIE_KONIEC`), które przenoszą albo kopiują (pulpit widzi tylko Pulpit). **Nad
oknem sesji przeglądarki:** znak przy kursorze rysuje ekran — z tabeli przeciągania (`K1_PC_STREFA`: „Kopia do sesji
„Ogólna””, albo dlaczego nie: foldery, Kosz, pendrive, więcej niż 8 plików); warstwa karty jest wtedy wyłączona, strefa
nic nie dostaje. Po puszczeniu VMM dostaje `UPUSC_STREFA` (punkt w obrazie, liczba plików), a źródło `PRZECIAGANIE_KONIEC`
z numerem strefy — kopie idą drogą z kroku 3 (`DOK_DO_STREFY` z trybem „upuść”). Ctrl przy puszczeniu: kopia zamiast
przeniesienia (D6) — klawisze idą w każdym komunikacie. Źródło i cel nigdy nie rozmawiają ze sobą. Log: `przeciąganie między
oknami: N elementów (rodzaj R)`, `upuszczono na pulpit (folder|Kosz)`, `upuszczono na Pliki`, `N plików do strefy S (okno W)`.

**Schowek (0.11.0 krok 5, ADR 0052 T8, D5 właściciela: A — `schowek.c`).** Jeden tekst (UTF-8, do 1 MiB), tylko w pamięci
ekranu, czyszczony przy wyłączaniu; w logu tylko liczba bajtów i skąd. Ekran widzi klawisze i wie, które okno ma fokus,
więc kopię (`SCHOWEK_USTAW`) bierze tylko od klienta okna z fokusem i tylko tuż po geście użytkownika w tym oknie
(aplikacja 2 s, strefa 10 s — przycisk „Kopiuj” na stronie po kliknięciu). Aplikacja prosi o tekst `SCHOWEK_DAJ`
(Ctrl+V — robi to szkielet aplikacji). **Strefa nigdy nie pyta:** gdy Ctrl+V idzie do okna sesji, ekran najpierw daje VMM-owi
tekst (`SCHOWEK`), potem klawisz — VMM wstrzymuje klawisze, aż gość potwierdzi, że tekst jest jego zaznaczeniem (najwyżej
0,5 s). Strona nie przeczyta schowka sama. Tekst z innej sesji — na ramce na 3 s „Wklejono z: Bank”. Log: `schowek:
skopiowano N B (z K1 0 | ze strefy S)`, `schowek: Ctrl+V w oknie strefy S — tekst dany (N B, …)`, `schowek: wklejone w oknie W`.

## Ctrl+Alt+Del i ekran zabezpieczeń (0.13.0 krok 6, ADR 0061, decyzja właściciela D8: A)
Delete (i kropkę klawiatury numerycznej) z wciśniętymi Ctrl i Alt rozpoznaje ekran (`zabezpieczenia.c`, przed resztą
`klawisz()` w `wskaznik.c`): nie dostaje go żadne okno — ani aplikacja, ani strefa — tylko powłoka (`KLAWISZ` z Delete do
jej ekranu zabezpieczeń albo pierwszej powierzchni); puszczenie tego Delete nie idzie nigdzie. Pełny ekran strefy się
kończy; strefa z fokusem dostaje puszczenie Ctrl i Alt. Na ekranie hasła przy starcie skrót jest zwykłym klawiszem.
Powłoka pokazuje wtedy powierzchnię `K1_POW_ZABEZPIECZENIA` (tylko powłoka, jedna, poza limitem pamięci klienta): ekran
składa ją nad wszystkim (warstwa 4 — nad pełnym ekranem strefy), resztę ekranu przyciemnia (`zab_zaslona`: ćwierć obrazu
na `#1C1714`), okno strefy nie idzie wtedy na warstwę karty (`warstwa.c`). Póki jest widoczna, dostaje każdy klawisz
(także modyfikatory nie idą do strefy) i każde kliknięcie (`pod_kursorem`), Alt+Tab nie przełącza. Log: „Ctrl+Alt+Del: do
powłoki — ekran zabezpieczeń[ (strefa z fokusem: puszczone Ctrl i Alt)]”.

## Pigułka głośności (0.12.0 krok 2, decyzja właściciela D4: A)
Klawisze głośności (`KEY_VOLUMEUP`, `KEY_VOLUMEDOWN`, `KEY_MUTE`) i klawisz mikrofonu (`KEY_MICMUTE`, X230 — bez skutku,
póki K1 nie ma mikrofonu) ekran daje **tylko powłoce** (`wskaznik.c`, jak Alt+Tab) — nigdy oknu z fokusem, także oknu
strefy. Powłoka zmienia głośność K1 (usługa dźwięku) i wysyła `GLOSNOSC_POKAZ`; ekran rysuje wtedy na 1,5 s pigułkę
300 × 44 u góry, na środku (16 px pod górnym paskiem; przy pełnym ekranie — 16 px od krawędzi): głośnik stanu, suwak
(`k1_suwak`, ten sam co w okienku „Dźwięk”), procenty; bez karty — „Brak karty dźwiękowej”. Rysuje ją ekran, nie gui: jest
nad każdym oknem i panelem, także nad pełnym ekranem przeglądarki (pod nim są panele gui), i strefa jej nie zasłoni (przy
pigułce okno strefy nie idzie na warstwę karty — `warstwa.c`). Opis ekranu: panel „Pigułka głośności” z elementem
„Głośność” (`K1_EL_ZMIENNE`, wybrany = wyciszone). Log: „pigułka głośności: P%[, wyciszone | (brak karty)]”.

## Głośnik na pasku sesji (0.12.0 krok 3, decyzja właściciela D6: A)
Póki sesja przeglądarki gra (powłoka mówi `STREFA_DZWIEK` — wie od usługi dźwięku, nigdy od strefy), na pasku jej okna
zaraz po nazwie sesji (i znaku szybkiej grafiki) stoi mały głośnik 30 × 24; wyciszona — głośnik z × na akcencie
(`strefy_dzwiek.c`). Kliknięcie → `STREFA_WYCISZ` do powłoki: sesję wycisza usługa dźwięku, nie strefa. Pasek rysuje
ekran poza obrazem strefy — strona go nie zasłoni ani nie podrobi (RYZYKA R26, R108). Opis ekranu: przycisk „Wycisz
sesję” okna strefy (wybrany = wyciszona). Log: „strefa N: gra / gra, wyciszona / nie gra”, „strefa N: głośnik na pasku —
wycisz / znów gra”.

## Ekran wyłączania (0.10.7 krok 6, T7; decyzja właściciela D7: szkic B „Kropki”)
Nadzorca, gdy przyjmie `WYLACZ` (wyłączanie albo ponowne uruchamianie; zablokowanie nie), wysyła ekranowi `WYLACZANIE`
na kanale sterującym — tylko on może (sprawdzane pole po polu, kierunek „sterowanie”, UID 0; od klienta ekranu odrzucone:
test w systemie „NEGATYWNY 0.10.7”). Od tej chwili do końca `wylaczanie.c` rysuje nad wszystkim — nad oknami, także oknem
strefy, i bez kursora — `k1_wylaczanie` (`src/przybornik/k1_wylaczanie.c`): tło okna K1 na cały ekran, trzy kropki w
kolorze akcentu po kolei rozjaśniane (obieg 1,5 s, 15 klatek na sekundę z zegara ekranu) i „Wyłączanie” albo „Ponowne
uruchamianie” (Plex Serif 24). Wejście nie trafia już do okien. Log „ekran wyłączania: …”. W opisie ekranu dla testów —
panel o tej nazwie; przy kliencie testowym kropki stoją (T8). Nadzorca oddaje sprawę do init dopiero, gdy strefy się
zamkną i ekran był widoczny co najmniej jeden obieg kropek (1,5 s; 0.10.9 — zgłoszenie właściciela z 2026-11-19: na X230
bez otwartej strefy K1 wyłączał się, zanim ekran go narysował; `test-m4`). Ekran kończy się razem z nadzorcą (init); ostatnią klatkę
do wyłączenia zasilania zostawia albo czyści sterownik grafiki (do sprawdzenia na sprzęcie). Wygląd pilnuje
`test-wzorce` na hoście (`podglad_wylaczania`: ta sama funkcja, wzorce `wylaczanie-jasny`, `wylaczanie-ciemny`).

## Doręczanie (0.8.5 krok 1)
Ekran nigdy nie czeka na klienta. Gdy kanał aplikacji jest pełny (aplikacja akurat długo liczy), to, co ekran
do niej wysyła, czeka w ekranie (`zalegle.c`, do 64 komunikatów na klienta) i idzie, gdy kanał znów przyjmuje
(`poll` z `POLLOUT` tylko dla klientów z zaległymi), w tej samej kolejności. Zasady:
- `RAMKA` czeka najwyżej raz na powierzchnię (druga mówiłaby to samo) i **nigdy nie przepada** — bez niej okno
  już nigdy by się nie odświeżyło;
- ruch wskaźnika zastępuje ruch tuż przed nim (liczy się ostatnie położenie), `USTAWIENIE` — wcześniejsze o tym
  samym kluczu; `PRZEWIN` tuż po `PRZEWIN` tej samej powierzchni i z tego samego źródła **dodaje się** (0.10.0 krok 4:
  ostatnie położenie, suma do `K1_PRZEWIN_MAKS`), a przy pełnej kolejce przepada jak wciśnięcie;
- wciśnięcia i puszczenia przycisków, klawisze, `FOKUS`, `ZAMKNIJ` — w kolejności nadania;
- klient, który w ogóle nie odbiera (kolejka pełna): najpierw przepadają najstarsze ruchy i powtórzenia klawisza,
  potem wciśnięcia; `PUSZCZENIE` i puszczony klawisz — dopiero gdy nic innego nie zostało. Log: „klient … nie
  odbiera: porzucono n ruchów i klawiszy, m innych” (liczby, bez kodów klawiszy, ≤ 2 wiersze/s).

Ile mieści sam kanał: kanały to pary gniazd `SOCK_SEQPACKET` (`socketpair`). Limit
`net.unix.max_dgram_qlen` (domyślnie 10) **nie dotyczy** pary połączonych gniazd — `unix_dgram_sendmsg`
w `net/unix/af_unix.c` (jądro 6.18) sprawdza pełną kolejkę odbiorcy tylko wtedy, gdy odbiorca nie jest połączony
z nadawcą (`unix_peer(other) != sk`). Granicą jest bufor nadawcy (`SO_SNDBUF`, domyślnie `net.core.wmem_default`
= 212 992 B): komunikat 32 B zajmuje w nim ok. 760 B, więc kanał mieści ok. 278 komunikatów (zmierzone:
`socketpair` + `send(MSG_DONTWAIT)` do `EAGAIN`). Przy myszy 125–1000 zdarzeń/s to 0,3–2 s ruchów — dopiero
potem działa kolejka w ekranie.

## Zachowanie
- Scena składana tylko w uszkodzonych prostokątach (do 16, potem prostokąt obejmujący), osobno
  dla każdego z dwóch buforów; kursor programowy odtwarzany ze sceny.
- Nowe okno: na środku z przesunięciem kaskadowym. Fokus (ochrona klawiatury, ADR 0012) dostaje,
  gdy żadne okno nie ma fokusu, okno z fokusem należy do tej samej aplikacji albo to pierwsze okno
  aplikacji połączonej ≤ 10 s temu; inaczej otwiera się tuż pod oknem z fokusem. Kliknięcie okna — fokus
  i wierzch; okno nie wchodzi pod górny pasek, pasek tytułu zostaje w zasięgu.
- Prędkość myszy: 0,5× / 1× / 2× (ustawienie). Bez okna z fokusem strzałki przesuwają kursor o 20 px.
- Zmiana tapety: ponowne liczenie (ok. 2 s w emulacji TCG) i przerysowanie.
- **Obraz bez kopii** (0.10.11 krok 2, ADR 0051; `strefa_bufor.c`, `warstwa.c`): okno strefy z szybką grafiką pokazuje bufor
  karty przeglądarki (`STREFA_BUFOR`, `STREFA_OBRAZ`). Gdy okno widać w całości (nic nad obrazem: okno z cieniem, panel,
  menu, obrys zmiany rozmiaru, „Wyłączanie”; obraz cały na ekranie) i karta ma nakładkę (i915) — **warstwa karty**: nakładka
  pokazuje bufor bez kopii, kursor na płaszczyźnie kursora, jedno zatwierdzenie atomowe na klatkę (bufor pulpitu, nakładka,
  kursor) ze zdarzeniem wymiany; nowy bufor najpierw próbą `TEST_ONLY`, odmowa = składanie. Inaczej (D4 „A”) ekran składa
  okno z bufora sam, nieprzezroczyście, z `DMA_BUF_IOCTL_SYNC` przed odczytem i po nim. Log: „warstwa karty: płaszczyzna …”
  przy starcie (albo „niedostępna”), „okno N: obraz strefy z bufora karty …”, „okno N na warstwie karty …”.
- Pomiar (0.10.11 krok 1, `pomiar.c`): czas składania sceny, zapisu do bufora karty i wymiany na karcie (od `PAGE_FLIP`
  do `FLIP_COMPLETE`); co 10 s, gdy jest otwarte okno strefy: `pomiar ekranu (10 s, średnio/95%): klatki N, składanie
  A/B ms, zapis do karty …, wymiana na karcie …` (same liczby).

## Zależności
musl (statycznie, w tym libm), jądro: DRM + `bochs` (QEMU), `i915` (karty Intela — od 0.10.9 krok 2, ADR 0049; page flip
jak bochs), `vmwgfx` albo `simpledrm` (firmware). Bez libdrm.

Od 0.10.9 krok 2 (S3) ekran mówi nadzorcy na kanale sterującym (fd 3), że jego obraz jest na ekranie: `EKRAN_OBRAZ`
(a = 1) raz po każdym starcie — po pierwszym zakończonym page flipie (silnik obrazu pokazuje nasze klatki), przy
simpledrm po ustawieniu trybu. Nadzorca dopiero od tej chwili liczy 15 s do potwierdzenia nowej wersji (A/B). Log
„obraz na ekranie — zgłoszony nadzorcy”.

## Wywołania systemowe
`ioctl` (DRM: GET_CAP, MODE_GETRESOURCES/GETCONNECTOR/GETENCODER, CREATE_DUMB, ADDFB, MAP_DUMB,
SETCRTC, PAGE_FLIP, DIRTYFB, RMFB, DESTROY_DUMB, VERSION; od 0.10.11 krok 2: SET_CLIENT_CAP, GETPLANERESOURCES, GETPLANE,
OBJ_GETPROPERTIES, GETPROPERTY, PRIME_FD_TO_HANDLE, ADDFB2, ATOMIC, GEM_CLOSE — i jako jedyne spoza DRM `DMA_BUF_IOCTL_SYNC`;
`lseek` — rozmiar bufora strefy), `memfd_create` (zapis ustawień, 0.5.6), `mmap` (bufory, powierzchnie — bez `PROT_EXEC`), `munmap`, `fcntl`
(`F_GET_SEALS`), `fstat`, `recvmsg`/`sendmsg`, `read`, `poll`, `close`, `clock_gettime`, `write` (log).

## Obsługa błędów
Brak karty/monitora/trybu → `k1_koniec()` (nadzorca uruchomi ponownie). Zamknięty kanał klienta →
usunięcie jego powierzchni (powłoka dostaje „okno zamknięte”). Zamknięty kanał sterujący → koniec.

## Powierzchnia ataku
Parsery komunikatów 16 B i 32 B (testy + fuzzing), zdarzenia DRM (sprawdzana długość), deskryptory
od klientów (tylko memfd z pieczęciami), piksele klientów (kopiowanie z przycinaniem). Sterownik DRM
w jądrze (ioctl).

## Testy
| Test | Rodzaj |
|---|---|
| `test-m4.sh` (początek): tryb, pierwsza klatka, mysz, strzałki, przycisk — log + zrzuty | funkcjonalny |
| `test-m4.sh`, `test-strefa.sh` (0.13.0 krok 6): Ctrl+Alt+Del — ekran zabezpieczeń nad oknem Ustawień i nad strefą (zrzut przyciemnienia), okno nie dostaje Delete, strefa tylko puszczenie Ctrl i Alt, kliknięcie nie trafia do strefy; Esc — klawisze znów do strefy | funkcjonalny, negatywny |
| `test-strefa.sh` (0.10.11 krok 2): okno strefy z bufora karty (strefa próbna, silnik testowy pośrednika) — kolor na zrzucie, powrót do własnych pikseli; na hoście `test_komunikat` (`STREFA_BUFOR`, `STREFA_OBRAZ` — każde pole poza zakresem) | funkcjonalny, negatywny (IPC) |
| `test-m4.sh`: okna, przeciąganie, fokus, minimalizacja, motyw, tapeta — log + zrzuty | funkcjonalny |
| w systemie: drugi proces nie przejmie ekranu (`SET_MASTER` → EBUSY) | negatywny |
| w systemie: złośliwe zdarzenia wejścia i obcy nadawca (`odrzucone 5`) | negatywny (IPC) |
| w systemie: powierzchnia bez memfd / bez pieczęci / zły rozmiar, panel od aplikacji, ekran zabezpieczeń od aplikacji (0.13.0 krok 6), ustawienia od aplikacji, sterowanie cudzym oknem, okno od obcego UID — odrzucone; poprawne przyjęte (RAMKA) | negatywny (IPC) |
| w systemie: klatka, próba w klatce; awaria ekranu → wraca z klientami | negatywny |
| w systemie (M4.1): późniejsze okno innej aplikacji nie dostaje fokusu; trzecie pełnoekranowe okno klienta odrzucone (limit pamięci) | negatywny |
| host `test_zalegle`: RAMKA raz na powierzchnię, scalanie ruchów i ustawień, kolejność ważnych, przepełnienie (ruchy i powtórzenia najpierw, RAMKA nigdy) | jednostkowy (ASan/UBSan) |
| w systemie (0.8.5): klient, który nie odbiera, gdy mysz wysyła 1500 ruchów, dostaje potem RAMKA i PUSZCZENIE; inny klient w tym czasie rysuje normalnie | negatywny |
| `test-m4.sh` (M4.1): zmiana rozdzielczości z Ustawień — nowy tryb, zrzut w nowym rozmiarze, panele na nowych miejscach | funkcjonalny |
| host: `test_komunikat`, `fuzz_komunikat`, `test_zdarzenie`, `fuzz_zdarzenie`, renderer z ASan/UBSan | jednostkowy / fuzzing |
| `test-pobrane.sh` (0.11.0 krok 5): schowek — Notatki → Ctrl+V w oknie strefy (dokładnie ten tekst, klawisz po potwierdzeniu gościa), kopia strefy → Notatki; NEGATYWNE: bez Ctrl+V strefa nic nie dostaje, kopia bez gestu odrzucona; host `test_komunikat` (`SCHOWEK*`), `test_schowek`, `fuzz_schowek` | funkcjonalny, negatywny (IPC) |
| `test-pliki.sh` (0.11.0 krok 4): folder z Plików na pulpit i z pulpitu na listę Plików (przeniesiony), Ctrl + folder na pulpit — odmowa w Plikach; `test-pobrane.sh`: plik z pulpitu na okno strefy próbnej — znak K1, nic do strefy przed puszczeniem, potem `UPUSC_STREFA` w punkcie i kopia bajt w bajt; host `test_komunikat` (`przeciaganie`: każde pole poza zakresem, zły kierunek), `test_przeciaganie` (`K1_PC_STREFA`, Ctrl) | funkcjonalny, negatywny (IPC) |
