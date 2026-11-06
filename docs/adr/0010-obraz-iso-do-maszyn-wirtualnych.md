# 0010. Obraz ISO do maszyn wirtualnych i grafika z firmware

- Status: Przyjęty (tymczasowy do M10)
- Data: 2026-11-06
- Dotyczy: SPEC §6 (start, UKI, Secure Boot — M10), ADR 0004 (initramfs), ADR 0006 (grafika)

## Kontekst
Właściciel chce pierwszy raz uruchomić K1 w maszynie wirtualnej (np. VirtualBox na swoim komputerze).
Dotąd K1 startował tylko w QEMU z jądrem podanym bezpośrednio (`-kernel`) i dyskiem virtio, a obraz
wyświetlał wyłącznie sterownik `bochs` (karta QEMU). Typowe maszyny wirtualne uruchamiają płytę
(ISO) przez BIOS albo UEFI i mają inne karty graficzne (VMware SVGA, VirtualBox VGA).
Sprawdzone w źródłach 6.18.60: `SYSFB_SIMPLEFB` oznacza bufor ramki z firmware (UEFI GOP albo VESA
z programu ładującego) jako „simple-framebuffer”, który obsługuje `DRM_SIMPLEDRM` (z `page_flip`
przez pomocnika atomowego); sterownik karty (np. `bochs`) przejmuje urządzenie od simpledrm
(aperture helpers). `BLK_DEV_LOOP` + `LOOP_CONFIGURE` (include/uapi/linux/loop.h) montuje plik jako
urządzenie blokowe; `LO_FLAGS_AUTOCLEAR` odłącza je, gdy zamknie się ostatni użytkownik.

## Decyzja
- **`scripts/zbuduj-iso.sh`** składa `build/wynik/k1-<wersja>.iso` programem `grub-mkrescue`
  (GRUB 2.12 z Ubuntu): start z UEFI i z BIOS-u, bez menu. GRUB ustawia tryb graficzny
  (`gfxpayload=keep`), ładuje jądro, `initramfs.cpio` i drugi archiwum `rdzen.cpio` z plikiem
  `/rdzen.squashfs`.
- **`k1.rdzen=wbudowany`**: `k1-initramfs` podpina `/rdzen.squashfs` do wolnego urządzenia loop
  (tylko do odczytu, auto-odłączanie), montuje i dalej jak dotąd. Obraz nie potrzebuje sterowników
  dysków ani napędów CD — działa w każdej maszynie, która uruchomi płytę.
- **Jądro:** `DRM_SIMPLEDRM` + `SYSFB_SIMPLEFB` (grafika z firmware), `BLK_DEV_LOOP`
  (bez wstępnie tworzonych urządzeń), `RTC_DRV_CMOS` + `RTC_HCTOSYS` (godzina). `nadzorca` szuka
  karty `card0`…`card7` (z simpledrm i innym sterownikiem numer bywa inny).
- **Testy:** `scripts/test-m4.sh` uruchamia ISO w QEMU przez UEFI (OVMF) i przez BIOS (SeaBIOS)
  z kartą `virtio-vga`, dla której K1 nie ma sterownika — obraz pokazuje simpledrm, tak jak
  w VirtualBoxie/VMware.

## Konsekwencje
- K1 da się uruchomić w maszynie wirtualnej (VirtualBox, VMware, QEMU/virt-manager), z klawiaturą
  i myszą PS/2. Rozdzielczość = ta ustawiona przez firmware/GRUB (preferowana 1280×800).
- **GRUB jest w łańcuchu startu obrazu ISO** — duży program spoza bazy zaufania, bez podpisów
  i Secure Boot (RYZYKA R25). To obraz do testów; w M10 zastąpi go podpisany UKI.
- Obraz ISO nie jest objęty sprawdzeniem powtarzalności (xorriso/GRUB); pliki wewnątrz
  (jądro, initramfs, rdzeń) są powtarzalne.
- Brak jeszcze: USB (klawiatury/myszy/tablety USB), dysków NVMe/SATA, Hyper-V (syntetyczne
  urządzenia) — przy testach na prawdziwym sprzęcie.

## Rozważane alternatywy
- Jądro z wbudowanym initramfs i linią poleceń jako jedyny plik EFI — działa tylko z UEFI
  i wymaga drugiej kompilacji jądra; GRUB daje UEFI i BIOS od razu.
- Sterowniki ATA/AHCI + ISO9660 w jądrze i odczyt obrazu z płyty — więcej kodu jądra (powierzchnia
  ataku) bez potrzeby.
- systemd-boot — zewnętrzny program, tylko UEFI.
