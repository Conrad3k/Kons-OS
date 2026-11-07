# 0018. Instalator: K1 na dysku komputera (M5f)

- Status: Przyjęty
- Data: 2026-11-07
- Dotyczy: ADR 0016 (obraz USB, „dysk laptopa nietknięty” — **zmienione**), ADR 0015, ADR 0007 (klatki),
  SPEC §6 (start, aktualizacje), etap M5f

## Kontekst
Właściciel (2026-11-07): „co gdyby zrobić instalator, który instalowałby i system, i miał miejsce na
pliki na głównym dysku? Na tym laptopie i tak nic nie mam” oraz zgoda: „instalator może usuwać inne
dane na dysku urządzenia”. Do 0.5.8 K1 działał tylko z pendrive'a (64 MiB na dane, wolne USB 2,
pendrive można zgubić razem z danymi). Instalator to pierwszy program K1, który może zniszczyć dane
— musi być najmniej uprzywilejowany i nie do uruchomienia „przypadkiem”.

## Decyzja
1. **Dwa programy.** Okno **Instalator** (`/rdzen/aplikacje/instalacja`, UID 204, klatka aplikacji) nie
   ma żadnych urządzeń: pyta nadzorcę o listę dysków (`INST_DYSKI`), pokazuje ją, przyjmuje
   potwierdzenie i wysyła `INST_START`. Zapisuje **osobny program** `instalator`
   (`/rdzen/system/instalator`, UID 105): bez okna, seccomp = baza + `pread64`, `pwrite64`,
   `fdatasync`, `getrandom`, Landlock bez ścieżek, jedno zadanie i koniec.
2. **Nadzorca otwiera urządzenia, nie programy.** Instalacja: źródło = partycja „K1-EFI” pendrive'a
   K1 (dokładnie jednego, na USB; `O_RDONLY`), cel = **cały** dysk wewnętrzny (`O_RDWR|O_EXCL` — nie
   da się, gdy cokolwiek na nim jest otwarte, np. dane działającego systemu). Aktualizacja: cel = tylko
   partycja „K1-EFI” dysku — **partycja danych nigdy nie trafia do programu zapisującego**. Dysk musi
   mieć ten sam rozmiar co na liście, którą widziało okno.
3. **Potwierdzenie** przed wyczyszczeniem dysku: wpisanie „USUŃ” (dowolna wielkość liter, także bez
   polskiej litery). Aktualizacja — jeden przycisk (dane zostają). Klawisze ma tylko okno z fokusem
   (ekran), więc potwierdzić może tylko ktoś przy klawiaturze.
4. **Układ dysku:** GPT (128 wpisów, obie kopie, ochronny MBR), wyrównanie do 1 MiB:
   „K1-EFI” 512 MiB (EFI System; miejsce na większy system i A/B w M10) + „K1-DANE” do końca dysku.
   GUID-y losowe. **Kolejność zapisu:** wyczyszczenie starych tablic (pierwszy i ostatni MiB) →
   kopia systemu → odczyt i porównanie → zerowanie pierwszego MiB „K1-DANE” (skarbiec założy magazyn
   przy pierwszym starcie) → **tablica partycji na końcu**. Przerwana instalacja zostawia dysk bez
   tablicy — nigdy pół-działający system.
5. **Start z dysku:** firmware UEFI ładuje `\EFI\BOOT\BOOTX64.EFI` (ścieżka domyślna — bez wpisów w
   NVRAM, których zapis na ThinkPadach bywa ryzykowny). GRUB (USB i dysk) bierze jądro z **partycji, z
   której sam wystartował** (`$cmdpath`) — z podłączonym pendrive'em K1 nie pomyli nośników.
6. **Które dane:** nadzorca daje skarbcowi partycję „K1-DANE”, gdy jest jedna, albo — gdy są dwie —
   tę z **dysku wewnętrznym** (nie USB). Zainstalowany K1 ma swoje dane także przy starcie z pendrive'a
   (aktualizacja); dane z pendrive'a są wtedy pomijane (wpis w logu).
7. Nowe komunikaty (32 B, sprawdzane w całości): `INST_DYSKI`, `INST_START` (okno → nadzorca),
   `INST_DYSK`, `INST_LISTA`, `INST_POSTEP`, `INST_WYNIK` (nadzorca → okno), `INST_ZADANIE`
   (nadzorca → instalator), `INST_ETAP`, `INST_GOTOWE` (instalator → nadzorca). Nadawcy: UID 0 / 204 /
   105, sprawdzane przez `SO_PASSCRED`. `WYLACZ` od okna — tylko po udanej instalacji.

## Konsekwencje
- ADR 0016 pkt „dysk laptopa nietknięty” przestaje być zasadą: dysk zmienia się **tylko** przez
  Instalator, po potwierdzeniu. Bez instalacji wszystko działa jak w 0.5.8.
- Aktualizacja = start z nowszego pendrive'a + „Zaktualizuj system”. Automatyczne aktualizacje,
  A/B i podpisy — M10 (partycja 512 MiB ma na to miejsce).
- Hasło: dane na dysku laptopa bez hasła są czytelne dla złodzieja laptopa — Instalator i README
  zalecają włączenie hasła (RYZYKA R41).
- Tylko UEFI (X230, QEMU OVMF); BIOS dla instalacji — niepotrzebny teraz.
- Partycja danych na pendrivie nadal ma 64 MiB; na dysku — cały dysk (magazyn czyta tylko swój
  dziennik, więc rozmiar partycji nie zwiększa pamięci skarbca).

## Rozważane alternatywy
- **Zapis przez samego nadzorcę** (bez osobnego programu) — duży kod parsujący i zapisujący w
  procesie z pełnymi uprawnieniami; odrzucone.
- **Kopiowanie plików systemu zamiast całej partycji** — wymaga sterownika FAT w K1; kopia partycji
  bajt w bajt jest prostsza i daje ten sam, sprawdzalny obraz co wydanie.
- **Wpis rozruchowy w NVRAM (efivarfs)** — znane problemy firmware ThinkPadów z zapisem zmiennych;
  ścieżka domyślna wystarcza.
- **Instalacja obok innego systemu (dzielenie dysku)** — właściciel nie potrzebuje; większe ryzyko
  pomyłki; odrzucone na teraz.
