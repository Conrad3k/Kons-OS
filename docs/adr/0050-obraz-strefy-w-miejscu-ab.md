# 0050. Obraz przeglądarki poza rdzeniem, w miejscu A/B obok k1.efi

- Status: Przyjęty; wdrożony w 0.10.9 (krok 4); uzupełniony 2026-11-20 — krok 4b: większe miejsca A/B (decyzja właściciela D10 „A”)
- Data: 2026-11-19
- Dotyczy: ADR 0042 (obrazy stref w rdzeniu), ADR 0043 (obraz przeglądarki), ADR 0022 (A/B, miejsce 128 MiB), ADR 0046
  (paczka i poczekalnia), ADR 0021 (dm-verity); plan `docs/PLAN-0.10.9.txt`: krok 4, T16, luki S31–S35, S46; RYZYKA R81

## Kontekst
Od 0.10.0 obraz strefy przeglądarki (SquashFS, xz, 104 MiB) był plikiem rdzenia (`/rdzen/strefy/przegladarka.img`).
Rdzeń jest wbudowany w k1.efi i po starcie leży w pamięci — więc obraz przeglądarki zajmował ok. 109 MB pamięci przez cały
czas działania komputera, także gdy przeglądarka była zamknięta (RYZYKA R81). k1.efi miał 122,7 MiB w miejscu A/B
o rozmiarze 128 MiB (manifest miejsca na 127. MiB) — zapas ok. 2 MiB.

Plan (T16) chciał trzech rzeczy: obraz poza rdzeniem (pamięć), szybszy format (start przeglądarki) i miejsce w miejscu
A/B na FFmpeg (krok 3) i sterownik karty strefy (krok 6) — przy aktualizacji z poprzedniej wersji bez utraty danych.

## Pomiar (2026-11-19, ten sam katalog strefy — 400 MB plików)
| Format (SquashFS) | Rozmiar | Względem dziś |
|---|---|---|
| xz, blok 1 MiB, filtr x86 (dziś) | 108,7 MB | — |
| zstd poziom 19, blok 1 MiB | 120,3 MB | +10,6 % |
| zstd poziom 22, blok 1 MiB | 120,2 MB | +10,6 % |
| zstd poziom 22, blok 128 KiB | 131,3 MB | +20,8 % |
| lz4 hc, blok 128 KiB | 171,8 MB | +58 % |

EROFS nie mierzony (brak `mkfs.erofs` w kontenerze; z lz4hc rozmiar rzędu SquashFS lz4). Czasu startu przeglądarki nie
da się zmierzyć w kontenerze (bez KVM) ani w CI (przeglądarki CI nie testuje — decyzja właściciela 2026-11-17).

## Miejsce na dysku (z kodu: `src/instalator/gpt.h`, `src/wspolne/k1_uklad.h`, `k1_paczka.h`)
Dysk z K1 0.10.x: partycja K1-EFI 512 MiB = FAT32 280 MiB (pliki startowe, grubenv, miejsca A i B po 128 MiB) + 232 MiB
za FAT-em (poczekalnia aktualizacji przez sieć), zaraz za nią K1-DANE (skarbiec od pierwszego MiB — nie da się jej
przesunąć bez przenoszenia danych). Naraz mieszczą się trzy wersje K1: działająca, poprzednia („Wróć”) i czekająca
w poczekalni. Obraz poza miejscami A/B potrzebowałby własnych dwóch kopii (A i B): 2 × 105 MiB w xz, 2 × 115–126 MiB
w zstd — razem z poczekalnią nie mieści się w 232 MiB za FAT-em. Na dyskach z 0.10.x jedyne miejsce bez przesuwania K1-DANE
to wolna część miejsc A/B, które po wyjęciu obrazu z rdzenia mają ok. 108 MiB wolne.

## Decyzja
1. **Obraz przeglądarki leży w swoim miejscu A/B, zaraz za wewnętrznym FAT-em z k1.efi**, i kończy się tam, gdzie kończy
   się obraz miejsca (`fat_bajty` manifestu miejsca, `k1_obraz_strefy_w_miejscu`). Ma za danymi drzewo skrótów dm-verity
   (jak rdzeń: bez superbloku, ta sama sól). `scripts/zbuduj-usb.sh` składa miejsce A: FAT z k1.efi + obraz; budowa
   przerywa z jasnym komunikatem, gdy się nie mieszczą (dziś zapas 2,5 MiB).
2. **W rdzeniu zostaje skrót obrazu strefy** — `/rdzen/strefy/przegladarka.skrot`: skrót główny dm-verity, liczba bloków
   danych, rozmiar całości (`k1_obraz_strefy.h`). Łańcuch zaufania: Secure Boot → podpisany k1.efi → linia poleceń
   (skrót rdzenia) → rdzeń (dm-verity) → skrót obrazu strefy → każdy blok obrazu (dm-verity w jądrze). Obraz jest
   związany z tym jednym wydaniem: obraz innej wersji albo ze zmienionym bajtem nie przechodzi sprawdzenia (fail closed).
3. **Nadzorca szuka obrazu i zakłada dm-verity** (`src/nadzorca/obraz_strefy.c`, wspólny kod z initramfs
   `src/wspolne/k1_verity.c`): partycje K1-EFI — najpierw dyski wewnętrzne, potem pendrive'y — miejsce, którego manifest
   ma identyfikator tego wydania (najpierw to, które GRUB uruchomił); w testach QEMU (rdzeń na `/dev/vda`) — dysk virtio
   dokładnie wielkości obrazu. Miejsce nie jest zaufaniem: liczy się tylko zgodność ze skrótem z rdzenia. Urządzenie
   powstaje raz (raz po pulpicie, wpis w logu), przed każdym startem strefy nadzorca czyta jego pierwszy blok — gdy nie
   może (wyjęty pendrive), szuka od nowa. VMM dostaje deskryptor jak dotąd (fd 6, tylko do odczytu) — zmiana w VMM:
   rozmiar przez `lseek` (urządzenie blokowe nie ma `st_size`). Brak obrazu → `K1_STREFA_BRAK_OBRAZU`, okienko
   „Przeglądarka nie może ruszyć”; K1 działa dalej.
4. **Format bez zmian: xz z blokiem 1 MiB.** Szybsze formaty (zstd +11 % do +21 %) nie mieszczą się w miejscu 128 MiB
   razem z k1.efi. Szybszy format — razem z większymi miejscami A/B (punkt „Czego ta decyzja nie daje”).
5. **Aktualizacje bez zmian formatów.** Manifest miejsca obejmuje obraz (BLAKE2b całego obrazu miejsca), paczka niesie
   obraz miejsca jak dotąd, poczekalnia i program zapisujący — bez zmian. Dlatego aktualizacja z 0.10.7 (przez sieć
   i z pendrive'a) działa starym programem zapisującym, a „Wróć do poprzedniej wersji” wraca też do starego obrazu.
   Strefa pobierania: wersja sprzed kroku 4 bierze obraz z rdzenia, nowa — z miejsca A/B.

## Skutki (zmierzone przy budowie)
- Rdzeń: 115,3 → 5,8 MB — tyle pamięci K1 zyskuje na stałe (ok. 109 MB; ważne na IdeaPadzie z 4 GB).
- k1.efi: 122,7 → 18,2 MiB (GRUB wczytuje mniej przy starcie). Miejsce A: k1.efi 20 MiB + obraz 104 MiB, zapas 2,5 MiB.
- ISO: 137 → 27 MB, ale **bez obrazu przeglądarki**: jądro K1 nie ma napędu CD, więc K1 uruchomiony z ISO (maszyna
  wirtualna) mówi „Przeglądarka nie może ruszyć” (zwykle i tak „brak wirtualizacji” — strefa w maszynie wirtualnej
  wymaga zagnieżdżonej wirtualizacji). Obraz USB i paczka aktualizacji — bez zmian rozmiaru.
- Pierwszy start przeglądarki po włączeniu komputera czyta obraz z nośnika (pendrive USB 2.0 na X230 — wolniej niż dotąd
  z pamięci), następne — z pamięci podręcznej jądra (odzyskiwalnej, gdy pamięci brak). Liczy się ona do klatki strefy,
  która pierwsza czytała.
- Urządzenie dm-verity obrazu trzyma nadzorca (do odczytu); test w systemie tego pilnuje (`t_sprzet`: dyski i partycje
  ma otwarte tylko skarbiec, nadzorca — tylko obraz strefy `k1-strefa-*`, tylko do odczytu).

## Czego ta decyzja nie daje (decyzja właściciela D10 planu 0.10.9 — podjęta 2026-11-20: „A”, niżej „Uzupełnienie”)
Miejsce A/B 128 MiB jest znowu prawie pełne: nie ma w nim miejsca na szybszy format ani na FFmpeg (krok 3) i sterownik
karty w strefie (krok 6). Potrzebne są większe miejsca A/B, a dyski z 0.10.x mają na K1 tylko 512 MiB. Możliwości:
(a) większa partycja K1-EFI i większe miejsca dla nowych instalacji i pendrive'ów (pole „slot bytes” bloku układu już
jest), a laptopy instalowane od nowa — dane w skarbcu laptopa giną; (b) przeprowadzka bez utraty danych: K1 zmniejsza
K1-DANE od końca (tylko gdy koniec jest wolny) i dokłada partycję na obrazy stref — osobny, duży krok z ryzykiem dla
danych; (c) zostać przy 128 MiB: własny, mały FFmpeg (T5 B), bez Mesy — bez szybkiej grafiki.

## Odrzucone
- Obraz w skarbcu (obiekty jak dyski sesji): skarbiec powstaje dopiero przy pierwszym starcie z hasłem, obraz musiałby
  przyjść na dysk inną drogą; każdy odczyt przez usługę skarbca — wolno.
- Dwa osobne miejsca na obrazy za FAT-em K1-EFI (zamiast poczekalni): bez poczekalni aktualizacja przez sieć nadpisywałaby
  poprzednią wersję („Wróć”); zysk wobec miejsca A/B — kilka MiB.
- Obraz z GRUB-a jako drugi initramfs: znowu w pamięci, a dołączany initramfs mógłby podmienić `/init` przed sprawdzeniem
  rdzenia.

## Uzupełnienie (2026-11-20): krok 4b — większe miejsca A/B
Właściciel wybrał D10 „A”: więcej miejsca na system, z zapasem („zajmij sobie więcej miejsca, na zapas”); laptopy instaluje
od nowa. Wdrożone w tym samym planie (krok 4b):
1. **Rozmiar miejsca czytany z bloku układu** (pole „slot bytes” było w formacie od 0.7.0): każdy pełny MiB od 128 MiB
   do 2 GiB; manifest miejsca w jego ostatnim MiB (`k1_slot_man_off`, `k1_slot_start_off` zamiast stałego 127. MiB).
   Stare dyski (128 MiB) dalej są rozpoznawane — Instalator i nadzorca widzą na nich K1.
2. **Nowe pendrive'y i instalacje: miejsca po 768 MiB**, K1-EFI 1600 MiB (dwa miejsca + 64 MiB na pliki startowe, bo
   aktualizacja pisze nowe obok starych), partycja K1-EFI na dysku 3 GiB (za FAT-em poczekalnia na paczkę do ok. 800 MiB).
   Dysk do instalacji: co najmniej 4 GB; pendrive: co najmniej 2 GB (obraz `.img` ma 1,7 GB, `.img.xz` do pobrania ok.
   146 MB — puste miejsce B to zera).
3. **Aktualizacja wymaga tego samego rozmiaru miejsc na dysku i w wydaniu**: inaczej program zapisujący odmawia przed
   jakimkolwiek zapisem (`K1_INST_BLAD_UKLAD`: „Na dysku jest starszy układ K1 … Zainstaluj K1 od nowa”). K1 0.10.7
   i starsze odrzucają paczkę nowego układu same (ich parser zna tylko 128 MiB) — fail closed, nic się nie zmienia.
   Paczka nie dostała nowego pola: rozmiar miejsc to połowa odstępu między przodem a tyłem (sprawdzany).
4. **Szybszy format obrazu przeglądarki: SquashFS zstd z blokami 128 KiB** (126 MiB z drzewem; xz 1 MiB — 104 MiB). Do
   przeczytania 4 KiB biblioteki strefa rozpakowuje 128 KiB szybkim dekoderem zamiast 1 MiB wolnym xz. lz4 odrzucony:
   o 58 % większy, a na pendrivie USB 2.0 (X230) pierwszy start jest ograniczony odczytem z nośnika. Jądro strefy bez XZ.
5. Miejsce A: zajęte 146 MiB (k1.efi 20 + obraz 126), **wolne 620 MiB** — na FFmpeg (krok 3), sterownik karty w strefie
   (krok 6) i wzrost Firefoksa. Czasu startu przeglądarki dalej nie da się zmierzyć w kontenerze ani w CI — mierzy
   właściciel na laptopach (DO_ZROBIENIA, punkt 4).
