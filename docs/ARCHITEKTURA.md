# Architektura K1 — jak to jest zbudowane

Specyfikacja (co i dlaczego): [`SPEC.md`](SPEC.md). Decyzje: [`adr/`](adr/README.md).
Ten plik opisuje **stan obecny**: mapę repo, przebieg budowy i startu, konwencje.

## W jednym zdaniu
K1 = jądro Linux (własna, minimalna konfiguracja) + nasz initramfs + nasz init, wszystko w C
(statycznie z musl), uruchamiane na razie w QEMU. Nie ma dystrybucji, powłoki ani bibliotek.

## Mapa repozytorium

| Ścieżka | Co tu jest |
|---|---|
| `VERSION`, `CHANGELOG.md` | wersja K1 (SemVer) i historia zmian |
| `Makefile` | kompilacja programów K1 (musl) i testów na hoście |
| `src/wspolne/` | wspólne moduły: log + błąd krytyczny (`k1_log`), parser linii poleceń jądra (`k1_cmdline`) |
| `src/initramfs/` | `/init` w initramfs: montuje obraz rdzenia, przełącza korzeń |
| `src/init/` | `/rdzen/init` — PID 1 |
| `jadro/` | wersja, konfiguracja i linia poleceń jądra, klucze PGP |
| `testy/m1/` | program testowy uruchamiany wewnątrz K1 |
| `testy/host/` | testy jednostkowe (ASan/UBSan) i fuzzing na hoście |
| `scripts/` | budowa, QEMU, zrzuty ekranu, testy, kontrole (opis niżej) |
| `docs/` | SPEC, ADR, ten plik, TESTY, ZALEZNOSCI, RYZYKA, `komponenty/` (opis każdego komponentu) |
| `.github/workflows/k1.yml` | CI: budowa + testy na `main`, artefakty, tag wersji |
| `build/` | wszystko, co powstaje przy budowie (ignorowane przez git) |

### Skrypty

| Skrypt | Co robi |
|---|---|
| `przygotuj-srodowisko.sh` | instaluje pakiety Ubuntu potrzebne do budowy i testów |
| `zbuduj.sh` | wszystko: `zbuduj-jadro.sh` + `zbuduj-obraz.sh` |
| `zbuduj-jadro.sh` | pobiera jądro (suma + podpis PGP), konfiguruje, sprawdza fragment, kompiluje |
| `zbuduj-obraz.sh` | kompiluje programy, sprawdza binaria, składa `initramfs.cpio` i obrazy SquashFS |
| `uruchom-qemu.sh` | uruchamia K1 w QEMU (log szeregowy do pliku, monitor do gniazda) |
| `zrzut-ekranu.sh` | zrzut ekranu działającej maszyny (PNG) |
| `test-m1.sh` | pełny test etapu M1 |
| `sprawdz-binaria.sh`, `sprawdz-konfiguracje-jadra.sh`, `sprawdz-kspp.sh` | kontrole bezpieczeństwa |
| `sprawdz-powtarzalnosc.sh` | drugi build w osobnym katalogu i porównanie sum (powtarzalność) |

## Jak powstaje obraz

```
jadro/wersja.env ──► pobranie + SHA-256 + podpis PGP ──► allnoconfig + jadro/k1.config ──► bzImage
                                                         (sprawdzenie każdej opcji)    └► nagłówki UAPI, gen_init_cpio
src/ ──► musl-gcc (static-pie, flagi SPEC §11) ──► k1-initramfs, init, k1-test-m1 ──► sprawdzenie ELF
                                                       │
                     initramfs.cpio  ◄── /init ────────┘
                     rdzen.squashfs  ◄── init (+ katalogi jadro/{dev,proc,sys}, system/)
                     rdzen-test.squashfs ◄── + k1-test-m1 jako system/nadzorca
```
Wynik w `build/wynik/` (+ `SHA256SUMS`, `srodowisko.txt`). Build jest powtarzalny: stały
`SOURCE_DATE_EPOCH` (data ostatniego commita), stałe dane budowy jądra, `-frandom-seed`.

## Jak startuje system (M1)

```
QEMU (-kernel bzImage -initrd initramfs.cpio, dysk virtio = rdzen.squashfs)
 └─ jądro 6.18 ── otwiera /dev/console w initramfs, uruchamia /init
     └─ k1-initramfs (PID 1)
          /proc/cmdline → k1.rdzen=/dev/vda (walidacja)
          tmpfs → nowy korzeń: aplikacje/ dane_uzytkownika/ rdzen/
          SquashFS z /dev/vda → /rdzen (ro) ; korzeń → ro ; usunięcie initramfs ; MS_MOVE + chroot
          execve /rdzen/init
     └─ /rdzen/init (PID 1)
          sysctl bezpieczeństwa → montowania /rdzen/jadro/{dev,proc,sys}, cgroup2, /dane_uzytkownika
          „K1: init OK” → nadzorca (M3; w obrazie testowym: program testowy) → pętla sygnałów
```
Szczegóły i tabela montowań: ADR 0004. Błędy: ADR 0005.

## Konwencje
- **Język:** komunikaty w systemie, dokumentacja, PR-y — po polsku; identyfikatory i komentarze
  w kodzie oraz commity — po angielsku.
- **Kod C:** małe moduły, jawne limity długości, sprawdzanie każdego błędu, bez `strcpy/strcat/sprintf`
  (tylko `snprintf`/`strncat` z limitem). Flagi w `Makefile` — zmiana tylko przez ADR.
- **Prefiksy logów:** `K1-INITRAMFS:`, `K1:` (init), `K1-TEST:`; błędy krytyczne `BŁĄD KRYTYCZNY:`.
- **Ścieżki w systemie** po polsku (`/rdzen`, `/dane_uzytkownika`, `/rdzen/system/…`).
- **Komponent = dokument** w `docs/komponenty/` (cel, granica zaufania, uprawnienia, wejścia/wyjścia,
  zależności, syscalle, błędy, powierzchnia ataku, testy) — SPEC §2.6.
- **Statusy:** ZAIMPLEMENTOWANE / PRZETESTOWANE (z dowodem) / PLANOWANE — SPEC §2.4.

## Wersjonowanie
SemVer od 0.1.0 w `VERSION`; tagi `k1-vX.Y.Z` zakłada CI po udanym buildzie i testach na `main`
(stare tagi `v0.1.x` to Kons-OS na Debianie, ADR 0001).

| Zmiana | Co podbić |
|---|---|
| ukończony etap (M2, M3, …) | MINOR: 0.1.0 → 0.2.0 |
| poprawka, aktualizacja jądra, drobna zmiana w systemie | PATCH: 0.1.0 → 0.1.1 |
| tylko dokumentacja / CI / repo | bez zmiany |

## Ograniczenia środowiska pracy
- Kontener deweloperski bez `/dev/kvm` → QEMU w emulacji TCG (start K1 ~1–2 s, cały test M1 ~1,5 min).
- Sieć kontenera: tylko HTTPS przez proxy; dozwolone m.in. apt Ubuntu, deb.debian.org, GitHub,
  cdn/www/git.kernel.org.
- CI (GitHub Actions) buduje tylko `main` — minuty prywatnego repo są ograniczone.
