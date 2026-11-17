# Komponent: aplikacje natywne (`/rdzen/aplikacje/<program>`)

| | |
|---|---|
| **Status** | PRZETESTOWANE (M4, Zasoby i Ekran: M4.1) — `scripts/test-m4.sh`, testy w systemie (klatki, próby) |
| **Kod** | `src/aplikacje/*/main.c`, szkielet `src/przybornik/k1_aplikacja.c` |
| **Decyzje** | ADR 0008; do 0.12 aplikacje leżą w obrazie rdzenia (`/rdzen/aplikacje`), potem podpisane obrazy w `/aplikacje` |

## Cel
Każda aplikacja to jeden wiersz w `K1_APLIKACJE` (`src/wspolne/k1_uslugi.h`) i katalog
`src/aplikacje/<program>/`; każda ma tu swój rozdział (sprawdza `scripts/sprawdz-czytelnosc.py`).

**Przewijanie (0.10.0 krok 4).** Kółko myszy i dwa palce na touchpadzie przesuwają to, co jest **pod kursorem**,
także w oknie bez fokusu (komunikat `PRZEWIN` od ekranu → `k1_apl.przewin`; `k1_przewin_wiersze`: ząbek = 3 wiersze,
części ząbka z touchpada się sumują). Gdzie: lista w **Plikach** (log „lista przewinięta: od wiersza N z M”, najwyżej
2 razy na sekundę — testy przewijają według niego), lista notatek i sam tekst w **Notatkach** (kursor zostaje, gdzie
był — klawisz albo kliknięcie wraca do niego), lista w **Wyborze pliku**, powiększone zdjęcie w **Podglądzie** (ząbek =
60 px; dopasowane do okna — bez zmian). Ustawienia, Zasoby, Zegar, Kalkulator i Instalator mieszczą się w oknie — nie
przewijają (lista sieci Wi-Fi pokazuje najwyżej 6 — przewijanie jej to pomysł na później).

### Zegar
Tarcza analogowa (sekundnik w kolorze akcentu), godzina, data po polsku, czas
letni/zimowy.

### Notatki
Od 0.5.8 wiele notatek: z lewej lista (tytuł = pierwsza niepusta linia, pod nim druga; ostatnio zmieniona na
górze, strzałki przewijania gdy się nie mieszczą), „Nowa notatka” (Ctrl+N), obok przycisk z folderem —
**„Otwórz…”** (Ctrl+O, 0.8.0): okno wyboru pliku K1, Ctrl+↑/↓ — poprzednia/następna, „Usuń notatkę” w stopce
z potwierdzeniem. Pisanie z polskimi znakami (AltGr), strzałki, Home/End, Backspace, Delete, Enter, kliknięcie
ustawia kursor, zawijanie wierszy, zapis automatyczny (0,8 s po zmianie; napis w stopce mówi, gdzie zapisano).

**Od 0.8.0 każda notatka to plik** (ADR 0030) w **Dokumenty › Notatki** (do 256 najnowszych notatek po 16 KB — przy
większej liczbie komunikat „Pokazano 256 najnowszych notatek”, starsze są w Plikach; 0.8.1): nazwa pliku
idzie za pierwszym wierszem („Lista: zakupy?” → „Lista zakupy”; bez znaków, których nazwa nie może mieć), dopóki
użytkownik nie zmieni jej w Plikach; ta sama nazwa w folderze — „ (2)”. Nowa notatka staje się plikiem przy
pierwszym zapisie; pusta znika po przejściu do innej (plik — do Kosza). „Usuń notatkę” przenosi plik do Kosza
(przywrócisz go w Plikach). Plik tekstowy otwarty w Notatkach z Plików, z pulpitu („Otwórz za pomocą”) albo przez
„Otwórz…” dochodzi do listy; zamiast „Usuń notatkę” ma „Zamknij plik” (plik zostaje na swoim miejscu); większy
niż 16 KB — komunikat w stopce. (Przeniesienie notatek z 0.5.6–0.7.6 do plików, które 0.8.0–0.8.7 robiły przy
pierwszym uruchomieniu, usunięto w 0.9.0 — decyzja właściciela z 2026-11-12: takie notatki nie przejdą już do Plików.)
**Notatki nie gubią tekstu** (0.8.1): notatka, której treści skarbiec nie oddał, jest tylko do odczytu („Nie udało
się wczytać tej notatki — spróbuję ponownie.”) — klawisz nie zapisze pustej notatki na plik; Notatki próbują znowu
przy następnej zmianie plików. Gdy plik notatki trafi do Kosza (w Plikach), a w notatce jest niezapisany tekst,
Notatki zapisują go jako nowy plik w Dokumenty › Notatki („Plik tej notatki trafił do Kosza — zapisano jej kopię.”);
notatka bez nowego tekstu po prostu znika z listy (jej plik jest w Koszu). Folder Notatek skarbiec pamięta po
numerze — zmiana jego nazwy, przeniesienie albo plik o nazwie „Notatki” nie tworzą „Notatki (2)”.
Moduły: `main.c` (okno), `spis.c` (lista, nazwy), `pliki.c` (skarbiec, nadzorca), `tekst.c`, `stare.c`.

### Wybór pliku
(0.8.0, ADR 0030, `src/aplikacje/wybor/`, UID 207) — **okno wyboru pliku K1**: gdy aplikacja prosi o plik
(`DOK_WYBIERZ`), skarbiec prosi nadzorcę o to okno i mówi mu, kto prosi i o jaki typ albo rodzinę typów
(`DOK_PROSBA`; rodzina od 0.8.2 — Podgląd prosi o „obrazy”, ADR 0037). Na górze
„Notatki prosi o plik (Dokument tekstowy). Dostanie tylko ten, który wybierzesz.”; z lewej stałe foldery, nad
listą „w górę” i ścieżka; lista: foldery, potem pliki (innego typu — wyszarzone); „Anuluj” i „Otwórz”
(też podwójne kliknięcie i Enter; Esc i × = „Anuluj”). Okno widzi tylko nazwy widocznych plików (bez treści,
bez Kosza); wybór idzie do skarbca (`DOK_WYBRANO`), który daje aplikacji dostęp do tego jednego pliku. Nie ma
go w menu Start ani na pulpicie (kolumna -1 w `K1_APLIKACJE`); otwarte ręcznie bez prośby — „Żadna aplikacja
nie prosi teraz o plik.” Moduły: `main.c` (okno), `dane.c` (skarbiec).

### Ustawienia
Wygląd (motyw jasny/ciemny, tapeta: 5 wariantów z miniaturami, po 4 w rzędzie; od 0.8.0 szósty kafelek „Zdjęcie”, gdy
ekran ma zdjęcie ustawione w Plikach — bez niego podpowiedź, jak je ustawić; samego zdjęcia Ustawienia nie widzą), Ekran
(rozdzielczość z listy od ekranu — do 18 trybów w trzech kolumnach, „zalecana” = preferowana przez monitor/kartę;
zmiana w locie, ADR 0012), Bateria (0.5.5: procent, stan, wykres naładowania od uruchomienia — linia
w kolorze akcentu z delikatnym wypełnieniem, pasek pod wykresem = zasilacz podłączony, godzina startu,
najniższy poziom; od 0.6.0 pod wykresem ostatnie wcześniejsze uruchomienie z małym wykresem — archiwum
od nadzorcy jako zapieczętowany memfd `BATERIA_ARCH`; bez baterii — napis), Hasło (0.5.7: włącz / zmień / wyłącz, pola z kropkami, Tab/Enter/Esc;
przez skarbiec — fd 5), Mysz (prędkość),
Data i czas (sekundy na pasku), Aplikacje (0.8.0, `aplikacje.c`; od 0.8.2 krok 5 — **każdy** typ pliku i jego
domyślna aplikacja: „Dokument tekstowy — Notatki”, „Obrazy (PNG, JPEG, GIF, BMP, WebP) — Podgląd” (typy jednej rodziny
w jednym wierszu), „Plik”, „Dokument PDF”, „Archiwum ZIP” — „K1 nie ma jeszcze aplikacji do tego typu pliku”; typ, który
otwierają co najmniej dwie aplikacje, ma przyciski wyboru (dziś żaden), wybór ustawia całą rodzinę; Pliki nie są już
aplikacją do otwierania — pokazują każdy plik; ustawienie ekranu `K1_UST_OTWIERANIE` + typ, zapisane w skarbcu; dopisek
odsyła do „Otwórz za pomocą”), Miejsce (0.8.5 krok 3, `miejsce.c`; decyzje właściciela D1–D3, szkic B w `docs/WYGLAD.md`): miejsce na pliki
w K1-DANE (nie pamięć RAM — ta jest w Zasobach; bez K1-DANE: „Pliki są tylko w pamięci do wyłączenia komputera”):
„Wolne: X z Y”, szeroki pasek podzielony na części w kolorach (największy folder — akcent, Kosz i „System” — szare,
mała część co najmniej 3 px), pod nim legenda w dwóch kolumnach (kolor, nazwa, rozmiar jak w Plikach) — foldery stałe,
Kosz, od 0.10.0 krok 5 „Strefy” (dyski sesji przeglądarki, które pamiętają; szałwia `#5F8A6A`), „System” (reszta
zajętego: ustawienia, notatki systemu, stare wersje); części puste nie są pokazane; suma części = zajęte. Pod legendą „Zapas systemu: X — zawsze wolny, żeby dało się zapisać zmiany.” (rezerwa skarbca) i „Pliki i foldery:
n z 16 384”. Liczby od skarbca: `SKARB_INFO` → `SKARB_MIEJSCE`, `SKARB_PODZIAL` → `SKARB_CZESC` (tylko liczby);
przy wejściu do sekcji i co 3 s, gdy jest pokazana (import w Plikach widać od razu). Na pendrivie K1 z wolnym miejscem za
K1-DANE: „Pendrive ma jeszcze X nieużywanego miejsca.” i przycisk **„Użyj całego pendrive'a na dane”** (przeniesiony
z Informacji) — kliknięcie = zgoda, `SKARB_POWIEKSZ`, w trakcie „Powiększam… Nie wyjmuj pendrive'a.”, potem „Gotowe —
cały pendrive jest teraz na dane.” (ADR 0033, `komponenty/skarbiec.md`). Sieć (0.9.0 krok 7, `siec.c`; decyzja D1:
szkic B w `docs/WYGLAD.md`): kafelek na każdą kartę sieciową komputera (nazwa z tabeli `K1_KARTY_SIECIOWE` albo rodzaj
i identyfikator, linijka opisu, plakietka stanu: Połączono / Łączę… / Brak kabla / Brak sterownika / Później — Wi-Fi /
Nieobsługiwana), pod nimi „Szczegóły karty przewodowej” (sterownik z jądra, łącze: prędkość i dupleks, adres karty
(MAC), adres w sieci z maską i routerem — gdy połączono); log „sieć: Intel 82574L: Połączono; szczegóły: e1000e, łącze
(kafelki od x,y co 76)” (bez adresu). Od 0.9.1 (krok 1, D1: szkic A; `wifi.c`) pod tytułem zakładki „Wi-Fi | Karty
sieciowe” (log „sieć: zakładki od x,y”): Wi-Fi — przełącznik radia (`WIFI_WLACZ`) i lista sieci w pobliżu (wachlarz z siłą
sygnału, nazwa, kłódka, uwaga; najwyżej 6 wierszy), przy pokazaniu prośba o szukanie (`WIFI_SZUKAJ`); log „wifi: włączone;
sieci 5 (z hasłem 4, otwarte 1, inne 0); wiersze od x,y co 40; przełącznik x,y; rodzaje 2,1,3,4,3; połączenie s (rząd r)” — bez
nazw sieci. Od 0.9.1 krok 2 (`wifi_haslo.c`, D1, D2, D4): kliknięcie sieci rozwija jej wiersz — pole hasła (kropki, nigdy
hasło), „Połącz” (sieć otwarta: napis „Sieć niezabezpieczona…”; zapamiętana: „Połącz” i „Zapomnij tę sieć”; połączona:
„Zapomnij tę sieć”); przy wierszu „Połączono”, „Łączę…” albo „zapamiętana”; złe hasło: czerwona ramka pola i „Złe hasło —
spróbuj jeszcze raz.” pod nim; Esc zamyka wiersz, wyjście z sekcji czyści wpisane hasło. Hasło idzie raz prosto do usługi
siec (fd 6, `KANAL_DO_SIECI`: `WIFI_HASLO`, `WIFI_WYBRANA`, `WIFI_POLACZ`, `WIFI_ZAPOMNIJ`) — nie przez nadzorcę; stan wraca
w `WIFI_POLACZENIE`. Log „wifi: rząd i otwarty (rodzaj r); pole x,y; Połącz x,y; Zapomnij x,y”. Karty sieciowe — kafelki
jak wyżej, karta Wi-Fi z plakietką Włączone/Wyłączone. Przeglądarka (0.10.0 krok 5, `przegladarka.c`; ADR 0020 p. 2, D2;
sekcja przed Informacjami — testy klikają sekcje po numerach; od 0.10.1 krok 4 wygląd wg D5 właściciela — szkic A „karta
na sesję”): karta na każdą sesję przeglądarki (od nadzorcy: `STREFA_NAZWA`, `STREFY_STAN`; `pole`, zaokrąglenie 10, jak
karty w Sieci) — kropka w kolorze sesji, nazwa, pod nią jedno zdanie stanu („Pamięta zalogowania, zakładki i historię.” /
„Zapomina wszystko po zamknięciu.” / „Otwarta teraz — zmiana zadziała od następnego otwarcia.”), w dolnym rzędzie ciche
„Zmień nazwę” (pole tekstu w miejscu nazwy, Enter albo „Zapisz”, Esc albo „Anuluj”; nazwa sprawdzana jak u nadzorcy —
`k1_strefa_nazwa_dobra`, do 20 bajtów; `STREFA_NAZWA`) i „Wyczyść dane…” (pytanie w okienku przybornika „Wyczyścić dane
sesji <nazwa>?” — „Wyczyść dane” wysyła `STREFA_WYCZYSC`, „Anuluj”, Esc albo kliknięcie obok zamyka; nieczynne, gdy sesja
jest otwarta; wynik `STREFA_WYCZYSZCZONO` jako napis pod listą), po prawej przełącznik „Pamięta | Zapomina”
(`STREFA_TRWALOSC`). Uwagi tylko, gdy dotyczą — jedna spokojna linia nad listą: bez K1-DANE „K1 działa bez dysku danych —
każda sesja zapomni po zamknięciu.” (przełącznik wyciszony na „Zapomina”, bez „Wyczyść dane…”), bez wirtualizacji
„Wirtualizacja wyłączona — sesje nie ruszą. Włącz ją w BIOS-ie.”. Jeden układ (`uklad()`) dla rysowania, kliknięć i logu.
Log „przeglądarka: sesje 3 (dane na dysku 1, wirtualizacja 1): 2 pamięta, karta x,y wxh, przełącznik x,y, Zmień nazwę x,y,
Wyczyść x,y; …” i „przeglądarka: pytanie o sesję N: Wyczyść dane x,y; Anuluj x,y” — numery stref, nigdy nazwy (tekst
użytkownika). Okno Ustawień od 0.10.0 krok 5 ma 680 × 480 (o jedną sekcję więcej). Informacje (`informacje.c`): wersja,
ekran, jądro, gdzie są dane i odesłanie do sekcji Miejsce.

### Instalator
(0.5.9, ADR 0018, `src/aplikacje/instalacja/`, UID 204) — lista dysków od nadzorcy,
instalacja po wpisaniu „USUŃ”, aktualizacja (dane zostają), postęp, „Wyłącz komputer”. Bez dostępu do
dysków — szczegóły: `komponenty/instalator.md`.

### Kalkulator
(0.7.1, `src/aplikacje/kalkulator/`, UID 205) — cztery działania liczone od lewej do
prawej (jak w zwykłym kalkulatorze: 12 + 7 × 3 = 57), procent (200 + 10 % = 220), zmiana znaku, pamięć
(MC, MR, M+, M−), C / CE / cofnij; liczby po polsku (przecinek, odstęp co trzy cyfry, do 15 cyfr, potem
zapis „1,5e20”); dzielenie przez zero — „Nie można dzielić przez zero”. Klawiatura: cyfry, + − * /,
Enter albo =, Backspace, Esc, % i klawiatura numeryczna. Silnik bez rysowania (`liczydlo.c`, test na
hoście `test_kalkulator`), okno w `main.c`. Bez dostępu do plików i sieci, bez kanałów od nadzorcy.

### Zasoby
(M4.1, ADR 0012) — procesor (procent wszystkich rdzeni) i pamięć (użyta z całości)
całego systemu: liczba + wykres z ostatniej minuty (jedna seria, kolor akcentu, linia 2 px);
tabela działających usług i aplikacji: procesor i pamięć każdej (z ich cgroup; od 0.8.2 najwyżej 14 wierszy,
ostatni „… i jeszcze n” — okno mieści się na ekranie 1024 × 768); czas pracy systemu. Dane od
nadzorcy co sekundę (fd 4), sprawdzane: nadawca UID 0, bez deskryptorów, każde pole.
0.6.3: wiersz **Bateria** (`ZASOBY_BATERIA`): procent, pobór mocy w W (`power_now` albo prąd ×
napięcie, uśredniony przez nadzorcę: połowa każdego nowego odczytu — od 0.8.7, wcześniej 30 %; liczba dogania
zmianę w ok. 15 s, resztę opóźnienia daje sama bateria, która też uśrednia; od nowa przy zmianie ładowanie ↔
rozładowanie; odczyt co 5 s, gdy Zasoby działają) i czas do rozładowania / naładowania (energia ÷
pobór; brak szacunku poniżej 0,1 W i powyżej 99 h).

### Pliki
(0.8.0, ADR 0029, `src/aplikacje/pliki/`, UID 206) — pliki i foldery użytkownika, jak w Windows: z lewej
stałe foldery (Pulpit, Dokumenty, Obrazy, Muzyka, Wideo, Pobrane) i Kosz (pełny — z kulką papieru); nad listą
„w górę” i ścieżka („Dokumenty › Projekt”); przyciski miejsca: „Nowy folder” (od razu zmiana nazwy, jak
w Windows), „Zmień nazwę”, „Usuń” (do Kosza); w Koszu: „Przywróć”, „Usuń na zawsze”, „Opróżnij Kosz” (oba
ostatnie z pytaniem w stopce). Przywrócony plik, którego folder jest w Koszu (albo już nie istnieje), trafia do
Dokumentów — stopka to mówi („Folder „Projekt” jest w Koszu — przywrócono do Dokumentów.”, 0.8.1). Gdy zostało mniej
niż 10 % miejsca na pliki, stopka mówi „Mało miejsca na pliki: zostało X z Y” (`SKARB_MIEJSCE` po każdej liście, 0.8.1). Lista: najpierw foldery, potem pliki po nazwie; kolumny nazwa, data zmiany
(w Koszu: usunięcia), rozmiar; podwójne kliknięcie (Enter, przycisk „Otwórz”) otwiera folder, a plik — w domyślnej
aplikacji jego typu (Ustawienia → Aplikacje; tekst — Notatki, zdjęcie — od 0.8.2 **Podgląd**); typ bez aplikacji (PDF,
ZIP, „Plik”) — stopka „K1 nie ma jeszcze aplikacji do tego typu pliku.” (0.8.2 krok 5: nigdy cisza). **Menu pod prawym
przyciskiem** (0.8.2 krok 5, `menu.c`; to samo co na pulpicie — `k1_menu` w przyborniku): plik — „Otwórz”, „Otwórz za
pomocą ›” (obok rozwija się lista aplikacji, które otwierają ten typ — `K1_OTWIERANIE`, ta, która otwiera go teraz,
z ✓; przy co najmniej dwóch także „Zawsze otwieraj tą aplikacją”), „Zmień nazwę”, „Usuń”; folder — „Otwórz”, „Zmień
nazwę”, „Usuń”; w Koszu — „Przywróć”, „Usuń na zawsze”; na pendrivie — „Importuj” (folder: „Otwórz”). Typ bez aplikacji:
„Otwórz” i „Otwórz za pomocą” wyszarzone, pod nimi powód. Wybrana aplikacja dostaje ten jeden plik (`DOK_OTWORZ`, ADR
0030). Do 0.8.1 był przycisk „Otwórz w…”, który podmieniał rząd przycisków. Z prawej **szczegóły** zaznaczonego (do 0.8.2 „podgląd” — decyzja D1): ikona,
nazwa, typ (rozpoznany z zawartości), rozmiar, daty, pochodzenie („utworzony w K1”), w Koszu — za ile dni
zniknie; plik tekstowy — początek treści (do 64 KiB, `DOK_CZYTAJ`); **zdjęcie** (0.8.0 krok 4, ADR 0032; PNG i JPEG,
od 0.8.2 także GIF, BMP, WebP — rodzina „obrazy”) — zmniejszone do 200×150 przez usługę `dekoder`
(`komponenty/dekoder.md`; plik idzie do niego kawałkami prosto od skarbca — **miniatura**; uszkodzone: „Miniatura
niedostępna”, ponad 100 Mpx: „Zdjęcie ma 150 Mpx — / K1 pokazuje do 100 Mpx”, za mało pamięci: „Za mało pamięci na
miniaturę”), a pod nim
przycisk **„Ustaw jako tapetę”** (poza Koszem): zdjęcie do 2560×1600 idzie od dekodera prosto do ekranu, postęp i wynik
w stopce. Klawisze: Enter (otwórz folder),
Backspace (w górę), F2 (zmień nazwę), Delete (do Kosza), strzałki, Esc. Złe nazwy (`\ / : * ? " < > |`,
kropka na końcu) i zajęte — komunikat w stopce. **Pendrive** (0.8.0 krok 3, ADR 0031): gdy włożony jest pendrive
(nie pendrive K1), pod Koszem pojawia się miejsce „Pendrive” — jego foldery i pliki (tylko do odczytu: bez zmiany nazwy
i usuwania) czyta usługa `czytnik` (`komponenty/czytnik.md`); przyciski „Importuj” (zaznaczony plik) i „Importuj
wszystkie” (pliki folderu): zdjęcia (rodzina „obrazy”) do Obrazów, reszta do Dokumentów, pochodzenie „z pendrive'a”; od 0.8.1
(krok 4) pliki do 4 GB, kawałkami po 1 MiB prosto z czytnika do skarbca (Pliki nie czytają treści; przy dużym pliku
stopka: „Importowanie: 1 z 3… 120 z 300 MB”), wyjęty pendrive w trakcie — skarbiec usuwa już zapisane kawałki;
miniatura zdjęcia do 128 MB i 100 Mpx (limity dekodera, od 0.8.2; wcześniej 16 MB i 24 Mpx); postęp i wynik w stopce; wyjęty pendrive — powrót do Dokumentów.
Od 0.8.2 (krok 2): folder z pendrive'a przychodzi w kolejnych listach (po 1024; stopka „Czytanie folderu… n
elementów”), widać i można zaimportować do 16 384 elementów jednego folderu („Importuj wszystkie” — gdy cały folder
jest już wczytany); plik dostaje czas „zmieniono” z pendrive'a (`DOK_CZAS`), a ponowny import pomija plik, który już
jest w Obrazach / Dokumentach z pendrive'a z tą samą nazwą, rozmiarem i czasem — stopka „Pominięto n — już
zaimportowane” (decyzja D3); brak miejsca i limit wpisów przerywają import z powodem: „Brak miejsca na pliki —
zaimportowano n z m.”, „Brak miejsca w pamięci (K1 działa bez nośnika danych) — …”, „Za dużo plików i folderów (K1
mieści 16 384) — …”. Lista sortowana `qsort` (16 384 elementy).
**Zaznaczanie wielu i przeciąganie** (0.8.2 krok 6, L24 faza A, decyzje D6 i D8, szkic z 2026-11-12): Ctrl+klik dokłada
albo odejmuje element, Shift+klik zaznacza od ostatnio klikniętego do tego (Ctrl+Shift — dokłada zakres), Ctrl+A —
wszystko w miejscu (także na pendrivie); zaznaczone wiersze mają kreskę w kolorze akcentu, szczegóły i stopka mówią
„Zaznaczone: n” (`k1_zaznaczenie` w przyborniku). Przyciski „Usuń”, „Przywróć”, „Usuń na zawsze”, „Importuj” i menu
pod prawym przyciskiem na jednym z zaznaczonych („Zaznaczone: n” — `k1_menu_wielu`) działają na wszystkie; „Otwórz”
i „Zmień nazwę” — na jeden. Zaznaczone (albo wiersz, który się chwyciło) **przeciąga się** lewym przyciskiem na folder
na liście albo na miejsce z lewej (stały folder, Kosz, Pendrive): przy kursorze półprzezroczysta karteczka z nazwą
i liczbą, pod nią znak, co się stanie („Przenieś do „Obrazy””, „Do Kosza”, „Przywróć do…”, zielony „Kopiuj do… (import)”,
ciemnoczerwony „Nie można — …”), cel obrysowany akcentem. Decyduje **jedna tabela reguł** (źródło, cel) w przyborniku
(`k1_przeciaganie`, ta sama na pulpicie): z folderu — przeniesienie albo Kosz, z Kosza — przywrócenie do folderu,
z pendrive'a — import (kopia) do folderu, na Kosz — „Z pendrive'a możesz tylko skopiować pliki do K1 (import).”,
na pendrive — „Nie możesz — pendrive jest w K1 tylko do odczytu.”, stały folder z lewej — „Nie możesz — foldery
Pulpit, … są stałe.”, folder w samym sobie, ponad 8 poziomów, brak miejsca na import — odmowa z powodem. Przed Koszem
pytanie (D8), gdy elementów jest więcej niż jeden albo folder ma coś w środku: **okienko** w wyglądzie menu przy
miejscu upuszczenia („Przenieść 12 elementów do Kosza?” — „Przenieś do Kosza” / „Anuluj”); zajęta nazwa w folderze
docelowym: „W „Obrazy” jest już „IMG_0001.JPG”.” — „Zachowaj oba” (przenoszony dostaje „ (2)”, `DOK_PRZENIES` b = 1),
„Pomiń”, „Anuluj”; odmowa — okienko z „OK”. Delete i „Usuń” pytają tak samo. Prośby idą do skarbca po jednej
(`zbiorczo.c`), a stopka mówi na końcu, jak poszło („Przeniesiono 12 elementów do „Obrazy”.”, a gdy coś się nie udało —
powód słowami tabeli, `k1_prz_powod`). Przeciąganie między oknami (Pliki ↔ pulpit) — po 0.8.2 (D6). Moduły: `main.c`
(okno, miejsce, lista, zaznaczenie), `wskaznik.c` (mysz i klawisze; 0.8.2 krok 6 — wydzielone z `main.c`),
`przeciaganie.c` (przeciąganie i jego okienka), `zbiorczo.c` (jedno działanie na wielu), `przyciski.c` (przyciski
miejsca), `menu.c` (menu pod prawym przyciskiem i okienko), `rysuj.c` (lista, szczegóły, stopka), `dane.c` (skarbiec
i nadzorca), `pendrive.c` (pendrive i import — od 0.8.2 krok 6 także do folderu, na który upuszczono), `obrazy.c`
(miniatura i tapeta przez dekoder — od 0.8.2 przez wspólny `k1_dekodowanie` z przybornika; kanał do dekodera = 5. kanał
aplikacji, `K1_APL_KANALY`).

### Podgląd
(0.8.2 krok 4, ADR 0037, `src/aplikacje/podglad/`, UID 208) — **zdjęcie w osobnym oknie**, jak w Zdjęciach Windows.
Podwójne kliknięcie zdjęcia (rodzina „obrazy”: PNG, JPEG, GIF, BMP, WebP) w Plikach albo na pulpicie otwiera je tu —
Podgląd jest domyślną aplikacją zdjęć (`K1_OTWIERANIE`; inną można wybrać w Ustawienia → Aplikacje). Wygląd: szkic C
(decyzja właściciela D5, `docs/WYGLAD.md`) — okno 960 × 640, zdjęcie na całym ciemnym obszarze, dopasowane do okna
(nigdy powiększone ponad 100 %); w lewym górnym rogu etykieta: nazwa, wymiary zdjęcia, rozmiar, data zmiany
i pochodzenie („Zachód.jpg · 4000 × 2667 · 2,4 MB · 10.11.2026 18:42 · z pendrive'a”); na dole pływający pasek:
**‹** „3 z 27” **›** (kolejność jak w Plikach — po nazwie; po ostatnim pierwsze), **−** **+** (przybliżenie: 1,5×, 2×,
3×, 4×… dopasowanego zdjęcia, z krótką animacją, najwyżej do 4 pikseli ekranu na piksel obrazu; przybliżone zdjęcie
przesuwa się przeciągnięciem myszą), **„Otwórz…”**, **„Obróć”**, **„Ustaw jako tapetę”**, **„Do Kosza”**.
Klawisze: ← / → (poprzednie / następne), Home / End, **+** / **−** (przybliż / oddal), **0** (dopasuj), **R** (obrót widoku o 90° w prawo — pliku nie zmienia), **Delete**
(do Kosza bez pytania — decyzja D2; potem następne zdjęcie), **Ctrl+O** („Otwórz…”: okno wyboru pliku z samymi
zdjęciami). Uruchomiony z menu Start pokazuje „Otwórz zdjęcie z Plików albo przyciskiem „Otwórz…” (Ctrl+O).”
Zdjęcia nie da się odczytać: „Tego zdjęcia nie da się odczytać.”, ponad 100 Mpx: „Zdjęcie ma 150 Mpx — K1 pokazuje do
100 Mpx.”, plik ponad 128 MB: „Plik jest za duży — K1 pokazuje zdjęcia do 128 MB.” Podgląd **widzi tylko zdjęcia
folderów, z których użytkownik w nim coś otworzył** (prawa w skarbcu: `Z_OBRAZY_FOLDERU`), tylko do odczytu i do
Kosza; nie dekoduje zdjęć sam — robi to `dekoder` w osobnej klatce (`k1_dekodowanie`, jak w Plikach; obraz najwyżej
1600 × 1600, obrót, dopasowanie i przybliżenie u siebie: `k1_skaluj`). Moduły: `main.c` (okno, pływający pasek, etykieta, klawisze, mysz), `dane.c`
(skarbiec, plik do otwarcia, Kosz, okno wyboru), `obraz.c` (dekoder, tapeta), `widok.c` (obrót, dopasowanie, przybliżenie z animacją, przeciąganie), `kolejnosc.c` (kolejność zdjęć —
czyste funkcje, test na hoście), `skala.c` (0.8.5: szybka droga — kopie zdjęcia 800 i 400 px, próbkowanie
dwuliniowe w liczbach stałoprzecinkowych; czyste funkcje, test na hoście).

**Płynność (0.8.5 krok 2).** Przybliżanie i przesuwanie tylko zmieniają stan widoku; piksele liczą się raz na klatkę,
przy rysowaniu okna. W ruchu (animacja, przeciąganie) widok powstaje z najmniejszej kopii zdjęcia, która ma jeszcze co
najmniej piksel na piksel ekranu (zdjęcie, połowa, ćwiartka — liczone raz po dekodowaniu i po obrocie), szybkim
próbkowaniem; 0,12 s po ostatnim ruchu — raz dokładnym filtrem `k1_skaluj` (ostro jak w 0.8.2). Animacja idzie za
zegarem, nie za klatkami: przy wolnej klatce przeskakuje. Kolejne „+” w trakcie animacji idą od poziomu docelowego.
Nieprzezroczyste zdjęcie kopiuje się wierszami (bez mieszania), ciemne tło rysuje się tylko wokół niego. Pomiar na
hoście (`make build/obj/pomiar_podgladu`, zdjęcie 1600 × 1600, -O2): klatka dokładnie 14–26 ms, szybko 1,6–2,6 ms
(ok. 12 %); tło okna z zaokrągleniem 4,8 → 0,6 ms (`k1_zaokraglony` nie liczy odległości we wnętrzu — obraz ten sam co
do bitu, `test_przybornik`; zyskuje każde okno). Log po każdym ruchu (najwyżej raz na ruch): „ruch: klatek n w t ms,
najdłuższa x ms, ostra y ms”.

## Granica zaufania
Klienci ekranu w roli **aplikacja**: tylko własne okna. Ustawienia jako jedyna mogą zmieniać
ustawienia (ekran sprawdza rolę i UID) — poza domyślną aplikacją typu pliku, którą od 0.8.2 zmieniają też Pliki
(„Zawsze otwieraj tą aplikacją”). Treść notatki nigdy nie trafia do logu (tylko długość).

## Uprawnienia
| Aplikacja | UID | seccomp (poza bazą) | Landlock | cgroup |
|---|---|---|---|---|
| Zegar | 200 | `memfd_create`, `ftruncate` | brak ścieżek | 32 MiB, 1 proces |
| Notatki | 201 | `memfd_create`, `ftruncate` (od 0.5.6 bez plików) | brak ścieżek | 32 MiB, 1 proces |
| Ustawienia | 202 | `memfd_create`, `ftruncate` | brak ścieżek | 32 MiB, 1 proces |
| Zasoby | 203 | `memfd_create`, `ftruncate` | brak ścieżek | 32 MiB, 1 proces |
| Instalator | 204 | `memfd_create`, `ftruncate` | brak ścieżek | 32 MiB, 1 proces |
| Kalkulator | 205 | `memfd_create`, `ftruncate` | brak ścieżek | 32 MiB, 1 proces |
| Pliki | 206 | `memfd_create`, `ftruncate` | brak ścieżek | 32 MiB, 1 proces |
| Wybór pliku | 207 | `memfd_create`, `ftruncate` | brak ścieżek | 32 MiB, 1 proces |
| Podgląd | 208 | `memfd_create`, `ftruncate` | brak ścieżek | 96 MiB (obraz do przybliżania: 1600 × 1600 i jego obrót), 1 proces |

## Wejścia i wyjścia
fd 3: kanał do ekranu. Okno: memfd (ADR 0008). Notatki: fd 4 — kanał do skarbca: od 0.8.0 `DOK_*` — tylko
własne pliki i pliki dane przez użytkownika (tabela praw w skarbcu), `DOK_WYBIERZ` → `DOK_WYBRANY`; stare obiekty
1–33 tylko do przeniesienia (`SKARB_CZYTAJ`, `SKARB_USUN`); fd 5 — od nadzorcy, jednokierunkowo: `PLIK_OTWORZ`
(obiekt = plik otwarty w Notatkach z Plików albo z pulpitu). Wybór pliku: fd 4 — kanał do skarbca (lista nazw,
`DOK_PROSBA`, `DOK_WYBRANO`). Zasoby: fd 4 —
kanał od nadzorcy (`ZASOBY_SYSTEM`, `ZASOBY_USLUGA`, od 0.6.3 `ZASOBY_BATERIA`), aplikacja nie może do niego pisać.
Ustawienia: lista trybów ekranu (`K1_KOM_TRYB`) dostaje tylko ona; od 0.5.5 fd 4 — kanał od nadzorcy
(`BATERIA`, `BATERIA_HIST`: do 16 próbek po 1 bajcie w komunikacie), jednokierunkowy, nadawca UID 0; od 0.9.0 ten sam
kanał: `SIEC_STAN`, `SIEC_KARTA`, `SIEC_SZCZEGOLY`, `SIEC_STEROWNIK`, `SIEC_ADRESY` (sekcja Sieć; `komponenty/siec.md`); od
0.9.1 `WIFI_STAN`, `WIFI_SIEC`, `WIFI_NAZWA` — i ten kanał działa w obie strony: Ustawienia wysyłają `WIFI_WLACZ`
(przełącznik radia) i `WIFI_SZUKAJ` (gdy pokazują listę sieci); nadzorca sprawdza UID nadawcy. Od 0.9.1 krok 2: `WIFI_POLACZENIE`
(stan połączenia, zapamiętane sieci) i fd 6 — kanał prosto do usługi siec (`KANAL_DO_SIECI`, jednokierunkowy; siec sprawdza
UID Ustawień): hasło Wi-Fi i wybór sieci.
Bez kanału (zamknięty) reszta Ustawień działa.
Pliki (0.8.0): fd 4 — kanał do skarbca z prawami do wszystkich plików użytkownika (i do niczego więcej: obiekty
innych aplikacji są dla Plików niewidoczne), komunikaty `DOK_*` (`komponenty/skarbiec.md`); fd 5 — od nadzorcy,
jednokierunkowo: `PLIKI_POKAZ` (a = plik albo folder do pokazania, b = `K1_PLIKI_*`: 1 Kosz, 2 — od 0.8.2 — pole nowej
nazwy pliku a, „Zmień nazwę” w menu pulpitu), gdy pulpit otwiera Pliki przy pliku albo Koszu (`URUCHOM` z miejscem —
tylko dla Plików). Lista ze skarbca sprawdzana wpis po wpisie
(`k1_dok_lista_czytaj`); nazwy i treść nigdy nie trafiają do logu (tylko liczby). Od 0.8.0 krok 3: fd 6 — od
nadzorcy `PENDRIVE` (jest / nie ma), `PENDRIVE_KANAL` (kanał do czytnika); do nadzorcy `PENDRIVE_OTWORZ`. Kanał do
czytnika: `PEN_*` (`komponenty/czytnik.md`); plik z pendrive'a idzie do skarbca w memfd od czytnika, bez czytania
(`DOK_NOWY` z pochodzeniem — prawo tylko Plików).
Podgląd (0.8.2): fd 4 — kanał do skarbca (`DOK_SPIS`, `DOK_CZYTAJ` kawałkami dla dekodera, `DOK_KOSZ`, `DOK_WYBIERZ` z
rodziną „obrazy”; tylko zdjęcia folderów otwartych w nim); fd 5 — od nadzorcy, jednokierunkowo: `PLIK_OTWORZ`; fd 6 —
do nadzorcy `DEKODER`, od nadzorcy `DEKODER_KANAL` (kanał do nowego dekodera: `DEK_*`, `komponenty/dekoder.md`).
Zdjęcie na tapetę: `TAPETA_ZDJECIE` do ekranu (memfd od dekodera, bez czytania), jak z Plików.

## Obsługa błędów
Zamknięcie okna (×) albo kanału → koniec procesu; aplikacje nie są restartowane (test).
Błąd zapisu notatki → komunikat w oknie i w logu.

## Testy
| Test | Rodzaj |
|---|---|
| `test-m4.sh`: Notatki — „Zażółć K1” (AltGr), zapis 9 znaków, tekst na zrzucie, przeciąganie okna | funkcjonalny |
| `test-m4.sh` (0.5.8): druga notatka, po ponownym starcie obie (ostatnio zmieniona na górze), otwarcie drugiej, usunięcie z potwierdzeniem | funkcjonalny |
| `test-m4.sh`: Zegar — tarcza na zrzucie; Ustawienia — ciemny motyw i tapeta na zrzucie | funkcjonalny |
| `test-m5.sh`: Ustawienia → Bateria — historia od nadzorcy (≥ 2 próbki), linia wykresu na całą szerokość na zrzucie `m5-bateria.png` | funkcjonalny |
| `test-m4.sh`: Ustawienia → Ekran — zmiana rozdzielczości (zrzut w nowym rozmiarze, pulpit ułożony od nowa); Zasoby — dane od nadzorcy, wykres pamięci na zrzucie | funkcjonalny |
| w systemie: klatki aplikacji (od 0.8.0 także Pliki: UID, brak plików i urządzeń) | funkcjonalny |
| w systemie: próby w klatkach (także Notatki: `open` zablokowane, dysk niedostępny) | negatywny |
| `test-m4.sh`: notatka wraca po wyłączeniu i ponownym starcie z tym samym dyskiem danych | funkcjonalny |
| w systemie: zabita aplikacja nie wraca sama | negatywny |
| `test-m4.sh` (0.8.0): Pliki — nowy folder, zmiana nazwy na klawiaturze, zajęta nazwa, wejście do folderu i „w górę”, Kosz (usuń, przywróć, usuń na zawsze z pytaniem, opróżnij), pliki po ponownym starcie; zrzut `m4-pliki.png` | funkcjonalny |
| host: `test_dokumenty` (skarbiec), `test_komunikat` (`DOK_*`, `PLIKI_POKAZ`), `fuzz_dokument` (lista) | jednostkowy / negatywny / fuzzing |
| host (0.8.0, ADR 0030): `test_dokumenty` — prawa Notatek (tylko własne pliki, nowy plik tylko w swoim folderze, Kosz tylko dla własnych), „Otwórz w…” (pulpit: tylko folder Pulpit; zły typ — odmowa), okno wyboru (jedna prośba naraz, tylko nazwy, folder i zły typ odrzucone, zamknięte okno = anulowanie, dostęp znika z aplikacją); `test_komunikat` — `DOK_OTWORZ`, `DOK_WYBIERZ`, `DOK_WYBRANO`, `DOK_WYBRANY`, `DOK_PROSBA`, `SKARB_URUCHOM`, `PLIK_OTWORZ`, `POKAZ_APL`, domyślne aplikacje | jednostkowy / negatywny |
| `test-pliki.sh` (0.8.0, krok 2): notatki jako pliki (od 0.9.0 dysk z `notatki_w_plikach`; wcześniej przeniesienie starych notatek), nazwa za pierwszym wierszem, okno wyboru pliku (wybór i „Anuluj”), Ustawienia → Aplikacje; od 0.8.2 krok 5: menu pod prawym przyciskiem w Plikach → „Otwórz za pomocą” → Notatki, „Zmień nazwę” z menu pulpitu (Pliki z polem nazwy) | funkcjonalny |
| host (0.8.2 krok 6): `test_przeciaganie` — każda para źródło/cel tabeli reguł, warunki (ten sam folder, w samym sobie, za głęboko, brak miejsca), pytanie przed Koszem (D8), okienka (pytanie, nazwa zajęta, odmowa), powody wyników skarbca, zaznaczanie (klik, Ctrl, Shift, Ctrl+A, 16 384 elementy), rysowanie na brzegach obrazu; `test_menu` — menu wielu zaznaczonych | jednostkowy |
| `test-pliki.sh` (0.8.2 krok 6): folder przeciągnięty na folder, Ctrl+klik, dwa na Kosz z pytaniem i „Przenieś do Kosza”, z Kosza na folder z lewej, stały folder na Kosz (odmowa); na pulpicie folder na folder i na Kosz z pytaniem; `test-pendrive.sh`: plik z pendrive'a na Kosz (odmowa), na „Obrazy” (import tam); zrzuty `pliki-7…12-*.png`, `pendrive-5a-tylko-import.png` | funkcjonalny |
| host (0.8.2 krok 5): `test_menu` — wiersze menu pliku dla każdego typu i miejsca (Pliki, pulpit, Kosz, pendrive), „Otwórz za pomocą” (bez Plików, ✓ przy tej, która otwiera, „Zawsze otwieraj” od dwóch aplikacji), komunikat bez aplikacji, położenie i trafienie, rysowanie na brzegach obrazu | jednostkowy |
| `test-pendrive.sh` (0.8.2 krok 5): podwójne kliknięcie pliku bez aplikacji (film) → stopka; menu pliku bez „Otwórz” | funkcjonalny / negatywny |
| host: `test_przybornik` — czas polski (zmiany czasu), układ PL, UTF-8, rysowanie na krawędziach | jednostkowy |
| host (0.8.2 krok 4, ADR 0037): `test_podglad` — kolejność zdjęć, ← / →, zdjęcie po Koszu, „3 z 27”; `test_dokumenty` — prawa Podglądu (bez gestu nic, tylko zdjęcia folderu, tylko odczyt i Kosz, okno wyboru z rodziną „obrazy”, po zamknięciu nic) | jednostkowy / negatywny |
| host (0.8.5 krok 2): `test_podglad` — wybór kopii, połowa zdjęcia (średnia 2 × 2), próbkowanie 1:1 i 2×, wycinek za brzegiem (ASan), przezroczystość; `test_przybornik` — zaokrąglone prostokąty i obrysy bit w bit jak w 0.8.3 (400 losowych); `pomiar_podgladu` — czasy klatek (nie w testach) | jednostkowy / pomiar |
| `test-pendrive.sh` (0.8.5 krok 2): 20 × „+” i 20 × „−” bez czekania, przeciąganie 20 ruchami — 400 % i z powrotem 100 %, log „ruch: klatek…”, zrzut; okno odpowiada dalej (←, ‹, Delete…) | funkcjonalny |
| `test-pendrive.sh` (0.8.2 krok 4): Podgląd — dwuklik zdjęcia w Plikach, „zdjęcie 4 z 10” w klatce UID 208, kolor na zrzucie, R (widok 60×80), ← uszkodzone (komunikat), ‹ PNG, Delete → Kosz i następne zdjęcie, Ctrl+O → okno wyboru (obrazy), „Ustaw jako tapetę”, × ; + i − (150 % → 100 %, kolor na zrzucie) | funkcjonalny / negatywny |
