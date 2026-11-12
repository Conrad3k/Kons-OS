# 0036. Skarbiec na lata — format 2: dziennik w odcinkach, spis skarbca, kompaktowanie po odcinku

- Status: Przyjęty, wykonany (format na dysku i testy — 0.8.2 krok 1a; kompaktowanie w przerwach — 1b; przejście
  starych magazynów i format 2 w systemie — 1c)
- Data: 2026-11-11
- Dotyczy: ADR 0015 (format skarbca — **uzupełnia go nowym formatem; format 1 zostaje do odczytu i przejścia**),
  ADR 0033 (powiększanie), ADR 0034 (limity, kompaktowanie wcześniej), `docs/PLAN-0.8.2.txt` (krok 1, luki L25–L28),
  RYZYKA R57, R58

## Kontekst
Format 1 (ADR 0015) to dziennik w **dwóch obszarach po połowie K1-DANE**: zapisy idą do aktywnego, a gdy się zapełni,
wszystkie żywe obiekty są kopiowane do drugiego (kompaktowanie). Przy danych użytkownika liczonych w gigabajtach
(pliki od 0.8.0, cały pendrive na dane od 0.8.1) ten układ ma cztery wady, które wynikają z niego samego (plan 0.8.2,
punkt 5):
- **L25 — start rośnie z danymi:** nazwa obiektu jest w tym samym szyfrogramie co treść, więc otwarcie odszyfrowuje
  każdy żywy rekord — ok. 30 s na 1 GB z pendrive'a USB 2.0 przy każdym starcie (R58);
- **L26 — połowa K1-DANE zawsze pusta** (zapas na kompaktowanie);
- **L27 — kompaktowanie przepisuje wszystko naraz** i przez ten czas skarbiec nie odpowiada (700 MiB ≈ 50 s z USB 2.0;
  R57);
- **L28 — sufit ok. 64 GiB plików** (65 536 obiektów × kawałek 1 MiB), niezależnie od wielkości dysku.
Sam indeks (zapis nazw obok treści) naprawiłby tylko L25, a format i tak trzeba by zmienić dla L26–L28 — zmieniamy go
raz, póki danych na pendrive'ach jest mało.

## Decyzja
Nowy format na dysku (**wersja 2** nagłówka; kod: `src/skarbiec/odcinki.c`, `spis.c`, `naglowek.c`; opis bajt po
bajcie: `docs/komponenty/skarbiec.md`). Kryptografia, klucz główny, hasło i nagłówek w dwóch kopiach — bez zmian
(ADR 0015).

1. **Odcinki.** Za nagłówkami K1-DANE dzieli się na odcinki równej wielkości. Wielkość: miejsce na 2, 4 … 64
   największe rekordy (rekord z kawałkiem 1 MiB to 257 bloków po 4 KiB, więc kawałki wypełniają odcinek do końca,
   bez reszty), tak by odcinków było co najmniej 16; na dużym pendrivie odcinek ma 64 kawałki (64,25 MiB). Najwyżej
   32 768 odcinków (2 TiB; większa partycja — tyle). Bardzo mała partycja (testy) — odcinki 64–256 bloków.
2. **Rekordy** dopisywane są do **głowy** — ostatniego odcinka ogona (pkt 3). Każdy ma dwie części szyfrowane
   osobno (XChaCha20-Poly1305): **metryczkę** (typ, właściciel, długość, czas, nazwa) i **treść**. Dane dodatkowe
   metryczki: uuid magazynu, numer odcinka, **generacja odcinka** (numer tego użycia odcinka, rośnie przy każdym),
   pozycja, liczba bloków; dane dodatkowe treści: MAC metryczki. Rekordu nie da się zmienić, przesunąć, wkleić
   z innego magazynu ani **wziąć ze starszego użycia tego samego odcinka**. Treść sprawdzana przy każdym odczycie.
3. **Spis skarbca** (punkt kontrolny): lista żywych obiektów (właściciel, nazwa, odcinek, pozycja, długość) i generacje
   odcinków w użyciu, w rekordach typu „spis” (kawałki do 1 MiB) na początku odcinka. Nagłówek wskazuje odcinek
   spisu i **ogon** — odcinki zapisane po spisie, po kolei (najwyżej 128; każdy z końcem i generacją). Nowy spis
   powstaje z nową głową, gdy od poprzedniego zapisano ponad 64 MiB (i ponad 16 × jego wielkość) albo ogon ma
   ćwierć odcinków — ale nie, gdy brakuje wolnych odcinków (wtedy dopiero przy pełnym ogonie).
   **Otwarcie** = nagłówek → spis → metryczki rekordów ogona (jeden blok na rekord), **nigdy treść**.
4. **Kompaktowanie po odcinku.** Odcinek jest wolny, gdy nie ma w nim żywych obiektów i nie należy do ogona (ogon może
   mieć usunięcia, o których spis jeszcze nie wie). Kompaktowanie wybiera odcinek spoza ogona z najmniejszą ilością
   żywych danych i przenosi jego obiekty do głowy — **po kilka MiB na raz** (`k1_mag_kompaktuj_krok`), z zatwierdzeniem
   nagłówka po każdym kroku; gdy przeniesie ostatni, odcinek jest wolny. W pracy opłaca się, gdy wolnych odcinków jest
   mniej niż zapas + 2, a odcinek da co najmniej ćwierć miejsca; przy otwarciu nigdy nie jest potrzebne.
5. **Zapas:** 3 % odcinków, co najmniej 2 — na kompaktowanie i spis. Miejsce na pliki = odcinki bez zapasu (91–96 %
   K1-DANE zamiast 50 %). **Zapis danych zawsze zostawia jeden wolny odcinek** (kompaktowanie zawsze może skończyć
   odcinek); usunięcie i kompaktowanie mogą wziąć ostatni. Brak miejsca w głowie i wolnych odcinków → kompaktowanie
   całych odcinków (takich, które dadzą co najmniej jeden największy rekord) albo nowy spis (odcinki starego ogona
   wolno wtedy kompaktować) → dopiero potem „brak miejsca”.
6. **Kolejność zapisu** jak w formacie 1: rekord → `fdatasync` → nagłówek → `fdatasync`. Nowa głowa najpierw
   zatwierdza to, co zapisano przed nią — odcinek zwolniony przez rekordy, których nagłówek jeszcze nie zna, nigdy nie
   jest nadpisany. Przerwa prądu w dowolnym miejscu = stan sprzed albo po zapisie (test po każdym zapisie, także
   z rozdartym nagłówkiem).
7. **Do 262 144 obiektów** (256 GiB plików w kawałkach po 1 MiB; L28). Indeks i tablice odcinków mają wielkość
   według partycji (obiekt zajmuje co najmniej blok), więc mały magazyn otwiera się z małymi tablicami. Format 1 —
   jak dotąd 65 536.
8. **Format 1 zostaje** (`obszary.c`): stary magazyn otwiera się jak dziś. Do kroku 1c system dalej **tworzy** format 1
   (`K1_MAG_FORMAT`); krok 1c przenosi stare magazyny na format 2 dopiero **po potwierdzeniu startu** nowej wersji
   (wycofanie aktualizacji A/B musi dalej czytać dane), korzystając z pustej dziś połowy partycji, i od tego
   kroku nowe magazyny mają format 2. Wersja 0.8.1 widzi magazyn formatu 2 jako „obce dane” i **nie nadpisuje** go
   (ADR 0015) — wycofanie po przejściu daje pracę bez K1-DANE, nie utratę danych.

## Konsekwencje
- **Pomiar** (host, bez `fdatasync`, `testy/host/pomiar_magazynu.c`; wzór próby T3):

  | K1-DANE / pliki | format 1: miejsce · otwarcie · najdłuższa przerwa | format 2: miejsce · otwarcie · najdłuższa przerwa |
  |---|---|---|
  | 1 GiB / 400 MiB | 50 % · 1,34 s, czyta 402 MiB · 2,3 s | 91,0 % · 0,001 s, czyta 0,1 MiB · krok 0,02 s |
  | 4 GiB / 1,9 GiB (f1), 3,6 GiB (f2) | 50 % · 6,1 s, czyta 1,9 GiB · 11,2 s | 95,7 % · 0,001 s, czyta 0,4 MiB · zapis 0,17 s, krok 0,02 s |
  | 64 MiB (maszyna wirtualna) / 40 MiB | 50 % | 91,0 % · 0,000 s · krok 0,005 s |

  Na pendrivie USB 2.0 czas to głównie odczyt: format 2 czyta przy starcie spis i metryczki ogona (zwykle < 1 MiB,
  najwyżej ok. 64 MiB + odcinek przy ogonie z samych małych rekordów).
- **Najdłuższa przerwa:** krok kompaktowania (4 MiB) to ok. 0,02 s procesora; zapis, który przy prawie pełnym
  magazynie sam musi skompaktować odcinek, czeka na przeniesienie do 64 MiB (0,17 s procesora, na USB 2.0 kilka
  sekund) — krok 1b przenosi kompaktowanie w przerwy między prośbami, zanim zabraknie wolnych odcinków.
- **Bez hasła** napastnik z nośnikiem zna klucz i może zbudować dowolny poprawny magazyn (jak w formacie 1, ADR 0015);
  parser formatu 2 (nagłówek, spis, ogon) sprawdza każde pole i jest fuzzowany także z poprawnym MAC
  (`fuzz_magazyn`, tryby 3–5). Cofnięcie całego nośnika do starszej kopii — nadal R38.
- **Pamięć skarbca:** przy 262 144 obiektach indeks i wpisy ok. 19 MiB (limit klatki 320 MiB, z czego Argon2id do
  256 MiB tylko przy haśle); mały magazyn — tyle, ile potrzebuje.
- **Mały magazyn marnuje więcej** końcówek odcinków przy mieszaniu małych rekordów z kawałkami 1 MiB (odcinek na
  2–8 kawałków); od kilku GB odcinek ma 64 kawałki i strata jest pomijalna.
- Testy: `test_odcinki` (przerwa prądu po każdym zapisie i z rozdartym nagłówkiem, zmienione i zamienione bajty,
  nagłówki z poprawnym MAC, które kłamią, stary odcinek podłożony po ponownym użyciu, pełny magazyn, kroki
  kompaktowania, powiększenie, 70 000 obiektów), `fuzz_magazyn` (tryby 3–5: zmiany bajtów, pola nagłówka, losowe
  ciągi operacji z ponownym otwarciem), `test_magazyn` (format 1 bez zmian).

## Wykonanie kroków 1b i 1c (2026-11-12)
- **Kompaktowanie w przerwach** (`src/skarbiec/przerwy.c`): krok 4 MiB po 0,5 s bez próśb, gdy wolnych odcinków jest
  mniej niż zapas + 2; **pilne** (zapas + 1 albo mniej) — krok także między prośbami. Na hoście 600 zapisów po 1 MiB
  w losowej kolejności przy 75 % zajętości: z krokami żaden zapis nie kompaktuje sam, bez kroków — 196 (`test_odcinki`).
  Każda prośba i każdy krok są mierzone; najdłuższa przerwa w odpowiedziach trafia do logu z końcem pracy.
- **Przejście** (`src/skarbiec/przejscie.c`, pkt 8): po `START_POTWIERDZONY` od nadzorcy (A/B sprawdzone po 15 s, nie
  czeka próba innej wersji) żywe obiekty są kopiowane krokami do odcinków **poza całym aktywnym obszarem** formatu 1
  (do wolnej połowy), format 1 działa w tym czasie; obiekty zmienione po kopii (rozpoznane po bloku rekordu) — w ostatnim
  kroku jeszcze raz; potem spis i nagłówek formatu 2 w obu slotach. Kompaktowanie i powiększenie formatu 1 zaczynają
  kopiowanie od nowa. Gdy dane nie mieszczą się w wolnej połowie — przejście odłożone (format 1 działa), ponownie po
  zwolnieniu 8 MiB albo powiększeniu. Format 1 zapisuje już tylko magazyn sprzed przejścia; nowe magazyny mają format 2.
- **QEMU** (`test-skarbiec.sh`): K1-DANE 64 MiB z 0.8.1 prawie pełna (25 MiB) — przejście w 7 krokach, najdłuższy krok
  0,22 s (TCG, razem 4,2 s), miejsce na dane 31 → 58 MiB, na pliki 23 → 50 MiB, każdy obiekt bajt w bajt (sprawdzone na hoście);
  magazyn z filmem 300 MB otwiera się przy ponownym starcie od razu i ekran dostaje ustawienia przy starcie.
- **Błąd z kroku 1a znaleziony w QEMU:** spis zapisuje liczbę odcinków z chwili zapisu; po powiększeniu partycji
  otwarcie wymagało liczby z nagłówka i odmawiało („dane uszkodzone”, dane całe) aż do następnego spisu. Teraz spis może
  znać mniej odcinków niż nagłówek — dopisane są wolne (generacja 0); test powiększenia ze spisem i przerwą prądu po
  każdym zapisie w `test_odcinki`. Format 2 nie był jeszcze w żadnym wydaniu.

## Rozważane alternatywy
- **Tylko indeks na starym układzie** (nazwy i położenia zapisywane osobno): naprawia L25, zostawia połowę partycji
  pustą, kompaktowanie wszystkiego naraz i sufit 64 GiB — a format i tak się zmienia.
- **Podsumowanie na końcu każdego odcinka** (jak w LFS) zamiast spisu: otwarcie czyta jedno podsumowanie na odcinek,
  ale usunięcia trzeba trzymać, dopóki żyje starsza wersja w innym odcinku, a kolejność wersji wymaga numerów
  rekordów — więcej przypadków brzegowych niż jeden spis.
- **Odcinki jako potęga dwójki** (np. 64 MiB): rekord z kawałkiem 1 MiB ma 257 bloków, więc odcinek mieści 63 kawałki
  i resztę; w małych magazynach (8 MiB → 7 kawałków) dane zjadały zapas wolnych odcinków (test pełnego magazynu).
  Odcinek = wielokrotność 257 bloków.
- **Większe obiekty** (kawałki 4 MiB) zamiast więcej obiektów: zmienia protokół plików i pamięć (bufor rekordu)
  bez potrzeby — 262 144 obiekty mieszczą się w pamięci skarbca.
- **System plików z jądra** (ext4, f2fs) — odrzucone w ADR 0015 (jądro parsuje dane z obcego nośnika, R34).
