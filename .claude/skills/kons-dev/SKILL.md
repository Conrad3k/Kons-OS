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
3. Ostatni run „K1 — budowa i testy” na `main` — czerwony naprawiasz najpierw.
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
     strefa (300+, na prośbę gui; 0.10.0, ADR 0042): VMM jednej mikro-maszyny KVM — /dev/kvm, jądro i obraz strefy od
       nadzorcy, jądro strefy (jadro/strefa.config) startuje bez firmware, urządzenia virtio-mmio, okno w ramce ekranu
```
Testy QEMU startują też `bzImage` + `initramfs.cpio` (tylko `test-m1`). ISO/USB mają GRUB-a do 0.12 (ADR 0010; dawne M10).

**System plików po starcie** (ADR 0004): `/` — tmpfs tylko do odczytu z trzema katalogami: `rdzen`
(SquashFS ro, nosuid, nodev; w nim `init`, `system/`, `aplikacje/`, `jadro/{dev,proc,sys}`), `aplikacje`
(pusty do 0.12), `dane_uzytkownika` (tmpfs noexec — dane tymczasowe). Trwałe dane są w **skarbcu**
(partycja GPT „K1-DANE”, własny format, Monocypher; ADR 0015), nie w systemie plików. `/proc` ma
`hidepid=invisible,subset=pid`. Nie używaj ścieżek `/dev`, `/proc`, `/sys`, `/tmp`, `/etc`.

**Aktualizacje (M6, ADR 0021, 0022):** wydanie ma manifest z podpisem EdDSA (`narzedzia/podpisz.c`, sekret CI
`K1_KLUCZ_PODPISU`; bez sekretu = wersja deweloperska, `K1_KLUCZ_JEST 0`). K1-EFI (format 2, `k1_uklad.h`):
FAT32 280 MiB, blok układu, grubenv (`k1_slot/k1_nowy/k1_start/k1_proba`), `/k1/a.img` i `/k1/b.img`
(128 MiB: k1.efi + manifest z identyfikatorem wydania). Aktualizacja idzie do wolnego miejsca, nowa wersja
startuje raz na próbę, `rozruch.c` w nadzorcy zatwierdza ją po 15 s albo zgłasza wycofanie. Instalator
odmawia niepodpisanych i starszych wersji. Klucz Secure Boot K1 wpisuje się przez MokManagera (`efi.c`:
`MokNew`/`MokAuth`). Pliki startowe (shim, GRUB, `grub.cfg`…) sprawdza **manifest plików startowych** (format 3, obok
manifestu miejsca A: skrót drzewa plików `k1_fat32`, bez grubenv i miejsc); aktualizacja je wymienia, gdy się różnią —
nowe obok starych, przełączenie jednym zapisem sektora startowego FAT (`src/instalator/start.c`, 0.9.0, ADR 0038).

**Sprzęt właściciela:** ThinkPad X230 (8 GB, SATA/AHCI, PS/2, EHCI+xHCI, Intel HD 4000 przez simpledrm,
TPM 1.2; kabel: Intel 82579LM, `e1000e`) i od 2026-11-13 Lenovo IdeaPad 110-15ISK (Skylake, tylko xHCI, Intel HD 520;
kabel 10/100 i Wi-Fi Qualcomm Atheros — ath10k, potwierdzone przez właściciela 2026-11-14; Ustawienia → Sieć pokazują identyfikatory kart), jedno konto na komputer.
X230 to maszyna testowa — szybka grafika i gry celują w nowszy sprzęt. Sterowniki — zawsze z jądra, nie własne (D0).

## 3. Mapa kodu

| Kod | W obrazie | Rola |
|---|---|---|
| `src/initramfs/` | `/init` | montuje rdzeń (dm-verity), przełącza korzeń |
| `src/init/` | `/rdzen/init` | PID 1 |
| `src/nadzorca/` | `system/nadzorca` | `main.c` pętla; `uslugi.c` usługi z tabel `K1_SYSTEM`/`K1_APLIKACJE` (start, kanały, restarty); `kanaly.h` nazwy kanałów `KANAL_*`; `logi.c` logi usług; `profile.c` klatki (`KLATKA_<NAZWA>`); `skarb.c` etapy startu; `urzadzenia.c`, `uevent.c`, `dyski.c`, `bateria.c`, `zasoby.c`, `instalacja.c`, `rozruch.c`, `efi.c`, `pendrive.c`, `dekoder.c`, `powieksz.c` (K1-DANE do końca pendrive'a), `siec*.c` (karty sieciowe, adres, stan dla ikony i Ustawień, zegar z internetu; 0.9.0), `wifi.c`, `wifi_nl.c` (karta Wi-Fi, przełącznik radia przez `/dev/rfkill`, szukanie sieci nl80211, lista dla Ustawień; 0.9.1), `wifi_polacz.c` (łączenie na prośbę siec: CONNECT, gniazdo EAPOL, klucze NEW_KEY, SET_STATION; kanał Ustawienia → siec `KN_SIEC`; 0.9.1 krok 2), `test.c` |
| `src/wejscie/` | `system/wejscie` | klawiatura, mysz (evdev, EVIOCGRAB) |
| `src/ekran/`, `src/grafika/` | `system/ekran` | DRM/KMS, składanie okien, kursor, tapety, znak K1; `zalegle.c` — to, czego pełny kanał klienta nie przyjął, czeka w ekranie (RAMKA nigdy nie przepada, ruchy się scalają; 0.8.5) |
| `src/gui/` | `system/gui` | pulpit w modułach (`gui.h`): warstwa ikon, paski, menu Start, menu kontekstowe, rozmieszczenie w skarbcu (fd 6) |
| `src/logowanie/` | `system/logowanie` | ekran hasła przy starcie |
| `src/skarbiec/` | `system/skarbiec` | `magazyn.c` format i szyfrowanie, `main.c` usługa, `obiekty.c`, `haslo.c`, `dokumenty.c` + `dok_*.c` pliki, foldery, Kosz, naprawa przy starcie, granica czasu, prawa, zapis w toku kawałkami (0.8.0–0.8.1, `dok_wew.h`); limity, indeks, kompaktowanie, rezerwa, kawałki — ADR 0034; `miejsce.c` miejsce na pliki i powiększenie K1-DANE (ADR 0033); format 2 (0.8.2, ADR 0036): `odcinki.c`, `spis.c`, `przejscie.c` (format 1 → 2 po potwierdzonym starcie), `przerwy.c` (kompaktowanie krokami między prośbami); format 1 (`obszary.c`) i przejście — tymczasowe do 0.10.0 |
| `src/instalator/` | `system/instalator` | zapis K1 na dysk (GPT), tylko urządzenia od nadzorcy; `start.c` — wymiana plików startowych (0.9.0) |
| `src/strefa/` | `system/strefa` | VMM strefy (0.10.0, ADR 0042): `kvm.c` maszyna i procesory (wątki), `start.c` start jądra strefy bez firmware (strona zerowa, e820, tablica MP), `virtq.c` + `mmio.c` kolejki i rejestry virtio (czyste: `test_virtio`, `fuzz_virtq`), urządzenia `blk.c` (obraz, tylko odczyt), `konsola.c`, `rng.c`, `wejscie.c`, `gpu.c` (2D), `vsock.c` (kanał sterujący), tabela `K1_URZADZENIA` w `strefa.h`; `okno.c` okno u ekranu; `gosc/` — `/init` strefy próbnej (tylko obraz testowy, tryby złośliwe przez `/dev/mem`). Tabela stref `K1_STREFY` (`k1_uslugi.h`), nadzorca `strefy.c`, ramka `src/ekran/strefy.c` |
| `src/siec/` | `system/siec` | adres od routera: klient DHCP w klatce (`dhcp.c` pakiety, `klient.c` stany i czasy); zegar z internetu (`sntp.c` pakiety, `czas.c` runda i zgodność źródeł, `zegar.c` gniazda; krok 8); gniazdo pakietowe, karty, kabel i ustawienie adresu — nadzorca `siec.c`, `siec_jadro.c`; stan dla ikony i Ustawień — `siec_stan.c`; gniazda UDP do serwerów czasu i zegar — `siec_zegar.c` (0.9.0, ADR 0039); Wi-Fi (0.9.1, ADR 0041): `wifi.c` opisy sieci z powietrza (nazwa do pokazania, zabezpieczenie) → lista, `lista.c` wyniki od nadzorcy w memfd i lista do niego (ze stanem połączenia); krok 2: `eapol.c` ramki EAPOL-Key, `wpa.c` logowanie WPA2 (czyste, `test_wpa`, `fuzz_eapol`), `polacz.c` połączenie, `wybor.c` wybór sieci i kanał Ustawień (fd od `SIEC_USTAWIENIA`), `pamiec.c` + `skarb.c` zapamiętane sieci w skarbcu (fd 4); kryptografia `src/wspolne/k1_sha1.c`, `k1_aes.c`, `k1_wymaz.h` |
| `src/czytnik/` | `system/czytnik` | pendrive dla Plików: FAT w klatce, tylko odczyt (0.8.0, ADR 0031); nadzorca `pendrive.c` |
| `src/dekoder/` | `system/dekoder` | jeden obraz (PNG, JPEG, GIF, BMP, WebP) → piksele, jednorazowy proces w klatce (Wuffs; 0.8.0, ADR 0032); plik kawałkami, limity w jednej tabeli `k1_dekoder.h`, dekodery w `K1_DEKODERY` (0.8.2); nadzorca `dekoder.c` (tabela klientów: Pliki, Podgląd), Pliki `obrazy.c`, Podgląd `obraz.c`, ekran `zdjecie.c` (tabela nadawców tapety) |
| `src/aplikacje/<program>/` | `aplikacje/<program>` | Zegar, Notatki, Ustawienia, Zasoby, Instalator (`instalacja`), Kalkulator, Pliki (0.8.0), Wybór pliku (`wybor`), Podgląd (`podglad`, 0.8.2: zdjęcie w oknie, zdjęcia jednego folderu — ADR 0037; 0.8.5: w ruchu szybko z kopii — `skala.c`, po zatrzymaniu `k1_skaluj`); Ustawienia → Miejsce (`miejsce.c`, 0.8.5: części z tabeli `K1_CZESCI` przez `SKARB_PODZIAL`/`SKARB_CZESC`, tylko liczby) |
| `src/przybornik/` | (linkowane) | okno aplikacji (`k1_aplikacja`), rysowanie, ikony `K1_IK_*`, tekst, liczby, czas i klawiatura PL, zdjęcie przez dekoder (`k1_dekodowanie`: prośba, kawałki od skarbca, odpowiedź — Pliki i Podgląd), menu pod prawym przyciskiem (`k1_menu`: wygląd i wiersze menu pliku — jedno dla pulpitu i Plików, 0.8.2), przeciąganie (`k1_przeciaganie`: jedna tabela reguł źródło/cel dla Plików i pulpitu, okienko pytania) i zaznaczanie wielu (`k1_zaznaczenie`, 0.8.2 krok 6) |
| `src/wspolne/` | (linkowane) | `k1_uslugi.h` **listy usług i aplikacji**, `k1_komunikat` (32 B), `k1_kanal`, `k1_odbior`, `k1_klatka`, `k1_log`, `k1_skarb_klient`, `k1_memfd`, `k1_indeks` (indeks po kluczu, 0.8.1), `k1_dokument` (foldery, **typy plików `K1_TYPY`** i ich rodziny, wpis pliku) + `k1_nazwy.c` (zasady nazw), `k1_pendrive` (lista folderu z pendrive'a), `k1_dekoder.h` (limity dekodera), `k1_skaluj`, `k1_efi`, `k1_uklad`, `k1_manifest`, `k1_fat32` (drzewo plików K1-EFI, 0.9.0), `k1_wifi` (nazwy sieci, kreski, format wyników szukania; 0.9.1), `k1_nl` (generic netlink — tylko nadzorca i program testowy; 0.9.1), `k1_sha256`, `k1_bajty.h`, `k1_io.h`, `k1_teraz.h` |
| `src/zewnetrzne/` | (linkowane) | Monocypher (skarbiec) i Wuffs (dekoder, 0.8.0) — jedyne obce biblioteki w systemie |
| `narzedzia/` | (host) | czcionki IBM Plex → bitmapy, ekran startowy GRUB-a, podpis, klucz Secure Boot |
| `jadro/` | `k1.efi`, `bzImage` | `k1.config`, `k1-zamkniete.config`, `README.md` (zmiana wersji jądra), `oprogramowanie-kart.txt` (pliki od producentów kart Wi-Fi wbudowane w jądro — jedna tabela z sumami; 0.9.1, ADR 0041) |
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
7. **Jeden właściciel zasobu i klatka dla każdego:** tylko `ekran` dotyka karty graficznej, tylko `wejscie`
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
4. Ręcznie: ikona `K1_IK_*` w `k1_rysuj.c`, scenariusz w `test-m4.sh` (położenia elementów loguj — test klika
   według logu), opis w `docs/komponenty/aplikacje.md`, pojęcia w słowniku.
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
`jadro/k1.config` z komentarzem po co; `scripts/zbuduj-jadro.sh` przerwie, jeśli nie weszła; przy
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
| QEMU z zewnątrz | `scripts/test-m1/m4/m5/m5f/m6.sh`, `test-pliki.sh`, `test-pendrive.sh`, `test-skarbiec.sh`, `test-strefa.sh` (0.10.0: strefa próbna w emulacji AMD SVM, okno w ramce, złośliwy gość, brak wirtualizacji) na `testy/qemu/k1qemu.py` | start i błędy startu, kursor, pulpit i aplikacje (zrzuty po kolorach), USB, instalator, aktualizacje i Secure Boot, Pliki, wirtualny pendrive ze zdjęciami (mtools + Pillow, `device_add`/`device_del`); `test-wifi.sh` (0.9.1): obraz testowy z `mac80211_hwsim.radios=7` — program testowy robi punkty dostępu testowe (`t_wifi.c`) |
| wszystko | `scripts/testy.sh` | to samo uruchamia CI (z KVM równolegle) — przed scaleniem: CI na PR-ze (§9) |

- **Bez stałych pauz:** `Maszyna.czekaj`/`czekaj_na` na log, `klik` czeka na potwierdzenie ekranu, `przeciagnij`
  (wciśnięcie, kilka ruchów, puszczenie), `klik_z("ctrl", x, y)` (QEMU `sendkey ctrl 1500` + klik),
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

**Pułapki, które już wystąpiły:**
- *Strefy (0.10.0, ADR 0042 T10):* QEMU 8.2 w TCG udaje AMD SVM z NPT, ale gość **bez stronicowania** (tryb rzeczywisty,
  przełączanie na LA57) widzi złą pamięć — start jądra strefy prosto w trybie 64-bit, CPUID bez LA57, strefa testowa
  z 1 procesorem; QEMU zostawia typ zdarzenia w `exit_int_info`, KVM 6.18 widzi „dostarczanie w toku” przy MMIO tuż po
  przerwaniu (`KVM_INTERNAL_ERROR_DELIVERY_EV`) — VMM ponawia, gość z x2APIC; bez `KVM_SET_CPUID2` gość nie wejdzie w tryb
  64-bit (EINVAL); bez `init=/init` jądro z rootfs nie szuka `/init`; KVM tworzy w procesie maszyny własne zadanie
  (`vhost_task`) — `pids.max` = 1 + procesory + 1; `GETCONNECTOR` w gościu: najpierw z liczbą trybów 0 (dopiero to
  wykrywa tryby), tablica nie mniejsza niż liczba trybów; `/proc/cmdline` nie istnieje dla procesów K1 (`subset=pid`) —
  tryb testu stref oznacza dysk z numerem seryjnym `k1-test-strefy` (`addr=0x1e`, żeby rdzeń został `/dev/vda`);
  zrzut ekranu ma kursor — piksel do sprawdzenia obok jego czubka; element strefy na pasku przesuwa wyśrodkowany pasek.
- *Wyłączanie (0.8.6):* init zatrzymuje najpierw samego nadzorcę (ten zapisuje historię baterii, póki skarbiec działa),
  potem resztę; usługi giną razem z nadzorcą (`PR_SET_PDEATHSIG`), więc nic w usłudze nie może liczyć na sprzątanie.
- *Wi-Fi (0.9.1):* sysfs jest w K1 tylko do odczytu — przełącznik radia przez `/dev/rfkill` (`RFKILL_OP_CHANGE`), nie
  `rfkillN/soft`; typ komunikatu niesie deskryptor zawsze albo nigdy (`k1_komunikat_z_deskryptorem`, inaczej odbiorca go
  odrzuca — stąd osobny `SIEC_WIFI_EAPOL`); klatka, która pisze memfd (np. dla skarbca), potrzebuje `.rozmiar_pliku`
  (inaczej SIGXFSZ = 25); jądro potrafi wpisać swój komunikat w środek wiersza K1 na porcie szeregowym, nawet w środku
  znaku UTF-8 — `k1qemu.py` usuwa je z bajtów przed dekodowaniem; bez `MAC80211_RC_MINSTREL` (za EXPERT) mac80211 nie rejestruje kart bez własnego doboru prędkości
  („Failed to select rate control algorithm”: ath9k, rtw88, hwsim); `Maszyna.czekaj(wzor, od=n)` — `n` to liczba
  wcześniejszych dopasowań (`q.ile(wzor)`), nie długość logu; pętla `timeout … until grep KONIEC` mogła skończyć się
  limitem — przed nowym przebiegiem testu sprawdź `pgrep -af wifi.py|qemu`, inaczej dwa przebiegi mieszają log.
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
- Próba kliknięcia "Enter" w przeglądarce nie działała tylko w testach agenta, a na prawdziwym sprzęcie klawisz działał zawsze.

## 8. Plan i zasady etapu

**Plan — jedno miejsce (ADR 0035):** stan, mapa drogi i otwarte decyzje — `docs/DO_ZROBIENIA.txt`; kroki bieżącej
wersji — `docs/PLAN-<wersja>.txt` (samodzielny: luki, kroki, decyzje; zakres zamrożony, nowe pomysły → „Później”;
numery kroków się nie zmieniają; zamknięty plan → `docs/archiwum/`). Nie powtarzaj stanu w innych plikach.
Etapy to wersje MINOR z tematem: **0.8 Pliki** (gotowe: 0.8.0–0.8.7) → **0.9 Sieć** (gotowe: 0.9.0 kabel, 0.9.1 Wi-Fi) → **0.10 Strefy** (teraz: 0.10.0, plan `docs/PLAN-0.10.0.txt`; krok 1 gotowy — strefa próbna) (mikro-maszyny, ramka,
przeglądarka; ADR 0020, 0024, 0025) → **0.11 Pliki ze stref** (ADR 0027) → **0.12 Bezpieczny start** (recovery, TPM)
→ **0.13 Audyt** → 1.0. Stare dokumenty mówią M7…M12 — tłumaczenie: tabela w ADR 0035 (M7 = 0.9, M8 = 0.10, M9 = 0.11,
M10 = 0.12, M12 = 0.13, M5.3 Laptop = PATCH w dowolnej wersji).

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
  potem **pełny komplet w CI na PR-ze** (etykieta `testy`) — scalasz dopiero przy zielonym. Pełny `scripts/testy.sh`
  w kontenerze tylko, gdy CI nie działa. Sama dokumentacja — bez testów.
- **Wersja** (`VERSION`, raz na paczkę; ADR 0035): nowy temat z mapy drogi → MINOR (0.9, 0.10…), kroki planu
  wersji, poprawki i jądro → PATCH, docs/CI → bez zmiany; wpis
  w `CHANGELOG.md`. `VERSION`, `CHANGELOG.md` i `DO_ZROBIENIA.txt` zmienia ten, kto robi wydanie. CI trwa ok. 25 min
  (budowa ~11, na PR-ze z jądrem z pamięci podręcznej mniej; testy ~12); repo jest publiczne, więc minuty CI są bezpłatne.
- **CI na PR-ze:** etykieta `testy` (`issue_write`) — build i testy bez wydania; to jest pełny komplet testów przed
  scaleniem. Wynik: `actions_list` (`list_workflow_runs`, gałąź PR-a), przy błędzie `get_job_logs`. `workflow_dispatch` daje 403.
- **Scalanie** (repo publiczne od 2026-11-14, ADR 0040 — scalasz sam po zielonych testach): commit po angielsku → `git push -u origin
  <gałąź>` → PR po polsku (najpierw co widzi użytkownik, potem technika, `Closes #N`) → **squash z własnym
  `commit_message`**. Tylko docs: `[skip ci]` w tytule commita scalenia. Zmiana w systemie: **bez**
  „[skip ci]” nigdzie w opisie (domyślny opis skleja commity gałęzi — tak przepadło wydanie 0.5.4).
- **Po scaleniu:** `send_later` ok. 15 min; CI padło → `get_job_logs` (`failed_only`) i poprawka (PATCH);
  przeszło → tag `k1-v<wersja>` i wydanie z ISO, obrazem USB i `k1-<wersja>-logi.tar.gz`. Logi testów są
  zawsze: konsola zadania (krok „Logi testów”), podsumowanie, wydanie, artefakt (7 dni). Stare artefakty usuwa
  workflow „Sprzątanie artefaktów”.

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
