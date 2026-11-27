# 0022. A/B i Secure Boot (M6c, M6d): zamknięte jądro k1.efi, dwa miejsca na system, klucz K1 w MOK

- Status: Przyjęty, wdrożony w 0.7.0 (zamyka etap M6); punkt 3 (manifest formatu 1) i konsekwencja o plikach startowych
  zmienione w 0.9.0 [uzupełnienie: ADR 0038]; od 0.13.0 linia poleceń wydania `jadro/cmdline-wydanie` (`console=ttynull`)
  i testowa kopia `k1-test.efi` [uzupełnienie: ADR 0059]
- Data: 2026-11-08
- Dotyczy: ADR 0021 (M6a–M6b), ADR 0018 (instalator), ADR 0010 (ISO), SPEC §6, RYZYKA R1, R25, R41, R42–R45

## Kontekst
Po M6a–M6b Instalator sprawdza odcisk i podpis systemu, a initramfs — obraz rdzenia (dm-verity). Brakowało
dwóch rzeczy z ADR 0021:
- **M6c (A/B):** aktualizacja nadpisywała jedyną kopię systemu; zepsuta albo przerwana aktualizacja = komputer
  bez systemu (ratunek tylko z pendrive'a).
- **M6d (Secure Boot):** firmware uruchamiał wszystko z pendrive'a i dysku; skrót dm-verity był w linii
  poleceń GRUB-a, którą zmieni każdy z dostępem do nośnika (albo root w K1 — trwale).

Eksperymenty w QEMU (OVMF z Secure Boot i kluczami Microsoftu, shim i GRUB z Ubuntu — te same pliki co
na pendrivie) przed decyzją:
1. Jądro podpisane naszym kluczem (RSA, `sbsign`), klucz w MOK → shim → GRUB Canonical → K1 startuje;
   bez klucza GRUB odmawia (`bad shim signature`). Nasze jądro nie ma protokołu „EFI handover”
   (`CONFIG_EFI_HANDOVER_PROTOCOL` wyłączone od M1), więc GRUB uruchamia je jak program EFI.
2. GRUB z Ubuntu **nie sprawdza initrd ani linii poleceń** — z zewnętrznym initramfs ktoś mógłby
   podsunąć własny `/init` podpisanemu jądru. Na x86 nie ma `INITRAMFS_FORCE`.
3. Gdy linia poleceń jest wbudowana i zawiera `noinitrd`, stub EFI nie wczytuje initrd od programu
   ładującego (LoadFile2), a bez „handover” nie ma innej drogi — sprawdzone: GRUB z `initrd` i
   `rdinit=` w konfiguracji, jądro uruchomiło tylko nasz initramfs.

## Decyzja
**1. Zamknięte jądro `k1.efi`** (`scripts/zbuduj-k1efi.sh`, `jadro/k1-zamkniete.config`): to samo jądro
(`k1.config`) + `CMDLINE_BOOL`, `CMDLINE_OVERRIDE` (linia z `jadro/cmdline`, `k1.rdzen=wbudowany`,
`k1.rdzen_skrot=…`, `noinitrd`, `quiet`) i `INITRAMFS_SOURCE` = nasz initramfs + `rdzen.squashfs`.
**Cały system to jeden plik (~6 MB), podpisany** — linii poleceń, initramfs ani obrazu rdzenia nie da się
zmienić bez unieważnienia podpisu. Budowany w tym samym katalogu co jądro deweloperskie (kilka plików się
zmienia); `bzImage` + `initramfs.cpio` zostają dla testów (`test-m1`: negatywne scenariusze startu).

**2. Klucz Secure Boot** (`narzedzia/klucz_sb.py`): RSA-2048 i samopodpisany certyfikat X.509 wyprowadzone
**deterministycznie z tego samego sekretu** `K1_KLUCZ_PODPISU` (SHAKE-256 z etykietą — niezależny od klucza
EdDSA manifestów): właściciel ma jeden sekret, każda budowa daje ten sam certyfikat, klucz wpisuje się raz.
Podpis `sbsign` przy zamrożonym zegarze (`faketime` = `SOURCE_DATE_EPOCH`) — powtarzalny. Klucz prywatny
jest usuwany zaraz po podpisaniu. **Bez sekretu**: klucz deweloperski z publicznego ziarna (CN „NIE UFAĆ”) —
podpisuje wersje deweloperskie tylko do testów; K1 nigdy nie proponuje jego wpisania.

**3. Partycja K1-EFI, format 2** (`src/wspolne/k1_uklad.h`): FAT32 280 MiB (klastry 4 KiB) z shimem,
GRUB-em, MokManagerem (`mmx64.efi`, podpisany przez Canonical), `grub.cfg`, `/boot/grub/grubenv` i dwoma
**miejscami na system** `/k1/a.img`, `/k1/b.img` (po 128 MiB, ciągłe): w środku mały FAT z `k1.efi`, na 127 MiB
manifest miejsca (format 2 — z identyfikatorem wydania). Gdzie leżą te pliki, mówi **blok układu** w
zarezerwowanych sektorach FAT32 (1 KiB, suma BLAKE2b). Kolejność plików przy budowie ustala położenie;
skrypt sprawdza (`mshowfat`), że każdy leży w jednym kawałku. Manifest formatu 1 zostaje na 47 MiB (w pustym
miejscu B) — dzięki temu **K1 0.6.2–0.6.3 aktualizuje się do 0.7.0 swoim starym Instalatorem** (kopiuje całą
partycję).

**4. GRUB (A/B)**: `load_env` → `k1_slot` (potwierdzone miejsce), `k1_nowy` (wypróbuj raz). Nowe miejsce:
`k1_start = k1_proba = k1_nowy`, `k1_nowy` czyszczone **przed** startem (`save_env`) — więc tylko jedna próba.
`loopback` + `linux (k1sys)/k1.efi`. Gdy nowego nie da się wczytać (uszkodzone, zły podpis) — od razu
potwierdzone; gdy żadnego — komunikat na ekranie (co zrobić) zamiast cichego menu.

**5. Instalator**: instalacja = kopia całej K1-EFI pendrive'a (sprawdzone manifesty: formatu 1 — pliki
startowe, formatu 2 — system); aktualizacja dysku z formatem 2 = system z miejsca A pendrive'a **do miejsca,
którego dysk nie używa**, odczyt i porównanie, potem grubenv `k1_nowy`. Działający system nie jest ruszany.
Dysk z 0.5.9–0.6.3 (bez bloku układu) dostaje raz całą partycję.

**6. Potwierdzenie po starcie** (nadzorca, `rozruch.c`): 15 s po pulpicie albo ekranie hasła — dla każdego
wewnętrznego dysku z K1: próba, która wystartowała **i jest dokładnie tym wydaniem, które działa**
(identyfikator wydania = SHA-256 plików rdzenia, w `/rdzen/system/wydanie` i w manifeście miejsca) →
`k1_slot`; próba, z której GRUB wrócił → wyczyszczona i zgłoszona w oknie Instalatora. Tak rozpoznajemy,
że uruchomiło się to miejsce, bez zmiennych UEFI (start z pendrive'a przy podłączonym dysku nie potwierdzi
cudzej próby). Pendrive'ów nie dotyka.

**7. Wpisanie klucza** (nadzorca, `efi.c`; okno Instalatora → „Secure Boot…”): na prośbę właściciela —
`MokNew` (EFI_SIGNATURE_LIST z certyfikatem K1) i `MokAuth` (SHA-256 listy i hasła „kons” jako UTF-16,
jak `mokutil`) w efivarfs (montowany tylko na chwilę); po restarcie shim pokazuje MokManagera (10 s na
naciśnięcie klawisza) → Enroll MOK → Continue → Yes → hasło → Reboot; potem F1 → Secure Boot → Enabled.
Okno pokazuje stan (Secure Boot wł./wył., klucz wpisany / czeka / brak). Tylko klucz wydania.

## Konsekwencje
- Jeden sekret (`K1_KLUCZ_PODPISU`) daje podpisy manifestów **i** Secure Boot. Do czasu jego dodania wydania
  są deweloperskie: działają z wyłączonym Secure Boot, okno mówi, że klucza nie wpisujemy.
- Pendrive ma 345 MiB (było 113): dwa miejsca po 128 MiB. Pobranie (`.img.xz`) — bez zmian (~8 MB).
- Aktualizacja A/B zmienia tylko miejsce systemu i grubenv. Pliki startowe (shim, GRUB, `grub.cfg`) na
  dysku są z instalacji/pierwszej aktualizacji do 0.7.0; ich wymiana (np. nowszy shim po odwołaniu
  starego) wymaga „Zainstaluj od nowa” albo przyszłej wersji Instalatora (RYZYKA R45).
- Secure Boot z kluczami Microsoftu dopuszcza też inne systemy podpisane przez MS (np. Ubuntu z pendrive'a);
  chroni przed podmianą **K1** (także trwałą, przez roota), nie przed uruchomieniem innego systemu.
  Dane chroni hasło (szyfrowanie), nie Secure Boot.
- Stare podpisane wersje K1 pozostają ważne dla shima (brak odwoływania wersji — R44); Instalator i tak nie
  zapisze starszej wersji.
- Testy: host (`test_uklad`, `test_sha256`, manifest formatu 2, fuzzing `fuzz_uklad`), QEMU `test-m5f`
  (aktualizacja A/B, potwierdzenie, wycofanie z uszkodzonego miejsca), `test-m6` (Secure Boot: z kluczem,
  bez klucza, zmieniony `k1.efi`; z kluczem wydania — pełne wpisanie przez MokManagera).
