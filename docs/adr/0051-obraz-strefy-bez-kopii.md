# 0051. Obraz strefy bez kopii: bufor karty prosto do ekranu K1, warstwa karty

- Status: Przyjęty; wdrożony w 0.10.11 (krok 2)
- Data: 2026-11-21
- Dotyczy: ADR 0042 (obraz strefy przez virtio-gpu 2D do okna K1), ADR 0049 (szybka grafika: pośrednik, most native context,
  niezmiennik 7), ADR 0044 (pamięć stref); plan `docs/PLAN-0.10.11.txt`: krok 2, T3, T4, luki P8–P12, decyzja właściciela D4;
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
