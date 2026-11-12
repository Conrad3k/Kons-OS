# 0037. Aplikacja Podgląd (0.8.2, krok 4): zdjęcie w osobnym oknie, zdjęcia jednego folderu

- Status: Przyjęty
- Data: 2026-11-12
- Dotyczy: ADR 0030 (prawa w tabeli, dostęp po geście, okno wyboru pliku, domyślne aplikacje), ADR 0032 (dekoder
  obrazów, tapeta ze zdjęcia), ADR 0027 p. 2 (zgoda = gest)

## Kontekst
Do 0.8.2 zdjęcie widać tylko jako miniaturę w panelu po prawej w Plikach (200 × 150). Właściciel chce oglądać
zdjęcia jak w Windows: podwójne kliknięcie otwiera zdjęcie w osobnym oknie, strzałki pokazują poprzednie
i następne z tego samego folderu. ADR 0030 daje aplikacji **jeden plik po geście** — przy takim prawie każda
strzałka wymagałaby nowego gestu w Plikach. Potrzebny jest węższy niż „wszystko”, a szerszy niż „jeden plik”
zakres, z jasną granicą.

## Decyzja
1. **Nowa aplikacja „Podgląd”** (`podglad`, numer 9, profil 17, UID 208, wiersz w `K1_APLIKACJE`), domyślna dla
   wszystkich typów rodziny „obrazy” (PNG, JPEG, GIF, BMP, WebP — wiersze w `K1_OTWIERANIE`). Panel po prawej
   w Plikach nazywa się od teraz **szczegóły**, a zdjęcie w nim — **miniatura** (decyzja właściciela D1: jedno
   słowo = jedno znaczenie).

2. **Zakres „zdjęcia z folderów otwartych w Podglądzie”** (`Z_OBRAZY_FOLDERU` w tabeli praw skarbca,
   `dok_kanal.c`). Gdy użytkownik otwiera zdjęcie w Podglądzie (podwójne kliknięcie w Plikach albo na pulpicie
   — `DOK_OTWORZ`; albo w oknie wyboru pliku — `DOK_WYBRANO`), skarbiec zapisuje przy dostępie także **folder,
   w którym plik wtedy leżał**. Podgląd widzi i czyta **tylko pliki rodziny „obrazy”** w tych folderach — nie
   tekst obok, nie inne foldery, nie Kosz, nie podfoldery. Dostęp trwa jak w ADR 0030 p. 5: do zamknięcia
   aplikacji, tylko w pamięci skarbca.

3. **Prawa Podglądu:** lista i treść (w zakresie p. 2), **do Kosza** (decyzja właściciela D2: Delete wyrzuca
   oglądane zdjęcie bez pytania — Kosz trzyma je 30 dni), prośba o okno wyboru pliku. Bez zapisu, zmiany nazwy,
   przenoszenia, usuwania na zawsze, opróżniania Kosza i otwierania w innych aplikacjach.

4. **Okno wyboru pliku z rodziną typów.** `DOK_WYBIERZ` ma pole „rodzina” (b; razem z typem — nigdy oba),
   `DOK_PROSBA` przekazuje je oknu wyboru (c). „Otwórz…” w Podglądzie prosi o rodzinę „obrazy”: okno pokazuje
   tylko zdjęcia, skarbiec odrzuca wybór innego pliku.

5. **Dekoder dla Podglądu** — ten sam, jednorazowy, w klatce (ADR 0032): wiersz Podglądu w tabeli klientów
   nadzorcy (`dekoder.c`), prośby na kanale fd 6 Podglądu. Kod po stronie aplikacji (prośba, kawałki od skarbca
   przekazywane bez czytania, odpowiedź) jest **jeden dla Plików i Podglądu**: `k1_dekodowanie` w przyborniku.
   Dekoder podaje teraz także wymiary samego zdjęcia (`DEK_PIKSELE` c, d — po obrocie z EXIF), które Podgląd
   pokazuje w pasku. Podgląd prosi o obraz najwyżej 928 × 928 i sam obraca widok (R) i dopasowuje go do okna
   (`k1_skaluj`, nigdy powiększenie > 100 %); pliku nie zmienia.

6. **„Ustaw jako tapetę” z Podglądu** — ekran przyjmuje zdjęcie na tapetę od aplikacji z **tabeli nadawców**
   (`zdjecie.c`: Pliki, Podgląd); od każdej innej — odmowa jak dotąd.

7. **Domyślna aplikacja dla zdjęć po aktualizacji.** Ustawienia ekranu zapisane przed tą wersją (K1U1–K1U3)
   mają dla zdjęć zapisane Pliki — wtedy jedyną możliwość, nie wybór użytkownika. Ekran czyta je bez tej wartości
   (zdjęcia dostają Podgląd); nowy zapis to K1U4.

8. **Klatka** — taka sama jak każdej aplikacji (32 MiB pamięci, `RLIMIT_FSIZE` 16 MiB): okno ma stały rozmiar
   (960 × 640; K1 nie ma okien na pełny ekran), obraz od dekodera ≤ 3,3 MiB. Plan zakładał 96 MiB dla okna na cały
   ekran 4096 × 2160 — niepotrzebne, dopóki okna nie zmieniają rozmiaru.

## Konsekwencje
- Przejęty Podgląd (np. błąd w kodzie, który mimo to dostałby się do procesu) widzi zdjęcia folderów, które
  użytkownik w nim otworzył, i może je wyrzucić do Kosza (do przywrócenia przez 30 dni) — nic więcej: żadnego
  tekstu, innego folderu, zapisu ani sieci. Same pliki zdjęć Podgląd tylko przekazuje dekoderowi — nie dekoduje
  ich sam (RYZYKA R60).
- Nowe pola w komunikatach (`DOK_WYBIERZ` b, `DOK_PROSBA` c, `DEK_PIKSELE` c, d) — sprawdzane pole po polu,
  testy na hoście; prawa Podglądu — testy negatywne przez prawdziwy kanał (`test_dokumenty`).
- Nowa aplikacja na liście: usługa, klatka, menu Start, obraz systemu, próba w klatce i spis w testach wynikają
  z listy; kanały — wiersz w `kanaly[]`; dekoder — wiersz w tabeli klientów.
- Poza tym krokiem: powiększanie fragmentu, pełny ekran, pokaz slajdów, przeciąganie (krok 6).
