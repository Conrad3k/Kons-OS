# Kons K1 Sovereign

Własny, minimalistyczny i bezpieczny system operacyjny na jądrze Linux. Nie jest dystrybucją
Linuksa: ma własny program startowy, własny ekran i pulpit, (wkrótce) własny magazyn danych
i ścisłą izolację aplikacji. Wszystko, co nasze, jest napisane w C.

> Wcześniej w tym repozytorium był **Kons-OS** — dystrybucja na bazie Debiana (wersje 0.1.0–0.1.2).
> Jest zachowany w gałęzi [`archiwum/debian-0.1.2`](../../tree/archiwum/debian-0.1.2) i tagach `v0.1.x`.

## Na jakim etapie jesteśmy

| Etap | Co to daje | Status |
|---|---|---|
| M0 Porządki | nowe repo, dokumenty, narzędzia, automatyczne budowanie | PRZETESTOWANE |
| M1 Start | system startuje w maszynie wirtualnej (QEMU) i pisze „K1: init OK” | PRZETESTOWANE |
| M2 Ekran | obraz na ekranie: tapeta, kursor; mysz i klawiatura | PRZETESTOWANE (w QEMU) |
| M3 Trusted Core | nadzorca usług, każda usługa we własnej „klatce”, restart po awarii | PRZETESTOWANE |
| M4 GUI | pulpit, okna, aplikacje Zegar, Notatki, Ustawienia; obraz ISO do maszyny wirtualnej | PRZETESTOWANE (QEMU: UEFI i BIOS) |
| M4.1 Porządki i audyt | przegląd bezpieczeństwa, wybór rozdzielczości, aplikacja Zasoby | PRZETESTOWANE |
| M5 Dane i sprzęt | zaszyfrowane dane, hasło, dyski i USB, pendrive, **instalator na dysk** | PRZETESTOWANE w QEMU (0.5.9); czeka na test na X230 |
| 0.6.0 Fundament | porządki w nadzorcy, zdarzenia jądra zamiast odpytywania, szybsze testy, historia baterii | PRZETESTOWANE |
| M6 Bezpieczne aktualizacje | podpisane wydania, wykrywanie podmiany systemu, A/B, Secure Boot z własnym kluczem | PRZETESTOWANE w QEMU (0.7.0); czeka na test na X230 |
| M7 Sieć | kabel, potem Wi-Fi | PLANOWANE |
| M8 Strefy | każda obca aplikacja (przeglądarka, Discord…) w swojej mikro-maszynie; kolorowa ramka; stała/jednorazowa; pełna izolacja/szybka grafika | PLANOWANE (ADR 0020) |
| M9 Pliki | aplikacja Pliki, przenoszenie plików ze stref przez okno K1, podgląd w izolacji | PLANOWANE |
| M10–M12 | tryb ratunkowy i TPM, audyt końcowy | PLANOWANE (kolejność: ADR 0019, 0020) |

Pełny plan i zasady: [`docs/SPEC.md`](docs/SPEC.md). Co zostało do zrobienia (i czego potrzeba od właściciela):
[`docs/DO_ZROBIENIA.txt`](docs/DO_ZROBIENIA.txt). Co się zmieniło: [`CHANGELOG.md`](CHANGELOG.md).

**Ważne:** K1 działa w maszynie wirtualnej: pulpit z paskiem na dole (Start, Zegar, Notatki,
Ustawienia), paskiem na górze (dźwięk, data i godzina), oknami i polską klawiaturą. Od 0.5.2 zna
dyski SATA/NVMe oraz klawiatury, myszy i pamięci USB — można spróbować startu z pendrive'a na laptopie
ThinkPad X230 (niżej). Notatki i ustawienia są na razie tylko w pamięci (znikają po wyłączeniu, do M5).
Wygląd: [`docs/WYGLAD.md`](docs/WYGLAD.md).

## Uruchom w maszynie wirtualnej
1. Pobierz obraz: **Releases** → najnowsze `k1-vX.Y.Z` → plik `k1-X.Y.Z.iso` (ok. 16 MB).
   (Albo Actions → „K1 — budowa i testy” → ostatnie zielone uruchomienie → Artifacts → `k1-<wersja>-obraz`.)
2. **VirtualBox** (zalecany): Nowa maszyna → typ *Other Linux (64-bit)*, bez dysku twardego.
   - Pamięć: **1024 MB**, procesory: **2**.
   - Mysz i klawiatura działają od razu (sprawdzone w VirtualBoxie z EFI). Jeśli kursor się nie
     rusza: System → Płyta główna → Urządzenie wskazujące: **Mysz PS/2**.
   - „Włącz EFI” może być włączone albo nie.
   - Ekran: kontroler **VMSVGA**, pamięć wideo **64 MB lub więcej** (większe rozdzielczości
     potrzebują więcej pamięci karty). Od wersji 0.5.0 rozdzielczość wybierasz w K1:
     **Ustawienia → Ekran**. Gdyby przy VMSVGA obraz w ogóle się nie pojawił, zmień kontroler na
     **VBoxSVGA** — wtedy K1 użyje obrazu z firmware (z EFI tylko 1024×768; większy:
     `"C:\Program Files\Oracle\VirtualBox\VBoxManage.exe" setextradata "NAZWA MASZYNY" VBoxInternal2/EfiGraphicsResolution 1920x1080`).
   - Pamięć masowa: napęd optyczny → wybierz pobrany plik ISO. Sieć i dźwięk można wyłączyć.
3. Uruchom. Po kilku sekundach pojawi się pulpit. Kliknij w okno maszyny, żeby „złapać” mysz
   (prawy Ctrl ją uwalnia). Polskie znaki: prawy Alt + litera (np. prawy Alt + a = ą).
4. Wyłączenie: znak K1 na dolnym pasku → **Wyłącz**.

Działa też w VMware (UEFI lub BIOS) i QEMU/virt-manager (karta `std` lub `virtio`; nie `vmware` —
sterownik VMware nie działa z tą kartą QEMU); Hyper-V — jeszcze nie.

## Uruchom na laptopie (ThinkPad X230) — z pendrive'a, z zapisem danych
Od 0.5.6 jest osobny obraz na pendrive: **`k1-X.Y.Z-usb.img.xz`** (Releases). Ma miejsce na Twoje dane
(ustawienia, notatki) — **zaszyfrowane, na tym samym pendrivie**. Bez Twojej decyzji K1 **niczego nie
zapisuje na dysku laptopa** — zmienia go tylko Instalator (punkt 7). Pendrive zostanie w całości nadpisany.
1. Nagraj `k1-X.Y.Z-usb.img.xz` na pendrive (≥ 512 MB): w Windowsie **Rufus** → wybierz plik
   (Rufus sam rozpakuje .xz; jeśli zapyta — tryb obrazu DD) → Start. Albo balenaEtcher.
   (Obraz `.iso` też zadziała z pendrive'a, ale bez zapisu danych — po wyłączeniu wszystko znika,
   a Ustawienia → Hasło piszą „Nie znaleziono miejsca na dane”. Sprawdzisz w Ustawieniach → Informacje:
   „Dane: zaszyfrowane na nośniku K1”.)
2. **Najpierw wyłącz Secure Boot** — dopóki klucz K1 nie jest wpisany (sekcja „Secure Boot z kluczem K1”
   niżej; wersje deweloperskie zawsze wymagają wyłączonego Secure Boot). Bez tego laptop pokaże
   „Secure Boot Violation” / „Invalid signature”, a po „OK” uruchomi system z dysku (sprawdzone
   na X230, 2026-11-07). Włącz laptop i od razu wciskaj **F1** (ustawienia BIOS-u):
   - **Security → Secure Boot → Secure Boot: Disabled**,
   - Startup → **UEFI/Legacy Boot**: *Both* albo *UEFI Only*,
   - **F10** → *Yes* (zapisz i uruchom ponownie).
   Potem przy włączaniu wciskaj **F12** i wybierz pendrive (USB HDD). Secure Boot możesz po teście
   włączyć z powrotem. **Uwaga, BitLocker:** jeśli Windows na dysku jest zaszyfrowany BitLockerem,
   zmiana Secure Boot może sprawić, że przy następnym starcie zapyta o *klucz odzyskiwania* — przed
   zmianą miej go pod ręką (konto Microsoft → urządzenia) albo w Windowsie wstrzymaj ochronę
   BitLocker („Wstrzymaj ochronę”).
3. Po kilku sekundach powinien pojawić się pulpit. Klawiatura, TrackPoint, touchpad (ruch i kliknięcia,
   bez przewijania dwoma palcami) i mysz USB powinny działać. Na górnym pasku przy głośniku — bateria
   (procent; błyskawica, gdy podłączysz zasilacz — zmiana widoczna do 15 s). Jasność ekranu i klawisze
   Fn — jeszcze nie.
4. Wyłączenie: znak K1 → **Wyłącz** (laptop powinien się sam wyłączyć), potem wyjmij pendrive.
5. Zapis: zmień tapetę albo motyw (Ustawienia), napisz coś w Notatkach, wyłącz i uruchom ponownie —
   wszystko powinno wrócić. Bez hasła dane są zaszyfrowane, ale ktoś z Twoim pendrive'em może je
   odczytać.
6. Hasło (od 0.5.7): Ustawienia → Hasło → przełącznik „Wymagaj hasła przy włączaniu” → wpisz hasło
   dwa razy → Zapisz. Przy następnym włączeniu zobaczysz ekran hasła. **Zapomniane hasło = nowe
   konto** (dane zaszyfrowane starym hasłem przepadają — trzeba nagrać pendrive od nowa).
7. **Instalacja na dysk laptopa** (od 0.5.9): znak K1 → **Instalator** → wybierz dysk → *Zainstaluj* →
   wpisz **USUŃ** → *Usuń wszystko i zainstaluj*. **Wszystko na tym dysku zostanie usunięte** (Windows,
   pliki). Po „Gotowe”: *Wyłącz komputer*, wyjmij pendrive, włącz laptop — K1 startuje z dysku (jeśli nie:
   F12 → dysk), a Twoje zaszyfrowane dane mają cały dysk. Dane z pendrive'a nie są przenoszone (nowe
   konto). **Ustaw hasło** (punkt 6) — bez niego złodziej laptopa może odczytać dane.
   **Aktualizacja:** nagraj nowszy `k1-…-usb.img.xz` na pendrive, **uruchom K1 z dysku** (bez F12),
   podłącz pendrive, Instalator → *Zaktualizuj system* → *Zaktualizuj*. Wymienia tylko system — Twoje
   dane zostają. Od 0.6.2 Instalator sprawdza odcisk i podpis systemu i nie pozwoli wrócić do starszej
   wersji (dlatego lepiej startować z dysku: wtedy sprawdza Twój system, a nie kod z pendrive'a).
   Od 0.7.0 nowa wersja trafia **obok** obecnej (A/B): po restarcie startuje na próbę, a gdyby nie
   wystartowała — wraca poprzednia, a Instalator o tym napisze.

## Klucz podpisu (jednorazowo, od 0.6.2)
Wydania K1 podpisuje GitHub kluczem, który znasz tylko Ty (i GitHub). **Nie wklejaj go nigdzie
indziej — także w czacie.**
1. W Windowsie otwórz **PowerShell** i wklej (generuje 32 losowe bajty):
   `$b = New-Object byte[] 32; [Security.Cryptography.RandomNumberGenerator]::Create().GetBytes($b); [Convert]::ToBase64String($b)`
2. Skopiuj wynik (44 znaki, kończy się `=`). Zapisz go też w bezpiecznym miejscu (menedżer haseł) —
   bez niego nie da się wydać aktualizacji, którą przyjmie zainstalowany K1.
3. GitHub → repozytorium **Kons-OS** → **Settings** → **Secrets and variables** → **Actions** →
   **New repository secret**: Name `K1_KLUCZ_PODPISU`, Secret: wklejony wynik → **Add secret**.
4. Następne wydanie będzie podpisane (opis wydania: „System: podpisane kluczem K1”). Zainstaluj je
   **od nowa** albo zaktualizuj — od tej chwili Twój K1 przyjmie tylko wersje podpisane tym kluczem.
   Ten sam sekret daje też klucz do Secure Boot (od 0.7.0) — nic więcej nie trzeba.

## Secure Boot z kluczem K1 (od 0.7.0, gdy wydanie jest podpisane)
Z włączonym Secure Boot laptop uruchomi tylko K1 podpisany Twoim kluczem (podmieniony albo uszkodzony
system się nie uruchomi). Klucz wpisuje się raz:
1. Uruchom K1 z dysku (Secure Boot jeszcze wyłączony) → menu Start → **Instalator** → na dole
   **Secure Boot…** → **Przygotuj klucz K1** → **Uruchom ponownie** (bez pendrive'a).
2. Pojawi się niebieski ekran *Shim UEFI key management* — **w ciągu 10 sekund naciśnij dowolny klawisz**
   (jeśli nie zdążysz, K1 wystartuje normalnie — wtedy powtórz punkt 1).
3. Strzałkami: **Enroll MOK** → Enter → **Continue** → Enter → **Yes** → Enter → wpisz hasło
   **kons** → Enter → **Reboot**.
4. Przy starcie od razu **F1** → **Security → Secure Boot → Secure Boot: Enabled** → **F10** → *Yes*.
5. K1 powinien wystartować normalnie. Instalator → Secure Boot… pokaże „włączony, klucz K1: wpisany”.
Gdyby K1 nie wystartował, na ekranie będzie krótkie wyjaśnienie — wtedy F1 → Secure Boot: Disabled i
napisz, co było na ekranie.


Napisz (Issues → „Feedback z testu” albo w czacie): czy pulpit się pojawił (jeśli ekran został czarny
— po ilu sekundach i czy wcześniej było logo Lenovo/GRUB), czy obraz jest ostry (1366×768), co działa
z klawiatury, TrackPointa, touchpada i myszy USB, czy bateria pokazuje sensowny procent, czy laptop
sam się wyłączył. Wersja BIOS-u
(widać w F1 → Main) też się przyda.

## Jak sprawdzić testy bez instalowania niczego
Po każdej zmianie na `main` GitHub sam buduje system i uruchamia testy:

1. Zakładka **Actions** → workflow **„K1 — budowa i testy”** → ostatnie uruchomienie.
2. Zielony ✔ = system się zbudował i przeszedł wszystkie testy (zrzuty ekranu w artefakcie `k1-<wersja>-logi`).

## Budowanie i testy (dla programisty)
Ubuntu 24.04:
```bash
scripts/przygotuj-srodowisko.sh   # pakiety (raz)
scripts/zbuduj.sh                 # jądro + programy + obrazy + ISO → build/wynik/
scripts/testy.sh                  # wszystkie testy (QEMU, także negatywne)
scripts/uruchom-qemu.sh           # ręczne uruchomienie; log: build/qemu/serial.log
scripts/uruchom-qemu.sh --iso build/wynik/k1-$(cat VERSION).iso --uefi --vga virtio   # jak w VM
```
Szczegóły: [`docs/ARCHITEKTURA.md`](docs/ARCHITEKTURA.md), [`docs/TESTY.md`](docs/TESTY.md).

## Dokumentacja
- [`docs/SPEC.md`](docs/SPEC.md) — specyfikacja (nadrzędna), [`docs/adr/`](docs/adr/README.md) — decyzje
- [`docs/ARCHITEKTURA.md`](docs/ARCHITEKTURA.md) — jak to jest zbudowane, mapa repo
- [`docs/komponenty/`](docs/komponenty/) — opis każdego elementu systemu
- [`docs/RYZYKA.md`](docs/RYZYKA.md) — czego system jeszcze nie chroni
- [`docs/ZALEZNOSCI.md`](docs/ZALEZNOSCI.md) — z czego korzystamy i dlaczego

## Feedback
Napisz w czacie z Claude albo załóż zgłoszenie: **Issues → New issue** (szablony: *Feedback z testu*,
*Błąd*, *Pomysł*).
