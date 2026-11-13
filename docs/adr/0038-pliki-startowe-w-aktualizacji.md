# 0038. Pliki startowe w aktualizacji: manifest plików startowych, nowe obok starych, jedno przełączenie

- Status: Przyjęty, wdrożony w 0.9.0 (krok 3 planu 0.9.0)
- Data: 2026-11-13
- Dotyczy: ADR 0021 (manifest i podpis), ADR 0022 (A/B, K1-EFI format 2), ADR 0018 (instalator); RYZYKA R45

## Kontekst
Aktualizacja A/B (ADR 0022) zapisuje tylko wolne miejsce na system i grubenv. **Pliki startowe** na dysku — shim,
GRUB, MokManager, `grub.cfg`, ekran startowy, certyfikat K1, `.disk/info` — zostają takie, jakie były przy instalacji
(albo przy pierwszej aktualizacji do 0.7.0). Gdyby producent firmware unieważnił stary shim (aktualizacja `dbx`/SBAT),
komputer przestałby startować, a jedyną drogą byłoby „Zainstaluj od nowa”, które usuwa dane (RYZYKA R45).

Drugi problem: pliki startowe sprawdzał dotąd tylko manifest formatu 1 (47 MiB w partycji K1-EFI pendrive'a), zostawiony
na czas aktualizacji z 0.6.2–0.6.3 — rozwiązanie tymczasowe z terminem 0.9.0. Obejmował surowe bajty pierwszych 47 MiB
partycji, a pliki startowe leżą za miejscami A i B (od ok. 257 MiB) — więc w praktyce sprawdzał tablice FAT i katalogi,
nie treść shima i GRUB-a.

Pliki startowe to zwykłe pliki w FAT32. Nowy shim albo GRUB ma inny rozmiar, więc zmienia się tablica FAT i katalogi;
zapis „w miejscu” przerwany zanikiem prądu zostawiłby dysk, z którego firmware nie wystartuje.

## Decyzja
**1. Manifest plików startowych (format 3, `k1_manifest.h`).** Ten sam 512-bajtowy układ co manifest miejsca (format 2):
wersja, identyfikator wydania, podpis EdDSA tym samym kluczem (`K1_KLUCZ_PODPISU`). Skrót to **skrót drzewa plików**
(`k1_fat32_skrot`, `src/wspolne/k1_fat32.c`): dla każdego pliku i folderu jego miejsce w drzewie, rodzaj i rozmiar,
bajty folderów bez numerów klastrów, treść plików — **bez treści grubenv i miejsc A i B** (zmieniają się bez zmiany
plików startowych; muszą być w drzewie dokładnie raz i w jednym kawałku, tam gdzie mówi blok układu). Skrót nie zależy
od tego, w których klastrach leżą pliki — ten sam zestaw plików daje ten sam skrót na pendrivie i na dysku.
Manifest leży w miejscu A, zaraz za manifestem miejsca (`K1_SLOT_START_OFF` = 127 MiB + 512 B); pisze go
`narzedzia/podpisz.c start` po bloku układu (`scripts/zbuduj-usb.sh`). Ma tę samą wersję i identyfikator wydania co
manifest miejsca obok — Instalator odmawia, gdy się różnią.
Dlaczego osobny manifest, a nie pole w manifeście miejsca: starsze K1 (0.7.0–0.8.7) czytają manifest miejsca A/B na
dysku i odrzucają nieznane pola — dopisanie skrótu sprawiłoby, że stary Instalator widziałby na dysku „brak wersji”
i pozwoliłby wrócić do starszej.

**2. Pendrive sprawdzany przed jakimkolwiek zapisem** (instalacja i aktualizacja): manifest miejsca (podpis, skrót
systemu), manifest plików startowych (podpis, to samo wydanie, ten sam rozmiar FAT), skrót drzewa plików pendrive'a.
Niezgodność = `K1_INST_BLAD_PODPIS`, dysk nietknięty.

**3. Instalacja** kopiuje K1-EFI pendrive'a **bez miejsca B** (pustego, nieobjętego żadnym manifestem), grubenv pisze
od nowa (miejsce A), zeruje manifesty miejsca B, a po zapisie **liczy skrót drzewa na dysku** — musi być taki jak
w manifeście (sprawdzone jest to, co zostało na dysku, nie to, co pendrive podał drugi raz).

**4. Aktualizacja: nowe obok starych, jedno przełączenie** (`src/instalator/start.c`), przed zapisem systemu:
1. Drzewo dysku i jego skrót; taki jak w manifeście → nic do zrobienia („pliki startowe na dysku aktualne”).
2. Każdy plik i folder pendrive'a (poza grubenv i miejscami) trafia do **klastrów, których drzewo dysku nie używa**;
   numery klastrów w folderach i wpisy FAT dla nowych klastrów są przepisane. Stare drzewo się nie zmienia — wpisy FAT
   starego drzewa w każdym zapisanym bloku zostają te same.
3. Nowe drzewo czytane z dysku od nowego korzenia: skrót musi być taki jak w manifeście, inaczej nie ma przełączenia.
4. **Przełączenie: jeden zapis sektora startowego FAT32** — pole „pierwszy klaster folderu głównego” (BPB_RootClus)
   wskazuje nowy korzeń. Potem kopia sektora startowego i FSInfo (liczba wolnych: nieznana).
5. Sprzątanie: wpisy FAT klastrów spoza nowego drzewa → wolne (druga kopia FAT wyrównana z pierwszą).

Zanik prądu przed krokiem 4 zostawia stare pliki startowe (nowe klastry są „zagubione”, nikt ich nie czyta), po nim —
nowe. Co jest wolne, wynika z drzewa, nie z wpisów FAT, więc następna aktualizacja zużyje zagubione klastry i posprząta
FAT (także gdy pliki są już aktualne). Grubenv i miejsca zostają w tych samych klastrach — piszą je w miejscu GRUB
(`save_env`), nadzorca (`rozruch.c`) i Instalator (A/B).
Gdy K1-EFI dysku ma inny układ (inny FAT albo inne położenie miejsc), jej FAT jest nieczytelny albo zabrakło miejsca
obok starych plików — pliki startowe **zostają stare**, wpis w logu, aktualizacja systemu idzie dalej (to stan sprzed
0.9.0, nie gorszy). Błąd zapisu albo różnica przy odczycie — odmowa całej aktualizacji.
Miejsce: K1-EFI ma 280 MiB, miejsca 256 MiB, pliki startowe ok. 4 MiB — wolnego jest ok. 19 MiB, starczy na drugi zestaw.
Plik, którego K1 nie położył (np. `NvVars`, który OVMF w QEMU bez pliku zmiennych zapisuje na partycję EFI przy każdym
starcie), sprawia, że drzewo dysku różni się od pendrive'a — wymiana go usuwa (K1-EFI należy tylko do K1).

**5. Miejsce na dysku niesie oba manifesty swojego wydania**: aktualizacja A/B kopiuje 1 KiB (manifest miejsca
i manifest plików startowych). Nikt nie czyta manifestu plików startowych z dysku — sprawdzany jest zawsze pendrive.

**6. Koniec formatu 1 (rozwiązanie tymczasowe 0.9.0):** pendrive nie ma już manifestu na 47 MiB (`podpisz obraz`,
`scripts/zbuduj-usb.sh`), `k1_efi_czytaj` nie rozpoznaje dysku 0.6.2–0.6.3 (bez bloku układu = nic), `k1_manifest_czytaj`
odrzuca format 1. Dysk bez bloku układu (K1 starszy niż 0.7.0) albo z nieczytelnym grubenv dostaje całą K1-EFI tak jak
przy instalacji (bez miejsca B, sprawdzoną skrótem) — dane zostają. Stary Instalator 0.6.2–0.6.3 odmówi pendrive'a 0.9.0
(nie znajdzie manifestu) — taki komputer najpierw na 0.8.x albo „Zainstaluj od nowa”.

## Konsekwencje
- Producent unieważni stary shim → aktualizacja z pendrive'a z nowszym shimem wymieni go na dysku bez utraty danych
  (R45 zamknięte; zostaje wyjątek: dysk z innym układem K1-EFI — dziś każdy dysk od 0.7.0 ma ten sam).
- Instalator parsuje FAT32 dysku (dotąd tylko kopiował bajty): `k1_fat32.c` sprawdza każdą liczbę, łańcuch klastrów
  kończy się tam, gdzie mówi rozmiar, żaden klaster nie leży w dwóch miejscach (pętla = błąd), najwyżej 128 plików —
  fuzzing `fuzz_fat32` (także sama wymiana w pamięci). Program zapisujący dostaje więcej pamięci statycznej (ok. 2,5 MiB:
  mapy klastrów) — mieści się w 16 MiB klatki, bez nowych wywołań jądra.
- Pomiar odporności: test na hoście przerywa wymianę przed każdym zapisem po kolei (`test_start`) — na dysku zawsze
  stare albo nowe pliki, następne uruchomienie kończy wymianę z czystym FAT; wynik czytają mtools i `fsck.fat` (narzędzie
  testów: `dosfstools`). W QEMU (`test-m5f`): stare pliki startowe na dysku → aktualizacja je wymienia, OVMF startuje
  z nowych; pendrive ze zmienionym plikiem startowym → odmowa; aktualizacja przerwana w trakcie → dysk dalej startuje.
- Zakładamy, że zapis jednego sektora jest niepodzielny (sektor startowy) — jak każdy system plików bez dziennika.
