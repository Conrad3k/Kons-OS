# 0048. Ramka okna strefy bez kolorowej obwódki

- Status: Przyjęty; wdrożony w 0.10.7 (krok 4)
- Data: 2026-11-19
- Dotyczy: ADR 0020 p. 3 (zaufana ramka — „obwódka w kolorze strefy”), ADR 0042 („Zaufana ramka”); plan
  `docs/PLAN-0.10.7.txt`: krok 4, D1, T5; RYZYKA R26, R50, R80

## Kontekst
Od 0.10.0 (krok 3, szkic A) ekran K1 rysował wokół okna strefy pasek tytułu i obwódkę 3 px — całe w kolorze strefy
(przeglądarka: kolor sesji Ogólna / Bank / Prywatna). Właściciel (2026-11-17, przegląd wyglądu 0.10.7): kolor obejmujący
całe okno „psuje estetykę”; okno przeglądarki ma wyglądać jak okna K1, a kolor sesji ma zostać, ale dyskretnie.

## Decyzja
Właściciel wybrał szkic A „Kropka i linia” (D1, 2026-11-18). Ekran (`src/ekran/strefy.c`) rysuje:
- ramkę jak okna K1: tło okna motywu, włoskowaty obrys, cień ekranu; obwódka zostaje 3 px (chwyty brzegów, rozmiary
  liczone przez VMM i gościa bez zmian), ale w kolorze tła okna;
- pasek tytułu `K1_STREFA_TYTUL` (40 px, bez zmian — T5): ikona strefy i napis w kolorach tekstu (bez fokusu wyciszone,
  jak w oknach K1), przyciski minimalizuj / maksymalizuj / zamknij jak w oknach K1;
- kolor strefy w jednym miejscu: kropka 10 px przed nazwą sesji (bez sesji — przed „osobna maszyna”) i linia 2 px pod
  paskiem, między paskiem a obrazem strefy; bez fokusu przygaszony jak dotąd (zmieszany z tłem);
- napis „<tytuł> · ● <sesja> · osobna maszyna”, sesja jednorazowa: „· zapomina” (dawniej „· zapomina po zamknięciu”
  i jasny pasek w kolorowej obwódce).
Ekran startu przeglądarki (odcinek w kolorze sesji, 0.10.2) — bez zmian.

## Dlaczego okno strefy dalej różni się od okna K1 (T5)
Zasada ADR 0020 p. 3 zostaje: ramkę rysuje ekran, **na zewnątrz** prostokąta strefy i przed nim; strefa ma tylko swój
prostokąt, więc nie zasłoni ani nie podrobi ramki (test negatywny w `test-strefa.sh`). Okno strefy rozpoznaje się po:
napisie „osobna maszyna” (oznaczenie bezpieczeństwa, nie opis — zasada Z2 planu go nie usuwa), ikonie strefy, kropce
i linii w kolorze sesji — żadne okno K1 ich nie ma. Zmienia się tylko to, ile koloru jest na ramce, nie kto ją rysuje.

## Skutki
- ADR 0020 p. 3 i ADR 0042 („Zaufana ramka”): dopisek `[korekta: ADR 0048]`.
- RYZYKA R80, R50: opis ramki. Wzorce `ramka-strefy-jasny`, `-ciemny` — nowe obrazy po „OK” właściciela.
- `test-strefa.sh` sprawdza tło okna wokół obrazu i linię w kolorze strefy pod paskiem (dawniej obwódkę w kolorze).
