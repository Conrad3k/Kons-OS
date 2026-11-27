# 0064. Aplikacje ze Sklepu jako wersja 0.14; Audyt → 0.15

- Status: Przyjęty (kierunek); wdrożenie — plan `docs/PLAN-0.14.0.txt`
- Data: 2026-11-27
- Dotyczy: ADR 0020 (strefy: każda obca aplikacja w swojej mikro-maszynie — tu: skąd się bierze), ADR 0024 (wspólna
  strefa — bez zmian, „Później”), ADR 0035 i 0053 (mapa drogi — **zmienia jej tabelę**), ADR 0046, 0047, 0056 (droga
  wydań, robot, łańcuch wydań — Sklep korzysta z tej samej drogi), SPEC §14, `docs/DO_ZROBIENIA.txt`, `README.md`,
  `docs/RYZYKA.md` (kolumna „Plan”), skill `kons-dev` §8

## Kontekst
ADR 0020 (pomysł właściciela z 2026-11-07) ustalił, że każda obca aplikacja — przeglądarka, Discord, Steam — działa we
własnej mikro-maszynie wirtualnej. Nie ustalił, **skąd** taka aplikacja trafia do K1. Dziś jest jedna: przeglądarka,
której obraz (SquashFS z drzewem dm-verity) buduje CI z tabeli pakietów `strefy/przegladarka.txt` i wkłada do miejsca A/B
razem z `k1.efi` (ADR 0050); wiersze jej sesji są w tabeli `K1_STREFY` (`src/wspolne/k1_uslugi.h`).

Rozmowa z właścicielem 2026-11-27 (w trakcie planu 0.13.0):
- „aplikacje trzecie można pobrać tylko z wbudowanego »sklepu« w K1, który pobierałby je bezpośrednio z Repozytorium” —
  np. Discord jako osobna, dostrojona maszyna z ikoną w stylu K1; kolejne aplikacje właściciel będzie zgłaszał po jednej;
- „0.14 aplikacje przed audytem”, „bez wgrywania aplikacji z pendrive”;
- robot „mógłby” aktualizować aplikacje, ale „nie może to być dealbreaker, przez który robot się wywali” — instalatory
  takich aplikacji i tak same sprawdzają najnowszą wersję;
- pierwsza aplikacja: **Discord** („dobry benchmark”);
- pamięć: „będziemy decydować per-app, ile K1 będzie na starcie przypisywał” — Discord na początek **500 MB**.

K1 i tak nie uruchomi obcego programu w świecie natywnym (niezmiennik 1: nic zapisywalnego nie jest wykonywalne; brak
powłoki i interpreterów — niezmiennik 2). „Tylko Sklep” nie odbiera więc niczego, co dziś działa — domyka drogę.

## Decyzja
1. **0.14 = Aplikacje.** Mapa drogi (zastępuje wiersz 0.14 tabeli z ADR 0053):

   | Wersja | Temat | Dawniej |
   |---|---|---|
   | **0.14** | **Aplikacje**: Sklep w K1, obraz bazowy, aplikacje ze Sklepu w swoich strefach, pierwsza — Discord (z głosem) | — (nowe) |
   | 0.15 | Audyt: fuzzing, testy izolacji, analiza statyczna, dokumentacja końcowa → 1.0 | M12 (w ADR 0053: 0.14) |

   Audyt po Sklepie, bo Sklep to największa nowa droga, którą do K1 wchodzi coś pobranego — audyt ma go objąć od razu.
2. **Jedyna droga obcej aplikacji to Sklep.** Bez wgrywania z pendrive'a, z pliku ani spod innego adresu. Sklep to okno
   K1 (rysuje je K1, nie strefa).
3. **Aplikacja ze Sklepu = wiersz w tabeli K1 + obraz z sieci.** Wiersz (numer, UID, profil, zgody, pamięć na start,
   ikona, kolor, trwałość) jest w kodzie K1, jak dziś sesje przeglądarki w `K1_STREFY` (zasada czytelności 5). Z sieci
   przychodzi tylko **obraz** — sprawdzony skrótem i podpisem — nigdy zgody ani numery: Sklep nie da aplikacji niczego,
   czego K1 o niej nie wie. Skutek: nowa aplikacja w Sklepie = jeden PR (przepis obrazu, wiersz, ikona) i wydanie K1.
4. **Jeden łańcuch zaufania.** Obrazy aplikacji buduje CI z przepisów w repo (`strefy/<aplikacja>.txt`) i podpisuje tym
   samym kluczem co wydania (`K1_KLUCZ_PODPISU`, ADR 0021); spis obrazów (**katalog Sklepu**) idzie tą samą drogą co
   ogłoszenie wydania (ADR 0056: podpis, termin ważności, zwłoka 20 minut, strefa pobierania).
5. **Zamkniętych aplikacji nie rozpowszechniamy.** Licencje Discorda, Steama, Spotify nie pozwalają włożyć ich plików do
   publicznego repo ani wydań. Obraz takiej aplikacji ma wszystko oprócz niej samej (biblioteki, nasz program startowy);
   aplikację pobiera **z oficjalnej strony producenta** nasz program w strefie, przy starcie, gdy jest nowsza (jak
   Flathub). Ufamy wtedy stronie producenta przez HTTPS zamiast naszej sumy — i tak uruchamiamy jego program, a izoluje
   go osobna maszyna. Aplikacje otwarte mogą być w obrazie w całości.
6. **Robot pomaga, nie warunkuje.** Robot może przebudowywać obrazy aplikacji (nowe pakiety Debiana bazy), ale jego błąd
   przy aplikacji kończy się zgłoszeniem (Issue) i nie zatrzymuje wydań jądra ani przeglądarki (ADR 0047). Aplikacja,
   która aktualizuje się sama (pkt 5), robota nie potrzebuje.
7. **Pamięć na start — przy każdej aplikacji osobno** (kolumna pamięci wiersza, decyzja właściciela): Discord — 500 MiB.
   Ponad nią strefa rośnie i oddaje pamięć jak dziś (balon i rozdzielacz, ADR 0044); liczbę zmienia się po pomiarze, za
   zgodą właściciela.
8. **Obraz bazowy.** Wspólne części obrazów (biblioteki Debiana, nasze programy w strefie) są jednym obrazem dla
   przeglądarki i wszystkich aplikacji; obraz aplikacji niesie tylko swoją warstwę — mniej pobierania, mniej miejsca na
   dysku i bez podwójnej pamięci podręcznej (szczegóły: ADR kroku 1 planu 0.14.0).
9. **Przesunięcie terminów:** co w żywych dokumentach miało termin „0.14” (audyt), ma teraz „0.15”; „M12” = 0.15.
   Historia bez zmian: ADR-y do 0063, `CHANGELOG.md`, `docs/archiwum/` i zgłoszenia na GitHubie sprzed 2026-11-27 mówią
   „0.14” o audycie.

## Konsekwencje
- SPEC §14 dostaje dopisek `[korekta: ADR 0064]`; `DO_ZROBIENIA.txt`, `README.md`, `RYZYKA.md`, skill, plany 0.13.0
  i 0.13.2 — nowe terminy. Mikrofon i wspólny obraz bazowy przechodzą z „Później” do planu 0.14.0.
- Nowe ryzyka (RYZYKA — wiersze dopisze krok, który je wnosi): obraz pobrany z sieci (tu: skrót i podpis), aplikacja
  zamknięta pobierana od producenta (tu: izolacja strefy, nie sprawdzenie treści), większa powierzchnia mostu (UDP dla
  rozmów głosowych), mikrofon (zaufany znak K1, zgoda w wierszu aplikacji).
- Sklep nie jest testowany prawdziwym Discordem w CI (konto, internet) — jak przeglądarka (decyzja z 2026-11-17): CI
  sprawdza części K1 (katalog, obraz, program startowy na serwerze testowym), Discorda — właściciel na laptopie.

## Rozważane alternatywy
- **Zgody i wiersze aplikacji z katalogu (bez wydania K1)** — szybciej dodaje aplikacje, ale wtedy podpisany plik z sieci
  decyduje o UID-ach, sieci i mikrofonie; przejęte CI dałoby aplikacji więcej niż K1 przewidział. Odrzucone (pkt 3).
- **Wgrywanie aplikacji z pendrive'a albo z pliku** — odrzucone przez właściciela; omija łańcuch wydań.
- **Pliki Discorda w naszym obrazie** — prościej i z naszą sumą, ale łamie licencję; odrzucone (pkt 5).
- **Każda aplikacja jako pełny obraz Debiana** — prościej w kroku 1, ale każda niesie ok. 150 MB tej samej bazy na dysku
  i drugi raz w pamięci K1; odrzucone (pkt 8).
- **Audyt przed Sklepem (0.14 Audyt, 0.15 Aplikacje)** — audyt nie objąłby największej nowej drogi danych; odrzucone przez
  właściciela.
