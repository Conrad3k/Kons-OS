# 0006. Ekran, wejście i minimalny nadzorca (M2)

- Status: Przyjęty
- Data: 2026-11-06
- Dotyczy: SPEC §7 (grafika rozdzielona na procesy, renderer własny, nadzorca), §12 (IPC), §14 (M2)

## Kontekst
M2 wymaga procesu `ekran` (DRM/KMS), pierwszej klatki (tapeta i logo K1) oraz klawiatury i myszy
z kursorem; dowód — zrzut ekranu. Sprawdzone w źródłach 6.18.60:
- `drivers/gpu/drm/tiny/bochs.c` — sterownik KMS dla karty „stdvga” QEMU (`-vga std`), obsługuje
  `page_flip` (`drm_atomic_helper_page_flip`), nie ma sprzętowego kursora.
- `drivers/gpu/drm/drm_auth.c` — pierwszy proces, który otworzy `/dev/dri/card0`, zostaje „DRM master”
  (`drm_master_open`); `DRM_IOCTL_SET_MASTER` innego procesu, gdy master istnieje, zwraca `EBUSY`.
- Klawiatura i mysz QEMU (i wielu laptopów) to PS/2 przez kontroler i8042 → `atkbd`, `psmouse` → evdev.
- Pipe w trybie pakietowym (`pipe2(O_DIRECT)`, man 2 pipe): każdy `write` to osobny pakiet, `read`
  zwraca jeden pakiet; zapisy ≤ `PIPE_BUF` są atomowe.

## Decyzja
- **Jądro:** `DRM` + `DRM_BOCHS` (bez fbdev/fbcon: `DRM_FBDEV_EMULATION=n`, `FB=n`), `INPUT_EVDEV`,
  `SERIO_I8042`, `KEYBOARD_ATKBD`, `MOUSE_PS2` (bez rozszerzeń producentów), bez `INPUT_MOUSEDEV`.
- **`ekran`** (`src/ekran/`): jedyny użytkownik karty graficznej. Bezpośrednio API KMS jądra
  (ioctl: GETRESOURCES/GETCONNECTOR/GETENCODER, CREATE_DUMB/ADDFB/MAP_DUMB, SETCRTC, PAGE_FLIP
  ze zdarzeniem zakończenia) — **bez libdrm**. Dwa bufory (podwójne buforowanie), kursor rysowany
  programowo: przed każdą klatką odtwarzany jest fragment tła spod poprzedniej pozycji kursora.
  Jeśli PAGE_FLIP jest niedostępny — rysowanie w jednym buforze + `DIRTYFB`.
- **Renderer** (`src/grafika/`): własny, programowy, w C — kształty jako funkcje odległości (SDF),
  co daje wygładzone krawędzie bez bibliotek; tło liczone raz przy starcie; dithering 4×4 przeciw
  pasmom w gradientach. LVGL niepotrzebny na tym etapie (brak tekstu i widżetów — wrócimy do tego w M4).
- **`wejscie`** (`src/wejscie/`): czyta urządzenia evdev (klawiatura = klawisze liter, mysz = REL_X/Y),
  przejmuje je na wyłączność (`EVIOCGRAB`), tłumaczy na komunikaty K1. **Nigdy nie zapisuje kodów
  klawiszy w logu.**
- **Protokół** (`src/wspolne/k1_zdarzenie.h`): 16 bajtów — wersja, typ, długość, kod, wartość, dx, dy,
  pole zarezerwowane = 0; odbiorca sprawdza każde pole (`k1_zdarzenie_sprawdz`, testy + fuzzing).
  Transport: pipe w trybie pakietowym od nadzorcy (wejscie → ekran, jednokierunkowo). W M3/M4
  przejdziemy na gniazda UNIX (potrzebne do przekazywania deskryptorów pamięci współdzielonej).
- **`nadzorca` w wersji minimalnej** (`src/nadzorca/`): uruchamia `wejscie` i `ekran`, łączy je
  kanałem (fd 3), zapisuje w logu zakończenie usługi. Pełny nadzorca (UID, capabilities, seccomp,
  Landlock, cgroups, restarty) — M3.
- **Zachowanie w M2:** mysz przesuwa kursor, strzałki na klawiaturze też (po 20 px), wciśnięty lewy
  przycisk zmienia kolor kursora na akcent. Ekran zapisuje w logu stan kursora (pozycja, przycisk,
  liczniki zdarzeń) najwyżej 2 razy na sekundę — to dowód dla testów.
- **Hak testowy:** jeśli w obrazie jest `/rdzen/system/test-systemu` (tylko obraz testowy), nadzorca
  uruchamia go po usługach i kończy się jego kodem wyjścia (init wyłącza wtedy maszynę).

## Konsekwencje
- K1 pokazuje obraz w QEMU (`-vga std`). Na prawdziwym sprzęcie potrzebny będzie inny sterownik
  (np. `simpledrm` z framebuffera UEFI) — razem z obsługą dysków/USB przy pierwszych testach właściciela.
- Wszystkie trzy usługi w M2 działają jako root — przejęcie `wejscie` lub `ekran` daje dziś pełne
  uprawnienia (RYZYKA R3, M3).
- Sterownik DRM i podsystem wejścia to nowa powierzchnia ataku jądra (RYZYKA R18).
- Awaria `ekran` nie zatrzymuje systemu (test), ale w M2 ekran nie jest restartowany (M3).

## Rozważane alternatywy
- virtio-gpu (ma sprzętowy kursor) — większy sterownik; bochs wystarcza i jest domyślny w QEMU.
- libdrm — zewnętrzna biblioteka; potrzebne ioctl to kilkanaście wywołań z nagłówków jądra.
- virtio-input / USB HID dla myszy w QEMU — PS/2 jest prostsze i występuje też na laptopach.
- Gniazda UNIX już w M2 — dodają `CONFIG_UNIX` bez potrzeby (kanał jest jednokierunkowy, od nadzorcy).
