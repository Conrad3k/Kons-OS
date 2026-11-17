# 0045. Opis ekranu dla testów

- Status: Przyjęty; wdrożony w 0.10.2 (krok 2)
- Data: 2026-11-17
- Dotyczy: niezmienniki kanałów i klatek (SKILL §4, pkt 6–7: każdy komunikat sprawdzany pole po polu, tylko nadzorca
  daje kanały i role), niezmiennik 8 (żadnych treści w logach); plan `docs/archiwum/PLAN-0.10.2.txt`: krok 2, T3, T4, S6–S8

## Kontekst
Testy QEMU (`test-m4`, `test-strefa`…) klikają według położeń, które aplikacje i pulpit wypisują do logu („menu Start
otwarte (pierwszy wiersz x,y co n…)”), i sprawdzają kolory pikseli. Każdy nowy element to nowa linia logu i nowe wyrażenie
regularne w teście; zmiana wyglądu psuje testy, które nie dotyczą wyglądu (S6). Właściciel przyjął 2026-11-17 propozycję:
K1 sam mówi testom, co ma na ekranie — okna, panele, przyciski, wiersze, kafelki, ikony, zakładki — po nazwach.

## Decyzja

**Format** (`src/wspolne/k1_opis.h`, `k1_opis.c` — czyste, `test_opis`, `fuzz_opis`): memfd zapieczętowany jak inne
(`k1_memfd`), nagłówek 16 B („K1OP”, wersja, liczba rekordów, rozmiar) i rekordy po 64 B: rodzaj (`K1_EL_*`), stan
(wybrany, niedostępny; okno: fokus), numer okna, prostokąt (`int16`, cały w powierzchni albo na ekranie), nazwa — 52 B
UTF-8 zakończone zerem i dopełnione zerami, bez znaków sterujących, niewidocznych i zmieniających kierunek
(`k1_napis_dobry`, ta sama zasada co nazwy sesji). Odbiorca sprawdza każde pole i odrzuca całość przy jednym złym.

**Kto co mówi:**
- **Klient ekranu** (aplikacja, gui, VMM — przez przybornik, T4): jedna tabela elementów na powierzchnię
  (`src/przybornik/k1_opis_klient.c`), wypełniana tam, gdzie element jest rysowany — `k1_przycisk`, wiersze `k1_menu`,
  przyciski ramki okna, `k1_segment`; panele powłoki (pasek, menu Start, pulpit, okienka) dopisują swoje elementy tą samą
  funkcją `k1_el`. Okno aplikacji ma nazwę = tytuł (`k1_aplikacja`). Tabela idzie do ekranu z zatwierdzeniem
  powierzchni, gdy się zmieniła (`OPIS` + memfd) — **tylko gdy ekran prosi** (ustawienie `K1_UST_OPIS` = 1).
- **Ekran** (`src/ekran/opis.c`): trzyma tabele powierzchni (pamięć dopiero przy włączonym opisie) i odpowiada na
  `OPIS_PYTANIE` wszystkimi widocznymi powierzchniami od dołu: rekord okna albo panelu, potem jego elementy — we
  współrzędnych ekranu (`OPIS_EKRANU` + memfd). Nazwę okna bez własnej daje tytuł aplikacji albo strefy.
- **Kto pyta (T3):** tylko klient roli **`TEST`** (`K1_ROLA_TEST`). Tę rolę daje nadzorca wyłącznie przez `TEST_POLACZ`
  — kanał programu testowego, który istnieje tylko w obrazie testowym. Ekran włącza `K1_UST_OPIS`, gdy taki klient się
  połączy, i wyłącza, gdy odejdzie; ustawienia tego nikt nie zmienia (`USTAW` odrzucone) i nie jest zapisywane.
  Pytanie od innej roli, opis od klienta, gdy nikt nie pyta, i każdy inny komunikat od klienta `TEST` (np. powierzchnia)
  — **odrzucone** (fail closed).

**Co nie może być w opisie (niezmiennik 8):** tekst wpisany przez użytkownika, treść plików, nazwy plików i nazwy sesji.
Nazwy pochodzą tylko z K1: etykiety przycisków i wierszy, tytuły aplikacji i stref, „plik N” zamiast nazwy pliku na
pulpicie, „sesja N” zamiast nazwy sesji (menu paska i okienko sesji podają kopię menu z neutralnymi nazwami —
`k1_menu_el`). Pola tekstowe — jeśli kiedyś trafią do opisu — tylko z etykietą, nigdy z zawartością.

**Testy:** program testowy w trybie opisu (dysk z numerem seryjnym `k1-test-opis`) pyta co pół sekundy i wypisuje opis do
logu, gdy się zmienił („K1-TEST: ekran n część/części: rekord | …”); `testy/qemu/k1qemu.py` składa go (`q.ekran()`,
`q.element()`, `q.czekaj_na_element()`, `q.klik_w("Menu Start", "Notatki")`). `test-m4` uruchamia pulpit na obrazie
testowym i klika po nazwach; ponowny start, hasło i ISO — dalej obraz produkcyjny.

## Dlaczego obraz produkcyjny go nie ma i jak tego pilnujemy
Kod odpowiedzi jest w ekranie i przyborniku także w obrazie produkcyjnym (jeden kod dla obu obrazów — tak jak kanał
testowy nadzorcy), ale w obrazie produkcyjnym nie ma programu testowego, więc nikt nie dostanie roli `TEST`: opis jest
wyłączony, klienci nic nie wysyłają ani nie alokują. Pilnują tego: test w systemie `t_opis` (rola aplikacji pyta —
brak odpowiedzi), `test_komunikat` (`USTAW` z `K1_UST_OPIS` odrzucone), `scripts/sprawdz-czytelnosc.py` (rola `TEST`
i `OPIS_PYTANIE` tylko w ekranie, w `src/wspolne/` i w testach — nigdy w innym programie K1) oraz istniejące sprawdzenie,
że obraz produkcyjny nie ma programu testowego.

## Konsekwencje
- Nowa aplikacja dostaje opis sama, jeśli rysuje przyciski i menu przybornikiem; własne elementy (listy, kafelki)
  zgłasza `k1_el` z nazwą K1 (przepis „Nowa aplikacja” w skillu).
- Testy klikają po nazwach; położenia z logu zostają tam, gdzie opis ich nie daje (listy plików, karty tapet, przyciski
  Kalkulatora, Notatki) — zapisane w `docs/TESTY.md`. Zrzuty wzorcowe (krok 3) wycinają okna według opisu.
- Koszt w obrazie testowym: memfd z tabelą przy każdej zmianie tabeli (nie przy każdej klatce — skrót tabeli).

## Uzupełnienie (0.10.2 krok 3): miejsca zmienne
Rodzaj rekordu 10 — `K1_EL_ZMIENNE`, **miejsce zmienne**: prostokąt, w którym K1 rysuje coś, co zmienia się samo (zegar
i stan na górnym pasku, liczby i wykresy Zasobów, numer wersji, czas, miejsce na pliki, karty sieciowe). Zrzuty wzorcowe
(`testy/qemu/k1wzorce.py`) go nie porównują. Ten sam format (wersja 1 — obie strony budowane razem, opis istnieje tylko
w obrazie testowym), te same sprawdzenia (`K1_EL_MAKS` = 10; `test_opis` odrzuca 11). Nazwa jak każda inna — tylko słowa
K1 („Zegar”, „Wersja systemu”), nigdy wartość, która się zmienia. Górny pasek jest od tego kroku panelem opisu („Pasek
górny”: „Dźwięk”, miejsce zmienne „Stan i zegar”).
