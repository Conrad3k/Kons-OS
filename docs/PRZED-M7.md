# Przed M7 — co zostało z porządków 0.7.1

Stan: 2026-11-09, wersja **0.7.2** (część punktów zrobiona — odhaczone niżej; reszta trafi do **0.7.3**). Pierwotnie: 2026-11-08, wersja **0.7.1** (wydana, wszystkie testy zaliczone). Porządki z ADR 0023 zostały
przerwane na prośbę właściciela (limit). Ten plik mówi, co dokończyć **przed rozpoczęciem M7 (Sieć)**,
jako jedna wersja porządkowa **0.7.2**. Zrobione punkty odhaczamy tutaj (`[x]`).

## Zasady
Wspólne zasady pracy (bezpieczeństwo, język, jak dzielimy się zadaniami, testy, scalanie): `AGENTS.md`.
Tutaj tylko: zachowanie systemu ma zostać takie samo (to porządki, nie nowe funkcje); wszystko trafia do
wersji **0.7.2**; punkt, nad którym pracujesz, oznacz `[w toku: Claude|Codex, gałąź …]`.

## 1. Testy (najważniejsze)
Test ma się nie udać przy prawdziwym błędzie — i tylko wtedy.
- [x] (0.7.2, za zgodą właściciela) `scripts/test-m1.sh` (ok. wiersza 83–86): ponowne uruchomienie
      `sprawdz-binaria.sh` / `sprawdz-konfiguracje-jadra.sh` po budowaniu — usunięte (budowanie je wymusza).
- [ ] [w toku: Claude, gałąź claude/blissful-mayer-ts7rzb] Testy w QEMU (`testy/qemu/k1qemu.py`, `scripts/test-m*.sh`): stałe `time.sleep(...)` (np. w
      `klik`, `monitor`, `przesun`) zastąpić czekaniem na wiersz w logu albo stan (`czekaj(...)`) — dziś
      mogą czasem fałszywie zawieść na wolnej maszynie.
- [ ] [w toku: Claude, gałąź claude/blissful-mayer-ts7rzb] Testy na hoście (`testy/host/test_*.c`): przejrzeć sprawdzenia, które tylko powtarzają
      implementację (porównują kod z nim samym) — przepisać na zachowanie albo usunąć.
- [x] (0.7.2) Sprawdzić, czy jakiś test „przechodzi”, gdy brakuje narzędzia (np. `grub-editenv` dla
      `test_uklad`, `sfdisk` dla `test_instalator`) — brak narzędzia ma być błędem, nie zaliczeniem.
- [x] (0.7.2) `testy/host/k1_test_host.h` nie jest zależnością reguł testów w `Makefile` (zmiana nagłówka nie
      przebudowuje testów).
- [x] (0.7.2, opisane w `docs/TESTY.md`) `test-m6`: wpisanie klucza przez MokManagera jest pomijane bez sekretu `K1_KLUCZ_PODPISU` (to
      zamierzone — zostaje, ale opisać w `docs/TESTY.md`).

## 2. Ostatnie przejrzenie kodu
Jedno pytanie: *czy te kawałki zmieniają się razem?* Szukać: kopii tej samej logiki lub stałych,
nieużywanego kodu (funkcje, pola, makra, ikony), opakowań bez treści, `if` dla jednego przypadku,
połkniętych błędów, nieaktualnych komentarzy. Trzy obszary:
- [ ] [w toku: Claude, gałąź claude/blissful-mayer-ts7rzb] system: `src/nadzorca/`, `src/wspolne/`, `src/instalator/`, `src/skarbiec/`, `src/init*/`, `src/wejscie/`
- [ ] [w toku: Claude, gałąź claude/blissful-mayer-ts7rzb] interfejs: `src/przybornik/`, `src/grafika/`, `src/gui/`, `src/ekran/`, `src/logowanie/`, `src/aplikacje/`, `narzedzia/`
- [ ] [w toku: Claude, gałąź claude/blissful-mayer-ts7rzb] skrypty i testy: `scripts/`, `Makefile`, `testy/`, `.github/workflows/`, `jadro/`

Znane, jeszcze niezrobione:
- [x] (0.7.2, `k1_hex_bajty`) Parsowanie szesnastkowe w `src/nadzorca/rozruch.c` (`hex`, identyfikator wydania) — kopia logiki,
      którą ma już `k1_cmdline`; jedna funkcja.
- [x] (0.7.2, `k1_bateria_stan` w `k1_liczba`) Teksty stanu baterii są w dwóch miejscach (`src/aplikacje/ustawienia/main.c` `stany[]`,
      `src/aplikacje/zasoby/main.c` `bateria_opis`) — jedno źródło w przyborniku.
- [x] (0.7.2, `k1_utf8_poczatek`) Notatki (`src/aplikacje/notatki/main.c`, edytor wielowierszowy) mają własne cofanie po znakach
      UTF-8 — sprawdzić, czy da się użyć wspólnych funkcji z `k1_rysuj.h` / `k1_klawiatura.h`.

## 3. Do sprawdzenia (drobne zmiany zachowania z 0.7.1)
Wspólny czytnik partycji (`src/wspolne/k1_efi.c`) zmienił dwa rzadkie przypadki — oba uznane za
poprawne, ale warto potwierdzić testem:
- [x] (0.7.2, `test_efi`) Instalator: dysk z A/B, ale z nieczytelnym `grubenv` — przed zapisem całej partycji sprawdza teraz
      wersję miejsca A (wcześniej nie sprawdzał), więc odmówi powrotu do starszej wersji.
- [x] (0.7.2, `test_efi`) Lista dysków w nadzorcy: przy nieczytelnym `grubenv` pokazuje wersję miejsca A (wcześniej:
      starego manifestu z 47 MiB).

## 4. Dokumentacja
- [x] (0.7.2) `docs/ARCHITEKTURA.md` — mapa repo i konwencje po 0.7.1: listy `K1_SYSTEM`/`K1_APLIKACJE`
      (`src/wspolne/k1_uslugi.h`), tabela kanałów `kanaly[]` (`src/nadzorca/uslugi.c`), `k1_efi.h`,
      `k1_io.h`, `k1_bajty.h`, `k1_liczba.h`, `k1_pole_klawisz`, `k1_bateria`, paleta `K1_*` w
      `k1_grafika.h`, `make_jadra` / `grub_poczatek` w `scripts/wspolne.sh`, Kalkulator.
- [x] (0.7.2) `docs/TESTY.md` — `test_efi`, `test_kalkulator`, scenariusz Kalkulatora w `test-m4`, jedna reguła
      fuzzingu (lista `FUZZ` w `Makefile`, korpus `testy/host/korpus-cmdline`), wspólne `k1_test_host.h`.
- [x] (0.7.2) `docs/DO_ZROBIENIA.txt` — nagłówek „Stan” (wciąż 0.7.0), wpis o 0.7.1 i 0.7.2.
- [x] (0.7.2) `README.md` — Kalkulator na liście aplikacji.

## Gdy wszystko odhaczone
Wydanie 0.7.3, w ADR 0023 dopisać „dokończone w 0.7.3”, ten plik usunąć (historia zostaje w git) —
i dopiero wtedy M7.
