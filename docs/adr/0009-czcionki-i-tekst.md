# 0009. Czcionki i rysowanie tekstu

- Status: Przyjęty
- Data: 2026-11-06
- Dotyczy: SPEC §7 (renderer własny, programowy), §2.7 (zależności), M4

## Kontekst
Pulpit i aplikacje potrzebują tekstu z polskimi znakami. Parsowanie plików TrueType/OpenType
w działającym systemie to złożony parser binarnych danych (historycznie źródło luk). Styl
(docs/WYGLAD.md) wymaga kroju bezszeryfowego do interfejsu i szeryfowego do nagłówków i zegara.
Ubuntu 24.04 ma pakiet `fonts-ibm-plex` (6.1.1, licencja SIL OFL 1.1) — rodzina IBM Plex Sans
i Plex Serif — oraz FreeType 2.13 (`libfreetype-dev`).

## Decyzja
- **Kroje:** IBM Plex Sans (Regular, Medium — interfejs) i IBM Plex Serif (Regular — nagłówki,
  Light — duże cyfry zegara). Jedna nadrodzina, spokojna i rzeczowa, z ciepłym szeryfem; licencja OFL
  pozwala osadzać.
- **Rasteryzacja w czasie budowy** na hoście: `narzedzia/czcionki.c` (FreeType, hinting „light”,
  8-bitowe pokrycie) generuje `build/gen/k1_czcionki_dane.c`: 6 stylów (13, 13 Medium, 15,
  15 Medium, 24 Serif, 64 Serif Light — same cyfry), ASCII + polskie litery + kilka znaków
  typograficznych, ~71 KB bitmap.
- **W systemie** tylko tablice: wyszukiwanie binarne glifu, mieszanie pokrycia z kolorem
  (`src/przybornik/k1_rysuj.c`), UTF-8 dekodowany ze sprawdzaniem (błędne sekwencje → „?”).
  Bez kerningu i łamania tekstu poza prostym zawijaniem w Notatkach.
- Własny kod zamiast LVGL (decyzja odłożona w ADR 0006).

## Konsekwencje
- W bazie zaufania nie ma parsera czcionek; dane czcionek są stałe i wbudowane w programy
  (gui i aplikacje, nie `ekran`).
- Budowa potrzebuje `fonts-ibm-plex`, `libfreetype-dev`, `pkg-config` (tylko host). Wynik zależy od
  wersji FreeType i plików czcionek — zapisane w `build/wynik/srodowisko.txt`.
- Znaki spoza zestawu rysują się jako „?”. Inne języki/rozmiary = zmiana listy w generatorze.

## Rozważane alternatywy
- stb_truetype w systemie — parser plików czcionek w działających procesach.
- Czcionka bitmapowa rysowana ręcznie — słaba jakość, brak polskich znaków w gotowych zestawach
  o odpowiednim wyglądzie.
- Inter / Noto — dobre, ale Plex ma spójny krój szeryfowy do nagłówków i zegara.
