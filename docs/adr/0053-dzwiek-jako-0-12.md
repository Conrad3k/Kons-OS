# 0053. Dźwięk jako wersja 0.12; Bezpieczny start → 0.13, Audyt → 0.14

- Status: Przyjęty
- Data: 2026-11-22
- Dotyczy: ADR 0035 (mapa drogi według wersji — **zmienia jej tabelę**), SPEC §14, `docs/DO_ZROBIENIA.txt` (mapa drogi),
  `README.md`, `docs/RYZYKA.md` (kolumna „Plan”), skill `kons-dev` §8; plan `docs/PLAN-0.12.0.txt`

## Kontekst
Mapa drogi z ADR 0035 miała po 0.11 „Pliki ze stref” wersję 0.12 „Bezpieczny start” (tryb ratunkowy, TPM, los GRUB-a),
potem 0.13 „Audyt” i 1.0. Dźwięk był w niej drobnym krokiem „Laptop” (PATCH w dowolnej wersji, po zgłoszeniach
właściciela), razem z jasnością ekranu i klawiszami Fn.

Od 0.10 K1 ma przeglądarkę w strefie, a od 0.11 pliki przechodzą między nią a Plikami — przeglądarka jest codziennym
narzędziem, a filmy i strony bez dźwięku są największym brakiem, który widać na laptopie. Dźwięk to też więcej niż
„sterownik”: nowa usługa z wyłącznym prawem do karty dźwiękowej, nowy kanał ze strefy (urządzenie virtio w VMM), okienko
na górnym pasku, klawisze, Ustawienia — temat na całą wersję MINOR, nie na PATCH.

Właściciel, 2026-11-22: „myślę, by w wersji 0.12 zająć się dźwiękiem”.

## Decyzja
1. **0.12 = Dźwięk.** Mapa drogi (zastępuje wiersze 0.12, 0.13 i „Laptop” tabeli z ADR 0035):

   | Wersja | Temat | Dawniej |
   |---|---|---|
   | **0.12** | **Dźwięk**: karta dźwiękowa w K1 (usługa dźwięku w klatce), głośność i klawisze, słuchawki, dźwięk z przeglądarki (strefy) | część „Laptop” (M5.3) |
   | 0.13 | Bezpieczny start — reszta: tryb ratunkowy, TPM, odwoływanie starych wersji, los GRUB-a | M10 (w ADR 0035: 0.12) |
   | 0.14 | Audyt: fuzzing, testy izolacji, analiza statyczna, dokumentacja końcowa → 1.0 | M12 (w ADR 0035: 0.13) |
   | (PATCH) | Laptop: jasność ekranu, klawisze Fn (bez głośności), tablet USB, I2C-HID — po zgłoszeniach | M5.3 |

2. **Przesunięcie dotyczy wszystkich terminów:** co w żywych dokumentach miało termin „0.12” (Bezpieczny start: GRUB
   w ISO i na pendrive'ie, TPM, obraz produkcyjny bez logu na porcie szeregowym, odwoływanie wersji, klucze), ma teraz
   „0.13”; co miało „0.13” (audyt) — „0.14”. Tłumaczenie dawnych etapów: M10 = 0.13, M12 = 0.14 (reszta tabeli z ADR 0035
   bez zmian).
3. **Historia bez zmian:** ADR-y do 0052, `CHANGELOG.md` i plany w `docs/archiwum/` zostają z dawnymi numerami (jak przy
   ADR 0035, punkt 6) — „0.12” w nich znaczy Bezpieczny start.
4. Architekturę dźwięku (usługa, kanały, urządzenie w strefie) opisze osobny ADR w kroku 1 planu 0.12.0.

## Konsekwencje
- SPEC §14 dostaje dopisek `[korekta: ADR 0053]`; `DO_ZROBIENIA.txt`, `README.md`, `RYZYKA.md`, skill, `ZALEZNOSCI.md`,
  `docs/komponenty/jadro.md` i `aplikacje.md` — nowe terminy.
- Bezpieczny start czeka jedną wersję dłużej: GRUB zostaje w ISO i na pendrive'ie do 0.13 (wpis „Tymczasowe”
  w `DO_ZROBIENIA.txt`), RYZYKA R10, R17, R21, R44 — termin 0.13.
- Plan wersji: `docs/PLAN-0.12.0.txt`.

## Rozważane alternatywy
- **Dźwięk jako kroki „Laptop” (PATCH) w 0.12 Bezpieczny start** — mieszałby dwa duże tematy w jednej wersji; zakres
  wersji ma być jednym tematem (ADR 0035, punkt 4).
- **Dźwięk po Bezpiecznym starcie (0.13)** — właściciel chce go wcześniej; Bezpieczny start nie blokuje dźwięku ani
  odwrotnie, więc kolejność jest decyzją produktową.
