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
| [0010](0010-obraz-iso-do-maszyn-wirtualnych.md) | Obraz ISO do maszyn wirtualnych i grafika z firmware | Przyjęty; GRUB zostaje — ADR 0058 (0.13.0) |
| [0011](0011-kolejnosc-etapow-po-m4.md) | Kolejność etapów po M4 | Zastąpiony przez 0019 (kolejność), 0035 (mapa drogi) |
| [0012](0012-przeglad-m4-1-tryby-ekranu-zasoby.md) | M4.1: przegląd bezpieczeństwa, tryby ekranu, aplikacja Zasoby | Przyjęty |
| [0013](0013-dyski-i-usb-m5a.md) | M5a: sterowniki dysków i USB; pierwszy laptop — ThinkPad X230 | Przyjęty; urządzenie wejścia z USB po pierwszym pulpicie czeka na zgodę — ADR 0060 |
| [0014](0014-wskaznik-baterii.md) | Wskaźnik baterii na górnym pasku | Przyjęty |
| [0015](0015-blob-vault-format-i-szyfrowanie.md) | Blob Vault: własny format na surowej partycji, szyfrowanie w przestrzeni użytkownika | Przyjęty |
| [0016](0016-obraz-usb-z-partycja-danych.md) | Obraz na pendrive z partycją danych; skarbiec w łańcuchu startu | Przyjęty |
| [0017](0017-haslo-i-ekran-logowania.md) | Hasło i ekran logowania (M5e) | Przyjęty; hasło ustawia się i zmienia tylko na ekranie zabezpieczeń — ADR 0061 |
| [0018](0018-instalator-na-dysk.md) | Instalator: K1 na dysku komputera (M5f) | Przyjęty |
| [0019](0019-przeglad-0-5-9-i-nowy-plan.md) | Przegląd po 0.5.9 i nowy plan etapów (Fundament → Bezpieczne aktualizacje → Sieć → Przeglądarka → Pliki) | Przyjęty; kolejność zmieniona przez 0026, mapa drogi — 0035 |
| [0020](0020-strefy-mikro-maszyny-na-aplikacje.md) | Strefy: każda obca aplikacja w swojej mikro-maszynie (trwałość i grafika do wyboru, zaufana ramka, pliki przez Gatekeepera) | Przyjęty |
| [0021](0021-bezpieczne-aktualizacje-m6.md) | Bezpieczne aktualizacje (M6): manifest, podpis, wersje, dm-verity, A/B, Secure Boot | Przyjęty |
| [0022](0022-ab-i-secure-boot-zamkniete-jadro.md) | A/B i Secure Boot (M6c, M6d): zamknięte jądro k1.efi, dwa miejsca na system, klucz K1 w MOK | Przyjęty; linia poleceń wydania bez portu szeregowego — ADR 0059 |
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
| [0035](0035-mapa-drogi-wedlug-wersji.md) | Mapa drogi według wersji (0.9 Sieć, 0.10 Strefy…) zamiast etapów M; jedno miejsce na plan | Przyjęty; mapa od 0.12 — ADR 0053 |
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
| [0048](0048-ramka-okna-strefy-bez-obwodki.md) | Ramka okna strefy bez kolorowej obwódki (0.10.7): ramka jak okna K1, kolor sesji w kropce przed jej nazwą i linii pod paskiem; ramkę dalej rysuje ekran poza obrazem strefy | Przyjęty, wdrożony (0.10.7, krok 4) |
| [0049](0049-karta-graficzna-dla-stref.md) | Karta graficzna dla stref (0.10.9): i915 w K1, węzeł render tylko dla dekodera filmów i pośrednika grafiki, most „native context” (tylko karty z pełnym PPGTT), filmy kartą tylko z szybką grafiką | Przyjęty, wdrożenie w krokach 2–6 planu 0.10.9 |
| [0050](0050-obraz-strefy-w-miejscu-ab.md) | Obraz przeglądarki poza rdzeniem (0.10.9): w miejscu A/B zaraz za k1.efi, w rdzeniu tylko jego skrót, dm-verity w nadzorcy, od kroku 4b miejsca A/B 768 MiB (rozmiar z bloku układu) i obraz w zstd | Przyjęty, wdrożony (0.10.9, kroki 4 i 4b) |
| [0051](0051-obraz-strefy-bez-kopii.md) | Obraz strefy bez kopii (0.10.11): bufor karty przeglądarki jako wyświetlacz strefy, VMM podaje go ekranowi, ekran pokazuje go warstwą karty (okno widoczne w całości) albo składa sam | Przyjęty, wdrożony (0.10.11, krok 2) |
| [0052](0052-droga-plikow-strefy.md) | Droga plików strefa ↔ K1 (0.11.0): port plików vsock 3 z ramkami, agent w strefie (folder pobranych), zgoda w okienku K1, zapis w Pobranych przez nadzorcę (właściciel `K1_WL_POBRANIA`), pochodzenie strefy; adresu strony nie ma; krok 3: pliki do strefy („Z K1”), „Otwórz w…” wpisywane przez kompozytor strefy, zmieniona wersja z powrotem | Przyjęty, wdrożenie w 0.11.0 (kroki 2, 3; uzupełnienia w krokach 4–5) |
| [0053](0053-dzwiek-jako-0-12.md) | Dźwięk jako 0.12 (prośba właściciela z 2026-11-22): Bezpieczny start → 0.13, Audyt → 0.14; terminy w żywych dokumentach przesunięte, historia bez zmian | Przyjęty |
| [0054](0054-dzwiek-w-k1.md) | Dźwięk w K1 (0.12.0): usługa dźwięku z wyłącznym prawem do karty (węzły /dev/snd tylko od nadzorcy), ALSA bez alsa-lib z listą ioctl, jeden format i głośność w usłudze, kanał klientów z pierścieniem w memfd (klient podaje tylko liczbę ramek), test z kartą QEMU zapisującą do WAV | Przyjęty, krok 1 wdrożony (0.12.0) |
| [0055](0055-tor-dzwieku-wysokiej-jakosci.md) | Tor dźwięku wysokiej jakości (0.12.3): liczby zmiennoprzecinkowe w usłudze dźwięku, ogranicznik zamiast przycinania, dither TPDF tylko gdy liczenie coś zmieniło, najlepszy format karty (24 bity HD Audio przez podformat), pierścień F32 strefy z S16, S32 i FLOAT gościa | Przyjęty, wdrożony (0.12.3); pkt 21 — poprawka poziomu i sceny na start (plan 0.13.0, krok 1) |
| [0056](0056-lancuch-wydan.md) | Łańcuch wydań (0.13.0): zwłoka wydania 20 minut liczona przez laptop, ogłoszenie formatu 2 (k1-ogloszenie2.bin) z czasem podpisu i terminem 7 dni, „Ogłoszenie dnia”, czas z nagłówka Date za TLS-em (stopka strefy pobierania), akcje GitHuba przypięte skrótem | Przyjęty, wdrożony (plan 0.13.0, krok 1) |
| [0057](0057-tpm-i-odwolywanie-starszych-wersji.md) | TPM i odwoływanie starszych wersji (0.13.0): generacja K1 w TPM 2.0 (tylko nadzorca, bez TSS), starsza wersja nie otwiera danych, stan magazynu w TPM — dane cofnięte do starszej kopii z pytaniem „To moje dane”, SBAT w `k1.efi` | Przyjęty, wdrożony (plan 0.13.0, krok 2) |
| [0058](0058-tryb-ratunkowy-grub-zostaje.md) | Tryb ratunkowy (0.13.0): ten sam `k1.efi` z `k1_tryb=r` w grubenv, menu GRUB-a po Esc albo po dwóch nieudanych startach, ekran ratunkowy (poprzednia wersja, raport na pendrive FAT16/FAT32); GRUB zostaje (termin ADR 0010 zamknięty) | Przyjęty, wdrożony (plan 0.13.0, krok 3) |
| [0059](0059-obraz-wydania-bez-portu-szeregowego.md) | Obraz wydania (0.13.0): `console=ttynull` w `k1.efi` (nic na porcie szeregowym; testy — `k1-test.efi` i obrazy `-test`), ekran awarii (panika jądra z opisem K1, `DRM_PANIC`), „Zapisz raport na pendrive” w Ustawieniach → Informacje, `mac80211_hwsim` tylko w jądrze testów | Przyjęty, wdrożony (plan 0.13.0, krok 4) |
| [0060](0060-dostep-fizyczny-zgoda-usb-zacheta-do-hasla.md) | Dostęp fizyczny (0.13.0): nowa klawiatura albo mysz z USB po pierwszym pulpicie czeka na „Pozwól” (nadzorca nie daje jej usłudze wejścia; zgoda w K1, nie w jądrze), okienko „Ustaw hasło” przy pierwszym starcie bez hasła | Przyjęty, wdrożony (plan 0.13.0, krok 5) |
| [0061](0061-zaufany-ekran-ctrl-alt-del.md) | Zaufany ekran K1 (0.13.0): Ctrl+Alt+Del rozpoznaje ekran — nie dostaje go żadne okno — i zawsze prowadzi do ekranu zabezpieczeń powłoki (nowy rodzaj powierzchni, nad wszystkim, reszta przyciemniona); hasło K1 ustawia się i zmienia tylko tam | Przyjęty, wdrożony (plan 0.13.0, krok 6) |
| [0062](0062-jadro-randstruct-kcfi-pietro-nizej.md) | Jądro (0.13.0): bez RANDSTRUCT (układ struktur publicznego, powtarzalnego wydania zna każdy), kCFI w audycie 0.15 (Clang z zachowanym `KSTACK_ERASE`, najpierw tryb ostrzegawczy na laptopach), ochrona jądra przez hiperwizor („piętro niżej”) po 1.0 | Przyjęty (decyzja, jądro bez zmian; plan 0.13.0, krok 7) |
| [0063](0063-filmy-karta-w-strefie.md) | Filmy kartą w strefie (0.13.2): kontekst filmów virtio-gpu z własnym zbiorem możliwości K1, nasz sterownik VA `virtio_gpu_drv_video.so`, klatka w buforze karty strefy (dekoder pisze ją do dma-bufa od pośrednika), dekoder strefy na prośbę VMM-a, pomiar zamiast bramki zysku; X230 bez filmów kartą (Firefox wymaga składania kartą) | Przyjęty; wdrożenie — kroki 2–4 planu 0.13.2 |
| [0064](0064-aplikacje-ze-sklepu.md) | Aplikacje ze Sklepu jako 0.14 (prośba właściciela z 2026-11-27): obce aplikacje tylko ze Sklepu K1 (bez pendrive'a), wiersz aplikacji w kodzie K1 i obraz z sieci podpisany kluczem wydań, zamknięte aplikacje pobierane od producenta w strefie, robot bez prawa weta, pamięć na start przy każdej aplikacji (Discord 500 MiB), obraz bazowy; Audyt → 0.15 | Przyjęty (kierunek); wdrożenie — plan 0.14.0 |
| [0065](0065-mikrofon.md) | Mikrofon (0.13.5): nagrywa usługa dźwięku (węzeł nagrywania tej samej karty, tylko gdy ktoś słucha), kto może słuchać — kolumna tabeli klientów (Ustawienia: sam poziom; strefa: tylko z przełącznikiem „Mikrofon”), pierścień w drugą stronę, znak „mikrofon włączony” rysowany przez K1, cisza przy ekranie zabezpieczeń; KOREKTOR i SCENA w obie strony zwalniają numery typów | Przyjęty; wdrożenie — kroki 1–2 planu 0.13.5 |
