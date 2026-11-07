# Komponent: wejscie (`/rdzen/system/wejscie`)

| | |
|---|---|
| **Status** | PRZETESTOWANE (M2, M3) — `scripts/test-m2.sh` (mysz i klawiatura przez monitor QEMU), klatka: testy w systemie |
| **Kod** | `src/wejscie/main.c`, protokół `src/wspolne/k1_zdarzenie.h` |
| **Decyzje** | ADR 0006, ADR 0007 (klatka, kanał) |

## Cel
Czyta klawiatury i myszy (evdev) i przekazuje zdarzenia do `ekran`. Nie wie nic o obrazie.

## Granica zaufania
Dane z urządzeń (jądro) są tłumaczone na komunikaty K1 z ograniczonymi zakresami (ruch dzielony na
porcje ≤ 1024 px, akumulator ograniczony do ±100 000). Odbiorca i tak sprawdza każdy komunikat.

## Uprawnienia
**M3 (ADR 0007):** UID/GID 100, bez capabilities, `NO_NEW_PRIVS`, seccomp (baza + `ioctl` tylko typu
`'E'`), Landlock bez żadnej ścieżki, cgroup `wejscie` (8 MiB, 1 proces). Urządzenia `event*` otwiera
nadzorca i przekazuje jako fd 4, 5, … — wejscie samo niczego nie otwiera.

## Wejścia i wyjścia
- Wejście: deskryptory 4… od nadzorcy (`event0..31`) — zostają urządzenia z klawiszami liter
  (klawiatura) lub osiami REL_X/REL_Y (mysz), przejmowane na wyłączność (`EVIOCGRAB`); inne są zamykane.
- Wyjście: fd 3 — kanał `SOCK_SEQPACKET` do ekranu (ekran sprawdza, że nadawca ma UID 100):
  `K1_ZD_KLAWISZ`, `K1_ZD_RUCH`, `K1_ZD_PRZYCISK`. Gdy ekran nie nadąża — zdarzenia są pomijane
  i liczone (nie blokuje się). Zamknięcie kanału przez ekran → koniec (nadzorca uruchomi ponownie).
- Log: wykryte urządzenia, liczba pominiętych zdarzeń. **Nigdy kody klawiszy.**

## Zależności
musl (statycznie); jądro: evdev, i8042, atkbd, psmouse.

## Wywołania systemowe
`ioctl` (EVIOCGBIT, EVIOCGRAB), `poll`, `read`, `sendmsg`, `write` (log), `fcntl`, `close` — w ramach
filtra seccomp (ADR 0007).

## Obsługa błędów
Brak urządzeń → komunikat, działa dalej (bez zdarzeń). Odłączone urządzenie → pomijane. Błąd zapisu
do ekranu inny niż „pełny kanał” → `k1_koniec()`.

## Powierzchnia ataku
Struktury `input_event` od jądra (stały rozmiar), ioctl evdev. Brak parsowania danych użytkownika.

## Ograniczenia
Hotplug przez nadzorcę: nowe urządzenie = ponowne uruchomienie `wejscie` z pełnym zestawem (co 2 s);
odłączone urządzenie `wejscie` przestaje czytać (log „urządzenie odłączone”). Od M5a klawiatury
i myszy USB (HID generic) — te same zdarzenia evdev co PS/2. Brak: pozycji bezwzględnej (tablet USB,
touchpad w trybie rozszerzonym — M5b), I2C-HID (nowsze laptopy), układ klawiatury liczy przybornik (M4).
