# Komponent: dekoder (obrazy PNG, JPEG, GIF, BMP, WebP)

| | |
|---|---|
| **Status** | ZAIMPLEMENTOWANE (0.8.0 krok 4; 0.8.2 krok 3: duże zdjęcia, GIF, BMP, WebP, plik kawałkami, wielu klientów; krok 4: Podgląd, wymiary zdjęcia w odpowiedzi); PRZETESTOWANE na hoście (`test_dekoder`, `fuzz_dekoder`) i w QEMU (`test-pliki.sh`: miniatura i tapeta; `test-pendrive.sh`: zdjęcia 48 i 100 Mpx, nagłówek 101 Mpx, GIF, BMP, WebP) |
| **Pliki** | `src/dekoder/main.c` (usługa: jedna prośba, plik kawałkami, jedna odpowiedź), `obraz.{c,h}` (rodzaj pliku, orientacja EXIF, zmniejszanie i obracanie), `wuffs.c` (tabela dekoderów Wuffs); `src/wspolne/k1_dekoder.h` (tabela limitów), `k1_skaluj.{c,h}`, `k1_dokument.{c,h}` (typy plików i ich rodzina — `K1_TYPY`, rozpoznanie `k1_dok_typ`); `src/nadzorca/dekoder.c` (tabela klientów, uruchamianie, pamięć z RAM), `profile.c` (klatka); `src/przybornik/k1_dekodowanie.{c,h}` (strona klienta: prośba, kawałki od skarbca, odpowiedź — jedna dla Plików i Podglądu, 0.8.2 krok 4); `src/aplikacje/pliki/obrazy.c` (Pliki), `src/aplikacje/podglad/obraz.c` (Podgląd); `src/ekran/zdjecie.c` (tapeta) |
| **Decyzje** | ADR 0032 (z uzupełnieniem 0.8.2), też 0026 p. 3, SPEC §7 „dekodery” |

## Cel
Zamienić plik obrazu na piksele — dla miniatury w Plikach, aplikacji Podgląd (0.8.2 krok 4, ADR 0037) i tapety ze
zdjęcia — tak, żeby obcy plik nie dotknął niczego poza jednym, krótko żyjącym procesem bez żadnych uprawnień.

Formaty (rodzina „obrazy” w `K1_TYPY`, dekoder — wiersz w `K1_DEKODERY` w `wuffs.c`): PNG, JPEG (zwykły i progresywny,
orientacja EXIF), GIF (pierwsza klatka animacji), BMP, WebP (stratny VP8 i bezstratny VP8L, z przezroczystością).
HEIC (iPhone) — nie: Wuffs go nie ma, a obcego dekodera HEVC do świata natywnego nie bierzemy (`docs/ZALEZNOSCI.md`).

## Granica zaufania
Plik jest wrogi. Dekodowanie robi Wuffs (Google; kod generowany z języka, który przy kompilacji dowodzi, że nie ma
odczytu ani zapisu poza tablicą i przepełnienia liczby — `docs/ZALEZNOSCI.md`), resztę `obraz.c`: rodzaj z pierwszych
bajtów (ta sama funkcja co w skarbcu — `k1_dok_typ`), orientacja EXIF (tylko znaczniki przed danymi obrazu i pierwsza
lista znaczników, każde przesunięcie w granicach), rozmiar sprawdzony przed przydziałem pamięci (tabela limitów niżej).
Prośbę przyjmuje tylko od aplikacji, dla której nadzorca go uruchomił (jej numer w jedynym argumencie programu, UID
sprawdzany przez `SO_PASSCRED`), plik tylko jako kolejne zapieczętowane memfd dokładnie zapowiedzianych rozmiarów.
Klient sprawdza odpowiedź (rozmiar, pieczęcie), ekran sprawdza ją jeszcze raz przed użyciem jako tapety.

**Jednorazowy:** jedna prośba, jedna odpowiedź, koniec procesu. Przejęty dekoder (np. błąd w Wuffs) nie widzi
następnego obrazu ani niczego poza kanałem do swojego klienta.

## Limity — jedna tabela (`k1_dekoder.h`, 0.8.2 krok 3)
Ograniczeniem jest pamięć, nie bezpieczeństwo (o bezpieczeństwie decydują Wuffs i klatka).

| Stała | Wartość | Po co |
|---|---|---|
| `K1_DEK_MAKS_BAJTY` | 128 MiB | plik (zdjęcie 100 Mpx z telefonu ma 20–60 MB) |
| `K1_DEK_MAKS_MPX` | 100 Mpx | większe zdjęcie (według nagłówka) — odmowa z liczbą Mpx |
| `K1_DEK_PAMIEC` | 1 GiB | plik + piksele (4 B każdy) + obszar roboczy dekodera muszą się zmieścić razem — sprawdzane przed przydziałem |
| `K1_DEK_ZAPAS` | 128 MiB | kod, stos, sam dekoder, wynik (≤ 2560×1600) |
| `K1_DEK_PAMIEC_RAM` | 1/6 RAM | na komputerze z mniej niż 8 GB nadzorca obniża klatkę … |
| `K1_DEK_PAMIEC_MIN` | 384 MiB | … ale nigdy poniżej limitu z 0.8.0–0.8.1 (24 Mpx) |

Pomiar (komputer budujący, `obraz_przygotuj` do tapety 2560×1600, zdjęcia z Pillow):

| Zdjęcie | Czas | Pamięć (szczyt) |
|---|---|---|
| 48 Mpx (8000×6000), zwykły JPEG | 0,72 s | 254 MiB |
| 48 Mpx, progresywny | 0,95 s | 391 MiB |
| 100 Mpx (10000×10000), zwykły | 1,37 s | 528 MiB |
| 100 Mpx, progresywny | 2,28 s | 813 MiB |

Najgorszy przypadek, który się mieści: 100 Mpx progresywny z pełnym kolorem (4:4:4) — ok. 0,95 GiB. Progresywny JPEG
w CMYK (4 kanały) 100 Mpx już nie: dekoder odpowiada „za mało pamięci”, zanim cokolwiek przydzieli.

## Uprawnienia
| UID | seccomp (poza bazą) | Landlock | cgroup i limity | deskryptory |
|---|---|---|---|---|
| 107 | `memfd_create` | brak ścieżek | 1216 MiB, 1 proces; `RLIMIT_AS` 1152 MiB (= `K1_DEK_PAMIEC` + `K1_DEK_ZAPAS`; za duży obraz kończy się błędem przydziału, nie zabiciem procesu), `RLIMIT_CPU` 20 s, `RLIMIT_FSIZE` 17 MiB (największy wynik 2560×1600×4); na komputerze z mniej niż 8 GB oba limity pamięci niższe (tabela wyżej) | fd 3: kanał do klienta |

## Klienci (`src/nadzorca/dekoder.c`)
Tabela „kto może prosić o dekoder”: aplikacja i droga, którą dostaje kanał. Dwa wiersze — Pliki (prośba `DEKODER` na
ich fd 6, razem z pendrive'em) i Podgląd (0.8.2 krok 4: jego fd 6, tylko `DEKODER`). Naraz działa jeden dekoder; każdy klient ma najwyżej jedną prośbę
w kolejce. Nowa prośba tego samego klienta zatrzymuje jego trwający dekoder (tamten obraz nie jest już potrzebny),
prośba innego klienta czeka na koniec trwającego; po końcu obsługiwany jest następny klient po kolei.

## Przebieg
1. Klient chce obrazu (Pliki: zaznaczony plik z rodziny „obrazy” → miniatura; Podgląd: zdjęcie w oknie; przycisk
   „Ustaw jako tapetę”): `DEKODER` do nadzorcy (fd 6 klienta). Stronę klienta (kroki 1, 3, 5) robi `k1_dekodowanie`.
2. Nadzorca tworzy kanał (obie strony z `SO_PASSCRED`), uruchamia dekoder w klatce (fd 3, argument: numer aplikacji
   klienta, limity pamięci z RAM) i daje klientowi drugi koniec w `DEKODER_KANAL`.
3. Klient → dekoder: `DEK_OBRAZ` (obiekt = numer pliku, a = długość, b × c = największy rozmiar: miniatura 200×150,
   Podgląd 928×928 — obraca i dopasowuje u siebie, tapeta 2560×1600). Potem plik kawałek po kawałku: Pliki proszą skarbiec o kawałek (`DOK_CZYTAJ`, najwyżej 1 MiB →
   `DOK_TRESC`, zapieczętowany memfd) i przekazują go dekoderowi bez czytania (`DEK_KAWALEK`). Zdjęcie 100 MB nie
   kosztuje Plików ani skarbca więcej pamięci niż jeden kawałek (do 0.8.1 skarbiec wysyłał całe zdjęcie ≤ 16 MiB w
   jednym memfd).
4. Dekoder: składa plik (kolejne kawałki, każdy dokładnie zapowiedzianej wielkości), rozpoznaje format, sprawdza rozmiar
   i pamięć, dekoduje do pełnego rozmiaru (32 bity, kolory przemnożone przez przezroczystość), zmniejsza z zachowaniem
   proporcji (nigdy nie powiększa; średnia z pola każdego piksela — `k1_skaluj`), obraca według EXIF (zdjęcia
   z telefonu trzymanego pionowo) i odsyła `DEK_PIKSELE` (a × b + memfd; c × d — wymiary samego zdjęcia po obrocie,
   od 0.8.2 krok 4: Podgląd je pokazuje) albo `DEK_BLAD`. Koniec. Bez kolejnego
   komunikatu przez 10 s — koniec.
5. Miniatura: Pliki kopiują piksele i rysują je w szczegółach; Podgląd — w oknie. Tapeta: klient przekazuje memfd ekranowi
   (`TAPETA_ZDJECIE`) bez czytania; ekran zapisuje zdjęcie w skarbcu i ustawia tapetę „Zdjęcie”.

## Komunikaty
| Typ | Kierunek | Pola |
|---|---|---|
| `DEKODER` | Pliki, Podgląd → nadzorca (fd 6) | — (uruchom dekoder) |
| `DEKODER_KANAL` | nadzorca → klient | + 1 fd: kanał do nowego dekodera |
| `DEK_OBRAZ` | klient → dekoder | obiekt = numer (odsyłany), a = długość pliku 1 B…128 MiB (`K1_DEK_MAKS_BAJTY`), b, c = największa szerokość ≤ 2560 i wysokość ≤ 1600; bez deskryptora (od 0.8.2; wcześniej + memfd z całym plikiem) |
| `DEK_KAWALEK` | klient → dekoder | obiekt jak w `DEK_OBRAZ`, a = numer kawałka 0…127 (po kolei), b = jego bajty 1 B…1 MiB (każdy poza ostatnim: 1 MiB); + zapieczętowany memfd b bajtów (0.8.2) |
| `DEK_PIKSELE` | dekoder → klient | obiekt, a × b = rozmiar obrazu; c × d = wymiary zdjęcia po obrocie (c ≥ a, d ≥ b, c × d ≤ 100 Mpx; 0.8.2 krok 4); + memfd a × b × 4 bajtów |
| `DEK_BLAD` | dekoder → klient | obiekt, a = `K1_DEK_BLAD_*` (nie obraz, za duży, uszkodzony, brak pamięci), b = liczba Mpx zdjęcia (zaokrąglona w górę, najwyżej 100 000) tylko przy „za duży”, inaczej 0 |
| `TAPETA_ZDJECIE` | Pliki, Podgląd → ekran | a × b; + memfd z `DEK_PIKSELE` (opis: `ekran.md`) |

## Obsługa błędów
Nie obraz, uszkodzony albo ucięty plik, zła suma kontrolna PNG, kawałek innej wielkości niż zapowiedziany albo nie po
kolei → `DEK_BLAD`; Pliki pokazują „Miniatura niedostępna” albo „Tego zdjęcia nie da się odczytać.” (Podgląd — to
drugie). Więcej niż 100 Mpx — w szczegółach „Zdjęcie ma 150 Mpx — / K1 pokazuje do 100 Mpx”, w Podglądzie i przy tapecie
to samo jednym zdaniem. Za mało pamięci (CMYK, mały komputer) — „Za mało pamięci na miniaturę” / „Za mało pamięci, żeby
pokazać to zdjęcie.”. Dekoder, który przekroczy
czas, jest zabijany przez jądro — klient traktuje zamknięty kanał jak błąd. Skarbiec, który nie da kawałka, kończy
pracę nad tym obrazem (bez komunikatu „Nie udało się.” w stopce). W logu tylko rodzaj (np. „Obraz JPEG”), rozmiar
w pliku i wyniku, czas — nigdy treść ani nazwa.

## Testy
| Test | Rodzaj |
|---|---|
| host `test_dekoder`: obrazy z Pillow (`testy/host/obrazy_dekoder.py`) — PNG, JPEG zwykły i progresywny, paleta, 16 bitów, przezroczystość, odcienie szarości, orientacja EXIF 3/6/8, zmniejszanie z proporcjami; GIF (też animowany: pierwsza klatka), BMP (też z paletą), WebP stratny, bezstratny i z przezroczystością; zdjęcia 48 i 100 Mpx zwykłe i progresywne; każdy typ z rodziny „obrazy” ma dekoder; `k1_skaluj` | jednostkowy |
| host `test_dekoder`: nagłówek 30000×30000 i 101 Mpx (odmowa z rozmiarem), CMYK progresywny 100 Mpx (za mało pamięci), ucięty JPEG, zła suma PNG, tekst, „BM” w tekście, RIFF bez WEBP, pusty plik, EXIF wskazujący poza siebie | negatywny |
| `fuzz_dekoder` (cały plik i rozmiar; korpus `testy/host/korpus-dekoder`: PNG, JPEG, GIF, BMP, WebP) | fuzzing |
| host `test_komunikat` (`DEKODER*`, `DEK_*` z `DEK_KAWALEK`, `TAPETA_ZDJECIE`, `USTAW` zdjęcia) | jednostkowy / negatywny |
| w systemie: próba w klatce dekodera (`t_proby`), spis plików rdzenia | negatywny |
| `test-pliki.sh` (QEMU): zdjęcie w Obrazach → podgląd w Plikach, „Ustaw jako tapetę” (kolor na zrzucie), Ustawienia → kafelek „Zdjęcie”, tapeta po ponownym starcie | funkcjonalny |
| `test-pendrive.sh` (QEMU, maszyna 8 GB): zdjęcia 48 i 100 Mpx (kilka MB, kilka kawałków; 100 Mpx tylko z KVM — w emulacji TCG trwa ok. 20 s procesora, tyle co limit), GIF, BMP, WebP → podgląd (kolor na zrzucie); nagłówek 101 Mpx → odmowa z liczbą Mpx; limit pamięci klatki w logu nadzorcy | funkcjonalny / negatywny |
