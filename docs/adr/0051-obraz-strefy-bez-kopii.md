# 0051. Obraz strefy bez kopii: bufor karty prosto do ekranu K1, warstwa karty

- Status: Przyjęty; wdrożony w 0.10.11 (krok 2); uzupełnienie — nowsze karty Intela (krok 3)
- Data: 2026-11-21
- Dotyczy: ADR 0042 (obraz strefy przez virtio-gpu 2D do okna K1), ADR 0049 (szybka grafika: pośrednik, most native context,
  niezmiennik 7), ADR 0044 (pamięć stref); plan `docs/archiwum/PLAN-0.10.11.txt`: krok 2, T3, T4, luki P8–P12, decyzja właściciela D4;
  RYZYKA R100

## Kontekst
Od 0.10.9 przeglądarka z szybką grafiką rysuje stronę kartą, ale gotowa klatka szła do ekranu przez kopie procesorem:
kompozytor strefy (do 0.10.11 krok 1 dwie: bufor karty → swoja kopia → scena), virtio-gpu 2D (`TRANSFER_TO_HOST_2D`: pamięć
gościa → kopia w VMM), VMM → okno u ekranu (pamięć wspólna), ekran → scena → bufor karty. Przy 1366 × 768 to ok. 4 MB na każdą
kopię i klatkę — przycięcia przy przewijaniu i filmach (prośba właściciela z 2026-11-20).

Klatka przeglądarki już jest buforem karty: VMM ma jego dma-buf od pośrednika (`POSR_BUFOR` — mapuje go strefie). Karty
Intela mają poza płaszczyzną pulpitu płaszczyznę nakładki i kursora (warstwy sprzętowe) — mogą pokazać obcy bufor bez kopii.

## Decyzja
1. **Strefa pokazuje bufor karty jako swój wyświetlacz.** Kompozytor strefy, gdy na wierzchu jest bufor karty na cały
   wyświetlacz (okno przeglądarki, zmaksymalizowane jak zawsze w K1, nic nad nim: bez menu, podpowiedzi, podglądu karty),
   nie rysuje sceny: robi z bufora framebuffer i pokazuje go (`SETCRTC`); jądro strefy wysyła `SET_SCANOUT_BLOB`
   (`src/strefa/kompozytor/bezposredni.c`, `gosc/kms.c`). Inaczej — scena jak dotąd. Kompozytor oddaje przeglądarce bufor
   dopiero dwie klatki później (ekran może go jeszcze pokazywać).
2. **VMM nie kopiuje — podaje ekranowi bufor** (`gpu_most.c` czyste, `okno_bufor.c`). `SET_SCANOUT_BLOB` sprawdzany pole po
   polu: zasób to bufor karty tej strefy, XRGB8888 albo ARGB8888, jedna płaszczyzna, cały obraz, wiersz i początek w buforze,
   nie większy niż okno. Dma-buf brany od pośrednika raz na bufor. Do ekranu nowymi komunikatami okna (rola `STREFA`):
   `STREFA_BUFOR` (z deskryptorem: numer 1…`K1_STREFA_BUFORY` = 4, rozmiar, wiersz, początek) — raz na bufor;
   `STREFA_OBRAZ` (numer i zmieniony prostokąt) — każda klatka; `RAMKA` jak po `ZATWIERDZ`. `ZATWIERDZ` wraca do własnych
   pikseli okna. Strefa nigdy nie dostaje deskryptora — ani ekranu, ani karty.
3. **Ekran pokazuje bufor — warstwą karty albo składając go sam** (`src/ekran/strefa_bufor.c`, `warstwa.c`; D4 „A”):
   - bufor sprawdzony (rozmiar przez `lseek`, obraz w nim, nie większy niż ekran), zmapowany tylko do odczytu i tylko część
     z obrazem; przed odczytem i po nim `DMA_BUF_IOCTL_SYNC` (jedyne wywołanie poza DRM w klatce ekranu);
   - okno widać w całości (nic go nie zasłania — inne okno z cieniem, panel, menu, obrys zmiany rozmiaru, ekran
     „Wyłączanie”; obraz cały na ekranie) i karta ma nakładkę (i915) → **warstwa karty**: nakładka pokazuje bufor (import
     dma-buf do węzła card ekranu, `ADDFB2` XRGB8888), kursor na płaszczyźnie kursora (inaczej byłby pod nakładką), jedno
     zatwierdzenie atomowe na klatkę (bufor pulpitu, nakładka, kursor) z tym samym zdarzeniem wymiany co `PAGE_FLIP`;
     nowy bufor najpierw próbą (`TEST_ONLY`); odmowa karty → ten bufor zawsze składany; odrzucone zatwierdzenie → warstwa
     wyłączona do końca (w logu);
   - wszystko inne → ekran składa okno z bufora sam (nieprzezroczyście, jak własne piksele okna) — droga zapasowa, ta sama
     w QEMU (karty QEMU nie mają nakładki).
   Ekran dalej **tylko pokazuje** gotowy bufor: nie dostaje węzła render i nie zleca karcie pracy (niezmiennik 7 bez zmian).
4. **Pamięć i limity (T4):** bufory należą do pośrednika strefy (jego cgroup, ADR 0044); ekran trzyma odwołanie do najwyżej
   `K1_STREFA_BUFORY` buforów na okno — do zastąpienia numeru albo końca okna. Zły bufor albo zła prośba = odmowa i licznik,
   nie koniec strefy ani ekranu.
5. **Bez zmian:** bez szybkiej grafiki (Bank, Prywatna, X230, strefa pobierania) obraz idzie jak dotąd (virtio-gpu 2D →
   okno). Ramkę strefy dalej rysuje K1.

## Odrzucone
- **Ekran składający pulpit kartą** (węzeł render dla ekranu, praca karty w ekranie) — zmiana niezmiennika 7 większa niż
  potrzeba (T3).
- **Zawsze kopia, warstwa tylko przy oknie zmaksymalizowanym** (D4 „B”) — prościej, mniej zysku.
- **Kopia w VMM z mapowania bufora** — dalej kopia procesorem na klatkę; ekran i tak składa z bufora sam, gdy trzeba.

## Skutki
- Przy oknie widocznym w całości: żadnej kopii obrazu strony procesorem; inaczej jedna (ekran) zamiast kilku.
- Ekran mapuje bufor ze strefy (tylko do odczytu; jego zawartość to piksele strefy) — nowa granica: R100.
- Bufor ARGB pokazywany jest nieprzezroczyście (okno strefy zawsze takie jest) — zmaksymalizowane okno przeglądarki nie ma
  przezroczystych pikseli.
- Warstwy karty nie sprawdza CI (QEMU nie ma nakładki) — tylko drogę zapasową (`test-strefa.sh`: strefa próbna z silnikiem
  testowym pośrednika pokazuje bufor jako wyświetlacz, kolor na zrzucie); warstwę — właściciel na IdeaPadzie.
- NIEPEWNE (z wykonania na sprzęcie): czy i915 przyjmie bufory Mesy jako nakładkę (wyrównanie wiersza do 64 B — szerokość
  okna co 8 px nie zawsze je daje; wtedy droga zapasowa), formaty i modyfikatory kafelkowe (krok 3, P10).

## Uzupełnienie (0.10.11 krok 3, 2026-11-21): nowsze karty zintegrowane Intela
Właściciel (2026-11-20): „niech ta wersja też upewni się, że kompatybilność jest z nowszymi kartami Intela
zintegrowanymi”; D5 — bez sprawdzenia na sprzęcie (takiego laptopa nie ma), D6 „A” — bez sterownika xe.
1. **Zakres (T5):** karty zintegrowane sterownika i915 jądra 7.3 bez `force_probe` — od Apollo Lake po Meteor / Arrow
   Lake; tabela generacji: `docs/komponenty/jadro.md`. Lunar Lake i nowsze (xe) — nie: Mesa nie ma dla xe strony strefy
   mostu (P13), a pulpit działa tam przez obraz z firmware.
2. **Pliki od producenta:** DMC dla każdej generacji z tabeli, GuC tam, gdzie i915 zleca pracę tylko przez niego (Alder
   Lake P, Raptor Lake, Meteor / Arrow Lake) — bez niego karta jest „zawieszona” od startu (`intel_uc.c`). HuC i GSC — nie.
3. **Warunek szybkiej grafiki bez zmian (T6):** pełne PPGTT, izolacja kontekstów, przypięte adresy, płoty wyjścia —
   każda generacja sprawdzana przy starcie tak samo jak dotąd; bez wyjątków dla nowych kart.
4. **Pośrednik — tylko to, o co pyta Mesa na nowszych kartach (P16):** rozszerzenie `SET_PAT` przy nowym buforze (Mesa
   dodaje je na Meteor / Arrow Lake: `has_set_pat_uapi`; indeks ≤ 4 — najwyższy w i915, jądro sprawdza go jeszcze wg karty),
   zawsze po „memory regions”, i odczyt `PROTECTED_CONTENT` kontekstu (iris przy zastępowaniu kontekstu po zawieszeniu).
   Zapytania (`DRM_I915_QUERY_*`), parametry i lista ioctl — bez zmian: Mesa z intel-virtio pyta na Gen11/Gen12 o te same
   (sprawdzone w `src/intel/dev/virtio`, `src/intel/common/i915`, `src/gallium/drivers/iris/i915`). `PROTECTED_CONTENT`
   przy nowym buforze albo kontekście — dalej odmowa.
5. **Warstwa karty:** bez zmian w kodzie — płaszczyzna wybierana po typie i formacie, nowy bufor próbą (`TEST_ONLY`).
6. **Skutki:** k1.efi +1,3 MiB; więcej generacji i915 w zasięgu strefy (R100), Mesa intel-virtio na Gen12 (R101);
   niesprawdzone na sprzęcie — wersję z czarnym ekranem przy aktualizacji cofa zatwierdzanie A/B (od pierwszej klatki),
   przy nowej instalacji z pendrive'a tej ochrony nie ma (P20).
