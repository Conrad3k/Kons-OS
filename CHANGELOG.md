# Historia zmian Kons-OS

Format: każda wersja ma sekcje **Dodane**, **Zmienione**, **Naprawione**, **Usunięte** (tylko te, które są potrzebne).
Numeracja wersji opisana w [docs/ARCHITEKTURA.md](docs/ARCHITEKTURA.md#wersjonowanie).

## [Niewydane]

### Dodane
- Lokalne budowanie ISO (`scripts/local-build.sh`) — test zmian przed scaleniem do `main`.
- `build.sh`: mirrory na czas budowy konfigurowalne (`KONS_MIRROR`, `KONS_MIRROR_SECURITY`).

## [0.1.1] — 2026-11-02

System odchudzony do podstaw: pulpit, ustawienia, przeglądarka.

### Usunięte
- Aplikacje: VLC, Evince, Ristretto, Mousepad, menedżer archiwów, GParted, Synaptic, dodatki `xfce4-goodies`.
- Bluetooth (bluez, blueman).
- Narzędzia konsolowe: git, curl, wget, htop, fastfetch, vim-tiny, zip/unzip, strony `man` (także polskie), `task-polish`.
- Pełny zestaw czcionek Noto — zostały tylko podstawowe (`fonts-noto-core`, `fonts-noto-mono`, emoji).

### Dodane
- Zasady pracy nad repo: skill `kons-dev`, `docs/ARCHITEKTURA.md`, ten plik, szablony zgłoszeń (feedback, błąd, pomysł).
- Automatyczny tag `vX.Y.Z` po udanym buildzie na `main`; numer wersji w nazwie artefaktu ISO.

### Zostało
- Pulpit XFCE z panelem ustawień, terminal, menedżer plików Thunar (część XFCE).
- Firefox ESR po polsku ze słownikiem, sieć/Wi-Fi, dźwięk z ustawieniami głośności.
- Polski język, wygląd Kons-OS, `kons-info`.

## [0.1.0] — 2026-11-02

Pierwsza wersja, która się buduje.

### Dodane
- System live na bazie Debiana 13 (trixie), amd64, start BIOS i UEFI.
- Pulpit XFCE + LightDM, motyw Arc-Dark, ikony Papirus, tapeta Kons-OS.
- Polski język, klawiatura i strefa czasowa domyślnie.
- Firefox ESR, VLC, Evince, GParted, Synaptic, NetworkManager, PipeWire, firmware non-free.
- Polecenie `kons-info`.
- Budowanie ISO w GitHub Actions (tylko na `main`).
