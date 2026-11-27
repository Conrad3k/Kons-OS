# 0059. Obraz wydania (0.13.0): nic na porcie szeregowym, ekran awarii, raport z Ustawień, udawane karty Wi-Fi tylko w testach

- Status: Przyjęty, wdrożony (plan 0.13.0, krok 4)
- Data: 2026-11-27
- Dotyczy: ADR 0022 (zamknięte jądro `k1.efi` — **uzupełniony**: dwie linie poleceń, testowa kopia `k1-test.efi`),
  ADR 0058 (tryb ratunkowy — raport ten sam, teraz także z Ustawień), ADR 0041 (Wi-Fi — `mac80211_hwsim` tylko w jądrze
  testów), niezmiennik 5 z `.claude/skills/kons-dev/SKILL.md` (PID 1 przy błędzie krytycznym — **zmieniony**: kończy się,
  jądro panikuje); plan `docs/PLAN-0.13.0.txt` (krok 4, D4, D5, T8, T9); Issue #234 (p. 3), #231 (p. 1), #237 (p. 1, 2);
  RYZYKA R10, R17, R21; opisy: `docs/komponenty/jadro.md`, `init.md`, `initramfs.md`, `nadzorca.md`, `aplikacje.md`

## Kontekst
Do 0.12.3 `k1.efi` wydania miał w wbudowanej linii poleceń `console=ttyS0`: cały log jądra i K1 szedł na port szeregowy
(R10, R21 — kto ma dostęp do portu albo karty z portem, czyta przebieg startu i pracy). Błąd krytyczny startu (np. obraz
rdzenia nie przechodzi dm-verity) było widać tylko tam: na ekranie laptopa zostawał ekran startowy, a po 5 s komputer się
wyłączał (R17, L16). Jądro wydania miało `mac80211_hwsim` (udawane karty Wi-Fi do testów, wyłączone tylko parametrem
`radios=0`) — kod osiągalny z zewnątrz bez pożytku dla użytkownika (#234 p. 3, L17).

Decyzje właściciela (2026-11-27): **D4: A** — logi wydania zostają w pamięci, „Zapisz raport” na pendrive, gdy trzeba
zgłosić błąd; **D5: A** — szybka grafika i filmy kartą w sesji Ogólnej bez zmian (domyślnie włączone; Bank i Prywatna —
bez, jak od 0.10.9), ryzyko R100 czeka na audyt 0.14.

## Decyzja
1. **Dwie linie poleceń, jedno jądro (T8).** `jadro/cmdline` — jądro testów (`bzImage` w QEMU, `console=ttyS0`);
   `jadro/cmdline-wydanie` — wbudowana w `k1.efi`: `console=ttynull panic=60 oops=panic k1.rdzen=wbudowany`
   (`scripts/zbuduj-k1efi.sh` odmawia, gdy jest w niej `console=ttyS…`). Ten sam skrypt składa też **`k1-test.efi`**: ta
   sama konfiguracja z dopiskiem `console=ttyS0 panic=-1` na końcu linii (późniejsze wygrywają) — **kopia testowa wydania**, tylko do obrazów
   `k1-<wersja>-test.iso` i `k1-<wersja>-test-usb.img` (`zbuduj-iso.sh --test`, `zbuduj-usb.sh --test`: bez paczki,
   ogłoszeń i `.xz`). Testy QEMU startujące z ISO/USB (`test-m4`, `-m5`, `-m5f`, `-m6`, `test-ratunek`) czytają log
   z tych obrazów; wydanie (GitHub Releases, artefakt PR-a) ma tylko `k1.efi`. Druga budowa różni się samą linią: kompiluje
   pliki czytające `CONFIG_CMDLINE` i łączy jądro jeszcze raz (ok. 1 min), bez drugiego katalogu jądra.
2. **ttynull.** `CONFIG_NULL_TTY=y`: `/dev/console` wydania istnieje (deskryptory 0–2 PID 1 i usług, jak dotąd), ale pisze
   donikąd. Bez `console=` jądro samo włączyłoby pierwszą konsolę z urządzeniem — port szeregowy; dlatego `console=ttynull`
   wprost. Log K1 zostaje w pamięci nadzorcy (`logi.c`, 40 KiB, od kroku 3): od pierwszego wiersza nadzorcy, wiersze
   initramfs i init idą tylko na konsolę (doszły do nadzorcy = zadziałały).
3. **Raport z Ustawień (D4: A).** Ustawienia → Informacje: „Zapisz raport na pendrive” — ten sam raport co w trybie
   ratunkowym (`src/nadzorca/raport.c`, wydzielony z `ratunek.c`; nagłówek „Kons K1: raport”, wiersz „Tryb: ratunkowy” albo
   „zwykły start”). Kanał `KANAL_AKTUALIZACJI` (fd 7 Ustawień): `INST_START` obiekt = 0, a = `K1_INST_RAPORT` (nadzorca
   przyjmuje tylko od Ustawień — `inst_prawa.c`), wynik `INST_WYNIK` b = `K1_INST_RAPORT`, c = numer pliku 0..9. Zapisany
   raport nie daje Ustawieniom prawa do ponownego uruchomienia (jak skończona aktualizacja). Tekst stanu raportu wspólny
   dla ekranu ratunkowego i Ustawień: `k1_raport_opis` (przybornik).
4. **Ekran awarii (R17).** `CONFIG_DRM_PANIC=y`, `DRM_PANIC_SCREEN="kmsg"`, kolory ciemnego motywu K1 (tło 0x262523,
   tekst 0xeeebe5): panika jądra rysuje ostatnie wiersze logu jądra na obrazie (simpledrm, i915, bochs, virtio — nie
   vmwgfx). `k1_awaria` (tylko PID 1: initramfs i init) wypisuje błąd jak dotąd, potem **czyści log jądra**
   (`SYSLOG_ACTION_CLEAR` — ekran paniki zaczyna od tego miejsca), wpisuje do `/dev/kmsg` (initramfs montuje devtmpfs jako
   pierwszy krok) siedem wierszy po polsku **bez polskich liter** (czcionka jądra ich nie ma; `k1_ascii`, test
   `test_log`): „Kons K1 <wersja> nie moze wystartowac”, „Powod (<program>): <opis>”, co zrobić (menu po dwóch nieudanych
   startach: poprzednia wersja albo tryb ratunkowy; Esc przy starcie), „Zrob zdjecie tego ekranu” — i **kończy się**
   (kod 1). Koniec PID 1 to panika jądra: jej ekran stoi do `panic=60`, potem jądro uruchamia komputer ponownie; GRUB liczy
   nieudany start (krok 3) i po drugim pokazuje menu. Do 0.12.3: 5 s na porcie szeregowym i wyłączenie — **niezmiennik 5
   zmieniony**: PID 1 dalej nie kończy się sam z siebie, ale błąd krytyczny kończy go celowo. Kodu QR nie ma
   (`DRM_PANIC_SCREEN_QR_CODE` wymaga Rusta w jądrze — nowa zależność). Opis błędu (program + zdanie) jest kodem błędu.
5. **`mac80211_hwsim` tylko w jądrze testów (T9).** `jadro/k1-zamkniete.config`: `# CONFIG_MAC80211_HWSIM is not set` —
   `k1.efi` i `k1-test.efi` go nie mają; `bzImage` (test-wifi, program testowy) — ma. Drugiej konfiguracji jądra w CI nie
   trzeba: `k1.efi` już dziś różni się od `bzImage` fragmentem zamkniętego jądra.
6. **IOMMU (#237 p. 1) i szybka grafika (#231 p. 1, D5: A)** — bez zmian w kodzie (`INTEL_IOMMU_DEFAULT_ON`,
   `IOMMU_DEFAULT_DMA_STRICT` są od 0.10.9); log DMAR na X230 i IdeaPadzie — lista „co sprawdzić” (krok 8).

## Konsekwencje
- Port szeregowy wydania milczy: zgłoszenie błędu = raport z Ustawień albo z trybu ratunkowego, zdjęcie ekranu awarii.
- Ekran awarii wymaga obrazu, który sterownik już wyświetla: obraz z firmware (simpledrm przy UEFI) albo i915, który
  przejmuje obraz BIOS-u; na `bochs` bez ustawionego trybu (QEMU z SeaBIOS) przed ekranem K1 nie ma na czym rysować —
  test idzie przez UEFI. Na X230 i IdeaPadzie (i915) — NIEPEWNE, lista „co sprawdzić”.
- Panika z oops jądra też pokazuje ekran (log jądra), także po starcie pulpitu; 60 s, potem ponowne uruchomienie.
- Budowa: drugi `k1.efi` (ok. 1 min) i dwa obrazy testowe (ok. 1 min); CI wydania buduje je też (testy na PR-ze ich
  potrzebują, budowa na `main` jest ta sama).
- „Później”: jądro wydania bez sterownika portu szeregowego (8250) — dziś zostaje (tylko nie jest konsolą), bo `k1-test.efi`
  ma tę samą konfigurację.

## Testy
- `scripts/test-wydanie.sh` (nowy): ekran awarii z linią poleceń wydania (zrzut, NEGATYWNE: port szeregowy pusty), obraz
  USB wydania do pulpitu (zrzut, port pusty), tryb ratunkowy z wydania → raport z logiem z pamięci, Ustawienia →
  Informacje → raport (NEGATYWNE: bez pendrive'a).
- `test-m1`: negatywne starty kończą się ekranem awarii (opis K1 w logu jądra, „Attempted to kill init”, żadna inna
  panika). Na hoście: `test_log` (`k1_ascii`), `test_komunikat` (`INST_START`/`INST_WYNIK` raportu), `test_inst_prawa`
  (raport tylko z Ustawień). `test-ratunek` w pełnym komplecie CI (krok 3 uruchamiał go tylko lokalnie).
