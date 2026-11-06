# 0008. Pulpit, okna i pierwsze aplikacje (M4)

- Status: Przyjęty
- Data: 2026-11-06
- Dotyczy: SPEC §7 (grafika rozdzielona na procesy, „przejęcie gui nie daje dostępu do danych ani
  systemu”), §12 (IPC), §14 (M4); ADR 0006, 0007

## Kontekst
M4 wymaga pulpitu, okien, paska i pierwszych aplikacji (zegar, notatki). Właściciel ustalił
(`docs/DO_ZROBIENIA.txt`, `docs/WYGLAD.md`): główny pasek na dole, wyśrodkowany jak w Windows 11;
dźwięk, (w przyszłości) bateria i zegar na górze; styl minimalistyczno-ciepły jak Claude Code /
Anthropic; trzecia aplikacja — Ustawienia. SPEC §7: `gui` rysuje do pamięci współdzielonej bez
dostępu do GPU, `ekran` składa obraz; przejęcie `gui` nie może dawać dostępu do danych ani systemu.
Fakty sprawdzone: memfd z pieczęciami `F_SEAL_SHRINK|F_SEAL_GROW|F_SEAL_SEAL` (memfd_create(2),
fcntl(2)) nie zmieni rozmiaru pod cudzym mapowaniem (brak SIGBUS); `F_GET_SEALS` zwraca `EINVAL`
dla deskryptora, który nie jest memfd/shmem; `vm.memfd_noexec=2` wymusza `MFD_NOEXEC_SEAL`.
`RLIMIT_FSIZE` dotyczy też `ftruncate` memfd (sprawdzone: `SIGXFSZ` przy limicie 0).

## Decyzja
- **`ekran` = kompozytor i menedżer okien (mechanizm), `gui` = powłoka (polityka wyglądu).**
  Każdy klient (powłoka, aplikacja) rysuje w swoich **powierzchniach**: memfd z dwiema połówkami
  (premultiplied ARGB, podwójne buforowanie), zapieczętowanym SHRINK/GROW/SEAL i przekazanym przez
  `SCM_RIGHTS`. `ekran` mapuje je tylko do odczytu, sprawdza pieczęcie i dokładny rozmiar, składa
  scenę we **własnej kopii** (klient nie zmieni obrazu bez zatwierdzenia), rysuje cienie okien
  i kursor. Po użyciu zatwierdzonej połówki odsyła `RAMKA` — klient może rysować drugą.
- **Protokół** — ten sam komunikat 32 B co w M3 (`k1_komunikat`), kierunki `DO_EKRANU` /
  `OD_EKRANU`: powierzchnia, zatwierdzenie, pokaż/ukryj i przesuń (panele), przeciągnij, minimalizuj,
  usuń; od ekranu: powitanie (rozmiar ekranu, rola), wskaźnik, klawisz (z modyfikatorami), fokus,
  ramka, okno (lista okien — tylko dla powłoki), ustawienie.
- **Role** (nadaje nadzorca, ekran sprawdza UID nadawcy każdego pakietu): **powłoka** — tylko panele
  (warstwa nad oknami), aktywowanie i minimalizowanie cudzych okien, lista okien; **aplikacja** —
  tylko własne okna (maks. 4); zmiana ustawień — **tylko aplikacja Ustawienia**. Naruszenie roli =
  odrzucenie i licznik; 50 błędów = rozłączenie klienta.
- **Wejście:** wskaźnik do powierzchni pod kursorem (przezroczyste piksele go przepuszczają),
  niejawny chwyt przy wciśniętym przycisku, klawisze do okna z fokusem, klawisz Super do powłoki,
  kliknięcie poza panelami — informacja dla powłoki (zamyka menu). Bez okna z fokusem strzałki
  przesuwają kursor (sterowanie samą klawiaturą, zachowanie z M2).
- **Okna rysują własną ramkę** (tytuł, minimalizuj, zamknij — `src/przybornik/k1_okno.c`);
  przeciąganie: aplikacja prosi `PRZECIAGNIJ`, ekran przesuwa okno tylko przy wciśniętym przycisku
  na tym oknie i trzyma pasek tytułu w zasięgu ekranu.
- **Aplikacje to osobne procesy w klatkach** (ADR 0007), uruchamiane przez nadzorcę na prośbę `gui`
  (kanał fd 4, `URUCHOM`, `WYLACZ`, nadawca sprawdzany): Zegar (UID 200), Notatki (201 — jedyna
  z dostępem do plików: Landlock tylko `/dane_uzytkownika/notatki`), Ustawienia (202). Nie są
  restartowane (zamknięcie = koniec). Do M10 (podpisane obrazy aplikacji w `/aplikacje`) leżą
  w obrazie rdzenia: `/rdzen/aplikacje/`.
- **Ustawienia** (motyw, sekundy na pasku, prędkość myszy, tapeta) trzyma `ekran` w pamięci
  i rozsyła wszystkim klientom; trwały zapis — z Blob Vault (M5).
- **Przybornik** (`src/przybornik/`): rysowanie (SDF, wygładzanie), tekst z czcionek bitmapowych
  (ADR 0009), ikony liniowe, motywy, polski układ klawiatury (programisty, AltGr), czas polski bez
  bazy stref (reguła UE dla CET/CEST), klient protokołu i szkielet aplikacji.
- **Zegar systemowy:** jądro czyta zegar sprzętowy (CMOS RTC) jako UTC przy starcie
  (`RTC_HCTOSYS`); usługi nie mają dostępu do `/dev/rtc`.

## Konsekwencje
- Przejęta powłoka **nie widzi treści okien** (składa je ekran), nie ma plików, urządzeń, gniazd
  ani nowych procesów; może: przestawiać i minimalizować okna, prosić o uruchomienie jednej z trzech
  aplikacji i o wyłączenie komputera (RYZYKA R27).
- Przejęta aplikacja ma tylko swoje okna i (Notatki) swój katalog. Może narysować w oknie cokolwiek,
  także imitację okna systemowego (RYZYKA R26).
- Pełne przerysowanie dużych obszarów w emulacji TCG trwa dziesiątki ms — akceptowalne; na sprzęcie
  znacznie szybciej. Brak akceleracji GPU (SPEC §7).
- Ustawienia i notatki giną po wyłączeniu do M5 (RYZYKA R6).

## Rozważane alternatywy
- Jedna powłoka rysująca też aplikacje — przejęcie `gui` dawałoby dostęp do danych notatek.
- Ekran rysujący ramki okien (tytuły) — wymagałby tekstu i czcionek w najbardziej uprzywilejowanym
  procesie grafiki; ramki po stronie aplikacji są prostsze, ryzyko podszywania opisane (R26).
- Wayland/X11 — SPEC §4: świat natywny bez nich.
- LVGL — zbędny przy kilku prostych kontrolkach; własny przybornik ~1500 linii.
