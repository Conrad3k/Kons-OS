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
Szukanie błędu przeglądarki (tylko z KVM): etykieta `testy-przegladarka` — budowa i sam `test-przegladarka` (ok. 20 min,
`K1_TESTY_TYLKO`; test wtedy WĄSKO — tylko otwarcie strony i liczniki, `K1_PRZEGLADARKA_WASKO`); do scalenia nigdy nie
wystarcza.

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
| przeglądarka w strefie (obraz, kompozytor, łącznik, sieć stref) | `test-przegladarka.sh`; na hoście `test_lacznik`, `fuzz_lacznik` |
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
| skarbiec | `test_magazyn` (format 1, awarie), `test_odcinki` (format 2: spis, kompaktowanie, **przerwa prądu po każdym zapisie**, 70 000 obiektów), `test_przejscie` (format 1 → 2, przerwy prądu), `test_indeks`, `test_dokumenty` (pliki, foldery, Kosz, prawa aplikacji przez prawdziwy kanał, nazwy, miejsce, zegar), `test_skarb_zalegle`, `test_notatki` |
| pulpit i aplikacje | `test_przybornik`, `test_menu`, `test_przeciaganie` (tabela reguł, zaznaczanie), `test_rozmieszczenie`, `test_kalkulator`, `test_podglad`, `test_zalegle` (kolejka ekranu), `podglad_grafiki` (renderer na brzegach obrazu) |
| pliki z zewnątrz | `test_fat` (obrazy z mtools/sfdisk: FAT12, FAT16 w MBR, FAT32 w GPT, „mały” FAT32 z 3000 plików, NTFS, uszkodzone), `test_dekoder` (PNG, JPEG, GIF, BMP, WebP; 48/100 Mpx; odmowy przed przydziałem) |
| start, instalator, aktualizacje | `test_uklad` (blok układu, grubenv — te same bajty co `grub-editenv`), `test_efi`, `test_manifest`, `test_sha256`, `test_instalator` (GPT czytana przez `sfdisk`), `test_start` (pliki startowe: przerwanie przed każdym z 28 zapisów, `fsck.fat`, mtools) |
| bateria | `test_bateria`, `test_bat_arch` |
| sieć | `test_siec` (DHCP, stan, komunikaty), `test_czas` (SNTP, runda), `test_wifi`, `test_wifi_polacz`, `test_wpa` (router w teście, KRACK), `test_krypto` (wektory FIPS/RFC/IEEE) |
| strefy | `test_virtio` (kolejki, wrogie łańcuchy), `test_strefa_start` (boot_params, tablice) |

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
`pisz`, `zrzut`, `sprawdz`, `wyniki`; maszynę zamyka `zamknij_qemu` (`scripts/wspolne.sh`); sieć testowa —
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
| `test-m4.sh` | `pulpit`, `ponownie`, `haslo`, `iso-bios` | kursor, mysz i klawiatura (dawny `test-m2.sh`: ruch, strzałki, przycisk, brak śladu); pulpit (ikony, menu pod prawym przyciskiem, przypinanie, przeciąganie na pasku i pulpicie), menu Start, Notatki (polskie znaki, dwie notatki), Zegar, Ustawienia (ciemny motyw, tapeta, rozdzielczość), Zasoby, Kalkulator, minimalizacja, Start → Wyłącz; dysk zaszyfrowany; po ponownym starcie ustawienia, rozmieszczenie i notatki wracają; hasło: ekran hasła, złe i dobre, Wyloguj; start z ISO przez BIOS (SeaBIOS, VESA, simpledrm) |
| `test-pliki.sh` | `pierwszy`, `ponownie`, `notatki` | foldery, nazwy (zajęta — odmowa), Kosz (przywróć, usuń na zawsze z pytaniem), pliki na pulpicie i ich menu, przeciąganie i Ctrl+klik, foldery stałe (odmowa), po ponownym starcie zostają; dysk formatu 1 z notatkami jako plikami: Notatki, okno wyboru pliku (Otwórz, Anuluj), „Otwórz za pomocą”, domyślna aplikacja, Ustawienia → Aplikacje |
| `test-pendrive.sh` | `pendrive`, `ponownie` | wirtualny pendrive FAT32 (mtools, Pillow): czytnik w klatce tylko do odczytu, import (pojedynczo, wszystkie, drugi raz — pominięte), pliki 17 MB i 300 MB kawałkami, miniatury (JPEG, PNG, GIF, BMP, WebP, 48/100 Mpx), NEGATYWNE: uszkodzony JPEG, 101 Mpx, plik z pendrive'a na Kosz, plik bez aplikacji; tapeta ze zdjęcia; Podgląd (przybliżanie, obrót, ← →, Delete, Ctrl+O, płynność); Ustawienia → Miejsce; wyjęcie, NTFS (odmowa), „mały” FAT32 z 1100 zdjęciami; po ponownym starcie tapeta i magazyn z filmem otwarty od razu |
| `test-skarbiec.sh` | `dziennik` (+ponownie), `przejscie`, `pelny`, `powiekszenie` (+ponownie), `dokonczenie` | dyski z `magazyn_testowy`: długi dziennik → kompaktowanie przy otwarciu; przejście z formatu 1 na 2 dopiero po potwierdzonym starcie, krokami < 1 s; prawie pełna K1-DANE: „Nowy folder” odrzucony, ustawienie systemu się zapisuje; „Użyj całego pendrive'a na dane” → nowa tablica, import 16 zdjęć; dokończenie powiększenia po przerwie prądu; na hoście treść każdego obiektu |
| `test-siec.sh` | 1 | karta e1000e kablem do sieci testowej: zanim jest adres, od K1 tylko DHCP; 5 złych odpowiedzi DHCP odrzuconych; z adresem — ARP tylko o swój adres, RST na 1034 SYN-y, żadnego ICMP; odnowienie dzierżawy; kabel wyjęty i włożony; ikona sieci, Ustawienia → Sieć; zegar z internetu (milczą, źle, niezgodnie, sprzed wydania → bez zmian; dobrze → ustawiony, Kosz przesunięty) |
| `test-wifi.sh` | `wifi`, `wifi-2` | baza przepisów radiowych; lista sieci (złośliwe nazwy pokazane bezpiecznie, zepsuty opis pominięty), przełącznik radia (rfkill), ikona; sieć otwarta, złe i dobre hasło WPA2, NEGATYWNE: krótka i podrobiona ramka EAPOL; „Zapomnij”; ponowny start — łączy się sama |
| `test-strefa.sh` | `strefa`, `strefa-bez-wirt` | KVM w emulacji (AMD SVM z NPT): VMM w klatce, okno strefy w ramce (obwódka, znaczek, plakietka), wejście tylko przy fokusie, pasek, minimalizacja, zamknięcie; NEGATYWNE: 4 złośliwe tryby gościa — koniec tylko strefy; procesor bez wirtualizacji — okienko odmowy; obraz produkcyjny bez strefy próbnej |
| `test-przegladarka.sh` | `przegladarka` | obraz produkcyjny z siecią QEMU (`-netdev user`: router 10.0.2.2 = serwer strony w Pythonie): Start → Przeglądarka, Firefox w ramce sesji „Ogólna”, adres wpisany z klawiatury → strona przez przekaźnik, łącznik i gniazdo od nadzorcy, tekst z formularza dociera do serwera; NEGATYWNE: strona testowa sama prosi o obrazek z sieci domowej (10.0.2.3) — odmowa D3 (bez wpisywania: tak działałaby złośliwa strona); w logu K1 żadnego adresu ani tekstu; zamknięcie kończy maszynę. Nazwy stron (DNS) — tylko na hoście (DNS sieci QEMU prowadzi do resolvera komputera, którego kontener testów nie ma). W emulacji (bez KVM) strefa ma 1 procesor; start Firefoksa ok. 4–5 min |
| `test-m5.sh` | `usb`, `pendrive1`, `pendrive2` | maszyna bez PS/2: klawiatura i mysz USB, mysz podłączana, odłączana i znów podłączana; bateria ACPI (wskaźnik, Ustawienia → Bateria, historia między startami); pendrive z K1-DANE włożony po starcie; obraz USB startuje z pendrive'a, pierwszy start tworzy magazyn, drugi otwiera |
| `test-m5f.sh` | 11 (`0-z-iso` … `6-starsza`) | start z ISO przez UEFI (OVMF, virtio, simpledrm, rdzeń przez loop) — Instalator nie ma skąd kopiować; uszkodzony system na pendrivie — odmowa przed zapisem; instalacja (GPT, pliki startowe = pendrive'a, `fsck.fat`); start z dysku; zmienione pliki startowe — odmowa; aktualizacja przerwana (wolny dysk, `quit`) i pełna; A/B: nowe miejsce na próbę i potwierdzone, uszkodzone → wycofanie; starsza wersja odrzucona |
| `test-m6.sh` | 5 (+2 z sekretem) | OVMF z Secure Boot i kluczami Microsoftu: klucz K1 w MOK → start; bez klucza i ze zmienionym `k1.efi` → odmowa z wyjaśnieniem; z sekretem `K1_KLUCZ_PODPISU` (CI na `main`) pełna droga przez MokManagera, bez niego tylko „wpisanie niedostępne”; K1 z dysku: „Przygotuj klucz K1” |

Pułapki QEMU, Wi-Fi, jądra i kanałów, które już wystąpiły: skill `kons-dev`, §7.
