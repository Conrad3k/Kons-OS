# 0005. Obsługa błędów krytycznych przy starcie

- Status: Przyjęty
- Data: 2026-11-06
- Dotyczy: SPEC §7 (PID 1: kontrolowana obsługa błędów krytycznych), §14 (M1: testy negatywne)

## Kontekst
Jeśli PID 1 (w initramfs lub `/rdzen/init`) zakończy działanie, jądro wywołuje panic („Attempted to
kill init!”). Błędy przy starcie (brak dysku, zły obraz, brak `/rdzen/init`, niezgodne jądro) muszą
kończyć się czytelnym komunikatem i bezpiecznym stanem, a nie kernel panic ani zawieszeniem.

## Decyzja
- Wspólna funkcja `k1_awaria()` (`src/wspolne/k1_log.c`): wypisuje
  `<TAG>: BŁĄD KRYTYCZNY: <opis>` i `system zostanie wyłączony za 5 s` na konsolę, czeka 5 s,
  `sync()`, `reboot(RB_POWER_OFF)`. Jeśli wyłączenie się nie uda — zatrzymuje się na zawsze
  (`pause()`), nigdy nie wychodzi.
- Zasada **„bezpieczna awaria”** (fail closed): każde nieudane montowanie i każde nieudane
  ustawienie bezpieczeństwa (sysctl) zatrzymuje start. System nie startuje „na pół gwizdka”.
- `/rdzen/init` blokuje wszystkie sygnały i obsługuje je synchronicznie (`sigwaitinfo`): SIGCHLD →
  zbieranie zakończonych procesów (także osieroconych); SIGTERM → kontrolowane wyłączenie;
  SIGINT (Ctrl-Alt-Del, po `reboot(RB_DISABLE_CAD)`) → kontrolowany restart; pozostałe → zapis
  w logu i pominięcie.
- Kontrolowane zamknięcie: SIGTERM do wszystkich procesów, do 3 s na zakończenie, SIGKILL, `sync()`,
  `reboot()`.
- **M1 (tymczasowo):** jeśli w obrazie jest `/rdzen/system/nadzorca` (tylko obraz testowy), init go
  uruchamia, a po jego zakończeniu wyłącza system. W M3 zastąpią to reguły restartu nadzorcy.

## Konsekwencje
- Testy negatywne mogą sprawdzić, że maszyna sama się wyłączyła z komunikatem, bez „Kernel panic”
  (`scripts/test-m1.sh`).
- Na prawdziwym sprzęcie w M1 komunikat widać tylko na porcie szeregowym (nie ma jeszcze ekranu —
  M2). Od M2 błędy krytyczne powinny być widoczne na ekranie (PLANOWANE).
- Błąd, który zabije samego init (np. błąd pamięci w init), nadal kończy się kernel panic — to
  nieusuwalna cecha Linuksa. Ograniczamy to, utrzymując init bardzo małym (≈240 linii z komentarzami) i bez
  parsowania danych zewnętrznych.

## Rozważane alternatywy
- Restart zamiast wyłączenia — przy trwałym błędzie (zły obraz) powoduje pętlę restartów.
- Zatrzymanie bez wyłączenia — zostawia maszynę włączoną bez informacji (brak ekranu w M1);
  w testach wymagałoby limitu czasu zamiast jednoznacznego zakończenia.
