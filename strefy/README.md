# Obrazy stref (0.10.0, ADR 0042, 0043)

| Plik | Co to jest |
|---|---|
| `przegladarka.txt` | Tabela pakietów Debiana 13 obrazu strefy przeglądarki: pakiet, wersja, archiwum, SHA-256, ścieżka. Pisze ją tylko `scripts/odswiez-strefe.py`; budowa (`scripts/zbuduj-strefe.sh`) sprawdza sumę każdego pakietu. |
| `przegladarka/k1.js` | Ustawienia Firefoksa w strefie (zablokowane): polski interfejs, połączenia tylko przez przekaźnik (SOCKS5), bez aktualizacji i telemetrii. W obrazie: `/etc/firefox-esr/k1.js`. |
| `przegladarka/policies.json` | Zasady Firefoksa (to samo, czego nie da się ustawić w `k1.js`). W obrazie: `/usr/share/firefox-esr/distribution/policies.json`. |
| `klawiatura-pl.xkb` | Mapa klawiatury PL dla kompozytora strefy (`wl_keyboard.keymap`), skompilowana z `xkb-data` Debiana 13 (reguły evdev, model pc105, układ pl) przez libxkbcommon. Nowa mapa: ta sama kompilacja z nowego `xkb-data` (zmiana tylko przy zmianie układu). |
| `klucze/debian-trixie.gpg` | Klucze archiwum Debiana 13 (automatyczny, bezpieczeństwa, wydania stabilnego) — sprawdzają podpis `InRelease`. |

## Skąd klucze Debiana 13 (łańcuch zaufania, 2026-11-15)
1. Pakiet `debian-archive-keyring` 2023.4ubuntu1 z archiwum Ubuntu 24.04 (`apt-get download` — podpisy Ubuntu
   sprawdza apt) — ma klucze Debiana 12, nie 13.
2. Tymi kluczami: `InRelease` gałęzi bookworm (dobry podpis kluczy „12/bookworm”), z niego suma `Packages.xz`, z niego
   suma pakietu `debian-archive-keyring_2023.3+deb12u2_all.deb` (f699e2f8…b4b8).
3. Z tego pakietu: `debian-archive-trixie-automatic.gpg`, `-trixie-security-automatic.gpg`, `-trixie-stable.gpg`
   (sklejone w `debian-trixie.gpg`, SHA-256 dbc72305…05db). Sprawdzone: dobre podpisy `InRelease` trixie
   i trixie-security.

Nowe klucze (Debian 14): ten sam łańcuch od kluczy, którym już ufamy.
