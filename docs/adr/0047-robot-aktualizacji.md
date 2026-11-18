# 0047. Robot aktualizacji

- Status: Przyjęty; wdrożony w 0.10.5 (krok 3) w trybie próbnym — pełny tryb włącza krok 8 po wydaniu 0.10.5
- Data: 2026-11-18
- Dotyczy: ADR 0003 (wersja jądra — uzupełniony: linię wybiera robot), ADR 0043 (pakiety strefy), ADR 0046 (aktualizacje
  przez sieć), AGENTS.md (praca we dwóch — robot trzeci); plan `docs/PLAN-0.10.5.txt`: krok 3, D6, T8–T11, T18, T19, S17–S24,
  S33, S51

## Kontekst
Poprawki bezpieczeństwa jądra Linux i Firefoksa wychodzą co kilka dni. Do 0.10.4 każdą wnosił do K1 człowiek (zmiana
`jadro/wersja.env` albo `strefy/przegladarka.txt`, testy, wydanie). Od 0.10.5 K1 sam pobiera nowe wersje z wydań (ADR 0046),
więc poprawka może dojść do laptopa w 1–2 dni — o ile ktoś zrobi wydanie. Właściciel zdecydował (D6): **wydaje je robot**;
większe zmiany (kolejny etap prac) wydają ludzie.

## Decyzja
**Co robot zmienia (T8):** wyłącznie numery wersji i sumy w czterech plikach: `jadro/wersja.env`, `strefy/przegladarka.txt`,
`VERSION`, `CHANGELOG.md`. Nigdy kodu, skryptów, konfiguracji jądra, list odrzuconych opcji ani workflow — workflow
sprawdza to przed PR-em i przerywa przy innym pliku. Źródła sprawdza jak budowa: `sha256sums.asc` kernel.org podpisany
kluczem z `jadro/klucze/` (próba 2026-11-18: podpisuje autosigner `B8868C80…`; bez niego robot kończy się błędem i niczego
nie zmienia), `InRelease` Debiana kluczami z `strefy/klucze/` (`scripts/odswiez-strefe.py`). Logika: `scripts/robot.sh`
(człowiek też może go uruchomić), przebieg: `.github/workflows/robot.yml`.

**Testy i wydanie (T9, S17):** zdarzenia z `GITHUB_TOKEN` nie uruchamiają innych workflow, poza `workflow_dispatch`
i `repository_dispatch` (dokumentacja GitHub Actions, „Triggering a workflow from a workflow”). Dlatego robot:
1. tworzy gałąź `robot/<co>-<wersja>` i PR z etykietą „robot”;
2. uruchamia `k1.yml` na tej gałęzi przez `workflow_dispatch` z `testy: true` — pełne testy jak etykieta „testy”;
3. czeka na wynik (`gh run watch`);
4. po zielonych: squash do main i `workflow_dispatch` `k1.yml` na main (budowa, tag, wydanie z ogłoszeniem i paczką).

Uprawnienia workflow: `contents`, `pull-requests`, `actions`, `issues` — `write`. Bez osobnego tokenu i bez nowego sekretu.
Ochrona gałęzi main, gdyby właściciel ją włączył, może zablokować scalenie przez `GITHUB_TOKEN` — wtedy Issue.

**Kiedy robot przestaje (T10) — Issue z etykietą „robot”, bez scalania:**
- konfiguracja nowej linii nie wchodzi albo porównanie konfiguracji ma pytania (T19);
- czerwone testy (PR zostaje otwarty);
- zła suma albo podpis źródła, każdy inny błąd przebiegu.

Robot nie zaczyna, gdy otwarty PR człowieka zmienia `VERSION`, jądro albo pakiety strefy; gdy jest już otwarty PR robota;
gdy na main trwa budowa.

**Nowe linie jądra (T11, T19, T20):**
- Ta sama linia — robot bierze najnowszą wersję (stable albo longterm).
- Nowsza linia **długoterminowa** (`longterm` z `releases.json`) — tylko po 7 dniach od ostatniego wydania, gdy nie ma
  otwartego Issue „błąd” (S24), i tylko gdy `scripts/porownaj-jadro.sh` nie ma pytań. Jak działa porównanie:
  - konfigurację, jaką K1 dziś buduje, bez wierszy „is not set” przenosi do nowych źródeł (`olddefconfig`);
  - opcja, którą jądro włączyłoby samo, a fragment nie — jest pytaniem, chyba że stoi na liście odrzuconych
    (`jadro/k1-odrzucone.txt`, `jadro/strefa-odrzucone.txt`; każda opcja z powodem; zmienia tylko człowiek);
  - pytamy tylko o opcje z pytaniem w Kconfig — S51: inne włącza `select`;
  - opcja włączona dotąd, której nowe źródła nie znają, też jest pytaniem.
- Próba: KVM_IOAPIC usunięta z fragmentu → wskazana (kod 1); pełne listy → 0 pytań.
- Linii krótkiej robot sam nie wybiera; na starszą nie wraca nigdy.

**Pierwsze wypełnienie list odrzuconych:**
- 265 opcji jądra K1 i 202 opcje jądra strefy: wszystkie „domyślnie tak” z 6.18, których K1 nie włącza od początku
  (allnoconfig, ADR 0003).
- Wspólny powód; przegląd pojedynczych opcji — krok 7 planu (7.x).

**Wydania ludzi i robota — nigdy dwa naraz (S19, S33):**
- **Po stronie robota:**
  - robot czeka na otwarty PR człowieka ze zmianą `VERSION`;
  - tuż przed scaleniem pobiera świeży main — gdy `VERSION` się zmieniło, zamyka swój PR (zacznie od nowa następnego dnia);
  - podbija tylko PATCH.
- **Po stronie CI** (`k1.yml`):
  - na main kolejka zamiast anulowania (`cancel-in-progress` tylko dla PR-ów);
  - tag i wydanie tylko wtedy, gdy `VERSION` budowanego commita to wciąż `VERSION` na main.

**Wpis w CHANGELOG robota (T18, D13):**
- znacznik `<!-- k1: poprawki -->` zamiast „Opis w K1:” — robot nie pisze tekstu, który pokazuje K1; K1 pokazuje swoje
  stałe zdanie (`K1_OGL_ZDANIE_POPRAWEK`);
- nowa linia jądra — także `<!-- k1: nowa linia jądra -->` (dopisek w pytaniu na laptopie).

**Tryb próbny:**
- Do wydania 0.10.5 `ROBOT_PROBNY: "1"` w `robot.yml`: PR i testy, bez scalania i wydania (inaczej robot wydałby 0.10.x
  przed 0.10.5).
- Krok 8 przestawia na `"0"`.
- Ręcznie: Actions → „Robot — poprawki jądra i przeglądarki” → Run workflow (wejście `proba`, `start` — gałąź startowa
  do prób, np. z celowo starszym jądrem).

## Konsekwencje
- Robot jest trzecim „asystentem” w AGENTS.md: gałęzie `robot/…`, wydania PATCH, nie rusza cudzych gałęzi.
- Ryzyko (RYZYKA R94): przejęte konto GitHub albo archiwum Debiana — podpisana wersja u wszystkich po pytaniu
  (R91; ochrona: podpisy źródeł, pytanie przed instalacją, „Wróć do poprzedniej wersji”).
- Próba 2026-11-18 (lokalnie, `scripts/robot.sh`):
  - jądro 6.18.60 → 6.18.64 znalezione, podpis sprawdzony, wpis i `VERSION` 0.10.3 przygotowane;
  - bez klucza autosignera — odmowa (kod 3), nic nie zmienione;
  - pakiety przeglądarki aktualne.

## Rozważone
- **Robot czeka na „tak” właściciela** — poprawka czekałaby na człowieka; tracimy to, po co jest robot (D6).
- **Osobny token z większymi prawami** — nowy sekret do pilnowania; `workflow_dispatch` wystarcza.
- **Zaczynać od konfiguracji domyślnej i wyłączać, czego nie chcemy** — każda nowa opcja weszłaby sama, bez decyzji (T19).
