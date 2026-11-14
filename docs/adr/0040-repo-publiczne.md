# 0040. Repo publiczne

- Status: Przyjęty
- Data: 2026-11-14
- Dotyczy: SPEC §2 (GitHub Actions) i §10 (scalanie), `AGENTS.md` („Praca we dwóch”), `docs/ARCHITEKTURA.md` (CI),
  skill `kons-dev` (scalanie, CI)

## Kontekst
Po scaleniu wydania 0.9.0 (PR #102) przebieg CI na `main` nie dostał maszyny: repo prywatne zużyło w listopadzie ok.
2000 minut GitHub Actions (69 przebiegów po ok. 30 min) — tyle, ile daje darmowy plan. Przy obecnym tempie miesiąc to
ok. 4500 minut; GitHub Pro (3000 minut) by nie wystarczył. Przed przejściem sprawdzono historię repo: bez kluczy
prywatnych, plików `.key`/`.pem` i tokenów; sekret podpisu `K1_KLUCZ_PODPISU` jest w ustawieniach Actions, nie w kodzie.

## Decyzja
Właściciel zmienił widoczność repo na **publiczne** (2026-11-14). Minuty Actions na zwykłych maszynach GitHuba są
wtedy bez limitu i bez opłat; przebieg 0.9.0 ponowiony.
- Scalanie bez zmian: asystent scala PR sam po zielonych testach (squash) — to zgoda właściciela, nie skutek
  prywatności repo.
- CI dalej buduje tylko `main` i PR-y z etykietą „testy” (etykiety nadaje właściciel albo asystent). Przebiegi z
  cudzych kopii repo nie dostają sekretów (zasada GitHuba); pamięć podręczna jądra (D4) nie zawiera nic tajnego.
- Oszczędne łączenie zmian w paczki zostaje (czas właściciela i limit Claude), ale minuty CI nie są już powodem.

## Skutki
- Kod, dokumenty, plany, zgłoszenia, PR-y i wydania są widoczne dla wszystkich; obcy mogą zgłaszać uwagi i PR-y —
  scala tylko właściciel albo asystent po testach.
- Jeden starszy commit ma prywatny adres e-mail autora (decyzja właściciela, czy go usuwać z historii — nie usunięto).
- Nigdy nie wpisujemy do repo sekretów — teraz tym bardziej (bez zmian: `AGENTS.md`, „Bezpieczeństwo”).
