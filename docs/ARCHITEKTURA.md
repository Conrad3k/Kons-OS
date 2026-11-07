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
| `src/wspolne/` | wspólne moduły: log i błędy (`k1_log`), parser linii poleceń jądra (`k1_cmdline`), protokół zdarzeń wejścia (`k1_zdarzenie`), protokół usług (`k1_komunikat`), kanały UNIX (`k1_kanal`), klatka usług (`k1_klatka`), tożsamości usług (`k1_uslugi.h`) |
| `src/grafika/` | renderer programowy: tapety (4 warianty), znak K1, kursor, składanie warstw (`k1_grafika`); API czcionek (`k1_czcionka.h`) |
| `src/przybornik/` | przybornik GUI (M4): rysowanie i tekst (`k1_rysuj`), motywy i części okien (`k1_okno`), klient protokołu okien (`k1_klient`), szkielet aplikacji (`k1_aplikacja`), czas polski (`k1_czas`), układ klawiatury PL (`k1_klawiatura`) |
| `src/gui/` | `/rdzen/system/gui` — powłoka pulpitu: górny pasek, dolny pasek, menu Start (M4) |
| `src/aplikacje/` | `/rdzen/aplikacje/{zegar,notatki,ustawienia}` — aplikacje natywne (M4) |
| `narzedzia/` | narzędzia budowy na hoście: `czcionki.c` (FreeType → bitmapy czcionek), `ekran_startowy.c` (logo dla GRUB-a, PNG przez zlib — 0.5.8) |
| `src/initramfs/` | `/init` w initramfs: montuje obraz rdzenia, przełącza korzeń |
| `src/init/` | `/rdzen/init` — PID 1 |
| `src/nadzorca/` | `/rdzen/system/nadzorca` — uruchamia usługi w klatkach, łączy je kanałami, restartuje (M3) |
| `src/wejscie/` | `/rdzen/system/wejscie` — klawiatura i mysz (evdev) → ekran |
| `src/ekran/` | `/rdzen/system/ekran` — jedyny proces z DRM/KMS i kompozytor okien: tryb, składanie, kursor, kierowanie wejścia, ustawienia |
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
| `zbuduj.sh` | wszystko: `zbuduj-jadro.sh` + `zbuduj-obraz.sh` + `zbuduj-iso.sh` |
| `zbuduj-iso.sh` | obraz ISO do maszyn wirtualnych (GRUB UEFI/BIOS, rdzeń wbudowany w initramfs; ADR 0010) |
| `zbuduj-jadro.sh` | pobiera jądro (suma + podpis PGP), konfiguruje, sprawdza fragment, kompiluje |
| `zbuduj-obraz.sh` | kompiluje programy, sprawdza binaria, składa `initramfs.cpio` i obrazy SquashFS |
| `uruchom-qemu.sh` | uruchamia K1 w QEMU (log szeregowy do pliku, monitor do gniazda); `--iso PLIK [--uefi] [--vga virtio]` — start z ISO |
| `zrzut-ekranu.sh` | zrzut ekranu działającej maszyny (PNG) |
| `testy.sh` | wszystkie testy (to uruchamia CI) |
| `test-m4.sh` | pulpit i aplikacje (klikanie, pisanie, przeciąganie, motyw, tapeta, wyłączenie) + start z ISO przez UEFI i BIOS |
| `test-m1.sh` | start, testy w systemie, błędy startu (M1–M2) |
| `test-m2.sh` | ekran i wejście: mysz/klawiatura przez monitor QEMU, zrzuty ekranu sprawdzane automatycznie |
| `sprawdz-binaria.sh`, `sprawdz-konfiguracje-jadra.sh`, `sprawdz-kspp.sh` | kontrole bezpieczeństwa |
| `sprawdz-powtarzalnosc.sh` | drugi build w osobnym katalogu i porównanie sum (powtarzalność) |

## Jak powstaje obraz

```
jadro/wersja.env ──► pobranie + SHA-256 + podpis PGP ──► allnoconfig + jadro/k1.config ──► bzImage
                                                         (sprawdzenie każdej opcji)    └► nagłówki UAPI, gen_init_cpio
IBM Plex (fonts-ibm-plex) ──► narzedzia/czcionki.c (FreeType, host) ──► build/gen/k1_czcionki_dane.c
src/ ──► musl-gcc (static-pie, flagi SPEC §11) ──► k1-initramfs, init, nadzorca, wejscie, ekran, gui,
                                                      zegar, notatki, ustawienia, k1-test-systemu ──► sprawdzenie ELF
                     initramfs.cpio  ◄── /init (k1-initramfs)
                     rdzen.squashfs  ◄── init, system/{nadzorca,wejscie,ekran,gui}, aplikacje/{zegar,notatki,ustawienia,zasoby},
                                         katalogi jadro/{dev,proc,sys}
                     rdzen-test.squashfs ◄── + system/test-systemu (+ plik ze „#!” do testu negatywnego)
                     k1-<wersja>.iso ◄── GRUB (UEFI + BIOS) + bzImage + initramfs.cpio + rdzen.cpio (/rdzen.squashfs)
```
Wynik w `build/wynik/` (+ `SHA256SUMS`, `srodowisko.txt`). Build jest powtarzalny: stały
`SOURCE_DATE_EPOCH` (data ostatniego commita), stałe dane budowy jądra, `-frandom-seed`.

## Jak startuje system (M4)

```
QEMU (-kernel bzImage -initrd initramfs.cpio, dysk virtio = rdzen.squashfs)
  albo ISO: firmware (UEFI/BIOS) → GRUB (tryb graficzny) → jądro + initramfs.cpio + rdzen.cpio, k1.rdzen=wbudowany
 └─ jądro 6.18 ── otwiera /dev/console w initramfs, uruchamia /init
     └─ k1-initramfs (PID 1)
          /proc/cmdline → k1.rdzen=/dev/vda albo =wbudowany (/rdzen.squashfs przez loop) (walidacja)
          tmpfs → nowy korzeń: aplikacje/ dane_uzytkownika/ rdzen/
          SquashFS z /dev/vda → /rdzen (ro) ; korzeń → ro ; usunięcie initramfs ; MS_MOVE + chroot
          execve /rdzen/init
     └─ /rdzen/init (PID 1)
          sysctl bezpieczeństwa → montowania /rdzen/jadro/{dev,proc,sys}, cgroup2, /dane_uzytkownika
          „K1: init OK” → /rdzen/system/nadzorca → pętla sygnałów (sieroty, wyłączanie)
     └─ nadzorca (root; ADR 0007) — każda usługa w klatce: UID, 0 capabilities, NO_NEW_PRIVS,
        │  Landlock, seccomp, cgroup; fd 0/1 = /dev/null, fd 2 = pipe logu → nadzorca → konsola
          ├─ ekran   (uid 101) fd 3: kanał sterujący (KLIENT + SCM_RIGHTS), fd 4: karta (DRM master)
          │            kompozytor: tapeta + okna + panele (memfd z pieczęciami) + cienie + kursor
          ├─ wejscie (uid 100) fd 3: kanał SEQPACKET do ekranu (SO_PASSCRED), fd 4…: urządzenia event*
          ├─ gui     (uid 102) fd 3: kanał do ekranu (rola: powłoka), fd 4: prośby do nadzorcy (URUCHOM, WYLACZ)
          └─ na prośbę gui: zegar (200), notatki (201, Landlock: /dane_uzytkownika/notatki), ustawienia (202),
             zasoby (203; fd 4: dane od nadzorcy — cgroup cpu/memory, jednokierunkowo)
          restart po awarii (0,2/1/2/4/4 s, maks. 5 na minutę); koniec ekranu → restart klientów
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
| ukończony etap (M2, M3, …) | MINOR: 0.1.0 → 0.2.0 |
| poprawka, aktualizacja jądra, drobna zmiana w systemie | PATCH: 0.1.0 → 0.1.1 |
| tylko dokumentacja / CI / repo | bez zmiany |

## Ograniczenia środowiska pracy
- Kontener deweloperski bez `/dev/kvm` → QEMU w emulacji TCG (start K1 ~1–2 s, pierwsza klatka ~2 s,
  wszystkie testy ~6 min). QEMU: karta `-vga std` (sterownik `bochs`) albo przy ISO `virtio-vga` (simpledrm), PS/2.
- Sieć kontenera: tylko HTTPS przez proxy; dozwolone m.in. apt Ubuntu, deb.debian.org, GitHub,
  cdn/www/git.kernel.org.
- CI (GitHub Actions) buduje tylko `main` — minuty prywatnego repo są ograniczone.
