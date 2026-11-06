# Kons K1 Sovereign

Własny, minimalistyczny i bezpieczny system operacyjny na jądrze Linux. Nie jest dystrybucją
Linuksa: ma własny program startowy, (wkrótce) własny ekran i pulpit, własny magazyn danych
i ścisłą izolację aplikacji. Wszystko, co nasze, jest napisane w C.

> Wcześniej w tym repozytorium był **Kons-OS** — dystrybucja na bazie Debiana (wersje 0.1.0–0.1.2).
> Jest zachowany w gałęzi [`archiwum/debian-0.1.2`](../../tree/archiwum/debian-0.1.2) i tagach `v0.1.x`.

## Na jakim etapie jesteśmy

| Etap | Co to daje | Status |
|---|---|---|
| M0 Porządki | nowe repo, dokumenty, narzędzia, automatyczne budowanie | PRZETESTOWANE |
| M1 Start | system startuje w maszynie wirtualnej (QEMU) i pisze „K1: init OK” | PRZETESTOWANE |
| M2 Ekran | obraz na ekranie: tapeta, logo, kursor; mysz i klawiatura | PRZETESTOWANE (w QEMU) |
| M3 Trusted Core | nadzorca usług, każda usługa we własnej „klatce” | PLANOWANE |
| M4 GUI | pulpit, okna, pierwsze aplikacje (zegar, notatki) | PLANOWANE |
| M5–M12 | magazyn danych, sieć, przeglądarka w izolacji, bezpieczny start, audyt | PLANOWANE |

Pełny plan i zasady: [`docs/SPEC.md`](docs/SPEC.md). Co się zmieniło: [`CHANGELOG.md`](CHANGELOG.md).

**Ważne:** K1 działa na razie tylko w maszynie wirtualnej QEMU: pokazuje tapetę z logo i kursor,
który rusza się myszą i strzałkami. Na prawdziwym komputerze jeszcze nie wystartuje (brak sterowników
dysków, USB i grafiki dla prawdziwego sprzętu) — to przyjdzie przed pierwszymi testami na laptopie.
Wygląd: [`docs/WYGLAD.md`](docs/WYGLAD.md).

## Jak to sprawdzić bez instalowania niczego
Po każdej zmianie na `main` GitHub sam buduje system i uruchamia testy:

1. Zakładka **Actions** → workflow **„K1 — budowa i testy”** → ostatnie uruchomienie.
2. Zielony ✔ = system się zbudował i przeszedł wszystkie testy.
3. Na dole strony w **Artifacts**: `k1-<wersja>-obraz` (jądro, initramfs, obrazy, logi testów).

## Budowanie i testy (dla programisty)
Ubuntu 24.04:
```bash
scripts/przygotuj-srodowisko.sh   # pakiety (raz)
scripts/zbuduj.sh                 # jądro + programy + obrazy → build/wynik/
scripts/testy.sh                  # wszystkie testy (QEMU, także negatywne)
scripts/uruchom-qemu.sh           # ręczne uruchomienie; log: build/qemu/serial.log
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
