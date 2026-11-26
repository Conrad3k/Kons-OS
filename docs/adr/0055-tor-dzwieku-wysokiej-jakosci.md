# 0055. Tor dźwięku wysokiej jakości: liczby zmiennoprzecinkowe w usłudze, ogranicznik, dither, 24 bity do karty, formaty strefy

- Status: Przyjęty, krok 1 wdrożony (plan 0.12.3); kroki 2–4 (korektor, Kons Spatial Audio) dopiszą swoje punkty
- Data: 2026-11-26
- Dotyczy: ADR 0054 pkt 3 (jeden format i głośność w usłudze — **zastąpiony** punktami 1–5 niżej), pkt 13–16 (dźwięk
  stref — formaty gościa, punkt 6); plan `docs/PLAN-0.12.3.txt` (krok 1, T1–T5, T8); opisy: `docs/komponenty/dzwiek.md`,
  `docs/komponenty/strefa.md`

## Kontekst
Po 0.12.0 tor dźwięku miał 16 bitów od początku do końca: klienci oddawali liczby całkowite, usługa sumowała je w 32
bitach, a głośność K1 i głośność sesji liczyła mnożeniem i dzieleniem całkowitym, które ucina w stronę zera. Pomiar
„przed” (krok 1a planu; ton 1 kHz o połowie pełnej skali, jeden strumień, karta 16-bitowa):

| głośność | błąd (kroki 16 bitów) | harmoniczne pod tonem |
|---|---|---|
| 100 % | 0 | — (bez zmian) |
| 50 % | 0,46 | 79 dB |
| 10 % | 0,53 | 35 dB |
| 5 % | 0,58 | 14 dB |

Błąd ucinania nie jest szumem, tylko zniekształceniem związanym z dźwiękiem (harmoniczne): przy 10 % ledwo 35 dB pod
tonem, przy 5 % — 14 dB, czyli wyraźne „ziarno” cichej muzyki. Ton o amplitudzie 3 kroków przy 50 % znikał całkiem
(wyjście: same zera). Głośne nakładające się strumienie były twardo przycinane (trzaski). Przeglądarka w strefie
przeliczała swój dźwięk (Firefox liczy w liczbach zmiennoprzecinkowych) do 16 bitów już w strefie, w alsa-lib — bez
dithera.

## Decyzja
1. **Liczby zmiennoprzecinkowe w środku (T2).** Usługa dźwięku liczy próbkę jako `float` (32 bity; ±1,0 = pełna skala):
   suma strumieni, głośność sesji, głośność K1, ogranicznik. `float` ma 24 bity dokładności przy każdej głośności, więc
   cichy dźwięk nie traci bitów aż do karty, a suma może wyjść ponad pełną skalę bez przepełnienia. Liczby całkowite
   16 bitów przechodzą do `float` dokładnie (dzielenie przez 2¹⁵), suma do 12 klientów też jest dokładna. Bez libm
   i bez `-ffast-math` — te same wyniki na hoście, w fuzzerze i w K1. Czyste funkcje: `src/wspolne/k1_dzw_tor.c`.
   Krzywa głośności bez zmian (sześcian; 50 % to dokładnie 0,125).
2. **Ogranicznik zamiast przycinania.** Gdzie ramka wyszłaby ponad pełną skalę, wzmocnienie obu kanałów schodzi do
   potrzebnego — łagodnie, zaczynając do 1 ms (48 ramek) przed głośną ramką w tym samym okresie (usługa ma cały okres,
   zanim zapisze kartę, więc to nie dodaje opóźnienia) — i wraca do 1,0 powoli (6 dB na 50 ms). Poniżej pełnej skali
   nic nie zmienia. W logu raz na minutę (gdy coś było): w ilu okresach działał.
3. **Dither TPDF przy zamianie na format karty (T3).** Próbka jest zaokrąglana do kroku karty z szumem o rozkładzie
   trójkątnym (±1 krok; dwa losowania z generatora xorshift32 — deterministyczny, te same liczby przy każdym
   uruchomieniu). Błąd staje się białym szumem bez związku z dźwiękiem, o stałej mocy ¼ kroku² (−96 dBFS przy 16
   bitach, −144 dBFS przy 24) — niezależnie od głośności. **Bez dithera w okresie, w którym każda próbka już jest
   pełnym krokiem karty:** jeden strumień 16 bitów przy 100 % idzie do karty 16-bitowej próbka w próbkę
   („przezroczystość”), a cisza zostaje cyfrową ciszą (także wyciszenie). Kształtowania szumu (noise shaping) nie ma
   — „Później”, jeśli odsłuch pokaże potrzebę.
4. **Format karty: najlepszy, jaki przyjmie (T1).** Usługa próbuje po kolei (tabela w `src/dzwiek/karta.c`):
   S32_LE z podformatem MSBITS_24 (HD Audio z przetwornikiem 24-bitowym), S32_LE z MSBITS_20, S32_LE bez podformatu
   (inny sterownik: jego `msbits`, najwyżej 24), S24_3LE, na końcu S16_LE jak dotąd. HD Audio podaje 20 i 24 bity
   tylko jako S32_LE z podformatem (`sound/hda/core/device.c`, `snd_pcm_hw_rule_subformats` w `sound/core/pcm_native.c`),
   więc pytanie o podformat mówi, ile bitów karta naprawdę gra — i do tylu bitów usługa zaokrągla z ditherem. Odmowę
   HW_PARAMS daje sam ioctl — bez nowego numeru w klatce. Log: „karta gra 24 bity (4 bajty na próbkę)”. Karta QEMU
   (`hda-output`) ogłasza tylko 16 bitów: QEMU testuje ścieżkę 16-bitową, a 24 bity — test na hoście i sprzęt
   właściciela (NIEPEWNE do pierwszego logu z X230 i IdeaPada: czy ich kodeki Realteka dają 24 bity).
5. **Głośność dalej cała w usłudze (T8).** Wzmacniacze kodeka zostają na 0 dB (ADR 0054 pkt 3). Pomiar „po”: z ditherem
   błąd przy 5 % to wciąż −96 dBFS szumu (16 bitów; −144 dBFS przy 24), harmoniczne 45 dB pod tonem przy 16 bitach
   i 94 dB przy 24 (było 14 dB), a ton 3 kroków przy 50 % wychodzi z wzmocnieniem 0,124 (idealnie 0,125; było 0).
   Przesuwanie części ściszenia do kodeka zmniejszyłoby też szum analogowy wzmacniacza, ale kroki wzmacniaczy różnią się
   między kodekami (0,75–1,5 dB), a zmiana w trakcie grania daje słyszalne stuknięcia — bez potrzeby, którą pokazałby
   odsłuch, to ryzyko się nie opłaca. Jeśli właściciel usłyszy szum przy małej głośności na X230 — „Później”.
6. **Formaty klientów i strefy (T4).** Pierścień klienta niesie jeden format z tabeli `K1_DZW_FORMATY` (`k1_dzwiek.h`):
   **S16** (gui, Ustawienia, program testowy — jak w 0.12.0) albo **F32** (VMM strefy; 8 bajtów ramka, pierścień 128 KiB);
   format jest w `DZWIEK_OTWORZ` a, rozmiar pierścienia musi się zgadzać. Próbki F32 to liczby od wrogiego gościa:
   usługa sprawdza każdą — liczba nieskończona albo NaN staje się zerem, ponad ±1,0 — ±1,0 — i liczy je (log raz na
   minutę). Strumień przy tym gra dalej: zepsuta liczba to najwyżej cisza tej próbki, a zamknięcie strumienia
   wyciszyłoby całą przeglądarkę za jedną złą stronę. Karta virtio-snd strefy ogłasza **S16, S32 i FLOAT** (tabela
   `DZW_FORMATY`, `src/strefa/dzwiek.h`); VMM przelicza je do pierścienia F32 bez straty: S16 dokładnie, S32 do 24 bitów
   `float` (tyle gra najlepsza karta), FLOAT kopiuje (sprawdza usługa). Jeden format pierścienia na VMM — strumień nie
   musi się zamykać i otwierać od nowa, gdy gość zmieni format. Log VMM: „gość gra w formacie S32” (raz na format).
   Firefox gra przez alsa-lib z jej konfiguracją domyślną; który format wybierze (`plug` przepuści FLOAT, `dmix` miesza
   w S32, gdy karta go ma) — NIEPEWNE, pokaże log na stanowisku i na sprzęcie. W obu wypadkach nie przelicza już do 16
   bitów.
7. **Częstotliwość: 48 kHz w całym torze (T5).** 44,1 kHz przelicza Firefox (jego jakość — NIEPEWNE, bez preferencji
   do zmiany w `k1.js`); 96/192 kHz — bez korzyści słyszalnej, większy koszt na X230 → „Później”.
8. **Opóźnienie (krok 1h).** Transfer wraca do gościa, gdy usługa zabrała jego ramki do okresu (ADR 0054 pkt 15), więc
   zegar przeglądarki nie widzi zapasu VMM-a (do 8192 ramek, ok. 170 ms) — ten zapas nie przesuwa obrazu względem
   dźwięku i zostaje bez zmian. Przesuwa go bufor karty: od zabrania ramek do głośnika mijają 2–4 okresy (ok. 43–85 ms),
   o tyle dźwięk jest później niż obraz — w granicach, których ucho zwykle nie zauważa przy filmie (ok. 100 ms), ale
   blisko zaleceń telewizji. Liczenie opóźnienia karty w `DZWIEK_ODTWORZONO` (np. `SNDRV_PCM_IOCTL_DELAY` — nowy numer
   w klatce) → „Później”, jeśli właściciel zobaczy rozjazd ust i głosu.

## Skutki
- Cichy dźwięk bez „ziarna”, głośne nakładające się dźwięki bez trzasków; na kartach 24-bitowych szum toru 48 dB niżej.
- Jeden strumień 16 bitów przy 100 % dalej przechodzi bez żadnej zmiany — pilnuje tego `test-dzwiek.sh` (plik WAV od QEMU
  porównany z próbą próbka w próbkę) i `test_dzw_tor`.
- Pierścień strefy jest dwa razy większy (128 KiB); pamięć klatki usługi bez zmian (16 MiB).
- Nowy format od strefy to nowa powierzchnia ataku: liczby zmiennoprzecinkowe od gościa (RYZYKA R77, R108):
  `test_dzw_tor`, `test_virtio_dzwiek`, `fuzz_dzwiek`, `fuzz_virtio_dzwiek`.
- Testy: `test_dzw_tor` (pomiary z tego ADR jako granice), `test_dzwiek` (formaty pierścienia, `DZWIEK_OTWORZ` a),
  `test_virtio_dzwiek` (formaty gościa, próbki do `float`), `t_dzwiek` (pierścień F32: zły rozmiar odrzucony),
  `test-dzwiek.sh` (karta QEMU — 16 bitów, przezroczystość, ton strefy w S32).
