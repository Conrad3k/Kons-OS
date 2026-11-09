# 0027. Przepływ plików: Pliki jako jedyny węzeł, zgoda = gest w interfejsie K1

- Status: Przyjęty (zasady; szczegóły protokołów — przy implementacji 0.8.0, M8, M9)
- Data: 2026-11-09
- Dotyczy: SPEC §8, §9 (Gatekeeper), ADR 0020 p. 5 (pliki ze stref), ADR 0024 (wspólna strefa),
  ADR 0025 (Przeglądarka K1), ADR 0026 (0.8.0 Pliki)

## Kontekst
Pytanie właściciela (2026-11-09): użytkownik pobiera PDF w przeglądarce (osobna strefa) i chce go
otworzyć we wbudowanej przeglądarce PDF, w edytorze z innej strefy albo w Google Docs — czy to się spina?
Model jest spójny (podobnie działa Qubes OS: każda aplikacja w osobnej maszynie, pliki kopiowane za
zgodą), ale bez wspólnych zasad każdy etap (0.8.0, M8, M9) zrobiłby to po swojemu, a użytkownik dostałby
albo za dużo okienek z pytaniami, albo za mało ochrony. Zasady trzeba ustalić przed 0.8.0, bo okno wyboru
pliku i pochodzenie plików powstają właśnie tam.

## Decyzja
1. **Pliki K1 są jedynym węzłem.** Strefy nie wymieniają plików bezpośrednio — każdy plik przechodzi przez
   Pliki (Gatekeeper). **Oryginał jest zawsze w Plikach**; strefa dostaje **kopię** jednego pliku.

2. **Zgoda = gest użytkownika w interfejsie rysowanym przez K1.** Strona ani aplikacja w strefie nie może
   go podrobić (klawiaturę i mysz przekazuje K1, a te elementy rysuje K1). Gestem jest:
   - kliknięcie „Pobierz” / „Zapisz” na pasku pobierania Przeglądarki K1,
   - wybór pliku w systemowym oknie wyboru pliku,
   - „Otwórz w…” / „Wyślij do…” w Plikach,
   - przeciągnięcie pliku z Plików (albo z pulpitu) na okno aplikacji,
   - kliknięcie „Zapisz zmiany” w powiadomieniu K1 (p. 5).
   **Osobne okno z pytaniem** pojawia się tylko wtedy, gdy przeniesienie zaczyna sama strefa, bez gestu
   (np. zwykła przeglądarka z M8 zapisuje do swojego folderu pobranych, aplikacja kładzie plik do „Do K1”).
   Spełnia to SPEC §9 („czeka na zgodę użytkownika”) bez zasypywania pytaniami.

3. **Pochodzenie zapisane przy każdym pliku** (skąd, która strefa/sesja, kiedy; np. „pobrane z
   example.com, sesja ogólna”) i pokazywane w Plikach. Na jego podstawie K1 przypomina przy wysyłaniu
   pliku z internetu do **wspólnej strefy** (ADR 0024: słabsza izolacja między aplikacjami).

4. **Prośba strony lub aplikacji o plik** (np. „Prześlij” w Google Docs) otwiera **systemowe okno wyboru
   pliku** (0.8.0); strefa dostaje tylko wybrany plik, nigdy listę plików. W Przeglądarce K1 idzie to
   jej wąskim protokołem (ADR 0025). Zwykła przeglądarka w strefie (M8, etap 1) widzi tylko swoją strefę —
   tam plik trzeba najpierw wysłać przez „Wyślij do…” (znane ograniczenie, znika z Przeglądarką K1).

5. **„Otwórz w…” z zapisem z powrotem.** Gdy plik jest wysłany do strefy do edycji, K1 pamięta to
   powiązanie (ten plik ↔ ta strefa). Gdy aplikacja zapisze go ponownie, K1 pokazuje swoje powiadomienie
   „Zapisać zmiany w oryginale?” — jedno kliknięcie, bez szukania folderu „Do K1”. Poprzednia wersja
   oryginału trafia do Kosza (można ją przywrócić przez 30 dni, ADR 0026).

6. **Domyślna aplikacja dla typu pliku** (typ rozpoznany z zawartości, nie z rozszerzenia): podwójne
   kliknięcie otwiera domyślną, „Otwórz w…” — inną. Wybór w Ustawieniach → Aplikacje (tam, gdzie
   przełączniki stref z ADR 0020). Domyślnie: aplikacje K1 (np. PDF → przeglądarka PDF K1).

7. **Pliki skomplikowane i ryzykowne (PDF, obrazy, później dokumenty biurowe) czyta jednorazowy proces
   w klatce** (SPEC §7): bez danych, sieci i innych procesów; oddaje tylko obraz (np. strony PDF jako
   bitmapy), okno aplikacji K1 go wyświetla. **Przeglądarka PDF K1** — osobna wersja po 0.8.0; silnik
   (np. PDFium — BSD, MuPDF — AGPL, Poppler — GPL) wybierany w ADR z uwagą na licencję i wpisem
   w `docs/ZALEZNOSCI.md`; bez JavaScriptu w PDF.

## Konsekwencje
- 0.8.0 musi od początku mieć: pochodzenie w metadanych, okno wyboru pliku dostępne dla aplikacji przez
  kanał (nie przez ścieżki), miejsce na powiązania „otwarty w” i domyślne aplikacje dla typów.
- M8/M9: protokół strefy obejmuje prośbę o plik, przekazanie kopii, zgłoszenie zapisu (p. 5);
  przeciąganie na okno strefy obsługuje ekran K1 (on wie, gdzie jest okno i co jest przeciągane).
- Schowek między strefami — jak w ADR 0020 (za każdym wklejeniem); szczegóły w M9 według tej samej zasady
  gestu.
- Koszt: plik w strefie i w Plikach może się rozjechać, jeśli użytkownik odrzuci zapis — świadomie:
  oryginał zmienia się tylko za jego zgodą.
