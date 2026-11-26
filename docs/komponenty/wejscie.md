# Komponent: wejscie (`/rdzen/system/wejscie`)

| | |
|---|---|
| **Status** | PRZETESTOWANE (M2, M3) — `scripts/test-m4.sh` (mysz i klawiatura przez monitor QEMU; do 0.10.0 `test-m2.sh`), `scripts/test-m5.sh` (USB), klatka: testy w systemie. Kółko (0.10.0 krok 4): PRZETESTOWANE w QEMU (`test-pendrive.sh`, `test-strefa.sh`); touchpad: ZAIMPLEMENTOWANE, test na hoście (`test_touchpad`) — QEMU nie udaje touchpada, sprawdza właściciel na X230 i IdeaPadzie |
| **Kod** | `src/wejscie/main.c` (urządzenia, klawiatura, mysz, kółko), `src/wejscie/touchpad.c` (touchpad — czysty, bez wywołań systemu), protokół `src/wspolne/k1_zdarzenie.h` |
| **Decyzje** | ADR 0006, ADR 0007 (klatka, kanał) |

## Cel
Czyta klawiatury, myszy i touchpady (evdev) i przekazuje zdarzenia do `ekran`. Nie wie nic o obrazie.

## Przewijanie (0.10.0 krok 4)
- **Kółko myszy:** `REL_WHEEL` / `REL_HWHEEL`, a gdy urządzenie je ma — dokładniejsze `REL_WHEEL_HI_RES` /
  `REL_HWHEEL_HI_RES` (wtedy zwykłe się pomija). Jednostka K1: 1/120 ząbka (`K1_ZD_ZABEK`), jak w jądrze. Kierunek K1:
  `dy` > 0 = dalej w dół (kółko do siebie), `dx` > 0 = w prawo. Zdarzenie `K1_ZD_PRZEWIJANIE`, źródło `K1_ZD_KOLKO`.
- **Touchpad** (jądro z protokołami Synaptics, Elantech, ALPS — `docs/komponenty/jadro.md`): urządzenie z `ABS_X`,
  `ABS_Y`, `BTN_TOUCH` i `BTN_TOOL_FINGER`, nie ekran dotykowy (`INPUT_PROP_DIRECT`). `touchpad.c` zamienia palce na
  zdarzenia K1, tak jak robią to inne systemy:
  - **jeden palec** — ruch kursora (wolny palec: 4 px/mm — dokładnie, szybki: do 16 px/mm; dalej działa prędkość z
    Ustawienia → Mysz);
  - **dwa palce** — przewijanie, po 1,5 mm ruchu (żeby stuknięcie dwoma palcami nie przewijało); kierunek
    **naturalny** (decyzja właściciela D8, 2026-11-16): treść jedzie za palcami, jak w telefonie; 8 mm = jeden ząbek;
    ruch głównie w pionie nie przewija w bok (i odwrotnie); źródło `K1_ZD_TOUCHPAD`;
  - **stuknięcie** (palce w dół i w górę w 180 ms, ruch ≤ 2 mm) — kliknięcie: jeden palec lewy, dwa prawy, trzy
    środkowy; nie przez 400 ms po klawiszu (dłoń na touchpadzie przy pisaniu);
  - **przycisk touchpada** — touchpad, który cały jest przyciskiem (`INPUT_PROP_BUTTONPAD`, np. X230): wciśnięcie
    w prawym dolnym rogu (dolne 20 %, prawa połowa) albo dwoma palcami = prawy przycisk, gdzie indziej lewy; puszczenie
    puszcza ten sam. Touchpad z osobnymi przyciskami — przyciski jak są. Z wciśniętym przyciskiem palec przesuwa kursor
    (przeciąganie).
  Bez bezwładności (przewijanie kończy się z palcami) — „bez przesady” (plan 0.10.0 krok 4).
- Log: każde urządzenie z rodzajem, nazwą i identyfikatorami (`urządzenie 2: touchpad (cały jest przyciskiem)
  „SynPS/2 Synaptics TouchPad” (0011:0002:0007)`) — po nim widać, jaki touchpad ma laptop (S29); nazwa tylko z
  drukowalnych znaków ASCII (urządzenie USB nazywa się samo).

## Granica zaufania
Dane z urządzeń (jądro) są tłumaczone na komunikaty K1 z ograniczonymi zakresami (ruch dzielony na
porcje ≤ 1024 px, akumulator ograniczony do ±100 000). Odbiorca i tak sprawdza każdy komunikat.

## Uprawnienia
**M3 (ADR 0007):** UID/GID 100, bez capabilities, `NO_NEW_PRIVS`, seccomp (baza + `ioctl` tylko typu
`'E'`), Landlock bez żadnej ścieżki, cgroup `wejscie` (8 MiB, 1 proces). Urządzenia `event*` otwiera
nadzorca i przekazuje jako fd 4, 5, … — wejscie samo niczego nie otwiera.

## Wejścia i wyjścia
- Wejście: deskryptory 4… od nadzorcy (`event0..31`) — zostają urządzenia z klawiszami liter
  (klawiatura), osiami REL_X/REL_Y (mysz) albo touchpady, przejmowane na wyłączność (`EVIOCGRAB`); inne są zamykane.
  Od 0.12.0 krok 2 zostaje też urządzenie z samymi **klawiszami głośności** (`KEY_VOLUMEUP` i `KEY_VOLUMEDOWN`, np.
  „extra buttons” laptopa albo „Consumer Control” klawiatury USB) — z niego idą dalej tylko klawisze głośności i wyciszenia.
  Log mówi, które urządzenie je ma („klawiatura (z klawiszami głośności)”, „klawisze głośności”) — nigdy wciśnięcia.
  Ekran daje te klawisze tylko powłoce (`ekran.md`, „Pigułka głośności”). X230 i IdeaPad: klawisze głośności idą
  przez klawiaturę (atkbd) — do sprawdzenia w logu na sprzęcie; sterowników platformy (thinkpad_acpi, ideapad-laptop)
  jądro K1 nie ma.
- Wyjście: fd 3 — kanał `SOCK_SEQPACKET` do ekranu (ekran sprawdza, że nadawca ma UID 100):
  `K1_ZD_KLAWISZ`, `K1_ZD_RUCH`, `K1_ZD_PRZYCISK`, `K1_ZD_PRZEWIJANIE` (0.10.0: `kod` = źródło, `dx`, `dy` w 1/120
  ząbka, |d| ≤ `K1_ZD_PRZEWIN_MAX`, nie oba 0). Gdy ekran nie nadąża — zdarzenia są pomijane
  i liczone (nie blokuje się). Zamknięcie kanału przez ekran → koniec (nadzorca uruchomi ponownie).
- Log: wykryte urządzenia, liczba pominiętych zdarzeń. **Nigdy kody klawiszy.**

## Zależności
musl (statycznie); jądro: evdev, i8042, atkbd, psmouse (od 0.10.0 z protokołami Synaptics, Elantech, ALPS).

## Wywołania systemowe
`ioctl` (EVIOCGBIT, EVIOCGRAB; od 0.10.0 też EVIOCGPROP, EVIOCGABS, EVIOCGNAME, EVIOCGID — wszystkie typu `'E'`), `poll`, `read`, `sendmsg`, `write` (log), `fcntl`, `close` — w ramach
filtra seccomp (ADR 0007).

## Obsługa błędów
Brak urządzeń → komunikat, działa dalej (bez zdarzeń). Odłączone urządzenie → pomijane. Błąd zapisu
do ekranu inny niż „pełny kanał” → `k1_koniec()`.

## Powierzchnia ataku
Struktury `input_event` od jądra (stały rozmiar), ioctl evdev. Brak parsowania danych użytkownika. Położenia palców
spoza zakresu urządzenia są przycinane, akumulatory ruchu i kółka ograniczone; każde wysłane zdarzenie mieści się
w granicach protokołu (`test_touchpad`: także wartości wrogie).

## Ograniczenia
Hotplug przez nadzorcę: nowe urządzenie = ponowne uruchomienie `wejscie` z pełnym zestawem (od 0.6.0 od razu po
zdarzeniu jądra);
odłączone urządzenie `wejscie` przestaje czytać (log „urządzenie odłączone”). Od M5a klawiatury
i myszy USB (HID generic) — te same zdarzenia evdev co PS/2. Brak: pozycji bezwzględnej (tablet USB), touchpadów I2C-HID (nowsze laptopy — jądro K1 nie ma I2C-HID; X230 i
IdeaPad 110 mają touchpad PS/2), bezwładności przewijania, przewijania trackpointem (środkowy przycisk + ruch);
układ klawiatury liczy przybornik (M4).
