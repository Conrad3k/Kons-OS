# Kons K1 Sovereign

Własny, minimalistyczny i bezpieczny system operacyjny na jądrze Linux. Nie jest dystrybucją
Linuksa: ma własny program startowy, własny ekran i pulpit, (wkrótce) własny magazyn danych
i ścisłą izolację aplikacji. Wszystko, co nasze, jest napisane w C.

> Wcześniej w tym repozytorium był **Kons-OS** — dystrybucja na bazie Debiana (wersje 0.1.0–0.1.2).
> Jest zachowany w gałęzi [`archiwum/debian-0.1.2`](../../tree/archiwum/debian-0.1.2) i tagach `v0.1.x`.

## Na jakim etapie jesteśmy

| Etap | Co to daje | Status |
|---|---|---|
| M0 Porządki | nowe repo, dokumenty, narzędzia, automatyczne budowanie | PRZETESTOWANE |
| M1 Start | system startuje w maszynie wirtualnej (QEMU) i pisze „K1: init OK” | PRZETESTOWANE |
| M2 Ekran | obraz na ekranie: tapeta, kursor; mysz i klawiatura | PRZETESTOWANE (w QEMU) |
| M3 Trusted Core | nadzorca usług, każda usługa we własnej „klatce”, restart po awarii | PRZETESTOWANE |
| M4 GUI | pulpit, okna, aplikacje Zegar, Notatki, Ustawienia; obraz ISO do maszyny wirtualnej | PRZETESTOWANE (QEMU: UEFI i BIOS) |
| M4.1 Porządki i audyt | przegląd bezpieczeństwa i porządki w kodzie | NASTĘPNY |
| M5 Dane i sprzęt | trwałe, zaszyfrowane dane, hasło, dyski i USB, start z pendrive'a | PLANOWANE |
| M5.1–M12 | próba przeglądarki, weryfikacja obrazu, sieć, przeglądarka w izolacji, bezpieczny start, audyt | PLANOWANE (kolejność: ADR 0011) |

Pełny plan i zasady: [`docs/SPEC.md`](docs/SPEC.md). Co zostało do zrobienia (i czego potrzeba od właściciela):
[`docs/DO_ZROBIENIA.txt`](docs/DO_ZROBIENIA.txt). Co się zmieniło: [`CHANGELOG.md`](CHANGELOG.md).

**Ważne:** K1 działa w maszynie wirtualnej: pulpit z paskiem na dole (Start, Zegar, Notatki,
Ustawienia), paskiem na górze (dźwięk, data i godzina), oknami i polską klawiaturą. Na prawdziwym
laptopie jeszcze nie (brak sterowników USB, dysków NVMe, touchpada) — to przyjdzie przed testami
na sprzęcie. Notatki i ustawienia są na razie tylko w pamięci (znikają po wyłączeniu, do M5).
Wygląd: [`docs/WYGLAD.md`](docs/WYGLAD.md).

## Uruchom w maszynie wirtualnej
1. Pobierz obraz: **Releases** → najnowsze `k1-vX.Y.Z` → plik `k1-X.Y.Z.iso` (ok. 16 MB).
   (Albo Actions → „K1 — budowa i testy” → ostatnie zielone uruchomienie → Artifacts → `k1-<wersja>-obraz`.)
2. **VirtualBox** (zalecany): Nowa maszyna → typ *Other Linux (64-bit)*, bez dysku twardego.
   - Pamięć: **1024 MB**, procesory: **2**.
   - System → Płyta główna: **Urządzenie wskazujące: Mysz PS/2** (ważne — mysz USB/tablet nie działa);
     „Włącz EFI” może być włączone albo nie (oba działają).
   - Ekran: kontroler **VMSVGA**, pamięć wideo 16 MB lub więcej.
   - Pamięć masowa: napęd optyczny → wybierz pobrany plik ISO. Sieć i dźwięk można wyłączyć.
3. Uruchom. Po kilku sekundach pojawi się pulpit. Kliknij w okno maszyny, żeby „złapać” mysz
   (prawy Ctrl ją uwalnia). Polskie znaki: prawy Alt + litera (np. prawy Alt + a = ą).
4. Wyłączenie: znak K1 na dolnym pasku → **Wyłącz**.

Działa też w VMware (UEFI lub BIOS) i QEMU/virt-manager; Hyper-V — jeszcze nie.

## Jak sprawdzić testy bez instalowania niczego
Po każdej zmianie na `main` GitHub sam buduje system i uruchamia testy:

1. Zakładka **Actions** → workflow **„K1 — budowa i testy”** → ostatnie uruchomienie.
2. Zielony ✔ = system się zbudował i przeszedł wszystkie testy (zrzuty ekranu w artefakcie `k1-<wersja>-logi`).

## Budowanie i testy (dla programisty)
Ubuntu 24.04:
```bash
scripts/przygotuj-srodowisko.sh   # pakiety (raz)
scripts/zbuduj.sh                 # jądro + programy + obrazy + ISO → build/wynik/
scripts/testy.sh                  # wszystkie testy (QEMU, także negatywne)
scripts/uruchom-qemu.sh           # ręczne uruchomienie; log: build/qemu/serial.log
scripts/uruchom-qemu.sh --iso build/wynik/k1-$(cat VERSION).iso --uefi --vga virtio   # jak w VM
```
Szczegóły: [`docs/ARCHITEKTURA.md`](docs/ARCHITEKTURA.md), [`docs/TESTY.md`](docs/TESTY.md).

## Dokumentacja
- [`docs/SPEC.md`](docs/SPEC.md) — specyfikacja (nadrzędna), [`docs/adr/`](docs/adr/README.md) — decyzje
- [`docs/ARCHITEKTURA.md`](docs/ARCHITEKTURA.md) — jak to jest zbudowane, mapa repo
- [`docs/komponenty/`](docs/komponenty/) — opis każdego elementu systemu
- [`docs/RYZYKA.md`](docs/RYZYKA.md) — czego system jeszcze nie chroni
- [`docs/ZALEZNOSCI.md`](docs/ZALEZNOSCI.md) — z czego korzystamy i dlaczego

## Feedback
Napisz w czacie z Claude albo załóż zgłoszenie: **Issues → New issue** (szablony: *Feedback z testu*,
*Błąd*, *Pomysł*).
