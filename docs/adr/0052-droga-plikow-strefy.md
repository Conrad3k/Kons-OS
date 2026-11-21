# 0052. Droga plików strefa ↔ K1: port plików, agent w strefie, zgoda w oknie K1, zapis przez nadzorcę

- Status: Przyjęty; wdrożenie w 0.11.0 — krok 2 (pliki ze strefy do Plików), uzupełniany w krokach 3–5
- Data: 2026-11-21
- Dotyczy: ADR 0027 (przepływ plików: Pliki jedynym węzłem, zgoda = gest w K1), ADR 0042 (strefy, VMM), ADR 0043
  (przeglądarka w strefie, Raptor Bridge), ADR 0029 (pliki w skarbcu), ADR 0034 (limity, zapis w toku kawałkami);
  plan `docs/PLAN-0.11.0.txt`: krok 2, T1–T4, luki P5–P14, decyzje właściciela D2 „A”, D3 „A”; RYZYKA R102

## Kontekst
Do 0.10.11 przeglądarka w strefie była szczelna w obie strony: plik pobrany ze strony zostawał w sesji. Strefa ma tylko
dwa porty vsock do swojego VMM (1 — kanał sterujący, 2 — połączenia przeglądarki, ADR 0043), a dysk sesji to obraz ext4
w kawałkach skarbca — K1 nie czyta z niego plików i nie powinien (ext4 ze strefy to niezaufany format, P5). ADR 0027
ustalił zasady (oryginał w Plikach, strefa dostaje kopię, zgoda gestem w oknie K1, osobne pytanie, gdy przeniesienie
zaczyna sama strefa); brakowało drogi bajtów i formatu.

## Decyzja
1. **Port plików (T1).** Trzeci port vsock VMM-a: `K1_PS_PORT` = 3, jedno połączenie naraz (drugie dostaje RST), z kredytem
   jak port 2 (`K1_PS_BUFOR` = 64 KiB). Bez wspólnego systemu plików (virtio-fs, 9p: duża powierzchnia po stronie K1, FUSE
   w gościu, demon na hoście). Po połączeniu płyną **ramki** (`src/wspolne/k1_pliki_strefy.h`, czyste, test i fuzzing):
   nagłówek 8 B (typ u16, zero u16, długość u32, little-endian), ramka najwyżej 8 + 4 + 64 KiB.
   - strefa → K1: `ZGLOS` (numer pliku w strefie, rozmiar u64, nazwa 1…255 B) — plik gotowy do przeniesienia;
     `DANE` (numer, 1…64 KiB) — bajty, tylko po `WYSLIJ`, po kolei, razem dokładnie tyle, ile zgłoszono; `BRAK` (numer) —
     pliku nie da się wysłać (zniknął, zmienił się).
   - K1 → strefa: `WYSLIJ` (numer) — po zgodzie użytkownika; `WYNIK` (numer, wynik: zapisany, zostaje w przeglądarce, za
     mało miejsca, za duży, błąd, odmowa — za dużo czeka).
   Każde złamanie zasad (zły typ, długość, numer, bajty bez `WYSLIJ` albo ponad rozmiar, więcej niż kredyt) zamyka
   **tylko połączenie plików** (RST) i przerywa wszystko, co z niego czekało — strefa i K1 działają dalej (kredyt ponad
   ogłoszony kończy maszynę jak na każdym porcie). Najwyżej `K1_PS_CZEKA` = 4 zgłoszenia czekają naraz; kolejne dostają
   `WYNIK` „odmowa” (zalew).
2. **Agent w strefie (T2).** Nasz program `src/strefa/agent/` (statycznie, jak przekaźnik), jedyny w strefie, który zna port
   plików. Uruchamia go `/init` strefy (gosc) obok przeglądarki, jako użytkownik przeglądarki. Pilnuje folderu pobranych
   (inotify): Firefox ESR pisze pobranie do `<nazwa>.part` i na końcu zmienia nazwę (P10) — agent zgłasza plik po
   `IN_MOVED_TO` (i po `IN_CLOSE_WRITE` pliku, który nie jest `.part` i nie jest pusty — „Zapisz stronę”); bajty wysyła
   dopiero na `WYSLIJ`, otwierając plik w swoim folderze (`openat` + `O_NOFOLLOW`, bez „..”) i sprawdzając, że rozmiar się
   nie zmienił. Folder pobranych jest stały i zablokowany w ustawieniach przeglądarki (`strefy/przegladarka/k1.js`:
   `browser.download.dir`, `folderList` 2, `useDownloadDir` — Firefox nie pyta, gdzie zapisać; P9). Agentowi K1 nie ufa:
   on tylko oszczędza użytkownikowi szukania pliku — wszystko, co mówi, sprawdza VMM i nadzorca.
3. **Zgoda (ADR 0027 p. 2, D2 „A”).** Przeniesienie zaczyna strefa, więc K1 pyta **przy każdym pobraniu**: okienko gui
   (rysuje K1 na swoim panelu — strona go nie podrobi) z sesją (kropka w jej kolorze), nazwą i rozmiarem: „Zostaw
   w przeglądarce” / „Zapisz w Pobranych”. Okienko nie znika po kliknięciu obok (wybór jest obowiązkowy); zasłonięte
   menu wraca, gdy menu się zamknie. Kilka pobrań — po kolei. Plik ponad `K1_DOK_MAKS_BAJTY` (4 GiB): okienko mówi „za
   duży”, tylko „OK”. Za mało miejsca albo przerwanie: okienko „Nie zapisano pliku” z powodem; plik zostaje w przeglądarce.
4. **Zapis przez nadzorcę (T3).** VMM nie dostaje kanału do Plików ani do skarbca poza swoim dyskiem sesji: VMM → nadzorca
   (`STREFA_PLIK`, `STREFA_PLIK_KAWALEK`, `STREFA_PLIK_KONIEC` na swoim fd 7; odpowiedzi `STREFA_PLIK_ODP`) → skarbiec.
   Nadzorca ma do tego osobny kanał skarbca z właścicielem `K1_WL_POBRANIA` (102): w tabeli praw skarbca **tylko nowy plik
   w folderze Pobrane z pochodzeniem strefy** — nic nie czyta, nic nie zmienia, nic nie widzi. Bajty idą kawałkami
   `K1_DOK_KAWALEK` (1 MiB) w zapieczętowanym memfd, każdy po potwierdzeniu poprzedniego przez skarbiec (VMM wstrzymuje
   kredyt strefy, póki kawałek nie jest zapisany) — zapis w toku z 0.8.1: przerwanie (strefa zamknięta, brak miejsca,
   błąd ramki) nie zostawia w Plikach nic połowicznego. Jedno przenoszenie naraz w całym K1 (następne czeka). Nazwę
   naprawia VMM zasadami nazw K1 (`k1_dok_nazwa_popraw`: ukośnik, znaki sterujące → „_”), nadzorca i skarbiec sprawdzają
   ją jeszcze raz; zajęta → „nazwa (2)”. Typ pliku rozpoznaje skarbiec z zawartości (`K1_TYPY`). W logach tylko liczby
   („strefa N, plik M, X B”) — nigdy nazwa.
5. **Pochodzenie (T4, D3 „A”).** Bajt `pochodzenie` wpisu pliku: obok K1 i pendrive'a pochodzenia stref
   `K1_POCH_STREFA_OD + numer strefy` (wyprowadzone z `K1_STREFY` — nowa kolumna „skąd”, np. „z przeglądarki”), w Plikach:
   „z przeglądarki · Ogólna” i data. **Adresu strony nie ma** (P12, sprawdzone w `libxul.so` Firefoksa ESR 153 z obrazu
   strefy): Firefox na Linuksie zapisuje go tylko przez GIO (`metadata::download-uri`, demon gvfs) — w strefie go nie ma,
   atrybutów `user.xdg.*` Firefox nie pisze. Strefa mogłaby zresztą skłamać; miejsce na adres (P13) — dopiero gdy będzie
   wiarygodne źródło (Przeglądarka K1, ADR 0025). Starsza wersja K1 (po „Wróć do poprzedniej wersji”) czyta nieznane
   pochodzenie jako „utworzony w K1” (P14) — plik zostaje.
6. **Sesja Prywatna** (zapomina) trzyma pobrane w pamięci strefy (P11): ogranicza je pamięć sesji; po „Zapisz w Pobranych”
   plik jest w Plikach jak z każdej sesji.

## Konsekwencje
- Pierwsza droga bajtów ze strefy do danych użytkownika: nadzorca dostaje od VMM tylko komunikaty 32 B i zapieczętowane
  memfd, a skarbiec — tylko nowy plik w Pobranych. Niezaufany jest format ramek (VMM), nazwa (VMM, nadzorca, skarbiec)
  i treść (skarbiec rozpoznaje typ; pliki z internetu otwiera K1 w klatkach jak każde inne — dekoder, Notatki).
- Ryzyko: użytkownik zapisze w Plikach złośliwy plik. Ogranicza je to, że pliki K1 czytają programy w klatkach (dekoder
  obrazów, Notatki tylko tekst), a PDF i inne pliki otwiera się w strefie (krok 3) — RYZYKA R102.
- Kroki 3–5 tego planu używają tej samej drogi w drugą stronę (pliki do strefy), dla przeciągania i schowka —
  uzupełnienia tego ADR.
