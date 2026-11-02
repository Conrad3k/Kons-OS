# Architektura Kons-OS

## W jednym zdaniu

Kons-OS = **Debian 13 (trixie)** + nasza lista pakietów + nasze pliki i skrypty konfiguracyjne,
złożone narzędziem **live-build** w obraz ISO, który budują **GitHub Actions**.

Nie mamy własnego jądra ani własnych pakietów `.deb` (jeszcze). Wszystko, co „nasze”, to konfiguracja.

## Jak powstaje ISO

```
push na main
   └─ GitHub Actions (.github/workflows/build-iso.yml)
        └─ kontener debian:trixie (--privileged)
             └─ ./build.sh
                  ├─ lb config   ← opcje obrazu (dystrybucja, bootloadery, parametry startu)
                  └─ lb build
                       ├─ bootstrap: debootstrap bazowego Debiana
                       ├─ chroot:    instalacja pakietów z config/package-lists/
                       │             skopiowanie config/includes.chroot/ do systemu
                       │             uruchomienie config/hooks/live/*.hook.chroot
                       └─ binary:    squashfs + GRUB (UEFI) + syslinux (BIOS) → *.hybrid.iso
   └─ artefakt kons-os-<wersja>-iso (30 dni) + log buildu
   └─ tag v<wersja> (jeśli nowy)
```

## Mapa repozytorium

| Ścieżka | Rola | Kiedy tu zaglądać |
|---|---|---|
| `VERSION` | numer wersji (jedno źródło prawdy) | przy każdej zmianie systemu |
| `CHANGELOG.md` | historia zmian dla ludzi | przy każdej zmianie systemu |
| `build.sh` | wywołanie `lb config` + `lb build` | zmiana bootloadera, parametrów startu, architektury |
| `config/package-lists/*.list.chroot` | **co jest zainstalowane** | dodanie/usunięcie programu |
| `config/includes.chroot/` | **pliki wgrywane 1:1** do systemu (ścieżka = ścieżka w systemie) | tapety, własne skrypty, gotowe pliki konfiguracyjne |
| `config/hooks/live/NNNN-*.hook.chroot` | **skrypty uruchamiane w środku systemu** podczas budowy | konfiguracja wymagająca poleceń (locale, alternatives, generowanie plików) |
| `.github/workflows/build-iso.yml` | CI: budowa ISO, artefakty, tagi | zmiana procesu budowania |
| `.github/ISSUE_TEMPLATE/` | formularze feedbacku | — |
| `.claude/skills/kons-dev/` | proces pracy dla Claude | — |
| `docs/` | dokumentacja | — |

Pliki w `config/` generowane przez `lb config` (np. `config/binary`, `config/chroot`) są w `.gitignore`:
źródłem prawdy jest `build.sh`.

## Warstwy systemu

1. **Baza** — Debian trixie z repo `main contrib non-free non-free-firmware` + aktualizacje bezpieczeństwa.
2. **Sprzęt** — firmware, NetworkManager, PipeWire (`kons-base.list.chroot`).
3. **Pulpit** — Xorg, LightDM, XFCE (z ustawieniami, terminalem, Thunarem), motywy, Firefox (`kons-desktop.list.chroot`).
   Zasada od 0.1.1: **minimum** — każdy dodatkowy program to świadoma decyzja właściciela.
4. **Tożsamość Kons-OS** — hooki `0100-kons-branding` (os-release, motd, tapeta, motyw XFCE/LightDM)
   i `0200-kons-locale` (polski), plus `includes.chroot` (tapeta, `kons-info`).
5. **Sesja live** — `live-boot`/`live-config`: użytkownik `kons`/`live`, autologowanie,
   parametry z `--bootappend-live` w `build.sh`.

## Konwencje

- **Hooki** numerujemy co 100 (`0100`, `0200`, …), żeby dało się wstawić coś pomiędzy. Każdy hook:
  `#!/bin/sh`, `set -e`, komentarz na górze „co i po co”, musi być wykonywalny.
- **Domyślne ustawienia użytkownika** trafiają do `/etc/skel/` (kopiowane do katalogu domowego nowego użytkownika).
- **Własne polecenia** mają prefiks `kons-` i leżą w `config/includes.chroot/usr/local/bin/`.
- **Listy pakietów** grupujemy komentarzami `# --- Sekcja ---`; nowa duża grupa = nowy plik `kons-<nazwa>.list.chroot`.
- **Język**: komunikaty widoczne w systemie, dokumentacja i opisy PR — po polsku. Komunikaty commitów — po angielsku.

## Wersjonowanie

Format `MAJOR.MINOR.PATCH` (SemVer), dopóki jesteśmy przed 1.0:

| Zmiana | Przykład | Co podbić |
|---|---|---|
| naprawa, drobna poprawka wyglądu | tapeta się nie ustawiała | PATCH: 0.1.0 → 0.1.1 |
| nowa funkcja, program, zmiana wyglądu | dodanie LibreOffice, instalator | MINOR: 0.1.1 → 0.2.0 |
| zmiana tylko dokumentacji / CI / repo | ten plik | **bez zmiany** wersji |
| system gotowy do codziennego używania (instalator, stabilność) | — | 1.0.0 (decyzja właściciela) |

Wersja trafia do `/etc/os-release`, nazwy ISO, nazwy artefaktu i tagu `vX.Y.Z` (tag zakłada CI po udanym buildzie).

## Ograniczenia, o których warto pamiętać

- **Minuty GitHub Actions**: prywatne repo ma limit (darmowy plan ~2000 min/mies.), build trwa ~20–25 min.
  Dlatego budujemy tylko `main`, a zmiany łączymy w paczki.
- **GitHub Releases** przyjmują pliki do 2 GiB, a ISO ma ~2,2 GB — dlatego ISO jest w artefaktach Actions, nie w Releases.
- **Brak testu uruchomienia w CI** — to, że ISO się zbudowało, nie znaczy, że system dobrze działa.
  Weryfikacja wizualna = feedback właściciela.
