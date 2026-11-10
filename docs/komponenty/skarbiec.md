# Komponent: skarbiec (Blob Vault)

| | |
|---|---|
| **Status** | PRZETESTOWANE (M5c na hoście, M5d w QEMU: dysk SATA i pendrive przez UEFI; 0.5.8: pendrive podłączony po starcie; 0.8.0: pliki i foldery — host i QEMU) |
| **Pliki** | `src/skarbiec/` w modułach (0.8.0, `skarbiec.h`): `main.c` (pętla, kanały, obiekty aplikacji), `obiekty.c` (obiekty według właściciela i nazwy: magazyn albo pamięć), `haslo.c` (odblokowanie, zmiana hasła, limit prób), `dokumenty.c` (pliki, foldery, Kosz), `dok_kanal.c` (komunikaty `DOK_*`, tabela praw); format: `magazyn.{c,h}` (rekordy, indeks), `naglowek.c` (nagłówek i owinięty klucz; `magazyn_wew.h`), `src/zewnetrzne/monocypher/`; wspólne: `src/wspolne/k1_memfd.*`, `k1_skarb_klient.*` (strona klienta), `k1_dokument.*` (wpis pliku, typy, foldery) |
| **Decyzje** | ADR 0015 (format, szyfrowanie), ADR 0016 (obraz USB), ADR 0013 (partycja „K1-DANE”), ADR 0029 (pliki, foldery, Kosz) |

## Cel
Trwałe, zaszyfrowane dane użytkownika: ustawienia (ekran), notatki (Notatki), historia baterii, rozmieszczenie
pulpitu, od 0.8.0 **pliki i foldery użytkownika** (Pliki, pulpit). Jedyny proces z dostępem do partycji danych.

## Granica zaufania
Wejście: zawartość partycji (mogła być zmieniona przez kogoś z nośnikiem w ręku) i komunikaty od
klientów. Każdy rekord na dysku jest uwierzytelniony (zmiana = odmowa otwarcia, nie śmieci);
klient widzi tylko obiekty swojego właściciela (właściciela kanału podaje nadzorca, nie klient);
każdy pakiet ma sprawdzany UID nadawcy (`SO_PASSCRED`, w obie strony).

## Uprawnienia
UID/GID 103, klatka (ADR 0007): seccomp = baza + `pread64`, `pwrite64`, `fdatasync` (partycja i memfd —
innych plików nie ma), `getrandom`, `memfd_create`, `sysinfo`; Landlock bez
żadnej ścieżki (nie otworzy niczego); cgroup 320 MiB (Argon2id do 256 MiB), 1 proces;
`RLIMIT_FSIZE` 24 MiB (memfd z plikiem do 16 MiB, 0.8.0; zapisu na urządzenie blokowe ten limit nie dotyczy —
`block/fops.c`). Bez partycji obiekty w pamięci: do 4096, razem do 128 MiB.

## Wejścia i wyjścia
- fd 3: kanał sterujący od nadzorcy — `SKARB_KLIENT(właściciel, uid, na_usb)` + koniec kanału klienta (0.6.3: `na_usb` wraca do klientów w `SKARB_WYNIK` d, bit 1).
- fd 4: partycja GPT „K1-DANE” (otwarta przez nadzorcę `O_RDWR|O_EXCL`); brak → tryb „tylko w pamięci”.
- Kanały klientów: `SKARB_CZYTAJ`, `SKARB_ZAPISZ` (+ zapieczętowany memfd o podanym rozmiarze),
  `SKARB_USUN` → `SKARB_DANE` (+ memfd) albo `SKARB_WYNIK` (wynik, tryb: dysk / pamięć).
- Klienci (0.5.6): ekran (fd 5, obiekt `K1_OB_USTAWIENIA`), Notatki (fd 4; od 0.5.8 obiekt 1 = lista,
  2…33 = notatki), Ustawienia (fd 5: `SKARB_INFO`, `SKARB_HASLO`), ekran hasła (`SKARB_ODBLOKUJ`),
  nadzorca (0.6.0: właściciel `K1_WL_SYSTEM`, obiekt `K1_OB_BATERIA` — historia baterii, `k1_bat_arch.h`),
  gui (0.7.5: rozmieszczenie pulpitu; 0.8.0: pliki folderu Pulpit), Pliki (0.8.0: pliki — `DOK_*`, niżej).
- Hasło (0.5.7, ADR 0017): stan „zablokowany” (magazyn z hasłem, nieotwarty) → `SKARB_STAN` do nadzorcy;
  `SKARB_ODBLOKUJ` tylko od ekranu hasła, `SKARB_HASLO` (obecne + nowe) tylko od Ustawień, `SKARB_INFO`
  od każdego; limit prób (od trzeciej złej: 5 s … 5 min) liczy skarbiec. Zablokowany: odczyt/zapis
  obiektów → błąd „złe hasło”.
- Log: utworzenie/otwarcie magazynu, klienci, zapisy (numer obiektu i rozmiar — nigdy treść).

## Pliki i foldery (0.8.0, ADR 0029)
Wspólny właściciel `K1_WL_DOKUMENTY` (101) — nie aplikacja. Stałe foldery 1–6 (Pulpit, Dokumenty, Obrazy, Muzyka,
Wideo, Pobrane) nie mają obiektów; wpisy użytkownika mają numery od 100:
- `m<numer>` — **wpis** (176 B, `k1_dokument.h`): numer, folder nadrzędny, rodzaj (plik / folder), typ, pochodzenie,
  kto utworzył, generacja treści, rozmiar, czasy utworzenia, zmiany i wrzucenia do Kosza, nazwa (≤ 127 B UTF-8);
- `t<numer>g<generacja>k<kawałek>` — **treść** po 1 MiB (plik ≤ 16 MiB). Zapis: kawałki nowej generacji → wpis →
  usunięcie starych kawałków; przy otwarciu skarbiec usuwa kawałki bez wpisu i wpisy nie do odczytania, a wpis bez
  folderu (albo w pętli) przenosi do Dokumentów.
- Typ pliku rozpoznaje skarbiec przy każdym zapisie z pierwszych bajtów (`K1_TYPY`: tekst, PNG, JPEG, PDF, ZIP,
  inny) — nigdy z nazwy. Nazwy jak w Windows (bez `\ / : * ? " < > |`, znaków sterujących i odwracających
  kierunek tekstu, bez spacji i kropki na końcu), unikalne w folderze bez względu na wielkość liter (także
  polskich); nowy wpis o zajętej nazwie dostaje „ (2)” przed rozszerzeniem. Do 8 poziomów folderów, 1024 wpisy.
- **Kosz**: „do Kosza” zapisuje czas we wpisie (folder idzie z zawartością); przywrócony wraca do swojego folderu
  (gdy go nie ma — do Dokumentów); „usuń na zawsze” i „opróżnij” tylko w Koszu; po 30 dniach skarbiec usuwa sam —
  przy otwarciu i co godzinę, według zegara; gdy zegar jest przed czasem wrzucenia (cofnięty) — nic.

**Prawa** — tabela w `dok_kanal.c` (wiersz na klienta; właściciel kanału od nadzorcy): Pliki — wszystko; pulpit
(gui) — tylko folder Pulpit (lista nazw i typów, „do Kosza”, „opróżnij Kosz”, „Otwórz w…”, bez treści); Notatki
(0.8.0, ADR 0030) — tylko pliki, które same utworzyły, i pliki dane im przez użytkownika: lista, treść, zapis,
nazwa, nowy plik tylko w swoim folderze **Dokumenty › Notatki** (skarbiec tworzy go, gdy go nie ma), do Kosza
tylko własne, okno wyboru pliku; Wybór pliku — lista widocznych (bez Kosza, bez treści) i odpowiedź; inni — nic
(`K1_DOK_BLAD_PRAWA`). Wpis spoza zasięgu klienta = „nie ma” (`K1_DOK_BLAD_BRAK`).

**Dostęp do pliku po geście** (0.8.0, ADR 0030, `dostep.c`): „Otwórz w…” w Plikach albo na pulpicie
(`DOK_OTWORZ`) — skarbiec sprawdza, że aplikacja otwiera ten typ (`K1_OTWIERANIE`), zapamiętuje „aplikacja X może
ten plik” i prosi nadzorcę (`SKARB_URUCHOM`) o uruchomienie jej z plikiem. Prośba aplikacji o plik
(`DOK_WYBIERZ`) — jedna naraz (`K1_DOK_BLAD_CZEKA`); skarbiec prosi nadzorcę o okno wyboru (`SKARB_URUCHOM` z
aplikacją „Wybór pliku”), oknu podaje prośbę (`DOK_PROSBA`), a gdy ono odpowie (`DOK_WYBRANO`: plik widoczny,
pasujący typ) — daje dostęp i odpowiada aplikacji (`DOK_WYBRANY`; 0 = anulowano, także gdy okno się zamknie).
Dostęp jest tylko w pamięci skarbca i znika, gdy zamknie się ostatni kanał tej aplikacji (do 64 naraz).

| Komunikat | Kierunek | Pola |
|---|---|---|
| `DOK_SPIS` | klient → | lista wpisów, które klient widzi (odpowiedź `DOK_LISTA`) |
| `DOK_CZYTAJ` | klient → | obiekt = plik, a = najwyżej tyle bajtów (podgląd: 64 KiB) → `DOK_TRESC` |
| `DOK_NOWY` | klient → | obiekt = folder, a = plik / folder, b = długość nazwy, c = długość treści; memfd: nazwa + treść |
| `DOK_ZAPISZ` | klient → | obiekt = plik, a = długość; memfd: nowa treść (nowa generacja) |
| `DOK_NAZWA` | klient → | obiekt = wpis, a = długość, b = 1: zajęta nazwa dostaje „ (2)” (0: odmowa); memfd: nowa nazwa |
| `DOK_PRZENIES` | klient → | obiekt = wpis, a = folder docelowy (nie do siebie, nie za głęboko) |
| `DOK_KOSZ`, `DOK_PRZYWROC`, `DOK_USUN` | klient → | obiekt = wpis (usuń: tylko z Kosza) |
| `DOK_OPROZNIJ` | klient → | wszystko z Kosza na zawsze |
| `DOK_LISTA` | → klient | a = długość, b = liczba wpisów; memfd: „K1D1”, liczba, w Koszu, wpisy |
| `DOK_TRESC` | → klient | obiekt = plik, a = wysłane bajty, b = cały rozmiar; memfd |
| `DOK_WYNIK` | → klient | obiekt, a = rodzaj prośby, b = wynik (0, `K1_MAG_*`, `K1_DOK_BLAD_*`), c = numer nowego wpisu |
| `DOK_ZMIANA` | → klient | pliki się zmieniły — do każdego klienta z prawem listy (odświeżają widok) |
| `DOK_OTWORZ` | Pliki, pulpit → | obiekt = plik, a = aplikacja (nie Pliki), która otwiera jego typ → `DOK_WYNIK` |
| `DOK_WYBIERZ` | aplikacja → | a = typ (0: każdy) — okno wyboru pliku; odpowiedź później: `DOK_WYBRANY` (błąd od razu: `DOK_WYNIK`) |
| `DOK_WYBRANO` | Wybór pliku → | obiekt = wybrany plik (0: anulowano) → `DOK_WYNIK` |
| `DOK_WYBRANY` | → aplikacja | obiekt = plik, którego może teraz używać (0: anulowano) |
| `DOK_PROSBA` | → Wybór pliku | a = aplikacja, która prosi, b = typ (0: każdy) |
| `SKARB_URUCHOM` | → nadzorca | kanał sterujący: obiekt = plik (0: bez), a = aplikacja do uruchomienia (z plikiem: jej fd 5) |

Zablokowany magazyn (hasło) — każda prośba `DOK_*` dostaje „złe hasło”. W logu tylko numery komunikatów, liczby
wpisów i wyniki — nigdy nazwy ani treść.

## Format na dysku (bloki po 4096 B, liczby little-endian)
| Miejsce | Zawartość |
|---|---|
| blok 0 i 1 | nagłówek (kopia z generacją parzystą / nieparzystą; nowsza poprawna wygrywa) |
| bloki 2… | dwa obszary dziennika po (N−2)/2 bloków; aktywny wskazuje nagłówek |

Nagłówek: `K1SKARB1`, wersja 1, flagi (bit 0 = hasło), uuid (16), generacja, liczba bloków, aktywny
obszar, Argon2id (KiB, przejścia), epoka, koniec dziennika, sól (16), owinięty klucz główny (nonce 24
+ MAC 16 + klucz 32), zera do bajtu 4080, MAC (BLAKE2b z kluczem pochodnym od klucza głównego).

Rekord (wielokrotność bloku): liczba bloków (u32), 0 (u32), nonce (24), MAC (16), szyfrogram:
typ (obiekt / usunięcie), właściciel, długość, czas, nazwa (32), BLAKE2b treści, treść, zera.
Dane dodatkowe AEAD: uuid, epoka, obszar, pozycja, liczba bloków.

Zapis: rekord → `fdatasync` → nagłówek (następna generacja, drugi slot) → `fdatasync`.
Pełny obszar → kompaktowanie do drugiego obszaru (nowa epoka). Formatowanie: tylko gdy pierwszy MiB
partycji to zera (inaczej „obce dane — nie nadpisuję”).

## Obsługa błędów
Brak partycji, dwie partycje „K1-DANE”, obce dane, uszkodzony magazyn, magazyn z hasłem (do M5e) →
dane tylko w pamięci, partycja nietknięta, wpis w logu. Awaria skarbca → nadzorca uruchamia go
ponownie i restartuje ekran (klienci dostają nowe kanały).

## Powierzchnia ataku
Parser nagłówka i rekordów (`naglowek.c`, `magazyn.c`, testy + fuzzing także z poprawnym MAC — bez hasła napastnik
zna klucz), komunikaty klientów (32 B), memfd od klientów (pieczęcie i rozmiar sprawdzane), od 0.8.0 nazwy plików
od klientów i wpisy z dysku (`k1_dok_wpis_czytaj` — każde pole) oraz tabela praw.

## Testy
| Test | Rodzaj |
|---|---|
| `test_magazyn` (host, ASan/UBSan): restart, zmieniony bajt, zamienione rekordy, uszkodzony nagłówek, przerwany zapis, hasło (złe, zmiana, usunięcie), kompaktowanie, brak miejsca, obce dane | funkcjonalny + negatywny |
| `fuzz_magazyn` (libFuzzer): surowy obraz, zmiany bajtów, nagłówki z poprawnym MAC | fuzzing |
| `test_komunikat`: komunikaty `SKARB_*` (zakresy, kierunki, deskryptory) | negatywny |
| w systemie: klatka (UID, tylko partycja „K1-DANE”), próba w klatce `skarbiec`, dysk trzyma tylko skarbiec | funkcjonalny + negatywny |
| `test-m4.sh`: tapeta, motyw, rozdzielczość i notatka wracają po wyłączeniu i ponownym starcie z tym samym dyskiem; na dysku brak jawnego tekstu | funkcjonalny |
| `test_dokumenty` (host, 0.8.0): nazwy (złe znaki, UTF-8, odwrócony tekst), typy z zawartości, wpis i lista (każde pole poza zakresem), plik w trzech kawałkach, nowa treść bez starych kawałków, przerwany zapis, foldery (unikalne nazwy, „(2)”, do siebie, za głęboko), Kosz (przywracanie, 30 dni, cofnięty zegar, opróżnianie), uszkodzone wpisy i kawałki przy otwarciu; prawa przez prawdziwy kanał: Pliki, pulpit (tylko Pulpit, bez treści), inni — nic; zły memfd | funkcjonalny + negatywny |
| `fuzz_dokument`: lista wpisów (Pliki i pulpit ją czytają), nazwy, typy | fuzzing |
| `test-m4.sh` (0.8.0): foldery i Kosz w Plikach, plik na pulpicie, wszystko po ponownym starcie | funkcjonalny |
| `test-m5.sh`: obraz USB przez UEFI — pierwszy start tworzy magazyn, drugi go otwiera | funkcjonalny |
| `test-m5.sh` (0.5.8): pendrive z pustą K1-DANE podłączony po starcie → restart skarbca, nowy magazyn, pulpit od nowa | funkcjonalny |
