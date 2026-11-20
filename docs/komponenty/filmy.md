# Komponent: filmy (dekoder filmów — sprzętowe dekodowanie filmów kartą graficzną)

| | |
|---|---|
| **Status** | ZAIMPLEMENTOWANE (0.10.9 krok 2: usługa, format próśb, pytanie przy starcie; linia w Ustawieniach usunięta w kroku 6 — filmy kartą nie w 0.10.9, decyzja właściciela 2026-11-20: „może” później); PRZETESTOWANE na hoście (`test_filmy`, `fuzz_filmy`) i w systemie w QEMU (`t_filmy`: dekoder testowy, klatka, złośliwe prośby, pamięć). Rozpakowanie na prawdziwej karcie Intela — PLANOWANE do sprawdzenia przez właściciela na sprzęcie (QEMU nie udaje karty Intela). Strona strefy (VMM, Firefox) — krok 3 |
| **Pliki** | `src/filmy/main.c` (kanał, okno, prośby), `karta.c` + `karta_h264.c` (karta przez VA-API: libva + i965), `testowy.c` (dekoder testowy: szare klatki), `obce.c` + `obce.txt` (co obce biblioteki dostają od programu K1 bez ścieżek i bibliotek współdzielonych), `filmy.h`, `karta.h`; `src/wspolne/k1_filmy.{h,c}` (format próśb, tabela `K1_KODEKI`, sprawdzanie pole po polu), `k1_kom_filmy.c` (komunikaty); `src/nadzorca/filmy.c` (węzeł render, pytanie przy starcie, dekoder strefy), `profile.c` (`KLATKA_FILMY`); `src/zewnetrzne/va/zrodla.txt` + `scripts/zbuduj-va.sh` (biblioteki); Ustawienia `przegladarka.c` (linia o karcie) |
| **Decyzje** | ADR 0049 (pkt 1–3; plan 0.10.9 T1–T3, T6–T8, S10–S17, S26) |

## Cel
Film z przeglądarki w strefie (krok 3) ma rozpakowywać karta graficzna laptopa, nie procesor — a strefa nadal nie
dotyka karty. Strefa wysyła do K1 tylko skompresowane dane i opis obrazu na poziomie VA-API; mały, zamknięty w klatce
proces K1 sprawdza każde pole i dopiero wtedy przekazuje to karcie. Klatki wracają jako NV12 (poziom 1 z T9).

## Granica zaufania
Klient (VMM strefy — w nim działa obcy kod przeglądarki) jest wrogi. Dekoder:
- przyjmuje komunikaty tylko od UID swojego klienta (VMM swojej strefy; nadzorca i program testowy — UID 0);
- z okna (pamięć wspólna z klientem) najpierw **kopiuje** prośbę do własnej pamięci, dopiero potem ją sprawdza
  (klient może zmieniać okno w każdej chwili); do okna tylko pisze (klatka);
- sprawdza prośbę pole po polu (`k1_filmy_sprawdz`): wersja, kodek filmu, liczby plastrów i bajtów z wiersza kodeka,
  długości, które muszą się dokładnie sumować, wymiary obrazu = wymiary powierzchni filmu, każde pole w zakresie
  H.264, każda wskazana klatka w granicach filmu, a **odniesienia tylko do klatek już rozpakowanych**; plaster tylko
  w granicach danych tej prośby;
- karta (i915 w jądrze, i965 w dekoderze) dostaje tylko to, co przeszło sprawdzenie. Sterownik karty i i965 są więc
  w zasięgu danych ze strefy — to ryzyko (RYZYKA R98, R99).

Jeden dekoder na strefę (Bank nie dzieli go z Ogólną), każdy z własnym UID i cgroup. Awaria albo przejęcie dekodera
dotyka tylko filmów tej strefy: film idzie na procesor w strefie, strefa i K1 żyją; restartu nie ma (do następnego
otwarcia sesji — T6).

## Uprawnienia
| Proces | UID | seccomp (poza bazą) | Landlock | cgroup i limity | deskryptory |
|---|---|---|---|---|---|
| pytanie przy starcie (wiersz `FILMY`) | 109 | `getuid`, `geteuid` (libva porównuje je przed czytaniem środowiska); `ioctl` tylko typu DRM (`'d'`) | brak ścieżek | `filmy`: `K1_FILMY_PAMIEC` (512 MiB) + 96 MiB, 1 proces, `RLIMIT_FSIZE` 0 | fd 3: kanał do nadzorcy, fd 4: węzeł render karty |
| dekoder strefy *nr* | 400 + *nr* | jak wyżej | brak ścieżek | `filmy-<nr>`, jak wyżej | fd 3: kanał do klienta, fd 4: węzeł render (dekoder testowy: `/dev/null`) |

Węzeł render (`/dev/dri/renderD*`) otwiera tylko nadzorca i tylko sterownika **i915** (`/sys/class/drm/renderD*/device/driver`);
dostaje go wyłącznie dekoder filmów — nigdy ekran (on ma węzeł card i DRM master), aplikacje ani VMM (skill kons-dev §4
p. 7, ADR 0049 pkt 2). Na węźle render jądro przyjmuje tylko ioctl rysowania i buforów (`DRM_RENDER_ALLOW`), nie obrazu.

Obce biblioteki (`obce.c`, lista `obce.txt` → `--wrap` w Makefile): funkcje plikowe (`open`, `fopen`, `access`, `stat`,
`opendir`, `readlink`…) kończą się od razu błędem „nie ma” bez wywołania jądra; `dlopen`/`dlsym` dają libvie funkcję
startową i965 wkompilowaną w program (bez bibliotek współdzielonych); rodzaj węzła DRM z samego deskryptora. Zabronione
wywołanie jądra kończy dekoder kodem 159 (jak każdą usługę) — Ustawienia pokazują wtedy kod.

## Kodeki — jedna tabela (`K1_KODEKI`, `k1_filmy.h`)
| Wiersz | Nazwa w Ustawieniach | Profil VA | Największy | Klatek | Plastrów | Bajtów obrazu |
|---|---|---|---|---|---|---|
| `H264_PODST` | H.264 | ConstrainedBaseline (13) | 4096 × 2304 | 20 | 256 | 8 MiB |
| `H264_GLOWNY` | H.264 | Main (6) | 4096 × 2304 | 20 | 256 | 8 MiB |
| `H264_WYSOKI` | H.264 | High (7) | 4096 × 2304 | 20 | 256 | 8 MiB |

Dlaczego tylko H.264 (krok 2): to jedyny format, który dekoduje i965 na obu laptopach (S9 — HD 4000: H.264, MPEG-2,
VC-1; HD 520: H.264, HEVC 8-bit, VP8, bez VP9), a YouTube w Firefoksie dostaje H.264 na prośbę przeglądarki (D4).
Nowy kodek = wiersz tutaj + jego rodzina parametrów w `k1_filmy.h` i jej sprawdzanie (HEVC, VP8 — jeśli krok 3 pokaże, że
Firefox w strefie ich użyje). Karta może mieć mniejsze granice niż tabela: dekoder pyta ją (`vaQuerySurfaceAttributes`)
i podaje najmniejsze.

## Pamięć (T8, S26)
Karty laptopów nie mają osobnej pamięci: bufory i915 to zwykła pamięć RAM, liczona do cgroup procesu, który je wypełnia
(`mm/shmem.c`), czyli do klatki dekodera. Przed otwarciem filmu dekoder liczy jego bufory (`k1_filmy_film_bajty`:
powierzchnie w wyrównaniu karty 128 × 32 + ćwierć na wektory ruchu) i odmawia (`K1_FILMY_BLAD_PAMIEC`), gdy wszystkie
otwarte filmy razem przekroczyłyby `K1_FILMY_PAMIEC` (512 MiB; 1080p z 20 klatkami ≈ 78 MB, 4K ≈ 354 MB) — film idzie
wtedy na procesor. Zamknięcie filmu oddaje bufory, koniec procesu — wszystko. Rozdzielacz pamięci (ADR 0044) dolicza
pamięć dekodera do strefy, gdy wybiera sesję do zamknięcia (`filmy_pamiec_mib`); balon jej nie odda. Jądro przy braku
pamięci zabija dekoder strefy przed usługą K1 (`oom_score_adj` jak VMM w tle). Dekoder testowy ma 32 MiB (testy: K1
w 512 MiB).

## Przebieg
**Pytanie przy starcie** (nadzorca `filmy.c`): bez węzła render i915 od razu „brak karty”. Z węzłem — 3 s po starcie
pulpitu nadzorca uruchamia dekoder `FILMY` (argument 0), ten inicjuje VA-API, pyta kartę o profile i rozmiary,
odpowiada `FILMY_UMIE` i kończy. Nadzorca podaje wynik Ustawieniom (`FILMY_KARTA`); dekoder zakończony bez odpowiedzi
→ stan „błąd” z kodem wyjścia.

**Dekoder strefy** (`filmy_dla_strefy`): nadzorca uruchamia go dla strefy *nr* (karta: tylko gdy pytanie dało „umie”),
klient dostaje drugi koniec kanału. Krok 3: razem z VMM-em strefy przy włączonym przełączniku, koniec z VMM-em
(`filmy_strefa_koniec`). Krok 2: prosi o niego tylko program testowy (`TEST_FILMY`). Dekoder kończy się, gdy klient
zamknie kanał.

1. Dekoder → klient: `FILMY_UMIE` (co karta rozpakowuje).
2. Klient → dekoder: `FILMY_OKNO` + memfd dokładnie `K1_FILMY_OKNO` bajtów z pieczęciami „bez zmniejszania i powiększania”
   (zmniejszone okno zakończyłoby dekoder sygnałem SIGBUS). Pierwsze 9 MiB — prośba, reszta (16 MiB) — klatka.
3. `FILMY_OTWORZ` (film 1…4, kodek, wymiary, liczba klatek): konfiguracja VA, powierzchnie, kontekst.
4. `FILMY_KLATKA` (film, klatka docelowa, długość prośby): kopia z okna, sprawdzenie, bufory VA (`VAPictureParameterBufferH264`,
   macierz, plastry, dane), obraz do karty.
5. `FILMY_POBIERZ` (film, klatka): karta kopiuje powierzchnię do liniowego obrazu NV12, procesor przepisuje go do okna
   (wiersze `k1_filmy_rzad`) → `FILMY_OBRAZ`.
6. `FILMY_ZAMKNIJ`: bufory filmu wracają.
Na każdą prośbę 2–6 (poza udanym `POBIERZ`) — `FILMY_WYNIK` (0 albo `K1_FILMY_BLAD_*`). Komunikat, który nie przejdzie
sprawdzenia, jest odrzucany bez odpowiedzi (liczony w logu).

## Komunikaty
| Typ | Kierunek | Pola |
|---|---|---|
| `FILMY_OKNO` | klient → filmy | wszystko 0; + memfd okna |
| `FILMY_OTWORZ` | klient → filmy | obiekt = film 1…4, a = kodek 1…`K1_KODEKI_MAKS`, b × c = 16…4096 × 16…2304, d = klatek 1…20 |
| `FILMY_KLATKA` | klient → filmy | obiekt = film, a = klatka docelowa 0…19, b = długość prośby 32 B…9 MiB |
| `FILMY_POBIERZ` | klient → filmy | obiekt = film, a = klatka |
| `FILMY_ZAMKNIJ` | klient → filmy | obiekt = film |
| `FILMY_UMIE` | filmy → klient | a = bity kodeków (tylko przy UMIE), b = `K1_FILMY_KARTA_BRAK`/`NIC`/`UMIE`, c = status VA 0…255 (NIC), d × e = największy rozmiar (UMIE) |
| `FILMY_WYNIK` | filmy → klient | obiekt = film (OKNO: 0), a = typ prośby, b = 0 albo `K1_FILMY_BLAD_*` |
| `FILMY_OBRAZ` | filmy → klient | obiekt = film, a = klatka, b × c = wymiary, d = długość wiersza (`k1_filmy_rzad(b)`); klatka w oknie od 9 MiB |
| `FILMY_KARTA` | nadzorca → Ustawienia (fd 4) | a = bity kodeków, b = `K1_FILMY_KARTA_*` (też `SPRAWDZA`, `BLAD`), c = kod (BLAD: kod wyjścia albo 128 + sygnał; NIC: status VA) |
| `TEST_FILMY` | program testowy → nadzorca | a = strefa, b = 0 karta / 1 dekoder testowy → `TEST_GNIAZDO` + kanał klienta |

## Ustawienia → Przeglądarka
Do 0.10.9 krok 5 jedna cicha linia pod listą sesji mówiła, co rozpakowuje karta („Karta graficzna tego komputera
rozpakowuje filmy: H.264” / „Ten komputer nie rozpakowuje filmów kartą graficzną.” …). **Od kroku 6 jej nie ma** — decyzja
właściciela 2026-11-20: filmy kartą nie wchodzą do 0.10.9 (strona strefy dla filmów — „może”, `docs/DO_ZROBIENIA.txt`).
Usługa i pytanie przy starcie zostają (gotowe na później); Ustawienia tylko logują `FILMY_KARTA` (test-m4).

## Dekoder testowy (T7, S17)
QEMU nie udaje karty Intela, więc drogę dekodera (okno, sprawdzanie, pamięć, klatka) testuje `testowy.c`: bez karty,
klatka szara (jasność 16 + pierwszy bajt danych mod 220 — test widzi, która prośba ją zrobiła). Wybiera go nadzorca
tylko na prośbę programu testowego (`TEST_FILMY` b = 1 — kanał testowy jest tylko w obrazie testowym), nigdy dla VMM-a:
niczego nie rozpakowuje, więc i tak nie zastąpiłby karty.

## Testy
| Test | Rodzaj |
|---|---|
| host `test_filmy`: dobra prośba H.264 (plaster P, odniesienie, macierz) przechodzi; każde pole poza zakresem, złe długości, przepełnienie przesunięcia, odniesienie do nierozpakowanej klatki, lista ponad aktywne, plaster I z odniesieniem — odmowa; otwieranie wg tabeli, pamięć filmów, nazwy w Ustawieniach; komunikaty `FILMY_*`, `TEST_FILMY` | jednostkowy / negatywny |
| `fuzz_filmy` (prośba + stan filmu) | fuzzing |
| host `test_pamiec_stref`: pamięć dekodera liczy się przy wyborze sesji do zamknięcia | jednostkowy |
| w systemie `t_filmy` (QEMU): bez karty Intela nadzorca nie daje dekodera z kartą; dekoder testowy strefy próbnej: `FILMY_UMIE`, klatka (UID 401, cgroup `filmy-1`, seccomp, tylko przekazane deskryptory), okno złej wielkości odrzucone, film, obraz, klatka w oknie; zły kodek, rozmiar ponad tabelę, za dużo klatek, dane plastra poza prośbą, odniesienie do klatki, której nie ma, zła długość — odrzucone, dekoder żyje; zalew filmów — odmowa przy braku pamięci, zamknięte oddają pamięć; koniec z kanałem | funkcjonalny / negatywny |
| w systemie `t_proby`: próby w klatkach `filmy` i `filmy-1`…`filmy-5` | negatywny |
| `test-m4` (QEMU): Ustawienia → Przeglądarka — linia „nie rozpakowuje” (stan 1 w logu) | funkcjonalny |
| sprzęt (właściciel, obraz USB z PR-a): linia w Ustawieniach na X230 (H.264) i IdeaPadzie (H.264), pulpit z i915 | ręczny |
