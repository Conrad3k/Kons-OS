# Historia zmian K1

Format: każda wersja ma sekcje **Dodane**, **Zmienione**, **Naprawione**, **Usunięte** (tylko potrzebne).
Numeracja: [docs/ARCHITEKTURA.md](docs/ARCHITEKTURA.md#wersjonowanie). Tagi wersji K1: `k1-vX.Y.Z`.

## [0.5.4] — 2026-11-07

### Naprawione
- **Zacinanie się całego pulpitu na prawdziwym laptopie** (ThinkPad X230, Twój pierwszy test):
  kursor, zmiana tapety i cały system działały skokowo. Dwie przyczyny: (1) w jądrze brakowało obsługi
  typów pamięci procesora (MTRR/PAT), więc każdy piksel szedł do karty bez buforowania; (2) przy
  obrazie z firmware ekran przy każdej klatce przepisywał cały ekran (ok. 4 MB). Teraz przy obrazie
  z firmware jest jeden bufor, a do karty trafiają tylko zmienione miejsca (np. sam kursor).
  W QEMU tego nie było widać — emulator nie odwzorowuje typów pamięci.

## [0.5.3] — 2026-11-07

### Dodane
- **Wskaźnik baterii** na górnym pasku (Twój pomysł): na laptopie przed ikoną dźwięku procent
  naładowania i ikona baterii, błyskawica przy ładowaniu, przy niskim poziomie (≤ 15 %) wypełnienie
  w kolorze akcentu. Na komputerze bez baterii (i w maszynie wirtualnej) nic się nie pokazuje.
  Stan sprawdzany co 15 s (ADR 0014).
- Jądro: sterownik Embedded Controller (przez niego laptopy, m.in. ThinkPad X230, podają stan
  baterii), baterii i zasilacza ACPI.
- Testy: sztuczna bateria laptopa w QEMU (tablica ACPI) — test w systemie, log powłoki i zrzut
  ekranu z ikoną; test na hoście odczytu stanu baterii (także z błędnymi danymi).

## [0.5.2] — 2026-11-07

Etap **M5a — dyski i USB** (pierwszy krok M5; ADR 0013). Pierwszy laptop: ThinkPad X230.

### Dodane
- Jądro rozpoznaje **dyski SATA** (kontroler AHCI — m.in. ThinkPad X230, VirtualBox) i **NVMe**,
  tablice partycji **GPT** (partycja danych K1 będzie szukana po nazwie „K1-DANE”).
- **USB**: kontrolery USB 3 (xHCI), USB 2 (EHCI) i USB 1.1 (OHCI); **klawiatury i myszy USB**
  działają w pulpicie, **pamięci USB** (pendrive'y) są widoczne. Tylko standardowe urządzenia —
  bez sterowników producentów i bez surowego dostępu do urządzeń.
- README: jak spróbować startu z pendrive'a na ThinkPadzie X230.

### Zmienione
- Odłączona mysz lub klawiatura USB jest „zapominana”, więc ponowne podłączenie (także pod tym samym
  numerem urządzenia) działa.
- Stare tablice partycji MBR nie są czytane przez jądro (mniej kodu wystawionego na obce nośniki).
- Testy: test w systemie z dyskami SATA/NVMe, pendrive'em i USB na trzech kontrolerach (dostęp do
  dysków tylko dla roota, nikt ich nie otwiera); nowy `scripts/test-m5.sh` — maszyna bez PS/2,
  tylko klawiatura i mysz USB, podłączanie i odłączanie myszy w trakcie pracy.

## [0.5.1] — 2026-11-07

### Naprawione
- Mysz mogła nie działać po bardzo szybkim starcie (np. pod KVM): nadzorca szukał urządzeń wejścia,
  zanim jądro zgłosiło mysz, i przestawał szukać, gdy znalazł klawiaturę. Teraz co 2 s sprawdza,
  czy doszło nowe urządzenie, i uruchamia usługę wejścia ponownie z pełnym zestawem. To też pierwszy
  krok do podłączania urządzeń w trakcie pracy (M5).

### Zmienione
- CI: logi testów zawsze dostępne, niezależnie od limitu miejsca na artefakty — w konsoli zadania,
  w podsumowaniu (lista OK/BŁĄD) i w wydaniu (`k1-<wersja>-logi.tar.gz`); artefakty 7 dni, obraz jako
  artefakt tylko z PR-ów.
- Testy pulpitu i wejścia czekają, aż mysz zostanie otwarta; test pulpitu sprawdza położenie kursora
  z logu ekranu po każdym ruchu; sprawdzanie wykresu Zasobów liczy każdą kolumnę.
- Dokumentacja: wstępne rozpoznanie etapu M5 (`docs/M5-ROZPOZNANIE.txt`).

## [0.5.0] — 2026-11-06

Etap **M4.1 — porządki i audyt** (ADR 0011, ADR 0012).

### Dodane
- Aplikacja **Zasoby**: procesor i pamięć całego systemu (liczby i wykresy z ostatniej minuty) oraz
  zużycie każdej usługi i aplikacji. Dane podaje nadzorca (statystyki cgroup) przez kanał, do którego
  aplikacja nie może pisać.
- **Ustawienia → Ekran**: wybór rozdzielczości z listy, którą podaje karta graficzna; zmiana od razu,
  pulpit układa się od nowa.
- Sterownik karty graficznej **VMware SVGA** (VirtualBox „VMSVGA”, VMware) — w maszynach wirtualnych
  więcej rozdzielczości niż 1024×768 z firmware.
- Ikony: Zasoby (puls), Ekran (monitor).

### Naprawione
- Pod dolnym paskiem i pod menu dźwięku „wyjeżdżała” jaśniejsza podstawa: cień panelu miał w środku
  dziurę przesuniętą 4 px w dół. Cień jest teraz liczony tylko poza panelem.
- Bezpieczeństwo (przegląd M4.1): nowe okno innej aplikacji nie przejmuje już klawiatury (ochrona
  przed „kradzieżą fokusu”); limit pamięci okien jednego klienta; ekran przycina obszar odświeżania
  do własnego okna klienta; log usług oczyszczany także ze znaków sterujących C1 i błędnego UTF-8;
  klatka zamyka wszystkie zbędne deskryptory (`close_range`); katalog notatek bez podążania za
  dowiązaniami.

### Zmienione
- Kod: kompozytor podzielony na `main.c`, `scena.c`, `okna.c`, `wskaznik.c`, `drm.c`; program
  testowy na pliki tematyczne (`testy/system/`).
- Testy: zmiana rozdzielczości i Zasoby w `test-m4.sh`; w systemie — ochrona fokusu, limit pamięci
  okien, klatka i próba aplikacji Zasoby; na hoście — `test_log`, nowe komunikaty w `test_komunikat`.


## [0.4.2] — 2026-11-06

### Zmienione
- Nowy znak K1 **„Warstwy”** (wybór właściciela z czterech kierunków): trzy arkusze papieru
  w odcieniach terakoty, jak na tapecie — w przycisku Start i w menu. Tarcza z blankami usunięta.
- README: w VirtualBoxie mysz i klawiatura działają od razu (test właściciela, VirtualBox z EFI);
  obejście dla niskiej rozdzielczości w trybie EFI.

## [0.4.1] — 2026-11-06

### Naprawione
- CI: instalacja narzędzi zawieszała się na serwerze GitHuba (ustawienie „bez pytań” dla apt
  ginęło przy `sudo`); teraz jest przekazywane wprost, instalacja pokazuje postęp w logu i ma limit
  10 minut. Bez zmian w samym systemie — obraz ISO jak w 0.4.0.

## [0.4.0] — 2026-11-06

Etap **M4 — GUI**: pulpit, okna i pierwsze aplikacje; pierwszy obraz ISO do maszyny wirtualnej.

### Dodane
- Pulpit (ustalenia właściciela, `docs/WYGLAD.md`): górny pasek — nazwa aktywnej aplikacji, dźwięk,
  data i godzina (czas polski, letni/zimowy sam); dolny pasek wyśrodkowany jak w Windows 11 — Start
  i aplikacje ze wskaźnikami; menu Start z „Uruchom ponownie” i „Wyłącz”; klawisz Super.
- Okna: zaokrąglone, z cieniem, przeciąganie za pasek tytułu, minimalizacja (też kliknięciem na
  pasku), zamykanie, fokus po kliknięciu.
- Aplikacje: **Zegar** (tarcza analogowa, data po polsku), **Notatki** (polskie znaki przez prawy Alt,
  zapis automatyczny), **Ustawienia** (motyw jasny/ciemny, tapeta, prędkość myszy, sekundy na
  zegarze, informacje; hasło — opis na M5).
- Nowe tapety na wzór zdjęcia właściciela (warstwy wygiętego papieru), 4 warianty: Glina (domyślna,
  kolory systemu), Zmierzch, Noc, Świt; nowy, płaski znak K1 (tarcza z blankami).
- Czcionki IBM Plex Sans i Serif (bitmapy liczone przy budowie, ADR 0009).
- **Obraz ISO** (`k1-<wersja>.iso`, ~16 MB) do VirtualBoxa/VMware/QEMU — start przez UEFI albo BIOS,
  grafika z firmware (simpledrm); CI publikuje go jako wydanie (Releases).
- Bezpieczeństwo (ADR 0008): ekran jako kompozytor sprawdza każdy komunikat okien i nadawcę, przyjmuje
  tylko zapieczętowaną pamięć współdzieloną, egzekwuje role (pulpit, Ustawienia); pulpit i każda
  aplikacja we własnej klatce (Notatki widzą tylko swój katalog, pulpit nie widzi treści okien).
- Testy: `scripts/test-m4.sh` (klikanie, pisanie po polsku, przeciąganie, motyw, tapeta, wyłączenie,
  start z ISO przez UEFI i BIOS — zrzuty sprawdzane automatycznie); w systemie: klatki i próby dla
  pulpitu i aplikacji, złośliwe okna, restart pulpitu (88 sprawdzeń); testy jednostkowe przybornika.

### Zmienione
- Jądro: simpledrm (grafika z firmware), loop (rdzeń wbudowany w ISO), zegar sprzętowy CMOS.
- `ekran` przepisany na kompozytor okien (`src/ekran/main.c` + `drm.c`); tapeta bez dużego logo.
- `/dane_uzytkownika` z prawami `0711` (aplikacja wchodzi tylko do swojego podkatalogu).
- Rozpoczęcie: `scripts/przygotuj-srodowisko.sh` instaluje też czcionki, FreeType i GRUB.

## [0.3.0] — 2026-11-06

Etap **M3 — Trusted Core**: każda usługa systemu działa w osobnej „klatce” i wraca po awarii.

### Dodane
- Nadzorca w pełnej wersji (ADR 0007): każda usługa ma własnego użytkownika, zero uprawnień
  administratora, blokadę podnoszenia uprawnień, białą listę wywołań jądra (seccomp), dostęp do plików
  tylko do własnego programu (Landlock) i limity pamięci oraz procesów (cgroups v2).
- Urządzenia (karta graficzna, klawiatura, mysz) otwiera nadzorca i przekazuje usługom — usługi nie
  widzą żadnych plików ani urządzeń poza przekazanymi.
- Kanały między usługami przez gniazda UNIX z kontrolą nadawcy: ekran odrzuca komunikaty od kogoś
  innego niż przypisana usługa. Nowy wspólny protokół komunikatów (32 bajty, testy + fuzzing).
- Logi usług przechodzą przez nadzorcę: bez znaków sterujących, z limitem, usługa nie podszyje się
  pod inną; usługi nie mają już dostępu do konsoli.
- Restart usług po awarii (ekran wraca razem z klientami), usługa padająca w kółko zostaje wyłączona.
- Testy: sprawdzenie klatek z zewnątrz, „próby” w klatkach (22 zabronione działania), obcy nadawca,
  restart ekranu, limit restartów; testy jednostkowe protokołu i kanałów, fuzzing protokołu.
- `docs/DO_ZROBIENIA.txt` — lista tego, co zostało do zrobienia, i decyzji potrzebnych od właściciela.
- CI: workflow „Sprzątanie artefaktów” (usuwa stare obrazy Kons-OS, które zajmowały miejsce).

### Zmienione
- Jądro: gniazda UNIX (`CONFIG_UNIX`) — tylko do kanałów między usługami.
- Skill `kons-dev` opisuje architekturę K1, niezmienniki bezpieczeństwa i przepisy na typowe zmiany.
- Decyzja: na razie bez `fortify-headers` (ADR 0007).

## [0.2.0] — 2026-11-06

Etap **M2 — Ekran**: K1 pokazuje obraz i reaguje na mysz i klawiaturę.

### Dodane
- Nowa tapeta i logo K1 (ustalenia właściciela, `docs/WYGLAD.md`): ciemne, ciepłe tło z łukiem
  „horyzontu”, logo — tarcza z blankami twierdzy (bezpieczeństwo, niezależność) z monogramem K1.
  Styl nawiązuje do Claude Code (terakota, krem, ciemny brąz).
- `ekran`: jedyny proces z dostępem do karty graficznej (DRM/KMS bez libdrm), własny renderer
  programowy z wygładzaniem krawędzi, podwójne buforowanie, kursor myszy.
- `wejscie`: klawiatura i mysz (PS/2, evdev); kody klawiszy nigdy nie trafiają do logu.
- Protokół zdarzeń wejście → ekran (16 bajtów, wersja, typ, długość, sprawdzanie każdego pola).
- `nadzorca` w wersji minimalnej: uruchamia `wejscie` i `ekran` i łączy je kanałem (pełny — M3).
- Testy: `scripts/test-m2.sh` (mysz i klawiatura przez monitor QEMU, zrzuty ekranu sprawdzane
  automatycznie), `scripts/testy.sh` (wszystko), nowe testy negatywne: przejęcie ekranu przez inny
  proces, złośliwe komunikaty do ekranu, awaria procesu ekran; testy jednostkowe i fuzzing protokołu.
- Jądro: DRM + sterownik `bochs` (QEMU), evdev, i8042, klawiatura AT, mysz PS/2 — bez fbdev i fbcon.

### Zmienione
- CI: tag wersji zakładany zaraz po udanych testach; brak miejsca na artefakty GitHuba nie blokuje już wersji.
- Program testowy w systemie: `/rdzen/system/test-systemu` (uruchamia go nadzorca).

## [0.1.0] — 2026-11-06

Pierwsza wersja Kons K1 Sovereign: etapy **M0** (porządki) i **M1** (start systemu).
Na razie bez ekranu — system startuje w maszynie wirtualnej i zgłasza się na porcie szeregowym.

### Dodane
- Jądro Linux 6.18.60 (LTS) z własną, minimalną konfiguracją: bez modułów, bez swapu i hibernacji,
  bez terminali, lockdown + Landlock + Yama, wzmocnienia według KSPP, sprawdzanie sumy i podpisu źródeł.
- `k1-initramfs`: znajduje obraz systemu, montuje go tylko do odczytu jako `/rdzen` i przekazuje
  sterowanie dalej; przy każdym błędzie czytelny komunikat i bezpieczne wyłączenie maszyny.
- `/rdzen/init` (PID 1): montowania pod `/rdzen/jadro`, 13 ustawień bezpieczeństwa jądra,
  sprzątanie osieroconych procesów, kontrolowane wyłączanie. W korzeniu systemu są tylko
  `/rdzen`, `/aplikacje`, `/dane_uzytkownika`.
- Testy: 33 sprawdzenia wewnątrz systemu (w tym negatywne: nie da się uruchomić programu z danych
  ani z pamięci, nie ma terminali, usunięte niebezpieczne wywołania jądra), 6 scenariuszy błędów
  startu, testy jednostkowe z sanitizerami i fuzzing parsera.
- Skrypty: budowa (powtarzalna), uruchamianie w QEMU z logiem, zrzut ekranu, testy.
- CI: GitHub Actions buduje i testuje `main`, publikuje obrazy i logi jako artefakt, zakłada tag `k1-v0.1.0`.
- Dokumentacja: specyfikacja, ADR 0001–0005, architektura, komponenty, testy, zależności, ryzyka.

### Usunięte
- Kons-OS na bazie Debiana (live-build, XFCE) — przeniesiony do gałęzi `archiwum/debian-0.1.2`.

---

# Archiwum: Kons-OS (Debian, live-build)

Historia poprzedniego systemu; kod w gałęzi `archiwum/debian-0.1.2`, tagi `v0.1.0`–`v0.1.2`.

## [Kons-OS 0.1.2] — 2026-11-02
- Naprawiona tapeta XFCE; firmware wybierane ręcznie (ISO ~1,3 GB); lokalne budowanie ISO.

## [Kons-OS 0.1.1] — 2026-11-02
- System odchudzony do pulpitu XFCE, ustawień i Firefoksa; szablony zgłoszeń; tagi wersji.

## [Kons-OS 0.1.0] — 2026-11-02
- Pierwsza wersja: Debian 13 live, XFCE, polski język, budowanie ISO w GitHub Actions.
