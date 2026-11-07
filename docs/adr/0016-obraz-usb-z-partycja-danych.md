# 0016. Obraz na pendrive z partycją danych; usługa skarbiec w łańcuchu startu

- Status: Przyjęty
- Data: 2026-11-07
- Dotyczy: ADR 0010 (ISO), ADR 0013, ADR 0015, SPEC §5, etap M5d

## Kontekst
Właściciel chce danych na pendrivie na ThinkPadzie X230 (dysk laptopa nietknięty). ISO nagrane na
pendrive jest tylko do odczytu i nie ma miejsca na dane. Skarbiec (ADR 0015) potrzebuje partycji
GPT „K1-DANE”. Pendrive pojawia się w systemie dopiero ~1 s po wykryciu (usb-storage czeka, aż
urządzenie się ustabilizuje), a `/proc` w K1 nie pokazuje linii poleceń (`subset=pid`).

## Decyzja
1. Drugi produkt budowy: **`k1-<wersja>-usb.img.xz`** (`scripts/zbuduj-usb.sh`): GPT ze stałymi
   GUID-ami, partycja „K1-EFI” (FAT, 48 MiB: program startowy UEFI — pkt 4 — jądro, initramfs,
   rdzeń w initramfs) i **„K1-DANE”** (64 MiB zer). Tylko UEFI.
   Nagrywanie: Rufus (tryb DD) albo balenaEtcher. ISO zostaje dla maszyn wirtualnych.
2. Nadzorca uruchamia **skarbiec przed ekranem**; szuka dokładnie jednej partycji „K1-DANE” (sysfs
   `PARTNAME`), czeka na nią do 4 s po starcie, a gdy sterownik usb-storage ma podłączone urządzenie —
   do 15 s. Dwie takie partycje → żadna (dane w pamięci, wpis w logu).
3. Ekran (ustawienia) i Notatki (notatka) są klientami skarbca; Notatki tracą dostęp do plików.
4. **Program startowy UEFI (ISO i obraz USB): podpisane pliki z Ubuntu** — shim (Microsoft) jako
   `\EFI\BOOT\BOOTX64.EFI` i GRUB dla nośników wymiennych (`gcdx64.efi`, Canonical) jako `grubx64.efi`;
   znajduje nośnik po `/.disk/info`. Powód: Windows Defender oznaczył ISO 0.5.5 (z własnym,
   niepodpisanym GRUB-em) heurystyką jako trojana. ISO składamy sami przez `xorriso` (bez
   `grub-mkrescue`), obrazy FAT tworzone deterministycznie — **całe ISO i obraz USB są powtarzalne**.
   Jądro K1 nie jest podpisane: z włączonym Secure Boot podpisany GRUB je odrzuci (do M10).

## Konsekwencje
- Na X230: start z pendrive'a przez UEFI (Secure Boot wyłączony), dane zaszyfrowane na tym samym
  pendrivie; 64 MiB wystarcza na ustawienia i notatki (powiększanie partycji do końca nośnika —
  później).
- Start bez partycji danych (ISO, VM bez drugiego dysku) jest dłuższy o ≤ 4 s (oczekiwanie).
- ISO i obraz USB powtarzalne w całości (RYZYKA R37); pliki shim/GRUB pochodzą z archiwum Ubuntu
  (sprawdzane przez apt) — nie budujemy ich, więc ich powtarzalność to wersja pakietu.

## Rozważane alternatywy
- **Partycja na danych dopisywana przez K1 do pendrive'a z ISO** — zapis tablicy partycji na nośniku
  startowym, trudne do bezpiecznego rozpoznania; odrzucone.
- **Parametr linii poleceń „czekaj na dane”** — `/proc/cmdline` niewidoczne w K1 (`subset=pid`).
- **BIOS w obrazie USB** — X230 ma UEFI; GRUB i386-pc wymaga osobnej partycji i instalacji
  w sektorze rozruchowym; niepotrzebne teraz.
