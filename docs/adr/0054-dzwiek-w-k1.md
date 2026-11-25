# 0054. Dźwięk w K1: usługa dźwięku z wyłącznym prawem do karty, ALSA bez alsa-lib, kanał z pierścieniem

- Status: Przyjęty, krok 1 wdrożony (0.12.0, krok 1)
- Data: 2026-11-25
- Dotyczy: SPEC §5 (usługi), §7 (klatki), niezmiennik 7 skilla (jeden właściciel zasobu — **rozszerza go o kartę
  dźwiękową**), ADR 0007 (klatki), ADR 0053 (0.12 = Dźwięk); plan `docs/PLAN-0.12.0.txt` (T1–T4, T7, T8, D1–D3);
  opis: `docs/komponenty/dzwiek.md`

## Kontekst
Do 0.11 K1 nie grał wcale: jądro bez sterowników dźwięku, na górnym pasku ikona głośnika z napisem „ta wersja nie
obsługuje jeszcze karty dźwiękowej”. Wersja 0.12 ma grać: najpierw dźwięk samego K1 (próba w okienku „Dźwięk”), potem
klawisze i słuchawki (krok 2), a na końcu dźwięk z przeglądarki w strefie (krok 3). Karta dźwiękowa to urządzenie jądra
z dużym sterownikiem (HD Audio, kodeki) — tak jak karta graficzna nie może być w rękach każdego programu.

Właściciel, 2026-11-25: **D1 = A** (mikrofonu w 0.12 nie ma — „Później”), **D2 = B** (okienko „Dźwięk” z suwakiem,
wyciszeniem, nazwą wyjścia, „Odtwórz próbę”, a pod spodem lista sesji, które grają — lista w kroku 3), **D3 = A** (K1
nie wydaje własnych dźwięków; jedyny to „Odtwórz próbę”).

## Decyzja
1. **Usługa dźwięku `dzwiek` (T1)** — wiersz `K1_SYSTEM` (profil 24, UID 111, przy starcie, bez okna), klatka
   `KLATKA_DZWIEK`. To jedyny proces z węzłami `/dev/snd`: nadzorca znajduje pierwszą kartę z odtwarzaniem (sysfs
   `class/sound`, zdarzenie jądra „sound”), otwiera jej węzeł odtwarzania (`pcmC<k>D<u>p`) i kontrolki (`controlC<k>`)
   i przekazuje je usłudze (`DZWIEK_KARTA`); swoje kopie zamyka. **Niezmiennik 7 rozszerzony:** węzłów karty dźwiękowej
   nie dostaje nikt poza usługą dźwięku (ekran, aplikacje, VMM stref — nie). Bez karty 5 s po starcie: `DZWIEK_BEZ_KARTY`.
   Karta, która przyjdzie później niż pierwsza (USB), jest pomijana (punkt 6 planu). Profil 24 to pierwszy wolny przed
   dekoderami filmów i pośrednikami stref — ich numery przesunęły się o jeden (FILMY 25, POSREDNIK 31; nie mają danych
   w skarbcu, więc to bezpieczne).
2. **ALSA bez alsa-lib (T2)** — ioctl z nagłówka UAPI `sound/asound.h`, jak DRM bez libdrm: na węźle odtwarzania INFO,
   HW_PARAMS, SW_PARAMS, PREPARE, WRITEI_FRAMES, DROP, HW_FREE; na kontrolkach CARD_INFO, ELEM_LIST, ELEM_INFO, ELEM_READ,
   ELEM_WRITE, TLV_READ. Klatka dopuszcza dokładnie te numery (nie cały typ `'A'`/`'U'`): bez ELEM_ADD/REPLACE/REMOVE,
   TLV_WRITE, POWER, SYNC_PTR (mapowania stron stanu) i LINK. alsa-lib czyta pliki konfiguracji i ładuje wtyczki —
   w klatce bez ścieżek i tak by nie działała.
3. **Jeden format (T3):** 48 kHz, 16 bitów ze znakiem, stereo; okres 1024 ramek, 4 okresy w buforze karty. Mieszanie
   strumieni klientów (suma 32-bitowa, obcięcie do 16 bitów) i **głośność programowa** w usłudze: krzywa sześcienna
   (50 % ≈ −18 dB), wzmocnienie z dokładnością 1/2²⁴ (każdy krok suwaka głośniejszy od poprzedniego). Sprzętowe wyjścia
   kodeka („Master”, „PCM”, „Speaker”, „Headphone”, „Front”, „Line Out”, „Bass Speaker” — po nazwach parsera HDA) usługa
   włącza i ustawia na 0 dB (bez wzmacniania) z ich skali dB (TLV); wejść, pętli mikrofonu i „beep” nie dotyka.
   Do sprawdzenia na X230 (krok 2): szum przy małej głośności.
4. **Kanał klientów (T4):** nadzorca robi parę gniazd na klienta z tabeli `K1_DZW_KLIENCI` (`k1_dzwiek.h`: gui,
   program testowy; krok 3 dopisze strefy), daje usłudze jej koniec z wierszem klienta i jego UID (`DZWIEK_KLIENT`),
   klientowi drugi (gui: `DZWIEK_KANAL` na fd 5 — od nowa po restarcie usługi albo gui). Oba końce sprawdzają nadawcę.
   Klient gra jeden strumień: **pierścień** — memfd dokładnie `K1_DZW_PIERSCIEN_BAJTY` (16384 ramek, 0,34 s) z pieczęciami
   przeciw zmniejszeniu i powiększeniu (`DZWIEK_OTWORZ`), usługa mapuje go tylko do odczytu; po każdym zapisie klient podaje
   **liczbę nowych ramek** (`DZWIEK_ZAPISANO`), nigdy indeks ani pozycję. Usługa liczy sama (`k1_dzw_zapisano`): więcej, niż
   mieści pierścień, albo ramki bez strumienia — klient traci kanał. Ramki są **kopiowane** z pierścienia przed mieszaniem
   (klient może pisać w tym czasie — wtedy gra tylko inny szum). Postęp wraca co 4096 ramek (`DZWIEK_ODTWORZONO`).
   Klient sterujący (gui, program testowy) zmienia głośność całego K1 (`DZWIEK_GLOSNOSC`), każdy klient słyszy stan
   (`DZWIEK_STAN`). Wszystkie komunikaty sprawdzane pole po polu (`k1_kom_dzwiek.c`), test i fuzzing na hoście.
5. **Energia (T8):** bez strumienia przez 5 s usługa zatrzymuje kartę i zwalnia jej bufor (DROP + HW_FREE). Usypianie
   kodeka (`power_save`) wymaga w jądrze `PM`, którego K1 nie ma — włączenie PM to osobna decyzja z pomiarem w kroku 2.
6. **Jądro (L1):** `SOUND`, `SND`, `SND_PCI`, `SND_HDA_INTEL`, `SND_HDA_GENERIC`, `SND_HDA_CODEC_REALTEK` + `ALC269`,
   `SND_HDA_CODEC_CONEXANT`; wprost wyłączone opcje „default y” (OSS, timery ALSA, `/proc/asound`, stare ioctl, USB,
   FireWire, SOF/AVS, HDMI, reszta rodzin Realteka, hwdep, beep, `INPUT_LEDS`), żeby robot (ADR 0047) nie dostał pytań —
   `scripts/porownaj-jadro.sh`: 0 pytań. `SND_HDA_I915` włącza sam sterownik i915 (wiązanie z kartą graficzną na nowszych
   Intelach).
7. **Testy (T7):** QEMU z `ich9-intel-hda` i `hda-output` z `-audiodev wav` zapisuje zagrany dźwięk do pliku;
   `scripts/test-dzwiek.sh` klika okienko po nazwach i słucha pliku (częstotliwości obu nut, cisza po wyciszeniu, stosunek
   głośności 50 % do 100 %, gra po restarcie usługi). Test w systemie `t_dzwiek` (bez karty): klatka, stan „brak karty”,
   wrogi klient, restart usługi. Prawdziwy głośnik — właściciel na X230 i IdeaPadzie.

## Konsekwencje
- Nowa powierzchnia ataku: sterownik HD Audio w jądrze, osiągalny tylko z usługi dźwięku (klatka, lista ioctl) —
  RYZYKA R107. Klienci widzą tylko pierścień i komunikaty usługi.
- Krok 2 (klawisze, słuchawki, Ustawienia → Dźwięk) i krok 3 (urządzenie virtio-snd w VMM strefy, klient „strefa” w tabeli
  `K1_DZW_KLIENCI`, lista „Grają teraz” z D2 = B) uzupełnią ten ADR.
- Okienko „Dźwięk” i ikona głośnika (wyciszony / cicho / głośno / bez karty) — `docs/WYGLAD.md`, `gui.md`.

## Rozważane alternatywy
- **Dźwięk w nadzorcy** — root bez klatki z dużym sterownikiem za ioctl (R23); odrzucone (T1).
- **Węzły karty dla każdej aplikacji (jak w zwykłych systemach)** — łamie niezmiennik 7; strefa dostałaby sterownik jądra
  K1 na wyciągnięcie ręki.
- **alsa-lib** — pliki konfiguracji, wtyczki ładowane `dlopen`, ścieżki; nie pasuje do klatki bez ścieżek (T2).
- **Mapowanie stron stanu PCM (mmap)** zamiast zapisu ramek — mniej opóźnienia, ale szersza lista ioctl i stron jądra
  w procesie; zapis okresami (21 ms) wystarcza na próbę i przeglądarkę.
- **Pozycje zapisu od klienta** (wspólny nagłówek w pierścieniu) — każda pozycja od klienta to liczba, której trzeba
  nie ufać; liczenie ramek w usłudze jest prostsze i nie ma czego sprawdzać.
