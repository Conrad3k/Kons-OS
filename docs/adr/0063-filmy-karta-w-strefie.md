# 0063. Filmy kartą w strefie (0.13.2): kontekst filmów virtio-gpu, nasz sterownik VA, klatka w buforze karty strefy, dekoder na prośbę; X230 bez filmów kartą

- Status: Przyjęty; wdrożenie w krokach 2–4 planu `docs/PLAN-0.13.2.txt`
- Data: 2026-11-27
- Dotyczy: ADR 0049 (pkt 4–6 — **korekta tą decyzją**: droga w strefie, klatka, bramka zysku; pkt 3 — dekoder filmów
  zostaje), ADR 0044 (pamięć dekodera w rozdzielaczu), ADR 0051 (bufory karty strefy); plan `docs/PLAN-0.13.2.txt`
  (krok 1, L1–L6, D3, D4, T1–T8); opisy: `docs/komponenty/filmy.md`, `docs/komponenty/strefa.md`,
  `docs/komponenty/posrednik.md`

## Kontekst
Od 0.10.9 w K1 czeka uśpiony **dekoder filmów** (usługa `filmy`, ADR 0049 pkt 3): umie rozpakować H.264 kartą Intela,
ale nikt w strefie z niego nie korzysta — strona strefy wypadła z 0.10.9 (decyzja właściciela 2026-11-20). Plan 0.13.2
(prośba właściciela z 2026-11-26) ją dokłada. Krok 1 miał przed kodem sprawdzić w źródłach, którędy film i klatka przejdą
przez Firefoksa, sterownik VA strefy, jądro strefy i VMM — i czy X230 może z tego skorzystać.

Źródła (te same wersje co w obrazie strefy i w K1): Firefox ESR 153.7.0 (`firefox-esr_153.7.0esr.orig.tar.xz`, Debian
security), Linux 7.3.6 (`drivers/gpu/drm/virtio`), libva 2.24.1, intel-vaapi-driver 2.4.1 (i965). Wyniki — plan,
„Ustalenia z wykonania”, krok 1. Najważniejsze:
- **L1** (`dom/media/platforms/ffmpeg/FFmpegVideoDecoder.cpp`, `CreateImageVAAPI`, `GetVAAPISurfaceDescriptor`): Firefox
  bierze każdą rozpakowaną klatkę VA-API **tylko** jako dma-buf (`vaExportSurfaceHandle` z `DRM_PRIME_2`) i oddaje ją
  składaniu obrazu (`DMABufSurfaceYUV`); kopii do zwykłej pamięci (`vaGetImage`) w tej drodze nie ma. Bez
  `vaExportSurfaceHandle` w libva Firefox VA-API w ogóle nie ładuje (`VALibWrapper.cpp`).
- **L2** (`FFmpegVideoDecoder.cpp`; `widget/gtk/GfxInfo.cpp`): VA-API jest wyłączone, gdy Firefox nie składa obrazu
  kartą („Hardware WebRender is off, VAAPI is disabled”), a `vaapitest` rusza tylko przy sprzętowym GL (`mIsAccelerated`);
  `media.hardware-video-decoding.force-enabled` omija tylko listę zablokowanych sterowników, nie `mIsAccelerated`.
- **L3** (`security/sandbox/linux`): proces RDD czyta `/usr/lib` (cały katalog), otwiera `/dev/dri` (odczyt i zapis)
  i wysyła każdy ioctl typu DRM (`'d'` — to także `DRM_IOCTL_VIRTGPU_*`) i DMA-BUF (`'b'`); gniazd nie tworzy.
  libva wybiera sterownik po nazwie sterownika jądra: `virtio_gpu` nie ma w jej mapie, więc szuka
  `virtio_gpu_drv_video.so` (`va/drm/va_drm_utils.c`, „Fallback to the drm driver”); funkcję startową szuka od
  `__vaDriverInit_1_<swoja>` w dół do `__vaDriverInit_1_0` (`va/va.c`).
- **L4** (`virtgpu_kms.c`, `virtgpu_ioctl.c`): jądro strefy przyjmuje kontekst z każdym zbiorem możliwości (capset)
  1…63, który podało urządzenie, ale maskę liczy przesunięciem na typie `int` (`1 << id`) — numer ≥ 31 psuje maskę.
  Kontekst wymaga cech `CONTEXT_INIT` i `VIRGL` (są przy szybkiej grafice). Ten sam dma-buf zasobu otwarty drugim
  deskryptorem tego samego urządzenia daje **ten sam** zasób (`virtgpu_gem_prime_import`) — jego numer zna
  `DRM_IOCTL_VIRTGPU_RESOURCE_INFO`. `EXECBUFFER` nie ma limitu poza pamięcią (`vmemdup_user`).
- **L5** (i965 `gen75_mfd.c`, `gen8_mfd.c`, `i965_drv_video.c`): dekoder karty pisze tylko do powierzchni w kafelkach Y
  („must be tiled”, „tile walk, must be 1”); i965 umie przyjąć bufor z zewnątrz (`DRM_PRIME`) i bierze jego kafelki
  z jądra (`get_tiling`).
- **L6** (`FFmpegRuntimeLinker.cpp`, `FFmpegLibWrapper.h`): Firefox ładuje `libavcodec.so.62` … `.53` (najwyżej 63) —
  FFmpeg 8.x daje 62; `media.ffmpeg.enabled` na Linuksie domyślnie `true`. Pref `media.ffmpeg.vaapi.enabled` (dziś w
  `k1.js`) w ESR 153 już nie istnieje.

Decyzje właściciela: **D3: A** (2026-11-27) — pomiar na IdeaPadzie to dowód w raporcie, nie bramka. **D4** nie była
pytana: plan zadaje ją tylko, gdy L2 wyjdzie „da się”, a wyszło „nie da się” (niżej, pkt 5).

## Decyzja
1. **Droga w strefie (T1): kontekst filmów.** Drugi rodzaj kontekstu virtio-gpu w tym samym urządzeniu, które ma szybka
   grafika (jeden most w VMM-ie — ADR 0049 pkt 4): własny zbiór możliwości K1 **`K1_FILMY` = 20** (poza numerami 1–6
   virtio, poniżej 31 przez maskę jądra). Urządzenie podaje go tylko w sesji z pośrednikiem grafiki **i** gdy karta
   rozpakowuje filmy (pkt 6); bez tego jest tylko zbiór `DRM` (jak dziś). Treść zbioru: wersja, bity kodeków
   (`K1_KODEKI`) i największy rozmiar — tyle wystarcza `vaapitest` Firefoksa (otwiera VA, pyta o profile i punkty
   wejścia, tworzy i niszczy konfigurację — `widget/gtk/vaapitest/vaapitest.cpp`) bez uruchamiania dekodera.
   W kontekście filmów:
   - **pamięć wspólna** (zasób blob 0 kontekstu — memfd VMM-a, zmapowany w strefie jak pamięć wspólna mostu): na
     początku miejsce odpowiedzi VMM-a (numer polecenia, wynik, wymiary), dalej prośba `k1_filmy` (dane plastrów);
   - **polecenie** w `SUBMIT_3D` — mała struktura K1 o stałym układzie (otwórz film, klatka, pobierz klatkę do bufora,
     zamknij), z płotem: VMM odpowiada na łańcuch dopiero, gdy dekoder odpowiedział, a wynik jest już w pamięci
     wspólnej (virtio pozwala odpowiadać nie po kolei);
   - VMM sprawdza polecenie pole po polu (jak `gpu_most.c`: kontekst, zasób, długości), **treści prośby nie rozbiera**:
     kopiuje ją do okna dekodera, a sprawdza ją dekoder (`k1_filmy_sprawdz` — test na hoście i fuzzing już są).
     Numery filmów kontekstu VMM zamienia na numery dekodera (najwyżej `K1_FILMY_FILMOW_MAKS` filmów strefy naraz).
2. **Sterownik VA strefy (T2): nasz, w C** — biblioteka `virtio_gpu_drv_video.so` w katalogu sterowników VA obrazu
   strefy (`/usr/lib/x86_64-linux-gnu/dri`), budowana pod glibc strefy jak nasza Mesa, z funkcją startową
   `__vaDriverInit_1_0` (pasuje do każdej libva 2.x), tylko profile H.264 z bitów zbioru możliwości. libva znajduje go
   sama (pkt L3) — bez `LIBVA_DRIVER_NAME` i `LIBVA_DRIVERS_PATH`; piaskownica RDD go wczyta i przepuści jego ioctl.
   Błąd od K1 = błąd VA, Firefox gra film procesorem.
3. **Klatka (T3): w buforze karty strefy, dekoder pisze ją tam.** Skoro Firefox chce każdą klatkę jako dma-buf, który
   złoży kartą (L1), powierzchnia VA musi być **buforem karty strefy** (zasób kontekstu DRM pośrednika grafiki — ten,
   który zna Mesa w procesie GPU Firefoksa). Sterownik VA przydziela powierzchnie jako bufory liniowe NV12 w kontekście
   DRM (szczegół — krok 4: Mesa/GBM albo prośba mostu `GEM_CREATE`; NIEPEWNE: NV12 liniowy w `intel-virtio`), a polecenie
   „pobierz klatkę” podaje numer zasobu tego bufora. VMM bierze jego dma-buf od pośrednika (`POSR_BUFOR` — już jest) i daje
   go dekoderowi nowym komunikatem z deskryptorem; dekoder sprawdza rozmiar (`lseek`) z wymiarami filmu i wpisuje klatkę
   NV12 prosto do niego (układ: wiersz `k1_filmy_rzad`, część kolorów od pełnej strony za jasnością — jedna funkcja
   w `k1_filmy.h`). Koszt: karta kopiuje powierzchnię do obrazu liniowego, procesor K1 przepisuje go do bufora — jak dziś
   do okna, bez kopii w VMM-ie i w strefie; Firefox składa kartą. Pamięć bufora liczy się do pośrednika (T14 planu
   0.10.9) — jedno liczenie.
   **Bez kopii** (karta rozpakowuje prosto do bufora strefy) wymaga bufora w kafelkach Y od strony gościa (L5) i importu
   w Mesie — „Później”, po pomiarze na IdeaPadzie (D3). **Odrzucone:** klatka do pamięci gościa (poziom 1 z ADR 0049
   pkt 6) — Firefox i tak potrzebuje dma-bufa karty.
4. **Kiedy dekoder strefy (T5 — korekta T6 planu 0.10.9): na prośbę.** VMM prosi nadzorcę o dekoder przy pierwszym
   otwarciu filmu (`STREFA_FILMY` na fd 7), nadzorca sprawdza: przełącznik sesji włączony, pośrednik tej strefy działa,
   karta „umie”, dekoder tej sesji nie zakończył się błędem — i odpowiada kanałem do dekodera (komunikat z deskryptorem)
   albo odmową (bez). Odmowa = film procesorem. VMM zamyka kanał, gdy zamknie się ostatni kontekst filmów — dekoder
   kończy się sam i oddaje pamięć; następny film = nowy dekoder. Dekoder zakończony błędem (awaria, sygnał) — do końca
   sesji bez filmów kartą. Większość sesji nie gra filmów, a dekoder przy filmie 4K trzyma do `K1_FILMY_PAMIEC`.
   Co umie karta, VMM wie od startu: strefa z pośrednikiem dostaje od nadzorcy na fd 7 **jako pierwsze**
   `STREFA_FILMY_KARTA` (bity kodeków, największy rozmiar; 0 — filmów kartą nie ma) i czyta je przed startem maszyny.
5. **X230 (D4): bez filmów kartą.** Firefox nie używa VA-API bez składania kartą i bez sprzętowego GL (L2), a X230 nie ma
   szybkiej grafiki (brak pełnego PPGTT — ADR 0049 pkt 7). Jedyną drogą byłoby udawanie `glxtest` i WebRendera — odrzucone
   w ADR 0049. Plan zadaje D4 tylko przy „da się”, więc wybór „B” wynika ze źródeł; Ustawienia na X230 mówią o filmach
   tak jak dziś o szybkiej grafice (krok 4, D1).
6. **Bramka zysku (D3: A — korekta ADR 0049 pkt 6).** Pomiar na IdeaPadzie (procesor sesji w Zasobach, bateria) to dowód
   w raporcie kroku 4, nie warunek: filmy kartą wchodzą, chyba że są gorsze niż procesorem (więcej procesora, zacinanie,
   błędy obrazu, rozjechany dźwięk) — wtedy krok 4 zostaje bez prefów filmów do poprawki.
7. **Prefy i biblioteki (T4, T6).** FFmpeg w strefie — własna mała budowa (ADR 0049 pkt 8): `libavcodec.so.61` (FFmpeg
   7.1 z Debiana 13 — krok 2; Firefox bierze każdą od .62 w dół, L6), LGPL bez części GPL, dekoder H.264 i `h264_vaapi`;
   libva z pakietów Debiana (`libva.so.2`, `libva-drm.so.2`, z `vaExportSurfaceHandle`). Sesja z filmami kartą (VMM: `k1.filmy=1` w linii poleceń gościa, jak `k1.grafika=1`)
   dostaje w `user.js` `media.hardware-video-decoding.enabled` = `true` oraz `media.mediasource.vp9.enabled` i
   `media.av1.enabled` = `false` (D2: YouTube daje wtedy H.264); bez filmów — odwrotnie, zapis przy każdym starcie w obie
   strony. `media.ffmpeg.enabled` zostaje domyślne (`true`); martwy `media.ffmpeg.vaapi.enabled` wypada z `k1.js`.
8. **Testy bez karty (T8).** Na stanowisku (`scripts/stanowisko.sh`) Firefox rysuje llvmpipe, więc VA-API nie rusza nigdy
   (L2) — próba „Firefox z próbnym sterownikiem VA” w kontenerze nic nie pokaże i jej nie ma. Zamiast niej: strefa próbna
   mówi kontekstem filmów wprost (tryby 23 i 24, dekoder testowy i silnik testowy pośrednika), sterownik VA ma test na
   hoście (tłumaczenie VA na polecenia K1, fuzzing odpowiedzi), a Firefoksa z kartą sprawdza właściciel na IdeaPadzie
   (krok 4 f; about:support → „Media”).

## Odrzucone
- vsock (piaskownica RDD nie tworzy gniazd — S7 planu 0.10.9) i wyłączenie piaskownicy RDD.
- Nowe urządzenie virtio-video / virtio-media z V4L2 w Firefoksie (`v4l2test`): sterownik gościa i dekoder bezstanowy
  V4L2 w Firefoksie — więcej kodu po obu stronach, wynik ten sam (dma-buf); jeden most w VMM-ie zostaje.
- Klatka do pamięci gościa i kopia w sterowniku VA (pkt 3) — Firefox potrzebuje dma-bufa karty.
- i965 w strefie (mówi i915, a strefa ma virtio-gpu), frontend VA Mesy (tylko sterowniki gallium), iHD (C++).
- Udawanie `glxtest`/WebRendera dla X230 (ADR 0049, „Odrzucone”).
- Dekoder strefy razem z VMM-em (T6 planu 0.10.9): każda sesja z szybką grafiką trzymałaby proces i915/i965 bez filmu.

## Skutki
- ADR 0049: dopisek „pkt 4–6: [korekta: ADR 0063]”. Niezmiennik 7 bez zmian: węzeł render dalej tylko dekoder filmów
  i pośrednik; dekoder dostaje dodatkowo dma-buf bufora strefy (tylko do zapisu klatki, `mmap` bez `PROT_EXEC`).
- Krok 2: FFmpeg (`libavcodec.so.62`) i libva w obrazie strefy, `k1.js` bez martwego prefu.
- Krok 3: zbiór możliwości `K1_FILMY`, kontekst filmów w VMM-ie (podział `gpu_lacze.c` przed rozbudową), komunikaty
  `STREFA_FILMY_KARTA`, `STREFA_FILMY`, `STREFA_FILMY_KANAL`, `STREFA_FILMY_ODMOWA` (nadzorca ↔ VMM) i klatka do bufora
  z deskryptorem (VMM → dekoder), tryby 23 i 24 strefy próbnej.
- Krok 4: sterownik VA strefy, prefy, Ustawienia (D1), test właściciela na IdeaPadzie.
- RYZYKA R98 (prośby dekodera ze strefy, dma-buf od VMM-a), nowy wiersz dla FFmpeg i libva w strefie (krok 2) i dla
  sterownika VA w procesie RDD (krok 4).
