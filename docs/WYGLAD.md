# Wygląd K1

Decyzje o wyglądzie podejmuje właściciel; ten plik zapisuje ustalenia i ich realizację.

## Ustalenia właściciela (2026-11-06)
- Nowa tapeta i logo (nie ze starego Kons-OS).
- Logo nawiązuje do **niezależności i bezpieczeństwa** systemu.
- Styl całego systemu nawiązuje do **Claude Code**: ciemne, ciepłe tło, terakotowy akcent, kremowe
  elementy, prostota.
- Obsługa (od M4) ma przypominać **macOS / Windows**: pulpit, okna, pasek, myszka — szczegóły
  (pasek na dole czy u góry, menu, dock) do ustalenia z właścicielem przed M4.

## Logo (M2)
Tarcza z **blankami twierdzy** na górze i monogramem **K1**:
- tarcza = bezpieczeństwo, ochrona;
- blanki (zęby muru obronnego) = twierdza, suwerenność, niezależność („Sovereign”);
- monogram K1 — kremowy, gruby, zaokrąglony.
Rysowane w kodzie (`src/grafika/k1_grafika.c`, funkcje `zbuduj_tarcze`, `sd_monogram`), skaluje się do
każdej rozdzielczości (wysokość logo = 30% wysokości ekranu).

## Tapeta (M2)
Ciemny, ciepły gradient promienisty z delikatną poświatą za logo i łukiem „horyzontu planety” w dolnej
części — z rozświetloną krawędzią w kolorze akcentu.

## Kursor
Klasyczna strzałka (jak w macOS/Windows): kremowe wypełnienie, ciemny obrys, lekki cień. Przy wciśniętym
przycisku myszy — wypełnienie w kolorze akcentu.

## Paleta

| Nazwa | Kolor | Gdzie |
|---|---|---|
| tło środek | `#2A231E` | środek tapety |
| tło brzeg | `#0E0C0B` | brzegi tapety, „planeta” |
| akcent (terakota) | `#D9774F` | poświata, horyzont, kursor wciśnięty |
| akcent jasny | `#E8956C` | górna część tarczy |
| akcent ciemny | `#A94F34` | dolna część tarczy, cień monogramu |
| krem | `#F3EDE2` | monogram, obrys tarczy, kursor |

Zrzuty z testów: `build/testy/m2-*.png` (artefakt CI `k1-<wersja>-obraz` / logi).
Podgląd bez QEMU: `make build/obj/podglad_grafiki && build/obj/podglad_grafiki 1920 1080 > tlo.ppm`.
