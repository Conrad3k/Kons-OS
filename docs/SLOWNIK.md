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
| **ekran** | Usługa, która jako jedyna rysuje na karcie graficznej: składa okna, rysuje kursor, (od M8) ramki stref. |
| **pulpit** (`gui`) | Usługa rysująca paski, menu Start i (od 0.7.5) ikony na pulpicie. Nie widzi treści okien. |
| **przybornik** | Wspólna biblioteka do rysowania: tekst, przyciski, okna, ikony, kolory K1. |
| **ikona na pulpicie** | Kafelek aplikacji (od 0.8.0 też pliku) w siatce na pulpicie: zaznacz, przeciągnij, otwórz podwójnym kliknięciem. |
| **przypięta aplikacja** | Aplikacja stale widoczna na dolnym pasku (kolejność ustala właściciel); nieprzypięta pokazuje się na pasku tylko, gdy działa. |
| **menu kontekstowe** | Menu pod prawym przyciskiem myszy (pulpit, ikona, dolny pasek, menu Start). |
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
| **Kosz** | Usunięte pliki; można je przywrócić; usuwane na zawsze po 30 dniach albo ręcznie („Opróżnij Kosz”). To stan pliku, nie folder; ikona na pulpicie. |
| **kompaktowanie** | Skarbiec przepisuje żywe dane do drugiej połowy K1-DANE i zapomina stare wersje (ADR 0015, 0034): dysk dostaje miejsce z powrotem, start znowu jest szybki. |
| **rezerwa dla systemu** | Ostatnie 8 MiB (albo 2 %, najwyżej ćwierć) skarbca, których pliki nie zajmą — na ustawienia, pulpit, baterię, tapetę (0.8.1). |
| **granica czasu** | Najwcześniejszy czas, jaki skarbiec przyjmuje od zegara (0.8.1): czas budowy wersji K1 albo najpóźniejszy widziany. Zegar za granicą = Kosz niczego nie usuwa. |
| **odzyskany plik** | Plik, którego wpis skarbiec naprawił przy starcie (0.8.1): treść cała, nazwa poprawiona albo „Odzyskany plik <numer>”. |
| **domyślna aplikacja** | Aplikacja, w której podwójne kliknięcie otwiera plik danego typu; wybór w Ustawieniach → Aplikacje. Które aplikacje otwierają który typ: tabela `K1_OTWIERANIE` (ADR 0030). |
| **„Otwórz w…”** | Otwarcie pliku w wybranej aplikacji (Pliki, pulpit). Aplikacja dostaje dostęp tylko do tego pliku. |
| **okno wyboru pliku** (program „Wybór pliku”) | Okno K1, w którym użytkownik wybiera plik dla aplikacji, która o niego prosi; aplikacja dostaje tylko wybrany plik, nigdy listę (ADR 0030). |
| **dostęp do pliku** | Prawo aplikacji do jednego pliku, dane przez skarbiec po geście użytkownika („Otwórz w…”, okno wyboru pliku); trwa, dopóki aplikacja działa. |
| **gest zgody** | Działanie użytkownika w miejscu rysowanym przez K1 (Pobierz, wybór pliku, „Otwórz w…”, przeciągnięcie) — jest zgodą na przeniesienie pliku. |
| **czytnik** | Usługa K1, która czyta pendrive z innego komputera (FAT) dla Plików — w klatce, tylko do odczytu; jedyna część K1, która czyta obcy system plików (ADR 0031). |
| **import** | Przeniesienie pliku z pendrive'a do Plików na wyraźny wybór użytkownika („Importuj”); oryginał zostaje na pendrivie, kopia dostaje pochodzenie „z pendrive'a”. |
| **Gatekeeper** | Część K1, która przyjmuje pliki z zewnątrz (strefy, pendrive): czeka na zgodę, czyta z limitami, zapisuje do Plików z pochodzeniem. |
| **dekoder** | Jednorazowy program w klatce, który czyta jeden ryzykowny plik (obraz, PDF) i oddaje tylko obraz; potem znika. Od 0.8.0 usługa `dekoder` (PNG, JPEG; ADR 0032). |
| **podgląd** | Panel po prawej w Plikach: szczegóły zaznaczonego pliku, początek tekstu albo zmniejszone zdjęcie (od dekodera). |
| **tapeta ze zdjęcia** (tapeta „Zdjęcie”) | Własne zdjęcie użytkownika jako tapeta: w Plikach „Ustaw jako tapetę”; ekran przechowuje je w skarbcu i rysuje „wypełniając” ekran (ADR 0032). |

## Strefy (od M8)
| Pojęcie | Znaczenie |
|---|---|
| **strefa** | Mikro-maszyna wirtualna z własnym jądrem dla obcej aplikacji (przeglądarka, Discord…). Błąd w niej nie sięga K1. |
| **osobna / wspólna strefa** | Jedna aplikacja na maszynę albo kilka drobnych, zaufanych aplikacji w jednej (ADR 0024). Przeglądarka zawsze osobna. |
| **stała / jednorazowa** | Strefa pamięta dane między uruchomieniami albo zaczyna zawsze od czysta i wszystko zapomina po zamknięciu. |
| **pełna izolacja / szybka grafika** | Strefa rysuje procesorem albo dostaje kartę graficzną przez pośrednika (słabsza izolacja). |
| **ramka** | Obwódka w kolorze strefy rysowana przez ekran K1 wokół jej okien; aplikacja nie może jej podrobić. |
| **sesja przeglądarki** | Osobna strefa silnika stron (np. „bank”, „prywatna”), z własnym kolorem karty (ADR 0025). |
