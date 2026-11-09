# Decyzje architektoniczne (ADR)

Każda zmiana architektury i każda korekta specyfikacji (`docs/SPEC.md`) ma tu swój plik
`NNNN-krotki-opis.md`. Raz przyjętego ADR-u nie zmieniamy treściowo — nowa decyzja to nowy ADR,
który „zastępuje” stary (status starego: *Zastąpiony przez NNNN*).

Szablon:

```markdown
# NNNN. Tytuł

- Status: Przyjęty | Zastąpiony przez NNNN | Proponowany
- Data: RRRR-MM-DD
- Dotyczy: SPEC §…, etap M…

## Kontekst
Problem, fakty (ze źródłami: plik w źródłach jądra, man-page, wynik testu).

## Decyzja
Co wybieramy.

## Konsekwencje
Co zyskujemy, co tracimy, co trzeba zrobić później. Ograniczenia bezpieczeństwa.

## Rozważane alternatywy
Krótko, dlaczego nie.
```

| Nr | Tytuł | Status |
|---|---|---|
| [0001](0001-k1-zamiast-kons-os.md) | K1 zamiast dystrybucji Kons-OS; archiwum i struktura repo | Przyjęty |
| [0002](0002-toolchain-i-flagi-kompilacji.md) | Toolchain (musl, statyczne PIE) i flagi kompilacji | Przyjęty |
| [0003](0003-jadro-wersja-i-konfiguracja.md) | Jądro: wersja, źródło, konfiguracja, wzmocnienia | Przyjęty |
| [0004](0004-start-i-uklad-systemu-plikow.md) | Start (initramfs → init) i układ systemu plików | Przyjęty |
| [0005](0005-bledy-krytyczne-przy-starcie.md) | Obsługa błędów krytycznych przy starcie | Przyjęty |
| [0006](0006-ekran-wejscie-i-nadzorca-m2.md) | Ekran (DRM/KMS), wejście (evdev) i minimalny nadzorca (M2) | Przyjęty |
| [0007](0007-nadzorca-klatki-uslug-m3.md) | Nadzorca, klatki usług i kanały między nimi (M3) | Przyjęty |
| [0008](0008-pulpit-okna-i-aplikacje-m4.md) | Pulpit, okna i pierwsze aplikacje (M4) | Przyjęty |
| [0009](0009-czcionki-i-tekst.md) | Czcionki i rysowanie tekstu | Przyjęty |
| [0010](0010-obraz-iso-do-maszyn-wirtualnych.md) | Obraz ISO do maszyn wirtualnych i grafika z firmware | Przyjęty (tymczasowy do M10) |
| [0011](0011-kolejnosc-etapow-po-m4.md) | Kolejność etapów po M4 | Przyjęty |
| [0012](0012-przeglad-m4-1-tryby-ekranu-zasoby.md) | M4.1: przegląd bezpieczeństwa, tryby ekranu, aplikacja Zasoby | Przyjęty |
| [0013](0013-dyski-i-usb-m5a.md) | M5a: sterowniki dysków i USB; pierwszy laptop — ThinkPad X230 | Przyjęty |
| [0014](0014-wskaznik-baterii.md) | Wskaźnik baterii na górnym pasku | Przyjęty |
| [0015](0015-blob-vault-format-i-szyfrowanie.md) | Blob Vault: własny format na surowej partycji, szyfrowanie w przestrzeni użytkownika | Przyjęty |
| [0016](0016-obraz-usb-z-partycja-danych.md) | Obraz na pendrive z partycją danych; skarbiec w łańcuchu startu | Przyjęty |
| [0017](0017-haslo-i-ekran-logowania.md) | Hasło i ekran logowania (M5e) | Przyjęty |
| [0018](0018-instalator-na-dysk.md) | Instalator: K1 na dysku komputera (M5f) | Przyjęty |
| [0019](0019-przeglad-0-5-9-i-nowy-plan.md) | Przegląd po 0.5.9 i nowy plan etapów (Fundament → Bezpieczne aktualizacje → Sieć → Przeglądarka → Pliki) | Przyjęty |
| [0020](0020-strefy-mikro-maszyny-na-aplikacje.md) | Strefy: każda obca aplikacja w swojej mikro-maszynie (trwałość i grafika do wyboru, zaufana ramka, pliki przez Gatekeepera) | Przyjęty |
| [0021](0021-bezpieczne-aktualizacje-m6.md) | Bezpieczne aktualizacje (M6): manifest, podpis, wersje, dm-verity, A/B, Secure Boot | Przyjęty |
| [0022](0022-ab-i-secure-boot-zamkniete-jadro.md) | A/B i Secure Boot (M6c, M6d): zamknięte jądro k1.efi, dwa miejsca na system, klucz K1 w MOK | Przyjęty |
| [0023](0023-porzadki-przed-m7.md) | Porządki w kodzie przed M7 (0.7.1): jedna lista aplikacji, wspólne części, Kalkulator | Przyjęty |
| [0024](0024-wspolna-strefa.md) | Wspólna strefa na drobne, zaufane aplikacje (obok osobnych stref, przeglądarka zawsze osobno) | Przyjęty (kierunek) |
| [0025](0025-przegladarka-k1.md) | Przeglądarka K1: własne okno i pasek adresu, silnik (WPE) w strefie, sesje = strefy | Przyjęty (kierunek) |
| [0026](0026-pulpit-i-pliki-przed-siecia.md) | Pulpit (0.7.5) i Pliki (0.8.0) przed Siecią: ikony na pulpicie, przypinanie, Kosz (30 dni), wspólne dokumenty, okno wyboru pliku, dekoder w izolacji | Przyjęty |
