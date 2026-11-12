# Architektura K1 — jak to jest zbudowane

Specyfikacja (co i dlaczego): [`SPEC.md`](SPEC.md). Decyzje: [`adr/`](adr/README.md).
Ten plik opisuje **stan obecny**: mapę repo, przebieg budowy i startu, konwencje.

## W jednym zdaniu
K1 = jądro Linux (własna, minimalna konfiguracja) + nasz initramfs, init, nadzorca, usługi grafiki
i wejścia, pulpit i aplikacje — wszystko w C (statycznie z musl), każda usługa w osobnej klatce.
Uruchamiane w QEMU i (obraz ISO) w maszynach wirtualnych. Nie ma dystrybucji, powłoki ani bibliotek.

## Mapa repozytorium

| Ścieżka | Co tu jest |
|---|---|
| `VERSION`, `CHANGELOG.md` | wersja K1 (SemVer) i historia zmian |
| `Makefile` | kompilacja programów K1 (musl) i testów na hoście |
| `src/wspolne/` | wspólne moduły: log i błędy (`k1_log`), parser linii poleceń jądra i liczby szesnastkowe (`k1_cmdline`), protokół zdarzeń wejścia (`k1_zdarzenie`), protokół usług (`k1_komunikat`), kanały UNIX (`k1_kanal`) i wspólny odbiór komunikatów (`k1_odbior`), klatka usług (`k1_klatka`); **`k1_uslugi.h` — jedyne listy usług `K1_SYSTEM` i aplikacji `K1_APLIKACJE`** (UID, profil, program, nazwa, ikona; nowa aplikacja = jeden wiersz); odczyt partycji K1-EFI — wersje, A/B, grubenv (`k1_efi`, `k1_uklad`, `k1_manifest`); pełny zapis/odczyt pod przesunięciem i mały plik z sysfs (`k1_io.h`: `k1_czytaj_plik`), czas od startu (`k1_teraz.h`), liczby w bajtach little-endian (`k1_bajty.h`), SHA-256 (`k1_sha256`), klient skarbca (`k1_skarb_klient`, `k1_memfd`), archiwum baterii (`k1_bat_arch`) |
| `src/grafika/` | renderer programowy: tapety (4 warianty), znak K1, kursor, składanie warstw (`k1_grafika`), wspólna paleta kolorów `K1_*` (`k1_grafika.h`); API czcionek (`k1_czcionka.h`) |
| `src/przybornik/` | przybornik GUI (M4): rysowanie, tekst i UTF-8, ikony, ikona baterii `k1_bateria`, przyciski (`k1_rysuj`), motywy i części okien (`k1_okno`), klient protokołu okien (`k1_klient`), szkielet aplikacji (`k1_aplikacja`), czas polski (`k1_czas`), układ klawiatury PL i pole tekstowe `k1_pole_klawisz` (`k1_klawiatura`), liczby po polsku i opis stanu baterii (`k1_liczba`) |
| `src/gui/` | `/rdzen/system/gui` — powłoka pulpitu: górny pasek, dolny pasek, menu Start (M4); ikony na pulpicie i menu pod prawym przyciskiem (0.7.5), pliki z folderu Pulpit (0.8.0) |
| `src/aplikacje/` | `/rdzen/aplikacje/<program>` — aplikacje natywne z listy `K1_APLIKACJE`: Zegar, Notatki, Ustawienia, Zasoby, Instalator (`instalacja`), Kalkulator, Pliki, Wybór pliku (`wybor`); każdy `.c` w katalogu aplikacji jest kompilowany |
| `narzedzia/` | narzędzia budowy na hoście: `czcionki.c` (FreeType → bitmapy czcionek), `ekran_startowy.c` (logo dla GRUB-a, PNG przez zlib — 0.5.8) |
| `src/initramfs/` | `/init` w initramfs: montuje obraz rdzenia, przełącza korzeń |
| `src/init/` | `/rdzen/init` — PID 1 |
| `src/nadzorca/` | `/rdzen/system/nadzorca` — uruchamia usługi w klatkach, łączy je kanałami, restartuje (M3); kanały każdej usługi w jednej tabeli `kanaly[]` (`uslugi.c`) |
| `src/wejscie/` | `/rdzen/system/wejscie` — klawiatura i mysz (evdev) → ekran |
| `src/ekran/` | `/rdzen/system/ekran` — jedyny proces z DRM/KMS i kompozytor okien: tryb, składanie, kursor, kierowanie wejścia, ustawienia |
| `src/skarbiec/` | `/rdzen/system/skarbiec` — Blob Vault: zaszyfrowane dane na partycji „K1-DANE” (M5c–M5e, ADR 0015–0017), od 0.8.0 pliki i foldery użytkownika (ADR 0029) |
| `src/logowanie/` | `/rdzen/system/logowanie` — ekran hasła przy starcie (M5e, ADR 0017) |
| `src/instalator/` | `/rdzen/system/instalator` — zapis K1 na dysk komputera, tablica GPT (M5f, ADR 0018) |
| `src/czytnik/` | `/rdzen/system/czytnik` — pendrive dla Plików: MBR/GPT i FAT12/16/32 w klatce, tylko odczyt (0.8.0 krok 3, ADR 0031) |
| `src/dekoder/` | `/rdzen/system/dekoder` — jeden obraz (PNG, JPEG, od 0.8.2 GIF, BMP, WebP; do 100 Mpx) → piksele, jednorazowy proces w klatce na prośbę klienta z tabeli (Pliki; 0.8.0 krok 4, ADR 0032) |
| `src/zewnetrzne/` | kod zewnętrzny w repozytorium: Monocypher (kryptografia skarbca) i Wuffs (dekoder obrazów) — jedyne obce biblioteki, wersje i uzasadnienie w `docs/ZALEZNOSCI.md` |
| `jadro/` | wersja, konfiguracja i linia poleceń jądra, klucze PGP |
| `testy/system/` | program testowy uruchamiany wewnątrz K1 (obraz testowy) |
| `testy/host/` | testy jednostkowe (ASan/UBSan), fuzzing i podgląd grafiki na hoście |
| `scripts/` | budowa, QEMU, zrzuty ekranu, testy, kontrole (opis niżej) |
| `docs/` | SPEC, ADR, ten plik, TESTY, ZALEZNOSCI, RYZYKA, `komponenty/` (opis każdego komponentu) |
| `.github/workflows/k1.yml` | CI: budowa + testy na `main` (i PR z etykietą `testy`), tag wersji, wydanie z ISO; logi testów zawsze: konsola, podsumowanie, wydanie (`k1-<wersja>-logi.tar.gz`) |
| `build/` | wszystko, co powstaje przy budowie (ignorowane przez git) |

### Skrypty

| Skrypt | Co robi |
|---|---|
| `przygotuj-srodowisko.sh` | instaluje pakiety Ubuntu potrzebne do budowy i testów |
| `zbuduj.sh` | wszystko: `zbuduj-jadro.sh` + `zbuduj-obraz.sh` + `zbuduj-k1efi.sh` + `zbuduj-iso.sh` + `zbuduj-usb.sh` |
| `zbuduj-k1efi.sh` | zamknięte jądro `k1.efi` (linia poleceń i rdzeń w środku), podpis Secure Boot (ADR 0022) |
| `zbuduj-usb.sh` | obraz na pendrive: K1-EFI w formacie 2 (A/B, grubenv, blok układu) + K1-DANE (ADR 0016, 0022) |
| `zbuduj-iso.sh` | obraz ISO do maszyn wirtualnych (GRUB UEFI/BIOS → `k1.efi`; ADR 0010) |
| `zbuduj-jadro.sh` | pobiera jądro (suma + podpis PGP), konfiguruje, sprawdza fragment, kompiluje |
| `zbuduj-obraz.sh` | kompiluje programy, sprawdza binaria, składa `initramfs.cpio` i obrazy SquashFS |
| `uruchom-qemu.sh` | uruchamia K1 w QEMU (log szeregowy do pliku, monitor do gniazda); `--iso PLIK [--uefi] [--vga virtio]` — start z ISO |
| `zrzut-ekranu.sh` | zrzut ekranu działającej maszyny (PNG) |
| `testy.sh` | wszystkie testy (to uruchamia CI) |
| `test-m4.sh` | pulpit i aplikacje (klikanie, pisanie, przeciąganie, motyw, tapeta, wyłączenie) + start z ISO przez UEFI i BIOS |
| `test-m1.sh` | start, testy w systemie, błędy startu (M1–M2) |
| `test-m2.sh` | ekran i wejście: mysz/klawiatura przez monitor QEMU, zrzuty ekranu sprawdzane automatycznie |
| `test-m5f.sh` | instalator: instalacja, start z dysku, aktualizacja A/B, potwierdzenie, wycofanie, starsza wersja |
| `test-m6.sh` | Secure Boot (OVMF secboot): klucz K1 w MOK, odmowy, wpisanie klucza przez MokManagera |
| `test-m5.sh` | sprzęt: klawiatura i mysz USB bez PS/2, dyski i pendrive (M5a) |
| `test-pliki.sh` | Pliki i Kosz: foldery, nazwy, Kosz, pliki na pulpicie, okno wyboru pliku, Notatki na plikach |
| `test-pendrive.sh` | wirtualny pendrive USB ze zdjęciami (mtools, Pillow): import, podgląd, tapeta ze zdjęcia, duże pliki, wyjęcie |
| `test-skarbiec.sh` | skarbiec na trudnym dysku danych: długi dziennik, prawie pełna K1-DANE, „Użyj całego pendrive'a” |
| `uslugi-systemu.sh` | nazwy usług systemu z `K1_SYSTEM` (dla `Makefile` i `zbuduj-obraz.sh` — jedna lista) |
| `ppm-na-png.py` | podgląd grafiki na hoście: PPM → PNG |
| `sprawdz-binaria.sh`, `sprawdz-konfiguracje-jadra.sh`, `sprawdz-kspp.sh` | kontrole bezpieczeństwa |
| `sprawdz-powtarzalnosc.sh` | drugi build w osobnym katalogu i porównanie sum (powtarzalność) |
| `wspolne.sh` | wspólne funkcje skryptów: `make_jadra` (make w katalogu jądra), `grub_poczatek` (początek każdego grub.cfg), `verity`, `obraz_fat`, `efi_podpisane`, `bateria_aml`, `zamknij_qemu` (koniec maszyny testowej) |

## Jak powstaje obraz

```
jadro/wersja.env ──► pobranie + SHA-256 + podpis PGP ──► allnoconfig + jadro/k1.config ──► bzImage
                                                         (sprawdzenie każdej opcji)    └► nagłówki UAPI, gen_init_cpio
IBM Plex (fonts-ibm-plex) ──► narzedzia/czcionki.c (FreeType, host) ──► build/gen/k1_czcionki_dane.c
src/ ──► musl-gcc (static-pie, flagi SPEC §11) ──► k1-initramfs, init, nadzorca, usługi z K1_SYSTEM,
                                                      aplikacje z K1_APLIKACJE, k1-test-systemu ──► sprawdzenie ELF
                     initramfs.cpio  ◄── /init (k1-initramfs)
                     rdzen.squashfs  ◄── init, system/{nadzorca + usługi z K1_SYSTEM}, aplikacje/{z K1_APLIKACJE},
                                         katalogi jadro/{dev,proc,sys}
                     rdzen-test.squashfs ◄── + system/test-systemu (+ plik ze „#!” do testu negatywnego)
                     k1.efi ◄── to samo jądro + wbudowana linia poleceń (noinitrd) + initramfs z rdzen.squashfs,
                                podpis Secure Boot (klucz z K1_KLUCZ_PODPISU; bez niego — deweloperski)
                     k1-<wersja>.iso ◄── GRUB (UEFI + BIOS) + k1.efi
                     k1-<wersja>-usb.img ◄── GPT: K1-EFI (shim, GRUB, MokManager, grubenv, /k1/a.img = k1.efi, /k1/b.img)
                                                  + K1-DANE
```
Wynik w `build/wynik/` (+ `SHA256SUMS`, `srodowisko.txt`). Build jest powtarzalny: stały
`SOURCE_DATE_EPOCH` (data ostatniego commita), stałe dane budowy jądra, `-frandom-seed`.

## Jak startuje system

```
QEMU (-kernel bzImage -initrd initramfs.cpio, dysk virtio = rdzen.squashfs) — testy
  albo ISO: firmware (UEFI/BIOS) → GRUB (tryb graficzny) → k1.efi
  albo pendrive/dysk (0.7.0): UEFI → shim (MS) → GRUB (Canonical) → grubenv (miejsce A/B, próba raz) →
       loopback /k1/<miejsce>.img → k1.efi (z Secure Boot: podpis sprawdzony przez shim, klucz K1 w MOK)
 └─ jądro 6.18 ── otwiera /dev/console w initramfs, uruchamia /init
     └─ k1-initramfs (PID 1)
          /proc/cmdline → k1.rdzen=/dev/vda albo =wbudowany (/rdzen.squashfs przez loop) (walidacja)
          tmpfs → nowy korzeń: aplikacje/ dane_uzytkownika/ rdzen/
          SquashFS przez dm-verity (skrót z linii poleceń, od 0.6.3) → /rdzen (ro) ; korzeń → ro ;
          usunięcie initramfs ; MS_MOVE + chroot
          execve /rdzen/init
     └─ /rdzen/init (PID 1)
          sysctl bezpieczeństwa → montowania /rdzen/jadro/{dev,proc,sys}, cgroup2, /dane_uzytkownika
          „K1: init OK” → /rdzen/system/nadzorca → pętla sygnałów (sieroty, wyłączanie)
     └─ nadzorca (root; ADR 0007) — każda usługa w klatce: UID, 0 capabilities, NO_NEW_PRIVS,
        │  Landlock bez ścieżek, seccomp, cgroup; fd 0/1 = /dev/null, fd 2 = pipe logu → nadzorca → konsola;
        │  fd 3 = kanał od nadzorcy, kolejne kanały według tabeli kanaly[] (src/nadzorca/uslugi.c)
          ├─ skarbiec (uid 103) — partycja K1-DANE (dane zaszyfrowane), potem
          ├─ ekran (101; karta graficzna, składanie okien) → wejscie (100; klawiatura, mysz)
          ├─ gui (102; pulpit) albo logowanie (104), gdy jest hasło
          ├─ na prośbę gui: aplikacje z K1_APLIKACJE (UID 200+); instalator (105) na prośbę okna Instalatora;
          │  czytnik (106, pendrive) i dekoder (107, jeden obraz) na prośbę Plików
          restart po awarii (0,2/1/2/4/4 s, maks. 5 na minutę); koniec ekranu → restart klientów
          szczegóły (kanały, deskryptory, etapy startu): docs/komponenty/nadzorca.md
          [obraz testowy: test-systemu (root, fd 3: kanał testowy) → po jego końcu nadzorca kończy się]
```
Szczegóły i tabela montowań: ADR 0004. Błędy: ADR 0005.

## Konwencje
- **Język:** komunikaty w systemie, dokumentacja, PR-y — po polsku; identyfikatory i komentarze
  w kodzie oraz commity — po angielsku.
- **Kod C:** małe moduły, jawne limity długości, sprawdzanie każdego błędu, bez `strcpy/strcat/sprintf`
  (tylko `snprintf`/`strncat` z limitem). Flagi w `Makefile` — zmiana tylko przez ADR.
- **Prefiksy logów:** `K1-INITRAMFS:`, `K1:` (init), `K1-NADZORCA:`, `K1-WEJSCIE:`, `K1-EKRAN:`, `K1-TEST:`;
  błędy krytyczne PID 1 `BŁĄD KRYTYCZNY:` (`k1_awaria` — wyłączenie), błędy usług `BŁĄD:` (`k1_koniec` — wyjście z kodem 1).
- **Kanały usług:** kanał od nadzorcy to zawsze fd 3. Komunikaty: stały rozmiar, wersja, typ, długość,
  pole zarezerwowane = 0, odbiorca sprawdza każde pole (SPEC §12).
- **Ścieżki w systemie** po polsku (`/rdzen`, `/dane_uzytkownika`, `/rdzen/system/…`).
- **Komponent = dokument** w `docs/komponenty/` (cel, granica zaufania, uprawnienia, wejścia/wyjścia,
  zależności, syscalle, błędy, powierzchnia ataku, testy) — SPEC §2.6.
- **Statusy:** ZAIMPLEMENTOWANE / PRZETESTOWANE (z dowodem) / PLANOWANE — SPEC §2.4.

## Wersjonowanie
SemVer od 0.1.0 w `VERSION`; tagi `k1-vX.Y.Z` zakłada CI po udanym buildzie i testach na `main`
(stare tagi `v0.1.x` to Kons-OS na Debianie, ADR 0001).

| Zmiana | Co podbić |
|---|---|
| nowy temat z mapy drogi (ADR 0035: 0.9 Sieć, 0.10 Strefy…) | MINOR: 0.8.x → 0.9.0 |
| kolejny krok planu wersji, poprawka, aktualizacja jądra | PATCH: 0.8.1 → 0.8.2 |
| tylko dokumentacja / CI / repo | bez zmiany |

## Ograniczenia środowiska pracy
- Kontener deweloperski bez `/dev/kvm` → QEMU w emulacji TCG (start K1 ~1–2 s, pierwsza klatka ~2 s,
  wszystkie testy ~6 min). QEMU: karta `-vga std` (sterownik `bochs`) albo przy ISO `virtio-vga` (simpledrm), PS/2.
- Sieć kontenera: tylko HTTPS przez proxy; dozwolone m.in. apt Ubuntu, deb.debian.org, GitHub,
  cdn/www/git.kernel.org.
- CI (GitHub Actions) buduje tylko `main` — minuty prywatnego repo są ograniczone.
