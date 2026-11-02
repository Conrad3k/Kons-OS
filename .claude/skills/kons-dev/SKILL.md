---
name: kons-dev
description: Proces pracy nad Kons-OS — jak wprowadzać zmiany w systemie, wersjonować, scalać do main, pilnować buildu ISO i obsługiwać feedback właściciela. Używaj przy każdej zmianie w repo Kons-OS (pakiety, wygląd, hooki, build, CI) oraz gdy właściciel zgłasza uwagi o działaniu systemu.
---

# Praca nad Kons-OS

Przeczytaj najpierw `docs/ARCHITEKTURA.md` — tam jest mapa repo, konwencje i zasady wersjonowania.

## 0. Na start sesji

1. `git fetch origin main` i pracuj na gałęzi opartej o `origin/main` (jeśli gałąź sesji ma już
   scalone wcześniej commity — `git merge origin/main`, **bez force-pusha**).
2. Sprawdź otwarte zgłoszenia na GitHubie (`list_issues`, repo `conrad3k/kons-os`) — zwłaszcza z etykietą
   `feedback` i `błąd`. Jeśli są, zaproponuj właścicielowi, które bierzemy w tej paczce.
3. Sprawdź wynik ostatniego buildu na `main` (`actions_list` → `list_workflow_runs`). Czerwony = napraw najpierw.

## 1. Planowanie paczki zmian

- Build kosztuje ~25 minut z miesięcznego limitu → **łącz kilka zmian w jedno scalenie**.
- Jeśli prośba jest niejasna co do efektu wizualnego, zapytaj krótko zanim zaczniesz; jeśli jest wybór
  techniczny, zdecyduj sam i powiedz, co wybrałeś.

## 2. Wprowadzanie zmian

- Pakiety → `config/package-lists/`; nazwy weryfikuj dla Debiana **trixie** (pakiet nieistniejący = build pada
  po ~10 min). Nie masz dostępu do deb.debian.org, więc przy niepewności wybierz pakiet, którego jesteś pewien,
  i powiedz o tym.
- Pliki → `config/includes.chroot/<ścieżka w systemie>`; konfiguracja poleceniami → hook w `config/hooks/live/`.
- Hooki: `sh -n` na każdym zmienionym; skrypty: `bash -n`; uruchom lokalnie to, co da się uruchomić
  (np. `kons-*` poza chrootem).
- Każda zmiana **systemu** (cokolwiek w `config/`, `build.sh`):
  - podbij `VERSION` wg tabeli w ARCHITEKTURA.md (raz na paczkę, nie na commit),
  - dopisz wpis w `CHANGELOG.md` (przenieś „Niewydane” do nowej sekcji wersji z datą).
- Zmiana tylko docs/CI/repo: bez podbijania wersji, wpis w „Niewydane”.
- Aktualizuj `docs/ARCHITEKTURA.md`, gdy zmieniasz strukturę lub konwencje.

## 3. Scalanie do main

Repo jest prywatne, właściciel zgodził się na scalanie bez pytania:

1. Commit (wiadomość po angielsku), `git push -u origin <gałąź>`.
2. PR po polsku: co się zmienia **z perspektywy użytkownika systemu**, potem szczegóły techniczne.
   Zamknij powiązane zgłoszenia przez `Closes #N`.
3. Scal **squash**. Jeśli zmiana nie dotyka systemu (tylko docs), dodaj `[skip ci]` do tytułu commita scalenia,
   żeby nie palić minut. (Workflow i tak buduje tylko przy zmianach w `config/`, `build.sh`, `VERSION`, workflow.)

## 4. Pilnowanie buildu

- Build na `main` startuje automatycznie. Sprawdzaj go `send_later` co ~5 min (logi przez `get_job_logs`
  są dostępne dopiero po zakończeniu kroku/joba).
- **Padł** → przeczytaj log (`get_job_logs`, `failed_only`), napraw, scal poprawkę jako PATCH, pilnuj dalej.
  Najczęstsze przyczyny: nieistniejący pakiet, błąd w hooku (`set -e`), brak katalogu w `includes.chroot`
  (git nie trzyma pustych katalogów).
- **Udał się** → podaj właścicielowi link do runu i nazwę artefaktu `kons-os-<wersja>-iso`, wypisz w punktach,
  co ma sprawdzić w tej wersji (konkretne rzeczy do kliknięcia/zobaczenia).
- Po zakończeniu usuń pozostałe zaplanowane sprawdzenia.

## 5. Feedback właściciela

Właściciel testuje ISO w VMware/VirtualBox albo z pendrive'a — **on jest jedynym, który widzi ekran**.

- Feedback przychodzi w czacie albo jako zgłoszenie z szablonu (`.github/ISSUE_TEMPLATE/`).
- Uwagi z czatu, których nie robisz od razu, zapisz jako zgłoszenie (`issue_write`, etykieta `feedback`/`pomysł`),
  żeby nie zginęły między sesjami.
- Przy błędach dopytaj o: wersję (`kons-info` albo nazwa ISO), gdzie uruchomione (VM/sprzęt), co widać
  (zrzut ekranu), co było oczekiwane.
- Nie zgłaszaj, że coś „działa”, dopóki właściciel tego nie potwierdził — mów „zbudowało się, do sprawdzenia: …”.

## 6. Komunikacja

- Po polsku, prosto — właściciel nie jest programistą (studiuje psychologię). Wyjaśniaj skutki, nie mechanikę.
- Linki do PR i runów zawsze pełnym URL-em.
