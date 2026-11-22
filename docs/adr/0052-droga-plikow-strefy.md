# 0052. Droga plików strefa ↔ K1: port plików, agent w strefie, zgoda w oknie K1, zapis przez nadzorcę

- Status: Przyjęty; wdrożenie w 0.11.0 — krok 2 (pliki ze strefy do Plików), krok 3 (pliki do strefy), krok 4 (przeciąganie
  między oknami), krok 5 (schowek) — uzupełnienia niżej
- Data: 2026-11-21
- Dotyczy: ADR 0027 (przepływ plików: Pliki jedynym węzłem, zgoda = gest w K1), ADR 0042 (strefy, VMM), ADR 0043
  (przeglądarka w strefie, Raptor Bridge), ADR 0029 (pliki w skarbcu), ADR 0034 (limity, zapis w toku kawałkami);
  plan `docs/archiwum/PLAN-0.11.0.txt`: krok 2, T1–T4, luki P5–P14, decyzje właściciela D2 „A”, D3 „A”; RYZYKA R103

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

## Uzupełnienie — krok 3: pliki z Plików do strefy, zapis z powrotem (2026-11-21; D4 właściciela: A)
7. **Gest i dostęp.** „Wyślij do” (podmenu sesji) i sesje w „Otwórz za pomocą” — w menu pliku Plików i pulpitu
   (`k1_menu`, jedna lista; sesje z wierszy `K1_STREFY` z oknem i nazwą sesji). Pliki / pulpit → skarbiec `DOK_DO_STREFY`
   (prawo `P_STREFA`) → skarbiec daje kanałowi nadzorcy `K1_WL_DO_STREF` (103) dostęp do **tego jednego pliku** (`dostep.c`,
   jak przy „Otwórz za pomocą”) i mówi nadzorcy `SKARB_DO_STREFY` (z wpisem pliku w memfd). Kanał 103 w tabeli praw: czyta
   tylko dane mu pliki, kładzie nowy plik (pochodzenie strefy) tylko w folderze takiego pliku, ten plik do Kosza, nowemu
   nazwę — nic więcej (`test_dokumenty`).
8. **Droga do strefy.** Zamkniętą sesję nadzorca otwiera (D4 „A”, `strefy_do.c`); VMM dostaje `STREFA_DO_PLIK` (nazwa,
   rozmiar, „otwórz”) i kawałki 1 MiB (`STREFA_DO_KAWALEK`) na swoje `STREFA_DO_ODP DALEJ`; plik czeka na agenta do 120 s
   (agent łączy się kilka sekund po starcie maszyny, także po resecie vsock gościa). Ramki `PLIK` i `BAJTY` idą jednym
   strumieniem z resztą ramek K1 (całe ramki, po kolei, `pliki_do.c`). Agent zapisuje kopię w folderze sesji **„Z K1”**
   (`/home/k1/Z K1`: ukryty plik `.k1-<nr>` z `O_EXCL`, `O_NOFOLLOW`, na końcu zmiana nazwy na wolną — „nazwa (2).ext”…)
   i odpowiada `ODEBRANY`. Jeden plik naraz w całym K1.
9. **„Otwórz w…” bez D-Bus (P16, T5).** Firefox w strefie działa z `--no-remote`, zdalne sterowanie ma tylko przez D-Bus
   (sprawdzone w `libxul.so`: `nsDBusRemoteServer`), a w strefie D-Bus nie ma. Agent pisze jedną linię z adresem pliku
   (`file:///home/k1/Z%20K1/<nazwa w %XX>`) do kolejki FIFO kompozytora strefy (`/run/k1/otworz`, zapis tylko dla grupy
   przeglądarki); kompozytor (nasz, `kompozytor/otworz.c`) przyjmuje tylko adresy folderu „Z K1” ze zwykłych znaków,
   najwyżej jeden na sekundę, i **wpisuje** go w okno z fokusem klawiatury: Ctrl+L, adres, Alt+Enter (nowa karta) — klawisze
   rozłożone w czasie (pasek adresu dostaje fokus z opóźnieniem). Sprawdzone na stanowisku z prawdziwym Firefoksem
   (`testy/stanowisko/otworz.py`). To mechanizm wewnątrz strefy: K1 nic nie wpisuje w swoje okna.
10. **Zmieniona wersja z powrotem (ADR 0027 p. 5).** Nadzorca pamięta powiązanie *sesja + numer K1 → plik* (do wyłączenia
    K1; 64 ostatnie). Agent zgłasza `ZGLOS` z polem **dotyczy** = numer K1, gdy pobranie ma nazwę pliku z K1 albo nazwę, którą
    Firefox nadaje kolejnej kopii („nazwa(1).pdf” — tak zapisuje zmieniony PDF), albo gdy plik w „Z K1” zostanie zapisany
    jeszcze raz (P17). Nadzorca przyjmuje taką zmianę tylko dla pliku, który K1 dał **tej** sesji (inaczej odmowa bez
    okienka); okienko K1 „<sesja> zmieniła plik — Zapisać zmiany w oryginale?”: **„Zapisz”** — nowa treść jako nowy plik obok
    oryginału, oryginał do Kosza (30 dni), nowy dostaje jego nazwę (kolejne kroki: zapis w toku, `DOK_KOSZ`, `DOK_NAZWA`;
    przerwa między nimi zostawia oba pliki — nic nie ginie); **„Zapisz jako nowy plik”** — „nazwa (2).ext” obok; **„Nie”** —
    nic. Następna zmiana idzie do nowej wersji.

## Uzupełnienie — krok 4: przeciąganie między oknami, też na okno przeglądarki (2026-11-21; D6 właściciela: A)
11. **Prowadzi ekran (T6).** Tylko ekran wie, gdzie są okna. Źródło (Pliki, pulpit) mówi mu, co przeciąga
    (`PRZECIAGANIE`: rodzaj `K1_PZ_*`, liczba, foldery, pierwszy element, folder — bez nazw; tylko powierzchnia, która trzyma
    wskaźnik z wciśniętym przyciskiem). Ekran mówi źródłu, że kursor jest nad czymś innym (`PRZECIAGANIE_GDZIE`), a celowi —
    co nad nim jest (`UPUSZCZANIE_CO`, `UPUSZCZANIE`: ruch, wyjście, upuszczenie, klawisze). Źródło i cel nie rozmawiają ze
    sobą; plik przekazuje skarbiec (i nadzorca do strefy), nigdy aplikacja aplikacji. **Robi ten, kto widzi pliki:** Pliki.
    Z pulpitu na Pliki — Pliki (cel) przenoszą plik pulpitu do folderu pod kursorem albo pokazanego; z Plików na pulpit —
    pulpit odpowiada tylko, gdzie (`PRZECIAGANIE_CEL`: Pulpit, folder na pulpicie, Kosz), a ekran przekazuje to Plikom
    (`PRZECIAGANIE_KONIEC`) — pulpit widzi tylko folder Pulpit (tabela praw skarbca bez zmian). Reguły — jedna tabela
    `k1_przeciaganie` (nowy cel `K1_PC_STREFA`, nowe działania `K1_PD_KOPIUJ`, `K1_PD_DO_STREFY`).
12. **Ctrl — kopia (D6 „A”).** Przeciągnięcie przenosi (jak w Windows na jednym dysku i jak w Plikach od 0.8.2); z Ctrl
    przy puszczeniu — kopia plików (`src/aplikacje/pliki/kopia.c`: kawałkami jak import — `DOK_CZYTAJ` → `DOK_NOWY` /
    `DOK_KAWALEK` → `DOK_KONIEC`; wolna nazwa „nazwa (2)” od skarbca, pochodzenie oryginału zostaje). Folderów się nie
    kopiuje (odmowa z tabeli: „kopiować możesz tylko pliki”).
13. **Na okno sesji przeglądarki — zgoda gestem (ADR 0027 p. 2).** Puszczenie pliku na okno sesji jest zgodą. Póki kursor
    tylko jest nad oknem, strefa **nic** nie dostaje — znak „Kopia do sesji „Ogólna”” rysuje ekran (z tabeli; odmowa dla
    folderów, Kosza, pendrive'a i więcej niż `K1_PRZ_DO_STREFY_MAKS` = 8 plików). Po puszczeniu VMM dostaje `UPUSC_STREFA`
    (punkt w obrazie, liczba plików) i robi z tego ramkę `UPUSC` dla agenta; źródło dostaje `PRZECIAGANIE_KONIEC` z numerem
    strefy i wysyła kopie jak „Wyślij do…” (`DOK_DO_STREFY` z trybem `K1_PS_TRYB_UPUSC`; `PLIK` ma pole tryb: wyślij,
    otwórz, upuść). Agent kładzie je w „Z K1” i, gdy przyjdzie ostatni, pisze kompozytorowi „upusc X Y <adresy>”
    (FIFO z kroku 3); kompozytor przyjmuje tylko adresy z „Z K1”, najwyżej 8, punkt na ekranie strefy, raz na sekundę,
    i **odgrywa upuszczenie** w swoim protokole Wayland (`kompozytor/dane.c`): `wl_data_offer` z `text/uri-list` (obiekty
    kompozytora mają numery od `0xff000000`, jak w libwayland), `enter`, `motion`, akcja „kopia”, `drop`; na `receive`
    pisze adresy. Sprawdzone na stanowisku z prawdziwym Firefoksem (P22, `testy/stanowisko/upusc.py`): strona dostaje
    pliki (nazwy i rozmiary), pole „Prześlij plik” się wypełnia. Przeciąganie zaczęte w strefie (karta, link) dalej jest
    od razu anulowane — nic nie wychodzi ze strefy tą drogą.
14. **Okno wyboru pliku K1 dla strony (T7) — „Później”.** Firefox pokazuje okno wyboru K1 tylko przez portal
    `xdg-desktop-portal` (`widget.use-xdg-desktop-portal.file-picker`), czyli przez D-Bus — w strefie nie ma D-Bus ani
    portalu, a wniesienie ich to duży obcy kod w strefie i nowa droga do K1. Zostaje okno GTK w strefie, które widzi tylko
    folder sesji („Z K1”, pobrane): plik dajesz stronie „Wyślij do…” albo przeciągając go na okno (ADR 0027 p. 4).

## Uzupełnienie — krok 5: schowek (2026-11-22; D5 właściciela: A)
15. **Właściciel: ekran (T8).** Jeden tekst UTF-8 (`K1_SCHOWEK_MAKS` = 1 MiB, `k1_schowek_dobry`: bez NUL i znaków
    sterujących poza tabulatorem i nowym wierszem), tylko w pamięci ekranu — nigdy w skarbcu ani w logu (liczby bajtów
    i skąd), czyszczony przy wyłączaniu. Obrazy w schowku — „Później”. Kopia (`SCHOWEK_USTAW`) tylko od klienta okna
    z fokusem i tylko tuż po geście użytkownika w tym oknie (klawisz albo wciśnięcie, które ekran temu oknu dał: aplikacja
    2 s, strefa 10 s — „Kopiuj” na stronie po kliknięciu); wklejenie w aplikacji K1 — `SCHOWEK_DAJ` po Ctrl+V (szkielet
    aplikacji robi to sam), odpowiedź `SCHOWEK`.
16. **Strefa — „za każdym wklejeniem” (ADR 0020, D5 „A”).** Strefa nie pyta o schowek i nie ma własnego, który strona mogłaby
    przeczytać później. Gdy ekran daje oknu sesji Ctrl+V, najpierw wysyła jej VMM-owi tekst (`SCHOWEK`), potem klawisz;
    VMM wysyła agentowi ramki `WKLEJ` + `WKLEJ_TEKST` i **wstrzymuje klawisze**, aż agent potwierdzi `WKLEJONE`
    (kompozytor zrobił z tekstu zaznaczenie okna z klawiaturą i anulował źródło przeglądarki) — najwyżej 0,5 s. Gestem jest
    samo Ctrl+V; „Wklej” z menu strony działa potem na tym samym tekście. Tekst z innej sesji: ramka okna na 3 s „Wklejono z:
    Bank”. Kopia w strefie: przeglądarka ustawia zaznaczenie (tylko klient z klawiaturą), kompozytor czyta tekst
    (`wl_data_source.send` do rury, do 1 MiB) i daje go agentowi (kolejka FIFO `/run/k1/schowek`), agent — `KOPIA` +
    `KOPIA_TEKST`; VMM sprawdza całość (długość jak zapowiedziana, tekst, najwyżej jedna kopia na 0,2 s), złamanie zasad
    zamyka port plików. Sprawdzone na stanowisku z prawdziwym Firefoksem (`testy/stanowisko/schowek.py`).

## Konsekwencje
- Pierwsza droga bajtów ze strefy do danych użytkownika: nadzorca dostaje od VMM tylko komunikaty 32 B i zapieczętowane
  memfd, a skarbiec — tylko nowy plik w Pobranych. Niezaufany jest format ramek (VMM), nazwa (VMM, nadzorca, skarbiec)
  i treść (skarbiec rozpoznaje typ; pliki z internetu otwiera K1 w klatkach jak każde inne — dekoder, Notatki).
- Ryzyko: użytkownik zapisze w Plikach złośliwy plik. Ogranicza je to, że pliki K1 czytają programy w klatkach (dekoder
  obrazów, Notatki tylko tekst), a PDF i inne pliki otwiera się w strefie (krok 3) — RYZYKA R103.
- Kroki 3–5 tego planu używają tej samej drogi w drugą stronę (pliki do strefy), dla przeciągania i schowka —
  uzupełnienia tego ADR.
- Krok 5: schowek łączy sesje — tekst skopiowany w Banku można wkleić w Ogólnej, ale tylko gestem użytkownika (Ctrl+V,
  które widzi K1), z informacją na ramce; kopia ze strefy działa tylko tuż po geście w jej oknie, więc strona w tle nie
  podmieni schowka. RYZYKA R106.
- Krok 4: nowa droga wejścia do strefy (upuszczenie) to nadal tylko kopia w „Z K1” po geście w K1; kompozytor strefy zna
  teraz obiekty, które sam tworzy (`wl_data_offer`) — powierzchnia w strefie, nie w K1. Pliki dostają od ekranu numery
  folderów pulpitu (Pulpit, jego foldery) — numery, które i tak widzą; skarbiec sprawdza prawa jak przy każdym przeniesieniu.
