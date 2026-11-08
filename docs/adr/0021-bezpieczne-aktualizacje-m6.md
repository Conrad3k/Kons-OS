# 0021. Bezpieczne aktualizacje (M6): manifest, podpis, wersje, dm-verity, A/B, Secure Boot

- Status: Przyjęty (M6a wdrożone w 0.6.2, M6b w 0.6.3; M6c–M6d w 0.7.0 — szczegóły i zmiany w ADR 0022)
- Data: 2026-11-07
- Dotyczy: ADR 0018 (instalator), ADR 0019 (plan), SPEC §6, RYZYKA R38, R41

## Kontekst
Od 0.5.9 K1 jest zainstalowany na dysku i aktualizowany Instalatorem z pendrive'a. Bez podpisów
Instalator zapisze na dysk każdy „system” z pendrive'a — także podrobiony albo starszy (z dziurą,
którą już poprawiliśmy). Nie ma też sprawdzenia, czy system na dysku nie został podmieniony.

Ważne rozróżnienie: **sprawdzanie podpisu ma sens tylko wtedy, gdy sprawdza zaufany system.**
Gdy komputer startuje z obcego pendrive'a, działa kod z tego pendrive'a — on niczego nie
„odmówi”. Dlatego aktualizacja ma się odbywać **z zainstalowanego K1** (start z dysku, pendrive
z nową wersją podłączony, Instalator → *Zaktualizuj*), a start z obcego nośnika zablokuje dopiero
Secure Boot (M6d).

## Decyzja — kroki
**M6a (0.6.2) — manifest, podpis, wersja.**
1. Partycja „K1-EFI” (obraz USB i zainstalowana) ma na stałym miejscu (**47 MiB od początku**, za
   systemem plików FAT 47 MiB) **manifest** (512 B, `src/wspolne/k1_manifest.h`): wersja (liczbowo i
   tekstem), długość i skrót BLAKE2b-256 obszaru FAT, identyfikator klucza i podpis EdDSA (Monocypher:
   EdDSA z BLAKE2b, deterministyczny — obrazy nadal powtarzalne).
2. **Klucz:** 32-bajtowe ziarno w sekrecie GitHub Actions `K1_KLUCZ_PODPISU` (wpisuje właściciel —
   nigdy w czacie ani w repozytorium). Przy budowie z sekretem: klucz publiczny trafia do programów K1
   (`build/gen/k1_klucz.h`), manifest jest podpisany. Bez sekretu (budowa lokalna, PR) — **wersja
   deweloperska**: manifest bez podpisu, klucza publicznego brak.
3. **Instalator (program zapisujący)** przed zapisem: czyta manifest źródła, liczy skrót obszaru FAT
   i porównuje; gdy system ma wbudowany klucz — **wymaga ważnego podpisu tym kluczem**; przy
   aktualizacji czyta manifest z dysku i **odmawia starszej wersji** (ta sama — wolno: naprawa).
   Dysk bez manifestu (instalacje 0.5.9–0.6.1) = wersja 0 (pierwsza aktualizacja przechodzi).
   Po zapisie — jak dotąd odczyt i porównanie.
4. Nowe błędy: `K1_INST_BLAD_PODPIS` (brak / zły podpis albo uszkodzona kopia), `K1_INST_BLAD_STARSZA`.

**M6b — dm-verity dla rdzenia:** obraz rdzenia z drzewem skrótów, skrót główny w initramfs (podpisanym
w M6d); podmiana bajtu = bezpieczne zatrzymanie (dawne M5.2).

*Wdrożone (0.6.3):* `veritysetup format --no-superblock` (SHA-256, bloki 4 KiB, stała sól — obraz
powtarzalny) dopisuje drzewo skrótów za SquashFS w tym samym pliku `rdzen.squashfs`; skrót główny
(`rdzen.squashfs.skrot`, też w `SHA256SUMS`) trafia do linii poleceń jądra jako `k1.rdzen_skrot=`
(GRUB z USB i ISO, QEMU). Initramfs: bez parametru albo z niepoprawnym (nie 64 znaki hex) — awaria;
rozmiar danych z nagłówka SquashFS (`bytes_used`), urządzenie `k1-rdzen` tworzone przez ioctl
device-mappera (bez libdevmapper), potem **odczyt całego urządzenia** (≈ 30 MB, każdy blok sprawdzony
od razu, nie dopiero przy pierwszym użyciu) i dopiero montowanie `/dev/dm-N`. Błąd odczytu = obraz
zmieniony → awaria z komunikatem. Jądro: `DM_VERITY` bez FEC i bez podpisu skrótu (podpis przyjdzie
z UKI w M6d — do tego czasu skrót chroni przed uszkodzeniem i podmianą samego obrazu, nie przed
podmianą całego pendrive'a razem z linią poleceń; ten przypadek zamyka Secure Boot).

**M6c — A/B:** w partycji K1-EFI 512 MiB dwa miejsca na system; aktualizacja pisze do nieużywanego,
GRUB próbuje nowego raz, po udanym starcie system go zatwierdza; nieudany = powrót do poprzedniego.

**M6d — Secure Boot z własnym kluczem:** jądro + initramfs jako jeden podpisany plik (UKI),
klucz K1 wpisany raz przez MokManager shima (krok właściciela przy klawiaturze); Secure Boot znów
włączony — wtedy obcy pendrive nie uruchomi niepodpisanego kodu.

## Konsekwencje
- Właściciel jednorazowo dodaje sekret (instrukcja w README: wygenerowanie 32 losowych bajtów w
  PowerShellu i wklejenie w GitHub → Settings → Secrets → Actions). Do tego czasu wydania są
  „deweloperskie” (bez podpisu) — Instalator to pokazuje.
- Utrata sekretu = nowy klucz = zainstalowane systemy nie przyjmą aktualizacji (trzeba zainstalować
  od nowa z pendrive'a — dane na dysku wtedy przepadają). Sekret GitHub nie da się odczytać, tylko
  użyć w CI; dlatego warto mieć kopię ziarna w menedżerze haseł właściciela.
- Rozmiar systemu FAT na pendrivie: 47 MiB (był 48).
