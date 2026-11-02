# Kons-OS

Linux-Based, Vibe-Coded operating system.

Projektujemy system operacyjny na bazie Linuxa.

Kons-OS to dystrybucja Linuksa oparta na **Debianie 13 (trixie)** z pulpitem **XFCE**,
polskim językiem i klawiaturą ustawionymi domyślnie oraz własnym wyglądem (tapeta,
ciemny motyw Arc-Dark, ikony Papirus). Uruchamia się jako system *live* z pendrive'a albo w maszynie wirtualnej.

## Jak zdobyć plik ISO (bez instalowania niczego)

System buduje się automatycznie na serwerach GitHuba (GitHub Actions):

1. Wejdź w zakładkę **Actions** w repozytorium.
2. Wybierz workflow **„Zbuduj ISO Kons-OS”** i ostatnie udane uruchomienie (zielony ✔).
3. Na dole strony, w sekcji **Artifacts**, pobierz `kons-os-<wersja>-iso` (plik zip z ISO w środku).

ISO buduje się automatycznie po każdej zmianie systemu na `main` (ok. 20–25 minut) i jest dostępne przez 30 dni.
Build można też uruchomić ręcznie przyciskiem **Run workflow**.

## Jak uruchomić

- **VirtualBox** (najprościej): nowa maszyna → typ *Linux / Debian (64-bit)*, 4 GB RAM,
  2 rdzenie → jako dysk optyczny wybierz pobrany plik `.iso` → Start.
- **Pendrive**: nagraj ISO programem [Rufus](https://rufus.ie) (tryb DD) albo
  [balenaEtcher](https://etcher.balena.io), a potem uruchom komputer z pendrive'a.

Użytkownik systemu live: `kons`, hasło: `live` (logowanie jest automatyczne).
W terminalu wpisz `kons-info`.

## Struktura repozytorium

| Ścieżka | Co tu jest |
|---|---|
| `build.sh` | Skrypt budujący ISO (live-build) |
| `VERSION` | Numer wersji systemu |
| `config/package-lists/` | Lista programów wchodzących w skład systemu |
| `config/hooks/live/` | Skrypty uruchamiane podczas budowy (branding, język) |
| `config/includes.chroot/` | Pliki kopiowane 1:1 do systemu (np. tapeta, `kons-info`) |
| `.github/workflows/build-iso.yml` | Automatyczne budowanie ISO na GitHubie |
| `CHANGELOG.md` | Historia zmian |
| `docs/` | Dokumentacja i pomysły na rozwój |
| `.claude/skills/kons-dev/` | Proces pracy dla Claude |

## Budowanie lokalnie

Potrzebny jest Debian 13 (np. w maszynie wirtualnej) i ok. 15 GB miejsca:

```bash
sudo apt install live-build
sudo ./build.sh
```

Gotowy plik pojawi się jako `kons-os-<wersja>-amd64.hybrid.iso`.

## Feedback

Po przetestowaniu wersji napisz w czacie z Claude albo załóż zgłoszenie w zakładce **Issues → New issue**
(szablony: *Feedback z testu*, *Błąd*, *Pomysł*). Zrzuty ekranu bardzo pomagają.

## Więcej

- [docs/ARCHITEKTURA.md](docs/ARCHITEKTURA.md) — jak zbudowany jest system, konwencje, wersjonowanie
- [CHANGELOG.md](CHANGELOG.md) — co się zmieniło w każdej wersji
- [docs/jak-modyfikowac.md](docs/jak-modyfikowac.md) — jak samemu coś zmienić
