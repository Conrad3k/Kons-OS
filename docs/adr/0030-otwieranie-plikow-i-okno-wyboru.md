# 0030. Pliki 0.8.0, krok 2: Notatki na plikach, „Otwórz w…”, domyślne aplikacje, okno wyboru pliku

- Status: Przyjęty
- Data: 2026-11-10
- Dotyczy: ADR 0029 p. 5, 7 i 8 (prawa, otwieranie z pulpitu, krok 2), ADR 0027 p. 2, 4, 6 (zgoda = gest,
  okno wyboru pliku, domyślne aplikacje)

## Kontekst
Po kroku 1 tylko Pliki i pulpit widzą pliki użytkownika; Notatki trzymają notatki we własnych obiektach
skarbca (32 sztuki, obiekty 1–33), a podwójne kliknięcie pliku zawsze otwiera Pliki. ADR 0027 wymaga, żeby
aplikacja dostawała **jeden plik po geście użytkownika w interfejsie K1** — nigdy listę plików ani ścieżkę.

## Decyzja
1. **Notatki to pliki.** Każda notatka jest plikiem tekstowym w folderze **Dokumenty › Notatki** (skarbiec
   tworzy folder, gdy go nie ma). Nazwa pliku idzie za pierwszym wierszem notatki („Lista zakupów”), dopóki
   użytkownik nie nada jej sam w Plikach. „Usuń notatkę” przenosi plik do Kosza (można go przywrócić).
   Przy pierwszym uruchomieniu nowej wersji Notatki przenoszą stare notatki do plików, a stare obiekty
   usuwają dopiero po udanym zapisie każdej z nich.

2. **Prawa Notatek (tabela w skarbcu):** lista, treść, zapis i zmiana nazwy — tylko plików, które Notatki
   same utworzyły, i plików, które użytkownik im dał (p. 3, 4); nowy plik — tylko we własnym folderze;
   do Kosza — tylko własne. Folderów ani innych plików Notatki nie widzą.

3. **„Otwórz w…” i domyślne aplikacje.** Które aplikacje otwierają który typ — **jedna tabela**
   `K1_OTWIERANIE` (`k1_dokument.h`); Pliki pokazują każdy plik (podgląd). Domyślną aplikację dla typu
   wybiera się w **Ustawienia → Aplikacje** (ustawienie ekranu, jak motyw — zapisane w skarbcu); bez wyboru
   — pierwsza z tabeli, a bez żadnej — Pliki. Podwójne kliknięcie pliku (Pliki, pulpit) otwiera domyślną,
   „Otwórz w…” daje wybór. Otwarcie w aplikacji: Pliki albo pulpit wysyła `DOK_OTWORZ` do skarbca;
   skarbiec sprawdza prawa klienta (pulpit — tylko folder Pulpit) i tabelę, **daje aplikacji dostęp do tego
   jednego pliku** i prosi nadzorcę (`SKARB_URUCHOM`) o uruchomienie aplikacji z tym plikiem (jej kanał
   fd 5, `PLIK_OTWORZ`); nadzorca prosi pulpit o pokazanie jej okna (`POKAZ_APL`).

4. **Okno wyboru pliku** to osobny, mały program K1 **„Wybór pliku”** (`wybor`, UID 207, własna klatka) —
   nie część aplikacji, która prosi o plik. Aplikacja wysyła do skarbca `DOK_WYBIERZ` (z typem pliku);
   skarbiec prosi nadzorcę o okno wyboru i podaje mu prośbę (`DOK_PROSBA`: kto prosi, jaki typ). Okno widzi
   nazwy plików (bez treści), użytkownik wybiera plik albo „Anuluj”; okno wysyła `DOK_WYBRANO`, skarbiec
   sprawdza, że prośba czeka i plik pasuje, daje dostęp i odpowiada aplikacji `DOK_WYBRANY`. Jedna prośba
   naraz; zamknięte okno = „Anuluj”. „Wybór pliku” nie ma ikony ani miejsca w menu Start (kolumna listy
   aplikacji: -1).

5. **Dostęp do pliku po geście** trwa, dopóki aplikacja działa (skarbiec zapomina go, gdy zamknie się
   ostatni kanał tego właściciela; po ponownym starcie systemu też). Pamięta go tylko skarbiec, w pamięci.

6. **Zapis z powrotem w aplikacjach K1** (Notatki) — od razu, bez pytania: aplikacja K1 nie jest strefą,
   a plik dostała gestem do edycji. Pytanie „Zapisać zmiany w oryginale?” z ADR 0027 p. 5 dotyczy stref (M9).

## Konsekwencje
- Nowe komunikaty: `DOK_OTWORZ`, `DOK_WYBIERZ`, `DOK_WYBRANO` (do skarbca), `DOK_WYBRANY`, `DOK_PROSBA`
  (ze skarbca), `SKARB_URUCHOM` (skarbiec → nadzorca), `PLIK_OTWORZ`, `POKAZ_APL` (nadzorca → aplikacja,
  pulpit); `DOK_NAZWA` z polem „dodaj (2), gdy zajęta”. Każdy sprawdzany pole po polu, z testami na hoście
  i fuzzingiem; prawa — testy negatywne przez prawdziwy kanał.
- Notatki: najwyżej 64 notatki po 16 KB (większy plik tekstowy Notatki odrzucają z komunikatem).
- Nowa aplikacja na liście (numer 8, profil 14) — klatka, próba i spis w testach wynikają z listy.
