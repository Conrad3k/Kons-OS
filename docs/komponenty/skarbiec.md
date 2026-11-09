# Komponent: skarbiec (Blob Vault)

| | |
|---|---|
| **Status** | PRZETESTOWANE (M5c na hoście, M5d w QEMU: dysk SATA i pendrive przez UEFI; 0.5.8: pendrive podłączony po starcie) — 0.5.8 |
| **Pliki** | `src/skarbiec/main.c` (usługa), `src/skarbiec/magazyn.{c,h}` (format: rekordy, indeks), `naglowek.c` (nagłówek i owinięty klucz; wspólne części: `magazyn_wew.h`), `src/zewnetrzne/monocypher/` (kryptografia), `src/wspolne/k1_memfd.*`, `src/wspolne/k1_skarb_klient.*` (strona klienta) |
| **Decyzje** | ADR 0015 (format, szyfrowanie), ADR 0016 (obraz USB), ADR 0013 (partycja „K1-DANE”) |

## Cel
Trwałe, zaszyfrowane dane użytkownika: ustawienia (ekran), notatki (Notatki); później historia baterii,
pliki (M9). Jedyny proces z dostępem do partycji danych.

## Granica zaufania
Wejście: zawartość partycji (mogła być zmieniona przez kogoś z nośnikiem w ręku) i komunikaty od
klientów. Każdy rekord na dysku jest uwierzytelniony (zmiana = odmowa otwarcia, nie śmieci);
klient widzi tylko obiekty swojego właściciela (właściciela kanału podaje nadzorca, nie klient);
każdy pakiet ma sprawdzany UID nadawcy (`SO_PASSCRED`, w obie strony).

## Uprawnienia
UID/GID 103, klatka (ADR 0007): seccomp = baza + `pread64`, `pwrite64`, `fdatasync` (tylko na
deskryptorze partycji — innych plików nie ma), `getrandom`, `memfd_create`, `sysinfo`; Landlock bez
żadnej ścieżki (nie otworzy niczego); cgroup 320 MiB (Argon2id do 256 MiB), 1 proces;
`RLIMIT_FSIZE` 2 MiB (memfd; zapisu na urządzenie blokowe ten limit nie dotyczy — `block/fops.c`).

## Wejścia i wyjścia
- fd 3: kanał sterujący od nadzorcy — `SKARB_KLIENT(właściciel, uid, na_usb)` + koniec kanału klienta (0.6.3: `na_usb` wraca do klientów w `SKARB_WYNIK` d, bit 1).
- fd 4: partycja GPT „K1-DANE” (otwarta przez nadzorcę `O_RDWR|O_EXCL`); brak → tryb „tylko w pamięci”.
- Kanały klientów: `SKARB_CZYTAJ`, `SKARB_ZAPISZ` (+ zapieczętowany memfd o podanym rozmiarze),
  `SKARB_USUN` → `SKARB_DANE` (+ memfd) albo `SKARB_WYNIK` (wynik, tryb: dysk / pamięć).
- Klienci (0.5.6): ekran (fd 5, obiekt `K1_OB_USTAWIENIA`), Notatki (fd 4; od 0.5.8 obiekt 1 = lista,
  2…33 = notatki), Ustawienia (fd 5: `SKARB_INFO`, `SKARB_HASLO`), ekran hasła (`SKARB_ODBLOKUJ`),
  nadzorca (0.6.0: właściciel `K1_WL_SYSTEM`, obiekt `K1_OB_BATERIA` — historia baterii, `k1_bat_arch.h`).
- Hasło (0.5.7, ADR 0017): stan „zablokowany” (magazyn z hasłem, nieotwarty) → `SKARB_STAN` do nadzorcy;
  `SKARB_ODBLOKUJ` tylko od ekranu hasła, `SKARB_HASLO` (obecne + nowe) tylko od Ustawień, `SKARB_INFO`
  od każdego; limit prób (od trzeciej złej: 5 s … 5 min) liczy skarbiec. Zablokowany: odczyt/zapis
  obiektów → błąd „złe hasło”.
- Log: utworzenie/otwarcie magazynu, klienci, zapisy (numer obiektu i rozmiar — nigdy treść).

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
zna klucz), komunikaty klientów (32 B), memfd od klientów (pieczęcie i rozmiar sprawdzane).

## Testy
| Test | Rodzaj |
|---|---|
| `test_magazyn` (host, ASan/UBSan): restart, zmieniony bajt, zamienione rekordy, uszkodzony nagłówek, przerwany zapis, hasło (złe, zmiana, usunięcie), kompaktowanie, brak miejsca, obce dane | funkcjonalny + negatywny |
| `fuzz_magazyn` (libFuzzer): surowy obraz, zmiany bajtów, nagłówki z poprawnym MAC | fuzzing |
| `test_komunikat`: komunikaty `SKARB_*` (zakresy, kierunki, deskryptory) | negatywny |
| w systemie: klatka (UID, tylko partycja „K1-DANE”), próba w klatce `skarbiec`, dysk trzyma tylko skarbiec | funkcjonalny + negatywny |
| `test-m4.sh`: tapeta, motyw, rozdzielczość i notatka wracają po wyłączeniu i ponownym starcie z tym samym dyskiem; na dysku brak jawnego tekstu | funkcjonalny |
| `test-m5.sh`: obraz USB przez UEFI — pierwszy start tworzy magazyn, drugi go otwiera | funkcjonalny |
| `test-m5.sh` (0.5.8): pendrive z pustą K1-DANE podłączony po starcie → restart skarbca, nowy magazyn, pulpit od nowa | funkcjonalny |
