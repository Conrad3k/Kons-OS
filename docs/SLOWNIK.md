# Słownik K1

Każde pojęcie ma **jedno** znaczenie — tak samo w kodzie, dokumentach, komunikatach w systemie i rozmowie.
Nowe pojęcie = nowy wiersz tutaj (zasada czytelności, `AGENTS.md`). Kod używa polskich nazw z tej listy
(np. `klatka`, `kanal`, `skarb`); w nawiasie — angielski odpowiednik, gdy ktoś szuka w literaturze.

## Start i system
| Pojęcie | Znaczenie |
|---|---|
| **jądro** | Jądro Linux z naszą minimalną konfiguracją (`jadro/k1.config`). Jedyna rzecz, którą bierzemy z Linuksa. |
| **karta sieciowa** | Urządzenie, przez które K1 łączy się z siecią — kablem (Ethernet; od 0.9.0) albo bez kabla (Wi-Fi; od 0.9.1). Sterownik zawsze z jądra (ADR 0039, 0041). Jądro nazywa ją „interfejsem sieciowym” (`eth0`, `wlan0`). |
| **Wi-Fi** (0.9.1) | Łączność bez kabla przez radio: karta Wi-Fi rozmawia z **punktem dostępu** (router domowy, sieć w kawiarni). W K1 jedna karta Wi-Fi naraz; kabel ma pierwszeństwo (ADR 0041). |
| **sieć Wi-Fi** | To, co widać na liście w Ustawienia → Sieć → Wi-Fi: nazwa (z powietrza, pokazana po oczyszczeniu — „?” zamiast znaków sterujących i złych bajtów), siła sygnału i zabezpieczenie (z hasłem — kłódka; niezabezpieczona; logowanie loginem; stare zabezpieczenie). Jedna pozycja na nazwę, najsilniejsze pierwsze, najwyżej 16. |
| **punkt dostępu** | Urządzenie, które ogłasza sieć Wi-Fi (router, hotspot). W testach: **punkt dostępu testowy** — udawana karta Wi-Fi (`mac80211_hwsim`), na której program testowy ogłasza sieć (`testy/system/t_wifi.c`). |
| **siła sygnału** (kreski) | 1–4 kreski z mocy sygnału w dBm (progi −55, −67, −75 — jedna tabela w `k1_wifi.c`); ikona „Wachlarz”: wypełniona część wachlarza. |
| **przełącznik radia** | Włącza i wyłącza Wi-Fi: w Ustawieniach (programowo) albo w obudowie komputera (np. X230 — wtedy K1 tylko to pokazuje). Jądro nazywa to rfkill. |
| **hasło sieci** (0.9.1 krok 2) | Hasło sieci Wi-Fi z hasłem (WPA2): 8–63 znaki (albo 64 cyfry szesnastkowe). Wpisane raz w Ustawieniach idzie prosto do usługi siec, która liczy z niego **klucz sieci** (PBKDF2) i hasło zapomina; hasło nigdy nie jest pokazywane z powrotem ani zapisywane. |
| **klucz sieci** | To, co K1 pamięta zamiast hasła sieci Wi-Fi: 32 bajty liczone z hasła i nazwy sieci (PBKDF2-SHA1, 4096 powtórzeń); z nim K1 loguje się do sieci (wymiana czterech wiadomości) bez znajomości hasła. |
| **zapamiętana sieć** | Sieć Wi-Fi, która raz przyjęła hasło (otwarta — połączenie): K1 pamięta jej nazwę i klucz sieci w skarbcu (dane usługi siec) i łączy się z nią sam — przy starcie i po utracie zasięgu, z najsilniejszą w zasięgu. „Zapomnij tę sieć” w Ustawieniach ją usuwa (D4). Najwyżej 16. |
| **logowanie do sieci** (WPA2) | Wymiana czterech wiadomości z punktem dostępu (ramki EAPOL): obie strony dowodzą, że znają klucz sieci, i ustalają klucze tego połączenia; robi to usługa siec w klatce, klucze do jądra wpisuje nadzorca. Złe hasło: punkt dostępu nie odpowiada na drugą wiadomość. |
| **sieć niezabezpieczona** (otwarta) | Sieć Wi-Fi bez hasła; K1 łączy się z nią (D2), ale w Ustawieniach pisze, że inni w pobliżu mogą widzieć, co przesyłasz. Sieci, które każą się zalogować na stronie, zadziałają z przeglądarką (0.10). |
| **plik od producenta** (karty) | Zamknięty program od producenta karty, który jądro wgrywa do karty Wi-Fi (firmware). W K1 wbudowany w podpisane jądro — tabela `jadro/oprogramowanie-kart.txt` (decyzja D3, ADR 0041). |
| **baza przepisów radiowych** | Plik `regulatory.db` z wireless-regdb: jakie kanały i moce wolno używać w danym kraju; wbudowany w jądro i sprawdzany podpisem. |
| **router** | Urządzenie w domu, do którego idzie kabel sieciowy; daje K1 adres (DHCP) i drogę do internetu. |
| **adres od routera** (DHCP) | Adres K1 w sieci domowej: usługa **siec** prosi o niego router, nadzorca go ustawia; ważny przez **dzierżawę** (czas od routera), odnawiany w połowie. Nie trafia do logu. |
| **ikona sieci** (0.9.0) | Wtyczka sieciowa na górnym pasku obok baterii: jasna z kablem — połączono; przygaszona z kropką w kolorze terakoty — łączę (czekam na adres od routera); przygaszona z krzyżykiem — kabel odłączony; przekreślona — brak obsługiwanej karty. Bez kabla, z kartą Wi-Fi (0.9.1): **wachlarz** — przygaszony z krzyżykiem — nie połączono; przekreślony — Wi-Fi wyłączone; z kropką w kolorze terakoty — łączę; wypełniony według siły sygnału — połączono (0.9.1 krok 2). |
| **identyfikator karty** | Dwie liczby szesnastkowe producent:urządzenie (np. `10ec:8136`), z PCI albo USB. Ustawienia → Sieć pokazują go przy karcie, której K1 nie zna — dopisanie sterownika to jedna linia w `jadro/k1.config`. |
| **sieć testowa** | W testach QEMU: drugi koniec kabla karty sieciowej maszyny, program w Pythonie (`testy/qemu/k1siec.py`) — wysyła ramki jak router albo obcy komputer i zapisuje każdą ramkę od K1. |
| **rdzeń** | Cały system K1 poza jądrem: obraz tylko do odczytu `/rdzen` (SquashFS). Nie da się go zmienić w działającym systemie. |
| **sprawdzanie rdzenia** (dm-verity) | Przy każdym odczycie jądro sprawdza, że rdzeń jest bajt w bajt taki, jak w wydaniu; zmiana = zatrzymanie. |
| **wydanie** | Jedna wersja K1 (`k1-vX.Y.Z`): podpisany plik `k1.efi` z jądrem i rdzeniem, ISO i obraz na pendrive. |
| **podpis** | Dowód, że wydanie pochodzi od właściciela projektu (klucz w sekrecie CI). Instalator odmawia niepodpisanych i starszych. |
| **miejsce A / B** | Dwa miejsca na system w partycji K1-EFI: aktualizacja trafia do wolnego, stare zostaje na wypadek błędu. |
| **K1-EFI** | Partycja startowa: pliki startowe i miejsca A/B. |
| **pliki startowe** | Wszystko w K1-EFI poza miejscami A/B i grubenv: shim, GRUB, MokManager, `grub.cfg`, ekran startowy, certyfikat K1, `.disk/info`. Od 0.9.0 aktualizacja wymienia je, gdy się zmieniły (ADR 0038). |
| **manifest** | Podpisany opis części wydania (512 B): wersja, identyfikator wydania, skrót. **Manifest miejsca** — system w miejscu A/B; **manifest plików startowych** (0.9.0) — skrót drzewa plików startowych, niezależny od tego, gdzie leżą. |
| **wymiana plików startowych** (0.9.0) | Nowe pliki startowe zapisane obok starych, sprawdzone z manifestem, przełączone jednym zapisem sektora startowego FAT; przerwanie zostawia stare albo nowe (`src/instalator/start.c`, ADR 0038). |
| **K1-DANE** | Partycja z zaszyfrowanymi danymi użytkownika; czyta ją tylko skarbiec. |
| **init** | Pierwszy program po starcie (PID 1): montuje, ustawia zabezpieczenia, uruchamia nadzorcę. Nigdy się nie kończy. |

## Usługi i izolacja
| Pojęcie | Znaczenie |
|---|---|
| **nadzorca** | Jedyny program z pełnymi uprawnieniami po starcie: uruchamia usługi i aplikacje w klatkach, otwiera urządzenia, tworzy kanały, restartuje po awarii. |
| **usługa** | Program systemowy K1 bez okna albo z rolą systemową (wejście, ekran, pulpit, skarbiec, instalator…). Lista: `K1_SYSTEM`. |
| **aplikacja** | Program K1 z oknem (Zegar, Notatki, Ustawienia…). Lista: `K1_APLIKACJE` — jedna aplikacja = jeden wiersz. |
| **klatka** (sandbox) | Granice każdej usługi i aplikacji: własny użytkownik, zero uprawnień, lista dozwolonych wywołań jądra, brak dostępu do plików, limity pamięci. |
| **kanał** | Jedyna droga rozmowy między dwoma programami (gniazdo UNIX). Tworzy go nadzorca; odbiorca zawsze wie, kto wysłał. |
| **zaległe** (komunikaty) | To, czego pełny kanał klienta jeszcze nie przyjął (klient akurat nie odbiera). Czeka u nadawcy — w ekranie (0.8.5) i w skarbcu (0.9.0) — i idzie w kolejności, gdy kanał znów przyjmuje. |
| **komunikat** | Wiadomość w kanale: zawsze 32 bajty, sprawdzana pole po polu przez odbiorcę. |
| **wejście** | Usługa, która jako jedyna czyta klawiaturę, mysz i touchpad. |
| **ekran** | Usługa, która jako jedyna rysuje na karcie graficznej: składa okna, rysuje kursor, (od 0.10) ramki stref. |
| **pulpit** (`gui`) | Usługa rysująca paski, menu Start i (od 0.7.5) ikony na pulpicie. Nie widzi treści okien. |
| **przybornik** | Wspólna biblioteka do rysowania: tekst, przyciski, okna, ikony, kolory K1. |
| **ikona na pulpicie** | Kafelek aplikacji (od 0.8.0 też pliku) w siatce na pulpicie: zaznacz, przeciągnij, otwórz podwójnym kliknięciem. Gdy plików Pulpitu jest więcej niż miejsc, ostatnia wolna komórka to „Jeszcze n” (0.8.2) — otwiera Pliki w folderze Pulpit. |
| **przypięta aplikacja** | Aplikacja stale widoczna na dolnym pasku (kolejność ustala właściciel); nieprzypięta pokazuje się na pasku tylko, gdy działa. |
| **menu kontekstowe** | Menu pod prawym przyciskiem myszy (pulpit, ikona, dolny pasek, menu Start; od 0.8.2 plik i folder w Plikach — to samo menu co na pulpicie, `k1_menu` w przyborniku). |
| **zaznaczenie** | To, co jest wybrane w Plikach (0.8.2 krok 6): jeden element (kliknięcie) albo wiele (Ctrl+klik, Shift+klik, Ctrl+A). Przyciski, menu i przeciąganie działają na całe zaznaczenie. `k1_zaznaczenie` w przyborniku. |
| **przeciągnąć** | Chwycić lewym przyciskiem zaznaczone pliki (albo ikonę) i przesunąć je myszą (0.8.2 krok 6); przy kursorze karteczka z nazwą i liczbą oraz **znak** — co się stanie po upuszczeniu („Przenieś do…”, „Do Kosza”, „Kopiuj do… (import)”, „Nie można…”). |
| **przewijanie** | Przesuwanie treści okna kółkiem myszy albo dwoma palcami na touchpadzie (0.10.0 krok 4) — zawsze okna **pod kursorem**, także bez fokusu. Jednostka: 1/120 **ząbka** kółka (`K1_PRZEWIN_ZABEK`); ząbek = 3 wiersze listy (`k1_przewin_wiersze`). Kółko — kierunek klasyczny, dwa palce — **naturalny** (treść jedzie za palcami, decyzja D8). Komunikat `PRZEWIN`, zdarzenie `K1_ZD_PRZEWIJANIE`. |
| **touchpad** | Płytka dotykowa laptopa. K1 czyta palce sam (`src/wejscie/touchpad.c`): jeden palec — kursor, dwa — przewijanie, **stuknięcie** — kliknięcie (jeden palec lewy, dwa prawy, trzy środkowy); touchpad, który cały jest przyciskiem: prawy dolny róg albo dwa palce = prawy przycisk. |
| **upuścić** | Puścić przeciągane pliki nad celem (folder, Kosz, pendrive). Co się stanie, mówi jedna **tabela reguł przeciągania** (źródło, cel — `k1_przeciaganie` w przyborniku, ta sama w Plikach i na pulpicie). Na razie w obrębie jednego okna albo pulpitu; między oknami — po 0.8.2 (D6). |
| **okienko** | Mały panel w wyglądzie menu kontekstowego z pytaniem („Przenieść 12 elementów do Kosza?”) albo odmową („Nie możesz — …”) i wierszami odpowiedzi; pokazuje się w miejscu upuszczenia (0.8.2 krok 6). |
| **rozmieszczenie pulpitu** | Które aplikacje są przypięte i w jakiej kolejności oraz gdzie leżą ikony; zapisane w skarbcu jako obiekt pulpitu. (Nie mylić z blokiem układu K1-EFI.) |
| **warstwa pulpitu** | Powierzchnia pulpitu z ikonami: nad tapetą, pod oknami (ADR 0028). |

## Dane i pliki
| Pojęcie | Znaczenie |
|---|---|
| **skarbiec** (Blob Vault) | Usługa, która jako jedyna czyta i zapisuje K1-DANE; szyfruje wszystko. |
| **obiekt** | Jedna rzecz zapisana w skarbcu (ustawienia, notatka, od 0.8.0 plik). Nigdy nie jest programem do uruchomienia. |
| **właściciel obiektu** | Kto może dany obiekt czytać; aplikacja widzi tylko swoje obiekty (i to, co użytkownik jej wybierze). |
| **Pliki** | Aplikacja z dokumentami użytkownika (0.8.0) i **jedyne przejście** plików między K1 a strefami (ADR 0027). |
| **oryginał / kopia** | Oryginał jest zawsze w Plikach; aplikacja albo strefa dostaje kopię jednego pliku. |
| **pochodzenie** | Zapis przy pliku: skąd przyszedł (pendrive, strona, strefa) i kiedy. |
| **plik** (w kodzie: dokument, `dok`) | Jeden dokument użytkownika w skarbcu: treść i wpis (nazwa, folder, typ, rozmiar, czasy, pochodzenie). Widać go w Plikach (od 0.8.0). |
| **folder** | Miejsce na pliki i inne foldery, jak w Windows (ADR 0029). To tylko wpis w skarbcu, nie katalog w jądrze. |
| **stały folder** | Pulpit, Dokumenty, Obrazy, Muzyka, Wideo, Pobrane — są zawsze, nie da się ich usunąć ani zmienić im nazwy. |
| **wpis** | Metadane pliku albo folderu w skarbcu (bez treści); lista wpisów to to, co pokazują Pliki. |
| **typ pliku** | Rodzaj treści rozpoznany przez skarbiec z pierwszych bajtów (tekst, PNG, JPEG, PDF…), nigdy z nazwy. Lista: `K1_TYPY`. |
| **rodzina typów** | Grupa typów plików, które K1 traktuje tak samo; dziś jedna: **obrazy** (PNG, JPEG, GIF, BMP, WebP — czyta je dekoder: miniatura, Podgląd, tapeta). Kolumna w `K1_TYPY`, nazwy w `K1_RODZINY` (0.8.2); okno wyboru pliku może pokazać jedną rodzinę. |
| **Kosz** | Usunięte pliki; można je przywrócić; usuwane na zawsze po 30 dniach albo ręcznie („Opróżnij Kosz”). To stan pliku, nie folder; ikona na pulpicie. |
| **kompaktowanie** | Skarbiec przepisuje żywe dane i zapomina stare wersje: dysk dostaje miejsce z powrotem. Format 1: wszystko naraz do drugiej połowy K1-DANE (ADR 0015, 0034); format 2: jeden odcinek, krokami po 4 MiB w przerwach między prośbami (ADR 0036). |
| **Miejsce** | Sekcja Ustawień (0.8.5): miejsce na pliki, co je zajmuje (części) i zapas systemu. Nie pamięć — pamięć to RAM (Zasoby). |
| **część** (miejsca na pliki) | Jeden wiersz podziału w sekcji Miejsce (0.8.5): folder stały z wszystkim w środku, Kosz, rezerwa dla systemu (`K1_CZESCI`); „System” = reszta zajętego. |
| **miejsce na pliki** | Ile plików zmieści skarbiec bez rezerwy dla systemu; pokazują je Ustawienia → Miejsce (0.8.5; w 0.8.1–0.8.3 Informacje). Format 1: połowa K1-DANE (druga połowa na kompaktowanie); format 2: 91–96 % (zapas wolnych odcinków, ADR 0036). |
| **format skarbca** | Układ danych na K1-DANE. Format 1 (0.5.6–0.8.1): dwie połowy; format 2 (0.8.2, ADR 0036): odcinki i spis skarbca. |
| **odcinek** (segment) | Część K1-DANE w formacie 2 skarbca (do 64 kawałków po 1 MiB); zapisy idą do kolejnych odcinków, kompaktowanie zwalnia po jednym (ADR 0036). |
| **przejście na format 2** | Jednorazowe przeniesienie magazynu z 0.5.6–0.8.1 (format 1) na format 2: w przerwach, do wolnej połowy K1-DANE, dopiero po potwierdzonym starcie; przerwa prądu zostawia stary albo nowy magazyn (0.8.2, ADR 0036). |
| **potwierdzony start** | Nowa wersja działa 15 s (pulpit albo ekran hasła) i nie czeka próba A/B innej wersji: nadzorca zatwierdza miejsce A/B i mówi skarbcowi `START_POTWIERDZONY` (ADR 0022, 0036). |
| **spis skarbca** (checkpoint) | Zapisana co jakiś czas lista żywych obiektów i ich miejsc w formacie 2; start czyta spis i to, co dopisano po nim (**ogon**), nie treść plików (ADR 0036). |
| **powiększenie K1-DANE** | K1-DANE rośnie do końca pendrive'a: pusta — sama przy pierwszym starcie (0.8.0), z danymi — po kliknięciu „Użyj całego pendrive'a na dane” w Ustawieniach (0.8.1, ADR 0033). Pliki zostają na miejscu. |
| **kawałek** | 1 MiB treści pliku: tak skarbiec trzyma pliki i tak je przesyłają czytnik, Pliki i skarbiec (od 0.8.1 plik do 4 GB = 4096 kawałków). |
| **zapis w toku** | Plik zapisywany kawałek po kawałku (0.8.1): widać go w Plikach dopiero po ostatnim kawałku; przerwany — znika bez śladu, stara treść zostaje. |
| **rezerwa dla systemu** | Ostatnie 8 MiB (albo 2 %, najwyżej ćwierć) skarbca, których pliki nie zajmą — na ustawienia, pulpit, baterię, tapetę (0.8.1). W sekcji Miejsce: „Zapas systemu”. |
| **granica czasu** | Najwcześniejszy czas, jaki skarbiec przyjmuje od zegara (0.8.1): czas budowy wersji K1 albo najpóźniejszy widziany. Zegar za granicą = Kosz niczego nie usuwa. |
| **odzyskany plik** | Plik, którego wpis skarbiec naprawił przy starcie (0.8.1): treść cała, nazwa poprawiona albo „Odzyskany plik <numer>”. |
| **domyślna aplikacja** | Aplikacja, w której podwójne kliknięcie („Otwórz”) otwiera plik danego typu; wybór w Ustawieniach → Aplikacje albo „Zawsze otwieraj tą aplikacją” (gdy typ ma co najmniej dwie). Które aplikacje otwierają który typ: tabela `K1_OTWIERANIE` (ADR 0030). Pliki nie są aplikacją do otwierania (od 0.8.2) — pokazują każdy plik; typ bez aplikacji: „K1 nie ma jeszcze aplikacji do tego typu pliku”. |
| **„Otwórz za pomocą”** | Otwarcie pliku w wybranej aplikacji: pozycja menu pod prawym przyciskiem (Pliki, pulpit), obok rozwija się lista aplikacji typu (0.8.2, decyzja D7; do 0.8.1 przycisk „Otwórz w…”). Aplikacja dostaje dostęp tylko do tego pliku. |
| **„Pokaż w Plikach”** | Pozycja menu pliku na pulpicie: Pliki otwierają się przy pliku (zaznaczony, szczegóły z prawej). |
| **okno wyboru pliku** (program „Wybór pliku”) | Okno K1, w którym użytkownik wybiera plik dla aplikacji, która o niego prosi; aplikacja dostaje tylko wybrany plik, nigdy listę (ADR 0030). |
| **dostęp do pliku** | Prawo aplikacji do jednego pliku, dane przez skarbiec po geście użytkownika („Otwórz za pomocą”, podwójne kliknięcie, okno wyboru pliku); trwa, dopóki aplikacja działa. |
| **gest zgody** | Działanie użytkownika w miejscu rysowanym przez K1 (Pobierz, wybór pliku, „Otwórz za pomocą”, przeciągnięcie) — jest zgodą na przeniesienie pliku. |
| **czytnik** | Usługa K1, która czyta pendrive z innego komputera (FAT) dla Plików — w klatce, tylko do odczytu; jedyna część K1, która czyta obcy system plików (ADR 0031). |
| **import** | Przeniesienie pliku z pendrive'a do Plików na wyraźny wybór użytkownika („Importuj”); oryginał zostaje na pendrivie, kopia dostaje pochodzenie „z pendrive'a”. |
| **ponowny import** | „Importuj” pliku, który już jest w Obrazach / Dokumentach z pendrive'a z tą samą nazwą, rozmiarem i czasem — K1 go pomija i mówi „Pominięto n — już zaimportowane” (0.8.2, decyzja D3). |
| **zasady nazw** | Co może być nazwą pliku lub folderu w K1 (`k1_nazwy.c`, 0.8.2): 1–127 bajtów, bez `\ / : * ? " < > |`, znaków niewidocznych i zmieniających kierunek, bez spacji (każdej szerokości) na brzegach i kropki na końcu, litera ze znakiem diakrytycznym jako jedna litera. Te same w skarbcu i czytniku. |
| **Gatekeeper** | Część K1, która przyjmuje pliki z zewnątrz (strefy, pendrive): czeka na zgodę, czyta z limitami, zapisuje do Plików z pochodzeniem. |
| **dekoder** | Jednorazowy program w klatce, który czyta jeden ryzykowny plik (obraz, PDF) i oddaje tylko obraz; potem znika. Od 0.8.0 usługa `dekoder` (PNG, JPEG; ADR 0032). |
| **Podgląd** | Aplikacja, która pokazuje zdjęcie w osobnym oknie: ← / →, obrót widoku, do Kosza, tapeta (0.8.2, ADR 0037). Do 0.8.2 „podgląd” znaczył panel po prawej w Plikach — dziś to **szczegóły** (decyzja D1). |
| **szczegóły** | Panel po prawej w Plikach: ikona, nazwa, typ, rozmiar, daty, pochodzenie zaznaczonego pliku, początek tekstu albo miniatura zdjęcia. |
| **miniatura** | Zdjęcie zmniejszone do 200 × 150 w szczegółach w Plikach (od dekodera). |
| **zdjęcia folderu** (w Podglądzie) | Zdjęcia (rodzina „obrazy”) folderu, z którego użytkownik otworzył zdjęcie w Podglądzie — tylko je Podgląd widzi (do odczytu i do Kosza), do zamknięcia okna (ADR 0037). |
| **tapeta ze zdjęcia** (tapeta „Zdjęcie”) | Własne zdjęcie użytkownika jako tapeta: w Plikach i (od 0.8.2) w Podglądzie „Ustaw jako tapetę”; ekran przechowuje je w skarbcu i rysuje „wypełniając” ekran (ADR 0032, 0037). |

## Strefy (od 0.10)
| Pojęcie | Znaczenie |
|---|---|
| **strefa** | Mikro-maszyna wirtualna z własnym jądrem dla obcej aplikacji (przeglądarka, Discord…). Błąd w niej nie sięga K1. |
| **osobna / wspólna strefa** | Jedna aplikacja na maszynę albo kilka drobnych, zaufanych aplikacji w jednej (ADR 0024). Przeglądarka zawsze osobna. |
| **stała / jednorazowa** | Strefa pamięta dane między uruchomieniami albo zaczyna zawsze od czysta i wszystko zapomina po zamknięciu. |
| **pełna izolacja / szybka grafika** | Strefa rysuje procesorem albo dostaje kartę graficzną przez pośrednika (słabsza izolacja). |
| **ramka** | Pasek tytułu i obwódka w kolorze strefy rysowane przez ekran K1 wokół jej okien; aplikacja nie może ich podrobić. Od 0.10.0 krok 3 (D1: szkic A): **pasek tytułu strefy** 40 px nad obrazem strefy + obwódka 3 px. (Krok 1–2: obwódka ze znaczkiem i plakietką — szkic B2, zastąpiony.) |
| **pasek tytułu strefy** (0.10.0 krok 3) | Pasek nad obrazem strefy w jej kolorze: ikona, „<tytuł> · <sesja> · osobna maszyna”, minimalizuj, zamknij — rysowany przez K1, nie przez strefę; za niego przesuwa się okno. |
| **pamięć kompresowana** (0.10.0 krok 3) | Wymiana (zram) w pamięci gościa strefy: rzadko używane strony ściśnięte, więc w tej samej pamięci strefy mieści się więcej. Nigdy na dysku; tylko w jądrze strefy (K1 nie ma wymiany). |
| **wirtualizacja** | Funkcja procesora (Intel VT-x, AMD-V), dzięki której K1 uruchamia strefy; włącza się ją w BIOS-ie. Bez niej strefa nie ruszy — K1 mówi, co włączyć. Jądro: KVM, urządzenie `/dev/kvm`. |
| **VMM** (strefa) | Program K1 `strefa`, który prowadzi jedną mikro-maszynę: jej pamięć, procesory, urządzenia, okno. Jeden proces na strefę, w klatce. (Virtual Machine Monitor.) |
| **gość** | To, co działa wewnątrz strefy: jądro strefy i jej programy. Dla K1 zawsze wrogi — wszystko od gościa sprawdzane pole po polu. |
| **jądro strefy** | Drugie jądro Linux z tych samych źródeł, z własną konfiguracją (`jadro/strefa.config`), działające wewnątrz stref; w rdzeniu jako `/rdzen/strefy/jadro`. |
| **obraz strefy** | Pliki strefy (SquashFS tylko do odczytu) w rdzeniu: `/rdzen/strefy/<nazwa>.img`; gość widzi go jako dysk `/dev/vda`. |
| **strefa próbna** (0.10.0) | Strefa testowa z naszym małym programem `gosc` w środku (wzór na ekranie, liczniki klawiszy i kliknięć, tryby złośliwego gościa) — tylko w obrazie testowym. |
| **urządzenie virtio** | Udawane urządzenie maszyny (dysk, konsola, losowość, klawiatura, wskaźnik, ekran, vsock), przez które gość rozmawia z VMM kolejkami w swojej pamięci. |
| **sesja przeglądarki** | Osobna strefa przeglądarki z własnym kolorem ramki: „Ogólna”, „Bank”, „Prywatna” (0.10.0, D2; wiersze `K1_STREFY`). Jednorazowa zapomina wszystko po zamknięciu. |
| **Przeglądarka** (0.10.0) | Pozycja menu Start: Firefox ESR z Debiana w strefie, w sesji „Ogólna” (ADR 0043). Nie mylić z przyszłą **Przeglądarką K1** (ADR 0025: okno rysuje K1, silnik WPE). |
| **kompozytor** (strefy) | Nasz program w strefie przeglądarki (`src/strefa/kompozytor`): serwer Wayland, który pokazuje okna przeglądarki na wyświetlaczu gościa i podaje im klawiaturę i mysz. |
| **przekaźnik** | Nasz program w strefie (`src/strefa/przekaznik`): przyjmuje połączenia przeglądarki (SOCKS5 na 127.0.0.1:1080) i każde przekazuje do VMM przez vsock. Nic nie czyta. |
| **łącznik** | Część VMM strefy (`lacznik.c`, `polaczenia.c`): sprawdza prośby SOCKS5, zamienia nazwy stron na adresy (DNS), prosi nadzorcę o gniazdo i przesyła bajty. Strefa nie ma karty sieciowej — to jej jedyna droga do internetu (ADR 0043). |
| **Raptor Bridge** | Nazwa całego mostu między strefą a światem przez K1 (nazwa właściciela, 2026-11-15): **przekaźnik** w strefie → vsock → **łącznik** w VMM → nadzorca (zasada D3, gniazdo). Nazwa systemu w dokumentach i rozmowie; w kodzie części mają swoje nazwy z tego słownika. Opis i weryfikacja — plan 0.10.5, krok 5. |
| **reguła D3** | Dokąd strefa może się łączyć: internet i sam router, nie reszta sieci domowej (`k1_siec_dla_strefy`; decyzja właściciela D3 planu 0.10.0). |

## Plan i wersje
| Pojęcie | Znaczenie |
|---|---|
| **mapa drogi** | Kolejność wersji MINOR z tematami: 0.9 Sieć, 0.10 Strefy, 0.11 Pliki ze stref… (ADR 0035, `docs/DO_ZROBIENIA.txt`). Zastępuje dawne „etapy M” (M7 = 0.9 itd.). |
| **plan wersji** | `docs/PLAN-<wersja>.txt`: kroki jednej wersji, luki, decyzje; jeden krok = jeden czat = jeden PR; zakres zamrożony. Zamknięty plan — `docs/archiwum/`. |
