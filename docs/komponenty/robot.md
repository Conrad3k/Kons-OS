# Robot aktualizacji (0.10.5 krok 3, ADR 0047)

**Co to:** workflow GitHub Actions `.github/workflows/robot.yml`, który raz dziennie (02:41 UTC) sprawdza, czy jest
poprawione jądro Linux albo poprawione pakiety przeglądarki strefy. Jeśli tak — przygotowuje nową wersję K1 (PATCH), puszcza
pełne testy i po zielonych sam ją wydaje. Wtedy laptop po 1–2 dniach pyta „Jest nowa wersja K1” (ADR 0046).
**Do wydania 0.10.5 działa w trybie próbnym:** robi PR i testy, bez scalania i wydania.

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
- **Czerwone testy:** PR robota zostaje otwarty — zmianę robi Claude albo Codex zwykłym PR-em.
- **Zła suma, zły podpis źródła, błąd przebiegu:** nic nie scalone.

## Jak zatrzymać
- **Zawsze:** GitHub → Actions → „Robot — poprawki jądra i przeglądarki” → ⋯ → **Disable workflow**. Włączenie — to samo
  miejsce.
- **Na czas dużych prac** (np. ręczna zmiana jądra w kroku planu): wyłącz workflow i dopisz to w `docs/DO_ZROBIENIA.txt`
  („Tymczasowe”, z terminem), żeby robot nie został wyłączony na zawsze.
- **Tryb próbny:** `ROBOT_PROBNY: "1"` w `robot.yml`.

## Jak przetestować
- **Ręcznie:** Actions → „Robot — poprawki jądra i przeglądarki” → Run workflow (`proba` = tak, `start` = gałąź startowa —
  np. z celowo starszym jądrem w `jadro/wersja.env`).
- **Lokalnie, bez GitHuba:** `scripts/robot.sh` w kopii repo — wypisze `co=…` i zmieni pliki.
- **Porównanie konfiguracji:**
  `scripts/porownaj-jadro.sh <źródła> build/wynik/jadro.config build/jadro-fragment.config jadro/k1-odrzucone.txt`.
