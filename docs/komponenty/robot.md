# Robot aktualizacji (0.10.5 krok 3, ADR 0047)

**Co to:** workflow GitHub Actions `.github/workflows/robot.yml`, który raz dziennie (02:41 UTC) sprawdza, czy jest
poprawione jądro Linux albo poprawione pakiety przeglądarki strefy. Jeśli tak — przygotowuje nową wersję K1 (PATCH), puszcza
pełne testy i po zielonych sam ją wydaje. Wtedy laptop po 1–2 dniach pyta „Jest nowa wersja K1” (ADR 0046).
**Działa w pełnym trybie** — od wydania 0.10.5 i znowu od wydania 0.10.9 (przed nimi — próbnym: PR i testy, bez scalania
i wydania; drugi raz od kroku 4 planu 0.10.9, który zmienił to, co niesie aktualizacja; niżej, „Tryb próbny”).

## Co sprawdza
| Co | Skąd | Jak sprawdza źródło | Plik, który zmienia |
|---|---|---|---|
| jądro Linux — ta sama linia | `www.kernel.org/releases.json` (stable, longterm) | `sha256sums.asc` podpisany kluczem z `jadro/klucze/` | `jadro/wersja.env` |
| jądro — nowsza linia długoterminowa | jw., `longterm`, po 7 dniach spokoju | jw. + `scripts/porownaj-jadro.sh` (T19) | `jadro/wersja.env` |
| pakiety przeglądarki (Firefox ESR, biblioteki, curl) | Debian 13 (trixie, -updates, -security) | `InRelease` kluczami z `strefy/klucze/` (`scripts/odswiez-strefe.py`) | `strefy/przegladarka.txt` |

Przy zmianie robot podbija `VERSION` (tylko PATCH) i dopisuje wpis w `CHANGELOG.md` ze znacznikiem `<!-- k1: poprawki -->`
(K1 pokazuje swoje zdanie „Ta aktualizacja zawiera istotne poprawki dotyczące bezpieczeństwa i jakości.”).

## Czego nie rusza
- **Pliki:** wszystko poza `jadro/wersja.env`, `strefy/przegladarka.txt`, `VERSION`, `CHANGELOG.md` — workflow przerywa
  przy każdym innym pliku. W szczególności: kod, skrypty, `jadro/k1.config`, `jadro/strefa.config`, listy odrzuconych opcji,
  workflow.
- **Wersje:** nigdy MINOR ani MAJOR.
- **Kiedy nie zaczyna:**
  - gdy otwarty PR człowieka zmienia `VERSION`, jądro albo pakiety strefy (log: „czekam na wydanie ludzi”);
  - gdy jest już otwarty PR robota;
  - gdy na main trwa budowa.

## Kiedy prosi człowieka (Issue z etykietą „robot”)
- **Nowa linia jądra z pytaniami o konfigurację:** lista opcji z opisem z Kconfig. Każdą rozstrzyga człowiek: do fragmentu
  albo do `jadro/*-odrzucone.txt` z powodem. Następny bieg robota rusza dalej.
- **Koniec krótkiej linii jądra (T20, 0.10.5 krok 7):** K1 może stać na linii „stable”, która nie jest długoterminowa (od
  0.10.5: 7.3). Jej poprawki robot robi sam; gdy linia kończy wsparcie, a nowszej długoterminowej nie ma, robot nie wybiera
  sam następnej krótkiej linii — Issue „Robot: linia jądra X kończy wsparcie — wybór następnej linii” (jedno otwarte naraz;
  przejście robi człowiek według `jadro/README.md`). Pojawi się długoterminowa nie starsza od obecnej — robot przechodzi na
  nią sam (7 dni spokoju, porównanie konfiguracji).
- **Czerwone testy:** PR robota zostaje otwarty — zmianę robi Claude albo Codex zwykłym PR-em.
- **Zła suma, zły podpis źródła, błąd przebiegu:** nic nie scalone.

## Wymagane ustawienie repozytorium
Robot otwiera i scala PR-y tokenem workflow (`GITHUB_TOKEN`). GitHub pozwala na to dopiero po jednym ustawieniu:
**Settings → Actions → General → Workflow permissions → „Allow GitHub Actions to create and approve pull requests”**.
Bez niego bieg kończy się błędem „GitHub Actions is not permitted to create or approve pull requests” i Issue „Robot:
przebieg nie skończył się” (tak było 2026-11-24 i 2026-11-25: Issue #216, #217). Włącza je właściciel, raz.

## Jak zatrzymać
- **Zawsze:** GitHub → Actions → „Robot — poprawki jądra i przeglądarki” → ⋯ → **Disable workflow**. Włączenie — to samo
  miejsce.
- **Na czas dużych prac** (np. ręczna zmiana jądra w kroku planu): wyłącz workflow i dopisz to w `docs/DO_ZROBIENIA.txt`
  („Tymczasowe”, z terminem), żeby robot nie został wyłączony na zawsze.
- **Tryb próbny:** `ROBOT_PROBNY: "1"` w `robot.yml` (do wydania 0.10.5 i od kroku 4 planu 0.10.9 do wydania 0.10.9; dziś `"0"` — pełny tryb) albo ręczne uruchomienie z `proba`.

## Jak przetestować
- **Ręcznie:** Actions → „Robot — poprawki jądra i przeglądarki” → Run workflow (`proba` = tak, `start` = gałąź startowa —
  np. z celowo starszym jądrem w `jadro/wersja.env`).
- **Lokalnie, bez GitHuba:** `scripts/robot.sh` w kopii repo — wypisze `co=…` i zmieni pliki.
- **Porównanie konfiguracji:**
  `scripts/porownaj-jadro.sh <źródła> build/wynik/jadro.config build/jadro-fragment.config jadro/k1-odrzucone.txt`.
