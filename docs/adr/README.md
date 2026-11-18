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
| [0011](0011-kolejnosc-etapow-po-m4.md) | Kolejność etapów po M4 | Zastąpiony przez 0019 (kolejność), 0035 (mapa drogi) |
| [0012](0012-przeglad-m4-1-tryby-ekranu-zasoby.md) | M4.1: przegląd bezpieczeństwa, tryby ekranu, aplikacja Zasoby | Przyjęty |
| [0013](0013-dyski-i-usb-m5a.md) | M5a: sterowniki dysków i USB; pierwszy laptop — ThinkPad X230 | Przyjęty |
| [0014](0014-wskaznik-baterii.md) | Wskaźnik baterii na górnym pasku | Przyjęty |
| [0015](0015-blob-vault-format-i-szyfrowanie.md) | Blob Vault: własny format na surowej partycji, szyfrowanie w przestrzeni użytkownika | Przyjęty |
| [0016](0016-obraz-usb-z-partycja-danych.md) | Obraz na pendrive z partycją danych; skarbiec w łańcuchu startu | Przyjęty |
| [0017](0017-haslo-i-ekran-logowania.md) | Hasło i ekran logowania (M5e) | Przyjęty |
| [0018](0018-instalator-na-dysk.md) | Instalator: K1 na dysku komputera (M5f) | Przyjęty |
| [0019](0019-przeglad-0-5-9-i-nowy-plan.md) | Przegląd po 0.5.9 i nowy plan etapów (Fundament → Bezpieczne aktualizacje → Sieć → Przeglądarka → Pliki) | Przyjęty; kolejność zmieniona przez 0026, mapa drogi — 0035 |
| [0020](0020-strefy-mikro-maszyny-na-aplikacje.md) | Strefy: każda obca aplikacja w swojej mikro-maszynie (trwałość i grafika do wyboru, zaufana ramka, pliki przez Gatekeepera) | Przyjęty |
| [0021](0021-bezpieczne-aktualizacje-m6.md) | Bezpieczne aktualizacje (M6): manifest, podpis, wersje, dm-verity, A/B, Secure Boot | Przyjęty |
| [0022](0022-ab-i-secure-boot-zamkniete-jadro.md) | A/B i Secure Boot (M6c, M6d): zamknięte jądro k1.efi, dwa miejsca na system, klucz K1 w MOK | Przyjęty |
| [0023](0023-porzadki-przed-m7.md) | Porządki w kodzie przed M7 (0.7.1): jedna lista aplikacji, wspólne części, Kalkulator | Przyjęty |
| [0024](0024-wspolna-strefa.md) | Wspólna strefa na drobne, zaufane aplikacje (obok osobnych stref, przeglądarka zawsze osobno) | Przyjęty (kierunek) |
| [0025](0025-przegladarka-k1.md) | Przeglądarka K1: własne okno i pasek adresu, silnik (WPE) w strefie, sesje = strefy | Przyjęty (kierunek) |
| [0026](0026-pulpit-i-pliki-przed-siecia.md) | Pulpit (0.7.5) i Pliki (0.8.0) przed Siecią: ikony na pulpicie, przypinanie, Kosz (30 dni), wspólne dokumenty, okno wyboru pliku, dekoder w izolacji | Przyjęty; mapa drogi — 0035 |
| [0027](0027-przeplyw-plikow-miedzy-strefami.md) | Przepływ plików: Pliki jako jedyny węzeł, zgoda = gest w interfejsie K1, „Otwórz w…” z zapisem z powrotem, domyślne aplikacje | Przyjęty |
| [0028](0028-pulpit-warstwa-ikon-i-rozmieszczenie.md) | Pulpit 0.7.5: warstwa ikon w ekranie, rozmieszczenie w skarbcu, menu kontekstowe, „Zamknij okno” z paska | Przyjęty |
| [0029](0029-pliki-foldery-kosz.md) | Pliki 0.8.0: dokumenty w skarbcu, foldery jak w Windows, Kosz (30 dni), prawa w tabeli, protokół `DOK_*` | Przyjęty |
| [0030](0030-otwieranie-plikow-i-okno-wyboru.md) | Pliki 0.8.0, krok 2: Notatki na plikach, „Otwórz w…”, domyślne aplikacje, okno wyboru pliku | Przyjęty |
| [0031](0031-zdjecia-z-pendrive-czytnik-fat.md) | Pliki 0.8.0, krok 3: zdjęcia z pendrive'a — czytnik FAT w klatce, import z pochodzeniem | Przyjęty |
| [0032](0032-dekoder-obrazow-i-tapeta-ze-zdjecia.md) | Pliki 0.8.0, krok 4: dekoder obrazów (Wuffs) w jednorazowym procesie, podgląd zdjęć, tapeta ze zdjęcia | Przyjęty |
| [0033](0033-k1-dane-do-konca-pendrivea.md) | Pliki 0.8.0, krok 5: pusta K1-DANE rośnie przy pierwszym starcie do końca pendrive'a | Przyjęty |
| [0034](0034-skarbiec-na-duze-dane.md) | Skarbiec na duże dane (0.8.1): indeks, limity, kompaktowanie wcześniej, rezerwa, pliki w kawałkach | Przyjęty |
| [0035](0035-mapa-drogi-wedlug-wersji.md) | Mapa drogi według wersji (0.9 Sieć, 0.10 Strefy…) zamiast etapów M; jedno miejsce na plan | Przyjęty |
| [0036](0036-skarbiec-format-2-odcinki-i-spis.md) | Skarbiec na lata (0.8.2): format 2 — dziennik w odcinkach, spis skarbca, kompaktowanie po odcinku, 262 144 obiekty | Przyjęty, wykonany (0.8.2) |
| [0037](0037-podglad-zdjec.md) | Aplikacja Podgląd (0.8.2): zdjęcie w osobnym oknie, zdjęcia jednego folderu (zakres w prawach skarbca), dekoder dla Podglądu | Przyjęty |
| [0038](0038-pliki-startowe-w-aktualizacji.md) | Pliki startowe w aktualizacji (0.9.0): manifest plików startowych, nowe obok starych, jedno przełączenie; koniec manifestu formatu 1 | Przyjęty, wdrożony (0.9.0) |
| [0039](0039-siec-w-k1.md) | Sieć w K1 (0.9.0): tylko IPv4 po kablu, gniazda sieciowe tylko u nadzorcy, jądro nie wysyła ICMP, sterowniki kart szeroko | Przyjęty, wdrożony (0.9.0) |
| [0040](0040-repo-publiczne.md) | Repo publiczne (2026-11-14): minuty CI bez limitu; scalanie i zasady bez zmian | Przyjęty |
| [0041](0041-wifi-w-k1.md) | Wi-Fi w K1 (0.9.1): sterowniki i pliki producentów w podpisanym jądrze, nl80211 tylko u nadzorcy, opisy sieci w klatce, własne logowanie WPA2 | Przyjęty, krok 1 wdrożony (0.9.1) |
| [0042](0042-strefy-w-k1.md) | Strefy w K1 (0.10.0): KVM w jądrze, własny VMM „strefa” w klatce, jądro i obrazy stref w rdzeniu, virtio-mmio, zaufana ramka, testy w emulacji | Przyjęty, krok 1 wdrożony (0.10.0) |
| [0043](0043-przegladarka-w-strefie.md) | Przeglądarka w strefie (0.10.0): Firefox ESR z pakietów Debiana 13, własny kompozytor Wayland, połączenia stref przez łącznik w VMM i gniazda od nadzorcy (D3) | Przyjęty, krok 2 wdrożony (0.10.0) |
| [0044](0044-pamiec-stref-balon.md) | Pamięć stref: balon (0.10.1) — virtio-balloon, zgłaszanie wolnych stron, granica = pamięć komputera minus rezerwa K1, twarda granica klatki, pamięć gościa ponad 4 GiB | Przyjęty, wdrożony (0.10.1) |
| [0045](0045-opis-ekranu-dla-testow.md) | Opis ekranu dla testów (0.10.2) — okna i elementy po nazwach, tylko dla roli TEST z obrazu testowego, bez treści użytkownika | Przyjęty, wdrożony (0.10.2) |
| [0046](0046-aktualizacje-przez-siec.md) | Aktualizacje przez sieć (0.10.5): ogłoszenie wydania i paczka podpisane kluczem K1, poczekalnia za FAT-em K1-EFI dysku, strefa pobierania z HTTPS, „Wróć do poprzedniej wersji” | Przyjęty, wdrożony (0.10.5, kroki 2a i 2b) |
| [0047](0047-robot-aktualizacji.md) | Robot aktualizacji (0.10.5): codzienne wydania PATCH z poprawionym jądrem i pakietami przeglądarki, tylko numery i sumy, testy przez workflow_dispatch, porównanie konfiguracji przy nowej linii, tryb próbny do 0.10.5 | Przyjęty, wdrożony (0.10.5; pełny tryb od wydania) |
