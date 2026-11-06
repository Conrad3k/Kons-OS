# 0012. M4.1: przegląd bezpieczeństwa, tryby ekranu, aplikacja Zasoby

- Status: Przyjęty
- Data: 2026-11-06
- Dotyczy: SPEC §7 (ekran, nadzorca), §12 (protokół), ADR 0007, ADR 0008, ADR 0010, ADR 0011 (M4.1)

## Kontekst
M4.1 (ADR 0011) to porządki i celowany przegląd części, które przyjmują dane od innych procesów
(nadzorca, ekran, kanały, klatka), zanim w M5 dojdą hasła i klucze. Właściciel zgłosił też:
w VirtualBoxie z EFI obraz ma tylko 1024×768 (simpledrm: jeden tryb z firmware), pod dolnym paskiem
i menu dźwięku „wyjeżdża szklana podstawa”, i poprosił o prostą aplikację „Zużycie zasobów”.

## Decyzja
1. **Przegląd** — znalezione i poprawione:
   - *Kradzież fokusu:* każde nowe okno dostawało klawiaturę — aplikacja mogła otworzyć okno
     w chwili, gdy użytkownik pisze w innym (np. hasło w M5). Teraz klawiaturę dostaje tylko pierwsze
     okno aplikacji uruchomionej przed chwilą (≤ 10 s od połączenia — uruchomienie przez użytkownika
     z pulpitu), okno tej samej aplikacji albo okno, gdy żadne nie ma fokusu. Inne otwierają się tuż
     pod oknem z fokusem (to, co na wierzchu, zawsze dostaje klawisze).
   - *Pamięć mapowana przez ekran:* klient mógł zgłosić 4 okna wielkości ekranu bez zapisywania
     pamięci; strony czytane przy składaniu obciążają cgroup ekranu (shmem) → ryzyko OOM ekranu.
     Teraz limit na klienta: 2 pełne ekrany (powłoka: 1).
   - *Obszar zatwierdzenia* (ZATWIERDZ) przycinany do własnej powierzchni (wcześniej klient mógł
     wymuszać przerysowanie dowolnego miejsca ekranu).
   - *Log usług:* oczyszczanie przepuszczało sekwencje C1 (np. CSI `0x9b`, także zapisane w UTF-8)
     i błędne UTF-8 na konsolę → `k1_log_oczysc` (C0, DEL, C1, nadmiarowe/niepoprawne UTF-8), testy.
   - *Klatka:* deskryptory zamykane `close_range` bez górnej granicy (wcześniej do 1024).
   - *Katalog notatek:* `fchown`/`fchmod` przez `O_NOFOLLOW|O_DIRECTORY` (od M5 katalog przetrwa
     restart, więc dowiązanie podłożone w nim nie może przekierować zmiany właściciela).
   Przejrzane bez zmian: walidacja `k1_komunikat`/`k1_zdarzenie`, `k1_kanal` (MSG_CTRUNC, nadmiarowe
   deskryptory, zerowe pakiety), filtr seccomp i Landlock, kolejność porzucania uprawnień,
   kanał gui→nadzorca, kanał testowy, zdarzenia DRM.
2. **Tryby ekranu:** ekran zbiera z karty listę trybów (różne rozmiary, ≥ 800×600, ≤ 1920×1200 (poza preferowanym) —
   pamięć ekranu, do 18, preferowany zawsze i oznaczony), wysyła ją Ustawieniom (`K1_KOM_TRYB`), a ustawienie
   `K1_UST_ROZDZIELCZOSC` przełącza tryb w locie: nowe bufory, `SETCRTC`, dopiero potem zwolnienie
   starych (błąd = zostaje poprzedni tryb). Klienci dostają ponownie `WITAJ` z nowym rozmiarem;
   powłoka układa panele od nowa, okna zostają na ekranie. Wybór trwa do wyłączenia (jak inne
   ustawienia do M5).
3. **Sterownik vmwgfx** (karta VMware SVGA II: VirtualBox „VMSVGA” — domyślna karta dla Linuksa —
   i VMware) w jądrze, z `HYPERVISOR_GUEST` (bez PARAVIRT/KVM_GUEST/Xen). Daje listę rozdzielczości
   tam, gdzie simpledrm ma jeden tryb. Uwaga: QEMU `-vga vmware` nie spełnia wymagań vmwgfx („no
   pitchlock”) — sterownik odpada po zabraniu obrazu simpledrm, więc w QEMU tej karty nie używamy
   (testy: `std`/`virtio`). Działanie w VirtualBoxie/VMware sprawdzi właściciel (CI tego nie umie).
4. **Zasoby** (aplikacja natywna 4, UID 203): procesor i pamięć całego systemu (wykresy z ostatniej
   minuty) i każdej usługi. Dane zbiera **nadzorca** (statystyki cgroup v2: `cpu.stat`,
   `memory.current`, `memory.stat` korzenia, `sysinfo`) i co sekundę wysyła je przez **jednokierunkowy
   kanał** (fd 4 aplikacji, `shutdown(SHUT_RD)` po stronie nadzorcy): `ZASOBY_SYSTEM`, `ZASOBY_USLUGA`
   (kierunek `OD_NADZORCY`, pola sprawdzane, testy i fuzzing). Aplikacja nie czyta żadnych plików
   (`/proc` ma `subset=pid`, Landlock bez ścieżek) i sprawdza, że nadawcą jest UID 0.
5. **Porządki:** `src/ekran/main.c` (1020 linii) podzielony na `main.c` (start, kanał sterujący,
   pętla, zmiana trybu), `scena.c` (składanie, wyjście), `okna.c` (klienci, powierzchnie, fokus),
   `wskaznik.c` (wejście), `drm.c` (KMS); program testowy na `wspolne.c`, `t_system.c`,
   `t_uslugi.c`, `t_okna.c`, `proba.c`, `k1_test_systemu.c` (main). Cień paneli (`k1_cien`) liczony
   poza kształtem panelu — naprawia „podstawę” pod paskiem i menu dźwięku.

## Konsekwencje
- Nowe okno innej aplikacji nie dostaje klawiatury (zachowanie jak w większości systemów z ochroną
  fokusu); test w systemie.
- Nowy kanał nadzorca → aplikacja: nadzorca nadal nie czyta nic od aplikacji Zasoby.
- vmwgfx to duży sterownik (TTM, wiele ioctl) — dostępny tylko dla ekranu (DRM master, seccomp na
  typ `'d'`); R18 rośnie. Na maszynach bez karty VMware nie jest ładowany.
- Rozdzielczość z listy jest ograniczona pamięcią karty (vmwgfx: VRAM) — nieudane przełączenie
  zostawia poprzedni tryb.

## Rozważane alternatywy
- Rozdzielczość tylko w GRUB-ie (`gfxmode`) — w VirtualBoxie z EFI firmware i tak podaje 1024×768.
- Statystyki czytane przez samą aplikację — wymagałoby dostępu do `/proc`/`sys` w klatce aplikacji.
- Fokus: nigdy dla nowych okien — gorsze dla użytkownika (uruchomiona aplikacja nie dostawałaby
  klawiatury).
