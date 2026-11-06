# Historia zmian K1

Format: każda wersja ma sekcje **Dodane**, **Zmienione**, **Naprawione**, **Usunięte** (tylko potrzebne).
Numeracja: [docs/ARCHITEKTURA.md](docs/ARCHITEKTURA.md#wersjonowanie). Tagi wersji K1: `k1-vX.Y.Z`.

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
