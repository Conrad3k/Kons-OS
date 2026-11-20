# Kons K1 Sovereign

Własny, minimalistyczny i bezpieczny system operacyjny na jądrze Linux. Nie jest dystrybucją
Linuksa: ma własny program startowy, własny ekran i pulpit, własny zaszyfrowany magazyn danych
(skarbiec) i ścisłą izolację aplikacji. Wszystko, co nasze, jest napisane w C. Z Linuksa bierzemy tylko
jądro — nie ma powłoki, terminala, pakietów ani bibliotek współdzielonych; każda usługa i aplikacja działa
w osobnej „klatce”, a system sprawdza przy starcie, czy nikt go nie podmienił.

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
| M5 Dane i sprzęt | zaszyfrowane dane, hasło, dyski i USB, pendrive, **instalator na dysk** | PRZETESTOWANE w QEMU (0.5.9) |
| 0.6.0 Fundament | porządki w nadzorcy, zdarzenia jądra zamiast odpytywania, szybsze testy, historia baterii | PRZETESTOWANE |
| M6 Bezpieczne aktualizacje | podpisane wydania, wykrywanie podmiany systemu, A/B, Secure Boot z własnym kluczem | PRZETESTOWANE w QEMU (0.7.0) |
| 0.7.5 Pulpit | ikony na pulpicie jak w Windows (w stylu K1), menu pod prawym przyciskiem, przypinanie i odpinanie na pasku | PRZETESTOWANE w QEMU (ADR 0028) |
| 0.8.0 Pliki | aplikacja Pliki, Kosz (30 dni albo ręcznie), okno wyboru pliku, zdjęcia z pendrive'a, miniatury zdjęć, tapeta z własnego zdjęcia, cały pendrive na dane | PRZETESTOWANE w QEMU (ADR 0029–0033) |
| 0.8.1 Pliki — dopracowanie | dane nie giną (naprawa przy starcie, zły zegar), 16 384 pliki, szybki start, miejsce w Informacjach, stary pendrive powiększany przyciskiem, pliki do 4 GB | PRZETESTOWANE w QEMU (ADR 0033, 0034) |
| 0.8.2 Pliki — dopracowanie, część 2 | skarbiec na duże dane (szybki start, prawie cały pendrive na pliki), pendrive z telefonu, duże zdjęcia, aplikacja Podgląd, „Otwórz za pomocą”, przeciąganie | PRZETESTOWANE w QEMU (ADR 0036, 0037) |
| 0.9.0 Sieć — kabel | adres od routera, nic w K1 nie odpowiada z zewnątrz, ikona sieci, Ustawienia → Sieć, zegar z internetu | PRZETESTOWANE w QEMU (ADR 0038, 0039) |
| 0.9.1 Sieć — Wi-Fi | Wi-Fi na laptopach (sterowniki z jądra, pliki od producentów kart), lista sieci, łączenie z hasłem i z siecią otwartą, zapamiętane sieci | PRZETESTOWANE w QEMU z udawanymi kartami (ADR 0041) i na laptopach właściciela (2026-11-14; IdeaPad: Qualcomm) |
| 0.10.0 Strefy — przeglądarka | Firefox w osobnej mikro-maszynie (strefie) z własnym jądrem, zaufana ramka w kolorze sesji, sesje Ogólna / Bank / Prywatna, które pamiętają albo zapominają, przewijanie kółkiem i touchpadem | PRZETESTOWANE w QEMU z KVM (ADR 0042, 0043); na sprzęcie — lista w `docs/DO_ZROBIENIA.txt` |
| 0.10.1 Strefy — balon | pamięć sesji według potrzeby (wolna wraca do K1), „Mało pamięci” i sesje w Zasobach, okienko „Którą sesję otworzyć?”, przeglądarka nie wysypuje się przy kartach, przewijanie po nowej karcie | PRZETESTOWANE w QEMU z KVM (ADR 0044); palce w Firefoksie — na stanowisku na hoście |
| 0.10.2 Strefy — testy | „Uruchamiam…” aż do okna przeglądarki, okno znika od razu po zamknięciu (sesja zapisuje się w tle, także przy wyłączaniu), testy klikają po nazwach (opis ekranu), zrzuty wzorcowe okien K1 | PRZETESTOWANE w QEMU (ADR 0045); przeglądarka — na sprzęcie (lista w `docs/DO_ZROBIENIA.txt`) |
| 0.10.5 Aktualizacje przez sieć | Ustawienia → Aktualizacje, K1 na dysku sam pobiera nową wersję z wydań na GitHubie i pyta przed instalacją, robot sam wydaje poprawki jądra i przeglądarki, zmiana rozmiaru i maksymalizacja okna przeglądarki, Alt+Tab, Przeglądarka na pasku i pulpicie, jądro Linux 7.3 | PRZETESTOWANE w QEMU (ADR 0046, 0047); na sprzęcie — lista w `docs/DO_ZROBIENIA.txt` |
| 0.10.7 Wygląd | krótsze napisy bez półpauz, Ustawienia i Pliki ułożone porządniej, okno przeglądarki bez kolorowej obwódki, napisy pod ikonami pulpitu na pigułce, tapeta z matową krawędzią, ekran „Wyłączanie”, Ustawienia → Aktualizacje już się nie zamykają | PRZETESTOWANE w QEMU (ADR 0048); wyłączanie z przeglądarką — na sprzęcie (lista w `docs/DO_ZROBIENIA.txt`) |
| 0.10.9 Szybka grafika | przełącznik „Szybka grafika” przy sesji: przeglądarka rysuje kartą przez pośrednika grafiki K1 (IdeaPad; X230 — wyciszony), sterownik karty Intela w jądrze, obraz przeglądarki poza pamięcią K1, większe miejsca na system | PRZETESTOWANE w QEMU z KVM (ADR 0049, 0050; silnik testowy zamiast karty); Firefox z kartą — na sprzęcie (lista w `docs/DO_ZROBIENIA.txt`) |
| 0.10 Strefy (dalej) | może: filmy rozpakowywane kartą graficzną; inne obce aplikacje, wspólna strefa | PLANOWANE (ADR 0020, 0024, 0025) |
| 0.11 Pliki ze stref | przenoszenie plików ze stref i do stref przez okno K1, schowek między strefami | PLANOWANE |
| 0.12–0.13 | tryb ratunkowy i TPM, audyt końcowy → 1.0 | PLANOWANE |

Od 0.9 etapy to numery wersji (ADR 0035); wcześniejsze etapy M0–M6 zostały w tabeli jako historia.
Plan — co teraz, co dalej i czego potrzeba od właściciela: [`docs/DO_ZROBIENIA.txt`](docs/DO_ZROBIENIA.txt). Zasady:
[`docs/SPEC.md`](docs/SPEC.md). Co się zmieniło: [`CHANGELOG.md`](CHANGELOG.md).

**Co już jest (0.10.9):** K1 działa w maszynie wirtualnej i z pendrive'a na laptopach (ThinkPad X230, Lenovo IdeaPad
110), a Instalator zapisuje go na dysk. Pulpit jak w Windows 11, w stylu K1: ikony na pulpicie (aplikacje, Kosz, Twoje
pliki i foldery z folderu Pulpit), pasek na dole z menu Start i przypiętymi aplikacjami (Zegar, Notatki, Ustawienia, Zasoby,
Kalkulator, Pliki, Przeglądarka; w menu Start też Instalator i Podgląd), pasek na górze (sieć, dźwięk, bateria, data
i godzina), okna, Alt+Tab, menu pod prawym przyciskiem i polska klawiatura. **Pliki**: foldery jak w Windows (Pulpit,
Dokumenty, Obrazy…), Kosz, przeciąganie i zaznaczanie wielu, „Otwórz za pomocą”, zdjęcia z pendrive'a z aparatu albo
telefonu (FAT; tylko odczyt i import), **Podgląd** zdjęć (PNG, JPEG, GIF, BMP, WebP, do 100 Mpx), tapeta z własnego
zdjęcia. Zna dyski SATA/NVMe oraz klawiatury, myszy, touchpady i pamięci USB. Notatki, pliki, ustawienia i hasło są
zaszyfrowane na partycji K1-DANE (pendrive albo dysk po instalacji). **Sieć** kablem i Wi-Fi: adres od routera, zegar
z internetu, lista sieci, hasło wpisane raz; z zewnątrz nic w K1 nie odpowiada. **Przeglądarka**: Firefox w osobnej
mikro-maszynie (strefie) w zaufanej ramce K1, sesje Ogólna / Bank / Prywatna, okno o dowolnym rozmiarze, **szybka
grafika** (strony rysuje karta, każdą prośbę strefy sprawdza K1; IdeaPad — na X230 karta nie oddziela pracy stref).
**Aktualizacje**: K1 na dysku sam sprawdza wydania na GitHubie, pobiera nową wersję i pyta przed instalacją (Ustawienia →
Aktualizacje). Czego jeszcze nie ma: innych obcych aplikacji, dźwięku, jasności ekranu i klawiszy Fn. Wygląd:
[`docs/WYGLAD.md`](docs/WYGLAD.md).

## Uruchom w maszynie wirtualnej
1. Pobierz obraz: **Releases** → najnowsze `k1-vX.Y.Z` → plik `k1-X.Y.Z.iso` (ok. 27 MB; od 0.10.9 bez przeglądarki — K1 nie ma
   napędu CD; przeglądarka jest na pendrive'ie i dysku).
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
   - Przeglądarki z ISO nie ma (od 0.10.9 jej obraz leży obok systemu na pendrive'ie albo dysku, a K1 nie czyta płyt) —
     K1 pokaże okienko, że przeglądarka nie może ruszyć.
3. Uruchom. Po kilku sekundach pojawi się pulpit. Kliknij w okno maszyny, żeby „złapać” mysz
   (prawy Ctrl ją uwalnia). Polskie znaki: prawy Alt + litera (np. prawy Alt + a = ą).
4. Wyłączenie: znak K1 na dolnym pasku → **Wyłącz**.

Działa też w VMware (UEFI lub BIOS) i QEMU/virt-manager (karta `std` lub `virtio`; nie `vmware` —
sterownik VMware nie działa z tą kartą QEMU); Hyper-V — jeszcze nie.

## Uruchom na laptopie (ThinkPad X230, IdeaPad 110) — z pendrive'a, z zapisem danych
Od 0.5.6 jest osobny obraz na pendrive: **`k1-X.Y.Z-usb.img.xz`** (Releases). Ma miejsce na Twoje dane
(ustawienia, notatki) — **zaszyfrowane, na tym samym pendrivie**. Bez Twojej decyzji K1 **niczego nie
zapisuje na dysku laptopa** — zmienia go tylko Instalator (punkt 7). Pendrive zostanie w całości nadpisany.
1. Nagraj `k1-X.Y.Z-usb.img.xz` na pendrive (≥ 2 GB): w Windowsie **Rufus** → wybierz plik
   (Rufus sam rozpakuje .xz; jeśli zapyta — tryb obrazu DD) → Start. Albo balenaEtcher.
   (Obraz `.iso` też zadziała z pendrive'a, ale bez zapisu danych — po wyłączeniu wszystko znika,
   a Ustawienia → Hasło piszą „Nie znaleziono miejsca na dane”. Sprawdzisz w Ustawieniach → Informacje:
   „Dane: zaszyfrowane na nośniku K1”.)
   Nowy pendrive od pierwszego startu ma na dane całą swoją pojemność. Pendrive nagrany wersją 0.5.6–0.7.6 ma mało
   miejsca na pliki — Ustawienia → Miejsce → **„Użyj całego pendrive'a na dane”** (Twoje pliki zostają na miejscu; nie
   wyjmuj pendrive'a, dopóki nie zobaczysz „Gotowe”). Tam też widać, ile miejsca jest zajęte i ile zostało.
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
3. Po kilku sekundach powinien pojawić się pulpit. Klawiatura, TrackPoint, touchpad (ruch, kliknięcia i — od
   0.10.0 — przewijanie dwoma palcami) i mysz USB z kółkiem powinny działać. Na górnym pasku przy głośniku — bateria
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
   konto). **Ustaw hasło** (punkt 6) — bez niego złodziej laptopa może odczytać dane. Dysk: co najmniej 4 GB (K1 zajmuje
   3 GB na system i jego aktualizacje, reszta to dane).
   **K1 zainstalowany wersją 0.10.7 albo starszą** (mniejsze miejsce na system) nie zaktualizuje się do nowszych wersji — ani przez
   sieć, ani z pendrive'a (Ustawienia → Aktualizacje pokażą błąd). Zainstaluj go od nowa z pendrive'a
   (Instalator) — dane z tego dysku przepadną.
   **Aktualizacja przez sieć (od 0.10.5):** K1 na dysku sam sprawdza (po połączeniu z internetem i co 6 godzin), pobiera
   nową wersję i pyta okienkiem „Jest nowa wersja K1” — instaluje dopiero po *Zainstaluj*. Wszystko w **Ustawieniach →
   Aktualizacje**: *Sprawdź teraz*, *Sprawdzaj sam*, *Wróć do poprzedniej wersji*. K1 uruchomiony z pendrive'a powie tylko,
   że jest nowsza wersja — nagraj nowy pendrive.
   Nowe wersje wydajemy my (nowe funkcje) albo robot na GitHubie (poprawki bezpieczeństwa jądra i przeglądarki, z opisem
   „Ta aktualizacja zawiera istotne poprawki dotyczące bezpieczeństwa i jakości.”) — każda przechodzi pełne testy
   i jest podpisana kluczem K1; K1 przyjmie tylko taką.
   **Aktualizacja z pendrive'a** (K1 na dysku bez internetu): nagraj nowszy `k1-…-usb.img.xz` na pendrive, **uruchom K1
   z dysku** (bez F12), podłącz pendrive, **Ustawienia → Aktualizacje** → *Zaktualizuj K1 na dysku do …*. Wymienia tylko
   system — Twoje dane zostają.
   K1 sprawdza odcisk i podpis systemu i nie pozwoli wrócić do starszej wersji (dlatego lepiej startować z dysku: wtedy
   sprawdza Twój system, a nie kod z pendrive'a).
   Nowa wersja trafia **obok** obecnej (A/B): po restarcie startuje na próbę, a gdyby nie wystartowała — wraca
   poprzednia, a K1 napisze o tym w okienku na pulpicie i w Ustawieniach → Aktualizacje. Aktualizacja wymienia też pliki
   startowe (shim, GRUB) — bezpiecznie, nawet gdy w trakcie zabraknie prądu. K1 starszy niż 0.7.0 na dysku:
   najpierw zaktualizuj go do 0.8.x albo zainstaluj od nowa. **Od 0.10.0** dane zapisane przez 0.5.6–0.8.1, których od
   tamtej pory nie otworzyła żadna wersja 0.8.2–0.9.1, się nie otworzą (K1 ich nie rusza) — uruchom taki nośnik najpierw
   raz z 0.9.1.
8. **Przeglądarka** (od 0.10.0): menu Start → **Przeglądarka** — prawdziwy Firefox w osobnej maszynie (strefie).
   **Najpierw raz włącz wirtualizację w BIOS-ie:** X230 — F1 → Security → Virtualization → „Intel Virtualization
   Technology” i „Intel VT-d Feature”: *Enabled*; IdeaPad 110 — F2 → Configuration → „Intel Virtual Technology”:
   *Enabled*; F10 zapisuje. Bez tego K1 powie, że przeglądarka nie może ruszyć. Sesje (od 0.10.1 okienko „Którą sesję
   otworzyć?” po kliknięciu Przeglądarki): **Ogólna** i **Bank** pamiętają zalogowania, zakładki i historię (zaszyfrowane
   w danych K1), **Prywatna** zapomina wszystko po zamknięciu. Ustawienia → **Przeglądarka**: pamięta / zapomina, zmiana
   nazwy, „Wyczyść dane…”. **Pamięć (od 0.10.1):** sesja bierze tyle pamięci, ile w tej chwili potrzebuje (najwyżej pamięć
   komputera minus 768 MB na 4 GB albo 1 GB na 8 GB, które K1 zostawia sobie), a nieużywaną oddaje K1 sama po kilku
   sekundach; na 4 GB zmieszczą się dwie lekkie sesje naraz. Gdy pamięci zaczyna brakować, K1 najpierw zabiera ją
   sesjom w tle, potem pokazuje okienko „Mało pamięci” z sesjami i „Zamknij”; ile używa każda sesja — w Zasobach.

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
   **Secure Boot…** → **Przygotuj klucz K1**
   → **Uruchom ponownie** (bez pendrive'a).
2. Pojawi się niebieski ekran *Shim UEFI key management* — **w ciągu 10 sekund naciśnij dowolny klawisz**
   (jeśli nie zdążysz, K1 wystartuje normalnie — wtedy powtórz punkt 1; gdy klucz dalej czeka, okno pokaże od razu
   **Uruchom ponownie**).
3. Strzałkami: **Enroll MOK** → Enter → **Continue** → Enter → **Yes** → Enter → wpisz hasło
   **kons** → Enter → **Reboot**.
4. Przy starcie od razu **F1** → **Security → Secure Boot → Secure Boot: Enabled** → **F10** → *Yes*.
5. K1 powinien wystartować normalnie. Instalator → Secure Boot… pokaże „włączony, klucz K1: wpisany”.
Gdyby K1 nie wystartował, na ekranie będzie krótkie wyjaśnienie — wtedy F1 → Secure Boot: Disabled i
napisz, co było na ekranie.


Napisz (Issues → „Feedback z testu” albo w czacie): czy pulpit się pojawił (jeśli ekran został czarny
— po ilu sekundach i czy wcześniej było logo Lenovo/GRUB), czy obraz jest ostry (1366×768), co działa
z klawiatury, TrackPointa, touchpada i myszy USB, czy bateria pokazuje sensowny procent, czy laptop
sam się wyłączył. Co sprawdzić w nowej wersji: lista w [`docs/DO_ZROBIENIA.txt`](docs/DO_ZROBIENIA.txt), punkt 4. Wersja BIOS-u
(widać w F1 → Main) też się przyda.

## Jak sprawdzić testy bez instalowania niczego
Każda zmiana w systemie przechodzi pełne testy na GitHubie, zanim trafi do `main`:

1. Zakładka **Pull requests** → zamknięty PR → zakładka **Checks** (albo **Actions** → „K1 — budowa i testy” → przebieg
   z gałęzi PR-a).
2. Zielony ✔ = system się zbudował i przeszedł wszystkie testy (logi i zrzuty ekranu w artefakcie `k1-<wersja>-logi`).
   Na `main` CI już tylko buduje i tworzy wydanie.

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
- [`docs/DO_ZROBIENIA.txt`](docs/DO_ZROBIENIA.txt) — plan: co teraz, co dalej, Twoje decyzje i testy na laptopie
- [`docs/SPEC.md`](docs/SPEC.md) — specyfikacja (nadrzędna), [`docs/adr/`](docs/adr/README.md) — decyzje
- [`docs/ARCHITEKTURA.md`](docs/ARCHITEKTURA.md) — jak to jest zbudowane, mapa repo
- [`docs/komponenty/`](docs/komponenty/) — opis każdego elementu systemu
- [`docs/SLOWNIK.md`](docs/SLOWNIK.md) — pojęcia K1 (jedno słowo = jedno znaczenie)
- [`docs/WYGLAD.md`](docs/WYGLAD.md) — ustalenia wyglądu, [`docs/TESTY.md`](docs/TESTY.md) — testy
- [`docs/RYZYKA.md`](docs/RYZYKA.md) — czego system jeszcze nie chroni
- [`docs/ZALEZNOSCI.md`](docs/ZALEZNOSCI.md) — z czego korzystamy i dlaczego
- [`CHANGELOG.md`](CHANGELOG.md) — co się zmieniło w każdej wersji

## Feedback
Napisz w czacie z Claude albo załóż zgłoszenie: **Issues → New issue** (szablony: *Feedback z testu*,
*Błąd*, *Pomysł*).
