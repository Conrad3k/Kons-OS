# 0029. Pliki 0.8.0: dokumenty w skarbcu, foldery jak w Windows, Kosz

- Status: Przyjęty
- Data: 2026-11-10
- Dotyczy: ADR 0026 p. 3 (zakres 0.8.0), ADR 0027 (przepływ plików), ADR 0015 (format skarbca),
  SPEC §9 (Blob Vault) — `[korekta: ADR 0029]` przy „magazyn obiektów zamiast drzewa katalogów”

## Kontekst
ADR 0026 zostawił właścicielowi pytanie: w Plikach foldery czy etykiety? Odpowiedź (2026-11-10):
**„co do folderów — niech będą takie, jak w Windowsie”**. SPEC §9 mówi o magazynie obiektów zamiast drzewa
katalogów — chodziło tam o to, żeby **jądro** nie parsowało systemu plików i żeby aplikacje nie dostawały
ścieżek. Foldery widziane przez użytkownika tego nie zmieniają: to metadane dokumentów w skarbcu, nie
katalogi w jądrze; żadna aplikacja nie dostaje ścieżki.

Do tej pory skarbiec znał tylko obiekty właściciela (aplikacji): numer 1..64, nazwa 32 bajty, najwyżej
256 żywych obiektów, każdy ≤ 1 MiB. Wspólnych dokumentów nie było.

## Decyzja
1. **Foldery jak w Windows.** Stałe foldery: **Pulpit, Dokumenty, Obrazy, Muzyka, Wideo, Pobrane** (stałe
   numery 1–6, nie da się ich usunąć ani zmienić im nazwy). W nich własne foldery użytkownika, dowolnie
   zagnieżdżone (najwyżej 8 poziomów). Plik leży w dokładnie jednym folderze. Nazwy jak w Windows: 1–127
   bajtów UTF-8, bez znaków `\ / : * ? " < > |` i znaków sterujących, bez spacji i kropki na końcu, unikalne
   w folderze bez względu na wielkość liter (ASCII); nowy folder to „Nowy folder”, „Nowy folder (2)”…
   Folder **Pulpit** = pliki widoczne na pulpicie.

2. **Dokument w skarbcu** — nowy właściciel `K1_WL_DOKUMENTY` (wspólny, nie aplikacji):
   - **wpis** (metadane) — obiekt `m<numer>`: rodzaj (plik/folder), folder nadrzędny, nazwa, typ, rozmiar,
     czasy utworzenia i zmiany, pochodzenie (np. „K1”, później „pendrive”), kto utworzył, czas wrzucenia do
     Kosza i folder, z którego przyszedł; format w `src/skarbiec/dokumenty.h`;
   - **treść** — obiekty `t<numer>g<generacja>k<kawałek>` po ≤ 1 MiB; nowa treść dostaje nową generację,
     wpis zapisywany jest **po** treści, a stare kawałki usuwane po wpisie — przerwany zapis zostawia starą
     wersję (przy otwarciu skarbiec usuwa kawałki bez wpisu);
   - limity: 1024 wpisy (pliki i foldery), 16 MiB na dokument; magazyn: do 4096 żywych obiektów.
   Metadane osobno od treści (SPEC §9); treść nigdy nie jest wykonywalna (skarbiec oddaje ją w memfd
   z pieczęciami, zapis tylko w noexec).

3. **Typ z zawartości, nie z nazwy**: skarbiec rozpoznaje go przy każdym zapisie po pierwszych bajtach
   (PNG, JPEG, PDF, ZIP, tekst UTF-8, inne). Typy są **jedną tabelą** `K1_TYPY` (`src/wspolne/k1_dokument.h`:
   nazwa, ikona) — kolejny typ = wiersz.

4. **Kosz** (decyzja właściciela z ADR 0026): „Usuń” przenosi do Kosza (folder razem z zawartością);
   „Przywróć” wraca do folderu, z którego przyszedł (gdy go już nie ma — do Dokumentów, nazwa z „(2)”, gdy
   zajęta); „Usuń na zawsze” i „Opróżnij Kosz” tylko w Koszu. Skarbiec usuwa na zawsze to, co leży w Koszu
   ponad 30 dni — przy otwarciu magazynu i co godzinę — według zegara komputera; gdy zegar jest wcześniej
   niż czas wrzucenia (cofnięty), nic nie usuwa.

5. **Prawa — tabela w skarbcu** (jeden wiersz na profil, `dokumenty.c`):
   - **Pliki** — wszystko (lista, treść, nowy folder, zmiana nazwy, przeniesienie, Kosz, przywracanie,
     usuwanie na zawsze, opróżnianie);
   - **pulpit (gui)** — tylko folder Pulpit: lista jego plików i liczba rzeczy w Koszu, „Usuń” (do Kosza),
     „Opróżnij Kosz”; nazw z innych folderów ani treści nie dostaje (przejęte gui nie czyta dokumentów);
   - pozostałe aplikacje — nic. Notatki, okno wyboru pliku i „Otwórz w…” dostaną wąskie prawa w następnym
     kroku (dostęp do jednego dokumentu po geście użytkownika, ADR 0027).
   Właściciela kanału podaje nadzorca (jak dotąd), więc aplikacja nie może podać się za Pliki.

6. **Protokół** — komunikaty `K1_KOM_DOK_*` w kanale do skarbca (32 B, sprawdzane pole po polu); lista
   wpisów, nazwy i treść jadą w zapieczętowanym memfd. Po każdej zmianie skarbiec wysyła `DOK_ZMIANA` do
   wszystkich klientów z prawem listy (Pliki i pulpit odświeżają widok). Lista ma stały układ
   (`k1_dokument.h`, `_Static_assert`) i jest sprawdzana przez odbiorcę (test i fuzzing).

7. **Otwieranie z pulpitu** — w tym kroku podwójne kliknięcie pliku na pulpicie otwiera Pliki z tym plikiem
   (podgląd), a Kosza — Pliki w Koszu: gui prosi nadzorcę o uruchomienie Plików z miejscem, nadzorca podaje
   je Plikom osobnym kanałem (fd 5). Domyślne aplikacje dla typów — w następnym kroku.

8. **0.8.0 w krokach** (każdy z testami, jedno wydanie 0.8.0 na końcu): (1) dokumenty, foldery, Pliki,
   Kosz, pliki na pulpicie; (2) Notatki na dokumentach, „Otwórz w…”, domyślne aplikacje, okno wyboru pliku;
   (3) zdjęcia z pendrive'a (czytnik FAT w klatce); (4) dekoder PNG/JPEG w jednorazowym procesie, tapeta ze
   zdjęcia; (5) K1-DANE do końca pendrive'a, porządki.

## Konsekwencje
- Skarbiec rośnie o moduł dokumentów — przedtem `main.c` dzielony na części (zasada „dziel przed
  rozbudową”); `RLIMIT_FSIZE` skarbca 2 → 24 MiB (memfd z dokumentem do 16 MiB).
- Nowa powierzchnia ataku: komunikaty `DOK_*`, nazwy od klientów, lista po stronie klientów — testy
  negatywne na hoście (prawa, nazwy, zakresy), fuzzing listy, scenariusz w QEMU.
- Na pendrivie K1-DANE ma dziś 64 MiB (połowa na kompaktowanie) — miejsca na zdjęcia przybędzie w kroku 5.
- Foldery to metadane: przeniesienie folderu zmienia jeden wpis, nie kopiuje treści.
