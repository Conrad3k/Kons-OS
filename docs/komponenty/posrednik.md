# Komponent: posrednik (pośrednik grafiki — szybka grafika strefy przez most „native context”)

| | |
|---|---|
| **Status** | ZAIMPLEMENTOWANE (0.10.9 krok 5: usługa, format próśb mostu, pytanie przy starcie, linia w Ustawieniach; krok 6: konteksty gościa, pośrednik strefy uruchamiany z jej VMM-em przy włączonym przełączniku, strona strefy — `docs/komponenty/strefa.md`, „Szybka grafika”); PRZETESTOWANE na hoście (`test_posrednik`, `fuzz_posrednik`, `test_gpu_most`, `fuzz_gpu_most`), w systemie w QEMU (`t_posrednik`: silnik testowy, prośby i odpowiedzi, złośliwe prośby, drugi kontekst gościa, pamięć, zawieszenie) i z wnętrza strefy próbnej (`test-strefa.sh`, tryby 16 i 17). Praca na prawdziwej karcie Intela (Firefox rysuje kartą) — do sprawdzenia przez właściciela na IdeaPadzie (QEMU nie udaje karty Intela) |
| **Pliki** | `src/posrednik/main.c` (kanał, okno, pamięć wspólna, udostępnianie i zwalnianie buforów), `prosby.c` (prośby mostu: kopia, sprawdzenie, wykonanie, odpowiedzi), `plot.c` (płoty: kiedy karta skończyła paczkę pracy, zawieszenie), `karta.c` (karta przez węzeł render; co umie karta), `testowy.c` (silnik testowy: karty nie ma), `posrednik.h`; `src/wspolne/k1_posrednik.{h,c}` (format próśb, tabele `K1_POSR_PROSBY` i `K1_POSR_IOCTLE`, sprawdzanie pole po polu, tabela buforów), `k1_kom_posrednik.c` (komunikaty); `src/nadzorca/posrednik.c` (pytanie przy starcie, pośrednik strefy), `karta.c` (węzeł render — wspólne z dekoderem filmów), `profile.c` (`KLATKA_POSREDNIK`); Ustawienia `przegladarka.c` (linia o szybkiej grafice) |
| **Decyzje** | ADR 0049 (pkt 2, 7; plan 0.10.9 T10–T15, S36–S41, S44) |

## Cel
Przeglądarka w strefie ma móc rysować kartą graficzną laptopa (płynne przewijanie, WebGL) — a strefa nadal nie dotyka
karty. W strefie działa prawdziwy sterownik karty (Mesa, iris z `intel-virtio`, krok 6) i sam przygotowuje pracę; przez
most do K1 idą tylko proste prośby: utwórz bufor, udostępnij go, wykonaj paczkę pracy, powiedz, kiedy skończona. Mały,
zamknięty w klatce proces K1 sprawdza każdą prośbę pole po polu i dopiero wtedy przekazuje ją karcie. To most
„native context” (T10) — wybrany zamiast mostu na poleceniach rysowania (virgl, Venus: K1 musiałby rozumieć cały język
grafiki) i zamiast oddania karty strefie (VFIO: laptop ma jedną kartę, obraz zostaje u `ekranu`).

## Granica zaufania
Klient (VMM strefy — w nim działa obcy kod przeglądarki i sterownika karty gościa) jest wrogi. Pośrednik:
- przyjmuje komunikaty tylko od UID swojego klienta (VMM swojej strefy; nadzorca i program testowy — UID 0);
- strumień próśb z okna najpierw **kopiuje** do własnej pamięci, dopiero potem go sprawdza (klient może zmieniać okno
  w każdej chwili); do pamięci wspólnej mostu tylko pisze (odpowiedź buduje u siebie i przepisuje co najwyżej tyle
  bajtów, ile gość tam przewidział);
- sprawdza każdą prośbę (`k1_posr_sprawdz`): nagłówek (długość, wyrównanie, miejsce odpowiedzi), rodzaj z tabeli
  `K1_POSR_PROSBY`, każde pole w zakresie, a **żadnego wskaźnika gościa nie używa** — każdy wskaźnik, który czyta
  jądro, buduje sam z pól, które przeszły sprawdzenie (łańcuchy rozszerzeń od nowa, numery zasobów gościa → uchwyty
  karty, a uchwyty nigdy nie wracają do gościa);
- przepuszcza **mniej niż virglrenderer** (wzór, nie biblioteka — T11): bez relokacji (iris używa tylko przypiętych
  adresów), bez płotów gościa (`FENCE_IN`, `FENCE_ARRAY`, `USE_EXTENSIONS` — wskaźniki), bez `SECURE`, bez parametrów
  kontekstu spoza listy (`SSEU`, `PROTECTED_CONTENT`…), bez kontekstu, którego nie da się zablokować (`BANNABLE` = 1),
  bez rozszerzeń bufora, których karty laptopów nie mają (`PROTECTED_CONTENT`, `SET_PAT`), bez zapytań o liczniki
  wydajności (`PERF_CONFIG`), tylko jeden rejestr do odczytu (zegar karty — jak lista jądra);
- liczy pamięć buforów strefy i odmawia, zanim zapyta kartę (T14).

Kartę od pracy innych stref i ekranu K1 oddziela sama karta: każdy klient węzła render ma własną przestrzeń adresów
karty (pełne PPGTT) i własne rejestry kontekstu. Na karcie bez tego pośrednik nie startuje (`K1_POSR_NIE_*`) — X230
(Ivy Bridge, tylko „aliasing PPGTT”) szybkiej grafiki nie ma (ADR 0049 pkt 7, D8). Sterownik karty w jądrze K1 jest mimo
to w zasięgu sprawdzonych próśb strefy — to ryzyko (RYZYKA R100) i dlatego przełącznik będzie domyślnie wyłączony (D5).

Jeden pośrednik na strefę (Bank nie dzieli go z Ogólną), każdy z własnym UID i cgroup. Awaria pośrednika albo
zawieszenie karty przez kontekst strefy kończy **szybką grafikę tej strefy** — K1, ekran i inne strefy żyją; restartu
nie ma (do następnego otwarcia sesji — T15).

## Uprawnienia
| Proces | UID | seccomp (poza bazą) | Landlock | cgroup i limity | deskryptory |
|---|---|---|---|---|---|
| pytanie przy starcie (wiersz `POSREDNIK`) | 110 | nic; `ioctl` tylko typu DRM (`'d'`) | brak ścieżek | `posrednik`: `K1_POSR_PAMIEC` (768 MiB) + 32 MiB, 1 proces, `RLIMIT_FSIZE` 0, 16 + 256 deskryptorów | fd 3: kanał do nadzorcy, fd 4: węzeł render karty |
| pośrednik strefy *nr* | 410 + *nr* | jak wyżej | brak ścieżek | `posrednik-<nr>`, jak wyżej | fd 3: kanał do klienta, fd 4: węzeł render (silnik testowy: `/dev/null`); potem okno, pamięć wspólna (zmapowane), kopie płotów |

Pośrednik nie ma `open`, `memfd_create` ani `socket`: okno i pamięć wspólną daje mu klient, bufor dla VMM-a to dma-buf
z karty (`PRIME_HANDLE_TO_FD`, ioctl DRM), kopie płotów — `fcntl(F_DUPFD_CLOEXEC)` (baza). Węzeł render otwiera tylko
nadzorca (`karta.c`, wspólne z dekoderem filmów) i tylko sterownika **i915**; dostają go wyłącznie dekoder filmów
i pośrednik grafiki — nigdy ekran, aplikacje ani VMM (skill kons-dev §4 p. 7, ADR 0049 pkt 2). Węższa lista numerów
ioctl w seccomp (dziś: cały typ `'d'`, jak ekran i dekoder filmów) — „opakowanie” z T15 (RYZYKA R100).

## Prośby mostu — jedna tabela (`K1_POSR_PROSBY`, `k1_posrednik.h`)
Układ każdej prośby jest bajt w bajt ten z virglrenderera 1.3.0 (jego pliki i915_proto.h i drm_hw.h, katalog drm), bo
pisze je Mesa w strefie; `_Static_assert` pilnuje rozmiarów i przesunięć, a `src/posrednik/prosby.c` — zgodności
z układami jądra (`i915_drm.h`).

| Prośba | Co robi | Co K1 sprawdza (poza nagłówkiem) | Odpowiedź |
|---|---|---|---|
| `IOCTL` | jeden z prostych ioctl z tabeli `K1_POSR_IOCTLE` | numer z tabeli, długość argumentu = jego rozmiar; pola: zegar karty (jedyny rejestr), tiling ≤ Y, domeny CPU/GTT/WC, `flags`/`pad` = 0, parametry kontekstu z listy (rozmiar 0, priorytet −1023…0), przestrzeń adresów bez rozszerzeń; numer zasobu gościa → uchwyt karty | `ret` (+ argument, gdy jądro go zapisuje) |
| `GETPARAM` | parametr karty | numer 1…59 | `ret`, wartość (wersja mapowania GTT ≥ 5 → 4, jak virglrenderer) |
| `QUERY` | zapytanie o kartę (topologia, silniki, regiony pamięci…) | rodzaj z listy (bez liczników wydajności), `flags` = 0 (podplastry: silnik rysowania), ≤ 64 KiB | `ret`, długość, dane |
| `GEM_CREATE` | bufor nazwany numerem blob gościa | rozmiar 1 B…512 MiB, blob ≥ 1, pamięć strefy (T14) | — |
| `GEM_CREATE_EXT` | to samo z regionami pamięci | jak wyżej + jedno rozszerzenie „regiony” (1…4, system albo urządzenie) | — |
| `KONTEKST` | kontekst karty z parametrami | do 8 parametrów z listy (priorytet, przechwytywanie błędów, `BANNABLE` = 1, odzyskiwalny, przestrzeń adresów, trwałość, silniki 1…8 znanych klas), `ctx_id` = 0, ≤ 64 kontekstów strefy | `ret`, numer kontekstu |
| `EXECBUFFER` | paczka pracy | flagi z listy (silnik, `NO_RELOC`, `HANDLE_LUT`, `BATCH_FIRST`), 1…4096 buforów bez relokacji, flagi bufora z listy (bez `PAD_TO_SIZE`), `rsvd` = 0, wyrównanie potęgą 2; płot wyjścia prosi K1 | `ret` |
| `MMAP_MODE` | jak gość mapuje bufor | tryb GTT…FIXED | — |
| `BUSY` | czy karta pracuje na buforze | zasób | `ret`, zajęty |

Strumień idzie prośba po prośbie; pierwsza, która nie przejdzie, zatrzymuje strumień (`POSR_WYNIK` b = `K1_POSR_BLAD_*`,
c = ile zrobiono) — jak w virglrendererze. Odmowa jądra (np. `EINVAL` dla nieznanego zasobu w paczce) nie zatrzymuje:
wraca do gościa w odpowiedzi (`ret` = errno). Po każdej prośbie pośrednik wpisuje jej numer (`seqno`) do pamięci wspólnej.

## Bufory (T12, T14)
Gość tworzy bufor (`GEM_CREATE`) pod numerem blob; gdy w strefie powstaje z niego zasób virtio-gpu, VMM mówi
`POSR_BLOB` (zasób, blob, rozmiar — musi się zgadzać) i od tej chwili prośby wskazują bufor numerem zasobu.
`POSR_BUFOR` daje VMM-owi bufor jako dma-buf (do odczytu i zapisu; VMM zmapuje go do pamięci gościa jako miejsce pamięci
KVM — **nigdy wykonywalnie**, niezmiennik 1; krok 6) i sposób mapowania (`K1_POSR_MAPA_*`). `POSR_ZWOLNIJ` — zasób
odchodzi, uchwyt karty też. Wszystkie bufory strefy razem najwyżej `K1_POSR_PAMIEC` (768 MiB; liczone przed
utworzeniem — `K1_POSR_BLAD_PAMIEC`, gość dostaje błąd prośby, Firefox wraca do rysowania procesorem). Bufory liczą się
do cgroup procesu, który je wypełnia (`mm/shmem.c`): pośrednika, gdy kartę z nimi wiąże, VMM-a, gdy gość pisze przez
mapowanie. Rozdzielacz pamięci (ADR 0044) dolicza pamięć pośrednika do strefy (`posrednik_pamiec_mib`). Jądro przy
braku pamięci zabija pośrednika strefy przed usługą K1 (`oom_score_adj` jak VMM w tle). Bufory utworzone, ale nigdy
nieprzypięte (do `K1_POSR_CZEKAJACE_MAKS` = 512), żyją do końca pośrednika — ograniczone limitem pamięci.

## Płoty i zawieszenie (T15, S39)
Każda paczka pracy daje płot wyjścia (sync_file jądra); pośrednik trzyma ostatni każdej osi (1 + silnik paczki, jak
`drm_timeline` virglrenderera). `POSR_PLOT` (oś, numer) — odpowiedź `POSR_PLOT_GOTOWY`, gdy ostatnia paczka tej osi się
skończy (pusta oś: od razu). Gdy płot się skończy, pośrednik pyta kartę, czy kontekst tej paczki był winny resetu
(`GET_RESET_STATS`: `batch_active`); i915 przy zawieszeniu resetuje sam silnik, oznacza winny kontekst i go blokuje
(`gt/intel_reset.c`), więc następna paczka dostaje `EIO`. Jedno i drugie = **zawieszenie**: bit kontekstu w
`banned_ctx_mask` pamięci wspólnej, pośrednik kończy się kodem `K1_POSR_KONIEC_ZAWIESZENIE` (3), nadzorca loguje powód
„kontekst strefy zawiesił kartę — koniec szybkiej grafiki tej strefy”. Obrazu ekranu reset silnika nie dotyka (S39 —
sprawdza właściciel na sprzęcie).

## Przebieg
**Pytanie przy starcie** (nadzorca `posrednik.c`): bez węzła render i915 od razu „brak karty”. Z węzłem — 4 s po starcie
pulpitu (sekundę po dekoderze filmów) nadzorca uruchamia pośrednika `POSREDNIK` (argument 0), ten sprawdza kartę
(`karta.c`: sterownik i915, pełne PPGTT, izolacja kontekstów, przypięte adresy, płoty wyjścia), odpowiada `POSR_UMIE`
i kończy. Nadzorca podaje wynik Ustawieniom (`POSR_KARTA`); pośrednik zakończony bez odpowiedzi → stan „błąd” z kodem.

**Pośrednik strefy** (`posrednik_dla_strefy`): nadzorca uruchamia go dla strefy *nr* (karta: tylko gdy pytanie dało
„umie”), klient dostaje drugi koniec kanału. Od kroku 6 razem z VMM-em strefy, gdy jej sesja ma włączony przełącznik
„Szybka grafika” (VMM: fd 10), koniec z VMM-em (`posrednik_strefa_koniec`); prosi o niego też program testowy
(`TEST_POSREDNIK`; strefa próbna w trybach 16 i 17 — silnik testowy). Pośrednik kończy się, gdy klient zamknie kanał
(bufory wracają do karty z zamknięciem węzła render).

**Konteksty gościa** (krok 6): każdy proces strefy, który otworzy kartę (Firefox: test GL przy starcie, proces GPU; Mesa:
jeden kontekst virtio-gpu na otwarcie), to po stronie gościa osobny kontekst — VMM numeruje je 1…`K1_POSR_GOSCIE` (8).
Każdy ma swoją pamięć wspólną mostu (`POSR_WSPOLNA` obiekt = kontekst gościa), swoje numery blobów (każdy proces liczy od 1),
swoje osie płotów; strumień próśb mówi, którego jest (`POSR_PROSBY` obiekt). Konteksty i przestrzenie adresów karty
pamiętają, który kontekst gościa je utworzył; `POSR_GOSC_KONIEC` (proces zamknął kartę) niszczy je, zamyka jego bufory
bez numeru zasobu i jego płoty (czekające — od razu „gotowe”), odmapowuje jego pamięć wspólną. Zasoby są strefy (numeruje
je jądro gościa dla całego urządzenia) — zostają do `POSR_ZWOLNIJ`.

1. Pośrednik → klient: `POSR_UMIE` (czy karta oddziela strefy; numer karty i wersja sterownika — VMM poda je gościowi
   w zbiorze możliwości DRM virtio-gpu, krok 6). Karta, która nie oddziela — pośrednik kończy się zaraz po nim.
2. Klient → pośrednik: `POSR_OKNO` + memfd dokładnie `K1_POSR_OKNO` (1 MiB) i dla każdego kontekstu gościa
   `POSR_WSPOLNA` + memfd pamięci wspólnej mostu (jego blob 0, 4 KiB…1 MiB) — wszystkie z pieczęciami „bez zmniejszania
   i powiększania”.
3. `POSR_PROSBY` (kontekst gościa, długość strumienia w oknie) → `POSR_WYNIK` (c = ile zrobiono); odpowiedzi w pamięci
   wspólnej tego kontekstu.
4. `POSR_BLOB`, `POSR_BUFOR` (→ `POSR_DMABUF` + dma-buf), `POSR_ZWOLNIJ`, `POSR_PLOT` (→ `POSR_PLOT_GOTOWY`),
   `POSR_GOSC_KONIEC`.
Komunikat, który nie przejdzie sprawdzenia, jest odrzucany bez odpowiedzi (liczony w logu, najwyżej linia na 100).

## Komunikaty
| Typ | Kierunek | Pola |
|---|---|---|
| `POSR_OKNO` | klient → posrednik | wszystko 0; + memfd okna |
| `POSR_WSPOLNA` | klient → posrednik | obiekt = kontekst gościa 1…8 (krok 6), a = rozmiar 4096…1 MiB (wielokrotność 4096); + memfd pamięci wspólnej |
| `POSR_PROSBY` | klient → posrednik | obiekt = kontekst gościa, a = długość strumienia 16 B…1 MiB (wielokrotność 4) |
| `POSR_BLOB` | klient → posrednik | obiekt = zasób 1…16383, a = blob ≥ 1 (numer kontekstu gościa d), b, c = rozmiar 1 B…512 MiB, d = kontekst gościa |
| `POSR_BUFOR` | klient → posrednik | obiekt = zasób |
| `POSR_ZWOLNIJ` | klient → posrednik | obiekt = zasób |
| `POSR_PLOT` | klient → posrednik | obiekt = oś 0…64, a, b = numer płotu, c = kontekst gościa (jego osie) |
| `POSR_GOSC_KONIEC` | klient → posrednik | obiekt = kontekst gościa, reszta 0 — proces strefy zamknął kartę (odpowiedź `POSR_WYNIK`) |
| `POSR_UMIE` | posrednik → klient | a = `K1_POSR_KARTA_BRAK`/`NIE`/`UMIE`, b = powód (`K1_POSR_NIE_*`, tylko NIE), c = numer karty PCI, d = rewizja, e = wersja sterownika (c–e tylko UMIE) |
| `POSR_WYNIK` | posrednik → klient | obiekt = zasób albo 0, a = typ prośby, b = 0 albo `K1_POSR_BLAD_*`, c = ile próśb zrobiono (`POSR_PROSBY`) |
| `POSR_DMABUF` | posrednik → klient | obiekt = zasób, a = `K1_POSR_MAPA_*`, b, c = rozmiar; + dma-buf |
| `POSR_PLOT_GOTOWY` | posrednik → klient | obiekt = oś, a, b = numer płotu, c = kontekst gościa |
| `POSR_KARTA` | nadzorca → Ustawienia (fd 4) | a = `K1_POSR_KARTA_*` (też `SPRAWDZA`, `BLAD`), b = kod (NIE: powód; BLAD: kod wyjścia albo 128 + sygnał) |
| `TEST_POSREDNIK` | program testowy → nadzorca | a = strefa, b = 0 karta / 1 silnik testowy → `TEST_GNIAZDO` + kanał klienta |

## Ustawienia → Przeglądarka
Druga cicha linia pod listą sesji (pod linią o filmach): „Karta graficzna tego komputera obsługuje szybką grafikę.”,
„Ten komputer nie obsługuje szybkiej grafiki: karta nie oddziela pracy stref.” (X230), „Ten komputer nie obsługuje
szybkiej grafiki.” (maszyna wirtualna, inna karta), „Pośrednik grafiki nie uruchomił się (kod N). Strony rysuje procesor.”
albo, przez pierwsze sekundy po starcie, „Sprawdzam, czy karta graficzna obsługuje szybką grafikę…”. Przełącznika
jeszcze nie ma (krok 6).

## Silnik testowy (S44)
QEMU nie udaje karty Intela, więc drogę pośrednika (okno, pamięć wspólna, sprawdzanie, odpowiedzi, pamięć, koniec po
zawieszeniu) testuje `testowy.c`: odpowiada jak karta Skylake, która oddziela strefy; bufor to tylko numer, a „pamięć
karty” to jeden rzadki memfd od nadzorcy (512 MiB, na fd 4 zamiast węzła render) — `POSR_BUFOR` daje go jako każdy bufor
(krok 6: VMM strefy próbnej mapuje go jak dma-buf, gość pisze i czyta „swój bufor”); paczka nie daje płotu, a paczka
o długości `K1_POSR_TEST_ZAWIESZENIE` „zawiesza kartę” (`EIO`). Wybiera go nadzorca tylko na prośbę programu testowego
(`TEST_POSREDNIK` b = 1, strefa próbna w trybach 16 i 17 — kanał testowy i strefa próbna są tylko w obrazie testowym),
nigdy dla sesji przeglądarki: niczego nie rysuje, więc i tak nie zastąpiłby karty.

## Pomiar (0.10.11 krok 1)
Od 0.10.11 krok 2 (ADR 0051) dma-buf bufora (`POSR_BUFOR`) VMM bierze też, gdy strefa pokaże bufor jako swój wyświetlacz
(`SET_SCANOUT_BLOB`) — i podaje go ekranowi K1 (nigdy strefie); pośrednik nie robi nic nowego. VMM mierzy każdą prośbę mostu (`POSR_PROSBY`) tam i z powrotem i liczy rodzaje mapowań buforów (`POSR_DMABUF` a: WB,
UC, WC) — wiersz `pomiar VMM` co 10 s (`docs/komponenty/strefa.md`, „Pomiar drogi obrazu”). Pośrednik sam nic nie loguje
więcej.

## Testy
| Test | Rodzaj |
|---|---|
| host `test_posrednik`: każda prośba, jak pisze ją Mesa, przechodzi; każde pole poza listą (relokacje, płoty gościa, wskaźniki w rozszerzeniach, ioctl i parametry spoza listy, inny rejestr, zapytania o wydajność, `BANNABLE` 0, nieznane klasy silników), złe długości, odpowiedź, która się nie mieści — odmowa; każdy ucięty kawałek prośby odrzucony bez czytania poza nim; tabela buforów (blob, zasób, rozmiar, pamięć strefy, limit czekających); komunikaty `POSR_*`, `TEST_POSREDNIK` | jednostkowy / negatywny |
| `fuzz_posrednik` (strumień próśb + rozmiar miejsca na odpowiedzi) | fuzzing |
| w systemie `t_posrednik` (QEMU): bez karty Intela nadzorca nie daje pośrednika z kartą; silnik testowy strefy próbnej: `POSR_UMIE`, klatka (UID 411, cgroup `posrednik-1`, seccomp, tylko przekazane deskryptory), okno złej wielkości odrzucone, strumień trzech próśb z odpowiedziami w pamięci wspólnej, bufor → zasób, paczka pracy, płot; relokacje, ioctl spoza listy, płot gościa, wskaźnik w rozszerzeniu, ucięty strumień — odrzucone; nieznany zasób w paczce → `EINVAL` dla gościa; komunikaty poza zakresem — odrzucone, pośrednik żyje; bufory ponad limit — odmowa; „zawieszenie karty” kończy pośrednika, kontekst zablokowany, ekran i nadzorca żyją | funkcjonalny / negatywny |
| w systemie `t_proby`: próby w klatkach `posrednik` i `posrednik-1`…`posrednik-5` | negatywny |
| `test-m4` (QEMU): Ustawienia → Przeglądarka — linia „nie obsługuje szybkiej grafiki” (stan 1 w logu) | funkcjonalny |
| sprzęt (właściciel, obraz USB z PR-a): linia w Ustawieniach na IdeaPadzie („obsługuje”) i X230 („nie oddziela pracy stref”) | ręczny |
