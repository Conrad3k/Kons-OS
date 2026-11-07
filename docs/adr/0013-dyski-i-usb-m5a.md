# 0013. M5a: sterowniki dysków i USB; pierwszy laptop — ThinkPad X230

- Status: Przyjęty
- Data: 2026-11-07
- Dotyczy: SPEC §3 (jądro), §7 (wejście, nadzorca), ADR 0003, ADR 0010, ADR 0011 (M5), etap M5a

## Kontekst
M5 (dane i sprzęt, ADR 0011) zaczyna się od jądra: bez sterowników dysków nie ma gdzie zapisać
danych, bez USB nie działają klawiatury i myszy USB ani pendrive'y. Rozpoznanie:
`docs/M5-ROZPOZNANIE.txt` (nazwy opcji sprawdzone w Kconfig 6.18.60).

Właściciel (2026-11-07): **jedno konto na komputer** i **pierwszy laptop: Lenovo ThinkPad X230,
8 GB RAM**. Co to oznacza (dane producenta i budowa platformy Intel 7. generacji; na samym sprzęcie
jeszcze niesprawdzone):
- chipset Intel QM77: dysk SATA (2,5″ i mSATA) przez **AHCI** — tryb „AHCI” w BIOS-ie (domyślny);
  **brak NVMe**;
- USB: 2 porty USB 3.0 (kontroler **xHCI**, który przy starcie przejmuje porty od EHCI —
  `drivers/usb/host/xhci-pci.c`, `usb_enable_intel_xhci_ports`) i port USB 2.0 oraz urządzenia
  wewnętrzne (kamera, czytnik linii papilarnych, Bluetooth) na **EHCI**;
- klawiatura, TrackPoint i touchpad przez **PS/2 (i8042)** — mamy od M2; nie trzeba I2C-HID;
- grafika Intel HD 4000: obraz z firmware (UEFI GOP albo VESA) → `simpledrm`; ekran 1366×768;
- procesor Ivy Bridge (x86-64 z AES-NI, bez AVX2) — nasze programy są budowane dla podstawowego
  x86-64 (bez `-march`), jądro ogólne;
- TPM w wersji 1.2 (nie 2.0) — ważne dla planu kluczy (M5e, M10).
VirtualBox: dysk SATA (AHCI) lub NVMe, USB 1.1 (OHCI) bez Extension Packa, xHCI/EHCI z nim.
QEMU q35 ma wbudowany kontroler AHCI; `qemu-xhci`, `usb-ehci`, `pci-ohci`, `nvme` — do testów.

## Decyzja
1. **Dyski:** `ATA` + `SATA_AHCI`, `SCSI` + `BLK_DEV_SD` (SATA i pamięci USB to w Linuksie dyski
   SCSI, `sd*`), `BLK_DEV_NVME`. Bez napędów CD (`BLK_DEV_SR`), bez `/dev/sg*` i bsg (surowe komendy
   SCSI), bez port multiplierów, starego IDE (`ATA_SFF`) i rozszerzeń ATA/ACPI.
2. **Tablice partycji: tylko GPT** (`PARTITION_ADVANCED` + `EFI_PARTITION`). Parser MBR i inne formaty
   wyłączone — mniej kodu jądra czytającego obce nośniki (stary MBR widać jako cały dysk; nasz kod
   w M5d sam czyta pierwszy sektor, żeby nie nadpisać cudzego dysku). Partycja danych K1 będzie
   rozpoznawana po nazwie GPT „K1-DANE” (sysfs `PARTNAME`, sprawdzone w teście).
3. **USB:** kontrolery xHCI, EHCI (wymagany przez X230) i OHCI (VirtualBox bez rozszerzeń); UHCI
   (stare chipsety Intela sprzed ~2011) — nie. Urządzenia: **tylko HID generic** (klawiatury, myszy)
   i **mass storage** (pendrive'y). Bez sterowników producentów, bez `hidraw`/`hiddev` (surowy
   dostęp do urządzeń HID), bez UAS. Ogłaszanie nowych urządzeń w logu jądra wyłączone.
4. **Podłączanie w trakcie pracy (wstęp do M5b):** nadzorca co 2 s sprawdza nowe urządzenia
   wejścia (od 0.5.1) i teraz także **zapomina odłączone** (węzeł zniknął) — urządzenie podłączone
   ponownie pod tym samym numerem jest zauważane. Pełny hotplug bez restartu `wejscie` (zdarzenia
   jądra) — M5b.
5. **Touchpad i TrackPoint X230:** zostają w podstawowym trybie PS/2 (bez rozszerzeń Synaptics
   i TrackPoint w jądrze): ruch i przyciski działają, przewijanie dwoma palcami — nie. Tryb
   rozszerzony daje pozycję bezwzględną (EV_ABS), którą `wejscie` pozna dopiero w M5b (tablet USB).
6. **Grafika X230:** na razie `simpledrm`; GRUB próbuje kolejno 1280×800, 1366×768, … — firmware
   X230 (ekran 1366×768) nie oferuje 1280×800, więc powinien wybrać natywne 1366×768. 1366×768 nie
   może być pierwsze: w QEMU (UEFI, karta emulowana) obraz w tym trybie jest krzywy (karta zaokrągla
   szerokość do 1360 — sprawdzone testem M4 na ISO). Sterownik `i915` (duży, nie do sprawdzenia w CI) — dopiero jeśli pierwszy test
   na laptopie pokaże, że jest potrzebny (zła rozdzielczość, czarny ekran).
7. **Nikt poza rootem** nie ma dostępu do dysków i surowego USB (węzły 0600 root, test); dysk otworzy
   tylko nadzorca i przekaże jednej usłudze (M5d, wzór: karta graficzna → ekran).

## Konsekwencje
- Jądro: +~0,5 MB (4,8 MB). Nowa powierzchnia ataku: USB i HID (urządzenie podłączone z zewnątrz
  może podawać złośliwe deskryptory), mass storage + parser GPT obcych nośników (RYZYKA R33–R35).
- Każde urządzenie USB jest autoryzowane od razu (`USB_DEFAULT_AUTHORIZATION_MODE=1`): „klawiatura”
  z pendrive'a może pisać (BadUSB). Bez tego nie zadziała zwykła klawiatura USB — świadomy wybór
  do czasu zaufanego okna zgody (RYZYKA R33).
- Test w systemie (`t_sprzet.c`): dyski SATA/NVMe/USB, GPT „K1-DANE”, MBR nieczytany, kontrolery,
  urządzenia wejścia USB, uprawnienia węzłów, nikt nie trzyma dysku. Test z zewnątrz
  (`scripts/test-m5.sh`): maszyna bez PS/2 — klawiatura i mysz USB, podłączanie i odłączanie myszy.
- X230 nie jest sprawdzany w CI — pierwszy test robi właściciel (README: pendrive, ustawienia BIOS-u).

## Rozważane alternatywy
- **MBR (`MSDOS_PARTITION`) włączony** — wygodne dla obcych pendrive'ów, ale K1 nie montuje obcych
  systemów plików (pliki z zewnątrz — Gatekeeper, M9); zbędny parser.
- **UAS** — szybsze dyski USB 3, ale znany z błędów i obejść; mass storage wystarcza.
- **`i915` od razu** — lepsza rozdzielczość i jasność ekranu, ale ~3 MB kodu jądra i duży interfejs
  ioctl bez możliwości testu w QEMU; poczekamy na wynik pierwszego startu.
- **Rozszerzenia Synaptics/TrackPoint** — przewijanie, gesty; wymagają obsługi pozycji bezwzględnej
  w `wejscie` (M5b) — wrócimy po testach na X230.
- **USB z autoryzacją ręczną** (`USB_DEFAULT_AUTHORIZATION_MODE=0`) — bezpieczniej, ale bez okna
  zgody w K1 nie działałaby żadna klawiatura USB.
