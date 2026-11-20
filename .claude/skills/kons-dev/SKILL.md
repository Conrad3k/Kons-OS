---
name: kons-dev
description: Praca nad Kons K1 Sovereign — własnym systemem operacyjnym na jądrze Linux (nie dystrybucją), z całą przestrzenią użytkownika w C (statycznie, musl). Zawiera architekturę, niezmienniki bezpieczeństwa, zasady czytelności, przepisy (nowa aplikacja, usługa, kanał, opcja jądra, wygląd), testy w QEMU i ich pułapki, plan etapów, wersje, scalanie, CI i raport dla właściciela. Używaj przy każdej pracy w repo Kons-OS (kod C, jądro, skrypty, testy, CI, dokumentacja, plan) i gdy właściciel zgłasza uwagi albo pyta o kierunek.
---

# Praca nad K1

Ten skill to **skrót i przewodnik**. Prawdą są dokumenty w repo — gdy skill i dokumenty się różnią,
wygrywają dokumenty, a skill poprawiasz w tym samym PR-ze.

| Dokument | Co w nim jest |
|---|---|
| `AGENTS.md` | wspólne zasady Claude i Codexa: właściciel, bezpieczeństwo, praca we dwóch, **czytelność** |
| `docs/SPEC.md` | specyfikacja nadrzędna; zmiany tylko przez ADR (`docs/adr/`, dopisek `[korekta: ADR NNNN]`) |
| `docs/DO_ZROBIENIA.txt` | **co teraz i co dalej**, decyzje właściciela, rzeczy tymczasowe |
| `docs/ARCHITEKTURA.md` | mapa repo, przebieg startu, konwencje, wersjonowanie |
| `docs/SLOWNIK.md` | pojęcia K1 — jedno słowo, jedno znaczenie |
| `docs/TESTY.md` | warstwy testów, `k1qemu.py`, jak uruchamiać |
| `docs/komponenty/*.md` | każda usługa: cel, granica zaufania, uprawnienia, kanały, testy |
| `docs/RYZYKA.md`, `docs/ZALEZNOSCI.md`, `docs/WYGLAD.md` | znane braki, obce biblioteki, ustalenia wyglądu |

## 1. Na start sesji

1. `git fetch origin main`; gałąź `claude/…` od `origin/main` (scalona wcześniej → `git checkout -B <gałąź>
   origin/main`; wypychanie z nadpisaniem jest zablokowane → `git merge -s ours origin/<gałąź>` i zwykły push).
2. Otwarte PR-y i zgłoszenia (`list_pull_requests`, `list_issues`: etykiety `feedback`, `błąd`, `pomysł`).
   Punkt z `DO_ZROBIENIA.txt` oznacz `[w toku: Claude, gałąź …]` w pierwszym commicie; punktów Codexa nie ruszaj.
3. Ostatni run „K1 — budowa i testy” na `main` (od 2026-11-17 sama budowa) — czerwony naprawiasz najpierw. **Robot**
   (0.10.5, ADR 0047): otwarte PR-y robota (gałęzie `robot/…`, etykieta „robot”), Issue „robot”, ostatni bieg workflow
   „Robot — poprawki jądra i przeglądarki” — Issue robota naprawiasz jak czerwone CI.
4. Środowisko: `scripts/przygotuj-srodowisko.sh && scripts/zbuduj.sh` (szybko: `make && make -j4 -O testy-host`).
   Kontener nie ma KVM — QEMU w emulacji; pełny komplet testów robi CI na PR-ze (§9).

## 2. Czym jest K1

**Nie dystrybucja.** Z Linuksa bierzemy tylko jądro (własna, minimalna konfiguracja). Reszta to nasz kod
w C, statycznie z musl: bez powłoki, pakietów, systemd, X11/Wayland, bibliotek współdzielonych i drzewa
FHS (`/usr`, `/etc`, `/bin`…). Docelowo dwa światy (SPEC §4): **natywny** (nasz kod) i **strefy** —
mikro-maszyny wirtualne dla obcych aplikacji (0.10, ADR 0020). Pliki między nimi tylko przez Pliki K1
i Gatekeepera (ADR 0027).

**Start (wydania, od 0.7.0):**
```
firmware UEFI → shim (Microsoft) → GRUB (Canonical) → k1.efi z miejsca A albo B w K1-EFI
  k1.efi = jądro + wbudowana linia poleceń + initramfs z rdzeniem, podpisane kluczem K1 (MOK)
→ /init = k1-initramfs: rdzeń (SquashFS + dm-verity, skrót z linii poleceń) → switch_root
→ /rdzen/init (PID 1): sysctl, montowania, sieroty, sygnały, wyłączanie
→ /rdzen/system/nadzorca (root): usługi i aplikacje w klatkach, kanały, logi, restarty
     skarbiec (103) → ekran (101) → wejscie (100) → gui (102) albo logowanie (104, gdy jest hasło)
     aplikacje (200+) na prośbę gui; instalator (105) na prośbę okna Instalatora; czytnik (106) na prośbę Plików, dekoder (107) na prośbę Plików i Podglądu
     siec (108) przy starcie: adres od routera (DHCP) dla karty z kablem, adres ustawia nadzorca (0.9.0, ADR 0039);
       potem zegar z internetu (kilka serwerów, większość zgodna), zegar ustawia nadzorca; Wi-Fi (0.9.1, ADR 0041): kartę,
       radio (rfkill) i szukanie sieci (nl80211) obsługuje nadzorca, opisy sieci z powietrza czyta siec w klatce
     filmy (109: pytanie przy starcie, co karta rozpakowuje → Ustawienia; 400 + nr: dekoder filmów strefy nr — węzeł render
       i915, prośby ze strefy pole po polu, klatki NV12 w oknie; 0.10.9 krok 2, ADR 0049; strona strefy — krok 3)
     posrednik (110: pytanie przy starcie, czy karta obsługuje szybką grafikę → Ustawienia; 410 + nr: pośrednik grafiki
       strefy nr — most native context, prośby ze strefy pole po polu, bufory jako dma-buf dla VMM-a; 0.10.9 krok 5,
       ADR 0049 pkt 7; strona strefy — krok 6)
     strefa (300+, na prośbę gui; 0.10.0, ADR 0042): VMM jednej mikro-maszyny KVM — /dev/kvm, jądro i obraz strefy od
       nadzorcy, jądro strefy (jadro/strefa.config) startuje bez firmware, urządzenia virtio-mmio, okno w ramce ekranu
```
Testy QEMU startują też `bzImage` + `initramfs.cpio` (tylko `test-m1`). Obraz przeglądarki (0.10.9 krok 4, ADR 0050) nie
jest w rdzeniu: leży w miejscu A/B zaraz za k1.efi (na końcu obrazu miejsca, manifest go obejmuje), w rdzeniu tylko jego
skrót; nadzorca zakłada dm-verity i daje go VMM-owi (fd 6). W testach z rdzeniem na `/dev/vda` — drugi dysk virtio
(`uruchom-qemu.sh --obraz-strefy`). ISO nie ma obrazu przeglądarki (jądro bez napędu CD). ISO/USB mają GRUB-a do 0.12 (ADR 0010; dawne M10).

**System plików po starcie** (ADR 0004): `/` — tmpfs tylko do odczytu z trzema katalogami: `rdzen`
(SquashFS ro, nosuid, nodev; w nim `init`, `system/`, `aplikacje/`, `jadro/{dev,proc,sys}`), `aplikacje`
(pusty do 0.12), `dane_uzytkownika` (tmpfs noexec — dane tymczasowe). Trwałe dane są w **skarbcu**
(partycja GPT „K1-DANE”, własny format, Monocypher; ADR 0015), nie w systemie plików. `/proc` ma
`hidepid=invisible,subset=pid`. Nie używaj ścieżek `/dev`, `/proc`, `/sys`, `/tmp`, `/etc`.

**Aktualizacje (M6, ADR 0021, 0022):** wydanie ma manifest z podpisem EdDSA (`narzedzia/podpisz.c`, sekret CI
`K1_KLUCZ_PODPISU`; bez sekretu = wersja deweloperska, `K1_KLUCZ_JEST 0`). K1-EFI (format 2, `k1_uklad.h`):
FAT32 1600 MiB, blok układu, grubenv (`k1_slot/k1_nowy/k1_start/k1_proba`), `/k1/a.img` i `/k1/b.img`
(768 MiB od 0.10.9 krok 4b — rozmiar w bloku układu, do kroku 4 128 MiB: k1.efi + obraz przeglądarki + manifest
z identyfikatorem wydania w ostatnim MiB; partycja na dysku 3 GiB z poczekalnią; inny rozmiar dysku i wydania →
`K1_INST_BLAD_UKLAD`, instalacja od nowa). Aktualizacja idzie do wolnego miejsca, nowa wersja
startuje raz na próbę, `rozruch.c` w nadzorcy zatwierdza ją po 15 s albo zgłasza wycofanie. Instalator
odmawia niepodpisanych i starszych wersji. Klucz Secure Boot K1 wpisuje się przez MokManagera (`efi.c`:
`MokNew`/`MokAuth`). Pliki startowe (shim, GRUB, `grub.cfg`…) sprawdza **manifest plików startowych** (format 3, obok
manifestu miejsca A: skrót drzewa plików `k1_fat32`, bez grubenv i miejsc); aktualizacja je wymienia, gdy się różnią —
nowe obok starych, przełączenie jednym zapisem sektora startowego FAT (`src/instalator/start.c`, 0.9.0, ADR 0038).

**Sprzęt właściciela:** ThinkPad X230 (8 GB, SATA/AHCI, PS/2, EHCI+xHCI, Intel HD 4000 — od 0.10.9 krok 2 przez i915, wcześniej simpledrm,
TPM 1.2; kabel: Intel 82579LM, `e1000e`) i od 2026-11-13 Lenovo IdeaPad 110-15ISK (Skylake, tylko xHCI, Intel HD 520;
kabel 10/100 i Wi-Fi Qualcomm Atheros — ath10k, potwierdzone przez właściciela 2026-11-14; Ustawienia → Sieć pokazują identyfikatory kart), jedno konto na komputer.
X230 to maszyna testowa — szybka grafika i gry celują w nowszy sprzęt. Sterowniki — zawsze z jądra, nie własne (D0).

## 3. Mapa kodu

| Kod | W obrazie | Rola |
|---|---|---|
| `src/initramfs/` | `/init` | montuje rdzeń (dm-verity), przełącza korzeń |
| `src/init/` | `/rdzen/init` | PID 1 |
| `src/nadzorca/` | `system/nadzorca` | `main.c` pętla; `uslugi.c` usługi z tabel `K1_SYSTEM`/`K1_APLIKACJE` (start, kanały, restarty); `kanaly.h` nazwy kanałów `KANAL_*`; `logi.c` logi usług; `profile.c` klatki (`KLATKA_<NAZWA>`); `skarb.c` etapy startu; `urzadzenia.c`, `uevent.c`, `dyski.c`, `bateria.c`, `zasoby.c`, `instalacja.c` (dwa okna: Instalator i Ustawienia → Aktualizacje; prawa — czyste `inst_prawa.c`, 0.10.5), `aktualizacja.c` + `aktualizacja_ust.c` (aktualizacja przez sieć, 0.10.5 krok 2, ADR 0046: kiedy sprawdzać, strefa pobierania, program zapisujący, stan dla Ustawień i gui — `docs/komponenty/aktualizacja.md`), `rozruch.c` (nieudana próba → `AB_NIEUDANA` do gui), `obraz_strefy.c` (0.10.9 krok 4, ADR 0050: obraz przeglądarki z miejsca A/B tego wydania pod dm-verity, skrót z rdzenia `/rdzen/strefy/<obraz>.skrot`; loop + dm-verity wspólne z initramfs — `src/wspolne/k1_verity.c`), `efi.c`, `pendrive.c`, `dekoder.c`, `powieksz.c` (K1-DANE do końca pendrive'a), `siec*.c` (karty sieciowe, adres, stan dla ikony i Ustawień, zegar z internetu; 0.9.0), `wifi.c`, `wifi_nl.c` (karta Wi-Fi, przełącznik radia przez `/dev/rfkill`, szukanie sieci nl80211, lista dla Ustawień; 0.9.1), `wifi_polacz.c` (łączenie na prośbę siec: CONNECT, gniazdo EAPOL, klucze NEW_KEY, SET_STATION; kanał Ustawienia → siec `KN_SIEC`; 0.9.1 krok 2), `test.c` |
| `src/wejscie/` | `system/wejscie` | klawiatura, mysz, kółko (evdev, EVIOCGRAB); `touchpad.c` — touchpad we własnym trybie (palce → ruch, przewijanie dwoma palcami w kierunku naturalnym D8, stuknięcia, przycisk touchpada; czysty, `test_touchpad`; 0.10.0 krok 4) |
| `src/ekran/`, `src/grafika/` | `system/ekran` | DRM/KMS, składanie okien, kursor, tapety, znak K1; `zalegle.c` — to, czego pełny kanał klienta nie przyjął, czeka w ekranie (RAMKA nigdy nie przepada, ruchy się scalają; 0.8.5); `opis.c` — opis ekranu dla testów (0.10.2, ADR 0045: tylko rola `TEST`); `rozmiar.c` — zmiana rozmiaru i maksymalizacja okna strefy, obszar roboczy z paneli, rozmiar sesji w skarbcu (0.10.5 krok 4: `ROZMIAR` do VMM, zamiana powierzchni `POWIERZCHNIA` e = 1 tylko w rozmiarze, o który prosił ekran) |
| `src/gui/` | `system/gui` | pulpit w modułach (`gui.h`): warstwa ikon, paski, menu Start, menu kontekstowe, rozmieszczenie w skarbcu (fd 6); `sesje.c` okienko „Którą sesję otworzyć?” (0.10.1 krok 4), `pamiec.c` fokus do nadzorcy i okienko „Mało pamięci” (krok 3); `wersja.c` okienko „Nowa wersja K1 nie uruchomiła się” (0.10.5 krok 1); `przelacznik.c` przełącznik okien Alt+Tab (0.10.5 krok 6: skrót rozpoznaje ekran — `wskaznik.c`, strefa go nie dostaje); Przeglądarka na pasku i pulpicie — pozycja `POZYCJA_MENU_STREFY`, klucz `ROZM_STREFA`, zapis `K1R3` |
| `src/logowanie/` | `system/logowanie` | ekran hasła przy starcie |
| `src/skarbiec/` | `system/skarbiec` | `magazyn.c` format i szyfrowanie, `main.c` usługa, `obiekty.c`, `haslo.c`, `dokumenty.c` + `dok_*.c` pliki, foldery, Kosz, naprawa przy starcie, granica czasu, prawa, zapis w toku kawałkami (0.8.0–0.8.1, `dok_wew.h`); limity, indeks, kompaktowanie, rezerwa, kawałki — ADR 0034; `miejsce.c` miejsce na pliki i powiększenie K1-DANE (ADR 0033); format 2 (0.8.2, ADR 0036): `odcinki.c`, `spis.c`, `przerwy.c` (kompaktowanie krokami między prośbami); format 1 i jego przejście — usunięte w 0.10.0 (nagłówek formatu 1 tylko rozpoznany: `K1_MAG_STARY`, nietknięty); `strefy.c` dyski stref (0.10.0 krok 5: obiekty `K1_OB_DYSK` + kawałek tylko dla właściciela-strefy, bez rezerwy plików, `SKARB_WYCZYSC`, część „Strefy”) |
| `src/instalator/` | `system/instalator` | zapis K1 na dysk (GPT), tylko urządzenia od nadzorcy; `start.c` — wymiana plików startowych (0.9.0); prosi o niego okno Instalatora (instalacja) albo Ustawienia → Aktualizacje (aktualizacja, 0.10.5); `siec.c` + `siec_instaluj.c` — aktualizacja przez sieć (0.10.5 krok 2, ADR 0046): ogłoszenie i paczka ze strefy pobierania, poczekalnia za FAT-em K1-EFI dysku, „Zainstaluj”, „Wróć” (test `test_siec_instalatora`); formaty `src/wspolne/k1_ogloszenie.h`, `k1_paczka.h`, narzędzie `narzedzia/wydanie.c` |
| `src/strefa/` | `system/strefa` | VMM strefy (0.10.0, ADR 0042): `kvm.c` maszyna i procesory (wątki; w emulacji QEMU 1 procesor i gość z `highres=off` — 0.10.5 krok 7), `start.c` start jądra strefy bez firmware (strona zerowa, e820, tablica MP), `virtq.c` + `mmio.c` kolejki i rejestry virtio (czyste: `test_virtio`, `fuzz_virtq`), urządzenia `blk.c` (obraz, tylko odczyt), `dane.c` + `dysk.c` (dysk strefy, która pamięta — 0.10.0 krok 5: kawałki 64 KiB w skarbcu przez fd 8, pusty dysk ext4 `/rdzen/strefy/dysk.img` na fd 9, osobny wątek, pamięć podręczna `dysk.c` czysta — `test_dysk_strefy`), `konsola.c`, `rng.c`, `wejscie.c`, `gpu.c` (2D; obraz z pamięci gościa przez czyste `tlo.c` — `test_tlo`, 0.10.5; od 0.10.9 krok 6 z pośrednikiem grafiki na fd 10 także 3D: `gpu_most.c` czyste polecenia — capset DRM, konteksty gościa, zasoby blob, mapowanie w obszarze 32 GiB + 1 GiB, `SUBMIT_3D` z płotem — `test_gpu_most`, `fuzz_gpu_most`; `gpu_lacze.c` wątek kolejki, kanał pośrednika, miejsca pamięci KVM; rejestry pamięci dzielonej w `mmio.c`), `vsock.c` (port 1: kanał sterujący — od 0.10.1 też „liczniki”, „kto”, linie z liczbami przez `linia.c`; port 2: połączenia przeglądarki z kredytem), `balon.c` (0.10.1 krok 2, ADR 0044: balon — zgłaszanie wolnych stron, cel od nadzorcy; czyste `strony.c`: `test_balon`, `fuzz_balon`), tabela `K1_URZADZENIA` w `strefa.h`; pamięć gościa: górna granica od nadzorcy (`pamiec_stref.c`), ponad 3 GiB druga część od 4 GiB; `okno.c` okno u ekranu; łącznik (krok 2, ADR 0043): `lacznik.c` SOCKS5 i DNS (czyste: `test_lacznik`, `fuzz_lacznik`), `polaczenia.c` stany połączeń i gniazda od nadzorcy (fd 7) — cały most (przekaźnik → vsock → łącznik → nadzorca) to
**Raptor Bridge**: `docs/komponenty/raptor-bridge.md`, test w strefie `scripts/test-most.sh` (0.10.5 krok 5: `gosc/most.c`, `fuzz_vsock`). Programy w strefie (nasze, statycznie): `gosc/` — `/init` (strefa próbna z trybami złośliwymi; tryb przeglądarki `przegladarka.c`; pamięć strefy `pamiec.c` — zram, kolejność przy braku pamięci, krok 3), `kms.c` wyświetlacz gościa, `kompozytor/` — Wayland bez libwayland (tabela `K1_INTERFEJSY`), `przekaznik/` — SOCKS5 127.0.0.1:1080 → vsock. Tabela stref `K1_STREFY` (`k1_uslugi.h`; sesje przeglądarki Ogólna/Bank/Prywatna, kolumna linii menu Start; od 0.10.5 kolumna „okno” — strefa pobierania bez okna: `gosc/pobieranie.c`, curl, łącznik tylko do `K1_HOSTY_POBIERANIA`), nadzorca `strefy.c` (powód końca: `strefy_powod.c`, 0.10.1) + `strefy_balon.c` (pamięć stref, cel balonu, rozdzielacz — zasada w czystym `pamiec_stref.c`; 0.10.1) + `strefy_siec.c` (gniazda dla stref, reguła D3 `k1_siec_dla_strefy`) + `strefy_ust.c` (sesje: pamięta / zapomina, nazwy — w skarbcu, format `sesje.c`; `STREFY_STAN`, `STREFA_NAZWA` do gui, ekranu, Ustawień; 0.10.0 krok 5), ramka `src/ekran/strefy.c` (krok 3: pasek tytułu 40 px `K1_STREFA_TYTUL` nad obrazem + obwódka; od 0.10.7 w kolorach okna K1, kolor sesji w kropce i linii pod paskiem — ADR 0048; powód końca strefy — kod wyjścia VMM → nadzorca `strefa_koniec` → okienko gui „<tytuł> zamknęła się”); obraz przeglądarki `strefy/` + `scripts/zbuduj-strefe.sh`, `scripts/odswiez-strefe.py` |
| `src/siec/` | `system/siec` | adres od routera: klient DHCP w klatce (`dhcp.c` pakiety, `klient.c` stany i czasy); zegar z internetu (`sntp.c` pakiety, `czas.c` runda i zgodność źródeł, `zegar.c` gniazda; krok 8); gniazdo pakietowe, karty, kabel i ustawienie adresu — nadzorca `siec.c`, `siec_jadro.c`; stan dla ikony i Ustawień — `siec_stan.c`; gniazda UDP do serwerów czasu i zegar — `siec_zegar.c` (0.9.0, ADR 0039); Wi-Fi (0.9.1, ADR 0041): `wifi.c` opisy sieci z powietrza (nazwa do pokazania, zabezpieczenie) → lista, `lista.c` wyniki od nadzorcy w memfd i lista do niego (ze stanem połączenia); krok 2: `eapol.c` ramki EAPOL-Key, `wpa.c` logowanie WPA2 (czyste, `test_wpa`, `fuzz_eapol`), `polacz.c` połączenie, `wybor.c` wybór sieci i kanał Ustawień (fd od `SIEC_USTAWIENIA`), `pamiec.c` + `skarb.c` zapamiętane sieci w skarbcu (fd 4); kryptografia `src/wspolne/k1_sha1.c`, `k1_aes.c`, `k1_wymaz.h` |
| `src/filmy/` | `system/filmy` | dekoder filmów (0.10.9 krok 2, ADR 0049): `main.c` kanał, okno (memfd z pieczęciami; prośba kopiowana przed sprawdzeniem), prośby; `karta.c` + `karta_h264.c` VA-API (libva + i965 + libdrm statycznie — `scripts/zbuduj-va.sh`, `src/zewnetrzne/va/zrodla.txt`); `testowy.c` szare klatki (tylko na prośbę programu testowego, `TEST_FILMY`); `obce.c` + `obce.txt` (`dlopen` daje i965, funkcje plikowe obcych bibliotek kończą się błędem bez wywołania jądra — `--wrap`); format `src/wspolne/k1_filmy.{h,c}` (tabela `K1_KODEKI`, `k1_filmy_sprawdz`; `test_filmy`, `fuzz_filmy`), komunikaty `k1_kom_filmy.c`; nadzorca `filmy.c` (węzeł render tylko i915, pytanie przy starcie, `filmy_dla_strefy`, pamięć w rozdzielaczu); dekoder strefy = wiersz strefy (`K1_PROFIL_FILMY_<STREFA>`, UID 400 + nr) |
| `src/posrednik/` | `system/posrednik` | pośrednik grafiki (0.10.9 krok 5, ADR 0049 pkt 7): strona K1 mostu native context — `main.c` kanał, okno i pamięć wspólna (memfd z pieczęciami; strumień kopiowany przed sprawdzeniem), dma-buf bufora dla VMM-a; `prosby.c` prośby (układ virglrenderera, wzór — nie biblioteka; mniej niż on: bez relokacji, płotów i wskaźników gościa); `plot.c` płoty i zawieszenie (koniec kodem 3); `karta.c` (pełne PPGTT, izolacja, softpin, płoty — inaczej „nie obsługuje”: X230); `testowy.c` silnik testowy (`TEST_POSREDNIK`; od kroku 6 „pamięć karty” — memfd od nadzorcy); od kroku 6 konteksty gościa (`POSR_GOSC_KONIEC`) i pośrednik strefy z jej VMM-em przy włączonym przełączniku sesji (`sesje.c` format `K1Z2`, `STREFA_GRAFIKA`/`STREFY_GRAFIKA`, znak na ramce `K1_IK_KARTA`); w strefie nasza Mesa (`scripts/zbuduj-mese.sh`, `src/zewnetrzne/mesa/zrodla.txt`) i `kompozytor/dmabuf.c` (linux-dmabuf v4); format `src/wspolne/k1_posrednik.{h,c}` (tabele `K1_POSR_PROSBY`, `K1_POSR_IOCTLE`, tabela buforów, pamięć strefy `K1_POSR_PAMIEC`; `test_posrednik`, `fuzz_posrednik`), komunikaty `k1_kom_posrednik.c`; nadzorca `posrednik.c` + wspólne z dekoderem filmów `karta.c`; pośrednik strefy = wiersz strefy (`K1_PROFIL_POSREDNIK_<STREFA>`, UID 410 + nr) |
| `src/czytnik/` | `system/czytnik` | pendrive dla Plików: FAT w klatce, tylko odczyt (0.8.0, ADR 0031); nadzorca `pendrive.c` |
| `src/dekoder/` | `system/dekoder` | jeden obraz (PNG, JPEG, GIF, BMP, WebP) → piksele, jednorazowy proces w klatce (Wuffs; 0.8.0, ADR 0032); plik kawałkami, limity w jednej tabeli `k1_dekoder.h`, dekodery w `K1_DEKODERY` (0.8.2); nadzorca `dekoder.c` (tabela klientów: Pliki, Podgląd), Pliki `obrazy.c`, Podgląd `obraz.c`, ekran `zdjecie.c` (tabela nadawców tapety) |
| `src/aplikacje/<program>/` | `aplikacje/<program>` | Zegar, Notatki, Ustawienia, Zasoby, Instalator (`instalacja`), Kalkulator, Pliki (0.8.0), Wybór pliku (`wybor`), Podgląd (`podglad`, 0.8.2: zdjęcie w oknie, zdjęcia jednego folderu — ADR 0037; 0.8.5: w ruchu szybko z kopii — `skala.c`, po zatrzymaniu `k1_skaluj`); Ustawienia → Miejsce (`miejsce.c`, 0.8.5: części z tabeli `K1_CZESCI` przez `SKARB_PODZIAL`/`SKARB_CZESC`, tylko liczby); Ustawienia → Aktualizacje (`aktualizacje.c`, 0.10.5 krok 1: wersja, A/B, aktualizacja z pendrive'a — kanał fd 7 `KANAL_AKTUALIZACJI`; okno 720 × 520); Instalator od 0.10.5 tylko instaluje |
| `src/przybornik/` | (linkowane) | okno aplikacji (`k1_aplikacja`), rysowanie, ikony `K1_IK_*`, tekst, liczby, czas i klawiatura PL, zdjęcie przez dekoder (`k1_dekodowanie`: prośba, kawałki od skarbca, odpowiedź — Pliki i Podgląd), menu pod prawym przyciskiem (`k1_menu`: wygląd i wiersze menu pliku — jedno dla pulpitu i Plików, 0.8.2), przeciąganie (`k1_przeciaganie`: jedna tabela reguł źródło/cel dla Plików i pulpitu, okienko pytania) i zaznaczanie wielu (`k1_zaznaczenie`, 0.8.2 krok 6) |
| `src/wspolne/` | (linkowane) | `k1_uslugi.h` **listy usług i aplikacji**, `k1_komunikat` (32 B), `k1_kanal`, `k1_odbior`, `k1_klatka`, `k1_log`, `k1_skarb_klient`, `k1_memfd`, `k1_indeks` (indeks po kluczu, 0.8.1), `k1_dokument` (foldery, **typy plików `K1_TYPY`** i ich rodziny, wpis pliku) + `k1_nazwy.c` (zasady nazw), `k1_pendrive` (lista folderu z pendrive'a), `k1_dekoder.h` (limity dekodera), `k1_skaluj`, `k1_efi`, `k1_uklad`, `k1_manifest`, `k1_fat32` (drzewo plików K1-EFI, 0.9.0), `k1_wifi` (nazwy sieci, kreski, format wyników szukania; 0.9.1), `k1_nl` (generic netlink — tylko nadzorca i program testowy; 0.9.1), `k1_sha256`, `k1_bajty.h`, `k1_io.h`, `k1_teraz.h` |
| `src/zewnetrzne/` | (linkowane) | Monocypher (skarbiec), Wuffs (dekoder, 0.8.0), fortify-headers (każdy program) i od 0.10.9 libva + i965 + libdrm (dekoder filmów; tylko sumy źródeł `va/zrodla.txt`, budowa `scripts/zbuduj-va.sh`) — jedyne obce biblioteki w systemie (`docs/ZALEZNOSCI.md`) |
| `narzedzia/` | (host) | czcionki IBM Plex → bitmapy, ekran startowy GRUB-a, podpis, klucz Secure Boot |
| `jadro/` | `k1.efi`, `bzImage` | `k1.config`, `k1-zamkniete.config`, `k1-odrzucone.txt` + `strefa-odrzucone.txt` (opcje „domyślnie tak”, których świadomie nie chcemy — robot pyta o resztę, `scripts/porownaj-jadro.sh`, ADR 0047), `README.md` (zmiana wersji jądra; robot: `scripts/robot.sh`, `.github/workflows/robot.yml`), `oprogramowanie-kart.txt` (pliki od producentów kart Wi-Fi wbudowane w jądro — jedna tabela z sumami; 0.9.1, ADR 0041) |
| `musl/` | (w każdym programie) | biblioteka C: `wersja.env`, `klucze/`, `latki/`, `README.md` (zmiana wersji, łatki) |
| `testy/host/`, `testy/system/`, `testy/qemu/` | — | testy na hoście i fuzzing; program testowy w systemie; biblioteka `k1qemu.py` |
| `scripts/` | — | budowa (`zbuduj*.sh`), testy (`testy.sh`, `test-m*.sh`, `test-pliki.sh`, `test-pendrive.sh`), sprawdzenia (`sprawdz-*.sh`), `wspolne.sh` |

## 4. Niezmienniki — nie wolno złamać

Każdego pilnuje test. Zmiana któregokolwiek = zmiana architektury → ADR i decyzja.

1. **Nic nie jest jednocześnie zapisywalne i wykonywalne.** Korzeń i `/rdzen` ro, zapis tylko w noexec,
   `vm.memfd_noexec=2`, bez `BINFMT_SCRIPT` (`#!`) i `binfmt_misc`. Dlatego żadnego JIT w świecie natywnym.
2. **Brak powłoki, terminali i interpreterów:** bez `/bin/sh`, getty, VT, pty/devpts, SysRq. Log tylko
   jednokierunkowo na port szeregowy.
3. **Jądro tylko przez fragment** `jadro/k1.config` na `allnoconfig`, każda linia z komentarzem. Bez modułów,
   swapu, hibernacji, io_uring, BPF, userfaultfd, kexec, 32-bit, IPv6 i zapory (ADR 0039: IPv4 od 0.9.0, gniazdo
   sieciowe otwiera tylko nadzorca, jądro nie wysyła ICMP — sysctl w init); lockdown confidentiality,
   Landlock, Yama.
4. **Programy: statyczne PIE z flagami SPEC §11** (`-Werror`), sprawdzane przez `scripts/sprawdz-binaria.sh`.
   Od 0.9.0: nasz musl 1.2.5 (`musl/`: suma, podpis PGP, łatki; `scripts/zbuduj-musl.sh` → `build/musl`) i
   `_FORTIFY_SOURCE=3` z fortify-headers (`src/zewnetrzne/fortify-headers`) — przepełnienie = SIGILL (pułapka `ud2`).
   Obca biblioteka tylko z wpisem i uzasadnieniem w `docs/ZALEZNOSCI.md` (np. grafika bez libdrm — ioctl
   z nagłówków UAPI w `build/naglowki`).
5. **PID 1 nigdy nie wychodzi** — błąd krytyczny → `k1_awaria()` (komunikat, 5 s, wyłączenie). Usługa przy
   błędzie → `k1_koniec()`; awaria usługi nie zatrzymuje systemu.
6. **Każdy komunikat sprawdzany pole po polu przez odbiorcę** (wersja, typ, długość, zakresy, zarezerwowane = 0),
   z testami i fuzzingiem. Odbiór przez `k1_odbierz_komunikat()`. Kanał od nadzorcy to zawsze fd 3.
7. **Jeden właściciel zasobu i klatka dla każdego:** tylko `ekran` steruje obrazem (węzeł card karty, DRM master; od
   0.10.9 krok 2, ADR 0049: węzeł render karty i915 otwiera nadzorca tylko dla dekodera filmów `filmy` i — od kroku 5 —
   pośrednika grafiki `posrednik`, każdego jednego na strefę — nigdy dla ekranu, aplikacji ani VMM-a), tylko `wejscie`
   urządzeń wejścia, tylko `skarbiec` partycji K1-DANE; urządzenia otwiera nadzorca i przekazuje deskryptory.
   Klatka (ADR 0007): własny UID, 0 capabilities, NO_NEW_PRIVS, Landlock bez ścieżek, seccomp z białą listą
   (kod wyjścia 159 = zabronione wywołanie), cgroup. Nowe wywołanie jądra = wpis w `src/nadzorca/profile.c`
   z uzasadnieniem. Odbiorca sprawdza UID nadawcy (`SO_PASSCRED`). **Aplikacje nie dostają ścieżek** —
   tylko kanały i deskryptory.
8. **Żadnych sekretów, haseł, treści ani kodów klawiszy w logach** (najwyżej liczniki, ≤ 2 wpisy/s).
9. **Powtarzalny build:** `SOURCE_DATE_EPOCH`, `-frandom-seed`, stałe dane budowy jądra, bez `__DATE__`
   i losowości (`scripts/sprawdz-powtarzalnosc.sh`).
10. **Fail closed:** nieudane ustawienie bezpieczeństwa (sysctl, montowanie, sprawdzenie rdzenia) zatrzymuje start.

Sekret `K1_KLUCZ_PODPISU` dodaje właściciel sam — **nigdy go nie proś i nie wpisuj**. Prywatny klucz Secure
Boot (`build/gen/sb/k1-sb.key`) jest kasowany po podpisaniu. Klucza deweloperskiego nie proponujemy do MOK.

## 5. Czytelność (decyzja właściciela, `AGENTS.md`)

System ma być czytelny od środka, także gdy rośnie: części może być dużo, ale każda mała, z jedną rolą
i w tym samym kształcie co pozostałe.
- plik `.c` ≤ 600 wierszy, funkcja ≤ 80 (bez `src/zewnetrzne/`); wyjątek tylko z `/* k1: limit — <powód> */`;
- **dziel przed rozbudową** (osobny commit), nie po;
- nowa rzecz = **wiersz w tabeli** (jak `K1_APLIKACJE`); gdy dodanie czegoś wymaga zmian w wielu miejscach,
  najpierw zrób z tego jedną tabelę;
- nowe pojęcie = wiersz w `docs/SLOWNIK.md`; bez synonimów w kodzie, dokumentach i komunikatach;
- rozwiązanie tymczasowe: `/* k1: tymczasowe — usunąć w <wersja> */` + wpis w `DO_ZROBIENIA.txt` („Tymczasowe”);
- **każdy MINOR kończy się porządkami** (co usunąć, scalić, uprościć) — wynik w CHANGELOG;
- dokumentacja nadąża: każda usługa i aplikacja ma opis, każdy komunikat wpis.

Wszystko to (limity, opisy usług/aplikacji/komunikatów, wpisy „Tymczasowe”, spis ADR, istniejące ścieżki `docs/…`) sprawdza `scripts/sprawdz-czytelnosc.py`
(test-m1, pierwszy krok testów) — uruchom go przed commitem: `python3 -I scripts/sprawdz-czytelnosc.py`.

Komentarze i commity po angielsku; nazwy w kodzie po polsku, zgodnie ze słownikiem.

## 6. Przepisy

**Nowa aplikacja** (wzór: `src/aplikacje/kalkulator/`):
1. Jeden wiersz `X(...)` w `K1_APLIKACJE` (`src/wspolne/k1_uslugi.h`): numer, profil, UID, program, tag,
   tytuł, opis w menu Start, ikona, czy na pasku i pulpicie przy pierwszym starcie, kanały od nadzorcy (`()` — żadnych). Numery się nie zmieniają (profil = właściciel danych w skarbcu).
2. Katalog `src/aplikacje/<program>/`: `main.c` ze `struct k1_apl` + funkcje zwrotne i `k1_apl_uruchom()`;
   każdy `.c` w katalogu się kompiluje.
3. Z listy wynika samo: usługa i klatka w nadzorcy, menu i pasek, nazwa okna, Zasoby, obraz, spis w testach.
4. Ręcznie: ikona `K1_IK_*` w `k1_rysuj.c`, scenariusz w `test-m4.sh`, opis w `docs/komponenty/aplikacje.md`, pojęcia
   w słowniku. **Opis ekranu** (0.10.2, ADR 0045): przyciski, menu i segmenty przybornika trafiają do opisu same; własne
   elementy (sekcje, kafelki) zgłoś `k1_el(K1_EL_…, "nazwa K1", x, y, w, h, stan)` tam, gdzie je rysujesz — test klika
   `q.klik_w("<tytuł okna>", "<nazwa>")`. Nigdy tekst użytkownika, nazwa pliku ani sesji (wtedy „plik N”, „sesja N”).
   **Zrzut wzorcowy** (0.10.2 krok 3): `q.wzorzec("<okno>-jasny", "<tytuł>")` i `-ciemny` w `test-wzorce.sh`; liczby, czas,
   wykresy — `k1_el(K1_EL_ZMIENNE, "nazwa K1", …)`; pierwszy przebieg daje obraz „po” — właściciel go ogląda, potem
   `scripts/przyjmij-wzorce.sh` (D3).
5. Dane spoza okna — kanał w ostatniej kolumnie wiersza (`(KANAL_ZASOBOW)`; nazwy w `src/nadzorca/kanaly.h`, nowy
   kanał = linia tam + moduł nadzorcy, który go obsługuje; wzór: Zasoby, fd 4). Nigdy ścieżki.
Gotowe części przybornika: `k1_przycisk`, `k1_pole_klawisz`, `k1_menu` (menu pod prawym przyciskiem), `k1_przeciaganie`
(tabela reguł przeciągania, okienko pytania, karteczka i znak przy kursorze), `k1_zaznaczenie` (wiele zaznaczonych),
`k1_liczba.h` (też `k1_odmiana`: „1 plik / 3 pliki / 5 plików”), kolory `k1_motyw` / `K1_*` (`k1_grafika.h`).
Ctrl/Shift przy kliknięciu: `apl.mody` (ekran podaje je w `WSKAZNIK` e, 0.8.2).

**Nowa usługa systemowa** (wzór: `siec`, 0.9.0 krok 6) — jeden wiersz w tabeli (0.9.0 krok 1):
1. Wiersz w `K1_SYSTEM` (`src/wspolne/k1_uslugi.h`): nazwa, profil, UID, program, nazwa w Zasobach, kiedy startuje
   (`PRZY_STARCIE` — i ponownie po awarii — albo `NA_PROSBE` przez swój moduł), rola przy ekranie (`BRAK`, gdy nie ma
   okna) i kanały od nadzorcy po kolei od fd 3 (u klienta ekranu od fd 4), np. `(KANAL_SIECI)`. Z wiersza wynika:
   `uslugi[]` (start, restart, kanały), nazwa i UID klatki, Zasoby, spis programów i plików obrazu
   (`scripts/uslugi-systemu.sh`), `t_inventory`.
2. Klatka `KLATKA_<NAZWA>` w `src/nadzorca/profile.c` — wywołania poza bazą i limity, każde z uzasadnieniem
   (bez niej nadzorca się nie zbuduje). Nowy kanał: linia `KANAL_*` w `src/nadzorca/kanaly.h` + moduł nadzorcy, który
   go obsługuje (jego koniec, wpis w pętli `main.c`). Szczególne sprzątanie po końcu usługi — `po_koncu()` w `uslugi.c`.
3. `src/<nazwa>/main.c`: komentarz na górze (zadanie, granica zaufania, etap); pierwsza linia `main()` —
   `k1_klatka_w_usludze(TAG)`; log `K1-<NAZWA>`; kanały na fd jak w wierszu. `Makefile` — tylko reguła budowy programu.
4. Testy w systemie: klatka (`t_klatki`), próba w klatce (`t_proby`), sprawdzenia funkcjonalne i **negatywne**.
   Scenariusz QEMU, jeśli potrzebny.
5. `docs/komponenty/<nazwa>.md` (sprawdza `sprawdz-czytelnosc.py`), wiersz w tabeli `docs/komponenty/nadzorca.md`, ADR
   jeśli zmienia architekturę, `docs/RYZYKA.md`, słownik.

**Nowy komunikat albo kanał:** typ w `k1_komunikat.h` z kierunkiem i zakresami pól, sprawdzanie w pliku swojej grupy
`src/wspolne/k1_kom_<kanał>.c` (nowa grupa — nowy plik i jedno wywołanie w `sprawdz_typ`, `k1_komunikat.c`); dla nowego formatu —
nagłówek w `src/wspolne/` ze stałym układem (`_Static_assert` na rozmiar i przesunięcia), sprawdzanie bez
założeń o wyrównaniu (`memcpy`), test `testy/host/test_*.c` (ASan/UBSan), fuzzing `testy/host/fuzz_*.c`
(lista `FUZZ` w `Makefile`) i test negatywny w systemie (jak `t_malicious_ipc`). Opis w dokumencie komponentu.

**Opcja jądra:** znajdź w `build/zrodla/linux-*/**/Kconfig*` (`depends on`, `select`), dopisz do
`jadro/k1.config` z komentarzem po co (i usuń ją z `jadro/k1-odrzucone.txt`, jeśli tam jest — listy „świadomie nie chcemy”
dla robota, T19 ADR 0047; zmienia je tylko człowiek); ręczna zmiana `jadro/wersja.env` tylko w kroku planu albo gdy robot
nie umie (Issue „robot”); `scripts/zbuduj-jadro.sh` przerwie, jeśli nie weszła; przy
wzmocnieniach `scripts/sprawdz-kspp.sh`; odstępstwa w `docs/komponenty/jadro.md`. Nowa wersja jądra:
`jadro/README.md`. Pamiętaj: `allnoconfig` wyłącza też opcje „default y” za `if EXPERT` (MTRR, PAT, ACPI_EC).

**Wygląd:** decyduje właściciel — najpierw **szkice do wyboru**, potem implementacja. Tapety na hoście
(`make build/obj/podglad_grafiki`, `podglad_grafiki W H -1 -1 0 WARIANT` → PPM → `python3 -I scripts/ppm-na-png.py`),
pulpit w QEMU + `scripts/zrzut-ekranu.sh`; zrzuty pokazuj (`SendUserFile`), ustalenia zapisuj w `docs/WYGLAD.md`.
Styl: minimalistyczno-ciepły (Claude/Anthropic), neutralne tła, jeden akcent (terakota), siatka 4 px, IBM Plex
Sans/Serif, ikony liniowe; pulpit jak w Windows 11 (pasek na dole, wyśrodkowany), ale w stylu K1.

## 7. Testy

| Warstwa | Jak | Co |
|---|---|---|
| host | `make testy-host`, `make fuzz` | parsery, formaty, logika (ASan/UBSan, libFuzzer); sprawdzaj zachowanie, nie powtarzaj wzorów z kodu |
| w systemie | `testy/system/` → `/rdzen/system/test-systemu` | obraz testowy = produkcyjny + program testowy; wynik `K1-TEST: WYNIK: OK (n/n)`; klatki, próby, złośliwe komunikaty |
| QEMU z zewnątrz | `scripts/test-*.sh` — jeden skrypt na część (mapa „która część — który skrypt” w `docs/TESTY.md`) na `testy/qemu/k1qemu.py` | start i błędy startu, pulpit i aplikacje, zrzuty wzorcowe, USB, instalator i aktualizacje (z pendrive'a i przez sieć), Secure Boot, Pliki, pendrive, skarbiec, sieć, Wi-Fi (`mac80211_hwsim`), strefa próbna, Raptor Bridge |
| wszystko | `scripts/testy.sh` | to samo uruchamia CI (z KVM równolegle) — przed scaleniem: CI na PR-ze (§9) |

- **Po nazwach (0.10.2, ADR 0045):** pulpit i okna K1 — obraz testowy z dyskiem `k1-test-opis` (wzór: `test-m4`, przebieg
  `pulpit`), `q.klik_w("Menu Start", "Notatki")`, `q.czekaj_na_element(okno, nazwa)`; położenia z logu tylko tam, gdzie
  opisu nie ma (`docs/TESTY.md`). Rola `TEST` i `OPIS_PYTANIE` tylko w ekranie i testach (pilnuje `sprawdz-czytelnosc`).
- **Zrzuty wzorcowe (0.10.2 krok 3, D3/D4):** `test-wzorce.sh` porównuje okna K1 co do piksela z `testy/wzorce/*.png`
  (`q.wzorzec(...)`, `testy/qemu/k1wzorce.py`); różnica → `build/testy/wzorce/<nazwa>-przed/-po/-roznica.png` (w CI:
  artefakt z logami). **Wzorce zmieniasz tylko po „OK” właściciela** na obrazy „przed / po” (`SendUserFile`), skryptem
  `scripts/przyjmij-wzorce.sh`, osobnym commitem. Zmienne miejsca (czas, liczby, wersja) — `K1_EL_ZMIENNE` w programie.
- **Bez stałych pauz:** `Maszyna.czekaj`/`czekaj_na` na log, `klik` czeka na potwierdzenie ekranu, `przeciagnij`
  (wciśnięcie, kilka ruchów, puszczenie), `klik_z("ctrl", x, y)` (QEMU `sendkey ctrl 1500` + klik), `klawisze(("alt", 1), ("tab", 1), …)` (0.10.5 krok 6: klawisze po kolei przez QMP — `sendkey` nie przytrzyma Alt przy kilku Tab),
  `zrzut(nazwa, gdy=…)` na stan ekranu, `cisza(wzór, s)` na „nic się nie dzieje”. Żadnego `time.sleep`.
- Ekran loguje kursor i lewy przycisk najwyżej 2×/s: podwójne kliknięcie — `Maszyna.dwuklik` (oba kliknięcia
  jednym poleceniem monitora), prawy przycisk — `prawy_klik` (potem czekaj na „menu kontekstowe”).
- Kanał testowy (fd 3 programu testowego): `TEST_POLACZ` (kanał do ekranu z dowolną rolą), `TEST_PROBA`
  (program testowy w klatce usługi), `URUCHOM` (aplikacja).
- Dyski w teście w systemie: `SPRZET_TEST` w `test-m1.sh` (obrazy `sfdisk`). Nowy obraz rdzenia w teście —
  zawsze z `verity` i skrótem (`scripts/wspolne.sh`). W `test-m6` psuj wolne miejsce w wewnętrznym FAT,
  nie miejsce A (z niego startuje).
- **Nie edytuj skryptów ani kodu, gdy testy działają w tle** — powłoka czyta skrypt w trakcie.
- Niestabilny test: najpierw szukaj błędu w harnessie, potem w systemie — nigdy go nie ignoruj ani nie wyłączaj.
- **Dziwny błąd testu, którego nie widać w kodzie K1** (decyzja właściciela 2026-11-15; np. zachowanie obcego programu
  w strefie — Enter w Firefoksie, przewijanie): najwyżej **1–2 próby** wyjaśnienia (log, jeden przebieg z diagnostyką).
  Potem nie szukaj dalej kolejnymi przebiegami CI — **oddaj test właścicielowi**: link do obrazu USB z artefaktu PR-a
  (`k1-<wersja>-obraz`, przebieg CI) i krótka lista „co kliknąć, co powinno się stać”; właściciel uruchamia go
  z pendrive'a na X230 / IdeaPadzie. Działa na sprzęcie → test w CI omija to zachowanie, z notatką w skrypcie i w
  `docs/TESTY.md` (wzór: dawny `test-przegladarka.sh` bez Entera — przebiegi 95–103 PR-a #119, cały dzień szukania;
  od 0.10.1 testu przeglądarki w CI nie ma wcale — decyzja właściciela 2026-11-17, niżej §9).

**Pułapki, które już wystąpiły:**
- *Jądro 7.x (0.10.5 krok 7):* nowe nazwy opcji wychodzą w `scripts/porownaj-jadro.sh` jako „ZNIKNĘŁA” + „PYTANIE”
  (`PROC_SYSCTL` → `SYSCTL`, `RANDOM_KMALLOC_CACHES` → `KMALLOC_PARTITION_*`, `MEMORY_BALLOON` → `BALLOON`); wybory
  (choice) allnoconfig potrafią się zmienić (TSX „auto”) — wpisuj je wprost; nowe pola UAPI łamią inicjalizację pozycyjną
  struktur (`-Werror`) — inicjuj po nazwach; jądro gościa z `HRTIMER_REARM_DEFERRED` pod emulowanym SVM gubi czas
  (klawisz po minutach) — VMM daje gościowi `highres=off` tylko w emulacji. Ścieżka gniazda monitora QEMU < 108 B: drzewo
  robocze w długim katalogu (scratchpad) psuje wszystkie testy QEMU — trzymaj je pod krótką ścieżką.
- *Dysk strefy (0.10.0 krok 5):* gość w trybie testowym 5 (`TRYB_DYSK`) jest uczciwy — VMM traktuje jako złośliwe tylko
  tryby 1–4 (`zlosliwy_gosc()`); strefa pamięta tylko z danymi na dysku (test-strefa dodaje K1-DANE),
  a nadzorca daje dysk dopiero po wczytaniu sesji ze skarbca; nazwy sesji to tekst użytkownika — nigdy w logu (menu gui
  loguje „sesja N”); `mke2fs` bez `MKE2FS_CONFIG=/dev/null` bierze cechy z konfiguracji komputera budowy (niepowtarzalnie).
- *Strefy (0.10.0, ADR 0042 T10):* QEMU 8.2 w TCG udaje AMD SVM z NPT, ale gość **bez stronicowania** (tryb rzeczywisty,
  przełączanie na LA57) widzi złą pamięć — start jądra strefy prosto w trybie 64-bit, CPUID bez LA57, strefa testowa
  z 1 procesorem; QEMU zostawia typ zdarzenia w `exit_int_info`, KVM 6.18 widzi „dostarczanie w toku” przy MMIO tuż po
  przerwaniu (`KVM_INTERNAL_ERROR_DELIVERY_EV`) — VMM ponawia, gość z x2APIC; bez `KVM_SET_CPUID2` gość nie wejdzie w tryb
  64-bit (EINVAL); bez `init=/init` jądro z rootfs nie szuka `/init`; KVM tworzy w procesie maszyny własne zadanie
  (`vhost_task`) — `pids.max` = 1 + procesory + 1; `GETCONNECTOR` w gościu: najpierw z liczbą trybów 0 (dopiero to
  wykrywa tryby), tablica nie mniejsza niż liczba trybów; `/proc/cmdline` nie istnieje dla procesów K1 (`subset=pid`) —
  tryb testu stref oznacza dysk z numerem seryjnym `k1-test-strefy` (`addr=0x1e`, żeby rdzeń został `/dev/vda`);
  zrzut ekranu ma kursor — piksel do sprawdzenia obok jego czubka; element strefy na pasku przesuwa wyśrodkowany pasek.
- *Kompozytor strefy na hoście (0.10.1 krok 1):* błąd Firefoksa w strefie da się odtworzyć bez maszyny — Firefox z kopii
  `build/strefa-przegladarka` w chroot (bind `/dev/null`, `/dev/urandom`…, tmpfs `/run`, `/tmp`, `/home`, `/dev/shm`;
  użytkownik 1000, środowisko jak w `gosc/przegladarka.c`) i kompozytor zbudowany na hoście z ASan: `drm.c` zastąpiony
  wyświetlaczem w pamięci (zrzut PPM), `/dev/input/eventN` — kolejka FIFO, do której skrypt pisze `struct input_event`
  (EV_ABS 0…32767). Tak znaleziono regułę 28 (podgląd karty). Rola powierzchni: ta sama rola drugi raz jest dozwolona.
  Od 0.10.1 krok 5 gotowe narzędzie: `scripts/stanowisko.sh [scenariusz]` (`testy/stanowisko/`, opis w TESTY.md).
- *Przewijanie w strefie (0.10.1 krok 5):* GTK kończy gest touchpada przeglądarki tylko ramką z `axis_stop` — i tylko
  z `axis_source` „finger” w tej samej ramce (bez niego bierze koniec za kółko); Firefox (APZ) wysyła każdą ramkę
  niezakończonego gestu do celu z jego początku — bez `axis_stop` po nowej karcie nic się nie przewijało, w żadnej karcie.
  Ramkę palców oznacza VMM (`BTN_TOOL_FINGER`), nie „same części ząbka” — tak wygląda też precyzyjne kółko. QEMU nie
  udaje touchpada ani precyzyjnego kółka: palce w Firefoksie sprawdza stanowisko.
- *Balon (0.10.1 krok 2):* bez FREE_PAGE_HINT zgłaszanie to kolejka 3; zgłaszane bloki to bufory do zapisu; pamięć
  wraca przez `fallocate(PUNCH_HOLE)` (nie `MADV_DONTNEED` — memfd współdzielony); gość ponad 3 GiB ma drugą część od
  4 GiB (`gosc_pamiec.dol`) — adres gościa zawsze przez `gp_wsk`. W emulacji (kontener) bezczynny gość bywa budzony
  z dużym opóźnieniem („rcu_sched self-detected stall”) — scenariusze gościa czekają aktywnie, nie śpią.
- *Rozdzielacz (0.10.1 krok 3):* rządzi balonem każdej strefy z gui, a ze stref programu testowego tylko trybu 10 —
  tryby 0–9 w test-strefa mają balon „testowy” (K1 w teście ma 1 GiB, czyli mniej niż rezerwa 768 MiB: rozdzielacz
  pompowałby balon i pokazywał „Mało pamięci” w środku klikania); okienka gui spoza menu (sesje, „Mało pamięci”) rysuje się
  na panelu menu kontekstowego przez `struct okienko_wlasne` (`kontekst_wlasne`), nie nowym panelem.
- *Pamięć strefy (0.10.0 krok 3):* gość ma stałą pamięć z wiersza `K1_STREFY` i bez wymiany — przy braku pamięci jego
  jądro zabija największy proces (często przeglądarkę = koniec strefy); dlatego zram w jądrze strefy (`gosc/pamiec.c`,
  nagłówek wymiany piszemy sami — obraz nie ma `mkswap`) i `oom_score_adj`; `/proc/vmstat` ma `oom_kill` tylko z
  `VM_EVENT_COUNTERS` (za EXPERT); katalog domowy przeglądarki to tmpfs — każda „pamięć podręczna na dysku” to pamięć.
  Firefox z obrazu strefy da się uruchomić w kontenerze pod Xvfb (`chroot` w kopię `build/strefa-przegladarka`,
  `GDK_BACKEND=x11`, `MOZ_GTK_TITLEBAR_DECORATION=client` udaje dekoracje Waylanda) — tak sprawdzono S28 (zrzut).
- *Przewijanie (0.10.0 krok 4):* QEMU `mouse_move 0 0 dz` — dz > 0 to kółko od siebie (`Maszyna.kolko(x, y, w_dol)`
  odwraca znak sam); QEMU nie udaje touchpada wielodotykowego — touchpad sprawdza właściciel na sprzęcie (log wejścia:
  nazwa i identyfikatory). Touchpad we własnym trybie nie ma `REL_X/REL_Y` — bez `touchpad.c` byłby martwy.
- *Wyłączanie (0.8.6):* init zatrzymuje najpierw samego nadzorcę (ten zapisuje historię baterii, póki skarbiec działa),
  potem resztę; usługi giną razem z nadzorcą (`PR_SET_PDEATHSIG`), więc nic w usłudze nie może liczyć na sprzątanie.
- *Wi-Fi (0.9.1):* sysfs jest w K1 tylko do odczytu — przełącznik radia przez `/dev/rfkill` (`RFKILL_OP_CHANGE`), nie
  `rfkillN/soft`; typ komunikatu niesie deskryptor zawsze albo nigdy (`k1_komunikat_z_deskryptorem`, inaczej odbiorca go
  odrzuca — stąd osobny `SIEC_WIFI_EAPOL`); klatka, która pisze memfd (np. dla skarbca), potrzebuje `.rozmiar_pliku`
  (inaczej SIGXFSZ = 25); jądro potrafi wpisać swój komunikat w środek wiersza K1 na porcie szeregowym, nawet w środku
  znaku UTF-8 — `k1qemu.py` usuwa je z bajtów przed dekodowaniem, a skrypty w powłoce czytają log przez `log_k1`
  (`scripts/wspolne.sh`; 0.10.5: `test-m5` padł w CI na „partycja danych [ 2.79] sda: sda1 sda2⏎K1-DANE otwarta”); bez `MAC80211_RC_MINSTREL` (za EXPERT) mac80211 nie rejestruje kart bez własnego doboru prędkości
  („Failed to select rate control algorithm”: ath9k, rtw88, hwsim); `Maszyna.czekaj(wzor, od=n)` — `n` to liczba
  wcześniejszych dopasowań (`q.ile(wzor)`), nie długość logu; pętla `timeout … until grep KONIEC` mogła skończyć się
  limitem — przed nowym przebiegiem testu sprawdź `pgrep -af wifi.py|qemu`, inaczej dwa przebiegi mieszają log (0.10.2: to
  samo z `test-wzorce` — koniec starego przebiegu zamknął gniazdem monitora maszynę nowego).
- *Obraz przeglądarki poza rdzeniem (0.10.9 krok 4, 4b):* miejsce A/B 768 MiB (k1.efi 20 MiB + obraz zstd 126 MiB, zapas
  620 MiB); `zbuduj-usb.sh` przerywa z „brakuje N KiB”. Obrazy testów na hoście zostają przy miejscach 128 MiB (format czyta
  każdy rozmiar od 128 MiB do 2 GiB) — nie powiększaj ich: `mcopy` pisze zera całego miejsca. Pendrive 1,6 GB: kopiuj go
  `cp --sparse=always`, `dd … conv=sparse`. Urządzenie blokowe nie ma `st_size` — rozmiar
  przez `lseek(SEEK_END)`. Test w systemie `t_sprzet` liczy deskryptory urządzeń blokowych: nadzorca może trzymać tylko
  obraz strefy `k1-strefa-*` pod dm-verity, do odczytu. Wyłączanie: nadzorca czeka co najmniej obieg kropek (1,5 s) — bez
  tego init kończył ekran, zanim narysował „Wyłączanie” (X230, 2026-11-19).
- *Czas lokalny (0.10.7 krok 5):* `localtime_r`, `mktime`, `strftime` z musl czytają strefę z `/etc/localtime` (`open`) —
  w klatce to kod 159 (Ustawienia → Aktualizacje padały dopiero w widoku „Sprawdzono o …”); czas polski tylko
  `k1_czas_warszawa` (`src/przybornik/k1_czas.h`), pilnuje `sprawdz-czytelnosc` (punkt 12).
- *Jądro i procesy:* `/proc/cmdline` nie istnieje dla usług (`subset=pid`) — o nośniku czytaj z sysfs;
  `hidepid` daje ENOENT zamiast EACCES; rodzicem usług jest nadzorca, nie init; filtr seccomp działa od
  `execve`, więc obejmuje start musl; `RLIMIT_FSIZE` ogranicza też `ftruncate` memfd (SIGXFSZ = 25).
- *Kanały:* `SO_PASSCRED` ustawia się na końcu **odbiorcy** (`k1_kanal_utworz`: `pary[0]` dla sprawdzającego);
  pipe otwarty ponownie przez `/proc/<pid>/fd` nie jest pakietowy, a `O_DIRECT` przy open → EINVAL.
- *C i musl:* `ioctl(int, int, …)` — rzutuj numer żądania na `int`; `-Wformat-truncation` wymaga buforów na
  pełny `d_name`; `read_file` w testach zwraca 0 przy sukcesie.
- *Urządzenia:* `LO_FLAGS_AUTOCLEAR` odłącza loop przy zamknięciu ostatniego deskryptora — zamykaj po `mount`;
  usb-storage pokazuje dysk po ~1 s; na X230 pendrive potrafi zniknąć i wrócić (EHCI → xHCI) — K1-DANE
  szukamy też po starcie.
- *QEMU:* usuń stary log i gniazdo **przed** startem QEMU w tle; q35 ma wbudowany AHCI (`bus=ide.0`);
  `mouse_move`/`sendkey` idą do ostatnio aktywnego urządzenia (z USB — do USB); „przerwanie w trakcie” (monitor `quit` po
  wpisie w logu) w CI z KVM przychodzi za późno — zapis kończy się wcześniej; spowolnij dysk tego przebiegu
  (`throttling.bps-write`, test-m5f, 0.9.0) i sprawdź, że przerwanie trafiło przed końcem; USB bez PS/2:
  `--maszyna ",i8042=off"`. **Wydajności nie widać w TCG** — emulator ignoruje typy pamięci (brak PAT dał na
  X230 zacinający się pulpit, 0.5.4).
- *GRUB i ekran startowy:* `background_image` rysuje się po wypisaniu czegoś (`echo ""`); `set color_normal=
  black/black` **po** `terminal_output gfxterm`; `quiet` wycisza „EFI stub”; w QEMU fazę GRUB-a widać na
  zrzutach dopiero z `sleep` w kopii `grub.cfg` (`mcopy -i obraz@@1M`).
- *Robot i numery wersji (0.10.5, ADR 0047, S19, S33):* robot i człowiek mogą podbić `VERSION` do tego samego numeru — git
  połączy dwie takie same zmiany bez konfliktu, a CHANGELOG dostanie dwa wpisy; dlatego obie strony sprawdzają `VERSION`
  na `main` tuż przed scaleniem. `k1.yml` na `main` nie anuluje trwającej budowy (kolejka) i taguje tylko `VERSION`, który
  `main` wciąż ma. Zdarzenia `GITHUB_TOKEN` nie uruchamiają workflow — robot woła `k1.yml` przez `workflow_dispatch`.
- *Przeglądarka w CI (0.10.0–0.10.1):* test z prawdziwym Firefoksem w strefie (CI z KVM) przez wiele dni padał na
  zachowaniu Firefoksa (Enter bez skutku, lista podpowiedzi niewidoczna, limity czasu), którego na sprzęcie nie było —
  od 0.10.1 go nie ma (decyzja właściciela 2026-11-17). Firefox w strefie ocenia sieć po kartach systemu (strefa ich nie
  ma) — `network.manage-offline-status` wyłączone w `strefy/przegladarka/k1.js`. Pełny Firefox w emulacji zagnieżdżonej
  (kontener bez KVM) nie rysuje okna — do szukania błędów: stanowisko (`scripts/stanowisko.sh`), obraz strefy wprost
  w QEMU z jednorazowym przekaźnikiem albo sonda zamiast Firefoksa w K1 (tak znaleziono zamykanie fd 0 w VMM).

## 8. Plan i zasady etapu

**Plan — jedno miejsce (ADR 0035):** stan, mapa drogi i otwarte decyzje — `docs/DO_ZROBIENIA.txt`; kroki bieżącej
wersji — `docs/PLAN-<wersja>.txt` (samodzielny: luki, kroki, decyzje; zakres zamrożony, nowe pomysły → „Później”;
numery kroków się nie zmieniają — wyjątek: **wydanie jest zawsze ostatnim krokiem** (decyzja właściciela 2026-11-17),
więc nowy krok dodany do planu bierze numer kroku wydania, a wydanie przesuwa się na koniec, póki się nie zaczęło;
zamknięty plan → `docs/archiwum/`). Nie powtarzaj stanu w innych plikach.
Etapy to wersje MINOR z tematem: **0.8 Pliki** → **0.9 Sieć** → **0.10 Strefy** (mikro-maszyny, ramka, przeglądarka,
aktualizacje przez sieć; ADR 0020, 0024, 0025) → **0.11 Pliki ze stref** (ADR 0027) → **0.12 Bezpieczny start** (recovery,
TPM) → **0.13 Audyt** → 1.0. Co gotowe, co teraz i który plan jest bieżący — tylko w `docs/DO_ZROBIENIA.txt` (tu tego nie
powtarzamy). Stare dokumenty mówią M7…M12 — tłumaczenie: tabela w ADR 0035 (M7 = 0.9, M8 = 0.10, M9 = 0.11, M10 = 0.12,
M12 = 0.13, M5.3 Laptop = PATCH w dowolnej wersji).
**Robot a plany (od 0.10.5):** robot wydaje z `main` codziennie — scalony krok planu musi być gotowy dla użytkownika,
a numer wydania z planu może zająć robot (zasady: `AGENTS.md`, „Robot aktualizacji”; plany, punkt 0 „Robot i wydania”).

**Przepływ plików (ADR 0027):** Pliki K1 to jedyny węzeł; oryginał zawsze w Plikach, strefy dostają kopie;
zgoda = gest użytkownika w interfejsie rysowanym przez K1; pochodzenie zapisane przy pliku.

**Zasady etapu (SPEC §2):** jeden etap naraz, następny dopiero z powtarzalnym testem. **Nie wymyślaj** nazw
wywołań, flag, opcji Kconfig ani zachowań jądra — sprawdzaj w `build/zrodla/`, `build/naglowki/`, man-pages;
niepewność oznaczaj wprost. Statusy: ZAIMPLEMENTOWANE / PRZETESTOWANE (tylko z dowodem) / PLANOWANE.
Konflikt SPEC z Linuksem → najmniejsza bezpieczna korekta, ADR, dopisek w SPEC.

## 9. Wersja, scalanie, CI

- **Przed scaleniem zmian w systemie** (decyzja właściciela 2026-11-14): lokalnie `make` bez ostrzeżeń, `sh -n` na
  skryptach, `scripts/test-m1.sh` i skrypty QEMU zmienianej części (mapa „która część — który skrypt” w `docs/TESTY.md`),
  przy zmianach w budowie `scripts/sprawdz-powtarzalnosc.sh` (nie commituj w trakcie — data commita wchodzi do buildu);
  potem **pełny komplet w CI na PR-ze** (etykieta `testy`) — scalasz dopiero przy zielonym. Przeglądarki
  CI nie testuje (decyzja właściciela 2026-11-17, `docs/TESTY.md`): w raporcie wydania daj właścicielowi listę „co
  sprawdzić w przeglądarce” (DO_ZROBIENIA, punkt 4); jego zgłoszenia odtwarzaj na stanowisku. Pełny `scripts/testy.sh`
  w kontenerze tylko, gdy CI nie działa. Sama dokumentacja — bez testów.
- **Wersja** (`VERSION`, raz na paczkę; ADR 0035): nowy temat z mapy drogi → MINOR (0.9, 0.10…), kroki planu
  wersji, poprawki i jądro → PATCH, docs/CI → bez zmiany; **robot** (ADR 0047) wydaje sam tylko PATCH z poprawionym jądrem
  i pakietami przeglądarki — MINOR i kroki planów robią ludzie. Przed scaleniem PR-a ze zmianą `VERSION` zaktualizuj gałąź
  z `main` tuż przed scaleniem: robot wydał w tym czasie → Twój numer to następny, wpis CHANGELOG nad wpisem robota; nie
  scalaj, gdy na `main` trwa budowa wydania; przeszkadzający PR robota możesz zamknąć (zrobi nowy jutro); robota wyłącza
  się na czas dużych prac (Actions → Disable workflow + wpis „Tymczasowe” w DO_ZROBIENIA). Wpis od 0.10.5 ma wiersz
  „Opis w K1:” (ADR 0046, `scripts/opisy-wersji.py`, pilnuje `sprawdz-czytelnosc`); wpis
  w `CHANGELOG.md`. `VERSION`, `CHANGELOG.md` i `DO_ZROBIENIA.txt` zmienia ten, kto robi wydanie. CI trwa ok. 25 min
  (budowa ~11, na PR-ze z jądrem z pamięci podręcznej mniej; testy ~12); repo jest publiczne, więc minuty CI są bezpłatne.
  Pobrane pliki (`build/pobrane`, `build/strefa-pakiety`) są w pamięci podręcznej Actions (klucz: tabele pobieranych
  plików) — wolny serwer nie trzyma budowy; każde `curl` ma limit połączenia i zatrzymania (60 s poniżej 2 KB/s).
- **CI na PR-ze:** etykieta `testy` (`issue_write`) — build i testy bez wydania; to jest pełny komplet testów przed
  scaleniem. Wynik: `actions_list` (`list_workflow_runs`, gałąź PR-a), przy błędzie `get_job_logs`. `workflow_dispatch` daje 403.
  Ponowny przebieg po nowym commicie: `issue_write` z `labels: []` NIE zdejmuje etykiety — zamień na `["feedback"]`, potem
  `["testy"]`. Każda zmiana etykiety to osobny przebieg, a nowszy anuluje starszy (`concurrency`) — liczy się ostatni
  przebieg z zadaniem `k1`; sprawdź, że ruszył i na którym commicie (PR #124: przebiegi 110–112).
- **Scalanie** (repo publiczne od 2026-11-14, ADR 0040 — scalasz sam po zielonych testach): commit po angielsku → `git push -u origin
  <gałąź>` → PR po polsku (najpierw co widzi użytkownik, potem technika, `Closes #N`) → **squash z własnym
  `commit_message`**. Tylko docs: `[skip ci]` w tytule commita scalenia. Zmiana w systemie: **bez**
  „[skip ci]” nigdzie w opisie (domyślny opis skleja commity gałęzi — tak przepadło wydanie 0.5.4).
- **Czekanie na CI:** testy z KVM trwają zwykle ok. 13–15 min (cały przebieg ok. 30 min). Trwają ponad dwa razy
  dłużej — od razu powiedz właścicielowi, zamiast czekać w ciszy. Logu trwającego zadania API nie daje; artefakt
  z logami pobierasz z `productionresultssa1.blob.core.windows.net` (dozwolony w środowisku od 2026-11-16).
- **Po scaleniu:** na `main` CI tylko buduje (bez testów — decyzja właściciela 2026-11-17; testy idą na PR-ze); budowa
  z jądrem od zera ok. 15 min. Padła → `get_job_logs` (`failed_only`) i poprawka (PATCH); przeszła → tag `k1-v<wersja>`
  i wydanie z ISO i obrazem USB. Logi testów są na PR-ze: konsola zadania (krok „Logi testów”), podsumowanie, artefakt
  (7 dni). Stare artefakty usuwa workflow „Sprzątanie artefaktów”.

## 10. Właściciel i raport

- Nie jest programistą, pisze po polsku. **Decyzje produktowe** (wygląd, zachowanie, aplikacje) — jego;
  **techniczne** — Twoje, z krótkim uzasadnieniem. Przy rekomendacji: jedna propozycja, nie przegląd wszystkiego.
- Mów o skutkach, nie mechanice; pojęcia ze słownika. Uwagi, których nie robisz od razu → zgłoszenie
  (`issue_write`). Przy błędzie dopytaj: wersja, gdzie uruchomione, co widać, czego się spodziewał.
- **Raport po etapie (SPEC §2.11):** cel, co zrobiono, dowody (log, testy, zrzuty), testy negatywne,
  ograniczenia, statusy, pytania do właściciela; `DO_ZROBIENIA.txt` zaktualizowany; linki pełnym URL-em.
- **Każdy raport o nowej wersji kończ linkami** — dopiero gdy wydanie istnieje (`get_release_by_tag`):
  `https://github.com/Conrad3k/Kons-OS/releases/download/k1-v<wersja>/k1-<wersja>.iso` i
  `…/k1-<wersja>-usb.img.xz`. Gdy CI jeszcze buduje — napisz to i podaj linki po sprawdzeniu.
