# 0035. Mapa drogi według wersji; jedno miejsce na plan

- Status: Przyjęty
- Data: 2026-11-11
- Dotyczy: SPEC §14 (kamienie milowe — **zastępuje listę etapów M i dopiski z ADR 0011, 0019, 0020, 0024–0026**),
  `docs/ARCHITEKTURA.md` (wersjonowanie), `docs/DO_ZROBIENIA.txt`, `docs/PLAN-0.8.2.txt`, `AGENTS.md`, skill `kons-dev`

## Kontekst
Przegląd planu (2026-11-11, prośba właściciela: „wydaje mi się, że panuje tu chaos”) pokazał, że kod jest uporządkowany
(wszystkie pliki i funkcje w limitach, bez wyjątków), a chaos jest w dokumentach planu:
- **numery etapów M zmieniały znaczenie** pięć razy (ADR 0011, 0019, 0020, 0024–0026). W SPEC §14 M6 to nadal
  „dekodery”, M10 „Secure Boot”, choć oba weszły do innych etapów; zmiany kolejności są jednym, długim dopiskiem;
  obok działają numery wersji (0.7.5, 0.8.0…), które tych etapów nie mają;
- **ten sam stan w sześciu miejscach** (`AGENTS.md` „Teraz”, skill, `DO_ZROBIENIA.txt`, `PLAN-0.8.1.txt`,
  `PLAN-0.8.2.txt`, ADR) — część zostaje w tyle; luki do kroków 0.8.2 były opisane w planie 0.8.1, kroki 5–10 dostały
  nowe numery 2–7;
- **`DO_ZROBIENIA.txt` mieszał historię z planem** (342 wiersze): siedem nie zamkniętych list testów na X230 (0.5.8 –
  0.8.1), punkty już zrobione, odnośniki do usuniętych plików, etapy, których już nie ma (M5.1, M11);
- **zakres wersji rośnie**: 0.8.0 miało 5 kroków, 0.8.1 — 10 (4 zrobione), 0.8.2 — znowu 7; Sieć przesuwa się
  z każdą wersją.

Decyzje właściciela z 2026-11-11: (A) najpierw porządek w dokumentach, potem skarbiec, potem reszta 0.8.2;
(B) zakres 0.8.2 zamrożony, po nim Sieć; (C) numery wersji zamiast etapów M; (D) 0.8.1 uznana za przetestowaną
na X230.

## Decyzja
1. **Etap = wersja MINOR z tematem.** Nowe dokumenty mówią „0.9 Sieć”, nie „M7”. Mapa drogi:

   | Wersja | Temat | Dawniej | Stan |
   |---|---|---|---|
   | 0.1–0.4 | start, ekran, klatki usług, pulpit i aplikacje | M0–M4 | gotowe |
   | 0.5 | przegląd, Zasoby; dyski, USB, skarbiec, hasło, instalator | M4.1, M5 | gotowe |
   | 0.6 | Fundament; podpisy i dm-verity | 0.6.0, M6a–b | gotowe |
   | 0.7 | A/B i Secure Boot, porządki, Kalkulator, Pulpit | M6c–d, 0.7.1–0.7.6 | gotowe |
   | **0.8** | **Pliki** (0.8.0 Pliki, 0.8.1 i 0.8.2 dopracowanie) | natywna część M9, dawne M6 „dekodery” | **teraz: 0.8.2** |
   | 0.9 | Sieć: kabel, DHCP, czas z sieci, potem Wi-Fi | M7 | następne |
   | 0.10 | Strefy: obca aplikacja w mikro-maszynie, zaufana ramka, przeglądarka, wspólna strefa, Przeglądarka K1 | M8 (z dawnym M11) | planowane |
   | 0.11 | Pliki ze stref: „Do K1”, „Wyślij do…”, schowek, zapis z powrotem | M9 | planowane |
   | 0.12 | Bezpieczny start — reszta: tryb ratunkowy, TPM, odwoływanie starych wersji, los GRUB-a | M10 | planowane |
   | 0.13 | Audyt: fuzzing, testy izolacji, analiza statyczna, dokumentacja końcowa → 1.0 | M12 | planowane |
   | (PATCH) | Laptop: i915, klawisze Fn, dźwięk, przewijanie touchpadem — małe kroki po zgłoszeniach | M5.3 | w dowolnej wersji |

   Po 0.9 jest 0.10 (SemVer), nie 1.0. Kolejność wewnątrz wersji ustala jej plan. Zmiana tej mapy = nowy ADR.
2. **Wersjonowanie:** MINOR — początek nowego tematu z mapy; PATCH — kolejne wydania w temacie (kroki planu wersji,
   poprawki, jądro, kroki „Laptop”); dokumentacja, CI i repo — bez zmiany wersji.
3. **Jedno miejsce na plan:**
   - `docs/DO_ZROBIENIA.txt` — mapa drogi, co teraz, co w kolejnych wersjach, otwarte decyzje i działania właściciela,
     rozwiązania tymczasowe, otwarte decyzje techniczne. Bez historii (ta jest w `CHANGELOG.md` i ADR-ach);
   - `docs/PLAN-<wersja>.txt` — plan bieżącej wersji, **samodzielny**: luki, kroki i ich stan, decyzje do niej.
     Kroki nie zmieniają numerów; zakończony plan przechodzi do `docs/archiwum/`;
   - `AGENTS.md`, skill `kons-dev` i `README.md` tylko na nie wskazują (jedno zdanie), nie powtarzają stanu.
4. **Zakres wersji zamrożony**, gdy jej plan jest przyjęty: nowy pomysł trafia do „Później” w `DO_ZROBIENIA.txt`;
   wyjątek — błąd, przez który giną dane albo system nie startuje.
5. **Testy właściciela na prawdziwym sprzęcie:** jedna lista dla ostatniego wydania, w `DO_ZROBIENIA.txt`.
   Wydania do 0.8.1 włącznie uznane za przetestowane (decyzja D). Nowa lista — przy wydaniu 0.8.2.
6. **Stare nazwy zostają w historii:** ADR-y 0001–0034 i `CHANGELOG.md` nie są przepisywane; do tłumaczenia „M7” na
   „0.9” służy tabela z punktu 1.

## Konsekwencje
- SPEC §14 opisuje wersje (dopisek `[korekta: ADR 0035]`); dawna lista M0–M12 jest w historii git i w tabeli wyżej.
- `DO_ZROBIENIA.txt` przepisany od nowa; `docs/PLAN-0.8.1.txt` → `docs/archiwum/PLAN-0.8.1.txt`, a luki i próby
  potrzebne w 0.8.2 przeniesione do `docs/PLAN-0.8.2.txt`.
- Kolejne ADR-y z planu 0.8.2 dostają numery: **0036** — format skarbca na duże dane (krok 1, dawniej planowany jako
  0035 „indeks skarbca”), **0037** — aplikacja Podgląd (krok 4, dawniej 0036).
- Przegląd pokazał też dokumenty, które nie nadążały (spis ADR-ów bez 0034, mapa w `ARCHITEKTURA.md` bez
  dekodera, `RYZYKA.md` „stan na 0.4.0”) — poprawione razem z tym ADR-em. Żeby nie wracało: `scripts/sprawdz-czytelnosc.py`
  dostaje dwa sprawdzenia — każdy ADR jest w spisie `docs/adr/README.md`, a każda ścieżka `docs/…` wymieniona w żywych
  dokumentach istnieje (plan 0.8.2, krok 1; zmiana skryptu testów idzie z pełnymi testami, nie w tym PR-ze).

## Rozważane alternatywy
- **Zostawić etapy M i poprawić tylko SPEC §14** — numery M dalej nie pasowałyby do wersji, które widzi właściciel
  (wydania, linki do ISO), a każda kolejna zmiana kolejności znowu przesuwałaby ich znaczenie.
- **Jeden wielki plik planu** (wszystkie wersje z krokami) — rósłby bez końca; plan bieżącej wersji jest długi
  (luki, kroki, decyzje) i po wydaniu przestaje być potrzebny na co dzień.
