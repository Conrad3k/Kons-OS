# Komponent: wejscie (`/rdzen/system/wejscie`)

| | |
|---|---|
| **Status** | PRZETESTOWANE (M2) — `scripts/test-m2.sh` (mysz i klawiatura przez monitor QEMU) |
| **Kod** | `src/wejscie/main.c`, protokół `src/wspolne/k1_zdarzenie.h` |
| **Decyzje** | ADR 0006 |

## Cel
Czyta klawiatury i myszy (evdev) i przekazuje zdarzenia do `ekran`. Nie wie nic o obrazie.

## Granica zaufania
Dane z urządzeń (jądro) są tłumaczone na komunikaty K1 z ograniczonymi zakresami (ruch dzielony na
porcje ≤ 1024 px, akumulator ograniczony do ±100 000). Odbiorca i tak sprawdza każdy komunikat.

## Uprawnienia
**M2:** root (RYZYKA R3). Potrzebne: odczyt `/rdzen/jadro/dev/input/event*`. M3: osobny UID,
tylko te urządzenia, seccomp, Landlock.

## Wejścia i wyjścia
- Wejście: `/rdzen/jadro/dev/input/event0..31` — urządzenia z klawiszami liter (klawiatura) lub
  osiami REL_X/REL_Y (mysz), przejmowane na wyłączność (`EVIOCGRAB`).
- Wyjście: fd 3 (pipe pakietowy od nadzorcy, nieblokujący): `K1_ZD_KLAWISZ`, `K1_ZD_RUCH`,
  `K1_ZD_PRZYCISK`. Gdy ekran nie nadąża — zdarzenia są pomijane i liczone (nie blokuje się).
- Log: wykryte urządzenia, liczba pominiętych zdarzeń. **Nigdy kody klawiszy.**

## Zależności
musl (statycznie); jądro: evdev, i8042, atkbd, psmouse.

## Wywołania systemowe
`open`, `ioctl` (EVIOCGBIT, EVIOCGRAB), `poll`, `read`, `write`, `fcntl`, `nanosleep`, `close`.

## Obsługa błędów
Brak urządzeń → komunikat, działa dalej (bez zdarzeń). Odłączone urządzenie → pomijane. Błąd zapisu
do ekranu inny niż „pełny kanał” → `k1_koniec()`.

## Powierzchnia ataku
Struktury `input_event` od jądra (stały rozmiar), ioctl evdev. Brak parsowania danych użytkownika.

## Ograniczenia
Brak hotplugu (urządzenia wykrywane raz przy starcie), brak układu klawiatury (kody, nie znaki —
M4), brak obsługi USB/I2C HID (prawdziwy sprzęt — później).
