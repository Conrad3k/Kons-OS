# 0024. Wspólna strefa na drobne, zaufane aplikacje

- Status: Przyjęty (kierunek; szczegóły techniczne — w ADR etapu M8)
- Data: 2026-11-08
- Dotyczy: ADR 0020 (strefy — rozszerza punkt 4 „grupy”), SPEC §8, RYZYKA R26

## Kontekst
ADR 0020: każda obca aplikacja w osobnej mikro-maszynie; „grupy” (kilka aplikacji w jednej
maszynie) tylko na wyraźne życzenie. Każda maszyna to osobne jądro i system — na X230 (8 GB) to
ok. 1–2 GB na maszynę, więc naraz zmieszczą się tylko nieliczne. Drobne programy (np. edytor, kalkulator
naukowy, odtwarzacz muzyki) nie potrzebują aż takiej izolacji jak przeglądarka. Rozmowa z właścicielem
2026-11-08: rozważone były też kontenery na jądrze K1, gVisor, przeglądarka natywna bez JIT, lekkie
przeglądarki natywne i przeglądarka zdalna; właściciel wybrał **wspólną strefę** jako uzupełnienie planu.

## Decyzja
1. Obok osobnych stref jest **jedna wspólna strefa**: jedna mikro-maszyna (własne jądro, jak każda
   strefa), a w niej kilka aplikacji, oddzielonych od siebie lżej — przegródkami (przestrzenie nazw
   i klatki **wewnątrz jądra gościa**, nie na jądrze K1). Jądro K1 nie zmienia się (dalej bez USER_NS).
2. **Trzeci przełącznik przy aplikacji** (obok trwałości i grafiki z ADR 0020): **osobna strefa** albo
   **wspólna strefa**. Domyślnie:
   - **osobna** — przeglądarka i wszystko, co otwiera treści z internetu (poczta, komunikatory) —
     przełącznika dla przeglądarki nie ma, zawsze osobna;
   - **wspólna** — proponowana dla drobnych, zaufanych narzędzi; właściciel może to zmienić.
3. **Ramka K1** pokazuje różnicę: etykieta „wspólna strefa” i jeden wspólny kolor dla wszystkich jej
   okien (osobne strefy — każda swój).
4. Wspólna strefa jest **stała** (jednorazowa ma sens tylko jako osobna maszyna); pliki i schowek —
   jak wszędzie, tylko przez Gatekeepera (M9). Aplikacje we wspólnej strefie nie widzą nawzajem swoich
   plików, ale błąd w jądrze gościa może je połączyć — wyjaśnienie przy przełączniku.

## Konsekwencje
- Mniej pamięci przy wielu drobnych aplikacjach; na X230 przeglądarka w osobnej strefie + kilka
  narzędzi we wspólnej naraz.
- Kolejność w M8 bez zmian: najpierw osobna strefa z przeglądarką (fundament), wspólna strefa — po
  niej, z testem, że aplikacje w niej nie widzą swoich plików i okien nawzajem.
- Do rozważenia przy M8 (osobno, nie przesądzone): tryb przeglądarki bez JIT jako dodatkowe
  zabezpieczenie w jej strefie.
