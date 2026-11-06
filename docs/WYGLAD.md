# Wygląd K1

Decyzje o wyglądzie podejmuje właściciel; ten plik zapisuje ustalenia i ich realizację.
Zrzuty z testów: `build/testy/m2-*.png`, `build/testy/m4-*.png` (artefakt CI `k1-<wersja>-logi`).
Podgląd tapety bez QEMU: `make build/obj/podglad_grafiki && build/obj/podglad_grafiki 1920 1080 -1 -1 0 WARIANT > tlo.ppm`
(PPM → PNG: `python3 -I scripts/ppm-na-png.py tlo.ppm tlo.png`).

## Ustalenia właściciela
- 2026-11-06 (M2): nowa tapeta i logo; logo nawiązuje do **niezależności i bezpieczeństwa**; styl
  jak **Claude Code** (ciemne, ciepłe tło, terakota, krem, prostota).
- 2026-11-06 (przed M4): **główny pasek na dole, wyśrodkowany jak w Windows 11**; na górze dźwięk,
  (w przyszłości) bateria i zegar. Cały interfejs **minimalistyczno-ciepły, w stylu Claude Code
  i Anthropic**. Aplikacje: Zegar, Notatki, **Ustawienia**.
- 2026-11-06 (M4, po zrzutach): tapeta **na wzór zdjęcia** właściciela (warstwy wygiętych
  „wstęg”), może być w kolorach systemu, **kilka wariantów**; **logo zminimalizowane**, eleganckie.
- 2026-11-06 (po pierwszym teście w VirtualBoxie): płaska tarcza „okropna — przebudować od zera”;
  z czterech kierunków (Pieczęć, Brama, Warstwy, Baszta) właściciel wybrał **C — Warstwy**.
  Rozmiary pulpitu: „bardzo fajne”.

## Kierunek (M4)
- **Charakter:** spokojny, rzeczowy, lekko ciepły.
- **Świat:** papier i glina — kremowy papier okien, terakota, ciemny brąz wieczoru.
- **Bogate miejsca:** tapeta (warstwy papieru) i tarcza Zegara; reszta interfejsu cicha: neutralne
  tła, włoskowate linie, jeden akcent.
- **Znak rozpoznawczy:** znak „Warstwy” — przycisk Start i nagłówek menu.

## Znak K1 (od 0.4.2): „Warstwy”
Trzy arkusze papieru leżące jeden na drugim — ta sama myśl co tapeta: warstwy ochrony, każda
osobno. Ćwiartki koła wychodzące z prawego dolnego rogu zaokrąglonego kwadratu, w trzech odcieniach
terakoty (`#B55C3E`, `#D97757`, `#E9A88B`), płasko, bez obrysu i poświaty. Rysowany w kodzie
(`k1_rysuj_znak` w `src/grafika/k1_grafika.c`) w każdym rozmiarze; czytelny od 20 px.
Wcześniej: tarcza z blankami i monogramem K1 (M2–0.4.1) — porzucona na prośbę właściciela.
Szkice kierunków: `build/podglad/logo-kierunki.png` (generowane skryptem w sesji, nie w repo).

## Tapety (od M4)
Warstwy długich, pochylonych arkuszy papieru: krawędź każdego schodzi z góry, zakręca i wychodzi
w prawym dolnym rogu; tam, gdzie krawędź jest zwrócona do patrzącego, widać ciepło oświetloną
ściankę i miękki cień na arkuszu niżej; powierzchnie odbijają „niebo” z delikatnymi chmurami.
Liczone w kodzie (`k1_rysuj_tlo`), w połowie rozdzielczości i powiększane, z ditheringiem.
Wybór: **Ustawienia → Wygląd → Tapeta** (miniatury z tego samego kodu).

| Nr | Nazwa | Kolory |
|---|---|---|
| 0 | **Glina** (domyślna) | kolory systemu: terakota i krem na ciemnym brązie |
| 1 | Zmierzch | jak zdjęcie właściciela: fiolet, błękit, róż, złoto |
| 2 | Noc | indygo i morski |
| 3 | Świt | jasna: brzoskwinia i lawenda |

## Pulpit (M4)
- **Górny pasek** (32 px, cała szerokość): przyciemnienie tapety (czarny 59%), kremowy tekst
  IBM Plex Sans Medium 13: z lewej nazwa aktywnej aplikacji (albo „Kons K1”), z prawej ikona
  głośnika i data z godziną („pt. 6 lis   19:42”; sekundy — w Ustawieniach).
- **Dolny pasek** (unosi się 8 px nad krawędzią, zaokrąglenie 16, tło okien 94%, włoskowaty obrys,
  miękki cień rzucany 4 px w dół, tylko poza panelem — od 0.5.0 bez „podstawy” pod paskiem): Start (znak K1 „Warstwy”), separator, ikony aplikacji 24 px w polach 44 px. Uruchomiona —
  kropka pod ikoną, aktywna — kreska 16×3 w kolorze akcentu.
- **Menu Start** (360 px): znak + „Kons K1” (Plex Serif 24) i wersja; wiersze aplikacji 56 px
  (ikona, nazwa, opis); stopka: „Uruchom ponownie”, „Wyłącz”.
- **Okna:** zaokrąglenie 12, pasek tytułu 44 px w kolorze okna (tytuł Plex Sans Medium 15,
  przyciski minimalizuj i zamknij po prawej), włoskowaty obrys, miękki cień rzucany przez ekran
  (mocniejszy dla aktywnego okna).

## Zasoby (M4.1)
Okno 560×520: „Procesor” i „Pamięć” — nazwa, liczba po prawej (Plex Sans Medium 15), pod spodem
wykres z ostatniej minuty na karcie (`pole`, zaokrąglenie 8, włoskowaty obrys): jedna seria w kolorze
akcentu (linia 2 px, delikatne wypełnienie pod nią), linia pomocnicza na 50%, najnowsza próbka przy
prawej krawędzi. Niżej tabela usług (procesor, pamięć) tekstem drugorzędnym. Bez drugiej osi
i bez kolorów spoza motywu.

## Ustawienia → Ekran (M4.1)
Rozdzielczości jako przyciski w trzech kolumnach (136×32): wybrana — wypełnienie akcentem, pozostałe
— `pole` z obrysem; pod listą „Zalecana: …” (tryb preferowany przez monitor/kartę). Przy jednym trybie (obraz z firmware) — wyjaśnienie
i wskazówka dla VirtualBoxa.

## Motywy okien

| Rola | Jasny (domyślny) | Ciemny |
|---|---|---|
| tło okna | `#FAF9F6` | `#262523` |
| pole / karta | `#FFFFFF` | `#302F2C` |
| linia (włoskowata) | tekst 9–10% | tekst 9–10% |
| najechanie / zaznaczenie | `#F0EEE9` / `#EBE8E1` | `#32312E` / `#3A3834` |
| tekst: główny / drugorzędny / wyciszony | `#1F1E1C` / `#5E5C57` / `#8D8A84` | `#EEEBE5` / `#ABA79F` / `#7D7A73` |
| akcent (wypełnienie) / tekst na nim | `#BC5A37` / biały (kontrast 4,5:1) | `#D97757` / `#1F1E1C` |

## Typografia
IBM Plex Sans (13, 13 Medium, 15, 15 Medium) i IBM Plex Serif (24 — nagłówki; Light 64 — cyfry
zegara). Hierarchia rozmiarem i kolorem, nie grubością. ADR 0009.

## Ikony
Liniowe, jedna grubość (1,6 przy 24 px), kolor roli tekstu, rysowane w kodzie (`k1_ikona`):
zegar, notatki, ustawienia (suwaki), głośnik, zasilanie, restart, zamknij, minimalizuj, kłódka,
informacja, mysz, wygląd (obrazek), zasoby (ramka z pulsem), ekran (monitor).

## Kursor
Klasyczna strzałka: kremowe wypełnienie, ciemny obrys, lekki cień; przy wciśniętym przycisku —
wypełnienie w kolorze akcentu.
