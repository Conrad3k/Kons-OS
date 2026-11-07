# 0014. Wskaźnik baterii na górnym pasku

- Status: Przyjęty
- Data: 2026-11-07
- Dotyczy: SPEC §7 (nadzorca, powłoka), §12 (protokół), ADR 0007, ADR 0008, ADR 0013

## Kontekst
Właściciel (2026-11-07): „w ramach tej wersji dodać wskaźnik baterii, jeśli system wykryje, że to
laptop” (wcześniej: „na górze dźwięk, (w przyszłości) bateria i zegar”). Pierwszy laptop to ThinkPad
X230 (ADR 0013).

Fakty: jądro podaje stan baterii przez klasę `power_supply` w sysfs (sterowniki ACPI `battery`
i `ac`). Na laptopach metody ACPI baterii czytają dane przez **Embedded Controller** — w naszej
konfiguracji `ACPI_EC` był wyłączony (allnoconfig), więc bez niego bateria X230 byłaby niewidoczna
(`drivers/acpi/Kconfig`: `ACPI_EC`, domyślnie tak dla x86, ale allnoconfig wyłącza). QEMU nie ma
urządzenia baterii; można jednak dodać maszynie własną tablicę ACPI (`-acpitable`) z baterią
i zasilaczem — jądro widzi je jak na laptopie.

## Decyzja
1. Jądro: `ACPI_EC`, `ACPI_AC`, `ACPI_BATTERY` (wybierają `POWER_SUPPLY`); bez hwmon i debugfs EC.
2. **Stan czyta nadzorca** (root, ma sysfs) co 15 s z `/rdzen/jadro/sys/class/power_supply`
   (`src/nadzorca/bateria.c`): baterie systemowe (bez `scope=Device` — np. bateria myszy, bez
   `present=0`), procent z `energy_*` (µWh), `charge_*` (µAh) albo `capacity`, stan
   z `status`, zasilacz z `online` typu `Mains`. Każda liczba sprawdzana (same cyfry, zakres),
   napisy tylko porównywane. Odczyt baterii ACPI rozmawia z EC — stąd rzadko.
3. Powłoka dostaje stan **nowym, jednokierunkowym kanałem fd 5** (wzór: Zasoby, fd 4):
   `k1_komunikat BATERIA` (`a` = jest bateria, `b` = procent 0..100 albo −1, `c` = stan, `d` =
   zasilacz), wysyłany tylko przy zmianie (po starcie gui — zawsze). Nadzorca nic z tego kanału nie
   czyta (`SHUT_RD`), gui sprawdza nadawcę (UID 0) i każde pole. Powłoka nadal nie czyta plików.
4. Wygląd: na górnym pasku przed ikoną dźwięku — [błyskawica przy ładowaniu] „73%” [ikona baterii
   z wypełnieniem]; poniżej 16 % przy rozładowywaniu wypełnienie w kolorze akcentu. Bez baterii
   (komputer stacjonarny, maszyna wirtualna) — nic.

## Konsekwencje
- Nadzorca czyta dane pochodzące z firmware (EC/ACPI) — mały parser z testami na hoście
  (`test_bateria`, także śmieciowe i skrajne wartości); RYZYKA R36.
- EC w jądrze: więcej kodu ACPI aktywnego na laptopach, ale bez niego wiele metod ACPI laptopa nie
  działa (bateria, w przyszłości przyciski i pokrywa).
- Testy: tablica ACPI `testy/acpi/bateria.asl` (kompilowana `iasl` z `acpica-tools`) — w teście
  w systemie bateria 73 % rozładowywana, w `test-m5.sh` ładowana z zasilaczem (log powłoki + zrzut);
  w scenariuszu `start` bez baterii — „brak baterii”.
- Na X230 (bateria główna i ewentualnie dodatkowa „slice”) — procent łączny ważony pojemnością.
  Czas do rozładowania — na razie nie pokazujemy (później, np. w okienku po kliknięciu).

## Rozważane alternatywy
- **Powłoka czyta sysfs sama** — wymagałaby otwierania plików (dziś Landlock bez żadnej ścieżki
  i seccomp bez `open`); przejęta powłoka zobaczyłaby więcej systemu.
- **Komunikat na istniejącym kanale fd 4** (gui → nadzorca) — mieszałby kierunki w jednym kanale;
  osobny jednokierunkowy kanał jest prostszy do sprawdzenia.
- **Powiadomienia jądra (uevent) zamiast co 15 s** — razem z hotplugiem w M5b, jeśli się przyda.
