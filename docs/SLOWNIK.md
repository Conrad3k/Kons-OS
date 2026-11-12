# Słownik K1

Każde pojęcie ma **jedno** znaczenie — tak samo w kodzie, dokumentach, komunikatach w systemie i rozmowie.
Nowe pojęcie = nowy wiersz tutaj (zasada czytelności, `AGENTS.md`). Kod używa polskich nazw z tej listy
(np. `klatka`, `kanal`, `skarb`); w nawiasie — angielski odpowiednik, gdy ktoś szuka w literaturze.

## Start i system
| Pojęcie | Znaczenie |
|---|---|
| **jądro** | Jądro Linux z naszą minimalną konfiguracją (`jadro/k1.config`). Jedyna rzecz, którą bierzemy z Linuksa. |
| **rdzeń** | Cały system K1 poza jądrem: obraz tylko do odczytu `/rdzen` (SquashFS). Nie da się go zmienić w działającym systemie. |
| **sprawdzanie rdzenia** (dm-verity) | Przy każdym odczycie jądro sprawdza, że rdzeń jest bajt w bajt taki, jak w wydaniu; zmiana = zatrzymanie. |
| **wydanie** | Jedna wersja K1 (`k1-vX.Y.Z`): podpisany plik `k1.efi` z jądrem i rdzeniem, ISO i obraz na pendrive. |
| **podpis** | Dowód, że wydanie pochodzi od właściciela projektu (klucz w sekrecie CI). Instalator odmawia niepodpisanych i starszych. |
| **miejsce A / B** | Dwa miejsca na system w partycji K1-EFI: aktualizacja trafia do wolnego, stare zostaje na wypadek błędu. |
| **K1-EFI** | Partycja startowa: pliki startowe i miejsca A/B. |
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
| **komunikat** | Wiadomość w kanale: zawsze 32 bajty, sprawdzana pole po polu przez odbiorcę. |
| **wejście** | Usługa, która jako jedyna czyta klawiaturę i mysz. |
| **ekran** | Usługa, która jako jedyna rysuje na karcie graficznej: składa okna, rysuje kursor, (od 0.10) ramki stref. |
| **pulpit** (`gui`) | Usługa rysująca paski, menu Start i (od 0.7.5) ikony na pulpicie. Nie widzi treści okien. |
| **przybornik** | Wspólna biblioteka do rysowania: tekst, przyciski, okna, ikony, kolory K1. |
| **ikona na pulpicie** | Kafelek aplikacji (od 0.8.0 też pliku) w siatce na pulpicie: zaznacz, przeciągnij, otwórz podwójnym kliknięciem. Gdy plików Pulpitu jest więcej niż miejsc, ostatnia wolna komórka to „Jeszcze n” (0.8.2) — otwiera Pliki w folderze Pulpit. |
| **przypięta aplikacja** | Aplikacja stale widoczna na dolnym pasku (kolejność ustala właściciel); nieprzypięta pokazuje się na pasku tylko, gdy działa. |
| **menu kontekstowe** | Menu pod prawym przyciskiem myszy (pulpit, ikona, dolny pasek, menu Start; od 0.8.2 plik i folder w Plikach — to samo menu co na pulpicie, `k1_menu` w przyborniku). |
| **zaznaczenie** | To, co jest wybrane w Plikach (0.8.2 krok 6): jeden element (kliknięcie) albo wiele (Ctrl+klik, Shift+klik, Ctrl+A). Przyciski, menu i przeciąganie działają na całe zaznaczenie. `k1_zaznaczenie` w przyborniku. |
| **przeciągnąć** | Chwycić lewym przyciskiem zaznaczone pliki (albo ikonę) i przesunąć je myszą (0.8.2 krok 6); przy kursorze karteczka z nazwą i liczbą oraz **znak** — co się stanie po upuszczeniu („Przenieś do…”, „Do Kosza”, „Kopiuj do… (import)”, „Nie można…”). |
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
| **miejsce na pliki** | Ile plików zmieści skarbiec bez rezerwy dla systemu; pokazują je Ustawienia → Informacje (0.8.1). Format 1: połowa K1-DANE (druga połowa na kompaktowanie); format 2: 91–96 % (zapas wolnych odcinków, ADR 0036). |
| **format skarbca** | Układ danych na K1-DANE. Format 1 (0.5.6–0.8.1): dwie połowy; format 2 (0.8.2, ADR 0036): odcinki i spis skarbca. |
| **odcinek** (segment) | Część K1-DANE w formacie 2 skarbca (do 64 kawałków po 1 MiB); zapisy idą do kolejnych odcinków, kompaktowanie zwalnia po jednym (ADR 0036). |
| **przejście na format 2** | Jednorazowe przeniesienie magazynu z 0.5.6–0.8.1 (format 1) na format 2: w przerwach, do wolnej połowy K1-DANE, dopiero po potwierdzonym starcie; przerwa prądu zostawia stary albo nowy magazyn (0.8.2, ADR 0036). |
| **potwierdzony start** | Nowa wersja działa 15 s (pulpit albo ekran hasła) i nie czeka próba A/B innej wersji: nadzorca zatwierdza miejsce A/B i mówi skarbcowi `START_POTWIERDZONY` (ADR 0022, 0036). |
| **spis skarbca** (checkpoint) | Zapisana co jakiś czas lista żywych obiektów i ich miejsc w formacie 2; start czyta spis i to, co dopisano po nim (**ogon**), nie treść plików (ADR 0036). |
| **powiększenie K1-DANE** | K1-DANE rośnie do końca pendrive'a: pusta — sama przy pierwszym starcie (0.8.0), z danymi — po kliknięciu „Użyj całego pendrive'a na dane” w Ustawieniach (0.8.1, ADR 0033). Pliki zostają na miejscu. |
| **kawałek** | 1 MiB treści pliku: tak skarbiec trzyma pliki i tak je przesyłają czytnik, Pliki i skarbiec (od 0.8.1 plik do 4 GB = 4096 kawałków). |
| **zapis w toku** | Plik zapisywany kawałek po kawałku (0.8.1): widać go w Plikach dopiero po ostatnim kawałku; przerwany — znika bez śladu, stara treść zostaje. |
| **rezerwa dla systemu** | Ostatnie 8 MiB (albo 2 %, najwyżej ćwierć) skarbca, których pliki nie zajmą — na ustawienia, pulpit, baterię, tapetę (0.8.1). |
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
| **ramka** | Obwódka w kolorze strefy rysowana przez ekran K1 wokół jej okien; aplikacja nie może jej podrobić. |
| **sesja przeglądarki** | Osobna strefa silnika stron (np. „bank”, „prywatna”), z własnym kolorem karty (ADR 0025). |

## Plan i wersje
| Pojęcie | Znaczenie |
|---|---|
| **mapa drogi** | Kolejność wersji MINOR z tematami: 0.9 Sieć, 0.10 Strefy, 0.11 Pliki ze stref… (ADR 0035, `docs/DO_ZROBIENIA.txt`). Zastępuje dawne „etapy M” (M7 = 0.9 itd.). |
| **plan wersji** | `docs/PLAN-<wersja>.txt`: kroki jednej wersji, luki, decyzje; jeden krok = jeden czat = jeden PR; zakres zamrożony. Zamknięty plan — `docs/archiwum/`. |
