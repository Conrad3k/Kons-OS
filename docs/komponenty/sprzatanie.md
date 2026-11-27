# Sprzątanie w GitHub Actions

Dwa workflow, które usuwają z GitHuba to, co zostaje po CI. Żaden nie rusza **wydań** (Releases: ISO, obraz USB,
ogłoszenia, paczki aktualizacji) ani kodu.

| Workflow | Kiedy | Co usuwa |
|---|---|---|
| **Sprzątanie artefaktów** (`.github/workflows/sprzatanie-artefaktow.yml`) | codziennie 03:17 UTC, ręcznie, przy zmianie pliku na `main` | artefakty starego Kons-OS i artefakty K1 (logi testów, obrazy z PR-ów) starsze niż 3 dni — zwalnia limit miejsca konta |
| **Sprzątanie przebiegów** (`.github/workflows/sprzatanie-przebiegow.yml`) | tylko ręcznie | stare przebiegi workflow — wpisy na liście Actions razem z ich logami i artefaktami |

## Sprzątanie przebiegów — jak uruchomić
1. GitHub → repo → **Actions** → po lewej **„Sprzątanie przebiegów”** → **Run workflow**.
2. Pola:
   - **dni** — przebiegi z ostatnich tylu dni zostają (domyślnie 30; 0 = usuń wszystkie stare);
   - **Których przebiegów** — wszystkie albo tylko jednego workflow (np. `k1.yml`);
   - **Tylko policz** — zaznaczone: pokazuje, ile by usunął, i nic nie usuwa.
3. Zielony **Run workflow**. Wynik („Usunięto N z M przebiegów”) jest na stronie przebiegu, w podsumowaniu.

## Czego nie usuwa
- najnowszego przebiegu każdego workflow — na liście Actions zawsze widać ostatni stan CI, robota, ogłoszenia dnia;
- przebiegów, które jeszcze trwają (robot sprawdza trwające budowy na `main`), i samego siebie.

## Ograniczenia
- Usuniętego przebiegu nie da się przywrócić: znikają jego log i artefakty (np. obraz USB z PR-a do sprawdzenia na sprzęcie).
- Token workflow ma ok. 1000 próśb do GitHuba na godzinę, więc jeden bieg usuwa najwyżej 900 przebiegów. Gdy zostało
  więcej, podsumowanie to mówi — wtedy uruchom go ponownie za godzinę.
- Uprawnienia: tylko `actions: write` (lista i usuwanie przebiegów); bez dostępu do kodu i sekretów.
