# 0056. Łańcuch wydań (0.13.0): zwłoka wydania 20 minut, ogłoszenie formatu 2 z terminem ważności, czas z HTTPS, akcje przypięte skrótem

- Status: Przyjęty, wdrożony (plan 0.13.0, krok 1)
- Data: 2026-11-26
- Dotyczy: ADR 0046 (aktualizacje przez sieć — ogłoszenie, poczekalnia, strefa pobierania; **uzupełniony**), ADR 0047
  (robot — jego wydania też czekają), ADR 0021 (klucz wydania w CI — bez zmian); Issue #230, #238 (p. 2); plan
  `docs/archiwum/PLAN-0.13.0.txt` (krok 1, D1, T1, T2); opisy: `docs/komponenty/aktualizacja.md`, `docs/komponenty/robot.md`,
  `docs/komponenty/instalator.md`

## Kontekst
Od 0.10.5 laptop z K1 sam pobiera każde wydanie z GitHuba i po jednym „Zainstaluj” je instaluje (ADR 0046); robot wydaje
sam (ADR 0047). Klucz podpisu jest tylko w GitHub Actions (ADR 0021). Przegląd bezpieczeństwa (2026-11-26, Issue #230,
priorytet 1): kto przejmie konto GitHub albo CI, wyda podpisaną wersję, która trafi na każdy laptop. Do tego (luki L2–L5
planu): ogłoszenie ma czas wydania, ale nie ma terminu ważności — stare ogłoszenie podsunięte przez łącze K1 przyjmie
jako „najnowsze” i zostanie na starej, dziurawej wersji; zegar jest tylko z SNTP (bez podpisu, R83); akcje GitHuba
przypięte tagiem; Ustawienia nie odróżniają „najnowsza” od „nie wiem”.

Z kodu (0.12.3): czytnik ogłoszenia (`k1_ogloszenie_czytaj`) wymaga formatu 1 i zer w bajtach 96–127, a długość pliku
musi być dokładna — **każde nowe pole w tym samym pliku zamknęłoby drogę aktualizacji** K1 0.12.3 na dyskach laptopów.

## Decyzja
1. **Zwłoka wydania 20 minut (D1 właściciela: A — „laptop czeka 20 minut; gdyby przejęto mi gita, to dostatecznie dużo
   czasu na zatrzymanie katastrofy”).** Nowa wersja pobrana do poczekalni czeka `K1_OGL_ZWLOKA` = 20 min, zanim K1
   zaproponuje „Zainstaluj”. Liczy ją **laptop, od chwili, gdy pierwszy raz miał tę wersję** (pobrał ją), nie od czasu
   wpisanego przez CI — kto przejmie CI, wpisze dowolny czas. Po 20 minutach nadzorca sprawdza wydanie jeszcze raz
   (`POTWIERDZ_PO` = 21 min, także przy wyłączonym „Sprawdzaj sam” — kończy to, co zaczęło „Sprawdź teraz”); dopiero
   świeże, podpisane ogłoszenie, które **wciąż** nazywa to samo wydanie (wersja, SHA-256 paczki, identyfikator budowy),
   potwierdza je. Ogłoszenie starszej wersji albo innej budowy tego samego numeru = wydanie zniknęło z GitHuba
   (właściciel je usunął): K1 **usuwa wersję z poczekalni** i nic nie instaluje. Ogłoszenie nowszej wersji — pobiera
   nowszą (ona czeka od nowa). Gdzie: znak poczekalni **formatu 2** (`k1_paczka.h`: „widziane” i „potwierdzone” w bajtach
   80–95; format 1 z K1 0.12.x czytany dalej — jako jeszcze nie widziany), czysta zasada `k1_pocz_zwloka` (zegar daleko
   wstecz — widziane od nowa), program zapisujący w `STAN` (`siec.c`: `zwloka()`), `ZAINSTALUJ` odmawia niepotwierdzonej
   (`siec_instaluj.c`, `K1_INST_BLAD_MIEJSCE`), nadzorca nie przyjmuje „Zainstaluj” w zwłoce. Ustawienia → Aktualizacje:
   „K1 0.13.1 jest pobrana. Za kilka minut będzie gotowa.” i dlaczego; okienko gui „Jest nowa wersja K1” — dopiero po
   zwłoce (`AKT_STAN` b = 0, c = wersja z `K1_AKT_FL_ZWLOKA`). Nie B (drugi podpis właściciela poza CI): każde wydanie,
   także robota, czekałoby na właściciela i jego drugi klucz — zostaje w „Później” jako mocniejsza ochrona.
2. **Ogłoszenie formatu 2 w osobnym pliku (T1).** Każde wydanie ma od 0.13.0 dwa pliki: `k1-ogloszenie.bin` — format 1,
   bajt w bajt jak dotąd (czyta go K1 do 0.12.x; nigdy się nie zmienia, także przez „Ogłoszenie dnia”) i
   `k1-ogloszenie2.bin` (`K1_OGL_PLIK`) — **format 2**: te same pola i **czas podpisu** (`podpisano`, u64 w bajtach 96–103;
   w formacie 2 nie zero, w formacie 1 zera — sprawdza to czytnik). K1 od 0.13 pyta tylko o `k1-ogloszenie2.bin` i bierze
   z sieci tylko format 2 (format 1 z sieci — odrzucony). Narzędzie: `wydanie ogloszenie PACZKA OPISY CZAS WYJSCIE
   WYJSCIE2` (oba pliki, format 2 podpisany czasem wydania). Zgodność wstecz sprawdza `test_ogloszenie`: **zamrożona kopia
   czytnika z K1 0.12.3** (`testy/host/zgodnosc/`, nigdy nie zmieniana) przyjmuje format 1 tej budowy pole po polu i
   odrzuca format 2. Format 1 robimy, dopóki K1 starszy niż 0.13 może być na laptopie — decyzja o jego końcu: audyt 0.14.
3. **Termin ważności: 7 dni (T1) i „Ogłoszenie dnia”.** K1 nie bierze ogłoszenia podpisanego ponad `K1_OGL_WAZNE` =
   7 dni przed czasem (pkt 4): mówi „Nie mogę sprawdzić, czy masz najnowszą wersję. Informacja o wydaniach jest starsza
   niż 7 dni.” (`K1_SIEC_FL_OGL_STARE` → `K1_AKT_W_STARE`) i nic nie pobiera. Czas podpisu przed czasem K1 (zegar K1
   wstecz) — świeże. Workflow **„Ogłoszenie dnia”** (`.github/workflows/ogloszenie.yml`, 04:23 UTC) co dzień podpisuje
   `k1-ogloszenie2.bin` najnowszego wydania od nowa z dzisiejszym czasem (`wydanie odswiez WE CZAS WYJSCIE`: tylko
   ogłoszenie formatu 2 podpisane **tym samym** kluczem, nigdy czas wcześniejszy niż był, bez klucza — nic; reszta bajtów
   bez zmian) i podmienia plik w wydaniu (`gh release upload --clobber`). Dlatego `k1-ogloszenie2.bin` nie ma w
   `SHA256SUMS` — chroni go własny podpis. Skutek: kto na łączu podsunie stare ogłoszenie, zatrzyma laptop na starej
   wersji najwyżej 7 dni (dotąd na zawsze). Koszt: gdy workflow nie działa ponad 7 dni (np. GitHub wyłącza zaplanowane
   workflow po 60 dniach bez zmian w repo), laptopy mówią „nie mogę sprawdzić” — RYZYKA R111.
4. **Czas z HTTPS (Issue #238 p. 2).** Strefa pobierania dokleja za każdym **całym** pobranym plikiem (curl z kodem 0)
   **stopkę** (`k1_stopka.h`, 16 B: „K1S1”, zera, czas w s): czas z nagłówka `Date` ostatniej odpowiedzi serwera HTTPS
   (curl `--write-out %output{…}%header{date}`, postać IMF-fixdate — `k1_data_http`, sprawdzona na każdym dniu
   1970–2099). Program zapisujący liczy terminy (świeżość ogłoszenia, zwłokę) **według czasu serwera**, gdy jest — ktoś na
   łączu przesunie czas SNTP, ale nie czas za TLS-em (certyfikat sprawdza curl) — a różnicę ponad godzinę wobec zegara
   K1 wpisuje do logu (same minuty). Brak stopki = pobieranie niepełne: ogłoszenie nie wzięte (bez flagi „odrzucone”),
   paczka odrzucona (`K1_INST_BLAD_SIEC`). Zegar K1 dalej tylko z SNTP (NTS — „Później”); czas z HTTPS nie ustawia zegara.
   Ufamy mu tyle, co strefie: zły czas może tylko kazać czekać dłużej albo odrzucić ogłoszenie.
5. **Akcje GitHuba przypięte pełnym skrótem commita (T2).** `actions/checkout`, `actions/cache/restore`,
   `actions/cache/save`, `actions/upload-artifact` w `k1.yml`, `robot.yml` i `ogloszenie.yml` — skrót tego, na co
   wskazywał tag w dniu przypięcia, z wersją w komentarzu (zachowanie bez zmian). Odświeża je **Dependabot** raz
   w miesiącu (`.github/dependabot.yml`): PR z nowszym skrótem; scala go człowiek po zielonych testach (jak każdą zmianę
   CI). Robot akcji nie zmienia.
6. **Droga z 0.12.3.** K1 0.12.3 na dysku czyta `k1-ogloszenie.bin` (format 1) i instaluje 0.13 jak dotąd — bez zwłoki
   (stary program zapisujący jej nie zna). Od 0.13 każda następna wersja (także robota) czeka 20 minut. Paczka
   aktualizacji bez zmian; obraz strefy i program zapisujący jednej wersji są w tym samym wydaniu, więc stopka nie
   potrzebuje zgodności wstecz.
7. **Czego to nie chroni.** Właściciel musi zauważyć obce wydanie w 20 minut (GitHub: „Watch → Releases” z
   powiadomieniami na telefon; logowanie dwuetapowe kluczem sprzętowym — działania właściciela z planu) i usunąć je.
   Wydania robota wyglądają tak samo jak obce — robot stoi od 2026-11-24 (Issue #216, #217), a gdy wróci, jego wydanie
   też czeka. Klucz dalej tylko w CI; laptop, który przez 20 minut po pobraniu nie ma sieci, potwierdzi wydanie przy
   najbliższym sprawdzeniu — gdy go już nie ma, usunie je. Podpis offline / drugi klucz — „Później” (D1 = B, C).

## Skutki
- Każda nowa wersja przez sieć jest gotowa do instalacji najwcześniej 20 minut po pobraniu (świadomy koszt, także dla
  pilnych poprawek — RYZYKA R91).
- Ustawienia → Aktualizacje mają dwa nowe widoki (zwłoka, ogłoszenie za stare); gui pokazuje okienko dopiero po zwłoce.
- Nowe formaty: ogłoszenie 2, znak poczekalni 2, stopka strefy pobierania — z testami na hoście i fuzzingiem.
- Testy: `test_ogloszenie` (format 2, termin, NEGATYWNE: format 2 bez czasu, format 1 z czasem, format 3; zgodność z
  czytnikiem 0.12.3), `test_paczka` (znak 2, znak 1 czytany, zwłoka), `test_stopka` i `fuzz_stopka` (stopka, `Date` wobec
  gmtime, NEGATYWNE: RFC 850, asctime, inna strefa, zły dzień tygodnia, 30 lutego, 24:00), `fuzz_ogloszenie` (format 2,
  zbudowane z powrotem to samo), `test_siec_instalatora` (stopka, format 2 z sieci, za stare / odświeżone, zwłoka, „Zainstaluj”
  w zwłoce — odmowa, wycofanie; NEGATYWNE: bez stopki, paczka bez stopki, format 1), `obrazy_paczki.sh` (`wydanie odswiez`;
  NEGATYWNE: czas wcześniejszy, obcy klucz, format 1, bez klucza), `test_komunikat`, `test_lacznik`;
  `test-aktualizacja-siec.sh` (serwer testowy przesuwa `Date`: zwłoka mija, okienko dopiero po niej; 8 dni — „za stare”,
  różnica zegara w logu).
- RYZYKA: R14, R83, R91, R94, R96 zmienione, nowe R111 („Ogłoszenie dnia” nie działa).
