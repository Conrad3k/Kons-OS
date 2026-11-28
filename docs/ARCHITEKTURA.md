# Architektura K1 — jak to jest zbudowane

Specyfikacja (co i dlaczego): [`SPEC.md`](SPEC.md). Decyzje: [`adr/`](adr/README.md).
Ten plik opisuje **stan obecny**: mapę repo, przebieg budowy i startu, konwencje.

## W jednym zdaniu
K1 = jądro Linux (własna, minimalna konfiguracja) + nasz initramfs, init, nadzorca, usługi grafiki
i wejścia, pulpit i aplikacje — wszystko w C (statycznie z musl), każda usługa w osobnej klatce.
Działa z pendrive'a i z dysku laptopa, w maszynach wirtualnych (ISO) i w testach w QEMU. Nie ma dystrybucji, powłoki ani
bibliotek. Obce aplikacje (przeglądarka) działają w strefach — mikro-maszynach z własnym jądrem.

## Mapa repozytorium

Jeden wiersz na katalog. Szczegóły części (moduły, kanały, klatki, testy) — w jej opisie w `docs/komponenty/`; skrót dla
asystentów — skill `kons-dev`, §3.

| Ścieżka | Co tu jest |
|---|---|
| `VERSION`, `CHANGELOG.md` | wersja K1 (SemVer) i historia zmian (od 0.10.5 każdy wpis ma „Opis w K1:” — pokazuje go Ustawienia → Aktualizacje) |
| `Makefile` | kompilacja programów K1 (musl) i testów na hoście; listy testów `TESTY_HOST` i fuzzerów `FUZZ` |
| `src/wspolne/` | kod wspólny: **`k1_uslugi.h` — jedyne listy usług `K1_SYSTEM`, aplikacji `K1_APLIKACJE` i stref `K1_STREFY`** (nowa usługa albo aplikacja = jeden wiersz), protokół usług `k1_komunikat` (sprawdzanie według kanałów w `k1_kom_<kanał>.c`), kanały, klatka, log, formaty z ustalonym układem (K1-EFI, manifest, ogłoszenie, paczka, opis ekranu), klient skarbca, typy plików `K1_TYPY`, SHA-256, AES, SHA-1 |
| `src/initramfs/`, `src/init/` | `/init` w initramfs (rdzeń przez dm-verity, przełączenie korzenia) i `/rdzen/init` — PID 1 (`docs/komponenty/initramfs.md`, `init.md`) |
| `src/nadzorca/` | `/rdzen/system/nadzorca` — usługi i aplikacje w klatkach (`KLATKA_<NAZWA>` w `profile.c`), kanały (`kanaly.h`), urządzenia, sieć, strefy, aktualizacje (`docs/komponenty/nadzorca.md`) |
| `src/ekran/`, `src/grafika/`, `src/wejscie/` | jedyny proces z DRM/KMS (składanie okien, kursor, ramka strefy, opis ekranu dla testów), renderer i paleta `K1_*`, klawiatura/mysz/touchpad (`ekran.md`, `wejscie.md`) |
| `src/gui/`, `src/logowanie/` | pulpit: paski, menu Start, ikony, okienka, przełącznik okien (`gui.md`); ekran hasła (`logowanie.md`) |
| `src/przybornik/` | przybornik okien aplikacji: rysowanie, tekst, ikony `K1_IK_*`, przyciski, menu, przeciąganie, zaznaczanie (`aplikacje.md`) |
| `src/aplikacje/` | `/rdzen/aplikacje/<program>` — aplikacje z `K1_APLIKACJE`: Zegar, Notatki, Ustawienia, Zasoby, Instalator, Kalkulator, Pliki, Wybór pliku, Podgląd (`aplikacje.md`) |
| `src/skarbiec/` | `/rdzen/system/skarbiec` — zaszyfrowane dane na partycji „K1-DANE” (format 2, ADR 0036), pliki i foldery, dyski stref (`skarbiec.md`) |
| `src/instalator/` | `/rdzen/system/instalator` — program zapisujący: instalacja na dysk (GPT), aktualizacja z pendrive'a i z sieci, pliki startowe (`instalator.md`, `aktualizacja.md`) |
| `src/siec/` | `/rdzen/system/siec` — DHCP, zegar z internetu, Wi-Fi (opisy sieci, logowanie WPA2, zapamiętane sieci); gniazda i karty — nadzorca (`siec.md`) |
| `src/strefa/` | `/rdzen/system/strefa` — VMM jednej strefy (KVM, virtio, okno, łącznik) i `gosc/` — nasze programy w strefie: `/init`, kompozytor Wayland, przekaźnik, pobieranie aktualizacji (`strefa.md`, `raptor-bridge.md`) |
| `src/czytnik/`, `src/dekoder/` | pendrive dla Plików (FAT, tylko odczyt) i jeden obraz → piksele, oba w klatce (`czytnik.md`, `dekoder.md`) |
| `src/filmy/` | dekoder filmów (0.10.9 krok 2): film ze strefy → karta graficzna (VA-API, i965) → klatki NV12, jeden na strefę, w klatce (`filmy.md`) |
| `src/posrednik/` | pośrednik grafiki (0.10.9 krok 5): prośby mostu native context ze strefy → sprawdzone pole po polu → karta graficzna (i915), jeden na strefę, w klatce (`posrednik.md`) |
| `src/zewnetrzne/` | Monocypher, Wuffs i fortify-headers, a od 0.10.9 sumy źródeł bibliotek dekodera filmów (`va/zrodla.txt`: libva, i965, libdrm — budowane przez `zbuduj-va.sh`) Mesy dla strefy (`mesa/zrodla.txt`, `zbuduj-mese.sh`) i od 0.13.2 FFmpeg dla strefy (`ffmpeg/zrodla.txt`, `zbuduj-ffmpeg.sh`) — oba w obrazie strefy, nie w K1 — jedyny obcy kod w programach K1 (`docs/ZALEZNOSCI.md`) |
| `jadro/` | wersja jądra (`wersja.env`, zmienia ją też robot), fragmenty konfiguracji (`k1.config`, `k1-zamkniete.config`, `strefa.config`), listy opcji świadomie odrzuconych, pliki od producentów kart, klucze PGP (`jadro/README.md`, `docs/komponenty/jadro.md`) |
| `musl/` | biblioteka C programów K1: wersja i suma, klucz PGP, łatki (`musl/README.md`) |
| `strefy/` | obraz strefy przeglądarki i pobierania: tabela pakietów Debiana z sumami (`przegladarka.txt`, zmienia ją też robot), ustawienia Firefoksa, klucze archiwum (`strefy/README.md`) |
| `narzedzia/` | narzędzia budowy na hoście: czcionki → bitmapy, ekran startowy GRUB-a, podpis manifestów i ogłoszenia wydania, klucz Secure Boot; poza systemem — motyw Claude Code w stylu K1 (`narzedzia/claude-motyw/`, sklepik `.claude-plugin/marketplace.json`, włączany w `.claude/settings.json`) |
| `testy/host/`, `testy/system/`, `testy/qemu/` | testy na hoście i fuzzing; program testowy w systemie (obraz testowy); biblioteka testów QEMU `k1qemu.py` (`docs/TESTY.md`) |
| `testy/wzorce/`, `testy/stanowisko/`, `testy/acpi/` | zrzuty wzorcowe okien K1 (zmienia je `scripts/przyjmij-wzorce.sh` po „OK” właściciela); stanowisko na hoście (Firefox z kompozytorem strefy); sztuczna bateria dla QEMU |
| `scripts/` | budowa, QEMU, testy, kontrole, robot (niżej) |
| `docs/` | SPEC, ADR, ten plik, `DO_ZROBIENIA.txt` (plan), plany wersji, TESTY, SLOWNIK, WYGLAD, ZALEZNOSCI, RYZYKA, `komponenty/`, `archiwum/` (zamknięte plany) |
| `AGENTS.md`, `CLAUDE.md`, `.claude/skills/kons-dev/` | zasady dla asystentów (Claude, Codex) i przewodnik po pracy w repo |
| `.github/workflows/` | `k1.yml` — CI: na PR-ze z etykietą `testy` budowa i pełne testy, na `main` sama budowa i wydanie (tag, ISO, obraz USB, ogłoszenie, paczka), gdy `VERSION` ma nową wersję; `robot.yml` — robot aktualizacji (`docs/komponenty/robot.md`); `sprzatanie-artefaktow.yml`, `sprzatanie-przebiegow.yml` (`docs/komponenty/sprzatanie.md`) |
| `.github/ISSUE_TEMPLATE/` | szablony zgłoszeń (Feedback z testu, Błąd, Pomysł) |
| `build/` | wszystko, co powstaje przy budowie (ignorowane przez git) |

### Skrypty

| Skrypt | Co robi |
|---|---|
| `przygotuj-srodowisko.sh` | instaluje pakiety Ubuntu potrzebne do budowy i testów |
| `zbuduj.sh` | wszystko: `zbuduj-jadro.sh`, `zbuduj-musl.sh`, `zbuduj-obraz.sh` (z nim `zbuduj-mese.sh`, `zbuduj-ffmpeg.sh` i `zbuduj-strefe.sh`), `zbuduj-k1efi.sh`, `zbuduj-iso.sh`, `zbuduj-usb.sh`, od 0.13.0 krok 4 także `zbuduj-iso.sh --test` i `zbuduj-usb.sh --test` (kopia testowa wydania: obrazy z `k1-test.efi`) |
| `zbuduj-musl.sh` | musl dla programów K1: pobranie (suma, podpis PGP), łatki, budowa → `build/musl` |
| `zbuduj-mese.sh` | Mesa dla strefy przeglądarki (0.10.9 krok 6, szybka grafika): źródła i meson z sumami w `src/zewnetrzne/mesa/zrodla.txt`; etap 1 — `mesa_clc` z LLVM 18 (tylko budowa), etap 2 — iris z `intel-virtio-experimental`, EGL, GBM, bez LLVM → `build/mesa/inst`; pomija budowę przy tym samym znaku (CI na PR-ze: pamięć podręczna) |
| `zbuduj-ffmpeg.sh` | FFmpeg dla strefy przeglądarki (0.13.2 krok 2, ADR 0063 pkt 7): źródła z sumą w `src/zewnetrzne/ffmpeg/zrodla.txt`; tylko `libavcodec.so.61` i `libavutil.so.59` (H.264, VA-API, LGPL, bez programów) → `build/ffmpeg/inst`; ok. 1–2 min, pomija budowę przy tym samym znaku |
| `zbuduj-va.sh` | biblioteki dekodera filmów (0.10.9 krok 2): libdrm, libva, i965 z pakietów Debiana (sumy w `src/zewnetrzne/va/zrodla.txt`), statycznie z naszym musl → `build/va`; woła go `make` |
| `zbuduj-strefe.sh` | obraz strefy przeglądarki i pobierania z pakietów Debiana z `strefy/przegladarka.txt` (sumy) |
| `odswiez-strefe.py` | odświeża tabelę pakietów strefy (`strefy/przegladarka.txt`) — najnowsze wersje z Debiana; używa go też robot |
| `zbuduj-k1efi.sh` | zamknięte jądro `k1.efi` (linia poleceń z `jadro/cmdline-wydanie` i rdzeń w środku), podpis Secure Boot (ADR 0022); od 0.13.0 krok 4 (ADR 0059) także `k1-test.efi` z `console=ttyS0` — tylko do kopii testowej wydania |
| `zbuduj-usb.sh` | obraz na pendrive: K1-EFI w formacie 2 (A/B, grubenv, blok układu) + K1-DANE (ADR 0016, 0022); `--test`: `k1-<wersja>-test-usb.img` z `k1-test.efi` (bez paczki i ogłoszeń) |
| `zbuduj-iso.sh` | obraz ISO do maszyn wirtualnych (GRUB UEFI/BIOS → `k1.efi`; ADR 0010); `--test`: `k1-<wersja>-test.iso` z `k1-test.efi` |
| `zbuduj-jadro.sh` | pobiera jądro (suma + podpis PGP), konfiguruje, sprawdza fragment, kompiluje |
| `zbuduj-obraz.sh` | kompiluje programy, sprawdza binaria, składa `initramfs.cpio` i obrazy SquashFS |
| `uruchom-qemu.sh` | uruchamia K1 w QEMU (log szeregowy do pliku, monitor do gniazda); `--iso PLIK [--uefi] [--vga virtio]` — start z ISO |
| `zrzut-ekranu.sh` | zrzut ekranu działającej maszyny (PNG) |
| `testy.sh`, `test-*.sh` (także `test-ratunek.sh`, `test-tpm.sh`, `test-wydanie.sh`), `stanowisko.sh`, `przyjmij-wzorce.sh` | testy (to uruchamia CI) — co sprawdza każdy skrypt: [`docs/TESTY.md`](TESTY.md) |
| `robot.sh`, `porownaj-jadro.sh` | robot aktualizacji (ADR 0047): nowe jądro i pakiety strefy, porównanie konfiguracji przy nowej linii jądra (`docs/komponenty/robot.md`) |
| `opisy-wersji.py` | „Opis w K1:” z `CHANGELOG.md` do ogłoszenia wydania (ADR 0046); sprawdza, że każdy wpis od 0.10.5 go ma |
| `oprogramowanie-kart.sh` | pliki od producentów kart Wi-Fi i baza przepisów radiowych dla jądra (tabela `jadro/oprogramowanie-kart.txt`, sumy SHA-256; 0.9.1) |
| `uslugi-systemu.sh` | nazwy usług systemu z `K1_SYSTEM` (dla `Makefile` i `zbuduj-obraz.sh` — jedna lista) |
| `ppm-na-png.py` | podgląd grafiki na hoście: PPM → PNG |
| `sprawdz-binaria.sh`, `sprawdz-konfiguracje-jadra.sh`, `sprawdz-kspp.sh` | kontrole bezpieczeństwa |
| `sprawdz-powtarzalnosc.sh` | drugi build w osobnym katalogu i porównanie sum (powtarzalność) |
| `sprawdz-czytelnosc.py` | zasady czytelności (`AGENTS.md`): limity wielkości plików i funkcji, opis każdej usługi, aplikacji i komunikatu, wpisy „Tymczasowe”, spis ADR, istniejące ścieżki w dokumentach, każdy skrypt, test i fuzzer opisany, stan wersji w README i planie — pierwszy krok `test-m1.sh` |
| `wspolne.sh` | wspólne funkcje skryptów: `make_jadra` (make w katalogu jądra), `grub_poczatek` (początek każdego grub.cfg), `verity`, `obraz_fat`, `efi_podpisane`, `bateria_aml`, `zamknij_qemu` (koniec maszyny testowej), `log_k1` (log bez komunikatów jądra wpisanych w środek wiersza K1) |

## Jak powstaje obraz

```
jadro/wersja.env ──► pobranie + SHA-256 + podpis PGP ──► allnoconfig + jadro/k1.config ──► bzImage
                                                       └► allnoconfig + jadro/strefa.config ──► strefa-jadro (0.10.0) ──► /rdzen/strefy/jadro
                                                         (sprawdzenie każdej opcji)    └► nagłówki UAPI, gen_init_cpio
IBM Plex (fonts-ibm-plex) ──► narzedzia/czcionki.c (FreeType, host) ──► build/gen/k1_czcionki_dane.c
musl/wersja.env ──► pobranie + SHA-256 + podpis PGP + musl/latki ──► build/musl (libc.a, musl-gcc)
src/ ──► build/musl/bin/musl-gcc (static-pie, flagi SPEC §11, _FORTIFY_SOURCE=3 z src/zewnetrzne/fortify-headers)
         ──► k1-initramfs, init, nadzorca, usługi z K1_SYSTEM, aplikacje z K1_APLIKACJE, k1-test-systemu
         ──► sprawdzenie ELF i pułapek fortify (sprawdz-binaria.sh)
                     initramfs.cpio  ◄── /init (k1-initramfs)
                     rdzen.squashfs  ◄── init, system/{nadzorca + usługi z K1_SYSTEM}, aplikacje/{z K1_APLIKACJE},
                                         katalogi jadro/{dev,proc,sys}, strefy/{jadro, przegladarka.skrot — skrót obrazu
                                         przeglądarki (0.10.9 krok 4, ADR 0050; do 0.10.7 cały obraz przegladarka.img),
                                         dysk.img — pusty dysk strefy, mke2fs przy budowie (0.10.0 krok 5)}
                     strefa-przegladarka.img ◄── pakiety Debiana + gosc, kompozytor, przekaznik, agent + nasza Mesa (SquashFS zstd,
                                                 bloki 128 KiB — od 0.10.9 krok 4b; wcześniej xz) + drzewo
                                                 dm-verity; poza rdzeniem — w miejscu A/B za k1.efi
                     rdzen-test.squashfs ◄── + system/test-systemu (+ plik ze „#!” do testu negatywnego)
                     k1.efi ◄── to samo jądro + wbudowana linia poleceń (noinitrd) + initramfs z rdzen.squashfs,
                                podpis Secure Boot (klucz z K1_KLUCZ_PODPISU; bez niego — deweloperski)
                     k1-<wersja>.iso ◄── GRUB (UEFI + BIOS) + k1.efi; od 0.10.9 bez obrazu przeglądarki (jądro K1 nie ma
                                         napędu CD; ADR 0050)
                     k1-<wersja>-usb.img ◄── GPT: K1-EFI 1600 MiB (shim, GRUB, MokManager, grubenv, /k1/a.img = FAT z k1.efi
                                                  + obraz przeglądarki z drzewem dm-verity, 0.10.9; /k1/b.img; miejsca
                                                  po 768 MiB od kroku 4b) + K1-DANE; na dysku K1-EFI 3 GiB (z poczekalnią)
                                                  + K1-DANE
                     k1-<wersja>-test.iso, k1-<wersja>-test-usb.img ◄── to samo z k1-test.efi (log na porcie
                                                  szeregowym; 0.13.0 krok 4, ADR 0059) — tylko testy QEMU, nie wydanie
```
Wynik w `build/wynik/` (+ `SHA256SUMS`, `srodowisko.txt`). Build jest powtarzalny: stały
`SOURCE_DATE_EPOCH` (data ostatniego commita), stałe dane budowy jądra, `-frandom-seed`.

## Jak startuje system

```
QEMU (-kernel bzImage -initrd initramfs.cpio, dysk virtio = rdzen.squashfs) — testy
  albo ISO: firmware (UEFI/BIOS) → GRUB (tryb graficzny) → k1.efi
  albo pendrive/dysk (0.7.0): UEFI → shim (MS) → GRUB (Canonical) → grubenv (miejsce A/B, próba raz) →
       loopback /k1/<miejsce>.img → k1.efi (z Secure Boot: podpis sprawdzony przez shim, klucz K1 w MOK)
       0.13.0 (ADR 0058): 1 s na Esc → menu (K1, tryb ratunkowy, poprzednia wersja); dwa starty bez obrazu
       (k1_licznik) → menu z trybem ratunkowym; tryb ratunkowy = ten sam k1.efi z k1_tryb=r w grubenv
 └─ jądro 7.3 ─── otwiera /dev/console w initramfs, uruchamia /init (konsola: testy — port szeregowy ttyS0; wydanie od
                  0.13.0 — ttynull, nic na zewnątrz, ADR 0059; błąd krytyczny PID 1 → ekran awarii: panika jądra
                  z opisem K1 na obrazie, po 60 s ponowne uruchomienie)
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
        │  fd 3 = kanał od nadzorcy (klient ekranu: do ekranu), kolejne — kanały z wiersza usługi (k1_uslugi.h)
          ├─ skarbiec (uid 103) — partycja K1-DANE (dane zaszyfrowane; nadzorca czeka na nią do 3 s, z pendrive'em do 15 s), potem
          ├─ ekran (101; karta graficzna, składanie okien) → wejscie (100; klawiatura, mysz)
          ├─ gui (102; pulpit) albo logowanie (104), gdy jest hasło
          │  [tryb ratunkowy (0.13.0, ADR 0058): tylko ekran, wejscie i ekran ratunkowy (logowanie --ratunek);
          │   raport na pendrive pisze instalator (105)]
          ├─ siec (108) przy starcie: adres od routera, zegar z internetu, Wi-Fi
          ├─ na prośbę gui: aplikacje z K1_APLIKACJE (UID 200+); strefy (300+, VMM — przeglądarka);
          │  instalator (105) na prośbę okna Instalatora albo Ustawień → Aktualizacje (też po pobraniu wersji z sieci);
          │  czytnik (106, pendrive) na prośbę Plików; dekoder (107, jeden obraz) na prośbę Plików albo Podglądu;
          │  strefa pobierania (bez okna) — sprawdzanie i pobieranie aktualizacji (aktualizacja.c, ADR 0046)
          ├─ filmy (109) i posrednik (110) przy starcie: jednorazowe pytanie, co umie karta (Ustawienia); przy strefie
          │  z przełącznikiem — dekoder filmów (400 + nr) i pośrednik grafiki (410 + nr) tej strefy, jedyne z węzłem
          │  render karty (0.10.9, ADR 0049)
          restart po awarii (0,2/1/2/4/4 s, maks. 5 na minutę); koniec ekranu → restart klientów;
          potwierdzenie startu A/B (rozruch.c): 15 s pulpitu albo ekranu hasła, liczone od pierwszego obrazu na ekranie
          (EKRAN_OBRAZ, 0.10.9 krok 2)
          szczegóły (kanały, deskryptory, etapy startu): docs/komponenty/nadzorca.md
          [obraz testowy: test-systemu (root, fd 3: kanał testowy) → po jego końcu nadzorca kończy się]
```
Szczegóły i tabela montowań: ADR 0004. Błędy: ADR 0005.

## Konwencje
- **Język:** komunikaty w systemie, dokumentacja, PR-y — po polsku; identyfikatory i komentarze
  w kodzie oraz commity — po angielsku.
- **Kod C:** małe moduły, jawne limity długości, sprawdzanie każdego błędu, bez `strcpy/strcat/sprintf`
  (tylko `snprintf`/`strncat` z limitem). Flagi w `Makefile` — zmiana tylko przez ADR.
- **Prefiksy logów:** `K1-INITRAMFS:`, `K1:` (init), `K1-NADZORCA:`, `K1-KLATKA:`, a dalej `K1-<NAZWA>:` każdej usługi
  (`K1-SKARBIEC:`, `K1-EKRAN:`, `K1-GUI:`…) i aplikacji (tag z `K1_APLIKACJE`, np. `K1-PLIKI:`), `K1-TEST:`, `K1-PROBA:`;
  błędy krytyczne PID 1 `BŁĄD KRYTYCZNY:` (`k1_awaria` — wyłączenie), błędy usług `BŁĄD:` (`k1_koniec` — wyjście z kodem 1).
- **Kanały usług:** kanał od nadzorcy to zawsze fd 3. Komunikaty: stały rozmiar, wersja, typ, długość,
  pole zarezerwowane = 0, odbiorca sprawdza każde pole (SPEC §12).
- **Ścieżki w systemie** po polsku (`/rdzen`, `/dane_uzytkownika`, `/rdzen/system/…`).
- **Komponent = dokument** w `docs/komponenty/` (cel, granica zaufania, uprawnienia, wejścia/wyjścia,
  zależności, syscalle, błędy, powierzchnia ataku, testy) — SPEC §2.6.
- **Statusy:** ZAIMPLEMENTOWANE / PRZETESTOWANE (z dowodem) / PLANOWANE — SPEC §2.4.

## Wersjonowanie
SemVer od 0.1.0 w `VERSION`; tagi `k1-vX.Y.Z` zakłada CI po udanej budowie na `main`, gdy `VERSION` ma nową wersję
(testy przeszły wcześniej na PR-ze; stare tagi `v0.1.x` to Kons-OS na Debianie, ADR 0001). Wydanie ma ISO, obraz USB,
ogłoszenie i paczkę — K1 na dysku pobiera z niego nową wersję sam (ADR 0046). Wersje PATCH z poprawionym jądrem i
pakietami przeglądarki wydaje robot (ADR 0047); MINOR i kroki planów — ludzie.

| Zmiana | Co podbić |
|---|---|
| nowy temat z mapy drogi (ADR 0035: 0.9 Sieć, 0.10 Strefy…) | MINOR: 0.8.x → 0.9.0 |
| kolejny krok planu wersji, poprawka, aktualizacja jądra | PATCH: 0.8.1 → 0.8.2 (jądro i pakiety strefy w linii — robot) |
| tylko dokumentacja / CI / repo | bez zmiany |

## Ograniczenia środowiska pracy
- Kontener deweloperski bez `/dev/kvm` → QEMU w emulacji TCG (start K1 ~1–2 s, pierwsza klatka ~2 s,
  budowa od zera ok. 5 min, z jądrem dłużej; wszystkie testy ok. 40 min, 4 rdzenie). QEMU: karta `-vga std` (sterownik `bochs`) albo przy ISO `virtio-vga` (simpledrm), PS/2.
- Sieć kontenera: tylko HTTPS przez proxy; dozwolone m.in. apt Ubuntu, deb.debian.org, GitHub,
  cdn/www/git.kernel.org.
- CI (GitHub Actions): PR z etykietą „testy” — budowa i pełne testy (z KVM), `main` — sama budowa i wydanie (repo
  publiczne — minuty bez limitu, ADR 0040; przebieg trwa ok. 30 min, więc zmiany łączymy w paczki).
