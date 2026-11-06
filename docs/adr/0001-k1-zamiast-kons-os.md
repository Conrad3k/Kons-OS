# 0001. K1 zamiast dystrybucji Kons-OS; archiwum i struktura repo

- Status: Przyjęty
- Data: 2026-11-06
- Dotyczy: SPEC §0, §2.9–2.10, §14 (M0)

## Kontekst
Repozytorium zawierało Kons-OS — dystrybucję Debiana 13 budowaną przez live-build (ostatnia wersja
0.1.2, tag `v0.1.2`). Właściciel postanowił zbudować zamiast niej Kons K1 Sovereign: własny userspace
w C na jądrze Linux, bez dystrybucji pod spodem (`docs/SPEC.md`). Starej wersji nie kasujemy.

## Decyzja
- Wersja debianowa zostaje w gałęzi **`archiwum/debian-0.1.2`** (= tag `v0.1.2`) oraz w tagach `v0.1.x`.
  Z `main` usuwamy jej pliki (`build.sh`, `config/`, skrypty live-build, workflow ISO).
- Nowa struktura `main`:

  | Ścieżka | Zawartość |
  |---|---|
  | `src/` | kod K1 w C (`wspolne/`, `initramfs/`, `init/`, kolejne komponenty) |
  | `jadro/` | wersja jądra, konfiguracja (fragment Kconfig), linia poleceń, klucze PGP |
  | `testy/` | testy w systemie (`m1/` …) i na hoście (`host/`: jednostkowe, fuzzing) |
  | `scripts/` | budowa, uruchamianie w QEMU, zrzuty ekranu, testy |
  | `docs/` | SPEC, ADR, ARCHITEKTURA, ZALEZNOSCI, RYZYKA, TESTY, opisy komponentów |
  | `build/` | wyniki budowy (ignorowane przez git) |
- Wersje K1: SemVer od **0.1.0** w pliku `VERSION`, tagi **`k1-vX.Y.Z`** zakłada CI po udanym
  buildzie i testach na `main`. Każdy ukończony etap (milestone) podbija MINOR (M1 → 0.1.0, M2 → 0.2.0…),
  poprawki — PATCH.
- CI (GitHub Actions) buduje i testuje tylko `main` (minuty prywatnego repo są ograniczone).

## Konsekwencje
- Historia Debiana jest dostępna, ale nie przeszkadza w nowym kodzie.
- `CHANGELOG.md` zaczyna nową historię K1; historia Kons-OS zostaje w nim jako sekcja archiwalna.
- Skill `kons-dev` i `CLAUDE.md` opisują teraz proces K1.

## Rozważane alternatywy
- Nowe repozytorium — odrzucone: właściciel chce zachować jedno repo i historię.
- Trzymanie obu systemów na `main` — odrzucone: mieszanie dwóch procesów budowy i CI.
