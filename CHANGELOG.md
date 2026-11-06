# Historia zmian K1

Format: każda wersja ma sekcje **Dodane**, **Zmienione**, **Naprawione**, **Usunięte** (tylko potrzebne).
Numeracja: [docs/ARCHITEKTURA.md](docs/ARCHITEKTURA.md#wersjonowanie). Tagi wersji K1: `k1-vX.Y.Z`.

## [Niewydane]

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
