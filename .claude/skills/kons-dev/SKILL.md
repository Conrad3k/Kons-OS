---
name: kons-dev
description: Proces pracy nad Kons K1 Sovereign — etapy (milestones), ADR, budowa i testy w QEMU, wersjonowanie, scalanie do main, pilnowanie CI, raport i feedback właściciela. Używaj przy każdej zmianie w repo (kod C, jądro, skrypty, CI, dokumentacja) oraz gdy właściciel zgłasza uwagi.
---

# Praca nad K1

Najpierw przeczytaj `docs/SPEC.md` (nadrzędna), `docs/ARCHITEKTURA.md` (mapa repo, konwencje,
wersje) i `docs/TESTY.md`. Specyfikację zmienia się tylko przez ADR (`docs/adr/`).

## 0. Na start sesji
1. `git fetch origin main`; pracuj na gałęzi opartej o `origin/main` (gałąź sesji ze scalonymi już
   commitami → `git merge origin/main`, bez force-pusha).
2. Zgłoszenia na GitHubie (`list_issues`, repo `conrad3k/kons-os`, etykiety `feedback`, `błąd`, `pomysł`).
3. Ostatni run workflow **„K1 — budowa i testy”** na `main` (`actions_list` → `list_workflow_runs`).
   Czerwony = napraw najpierw.
4. Środowisko (kontener jest efemeryczny):
   `scripts/przygotuj-srodowisko.sh && scripts/zbuduj.sh && scripts/test-m1.sh` (~5 min; jądro ~2–3 min).

## 1. Zasady etapu (SPEC §2)
- Jeden etap (milestone) naraz; następny dopiero, gdy bieżący ma **powtarzalny test** (skrypt
  `scripts/test-mN.sh`, opis w `docs/TESTY.md`).
- Nie wymyślaj nazw syscalli, flag, opcji Kconfig ani zachowań jądra — sprawdzaj w źródłach
  (`build/zrodla/linux-*/`), nagłówkach (`build/naglowki/`), man-pages. Niepewność oznaczaj wprost.
- Statusy: ZAIMPLEMENTOWANE / PRZETESTOWANE (tylko z dowodem: log szeregowy, zrzut ekranu, wynik
  testu) / PLANOWANE. Nigdy „działa”, jeśli nie było uruchomione.
- Każdy mechanizm bezpieczeństwa: opis ograniczeń + test negatywny (co ma się NIE udać).
- Nowy komponent → `docs/komponenty/<nazwa>.md` (cel, granica zaufania, uprawnienia, wejścia/wyjścia,
  zależności, syscalle, błędy, powierzchnia ataku, testy).
- Nowa zależność → `docs/ZALEZNOSCI.md` z uzasadnieniem. Nowe ryzyko/ograniczenie → `docs/RYZYKA.md`.
- Konflikt SPEC z Linuksem → najmniejsza bezpieczna korekta + ADR + dopisek `[korekta: ADR NNNN]` w SPEC.

## 2. Zmiany
- **Jądro:** tylko przez `jadro/k1.config` (fragment do `allnoconfig`). Nazwy opcji sprawdź w `Kconfig*`;
  `scripts/zbuduj-jadro.sh` przerwie build, jeśli opcja nie weszła. Zmiana wersji: `jadro/README.md`.
- **Kod C:** flagi w `Makefile` (zmiana = ADR); `make` musi przejść bez ostrzeżeń (`-Werror`);
  parsery mają testy w `testy/host/` (ASan/UBSan) i fuzzing (`make fuzz`).
- **Skrypty:** `sh -n` na zmienionych; uruchom je.
- Przed scaleniem zmian systemu: pełny test etapu lokalnie (`scripts/test-mN.sh` + wcześniejszych).
  Przy zmianach w budowie: `scripts/sprawdz-powtarzalnosc.sh`.
- **Wersja** (`VERSION`, raz na paczkę): ukończony etap → MINOR, poprawka/aktualizacja jądra → PATCH,
  tylko docs/CI → bez zmiany. Wpis w `CHANGELOG.md` (sekcja nowej wersji z datą albo „Niewydane”).
- Łącz zmiany w paczki — CI kosztuje minuty (~10 min na run), a właściciel ma ograniczony limit Claude.

## 3. Scalanie do main
Repo prywatne, właściciel zgodził się na scalanie bez pytania:
1. Commit po angielsku, `git push -u origin <gałąź>`.
2. PR po polsku: najpierw co się zmienia **dla użytkownika**, potem technika. `Closes #N` dla zgłoszeń.
3. Scal **squash**. Zmiany tylko w docs: dopisz `[skip ci]` do tytułu commita scalenia
   (workflow i tak startuje tylko przy zmianach w `src/`, `jadro/`, `testy/`, `scripts/`, `Makefile`, `VERSION`).

## 4. Pilnowanie CI
- Run na `main` startuje sam. Sprawdzaj przez `send_later` co ~5 min (logi przez `get_job_logs`
  dostępne po zakończeniu kroku). Po zakończeniu usuń zaplanowane sprawdzenia.
- **Padł** → przeczytaj log (`get_job_logs`, `failed_only`), napraw, scal poprawkę (PATCH), pilnuj dalej.
  Logi testów: artefakt `k1-<wersja>-logi`.
- **Udał się** → tag `k1-v<wersja>` zakłada CI; podaj właścicielowi link do runu.

## 5. Raport po etapie (SPEC §2.11)
Krótko, po polsku, prostym językiem (skutki, nie mechanika): cel i kryteria, co zrobiono, dowody
(fragment logu, wynik testów), testy negatywne, ograniczenia (z `docs/RYZYKA.md`), statusy.
Nie wklejaj całych plików. Linki do PR i runów — pełnym URL-em.

## 6. Feedback właściciela
Właściciel jest jedynym, kto ma prawdziwy sprzęt (laptop, pendrive) — od M2 będzie testował obraz.
- Uwagi z czatu, których nie robisz od razu → zgłoszenie (`issue_write`, etykieta `feedback`/`pomysł`).
- Przy błędach dopytaj: wersja, gdzie uruchomione, co widać (zdjęcie/zrzut ekranu), czego się spodziewał.
- Decyzje produktowe podejmuje właściciel; techniczne — Ty (krótko powiedz, co wybrałeś i dlaczego).
