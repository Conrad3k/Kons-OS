# 0025. Przeglądarka K1: własne okno, pożyczony silnik w strefie

- Status: Przyjęty (kierunek); niewdrożony — w 0.10.0 przeglądarką jest Firefox w strefie (ADR 0043); Przeglądarka K1
  (okno K1, silnik w strefie) — później
- Data: 2026-11-08
- Dotyczy: ADR 0020 (strefy), ADR 0024 (wspólna strefa), SPEC §8, DO_ZROBIENIA (sesje przeglądarki)

## Kontekst
Pytanie właściciela: czy własna, systemowa przeglądarka odpada? Własny silnik stron (HTML, CSS,
JavaScript) od zera — tak: to praca zespołów na lata (np. Ladybird) i stałe łatanie dziur. Natywny
Chromium/Firefox na K1 — też nie: wymagają zapisywalnej i wykonywalnej pamięci (JIT), czego K1 zabrania
(niezmiennik 1), i dziesiątek bibliotek, których K1 nie ma. Plan M8 (ADR 0020): zwykła przeglądarka w
osobnej strefie. Jest trzecia droga: **interfejs przeglądarki jako aplikacja K1, silnik w strefie**.

## Decyzja
1. **Etap 1 (M8):** zwykła przeglądarka w osobnej strefie (Firefox albo WPE — ADR w M8). Daje internet
   najszybciej i sprawdza same strefy.
2. **Etap 2 (po M8): „Przeglądarka K1”** —
   - **okno, karty, pasek adresu, pobieranie i ustawienia** to natywna aplikacja K1 (przybornik, zwykła
     klatka, bez sieci i bez silnika stron);
   - **silnik w strefie** (kierunek: WPE WebKit — zrobiony do osadzania, rysuje stronę do pamięci, resztę
     zostawia gospodarzowi): renderuje stronę do pamięci współdzielonej, dostaje klawiaturę i mysz tylko
     dla aktywnej karty, sieć przez usługę `siec` K1;
   - aplikacja i silnik rozmawiają wąskim, sprawdzanym pole po polu protokołem (adres do otwarcia, tytuł,
     postęp, prośba o pobranie) — silnik nie dostaje nic poza tym.
3. **Pasek adresu rysuje K1:** pokazuje prawdziwy adres i stan połączenia; strona nie może go zasłonić
   ani podrobić (ochrona przed podróbkami stron).
4. **Sesje = strefy:** każda sesja (np. „bank”, „ogólna”, „prywatna”) to osobna strefa silnika; karta ma
   kolor swojej strefy; „prywatna” = strefa jednorazowa. Odpowiada to na otwarte pytanie o sesje
   (nazwy i liczbę sesji wybiera właściciel przed tym etapem).
5. **Pobrane pliki** idą od razu do Gatekeepera (M9), nigdy prosto do danych.
6. Zawsze w **osobnej strefie** (ADR 0024: przeglądarka nigdy we wspólnej). Do rozważenia: silnik bez JIT
   jako dodatkowe zabezpieczenie (wolniejszy JavaScript).

## Konsekwencje
- Więcej pracy niż sama przeglądarka w strefie (protokół, interfejs), ale lepsza ochrona (zaufany pasek
  adresu), sesje i spójny wygląd z K1.
- Bez rozszerzeń i synchronizacji kont przeglądarek; WebKit — nieco gorsza zgodność z rzadkimi stronami niż
  Chromium. Zwykła przeglądarka z etapu 1 może zostać jako zapasowa strefa.
- Aktualizacje silnika przychodzą razem z obrazem strefy (podpisane, jak reszta K1).
