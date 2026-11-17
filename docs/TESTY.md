# Testy K1

Zasada (SPEC §2.4, §13): status **PRZETESTOWANE** tylko z dowodem — log z portu szeregowego, zrzut ekranu albo wynik
testu. Każdy mechanizm bezpieczeństwa ma test negatywny (co ma się NIE udać).

Ten plik opisuje, **co** sprawdza każda warstwa i każdy skrypt — krótko. Każde pojedyncze sprawdzenie ma w skrypcie
opis, który test wypisuje (`[OK]`/`[BŁĄD]` + opis); pełna lista jest więc w wyniku testu (CI: podsumowanie zadania,
`k1-<wersja>-logi.tar.gz` w wydaniu). Tabele kroków 0.5–0.10.0 (do 0.10.0 w tym pliku): historia git
(`git log -p docs/TESTY.md`), `CHANGELOG.md`, `docs/archiwum/`.

## Jak uruchomić

```bash
scripts/przygotuj-srodowisko.sh   # raz w nowym kontenerze (pakiety Ubuntu)
scripts/zbuduj.sh                 # jądro + programy + obrazy + ISO → build/wynik/
make -j4 -O testy-host            # testy na hoście, równolegle (ok. 1,5 min z kompilacją)
scripts/test-m1.sh                # czytelność, host, fuzzing, start, testy w systemie, błędy startu
scripts/test-<część>.sh           # jedna część (mapa niżej)
scripts/testy.sh                  # wszystko — to uruchamia CI (z KVM równolegle; bez KVM po kolei, ok. 40 min)
```

**Przed scaleniem** (decyzja właściciela 2026-11-14, `AGENTS.md`): lokalnie `scripts/test-m1.sh` i skrypty zmienianej
części; pełny komplet — CI na PR-ze (etykieta `testy`, z KVM ok. 12 min testów). Repo jest publiczne, minuty CI są
bezpłatne; kontener bez KVM liczy QEMU w emulacji, więc pełny komplet lokalnie — tylko gdy CI nie działa.
**Przeglądarki nie testuje CI** (decyzja właściciela 2026-11-17, przy wydaniu 0.10.1): `test-przegladarka.sh` usunięty
— przez wiele dni padał w CI na zachowaniu Firefoksa (przebiegi 125–142 i dalej), a błędy, które naprawdę coś znaczyły
(przewijanie po nowej karcie), znalazł właściciel na sprzęcie. Przeglądarkę sprawdza właściciel na X230 i IdeaPadzie
(lista w `docs/DO_ZROBIENIA.txt`, punkt 4) i zgłasza błędy; Claude odtwarza je na **stanowisku** (`scripts/stanowisko.sh`,
niżej) i poprawia. W CI zostaje wszystko pod przeglądarką, co jest K1: strefa i VMM (`test-strefa.sh` — strefa próbna),
łącznik i reguła sieci D3 (`test_lacznik`, `fuzz_lacznik`), dysk sesji (`test_dysk_strefy`, `t_skarb_strefy`), balon,
kompozytor (`test_palce`). Integracyjnie (prawdziwy Firefox, strona przez łącznik, odmowa D3 przez nadzorcę) — tylko na
sprzęcie.
**Scalenie przez Claude'a uruchamia CI na `main`** (sprawdzone 2026-11-16: po scaleniu #124 sam ruszył przebieg 113 —
zdarzenie `push`, zielony). Po #119 przebiegu na liście Actions nie widać z nieznanego powodu — lista bywa niekompletna
(przebiegów 75–106 na niej nie ma), więc brak przebiegu na liście niczego nie dowodzi. Dowodem przed scaleniem jest pełny
przebieg na PR-ze (etykieta `testy` — liczy stan „`main` + PR”). Jeśli po scaleniu zmiany w systemie przebieg na `main`
nie ruszy w ciągu kilku minut, Claude prosi właściciela o „Run workflow” (Actions → „K1 — budowa i testy”; Claude dostaje
tu 403). Wydanie (tag, ISO, obraz USB) powstaje tylko z przebiegu na `main`, gdy `VERSION` ma nową wersję.
**Na `main` testów nie ma** (decyzja właściciela 2026-11-17): przebieg na `main` tylko buduje i wydaje — pełny komplet
idzie wcześniej, na PR-ze (przebieg 171 wydania 0.10.1 zawiesił się w test-strefa, choć ten sam kod przeszedł na PR-ze).
Wydanie nie ma już pliku `k1-<wersja>-logi.tar.gz` — logi testów są w artefakcie przebiegu PR-a.
**Pamięć podręczna na PR-ze:** PR widzi kopie zapisane na `main` i swoje własne, nie cudze. Zbudowane jądro zapisuje
tylko przebieg na `main`, więc pierwszy PR po zmianie jądra albo tabel pobieranych plików buduje i pobiera wszystko od
zera — dłużej niż zwykle (limit zadania: 80 min; sam krok testów: 45 min, żeby po zawieszonym teście zostały logi). Z KVM `testy.sh` co minutę pisze, które testy jeszcze trwają i od ilu minut („[k1] trwa (12 min): test-strefa”) — widać to na żywo na stronie przebiegu. Gdy `git.kernel.org` nie odpowiada, pliki kart idą z zapasu (gitlab.com)
po pierwszej nieudanej próbie (przebieg 108 PR-a #123 wisiał na tym ponad 40 min).

Zmienne: `K1_TEST_CZAS` — limit na jedno uruchomienie QEMU (domyślnie 240 s), `K1_FUZZ_CZAS` — sekundy na fuzzer
(domyślnie 10; CI: 30), `K1_TESTY_ROWNOLEGLE=0|1` — wymuś po kolei / równolegle (domyślnie: równolegle, gdy jest
`/dev/kvm`), `K1_DUZE_ZDJECIA=0` — `test_dekoder` bez zdjęć 48 i 100 Mpx. Logi: `build/testy/<scenariusz>.log`,
zrzuty `build/testy/<część>-*.png`. Ręcznie: `scripts/uruchom-qemu.sh [--obraz build/wynik/rdzen-test.squashfs]`
i `tail -f build/qemu/serial.log`; zrzut: `scripts/zrzut-ekranu.sh plik.png`.

### Która część — który skrypt

| Zmieniasz | Uruchom (oprócz `test-m1.sh`) |
|---|---|
| init, initramfs, jądro, klatki, kanały, nadzorca (ogólnie) | `test-m1.sh` wystarczy; jądro i sprzęt — też `test-m5.sh` |
| ekran, wejście, gui, aplikacje, przybornik, logowanie | `test-m4.sh` |
| Pliki, Kosz, Notatki, okno wyboru pliku, przeciąganie | `test-pliki.sh` |
| czytnik, dekoder, Podgląd, tapeta ze zdjęcia, Ustawienia → Miejsce | `test-pendrive.sh` |
| skarbiec (format, kompaktowanie, miejsce, powiększenie) | `test-skarbiec.sh` (+ `test-pliki.sh`) |
| siec, kabel, DHCP, zegar z internetu | `test-siec.sh` |
| Wi-Fi | `test-wifi.sh` |
| strefy (KVM, VMM, ramka) | `test-strefa.sh` |
| przewijanie (kółko, touchpad; 0.10.0 krok 4) | na hoście `test_touchpad`, `test_zdarzenie`, `test_zalegle`, `fuzz_zdarzenie`, `test_palce` (gest palców w kompozytorze strefy, 0.10.1 krok 5); w systemie `t_okna_przewijanie`; QEMU: `test-pendrive.sh` (lista Plików), `test-strefa.sh` (gość); touchpad — tylko na sprzęcie (QEMU go nie udaje); palce w Firefoksie — `scripts/stanowisko.sh` (niżej) |
| przeglądarka w strefie (obraz, kompozytor, łącznik, sieć stref) | na hoście `test_lacznik`, `fuzz_lacznik`, `test_palce`; `test-strefa.sh` (strefa próbna); Firefox — `scripts/stanowisko.sh` i właściciel na sprzęcie (od 0.10.1 bez testu w CI) |
| sesje i dysk strefy (0.10.0 krok 5) | na hoście `test_dysk_strefy`, `test_sesje`, `test_komunikat` (strefy); w systemie `t_skarb_strefy`; QEMU: `test-strefa.sh` (licznik na dysku strefy próbnej); ciasteczko w prawdziwej przeglądarce — na sprzęcie |
| USB, bateria, pendrive z danymi | `test-m5.sh` |
| instalator, aktualizacja, A/B, pliki startowe, ISO | `test-m5f.sh` (+ `test-m6.sh`) |
| Secure Boot, MOK, `efi.c` | `test-m6.sh` |
| budowa (`scripts/zbuduj*.sh`, `Makefile`, musl) | `scripts/sprawdz-powtarzalnosc.sh` + pełny komplet w CI |

## Warstwy

### 1. Sprawdzane przy budowie
Zabezpieczenia programów (static-pie, RELRO, BIND_NOW, NX, brak RWX, pułapki fortify — `scripts/sprawdz-binaria.sh`,
próbka `testy/host/proba_fortify.c`: w granicach działa, przepełnienie — SIGILL), konfiguracja jądra (każda linia
`jadro/k1.config` w `.config`, `scripts/sprawdz-konfiguracje-jadra.sh`; KSPP — `scripts/sprawdz-kspp.sh`), musl (suma,
podpis PGP, łatki — `scripts/zbuduj-musl.sh`), pliki od producentów kart (sumy — `scripts/oprogramowanie-kart.sh`).
Błąd = budowa przerwana. Powtarzalność: `scripts/sprawdz-powtarzalnosc.sh` (dwie budowy → te same bajty).

### 2. Czytelność (`scripts/sprawdz-czytelnosc.py`, pierwszy krok `test-m1.sh`)
Plik `.c` ≤ 600 wierszy, funkcja ≤ 80 (wyjątek tylko z `k1: limit`), pliki testów ≤ 600; opis każdej usługi, aplikacji
i komunikatu; wpisy „Tymczasowe”; spis ADR; ścieżki `docs/…` w dokumentach istnieją.

### 3. Na hoście (`make testy-host`, glibc + ASan/UBSan)
Każdy test to osobny cel (`host-<nazwa>`, lista `TESTY_HOST` / `TESTY_HOST_PLIKI` w `Makefile`), więc `make -j4 -O`
puszcza je równolegle. Wspólne makra: `testy/host/k1_test_host.h` (`EXPECT`, `K1_TEST_KONIEC`). Brak narzędzia, którego
test używa (`grub-editenv`, `sfdisk`, mtools, `fsck.fat`), to błąd, nie pominięcie. Sprawdzenia opisują zachowanie
(oczekiwane liczby, teksty, bajty formatu), a nie powtarzają wzorów z kodu.

| Obszar | Testy |
|---|---|
| kanały, komunikaty, logi | `test_komunikat` (każdy typ: pola, zakresy, kierunek, deskryptor), `test_odbior`, `test_zdarzenie`, `test_log` (czyszczenie, limit), `test_cmdline` (`k1.rdzen`, `k1.rdzen_skrot`: brak, „..”, powtórzony, za długi), `test_uevent` |
| skarbiec | `test_odcinki` (format 2: spis, kompaktowanie, **przerwa prądu po każdym zapisie**, 70 000 obiektów, hasło, obce dane, magazyn formatu 1 nietknięty; `test_magazyn` i `test_przejscie` — usunięte z formatem 1 w 0.10.0), `test_indeks`, `test_dokumenty` (pliki, foldery, Kosz, prawa aplikacji przez prawdziwy kanał, nazwy, miejsce, zegar), `test_skarb_zalegle`, `test_notatki` |
| pulpit i aplikacje | `test_przybornik`, `test_menu`, `test_przeciaganie` (tabela reguł, zaznaczanie), `test_rozmieszczenie`, `test_kalkulator`, `test_podglad`, `test_zalegle` (kolejka ekranu), `podglad_grafiki` (renderer na brzegach obrazu) |
| pliki z zewnątrz | `test_fat` (obrazy z mtools/sfdisk: FAT12, FAT16 w MBR, FAT32 w GPT, „mały” FAT32 z 3000 plików, NTFS, uszkodzone), `test_dekoder` (PNG, JPEG, GIF, BMP, WebP; 48/100 Mpx; odmowy przed przydziałem) |
| start, instalator, aktualizacje | `test_uklad` (blok układu, grubenv — te same bajty co `grub-editenv`), `test_efi`, `test_manifest`, `test_sha256`, `test_instalator` (GPT czytana przez `sfdisk`), `test_start` (pliki startowe: przerwanie przed każdym z 28 zapisów, `fsck.fat`, mtools) |
| bateria | `test_bateria`, `test_bat_arch` |
| sieć | `test_siec` (DHCP, stan, komunikaty), `test_czas` (SNTP, runda), `test_wifi`, `test_wifi_polacz`, `test_wpa` (router w teście, KRACK), `test_krypto` (wektory FIPS/RFC/IEEE) |
| strefy | `test_virtio` (kolejki, wrogie łańcuchy, linie z liczbami od gościa — 0.10.1), `test_strefa_start` (boot_params, tablice), `test_powod_strefy` (powód końca strefy: kod VMM i zdarzenia klatki — 0.10.1 krok 1), `test_balon` + `fuzz_balon` (listy stron, bloki, statystyki balonu — 0.10.1 krok 2), `test_pamiec_stref` (górna granica: D2; od 0.10.1 krok 3 rozdzielacz — sesje w tle oddają pierwsze, sesja z fokusem ostatnia, zapas gościa i pamięć na start nietknięte, to, o co już poproszono, się liczy, histereza, „mało pamięci” tylko gdy żaden balon nie może wziąć więcej, krytycznie — do zamknięcia sesja w tle z największą pamięcią, nigdy ta z fokusem) |

Pomiary (nie w `testy-host`, bez ASan, ręcznie): `make build/obj/pomiar_magazynu`, `make build/obj/pomiar_podgladu`.
Programy pomocnicze dla skryptów QEMU: `magazyn_testowy` (dyski skarbca), `notatki_w_plikach` (magazyn formatu 1).

### 4. Fuzzing (`make fuzz`, clang + libFuzzer, ASan/UBSan)
Jedna reguła dla listy `FUZZ` w `Makefile` (plik `testy/host/fuzz_<nazwa>.c` + dwa wiersze: źródła i długość wejścia).
Każdy fuzzer to osobny cel (`fuzz-<nazwa>`); `test-m1.sh` puszcza je równolegle. Korpusy startowe:
`testy/host/korpus-<nazwa>`; zebrany korpus: `build/fuzz-korpus-<nazwa>` — w CI zachowywany między przebiegami (pamięć
podręczna Actions), więc każdy przebieg idzie głębiej. Przypadki, które libFuzzer zapisze (błąd, „wolny”):
`build/fuzz-wyniki/<nazwa>-…`. Parsery: linia poleceń, zdarzenia, komunikaty, magazyn (formaty 1 i 2, przejście),
uevent, archiwum baterii, manifest, układ K1-EFI, rozmieszczenie, wpisy plików, FAT, lista pendrive'a, dekoder, drzewo
K1-EFI, DHCP, SNTP, opisy sieci Wi-Fi, EAPOL, kolejki virtio.

### 5. W systemie (`testy/system/*.c` → `/rdzen/system/test-systemu`, obraz `rdzen-test.squashfs`)
Obraz testowy = produkcyjny + program testowy, który uruchamia nadzorca (jako root bez klatki: izolację sprawdza
z zewnątrz, przez `/proc/<pid>`, i od środka — próby). Wynik: `K1-TEST: WYNIK: OK (n/n)`. Scenariusz `testy-w-systemie`
w `test-m1.sh` dokłada sprzęt: SATA z GPT „K1-DANE”, NVMe z MBR, pendrive na EHCI, klawiatura USB na xHCI, mysz na OHCI,
baterię ACPI (`testy/acpi/bateria.asl`). NEGATYWNE = coś, co ma się nie udać.

| Plik | Co sprawdza |
|---|---|
| `t_system.c` | korzeń z trzema katalogami, `/` i `/rdzen` tylko do odczytu, `noexec` danych; NEGATYWNE: program w danych, memfd z prawem wykonania, skrypt `#!` — nie startują; `/proc` bez `sys`, `kallsyms`, `cmdline`; zwykły użytkownik nie widzi init i nie wyśle mu sygnału; brak `ptmx`, `pts`, `tty*`, `mem`, `port`; sysctl z init (13 + 23 sieci); LSM landlock, lockdown `[confidentiality]`, yama; NEGATYWNE: moduły, kexec, io_uring, BPF, userfaultfd, `modify_ldt`, `iopl` → ENOSYS, AF_INET6 → EAFNOSUPPORT; init zbiera sieroty i przeżywa SIGKILL; spis plików `/rdzen` (`t_inventory`), brak setuid; `t_fortify` (przepełnienie → SIGILL); `t_kvm` |
| `t_uslugi.c` | klatki każdej usługi i aplikacji z `k1_uslugi.h` (`t_klatki`: UID, 0 capabilities, `NoNewPrivs`, seccomp, cgroup, tylko przekazane deskryptory) i **próby w klatkach** (`t_proby`, `proba.c`: execve, open, setuid, socket każdego rodzaju, fork, kill, ptrace, mount, unshare, memfd, `PROT_EXEC`, obcy ioctl, zegar, nl80211, `/dev/kvm` — wszystko zablokowane); złośliwe komunikaty i obcy nadawca odrzuceni; awaria ekranu → wraca, padająca usługa wyłączona po 6 razach |
| `t_okna.c` | złośliwe okna (bez deskryptora, memfd bez pieczęci, za mały, panel i ustawienie od aplikacji, cudze okno, obcy UID), ochrona fokusu, limit pamięci powierzchni, zaległe komunikaty ekranu; awaria gui → okna zostają |
| `t_skarbiec.c` | zaległe odpowiedzi skarbca (klient, który nie odbiera, nie blokuje innych) |
| `t_sprzet.c` | dyski i kontrolery widoczne, K1-DANE po nazwie, MBR nieczytany, węzły dysków tylko root, brak `hidraw`/`sg`/`sr0`, nikt nie trzyma dysku; bateria |
| `t_strefy.c` | klatka VMM strefy próbnej (UID 300, cgroup, deskryptory), zabity VMM kończy tylko strefę |
| `t_wifi.c`, `t_wifi_ap.c` | nie sprawdzenia, a **punkty dostępu testowe** dla `test-wifi.sh` (obraz testowy z `mac80211_hwsim.radios=7`: beacon, uwierzytelnienie, strona routera WPA2, DHCP) |

### 6. QEMU z zewnątrz (`scripts/test-*.sh`, biblioteka `testy/qemu/k1qemu.py`)
Klasa `Maszyna`: `czekaj`, `czekaj_na`, `cisza`, `klik`, `dwuklik`, `prawy_klik`, `przycisk`, `przeciagnij`, `klik_z`,
`kolko` (0.10.0: ząbki kółka nad punktem — QEMU `mouse_move 0 0 dz`, dz > 0 = od siebie; czeka na licznik
„przewijanie N” ekranu), `pisz`, `zrzut`, `sprawdz`, `wyniki`; maszynę zamyka `zamknij_qemu` (`scripts/wspolne.sh`); sieć testowa —
`testy/qemu/k1siec.py` (drugi koniec kabla, router z DHCP i czasem).

**Bez stałych pauz.** Krok czeka na dowód, że się stał, nie na upływ czasu: polecenie monitora — na odpowiedź;
kliknięcie — na wiersz ekranu „przycisk wciśnięty/puszczony”; zrzut — aż ekran pokaże sprawdzany stan
(`zrzut(nazwa, gdy=…)`), zrzut na pamiątkę — aż obraz przestanie się zmieniać; to, że coś się *nie* dzieje —
`cisza(wzór, s)` (może najwyżej zaliczyć za wcześnie, nigdy fałszywie zawieść). Jedyny wyjątek: klawisze w MokManagerze
(`test-m6`, tylko z sekretem) — UEFI nie pisze logu. W każdym scenariuszu: brak „Kernel panic”, „BŁĄD”, zablokowanych
wywołań i odrzuconych komunikatów; tekstów, haseł, nazw plików i sieci nie ma w logu.

| Skrypt | Starty | Co sprawdza |
|---|---|---|
| `test-m1.sh` | `start`, `testy-w-systemie`, 7 negatywnych (z KVM równolegle) | start z dm-verity aż do pulpitu, brak baterii; testy w systemie (warstwa 5) z restartem ekranu i kontrolowanym wyłączeniem; NEGATYWNE: brak dysku, dysk bez obrazu, obraz bez init, zły `k1.rdzen` (pozostałe przypadki parametru — `test_cmdline`), brak skrótu, zły skrót, jeden bajt obrazu zmieniony — każdy kończy się komunikatem i wyłączeniem, bez paniki |
| `test-m4.sh` | `pulpit`, `ponownie`, `haslo`, `iso-bios` | kursor, mysz i klawiatura (dawny `test-m2.sh`: ruch, strzałki, przycisk, brak śladu); pulpit (ikony, menu pod prawym przyciskiem, przypinanie, przeciąganie na pasku i pulpicie), menu Start, okienko sesji (0.10.1 krok 4: Start → Przeglądarka — kafelki Ogólnej, Banku i Prywatnej z kropką w kolorze sesji, zrzut `m4-1f-okienko-sesji`; „Anuluj” nie uruchamia strefy), Notatki (polskie znaki, dwie notatki), Zegar, Ustawienia (ciemny motyw, tapeta, rozdzielczość; 0.10.1 krok 4: Przeglądarka w obu motywach — trzy karty sesji, rząd pikseli między kartami w kolorze tła okna, czyli opis nie wchodzi na następną kartę; zrzuty `m4-5c-przegladarka-jasny`, `-ciemny`), Zasoby, Kalkulator, minimalizacja, Start → Wyłącz; dysk zaszyfrowany; po ponownym starcie ustawienia, rozmieszczenie i notatki wracają; hasło: ekran hasła, złe i dobre, Wyloguj; start z ISO przez BIOS (SeaBIOS, VESA, simpledrm) |
| `test-pliki.sh` | `pierwszy`, `ponownie`, `notatki` | foldery, nazwy (zajęta — odmowa), Kosz (przywróć, usuń na zawsze z pytaniem), pliki na pulpicie i ich menu, przeciąganie i Ctrl+klik, foldery stałe (odmowa), po ponownym starcie zostają; dysk formatu 1 z notatkami jako plikami: Notatki, okno wyboru pliku (Otwórz, Anuluj), „Otwórz za pomocą”, domyślna aplikacja, Ustawienia → Aplikacje |
| `test-pendrive.sh` | `pendrive`, `ponownie` | wirtualny pendrive FAT32 (mtools, Pillow): czytnik w klatce tylko do odczytu, import (pojedynczo, wszystkie, drugi raz — pominięte), pliki 17 MB i 300 MB kawałkami, miniatury (JPEG, PNG, GIF, BMP, WebP, 48/100 Mpx), NEGATYWNE: uszkodzony JPEG, 101 Mpx, plik z pendrive'a na Kosz, plik bez aplikacji; tapeta ze zdjęcia; Podgląd (przybliżanie, obrót, ← →, Delete, Ctrl+O, płynność); Ustawienia → Miejsce; wyjęcie, NTFS (odmowa), „mały” FAT32 z 1100 zdjęciami (0.10.0 krok 4: kółko nad listą — 3 wiersze na ząbek, w górę ponad początek, NEGATYWNY: nad miejscami po lewej nic); po ponownym starcie tapeta i magazyn z filmem otwarty od razu |
| `test-skarbiec.sh` | `dziennik` (+ponownie), `przejscie`, `pelny`, `powiekszenie` (+ponownie), `dokonczenie` | dyski z `magazyn_testowy`: długi dziennik → kompaktowanie przy otwarciu; przejście z formatu 1 na 2 dopiero po potwierdzonym starcie, krokami < 1 s; prawie pełna K1-DANE: „Nowy folder” odrzucony, ustawienie systemu się zapisuje; „Użyj całego pendrive'a na dane” → nowa tablica, import 16 zdjęć; dokończenie powiększenia po przerwie prądu; na hoście treść każdego obiektu |
| `test-siec.sh` | 1 | karta e1000e kablem do sieci testowej: zanim jest adres, od K1 tylko DHCP; 5 złych odpowiedzi DHCP odrzuconych; z adresem — ARP tylko o swój adres, RST na 1034 SYN-y, żadnego ICMP; odnowienie dzierżawy; kabel wyjęty i włożony; ikona sieci, Ustawienia → Sieć; zegar z internetu (milczą, źle, niezgodnie, sprzed wydania → bez zmian; dobrze → ustawiony, Kosz przesunięty) |
| `test-wifi.sh` | `wifi`, `wifi-2` | baza przepisów radiowych; lista sieci (złośliwe nazwy pokazane bezpiecznie, zepsuty opis pominięty), przełącznik radia (rfkill), ikona; sieć otwarta, złe i dobre hasło WPA2, NEGATYWNE: krótka i podrobiona ramka EAPOL; „Zapomnij”; ponowny start — łączy się sama |
| `test-strefa.sh` | `strefa`, `strefa-bez-wirt` | KVM w emulacji (AMD SVM z NPT): VMM w klatce, okno strefy w ramce (pasek tytułu K1 nad obrazem i obwódka — krok 3, szkic A; przesuwanie za pasek, minimalizuj i zamknij na pasku), pamięć kompresowana w gościu (zram), linia liczników strefy co kilka sekund i przy końcu, szczyt pamięci klatki u nadzorcy (0.10.1 krok 1), **balon** (0.10.1 krok 2: złośliwi goście balonu — strona poza pamięcią, blok nie z całych stron, za długa lista — koniec tylko strefy; uczciwy gość w trybie 9: górna granica ponad pamięć na start, zwolnione 64 MiB wracają do K1 — „oddane” w liczniku, cel balonu 32 MiB i 0 na prośbę programu testowego, nagły skok prawie do granicy — żyje), wejście tylko przy fokusie, kółko (0.10.0 krok 4: `REL_WHEEL` i `REL_WHEEL_HI_RES` w gościu — także bez fokusu, do okna pod kursorem), dolny pasek; NEGATYWNE: 4 złośliwe tryby gościa — koniec tylko strefy; procesor bez wirtualizacji — okienko odmowy; obraz produkcyjny bez strefy próbnej. Krok 5: K1 z dyskiem danych (K1-DANE) — strefa próbna pamięta: gość liczy na dysku strefy swoje uruchomienia (1; po złośliwych gościach 2), program testowy prosi o „Wyczyść dane sesji” — znów 1; liczby dysku z logu VMM, bez błędów. 0.10.1 krok 3: **rozdzielacz** — strefa w trybie 10 (zwykły gość, balonem rządzi rozdzielacz), K1 w teście ma 1 GiB, czyli mniej niż rezerwa: nadzorca sam ustawia cel balonu i gość oddaje strony, gui mówi, która strefa ma fokus, Zasoby pokazują przydział strefy, okienko „Mało pamięci” (zrzut `strefa-9-malo-pamieci`) — „Zamknij” zamyka strefę bez okienka „zamknęła się” |
| `test-m5.sh` | `usb`, `pendrive1`, `pendrive2` | maszyna bez PS/2: klawiatura i mysz USB, mysz podłączana, odłączana i znów podłączana; bateria ACPI (wskaźnik, Ustawienia → Bateria, historia między startami); pendrive z K1-DANE włożony po starcie; obraz USB startuje z pendrive'a, pierwszy start tworzy magazyn, drugi otwiera |
| `test-m5f.sh` | 11 (`0-z-iso` … `6-starsza`) | start z ISO przez UEFI (OVMF, virtio, simpledrm, rdzeń przez loop) — Instalator nie ma skąd kopiować; uszkodzony system na pendrivie — odmowa przed zapisem; instalacja (GPT, pliki startowe = pendrive'a, `fsck.fat`); start z dysku; zmienione pliki startowe — odmowa; aktualizacja przerwana (wolny dysk, `quit`) i pełna; A/B: nowe miejsce na próbę i potwierdzone, uszkodzone → wycofanie; starsza wersja odrzucona |
| `test-m6.sh` | 5 (+2 z sekretem) | OVMF z Secure Boot i kluczami Microsoftu: klucz K1 w MOK → start; bez klucza i ze zmienionym `k1.efi` → odmowa z wyjaśnieniem; z sekretem `K1_KLUCZ_PODPISU` (CI na `main`) pełna droga przez MokManagera, bez niego tylko „wpisanie niedostępne”; K1 z dysku: „Przygotuj klucz K1” |

Pułapki QEMU, Wi-Fi, jądra i kanałów, które już wystąpiły: skill `kons-dev`, §7.

## Stanowisko na hoście (`scripts/stanowisko.sh`, 0.10.1 krok 5)

Narzędzie do szukania błędów przeglądarki w strefie bez maszyny wirtualnej (kilka minut zamiast przebiegu CI):
kompozytor strefy zbudowany na tym komputerze z ASan/UBSan (ekran w pamięci — `testy/stanowisko/ekran.c` zamiast
`drm.c`) i prawdziwy Firefox z obrazu strefy (`build/strefa-przegladarka`) w chroot, jako użytkownik 1000, ze
środowiskiem jak w `gosc/przegladarka.c`. Wejście kompozytora to kolejka FIFO jako `/dev/input/event0` (zdarzenia jak
z virtio-input VMM: `idz`, `klik`, `kolko`, `dokladne`, `palce`, `pisz`), Firefox loguje protokół Wayland
(`WAYLAND_DEBUG`), strona testowa wpisuje do tytułu, jak daleko ją przewinięto. Scenariusz to plik Pythona
(`testy/stanowisko/palce.py` — przewijanie dwoma palcami po nowej karcie; `sprawdz(opis, warunek)`). Wymaga roota
(`unshare -m`, `chroot`) i `scripts/zbuduj.sh`. Nie wchodzi do `scripts/testy.sh` (Firefox na hoście zależy od komputera
budowy) — błędy, które tak znaleziono: reguła 28 (0.10.1 krok 1), brak `axis_stop` (krok 5). Wyniki:
`build/stanowisko/` (logi, zrzuty `.ppm` — `python3 -I scripts/ppm-na-png.py`).
