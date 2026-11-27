# Komponent: skarbiec (Blob Vault)

| | |
|---|---|
| **Status** | PRZETESTOWANE (M5c na hoście, M5d w QEMU: dysk SATA i pendrive przez UEFI; 0.5.8: pendrive podłączony po starcie; 0.8.0: pliki i foldery — host i QEMU; 0.8.2: format 2 i przejście starych magazynów — host i QEMU; 0.10.0: bez formatu 1, dyski stref — host, w systemie i QEMU) |
| **Pliki** | `src/skarbiec/` w modułach (0.8.0, `skarbiec.h`): `main.c` (pętla, kanały, obiekty aplikacji), `obiekty.c` (obiekty według właściciela i nazwy: magazyn albo pamięć), `haslo.c` (odblokowanie, zmiana hasła, limit prób), `dokumenty.c` + `dok_drzewo.c`, `dok_kosz.c`, `dok_wczytaj.c` (pliki, foldery, Kosz; wspólne: `dok_wew.h`), `dok_kanal.c` (komunikaty `DOK_*`, tabela praw), `miejsce.c` (miejsce na pliki, powiększenie K1-DANE — 0.8.1), `przerwy.c` (praca w przerwach: kompaktowanie krokami — 0.8.2), `zalegle.c` (odpowiedzi, których pełny kanał klienta jeszcze nie przyjął — 0.9.0), `strefy.c` (dyski stref — 0.10.0 krok 5); format: `magazyn.{c,h}` (funkcje publiczne, indeks), `odcinki.c` i `spis.c` (format 2: odcinki, spis skarbca — 0.8.2, ADR 0036), `naglowek.c` (nagłówek i owinięty klucz; `magazyn_wew.h`); format 1 (`obszary.c`) i przejście na format 2 (`przejscie.c`) — usunięte w 0.10.0, `src/zewnetrzne/monocypher/`; wspólne: `src/wspolne/k1_memfd.*`, `k1_skarb_klient.*` (strona klienta), `k1_dokument.*` (wpis pliku, typy, foldery) |
| **Decyzje** | ADR 0015 (format, szyfrowanie), ADR 0016 (obraz USB), ADR 0013 (partycja „K1-DANE”), ADR 0029 (pliki, foldery, Kosz), ADR 0034 (duże dane: indeks, limity, kompaktowanie, rezerwa), ADR 0033 (K1-DANE do końca pendrive'a, od 0.8.1 także z danymi), ADR 0036 (format 2: odcinki, spis, kompaktowanie po odcinku — 0.8.2) |

## Cel
Trwałe, zaszyfrowane dane użytkownika: ustawienia (ekran), notatki (Notatki), historia baterii, rozmieszczenie
pulpitu, od 0.8.0 **pliki i foldery użytkownika** (Pliki, pulpit). Jedyny proces z dostępem do partycji danych.

## Granica zaufania
Wejście: zawartość partycji (mogła być zmieniona przez kogoś z nośnikiem w ręku) i komunikaty od
klientów. Każdy rekord na dysku jest uwierzytelniony (zmiana = odmowa otwarcia, nie śmieci);
klient widzi tylko obiekty swojego właściciela (właściciela kanału podaje nadzorca, nie klient);
każdy pakiet ma sprawdzany UID nadawcy (`SO_PASSCRED`, w obie strony).

## Uprawnienia
UID/GID 103, klatka (ADR 0007): seccomp = baza + `pread64`, `pwrite64`, `fdatasync` (partycja i memfd —
innych plików nie ma), `getrandom`, `memfd_create`, `sysinfo`; Landlock bez
żadnej ścieżki (nie otworzy niczego); cgroup 320 MiB (Argon2id do 256 MiB), 1 proces;
`RLIMIT_NOFILE` 64 (od 0.9.0: memfd czekający z odpowiedzią dla klienta z pełnym kanałem; było 32); `RLIMIT_FSIZE` 24 MiB (memfd z plikiem do 16 MiB, 0.8.0; od 0.8.1 kawałek 1 MiB, w 0.8.1 w jednej odpowiedzi najwyżej całe zdjęcie dla dekodera — 16 MiB, od 0.8.2 też tylko kawałek; zapisu na urządzenie blokowe ten limit nie dotyczy —
`block/fops.c`). Bez partycji obiekty w pamięci: do 65 536, razem do 128 MiB.

## Wejścia i wyjścia
- fd 3: kanał sterujący od nadzorcy — `SKARB_KLIENT(właściciel, uid, na_usb, zapas)` + koniec kanału klienta (0.6.3: `na_usb` wraca do klientów w `SKARB_WYNIK` d, bit 1; 0.8.1: d = ile MiB pendrive ma jeszcze za K1-DANE) i `POWIEKSZONO` (wynik powiększenia, niżej); od 0.9.0 `ZEGAR_PRZESUNIETY` (zegar z internetu, niżej); od 0.10.0 `SKARB_WYCZYSC` (dane sesji strefy, niżej). `START_POTWIERDZONY` (0.8.2–0.9.1, przejście formatu 1 na format 2) — usunięty w 0.10.0.
- fd 4: partycja GPT „K1-DANE” (otwarta przez nadzorcę `O_RDWR|O_EXCL`); brak → tryb „tylko w pamięci”.
- Kanały klientów: `SKARB_CZYTAJ`, `SKARB_ZAPISZ` (+ zapieczętowany memfd o podanym rozmiarze),
  `SKARB_USUN` → `SKARB_DANE` (+ memfd) albo `SKARB_WYNIK` (wynik, tryb: dysk / pamięć).
- Klienci (0.5.6): ekran (fd 5, obiekt `K1_OB_USTAWIENIA`), Notatki (fd 4; od 0.5.8 obiekt 1 = lista,
  2…33 = notatki), Ustawienia (fd 5: `SKARB_INFO`; `SKARB_HASLO` do 0.12.3), gui (fd 6, od 0.13.0 krok 6: `SKARB_INFO`, `SKARB_HASLO` — ekran
  zabezpieczeń), ekran hasła (`SKARB_ODBLOKUJ`),
  nadzorca (0.6.0: właściciel `K1_WL_SYSTEM`, obiekt `K1_OB_BATERIA` — historia baterii, `k1_bat_arch.h`),
  gui (0.7.5: rozmieszczenie pulpitu; 0.8.0: pliki folderu Pulpit), Pliki (0.8.0: pliki — `DOK_*`, niżej), siec (0.9.1
  krok 2: fd 4, właściciel = profil 18, obiekt 1 — zapamiętane sieci Wi-Fi z kluczami sieci i przełącznik radia; format
  „K1WF” — `komponenty/siec.md`; skarbiec zablokowany → siec pyta co 5 s), VMM strefy, która pamięta (0.10.0 krok 5: fd 8,
  właściciel = profil wiersza strefy — kawałki jej dysku, niżej), nadzorca (0.10.0 krok 5: sesje stref — obiekt 4,
  `komponenty/strefa.md`).
- Hasło (0.5.7, ADR 0017): stan „zablokowany” (magazyn z hasłem, nieotwarty) → `SKARB_STAN` do nadzorcy;
  `SKARB_ODBLOKUJ` tylko od ekranu hasła, `SKARB_HASLO` (obecne + nowe) tylko od pulpitu (gui — ekran zabezpieczeń; do 0.12.3 od Ustawień; 0.13.0 krok 6, ADR 0061), `SKARB_INFO`
  od każdego; limit prób (od trzeciej złej: 5 s … 5 min) liczy skarbiec. Zablokowany: odczyt/zapis
  obiektów → błąd „złe hasło”.
- Log: utworzenie/otwarcie magazynu, klienci, zapisy (numer obiektu i rozmiar — nigdy treść).

## Doręczanie (0.9.0 krok 2, RYZYKA R64)
Skarbiec nigdy nie czeka na klienta i **nie gubi odpowiedzi**. Wszystko, co idzie do klienta, idzie przez jedno miejsce
(`do_klienta`, `main.c`). Gdy kanał klienta jest pełny (klient akurat długo liczy albo importuje), odpowiedź czeka
w skarbcu razem z memfd, który niesie (`zalegle.c`, do 32 na klienta), i idzie, gdy kanał znów przyjmuje (`poll`
z `POLLOUT` tylko dla klientów z zaległymi) — w tej samej kolejności. Zasady:
- „pliki się zmieniły” (`DOK_ZMIANA`) czeka najwyżej raz — klient po nim i tak prosi o listę i dostaje to, co jest
  wtedy;
- wszystko inne — w kolejności nadania, nic nie przepada celowo;
- **dopóki coś czeka na klienta, skarbiec nie bierze jego próśb** (klient jest zajęty; jego prośby czekają w jego
  kanale) — inni klienci są obsługiwani jak zwykle. Dlatego w kolejce jednego klienta są najwyżej odpowiedzi na jedną
  prośbę (najwięcej: `SKARB_PODZIAL` — część po części) i to, co wywołali inni (zmiana, plik dla okna wyboru).
  Pełna kolejka byłaby błędem: wpis w logu „kolejka klienta pełna …” z liczbą utraconych.
Klient, który odszedł (kanał zamknięty), traci to, co na niego czekało — razem z memfd.

## Miejsce na pliki i cały pendrive na dane (0.8.1 krok 3, ADR 0033)
- `SKARB_MIEJSCE` (skarbiec → klient, obiekt 1; po `WYNIK` na `SKARB_INFO` i `SKARB_POWIEKSZ` oraz po każdej `DOK_LISTA`):
  a = zajęte MiB (0…b), b = MiB na pliki, c = pliki i foldery razem z Koszem (0…16 384), d = ile MiB pendrive ma jeszcze
  za K1-DANE (0, gdy nie pendrive albo < 16 MiB), e = 0. **Miejsce na pliki** = miejsce na dane magazynu bez rezerwy dla
  systemu — format 2 (od 0.8.2): odcinki bez wolnego zapasu na kompaktowanie (3 %, co najmniej 2), czyli ok. 90 %
  K1-DANE (format 1 do 0.8.1: połowa partycji); bez K1-DANE — pamięć (128 MiB) bez rezerwy.
  Zajęte = żywe obiekty (z ustawieniami); gdy system sięgnął do swojej rezerwy, zajęte = całość. Ustawienia → Miejsce
  (do 0.8.3: Informacje) pokazują „Miejsce na pliki: zajęte X z Y” z paskiem i „Pliki i foldery: n z 16 384” (bez K1-DANE: „w pamięci do
  wyłączenia: zostało X”), Pliki — stopkę „Mało miejsca na pliki”, gdy zostało < 10 %.
- **Podział zajętego miejsca** (0.8.5 krok 3, decyzja właściciela D2 — według folderów): `SKARB_PODZIAL` (tylko
  właściciel Ustawienia, magazyn otwarty; inny — `WYNIK` z `K1_MAG_ARG` i nic więcej) → `SKARB_CZESC` dla każdej części
  z tabeli `K1_CZESCI` (`k1_dokument.h`): foldery stałe (`K1_FOLDERY` — plik w folderze użytkownika liczy się do folderu
  stałego, w którym leży), Kosz (wszystko w Koszu, też w folderze wyrzuconym do Kosza) i rezerwa dla systemu; a = KiB
  (żywe pliki, bez starych wersji i folderów; do `INT32_MAX`), b = liczba plików (rezerwa: 0); potem `WYNIK`
  (a = `SKARB_PODZIAL`). **Tylko liczby** — Ustawienia nie dostają nazw ani listy plików (nie mają praw do `DOK_*`).
  Reszta zajętego (ustawienia, notatki systemu, stare wersje przed kompaktowaniem) to w Ustawieniach „System”
  (`dok_podzial`, `dok_drzewo.c`; `miejsce_podzial`, `miejsce.c`).
- **Powiększenie z danymi** (decyzja właściciela D4: przycisk, kliknięcie = zgoda, nigdy samo przy starcie): Ustawienia
  → `SKARB_POWIEKSZ` (tylko właściciel Ustawienia, magazyn otwarty, d ≥ 16) → skarbiec do nadzorcy `POWIEKSZ_DANE`
  (kanał sterujący) → nadzorca pisze nową tablicę GPT i `BLKPG_RESIZE_PARTITION` (jak dla pustej K1-DANE, `powieksz.c`)
  → `POWIEKSZONO` (a = 0, `K1_SKARB_ARG`: nie da się, `K1_SKARB_IO`: błąd zapisu) → skarbiec czyta nowy rozmiar
  partycji (`lseek` na swoim fd — jądro zmienia rozmiar otwartej partycji, `block/partitions/core.c`) i
  `k1_mag_powieksz`: nowe wolne odcinki za ostatnim, tylko nagłówek (oba sloty). → `WYNIK` (a = `SKARB_POWIEKSZ`) i `MIEJSCE` do Ustawień, `DOK_ZMIANA` do klientów (Pliki odświeżają stopkę).
- **Przerwa prądu w każdym miejscu zostawia działający magazyn:** przed nową tablicą — wszystko po staremu; po tablicy,
  przed nagłówkiem — magazyn starego rozmiaru na większej partycji (poprawny); przy starcie skarbiec widzi partycję
  większą od magazynu i **kończy powiększenie** („magazyn powiększony do całej partycji K1-DANE (dokończenie po
  starcie)”); w trakcie kompaktowania albo zapisu nagłówka — stary albo nowy nagłówek, oba poprawne (test po każdym
  zapisie w `test_odcinki`). Format na dysku bez zmian.

## Zapis w toku (0.8.1 krok 4, ADR 0034 — uzupełnienie)
Treść podróżuje kawałkami po 1 MiB (`K1_DOK_KAWALEK`), więc pamięć usług nie rośnie z wielkością pliku (do 4 GiB).
- **Zapis:** `DOK_NOWY` (e = 1) albo `DOK_ZAPISZ` (b = 1) z pierwszym pełnym kawałkiem otwiera **zapis w toku**
  (`dok_zapis.c`): nowy plik dostaje numer od razu (nikt inny go nie weźmie), nowa treść — następną generację. Każdy
  kawałek jest od razu obiektem skarbca; klient wysyła następny (`DOK_KAWALEK`) dopiero po `DOK_WYNIK` poprzedniego —
  jeden kawałek w drodze. Wszystkie kawałki poza ostatnim mają dokładnie 1 MiB. `DOK_KONIEC` (a = 0) zapisuje wpis
  (dla nowego pliku: wolna nazwa w chwili końca, numer w `DOK_WYNIK` c); a = 1 porzuca. Jeden zapis w toku na kanał
  i na plik (drugi: `K1_DOK_BLAD_ZAPIS`, −29). Nieudany kawałek (np. brak miejsca), zamknięty kanał, plik w Koszu
  przed końcem → zapis porzucony, jego kawałki usunięte, stara treść nietknięta. Przerwa prądu → kawałki bez wpisu
  albo nowszej generacji usuwa start (`dok_wczytaj`, jak dotąd). Zapis całości w jednym komunikacie (e = 0) idzie
  przez ten sam kod.
- **Odczyt:** `DOK_CZYTAJ` (a = najwyżej tyle bajtów, b = od kawałka) → `DOK_TRESC` (a = wysłane, b = kawałek,
  c, d = rozmiar pliku: niższe i wyższe 32 bity); za końcem pliku — `K1_MAG_ARG`. Jedna odpowiedź to najwyżej kawałek
  (1 MiB) — od 0.8.2 także dla dekodera: zdjęcie idzie do niego kawałek po kawałku przez Pliki (`komponenty/dekoder.md`;
  w 0.8.1 całe zdjęcie do 16 MiB w jednym memfd).

## Dyski stref (0.10.0 krok 5, ADR 0042 T11, `strefy.c`)
Sesja przeglądarki, która pamięta, trzyma swój dysk (2 GiB, ext4 gościa) w skarbcu: kawałek `i` to obiekt `K1_OB_DYSK + i`
(nazwa „o65536”…) właściciela = profil wiersza strefy, najwyżej 64 KiB (`K1_STREFA_DYSK_KAWALEK`). Te same komunikaty co
obiekty aplikacji (`SKARB_CZYTAJ`, `SKARB_ZAPISZ`, `SKARB_USUN`), ale:
- **tylko strefa** (właściciel z tabeli `K1_STREFY`, `k1_profil_strefy`) nazwie obiekt dysku — inny klient dostaje
  `K1_MAG_ARG` (log „odrzucono: obiekt N dysku strefy…”); sesje nie widzą swoich dysków (różni właściciele);
- kawałek **nie bierze rezerwy**, którą pliki zostawiają systemowi (`dok_rezerwa`) — brak miejsca: `K1_MAG_PELNY`, strefa
  widzi błąd zapisu, ustawienia dalej się zapisują;
- zapisy kawałków nie trafiają do logu po jednym (za dużo) — tylko błędy; VMM loguje liczby na końcu sesji;
- **„Wyczyść dane sesji”** — nadzorca `SKARB_WYCZYSC` (kanał sterujący, a = właściciel strefy) → skarbiec usuwa każdy
  kawałek tego właściciela → `SKARB_WYCZYSZCZONO` (a = właściciel, b = wynik, c = ile kawałków) do nadzorcy;
- Ustawienia → Miejsce: część `K1_CZESC_STREFY` („Strefy”) — bajty wszystkich dysków stref (`SKARB_CZESC` b = 0: to nie pliki).

## Pliki i foldery (0.8.0, ADR 0029)
Wspólny właściciel `K1_WL_DOKUMENTY` (101) — nie aplikacja. Stałe foldery 1–6 (Pulpit, Dokumenty, Obrazy, Muzyka,
Wideo, Pobrane) nie mają obiektów; wpisy użytkownika mają numery od 100:
- `m<numer>` — **wpis** (176 B, `k1_dokument.h`): numer, folder nadrzędny, rodzaj (plik / folder), typ, pochodzenie,
  kto utworzył, generacja treści, rozmiar, czasy utworzenia, zmiany i wrzucenia do Kosza, nazwa (≤ 127 B UTF-8);
- `t<numer>g<generacja>k<kawałek>` — **treść** po 1 MiB (plik ≤ 4 GiB = 4096 kawałków, od 0.8.1; do 0.8.0 16 MiB).
  Zapis: kawałki nowej generacji → wpis → usunięcie starych kawałków;
- `a<profil>` — numer folderu aplikacji (np. Dokumenty › Notatki, 0.8.1): folder znajduje się po numerze, także po
  zmianie nazwy albo przeniesieniu; gdy trafi do Kosza albo zniknie — nowy folder (i nowy numer);
- `czas` — najpóźniejszy czas zegara, jaki skarbiec widział (0.8.1, zapis najwyżej raz na godzinę).

**Przy otwarciu nic nie ginie** (0.8.1, plan L2, `dok_wczytaj.c`): wpis, który nie przechodzi sprawdzenia pole po polu,
skarbiec **naprawia**, gdy da się odczytać numer, rodzaj, rozmiar i generację, a treść tej generacji jest na dysku —
złe znaki nazwy zamienia na „_” (nic z nazwy nie zostało: „Odzyskany plik <numer>”), pole poza zakresem na
bezpieczną wartość (folder → Dokumenty, twórca → Pliki, typ → „Plik”), wolną nazwę dostaje jak zawsze („ (2)”), i
zapisuje wpis od nowa. Wpis, którego nie da się naprawić, zostaje na dysku **razem z treścią** (log: „nieczytelne
wpisy n”) — późniejsza wersja może go odczytać; jego numeru nie dostaje żaden nowy wpis. Kawałki treści skarbiec
usuwa tylko wtedy, gdy nie ma obiektu wpisu albo gdy są starszej generacji wpisu, który się wczytał — nigdy kawałków
pliku, którego własnej treści brak (zła generacja: nic nie zgadujemy). Wpis bez folderu (albo w pętli) przenosi do
Dokumentów.
- Typ pliku rozpoznaje skarbiec przy każdym zapisie z pierwszych bajtów (`K1_TYPY`: tekst, PNG, JPEG, PDF, ZIP,
  inny) — nigdy z nazwy. Nazwy jak w Windows (bez `\ / : * ? " < > |`, znaków sterujących i odwracających
  kierunek tekstu, bez spacji i kropki na końcu), unikalne w folderze bez względu na wielkość liter (A–Z, Latin-1
  i Latin Extended-A — polskie, Ä/ä, Č/č…; 0.8.2); nowy wpis o zajętej nazwie dostaje „ (2)” przed rozszerzeniem
  (rozszerzenie do 16 bajtów; dłuższe „rozszerzenie” jest ucinane razem z nazwą — 0.8.2, L8). Do 8 poziomów
  folderów, 16 384 wpisy (0.8.1, ADR 0034; razem z Koszem), wpisy znajdowane po numerze przez indeks (`k1_indeks`).
- **Zasady nazw** (0.8.2 krok 2, L9; `src/wspolne/k1_nazwy.c`, te same w czytniku): jedna tabela znaków, których nazwa
  nie może mieć — nigdzie (sterujące, niewidoczne: U+200B…U+200F, U+00AD, U+2060…U+2064, U+FEFF, U+180E; zmieniające
  kierunek; wypełniacze Hangul; znaki tagów; obszar prywatny; niecharaktery), na brzegach (spacje każdej szerokości:
  U+0020, U+00A0, U+2000…U+200A, U+202F, U+205F, U+3000 — w środku wolno) i na początku (znak diakrytyczny bez
  litery); druga tabela: litera + znak diakrytyczny, które są jedną literą (polskie i częste łacińskie, NFC) — nazwa
  musi mieć jedną literę („ą”), nie dwie („a” + U+0328), więc dwie wyglądające tak samo nazwy to ta sama nazwa.
  Wpis z wcześniejszej wersji, którego nazwy dziś nie wolno, przy wczytaniu dostaje poprawioną nazwę (zakazany znak →
  „_”, litera + znak → jedna litera, zajęta → „ (2)”) — nigdy nie jest odrzucany.
- **Rezerwa dla systemu** (0.8.1, ADR 0034): nowy plik albo nowa treść pliku nie zajmie ostatnich 8 MiB (albo 2 %
  obszaru; najwyżej ćwierć obszaru — mała K1-DANE dalej przyjmuje pliki) — wynik `K1_DOK_BLAD_BRAK_MIEJSCA` (−28), w Plikach „Brak miejsca na pliki”; ustawienia, pulpit, bateria i
  tapeta zapisują się dalej. Zmiana nazwy, przeniesienie, Kosz i usuwanie działają zawsze.
- **Kosz**: „do Kosza” zapisuje czas we wpisie (folder idzie z zawartością); przywrócony wraca do swojego folderu
  (gdy go nie ma albo jest w Koszu — do Dokumentów, Pliki to mówią); „usuń na zawsze” i „opróżnij” tylko w Koszu; po
  30 dniach skarbiec usuwa sam — przy otwarciu i co godzinę. W Koszu jest to, co wyrzucono samo (`k1_dok_w_koszu`:
  plik i jego folder wyrzucone osobno to dwa elementy) — tak samo liczy skarbiec, lista i Pliki.
- **Granica czasu** (0.8.1, plan L3, `dok_czas.c`): K1 nie ustawia zegara komputera (do 0.9), a słaba bateria cofa go
  np. do 2000 r. Skarbiec zna dolną granicę: czas budowy tej wersji K1 (`K1_CZAS_BUDOWY` z `SOURCE_DATE_EPOCH` —
  powtarzalny) i najpóźniejszy widziany czas (obiekt `czas`). Czasy we wpisach (utworzono, zmieniono, Kosz) nigdy
  nie są wcześniejsze; gdy zegar jest za granicą, Kosz niczego nie usuwa na zawsze. Czas w Koszu z złego zegara
  (sprzed 1.11.2026 — żadna wersja K1 nie miała wtedy Kosza — albo ponad dobę w przyszłości) skarbiec ustawia na
  „teraz”, gdy zegar jest dobry: 30 dni liczy się od nowa. Zegar raz ustawiony za daleko w przód (np. 2035) blokuje
  Kosz do tej daty — bezpiecznie (nic nie ginie), rozwiąże to czas z sieci (0.9, RYZYKA R56).

**Prawa** — tabela w `dok_kanal.c` (wiersz na klienta; właściciel kanału od nadzorcy): Pliki — wszystko; pulpit
(gui) — tylko folder Pulpit (lista nazw i typów, „do Kosza”, „opróżnij Kosz”, „Otwórz za pomocą” (do 0.8.1 „Otwórz w…”),
od 0.8.2 krok 6 „przenieś” pliku Pulpitu do folderu Pulpitu — plik upuszczony na folder, bez treści); Notatki
(0.8.0, ADR 0030) — tylko pliki, które same utworzyły, i pliki dane im przez użytkownika: lista, treść, zapis,
nazwa, nowy plik tylko w swoim folderze **Dokumenty › Notatki** (skarbiec tworzy go, gdy go nie ma), do Kosza
tylko własne, okno wyboru pliku; Wybór pliku — lista widocznych (bez Kosza, bez treści) i odpowiedź; Podgląd (0.8.2
krok 4, ADR 0037, zakres `Z_OBRAZY_FOLDERU`) — tylko zdjęcia (rodzina „obrazy”) folderów, w których leżały pliki dane
mu przez użytkownika (`dostep.c` zapisuje folder przy dostępie): lista, treść, do Kosza (decyzja D2), okno wyboru
pliku z rodziną „obrazy” (`DOK_WYBIERZ` b; `DOK_PROSBA` c; skarbiec odrzuca wybór pliku innej rodziny); nadzorca
— kanałem z właścicielem `K1_WL_POBRANIA` (0.11.0 krok 2, ADR 0052, zakres `Z_POBRANE`) — tylko nowy plik w folderze
Pobrane z pochodzeniem strefy (`K1_POCH_STREFA_OD` + numer; twórca = profil strefy), zapisem w toku (`DOK_NOWY` z pierwszym
kawałkiem, `DOK_KAWALEK`, `DOK_KONIEC`), bez listy i treści; pochodzenia strefy nie może nadać nikt inny (też Pliki);
starsza wersja K1 czyta je jako „utworzony w K1” (`dok_wczytaj.c`, P14); od kroku 3 drugi kanał nadzorcy — właściciel
`K1_WL_DO_STREF` (zakres `Z_STREFA`): czyta tylko pliki dane strefom (`DOK_DO_STREFY` od Plików i pulpitu, prawo `P_STREFA`;
dostęp w `dostep.c` jak przy „Otwórz za pomocą”), kładzie nowy plik z pochodzeniem strefy tylko w folderze takiego pliku
(i dostaje do niego dostęp), dany plik do Kosza, nowemu nazwę — „Zapisać zmiany w oryginale?”; inni — nic
(`K1_DOK_BLAD_PRAWA`). Wpis spoza zasięgu klienta = „nie ma” (`K1_DOK_BLAD_BRAK`).

**Dostęp do pliku po geście** (0.8.0, ADR 0030, `dostep.c`): „Otwórz za pomocą” albo podwójne kliknięcie w Plikach albo na pulpicie
(`DOK_OTWORZ`) — skarbiec sprawdza, że aplikacja otwiera ten typ (`K1_OTWIERANIE`), zapamiętuje „aplikacja X może
ten plik” i prosi nadzorcę (`SKARB_URUCHOM`) o uruchomienie jej z plikiem. Prośba aplikacji o plik
(`DOK_WYBIERZ`) — jedna naraz (`K1_DOK_BLAD_CZEKA`); skarbiec prosi nadzorcę o okno wyboru (`SKARB_URUCHOM` z
aplikacją „Wybór pliku”), oknu podaje prośbę (`DOK_PROSBA`), a gdy ono odpowie (`DOK_WYBRANO`: plik widoczny,
pasujący typ) — daje dostęp i odpowiada aplikacji (`DOK_WYBRANY`; 0 = anulowano, także gdy okno się zamknie).
Dostęp jest tylko w pamięci skarbca i znika, gdy zamknie się ostatni kanał tej aplikacji. Jedna aplikacja dostaje
najwyżej 64 pliki (`K1_DOK_DOSTEP_APL`, 0.8.1): 65. — odmowa `K1_DOK_BLAD_OTWARTE` z komunikatem w Plikach i w oknie
wyboru („zamknij ją i otwórz ponownie”); wcześniej najstarszy znikał po cichu.

| Komunikat | Kierunek | Pola |
|---|---|---|
| `DOK_SPIS` | klient → | lista wpisów, które klient widzi (odpowiedź `DOK_LISTA`) |
| `DOK_CZYTAJ` | klient → | obiekt = plik, a = najwyżej tyle bajtów (początek tekstu w szczegółach: 64 KiB; ≤ 1 MiB — do 0.8.1 ≤ 16 MiB), b = od kawałka (0.8.1) → `DOK_TRESC` |
| `DOK_NOWY` | klient → | obiekt = folder, a = plik / folder, b = długość nazwy, c = długość treści (≤ 1 MiB), d = pochodzenie (0: K1; inne — tylko z prawem `P_POCHODZENIE`: Pliki, import z pendrive'a, ADR 0031), e = 1 zapis w toku (0.8.1); memfd: nazwa + treść |
| `DOK_ZAPISZ` | klient → | obiekt = plik, a = długość (≤ 1 MiB), b = 1 zapis w toku (0.8.1); memfd: nowa treść (nowa generacja) |
| `DOK_KAWALEK` | klient → | następny kawałek zapisu w toku (0.8.1): a = długość 1…1 MiB; memfd → `DOK_WYNIK` |
| `DOK_KONIEC` | klient → | a = 0 koniec zapisu w toku (wpis), 1 porzucenie → `DOK_WYNIK` (c = numer nowego pliku) |
| `DOK_CZAS` | Pliki → | (0.8.2 krok 2) obiekt = plik z pochodzeniem innym niż K1 (import), a, b = czas „zmieniono” z pendrive'a (dwa pola, 1 s … koniec 2107 r.); tylko z prawem `P_POCHODZENIE` → `DOK_WYNIK`. Ponowny import tego samego pliku Pliki pomijają |
| `DOK_NAZWA` | klient → | obiekt = wpis, a = długość, b = 1: zajęta nazwa dostaje „ (2)” (0: odmowa); memfd: nowa nazwa |
| `DOK_PRZENIES` | klient → | obiekt = wpis, a = folder docelowy (nie do siebie, nie za głęboko), b = 1: zajęta nazwa dostaje „ (2)” — „Zachowaj oba” (0.8.2 krok 6; 0: odmowa `K1_DOK_BLAD_ZAJETA`) |
| `DOK_PRZYWROC` | klient → | obiekt = wpis w Koszu, a = 0: do swojego folderu (gdy go nie ma — do Dokumentów), a = folder: tam, gdzie go upuszczono (0.8.2 krok 6; folder widoczny, nie w samym sobie, nie za głęboko — inaczej `K1_DOK_BLAD_MIEJSCE`); zajęta nazwa dostaje „ (2)” |
| `DOK_KOSZ`, `DOK_USUN` | klient → | obiekt = wpis (usuń: tylko z Kosza) |
| `DOK_OPROZNIJ` | klient → | wszystko z Kosza na zawsze |
| `DOK_LISTA` | → klient | a = długość, b = liczba wpisów; memfd: „K1D1”, liczba, w Koszu, wpisy |
| `DOK_TRESC` | → klient | obiekt = plik, a = wysłane bajty, b = kawałek, c, d = cały rozmiar (0.8.1: dwa pola, do 4 GiB); memfd |
| `DOK_WYNIK` | → klient | obiekt, a = rodzaj prośby, b = wynik (0, `K1_MAG_*`, `K1_DOK_BLAD_*` od −20 do −29), c = numer nowego wpisu (`DOK_NOWY`, `DOK_KONIEC`) |
| `DOK_ZMIANA` | → klient | pliki się zmieniły — do każdego klienta z prawem listy (odświeżają widok) |
| `DOK_OTWORZ` | Pliki, pulpit → | obiekt = plik, a = aplikacja (nie Pliki), która otwiera jego typ → `DOK_WYNIK` |
| `DOK_WYBIERZ` | aplikacja → | a = typ (0: każdy) — okno wyboru pliku; odpowiedź później: `DOK_WYBRANY` (błąd od razu: `DOK_WYNIK`) |
| `DOK_WYBRANO` | Wybór pliku → | obiekt = wybrany plik (0: anulowano) → `DOK_WYNIK` |
| `DOK_WYBRANY` | → aplikacja | obiekt = plik, którego może teraz używać (0: anulowano) |
| `DOK_PROSBA` | → Wybór pliku | a = aplikacja, która prosi, b = typ (0: każdy) |
| `SKARB_URUCHOM` | → nadzorca | kanał sterujący: obiekt = plik (0: bez), a = aplikacja do uruchomienia (z plikiem: jej fd 5) |
| `SKARB_MIEJSCE` | → klient | miejsce na pliki (0.8.1, wyżej): a = zajęte MiB, b = na pliki MiB, c = wpisy, d = MiB pendrive'a za K1-DANE, e = 1 pliki są w pamięci — bez K1-DANE (0.8.2) |
| `SKARB_PODZIAL` | Ustawienia → | co zajmuje miejsce na pliki (0.8.5): → `SKARB_CZESC` dla każdej części, potem `SKARB_WYNIK` |
| `SKARB_CZESC` | → Ustawienia | jedna część (0.8.5): obiekt = część 1…`K1_CZESCI_MAKS` (foldery stałe, Kosz, rezerwa), a = KiB, b = pliki (rezerwa: 0); bez nazw |
| `SKARB_POWIEKSZ` | Ustawienia → | „Użyj całego pendrive'a na dane” (gest użytkownika) → `SKARB_WYNIK` + `SKARB_MIEJSCE` |
| `POWIEKSZ_DANE` | → nadzorca | kanał sterujący: powiększ K1-DANE do końca pendrive'a (magazyn otwarty, prośba z Ustawień) |
| `POWIEKSZONO` | nadzorca → | kanał sterujący: a = 0 albo `K1_SKARB_ARG` / `K1_SKARB_IO` |
| `SKARB_WYCZYSC` | nadzorca → | kanał sterujący (0.10.0 krok 5): a = właściciel — profil strefy; każdy kawałek dysku tej strefy usunięty („Wyczyść dane sesji”) |
| `SKARB_WYCZYSZCZONO` | → nadzorca | kanał sterujący (0.10.0 krok 5): a = ten właściciel, b = 0 albo `K1_MAG_*`, c = usunięte kawałki |
| `SKARB_ZWOLNIJ` | nadzorca → | kanał sterujący (0.10.10 krok 1), pola zerowe: Instalator instaluje od nowa na dysku tej K1-DANE (użytkownik wpisał „USUŃ”) — magazyn zamknięty bez zapisu (klucz wymazany), partycja oddana, od teraz obiekty tylko w pamięci (do wyłączenia komputera) |
| `SKARB_ZWOLNIONO` | → nadzorca | kanał sterujący (0.10.10 krok 1), pola zerowe: partycja zamknięta |
| `ZEGAR_PRZESUNIETY` | nadzorca → | kanał sterujący (0.9.0 krok 8): a, b = o ile sekund nadzorca przestawił zegar z internetu (ze znakiem, 64 bity, najwyżej ±200 lat). Skarbiec: najpóźniejszy widziany zegar = teraz (także gdy zegar cofnięto), a czas każdego pliku w Koszu przesuwa się o tyle, o ile przesunął się czas zapisywany we wpisach (`dok_zegar_przesuniety`) — plik leżący w Koszu 2 dni ma dalej 28 dni, także gdy zegar skoczył z 2000 r. na dziś |

Zablokowany magazyn (hasło) — każda prośba `DOK_*` dostaje „złe hasło”. W logu tylko numery komunikatów, liczby
wpisów i wyniki — nigdy nazwy ani treść.

## Stan magazynu dla TPM (0.13.0 krok 2, ADR 0057, `epoka.c`)
Nadzorca pamięta w TPM 2.0, jak daleko doszedł magazyn, i tak widzi dysk cofnięty do starszej kopii. Skarbiec wysyła mu
`SKARB_EPOKA` (kanał sterujący): identyfikator magazynu (8 pierwszych bajtów uuid z nagłówka) i generację nagłówka, która
**jest na dysku** (`k1_magazyn.na_dysku` — ustawiana dopiero po zapisie i synchronizacji nagłówka, albo przy otwarciu);
raz zaraz po otwarciu (e = 0; także po utworzeniu nowego magazynu i po odblokowaniu hasłem) i raz po pierwszym zapisie
tej sesji (e = 1). Format bez zmian. Nic z tego w logu. Bez K1-DANE (dane tylko w pamięci) — nic.

## Format na dysku (bloki po 4096 B, liczby little-endian)
Format 2 (0.8.2, ADR 0036, niżej). **Format 1** (0.5.6–0.8.1: dwa obszary po pół partycji, nagłówek „K1SKARB1” w wersji 1)
i jego przejście na format 2 (0.8.2–0.9.1, po potwierdzonym starcie) — **usunięte w 0.10.0** (decyzja właściciela D7
planu 0.10.0: żaden taki nośnik nie został). Skarbiec 0.10.0 rozpoznaje jeszcze nagłówek formatu 1 (`K1_MAG_STARY`): nie
otwiera go ani nie nadpisuje — log „partycja K1-DANE ma magazyn formatu 1 … uruchom ten nośnik raz z K1 0.9.1”, dane
tylko w pamięci (`test_odcinki`, `test-skarbiec.sh` „stary”: partycja nietknięta). Formatowanie: tylko gdy pierwszy MiB
partycji to zera (inaczej „obce dane — nie nadpisuję”).

### Format 2: odcinki i spis skarbca (0.8.2, ADR 0036)
| Miejsce | Zawartość |
|---|---|
| blok 0 i 1 | nagłówek, wersja 2 (jak w formacie 1: kopia z generacją parzystą / nieparzystą) |
| bloki 2… | N odcinków po S bloków: S = 257 × 2, 4 … 64 (miejsce na tyle kawałków 1 MiB), co najmniej 16 odcinków, gdy partycja pozwala (bardzo mała partycja — S 64–256); reszta partycji za ostatnim odcinkiem nieużywana |

Nagłówek: `K1SKARB1`, wersja 2, flagi, uuid, generacja, liczba bloków (2 + N·S), S, Argon2id (KiB, przejścia), N,
licznik generacji odcinków, bloki i wpisy spisu, sól, owinięty klucz główny, długość ogona (1–128), zero, ogon (po
16 B: odcinek, ile bloków zapisano, generacja; generacje rosną), zera do bajtu 4080, MAC. Pierwszy odcinek ogona
zaczyna się spisem; ostatni to **głowa** — tam idą nowe rekordy.

Rekord (wielokrotność bloku): liczba bloków (u32), 0 (u32), nonce (24) i MAC (16) metryczki, **metryczka**
(48 B szyfrogramu: typ — obiekt / usunięcie / spis, 0, właściciel, długość, czas, nazwa 32), nonce (24) i MAC (16)
treści, **treść** (szyfrogram), zera do końca bloku. Dane dodatkowe metryczki: uuid, odcinek, jego generacja,
pozycja, liczba bloków; treści: MAC metryczki. Rekord z kawałkiem 1 MiB ma 257 bloków.

Spis: rekordy typu „spis” (kawałki do 1 MiB) — numer kawałka, liczba wpisów, N spisu (odcinki w chwili zapisu; po
powiększeniu nagłówek ma więcej — dopisane odcinki są wolne), 0; w kawałku 0 generacja każdego z N odcinków
(0 — bez żywych obiektów); wpis: właściciel (u16), długość nazwy (u8), 0, odcinek, pozycja, długość, nazwa.
Otwarcie: nagłówek → spis (każdy wpis sprawdzony: odcinek w użyciu, nie odcinek spisu, rekord mieści się w odcinku,
nazwa bez zer, bez powtórzeń) → metryczki rekordów ogona po kolei (odcinek ogona, który po spisie został głową,
nie może już mieć obiektów ze spisu). Treść — dopiero przy odczycie obiektu.

Zapis: rekord do głowy → `fdatasync` → nagłówek → `fdatasync`; pełna głowa → wolny odcinek (bez żywych obiektów, spoza
ogona) dostaje nową generację i zostaje głową (czasem z nowym spisem — wtedy ogon zaczyna się od niego). Zapas: 3 %
odcinków, co najmniej 2; zapis danych zawsze zostawia jeden wolny. Kompaktowanie: odcinek spoza ogona z najmniejszą
ilością żywych danych — jego obiekty do głowy, po kilka MiB na krok (`k1_mag_kompaktuj_krok`). Do 262 144 obiektów.

### Kompaktowanie w przerwach (0.8.2 krok 1b, `przerwy.c`)
Pętla skarbca (`main.c`) pyta `przerwa_czeka()`, ile ciszy potrzeba przed następnym kawałkiem pracy:
- krok kompaktowania (najwyżej **4 MiB** przeniesionych — ok. 0,02 s procesora, ułamek sekundy z USB 2.0) po
  **0,5 s** bez próśb, gdy wolnych odcinków jest mniej niż zapas + 2; **pilnie** (zapas + 1 albo mniej wolnych) — krok także
  między prośbami, żeby zapis nie musiał sam kompaktować odcinka (do 64 MiB; `test_odcinki`: 600 zapisów po 1 MiB przy
  75 % zajętości — z krokami żaden zapis nie kompaktuje sam, bez kroków 196);
- każda prośba klienta i każdy krok są mierzone: **najdłuższa przerwa w odpowiedziach** trafia do logu z końcem pracy
  („kompaktowanie (w przerwach): odcinek wolny po n krokach, najdłuższy krok … s (procesor … s); najdłuższa przerwa w odpowiedziach
  … s”), a prośba dłuższa niż 1 s — osobno („skarbiec nie odpowiadał … s”, najwyżej raz na 10 s);
- błąd kroku wstrzymuje kompaktowanie w przerwach do końca sesji (zapis i tak kompaktuje, gdy musi).

## Czas otwarcia (0.8.1, RYZYKA R58; format 2 — ADR 0036)
Klienci nie czekają: ekran po 1,5 s rusza z ustawieniami domyślnymi i stosuje zapisane, gdy przyjdzie odpowiedź
(0.8.1, gdy format 1 czytał przy otwarciu każdy rekord). Format 2: otwarcie czyta spis i metryczki ogona, nie treść — 4 GiB plików: 0,001 s procesora i 0,4 MiB odczytu
(format 1: 6,1 s i 1,9 GiB; pomiar `testy/host/pomiar_magazynu.c`, ADR 0036). W systemie od kroku 1c planu 0.8.2:
log „magazyn otwarty: n obiektów (format 2, t s)”; w QEMU magazyn z filmem 300 MB — ekran dostaje ustawienia od razu
(`test-pendrive.sh`, ponowny start).

## Obsługa błędów
Brak partycji, dwie partycje „K1-DANE”, obce dane, uszkodzony magazyn, magazyn z hasłem (do M5e) →
dane tylko w pamięci, partycja nietknięta, wpis w logu. Awaria skarbca → nadzorca uruchamia go
ponownie i restartuje ekran (klienci dostają nowe kanały).

## Powierzchnia ataku
Parser nagłówka i rekordów (`naglowek.c`, `magazyn.c`, `odcinki.c`, `spis.c` — spis i ogon, testy + fuzzing także z poprawnym MAC — bez hasła napastnik
zna klucz), komunikaty klientów (32 B), memfd od klientów (pieczęcie i rozmiar sprawdzane), od 0.8.0 nazwy plików
od klientów i wpisy z dysku (`k1_dok_wpis_czytaj` — każde pole) oraz tabela praw.

## Testy
| Test | Rodzaj |
|---|---|
| `test_odcinki` (host, 0.8.2 krok 1a, ADR 0036): format 2 — przerwa prądu po każdym zapisie (i rozdarty nagłówek) z kompaktowaniem i spisami, zmienione i zamienione bajty, nagłówki z poprawnym MAC, które kłamią, stary odcinek podłożony po ponownym użyciu, pełny magazyn, kroki kompaktowania, powiększenie, 70 000 obiektów; otwarcie czyta < 1/16 danych ; od 0.10.0 także pusta partycja, za mała, obce dane i magazyn formatu 1 (rozpoznany, nietknięty) | funkcjonalny + negatywny |
| `test_indeks` (host, 0.8.1): indeks 200 000 losowych operacji (dodaj, usuń, przenieś), także same kolizje | funkcjonalny |
| `test-skarbiec.sh` (QEMU, 0.8.1; 0.10.0): K1-DANE z magazynem formatu 1 — skarbiec go nie otwiera ani nie nadpisuje (na hoście ta sama suma), pulpit startuje; K1-DANE 190 MiB z 20 000 wersjami jednego obiektu — otwiera się od razu (format 2: spis i ogon), na hoście ostatnia wersja; K1-DANE 64 MiB prawie pełna — „Nowy folder” odrzucony (−28), ustawienie się zapisuje, Pliki: „mało miejsca”; krok 3: ta sama pełna K1-DANE na pendrivie większym o 512 MiB → Miejsce → „Użyj całego pendrive'a” → 64 → 576 MiB, miejsce na dane 58 → 556 MiB, import 16 zdjęć po 2,8 MB (16 z 16), ponowny start, tablica (sfdisk); tablica powiększona bez magazynu → dokończenie przy starcie. Do 0.9.1 także długi dziennik formatu 1 i przejście na format 2 | funkcjonalny + negatywny |
| `fuzz_magazyn` (libFuzzer): surowy obraz, zmiany bajtów, nagłówki z poprawnym MAC (format 2), losowe ciągi operacji z ponownym otwarciem (od 0.10.0 bez trybów formatu 1) | fuzzing |
| `test_dokumenty` (0.8.1 krok 4): zapis w toku przez prawdziwy kanał — plik 2 MiB + 12 345 B w trzech kawałkach, odczyt kawałkami, kawałek po krótkim (zapis porzucony), koniec bez zapisu, odczyt za końcem, nowa treść kawałkami przerwana Koszem, porzucenie przez klienta i zamknięty kanał, dwa zapisy na kanale i na pliku, przerwa prądu (kawałki usunięte przy starcie), 4096 kawałków i ani jednego więcej | funkcjonalny + negatywny |
| `test_komunikat`: komunikaty `SKARB_*` (zakresy, kierunki, deskryptory; 0.8.1 krok 4: `DOK_KAWALEK`, `DOK_KONIEC`, rozmiary do 4 GiB w dwóch polach, kawałki w `DOK_CZYTAJ`/`DOK_TRESC`/`PEN_*`; krok 3: `SKARB_MIEJSCE`, `SKARB_POWIEKSZ`, `POWIEKSZ_DANE`, `POWIEKSZONO`, zapas w `SKARB_KLIENT`; 0.8.2: `START_POTWIERDZONY`; 0.8.5: `SKARB_PODZIAL`, `SKARB_CZESC` — numer części, rezerwa bez plików, ujemne, pola zarezerwowane) | negatywny |
| `test_dokumenty` (0.8.5): `dok_podzial` — pliki w folderach użytkownika liczą się do folderu stałego, folder w Koszu z plikiem i plik w Koszu — do Kosza, pusty plik, foldery nic, rezerwa; po przywróceniu z Kosza — znów w folderze; kanał (`miejsce_podzial`): Ustawienia dostają wszystkie części, poprawne i po kolei, NEGATYWNY — Pliki, Notatki, Podgląd, pulpit nie dostają nic | jednostkowy / negatywny |
| w systemie: klatka (UID, tylko partycja „K1-DANE”), próba w klatce `skarbiec`, dysk trzyma tylko skarbiec | funkcjonalny + negatywny |
| `test_skarb_zalegle` (host, 0.9.0 krok 2): kolejka zaległych na prawdziwym kanale — od razu, gdy kanał przyjmuje; pełny kanał: czekają w kolejności, z deskryptorem, `DOK_ZMIANA` raz, po wznowieniu odbioru wszystko dochodzi; zamknięty kanał i koniec klienta zwalniają memfd; pełna kolejka liczy stratę | funkcjonalny + negatywny |
| w systemie (`t_skarb_zalegle`, 0.9.0 krok 2): klient przestaje odbierać i wysyła prośby `SKARB_INFO`, aż skarbiec przestanie je brać; w tym czasie drugi klient tworzy folder → pierwszy po wznowieniu dostaje odpowiedź na każdą prośbę i „pliki się zmieniły”, skarbiec ten sam. Na skarbcu 0.8.7 czerwony: 1500 wysłanych, 236 odpowiedzi, 0 zmian | negatywny |
| `test-m4.sh`: tapeta, motyw, rozdzielczość i notatka wracają po wyłączeniu i ponownym starcie z tym samym dyskiem; na dysku brak jawnego tekstu | funkcjonalny |
| `test_dokumenty` (host, 0.8.0): nazwy (złe znaki, UTF-8, odwrócony tekst), typy z zawartości, wpis i lista (każde pole poza zakresem), plik w trzech kawałkach, nowa treść bez starych kawałków, przerwany zapis, foldery (unikalne nazwy, „(2)”, do siebie, za głęboko), Kosz (przywracanie, 30 dni, cofnięty zegar, opróżnianie), uszkodzone wpisy i kawałki przy otwarciu; prawa przez prawdziwy kanał: Pliki, pulpit (tylko Pulpit, bez treści), inni — nic; zły memfd | funkcjonalny + negatywny |
| `test_dokumenty` (0.8.2 krok 2): zasady nazw (próba T1/6: znaki niewidoczne, kierunku, spacje na brzegach, znak diakrytyczny bez litery, NFD → NFC, porównanie Ä/ä, Č/č), wolna nazwa dla nazwy z długim „rozszerzeniem” (próba T1/2), wpisy z 0.8.1 z zakazaną nazwą poprawione przy wczytaniu (treść cała), `DOK_CZAS` (tylko Pliki, tylko plik z pendrive'a) | funkcjonalny + negatywny |
| `test_dokumenty` (0.8.1 krok 2): 16 384 wpisy i ani jednego więcej, wczytanie i usunięcie drzewa tysięcy wpisów (< 0,1 s), rezerwa 8 MiB przy pełnych plikach | funkcjonalny + negatywny |
| `test_dokumenty` (0.8.1 krok 1): wpis z jednym złym bajtem nazwy — naprawiony, treść 3 MiB cała (próba T2 planu); nieznany rodzaj i zła generacja — nic nie usunięte, po poprawce plik wraca; nic z nazwy — „Odzyskany plik”; nowy wpis nie bierze numeru nieczytelnego; folder aplikacji po numerze (plik „Kalkulator”, zmiana nazwy, przeniesienie, Kosz); zegar 2000 / 5 lat w przód / cofnięty / po ponownym starcie / bez dobrego czasu; licznik Kosza; 65. plik dla aplikacji | funkcjonalny + negatywny |
| `test_notatki` (host, 0.8.1): prawdziwy kod Notatek (`pliki.c`, `spis.c`) z prawdziwym kodem skarbca przez gniazdo — notatka nie do odczytu (tylko do odczytu, plik nietknięty, ponowna próba), plik notatki do Kosza w trakcie pisania (kopia, w obu kolejnościach), 300 plików → 256 najnowszych z komunikatem | funkcjonalny + negatywny |
| `fuzz_dokument`: lista wpisów (Pliki i pulpit ją czytają), nazwy, naprawa nazw (zawsze pusta albo poprawna), typy | fuzzing |
| `test-m4.sh` (0.8.0): foldery i Kosz w Plikach, plik na pulpicie, wszystko po ponownym starcie | funkcjonalny |
| `t_skarb_strefy` (w systemie, 0.10.0 krok 5): sesja Ogólna zapisuje i czyta kawałek swojego dysku; NEGATYWNE: Bank go nie widzi (`K1_MAG_BRAK`), Notatki nie nazwą obiektu dysku (`K1_MAG_ARG`); „Wyczyść dane sesji” od nadzorcy — kawałek znika | funkcjonalny + negatywny |
| `test-m5.sh`: obraz USB przez UEFI — pierwszy start tworzy magazyn, drugi go otwiera | funkcjonalny |
| `test-m5.sh` (0.5.8): pendrive z pustą K1-DANE podłączony po starcie → restart skarbca, nowy magazyn, pulpit od nowa | funkcjonalny |
