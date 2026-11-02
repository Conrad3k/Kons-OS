# Historia zmian Kons-OS

Format: każda wersja ma sekcje **Dodane**, **Zmienione**, **Naprawione**, **Usunięte** (tylko te, które są potrzebne).
Numeracja wersji opisana w [docs/ARCHITEKTURA.md](docs/ARCHITEKTURA.md#wersjonowanie).

## [Niewydane]

### Dodane
- Zasady pracy nad repo: skill `kons-dev`, `docs/ARCHITEKTURA.md`, ten plik, szablony zgłoszeń (feedback, błąd, pomysł).
- Automatyczny tag `vX.Y.Z` po udanym buildzie na `main`; numer wersji w nazwie artefaktu ISO.

## [0.1.0] — 2026-11-02

Pierwsza wersja, która się buduje.

### Dodane
- System live na bazie Debiana 13 (trixie), amd64, start BIOS i UEFI.
- Pulpit XFCE + LightDM, motyw Arc-Dark, ikony Papirus, tapeta Kons-OS.
- Polski język, klawiatura i strefa czasowa domyślnie.
- Firefox ESR, VLC, Evince, GParted, Synaptic, NetworkManager, PipeWire, firmware non-free.
- Polecenie `kons-info`.
- Budowanie ISO w GitHub Actions (tylko na `main`).
