# ADR 0023 — porządki w kodzie przed M7 (0.7.1)

**Status:** przyjęte (2026-11-08)

## Kontekst
Przed etapem M7 przejrzeliśmy cały kod jednym pytaniem: *czy te kawałki zmieniają się razem?* Wiele
rzeczy było zapisanych w kilku miejscach (lista aplikacji w 8 plikach, odczyt partycji K1-EFI w trzech,
kolory, ikona baterii, formatowanie liczb, pętle zapisu na dysk, mechanizm testów na hoście), co przy
każdej zmianie wymagało pamiętania o wszystkich kopiach.

## Decyzja
- Jedna lista usług i aplikacji: makra `K1_SYSTEM` / `K1_APLIKACJE` w `src/wspolne/k1_uslugi.h`;
  z niej nadzorca (usługi, profile klatek), pulpit, ekran, Zasoby, budowanie obrazu i testy.
- Kanały usług zadeklarowane w tabeli `kanaly[]` nadzorcy; start i koniec usługi bez wyjątków dla
  poszczególnych usług.
- Jeden czytnik partycji K1-EFI (`k1_efi.h`), pełny zapis/odczyt (`k1_io.h`), liczby little-endian
  (`k1_bajty.h`), wersja jako tekst (`k1_wersja_tekst`).
- W przyborniku: liczby (`k1_liczba.h`), przyciski, pole tekstowe, ikona baterii; paleta K1 w
  `k1_grafika.h`.
- Wspólne kroki skryptów w `scripts/wspolne.sh` (`make_jadra`, `grub_poczatek`), jedna reguła fuzzingu,
  wspólny mechanizm testów na hoście (`testy/host/k1_test_host.h`) i w QEMU (`testy/qemu/k1qemu.py`).
- Nowa aplikacja Kalkulator jako pierwsza dodana według nowego przepisu (skill `kons-dev`).

## Konsekwencje
Typowa zmiana (nowa aplikacja, nowy kolor, format partycji) jest w jednym miejscu. Zachowanie systemu
bez zmian, potwierdzone pełnym `scripts/testy.sh`. Dalsze przeglądy (pomocnicy szukający pozostałych
powtórzeń) przerwane na prośbę właściciela — do wznowienia przy okazji.
