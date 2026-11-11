# 0026. Pulpit (0.7.5) i Pliki (0.8.0) przed Siecią

- Status: Przyjęty (kolejność i zakres; szczegóły techniczne — przy implementacji, w tym ADR albo nowym; mapa drogi według wersji: ADR 0035)
- Data: 2026-11-09
- Dotyczy: ADR 0019 (kolejność etapów — **zmienia ją po 0.7.3**), ADR 0020 (pliki ze stref), SPEC §7
  (dekodery), §9 (Blob Vault, Gatekeeper), §14

## Kontekst
Po 0.7.3 (koniec porządków, ADR 0023) następnym etapem miało być M7 „Sieć”. Właściciel zapytał
(2026-11-09), czy nie lepiej najpierw zrobić pliki i izolację aplikacji, a dopiero potem podłączać
system do internetu, i zauważył braki na pulpicie: ikony aplikacji na pulpicie, Kosz, odpinanie
z dolnego paska — czyli „część plikową”.

Analiza:
- **„Izolacja przed internetem” K1 już spełnia.** Każda aplikacja natywna jest w klatce od M3
  (osobny UID, seccomp, Landlock, cgroup, brak ścieżek). M7 nie daje internetu żadnej aplikacji —
  dodaje usługę DHCP i czasu w klatce. Jedyny program, który naprawdę dotknie sieci (przeglądarka),
  przychodzi w M8, od razu w mikro-maszynie. Uwaga: dziś jądro nie ma `CONFIG_INET` — M7 to pierwszy
  moment, gdy z zewnątrz da się coś „powiedzieć” systemowi; mała, ale prawdziwa zmiana.
- **Część plikowa nie zależy ani od sieci, ani od stref**, za to strefy zależą od niej: w ADR 0020
  pliki ze stref idą do aplikacji Pliki („Do K1”, „Wyślij do…”). Sposób, w jaki K1 trzyma dokumenty
  użytkownika i daje je aplikacjom (dziś każda aplikacja widzi w skarbcu tylko swoje obiekty, wspólnych
  dokumentów nie ma), trzeba zaprojektować przed strefami, a nie dokładać po nich.
- **Widoczność:** sam kabel z DHCP nic nie zmienia dla użytkownika, dopóki nie ma przeglądarki; pulpit,
  Kosz i Pliki właściciel oceni od razu na X230.
- **Testowalność:** pulpit i pliki testują się w obecnych testach QEMU (TCG); M8 wymaga KVM, którego
  kontener deweloperski nie ma.
- Wraca pominięte M6 ze SPEC §14 („dekodery w izolacji”, np. tapeta z PNG): bezpieczna droga dla obcych
  plików powstaje i jest testowana na pendrivie, zanim przyjdą pliki z internetu.

## Decyzja
1. **Nowa kolejność:** 0.7.3 → **0.7.5 Pulpit** → **0.8.0 Pliki** → M7 Sieć → M8 Strefy →
   **M9 Pliki ze stref** (reszta dawnego M9) → M10 → M12. Numery wersji wybrał właściciel; 0.7.4 zostaje
   wolne na ewentualne poprawki po testach na X230. Numery etapów M7–M12 bez zmian.

2. **0.7.5 Pulpit** — pulpit **jak w Windows, ale estetycznie, w stylu K1** (decyzja właściciela;
   `docs/WYGLAD.md`: neutralne tła, jeden akcent, siatka 4 px, Plex, ikony liniowe):
   - **ikony na pulpicie** w siatce: aplikacje (i od 0.8.0 pliki oraz Kosz); zaznaczanie, przeciąganie
     na inne miejsce, podwójne kliknięcie otwiera; położenia zapamiętane;
   - **menu pod prawym przyciskiem myszy** (nowy element przybornika): na pulpicie (np. „Tapeta…”,
     „Ułóż ikony”), na ikonie (Otwórz, Przypnij do paska / Odepnij, Usuń z pulpitu), na dolnym pasku
     (Odepnij z paska, Zamknij okno);
   - **przypinanie i odpinanie** aplikacji na dolnym pasku, zmiana kolejności przeciąganiem; uruchomiona
     aplikacja spoza paska pokazuje się na nim, dopóki działa (jak w Windows);
   - układ pulpitu i paska **trwały** (skarbiec). Kierunek techniczny: gui staje się klientem skarbca
     z własnym właścicielem (widzi tylko swój obiekt układu); dokładny protokół — przy implementacji;
   - przed implementacją **szkice do wyboru** dla właściciela (jak przy ekranie hasła).

3. **0.8.0 Pliki** — natywna część dawnego M9 (bez stref):
   - **wspólne dokumenty użytkownika w skarbcu**: obiekt ma nazwę (dłuższą niż dzisiejsze 32 bajty),
     typ rozpoznany z zawartości (nie z rozszerzenia), rozmiar, czas, pochodzenie (np. „pendrive”);
     metadane osobno od treści (SPEC §9); obiekty nigdy wykonywalne. Foldery czy etykiety (SPEC §9:
     magazyn obiektów zamiast drzewa katalogów) — pytanie do właściciela na starcie 0.8.0;
   - **aplikacja Pliki** (nowy UID): lista i siatka, podgląd, zmiana nazwy, usuwanie do Kosza; miejsce
     „Pulpit” = pliki widoczne na pulpicie;
   - **Kosz** (decyzja właściciela): usunięte trafiają do Kosza i są **usuwane na zawsze po 30 dniach**
     **albo ręcznie** („Opróżnij Kosz”, „Usuń na zawsze”); można je przywrócić. Ikona Kosza na pulpicie.
     Liczy skarbiec według zegara komputera; gdy zegar się cofnął, nic nie usuwa (bezpieczniej zostawić
     niż skasować za wcześnie). Uczciwy opis: „usunięte na zawsze” = niedostępne i zaszyfrowane; miejsce
     na dysku jest nadpisywane przy kompaktowaniu skarbca, nie od razu;
   - **systemowe okno wyboru pliku** rysowane przez K1 (nie przez aplikację): aplikacja dostaje tylko
     plik wybrany przez użytkownika (deskryptor / memfd z pieczęciami), nigdy listę ani ścieżki
     (SPEC §9 „dostęp przez capability”);
   - **Notatki** jako pierwsza aplikacja na wspólnym modelu (istniejące notatki przeniesione; widoczne
     w Plikach, usuwane do Kosza) — do potwierdzenia przy implementacji;
   - **import z pendrive'a**: tylko po wyborze użytkownika, z zapisanym pochodzeniem; pendrive (FAT)
     czytany **małym programem w klatce**, bez obsługi FAT w jądrze (jądro dalej nie montuje obcych
     systemów plików — RYZYKA R34);
   - **dekoder obrazów (PNG, JPEG) w jednorazowym procesie** (SPEC §7: seccomp, limity, wejście jako
     deskryptor, wyjście jako memfd z pieczęciami; testy i fuzzing): podgląd w Plikach i **tapeta
     z własnego zdjęcia**;
   - partycja K1-DANE na pendrivie powiększona do końca nośnika (dziś 64 MiB — za mało na zdjęcia).

4. **M7 Sieć** bez zmian zakresu (kabel, DHCP, czas z sieci; potem Wi-Fi), z dodatkowym testem:
   **nic w K1 nie odpowiada na połączenia z zewnątrz** (brak nasłuchujących gniazd).

5. **M9 Pliki ze stref** (po M8): folder „Do K1” → okno K1 z pytaniem → Pliki, „Wyślij do…” w Plikach,
   schowek między strefami za każdym wklejeniem, pobrane z przeglądarki przez Gatekeepera (ADR 0020 p. 5).

## Konsekwencje
- Sieć i przeglądarka przesuwają się o dwie wersje; w zamian strefy dostają gotowe Pliki, a bezpieczna
  droga dla obcych plików (czytnik pendrive'a, dekoder w izolacji, zgoda użytkownika) jest sprawdzona,
  zanim przyjdą pliki z internetu.
- Nowe powierzchnie ataku, każda z testami negatywnymi i fuzzingiem: czytnik FAT, dekodery PNG/JPEG,
  okno wyboru pliku (aplikacja nie może go podrobić — rysuje je K1), gui jako klient skarbca (przejęte
  gui widzi tylko układ pulpitu). Wpisy w `docs/RYZYKA.md` — przy implementacji.
- `docs/DO_ZROBIENIA.txt`, `README.md`, `AGENTS.md` i skill `kons-dev` pokazują nową kolejność.
