# 0049. Karta graficzna dla stref: i915 w K1, filmy kartą i szybka grafika przez most „native context”

- Status: Przyjęty; wdrożony w 0.10.9 (kroki 2, 5, 6 planu `docs/archiwum/PLAN-0.10.9.txt`); filmy kartą po stronie
  strefy (pkt 4–6) — nie wdrożone: wypadły z wersji (decyzja właściciela 2026-11-20, „może” w `docs/DO_ZROBIENIA.txt`)
  — wracają w 0.13.2; pkt 4–6: **[korekta: ADR 0063]** (kontekst filmów zamiast wspólnego kontekstu DRM, klatka w
  buforze karty strefy, dekoder strefy na prośbę, pomiar zamiast bramki zysku)
- Data: 2026-11-19
- Dotyczy: SPEC (grafika rozdzielona na procesy; niezmiennik „jeden właściciel zasobu” — skill kons-dev §4 p. 7),
  ADR 0020 p. 2 (strefy rysują procesorem), ADR 0042, ADR 0043 (Firefox w strefie, rysowanie procesorem); plan 0.10.9:
  krok 1, decyzje T1–T16, D8; luki S1–S9, S26–S30, S36–S39

## Kontekst
Plan 0.10.9 chce dwóch przełączników przy sesji przeglądarki: **filmy kartą** (strefa wysyła skompresowany film, K1
rozpakowuje go kartą) i **szybka grafika** (przeglądarka w strefie rysuje kartą). Dziś tylko `ekran` dotyka karty, przez
simpledrm (obraz z firmware), a strefa rysuje procesorem. Krok 1 miał przed pisaniem kodu sprawdzić, czy ta droga
w ogóle działa. Wykonanie (PR #181, 2026-11-19): ustalenia ze źródeł (Linux 7.3.5, Mesa 26.2.6, virglrenderer 1.3.0,
libva 2.24.1, intel-vaapi-driver 2.4.1, FFmpeg 8.1.2, Firefox ESR 153.7) — szczegóły w planie, krok 1, „Ustalenia
z wykonania”; obraz próbny z i915 i sondą karty (kod tylko w historii PR-a #181, commit `aeeb6f7`, nie w `main`).
Właściciel uruchomił obraz próbny na IdeaPadzie (2026-11-19): pulpit z i915 bez czarnego ekranu, płynny. Okno sondy
się nie pokazało (błąd sondy: okno wyższe niż ekran 1366×768); właściciel zdecydował iść dalej bez drugiego testu,
więc pomiary ze sprzętu przechodzą do kroków 2, 3 i 5 (niżej, „Sprawdzenia przeniesione”).

## Decyzja
1. **Sterownik karty (T1).** Jądro K1 dostaje i915 (Intel Gen7 Ivy Bridge — X230, Gen9 Skylake — IdeaPad). i915 zawsze
   usuwa simpledrm przy starcie (`aperture_remove_conflicting_pci_devices`, bez wyjątku dla `disable_display`), więc
   przejmuje też obraz (KMS); `ekran` dalej jedynym właścicielem węzła `card` i DRM master. simpledrm, bochs i vmwgfx
   zostają dla innych komputerów i maszyn wirtualnych. Koszt: +1,3 MiB jądra.
2. **Właściciel karty (zmiana niezmiennika 7).** Obrazem steruje tylko `ekran`. Węzeł **render** karty
   (`/dev/dri/renderD*`) otwiera nadzorca i daje go tylko dwóm nowym usługom, każdej w klatce, jedna na strefę, tylko
   przy włączonym przełączniku: **dekoderowi filmów** (krok 2) i **pośrednikowi grafiki** (krok 5). Nigdy `ekranowi`,
   aplikacjom ani VMM-owi strefy. Listy ioctl w ich profilach (typ `'d'` i numery z wykonania, S16).
3. **Dekoder filmów (T2, T3, T6, T8).** libva + intel-vaapi-driver (i965) + libdrm, statycznie z naszym musl, bez dlopen:
   program podaje własne `dlopen`/`dlsym`, które zwracają funkcję startową i965 — libva bez łatek (sprawdzone budową).
   i965 z łatkami Debiana 0001–0004 (bez programów karty bez źródeł; dekodowanie zostaje). Ze strefy przychodzą prośby
   na poziomie VA (parametry obrazu i plasterków, skompresowane dane), sprawdzane pole po polu tabelą `K1_KODEKI`;
   klatki wracają jako NV12 (poziom 1 z T9). Jeden proces na strefę, awaria = film na procesorze. Bufory i915 liczą się
   do cgroup procesu, który je wypełnia (`mm/shmem.c`: `mem_cgroup_charge(folio, NULL, …)`), więc do klatki dekodera —
   jedno liczenie pamięci z balonem i podziałem (ADR 0044). Ostrzeżenia obcego kodu (`-Wall -Wextra`): i965 1163,
   libva 254, libdrm_intel 108 — kompilowany bez naszego `-Werror`, z naszymi flagami utwardzenia (jak Wuffs).
4. **Droga w strefie (T4, T12): B — kontekst virtio-gpu z węzłem `/dev/dri` w strefie.** Proces dekodowania Firefoksa
   (RDD) nie utworzy gniazda (`socket` → EACCES), więc vsock (A) odpada bez wyłączania piaskownicy; za to otwiera
   `/dev/dri` i wysyła ioctl DRM i DMA-BUF. Filmy i szybka grafika jadą tym samym mostem w VMM-ie.
5. **Filmy kartą tylko razem z szybką grafiką (nowe, S6).** Firefox ESR 153 (`widget/gtk/GfxInfo.cpp`) sprawdza VA-API
   tylko wtedy, gdy test OpenGL (`glxtest`) znalazł **sprzętowy** sterownik: przy braku GL, llvmpipe albo softpipe
   dekodowanie kartą jest zablokowane. Strefa bez szybkiej grafiki nie ma GL, więc filmy kartą działają tylko w sesji
   z szybką grafiką. Skutek: na X230 (bez szybkiej grafiki, pkt 7) filmy kartą w przeglądarce nie działają — właściciel
   przyjął to 2026-11-19 („na X230 ta opcja i tak nie zadziała”). Kolejność kroków: 1, 4, 2, 5, 6, 3 (plan, punkt 0);
   jak pokazać to w Ustawieniach — decyzja D9 planu (krok 3).
6. **Droga gotowej klatki (T9): poziom 1 w 0.10.9.** Klatka wraca do strefy jako NV12. Główny koszt poziomu 1 z T9 —
   składanie filmu procesorem w strefie — znika, bo filmy działają tylko z szybką grafiką, a wtedy Firefox składa obraz
   kartą. Poziom 2 (klatka prosto na ekran K1) → „Później”. Bramka zysku przenosi się do kroku 3: jeśli na IdeaPadzie
   filmy kartą nie dadzą wyraźnie mniej procesora niż bez nich (kierunek z T9: co najmniej połowa), przełącznik filmów
   wypada z wersji, a szybka grafika zostaje.
7. **Szybka grafika (T10–T15): most „native context”.** Strona gościa: Mesa z opcją `intel-virtio-experimental`
   (sterownik iris, Gen8+; budujemy sami, bez LLVM — S43). Strona K1: **pośrednik grafiki**, nasz kod w C na wzór
   `virglrenderer/src/drm/i915` (protokół: 9 rodzajów próśb, `IOCTL_SIMPLE` z listą ok. 12 ioctl — sprawdzany tabelą,
   niezmiennik 6). virglrenderer nie sprawdza rodzaju PPGTT — pośrednik **odmawia startu na karcie bez pełnego PPGTT**
   (`I915_PARAM_HAS_ALIASING_PPGTT` < 2). Ivy Bridge (X230) ma tylko „aliasing” (`i915_pci.c`, `GEN7_FEATURES`) —
   wszystkie konteksty w jednej przestrzeni adresów — więc **na X230 szybkiej grafiki nie ma** (T15, D8 A); Skylake
   (IdeaPad) ma pełne, 48 bitów. Zawieszenie karty: i915 resetuje sam silnik i oznacza winny kontekst, po kilku
   zawieszeniach blokuje go (`gt/intel_reset.c`); obrazu to nie dotyka. Minimum bezpieczeństwa w 0.10.9 i „opakowanie”
   później — jak T15 planu (wpis w RYZYKA w kroku 5, termin najpóźniej 0.13).
8. **FFmpeg w strefie (T5): B — własna budowa** tylko z potrzebnymi dekoderami (sprawdzone na musl w sondzie:
   `--enable-small`, H.264 + VA-API; w strefie pod glibc Debiana, krok 3) — pakiet Debiana ciągnie wiele bibliotek.

## Odrzucone
- Most na poleceniach rysowania (virgl — OpenGL, Venus — Vulkan): K1 musiałby rozumieć cały język grafiki (T10).
- Przekazanie karty strefie (VFIO): laptop ma jedną kartę, obraz zostaje u `ekranu`.
- Droga przez vsock (T4 A) i wyłączenie piaskownicy RDD Firefoksa.
- Sterownik iHD (C++, tylko Gen8+; K1 nie ma biblioteki C++) — także dla VP9 na Skylake, którego i965 nie umie.
- Własny `glxtest`/`vaapitest` w obrazie strefy, który „udaje” sprzętowy GL (dałby X230 filmy H.264 bez szybkiej
  grafiki): opiera się na wewnętrznym formacie Firefoksa, a robot aktualizuje Firefoksa codziennie — kruche. „Później”.
- Szybka grafika na karcie bez pełnego PPGTT (X230): strefa widziałaby pamięć karty innych kontekstów.
- simpledrm obok i915: i915 i tak go usuwa.

## Sprawdzenia przeniesione (krok 1 bez pomiarów ze sprzętu, decyzja właściciela 2026-11-19)
- Krok 2: pulpit z i915 na X230 (IdeaPad — potwierdzony); lista formatów karty z prawdziwego dekodera (S9 — źródła:
  X230 H.264, MPEG-2, VC-1; IdeaPad H.264, HEVC 8-bit, VP8, bez VP9); bufory w cgroup (S26); lista ioctl (S16).
- Krok 3: pomiar zysku na IdeaPadzie (bramka z pkt 6).
- Krok 5: pełne oddzielenie kontekstów na IdeaPadzie próbą (S38) i zawieszenie karty bez skutków dla pulpitu (S39).

## Skutki
- SPEC: dopisek **[korekta: ADR 0049]** przy „ekran: jedyny proces z DRM/KMS”.
- Skill kons-dev §4 p. 7 i dokumenty komponentów — w krokach, które to wdrażają (2: dekoder filmów, 5: pośrednik).
- Plan 0.10.9: kolejność 1, 4, 2, 5, 6, 3 (wydanie w ostatnim wykonanym kroku), decyzja D9.
- Miejsce A/B: z i915 i sondą `k1.efi` miał 128 264 KiB (zapas ok. 0,7 MiB) — dekoder filmów i Mesa nie zmieszczą się
  bez kroku 4 (obraz przeglądarki poza rdzeniem), dlatego krok 4 idzie przed krokiem 2.
