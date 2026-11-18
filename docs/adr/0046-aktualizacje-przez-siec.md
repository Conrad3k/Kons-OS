# 0046. Aktualizacje przez sieć

- Status: Przyjęty; krok 2a wdrożony w 0.10.5 (formaty, podpis, program zapisujący), krok 2b — strefa pobierania,
  nadzorca, Ustawienia i gui
- Data: 2026-11-18
- Dotyczy: ADR 0021 (bezpieczne aktualizacje — uzupełniony), ADR 0022 (A/B), ADR 0038 (pliki startowe), ADR 0042
  i 0043 (strefy, łącznik), SPEC §8 (aktualizacje); plan `docs/PLAN-0.10.5.txt`: krok 2, T2–T7, T18, S7–S16, S25, S50

## Kontekst
Do 0.10.4 nowa wersja K1 trafiała na dysk tylko z pendrive'a (Ustawienia → Aktualizacje, ADR 0021, 0022). Właściciel chce,
żeby K1 sam sprawdzał i pobierał nowe wersje z wydań na GitHubie, a instalował dopiero po „Zainstaluj” (D4), i żeby przed
tym pytaniem było widać, co nowa wersja zmienia (D13). GitHub działa tylko przez HTTPS, którego K1 w sobie nie ma.

## Decyzja

**Zaufanie bez zmian:** K1 przyjmuje przez sieć tylko wersję podpisaną kluczem K1 (ten sam klucz i ten sam podpis EdDSA
co manifesty, ADR 0021) i nigdy wersji nie nowszej od tej, która działa. GitHub, łącze i strefa pobierania mogą najwyżej
nie podać nowej wersji albo podać śmieci — śmieci są odrzucane. Wersja deweloperska (bez klucza w K1) przez sieć nie
przyjmuje niczego — wyjątek: test aktualizacji w obrazie testowym (niżej), gdzie przyjmuje pliki bez podpisu.

**Skąd (T2, S7 — POTWIERDZONE próbą 2026-11-18):** z najnowszego wydania, nie z repo:
`https://github.com/Conrad3k/Kons-OS/releases/latest/download/k1-ogloszenie.bin`. GitHub odpowiada przekierowaniem 302
na `github.com/…/releases/download/k1-v<wersja>/…`, a stamtąd na `objects.githubusercontent.com` (dawniej też
`release-assets.githubusercontent.com` — obie nazwy na liście hostów). CI publikuje wydanie z `--latest`.

**Ogłoszenie wydania (T3; `src/wspolne/k1_ogloszenie.h`):** do 8 KiB, stały układ: wersja, identyfikator wydania,
wielkość i SHA-256 paczki, czas wydania, flaga „nowa linia jądra”, do 8 opisów wersji, podpis EdDSA nad całością.
**Korekta T3:** w ogłoszeniu nie ma nazwy paczki — K1 składa adres paczki z samego numeru wersji
(`k1-v<w>/k1-<w>.k1a`), więc żaden tekst z sieci nie trafia do adresu.

**Opis wersji (T18, D13):** wiersz „Opis w K1:” we wpisie CHANGELOG (od 0.10.5; `scripts/opisy-wersji.py`, pilnuje
`sprawdz-czytelnosc`), wpis robota — znacznik `<!-- k1: poprawki -->`: K1 pokazuje wtedy swoje stałe zdanie
(`K1_OGL_ZDANIE_POPRAWEK` w `k1_ogloszenie.h` — robot nie pisze tekstu, który pokazuje K1). Tekst: poprawny UTF-8, tylko
znaki z tabeli, które mają czcionki K1 (litery z polskimi, cyfry, spacja, podstawowa interpunkcja, „ ” – — … ·), bez
znaków sterujących i adresów, do 400 B. Tę samą funkcję (`k1_ogl_tekst_dobry`) ma narzędzie podpisu — zły tekst nie
przejdzie budowy. Co pokazać: `k1_ogl_wybierz` (wersje nowsze od działającej, najnowsza u góry, „poprawki” z rzędu jako
jedno zdanie, „oraz wcześniejsze zmiany”).

**Paczka aktualizacji (T4; `src/wspolne/k1_paczka.h`):** nagłówek z oboma manifestami (format 2 i 3), wnętrze miejsca A
(system, `k1.efi`), przód K1-EFI wydania (sektor startowy, FAT-y, foldery, grubenv — do pierwszego miejsca) i jej tył
(od końca drugiego miejsca: pliki startowe; zera na końcu obcięte). Bez kompresji i bez własnego podpisu: SHA-256 całości
podpisuje ogłoszenie, system i pliki startowe — ich manifesty. Paczkę i ogłoszenie robi `narzedzia/wydanie.c`
w `scripts/zbuduj-usb.sh` (tym samym kluczem co manifesty, wspólne wczytanie klucza: `narzedzia/klucz.c`).

**Poczekalnia — korekta T7:** paczka nie idzie prosto do wolnego miejsca A/B, tylko do **poczekalni**: części partycji
K1-EFI dysku za jej FAT32 (partycja instalowana od M5f ma 512 MiB, FAT 280 MiB — ok. 230 MiB, których nic nie czyta;
`k1_pocz_od`). Znak poczekalni (4 KiB, „K1W1”: wersja, wielkość i SHA-256 paczki, skrót BLAKE2b przeciw uszkodzeniom) jest
zerowany przed pierwszym bajtem i zapisywany po sprawdzeniu ostatniego; za nim ogłoszenie, od 1 MiB paczka. Dlaczego:
(1) pobieranie nie dotyka niczego, co startuje — nawet wolnego miejsca; (2) poprzednia wersja zostaje w wolnym miejscu,
dopóki nowa czeka — „Wróć do poprzedniej wersji” działa też wtedy; (3) czekająca wersja przetrwa ponowne uruchomienie
(„Później”) razem ze swoim opisem. Koszt: przy „Zainstaluj” jedna kopia systemu z poczekalni do wolnego miejsca (ok. 128
MiB — sekundy). Pendrive ma partycję K1-EFI równą FAT-owi — poczekalni nie ma, więc K1 z pendrive'a przez sieć się nie
aktualizuje (D5).

**Kto co robi (T6):**
- **Program zapisujący** (`instalator`, UID 105, klatka bez ścieżek) — jedyny czyta to, co przyszło z sieci; nowe tryby
  `K1_INST_SIEC_*` (`src/instalator/siec.c`, `siec_instaluj.c`; test `test_siec_instalatora` na obrazach K1-EFI z mtools):
  - `STAN`: ogłoszenie ze strefy (podpis, format), znak poczekalni, wersje miejsc → `INST_SIEC` (działa / czeka / do
    pobrania / poprzednia / flagi); ogłoszenie do pokazania (świeże albo czekającej wersji) do memfd od nadzorcy;
  - `POBIERZ`: ogłoszenie z `STAN` jeszcze raz (podpis), paczka ze strefy — **nagłówek i podpisy obu manifestów, wersja
    i identyfikator wydania sprawdzone, zanim cokolwiek zostanie zapisane**; potem strumieniem do poczekalni z liczeniem
    SHA-256 i skrótu systemu, odczyt i ponowne SHA-256 z dysku, na końcu ogłoszenie i znak;
  - `ZAINSTALUJ`: całość jeszcze raz z dysku (znak, podpis ogłoszenia, SHA-256, podpisy manifestów, skrót systemu, skrót
    plików startowych z przodu i tyłu w pamięci) → pliki startowe obok starych (`start.c`, ADR 0038) → system do wolnego
    miejsca (odczyt i porównanie) → jego manifesty → grubenv `k1_nowy` → znak wyzerowany;
  - `WROC` (D7, S25): wolne miejsce ma wersję starszą od działającej (tę, która działała przed aktualizacją — była
    potwierdzona) → podpis i skrót sprawdzone → `k1_nowy`. Start na próbę i potwierdzenie jak po każdej aktualizacji.
- **Nadzorca** — kiedy sprawdzać (D4: po połączeniu z siecią i co 6 godzin, „Sprawdzaj sam”), start strefy pobierania,
  potok strefa → program zapisujący, memfd ogłoszenia dla Ustawień i gui, kanał Ustawień (krok 2b). Ogłoszenia nie czyta.
- **Strefa pobierania** (krok 2b) — HTTPS (curl z Debiana w strefie), bajty przez vsock do VMM, z VMM potokiem do
  programu zapisującego; łącznik tej strefy łączy tylko z nazwami z jednej tabeli (`github.com`,
  `objects.githubusercontent.com`, `release-assets.githubusercontent.com`), adres IP podany wprost — odmowa.
- **Ustawienia i gui** — stan, pytanie, opis (memfd od nadzorcy, sprawdzany jeszcze raz `k1_ogloszenie_czytaj` — bez
  podpisu, bez kryptografii w aplikacjach).

**Która wersja (S25):** do pobrania tylko wersja nowsza od działającej **i** od tej w wolnym miejscu, gdy tamta jest
nowsza od działającej (nie wystartowała albo właściciel z niej wrócił) — K1 nie proponuje jej znowu, tylko następną. Bez
osobnego zapisu „odrzuconej wersji”: mówi to samo wolne miejsce. Przy próbie w toku (`k1_nowy` albo `k1_proba` w grubenv)
nic się nie pobiera ani nie instaluje.

**Test aktualizacji:** `INST_ZADANIE` c = 1 tylko od nadzorcy w trybie testu aktualizacji (krok 2b: włącza go program
testowy obrazu testowego); wtedy wersja deweloperska przyjmuje ogłoszenie i paczkę bez podpisu, a strefa pobierania łączy
się z serwerem testowym na „routerze” QEMU (krok 2b). W obrazie z kluczem (CI z sekretem) podpis sprawdzany zawsze.

## Konsekwencje
- Wydanie ma dwa nowe pliki: `k1-<wersja>.k1a` i `k1-ogloszenie.bin` (oba w `SHA256SUMS`).
- Program zapisujący: klatka 64 MiB (przód i tył paczki w pamięci przy „Zainstaluj”) i `RLIMIT_FSIZE` 16 KiB (memfd
  ogłoszenia).
- Format paczki zakłada układ K1-EFI z `zbuduj-usb.sh`: oba miejsca jedno za drugim, grubenv przed nimi
  (`k1_paczka_uklad`); inny układ = narzędzie nie zbuduje paczki.
- Ryzyka (RYZYKA): przejęte konto GitHub = podpisana wersja u wszystkich dopiero razem z kluczem (sekret CI); ukrywanie
  nowej wersji (ktoś na łączu blokuje pobieranie) — K1 mówi „Nie udało się sprawdzić”; GitHub widzi adres komputera.

## Rozważone
- **TLS w K1** — dużo obcego kodu w najbardziej zaufanej części, a podpis i tak chroni treść (T5).
- **Zapis prosto do wolnego miejsca** (plan T7) — prostsze o jedną kopię, ale pobieranie niszczyłoby poprzednią wersję
  (D7) i dotykało miejsca, z którego GRUB może startować.
- **Nazwa paczki w ogłoszeniu** (plan T3) — zbędna: wynika z wersji.
