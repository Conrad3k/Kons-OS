# 0019. Przegląd po 0.5.9 i nowy plan etapów

- Status: Przyjęty (kolejność po 0.7.3 zmieniona: ADR 0026; mapa drogi według wersji: ADR 0035)
- Data: 2026-11-07
- Dotyczy: ADR 0011 (kolejność etapów — **zastępuje ją od M5.2**), SPEC §14, ADR 0018

## Kontekst
Od 0.5.9 K1 instaluje się na dysk laptopa i tam trzyma dane. Właściciel poprosił o krótką
weryfikację, „czy repo i architektura to nie syf”, i o dostosowanie planu („be thorough in your
analysis and bold in your recommendations”). Stan: ~11,7 tys. wierszy C (bez Monocyphera),
14 programów, 19 ADR, 11 dokumentów komponentów, testy: host (ASan/UBSan, fuzzing), w systemie
(klatki, próby), QEMU (zrzuty, ponowne starty, instalacja), powtarzalne obrazy, CI z wydaniami.

## Przegląd — co jest dobre
- **Granice zaufania są prawdziwe, nie dekoracyjne:** każdy proces ma własny UID, białą listę
  wywołań, Landlock bez ścieżek, cgroup; urządzenia otwiera tylko nadzorca i przekazuje
  deskryptory; jeden format komunikatu (32 B) sprawdzany w jednym miejscu z kierunkiem kanału,
  nadawca zawsze po `SO_PASSCRED`. Instalator (ADR 0018) jest dowodem, że wzorzec się skaluje:
  najbardziej niebezpieczna operacja dostała najmniej uprawnień.
- Mało zależności (jądro, musl, Monocypher, pliki startowe z Ubuntu), powtarzalne obrazy, decyzje
  zapisane w ADR, ryzyka w rejestrze, każda zmiana przechodzi testy w emulatorze.

## Przegląd — co jest „syfem” (dług techniczny, od najważniejszego)
1. **`src/nadzorca/main.c` ma 1562 wiersze** i rośnie z każdym etapem (profile, usługi, partycja
   danych, stan skarbca, bateria, Zasoby, instalator, kanał testowy). To proces z pełnymi
   uprawnieniami — powinien być **najmniejszy**, a jest największy. Stan startu to zbiór flag
   (`skarb_czekal`, `stan_skarbca`, `przelaczanie`, `skarb_bez_dysku`) i limitów czasu, nie
   jawna maszyna stanów.
2. **Odpytywanie zamiast zdarzeń:** nowe urządzenia wejścia (co 2 s), partycja danych (co 2 s
   przez 2 min), bateria (co 15 s), czekanie na pendrive po starcie — heurystyki czasowe, które
   na X230 już raz zawiodły (0.5.8). Jądro podaje te zdarzenia przez netlink (uevent).
3. **Powielony kod:** pętla „odbierz → sprawdź UID → sprawdź komunikat → typ” występuje 18 razy;
   `teraz()` w 4 plikach; w testach QEMU (≈1600 wierszy powłoki z wklejonym Pythonem) funkcje
   `czekaj/monitor/idz/klik` skopiowane w 4 skryptach.
4. **Czas testów:** pełny zestaw to ~35–40 min w emulacji (TCG) i rośnie z każdym startem QEMU;
   testy są niezależne, a uruchamiane po kolei.
5. **Dokumentacja dryfuje:** mapa repozytorium w `ARCHITEKTURA.md` nie miała skarbca i ekranu
   hasła (poprawione w 0.5.9); część opisów zostaje w stanie „do M5e”.
6. **Bezpieczeństwo startu:** Secure Boot musi być wyłączony, jądro i rdzeń nie są sprawdzane
   (dm-verity, podpisy — M10). Odkąd K1 jest **zainstalowany i aktualizowany**, to już nie jest
   „później”: aktualizacja z dowolnego pendrive'a zapisuje dowolny system na dysk.

Werdykt: **architektura jest zdrowa** (rzadko spotykana dyscyplina jak na projekt tej
wielkości); dług jest skupiony w trzech miejscach (nadzorca, odpytywanie, kod testów) i da się go
spłacić jednym etapem porządków, zanim urośnie.

## Decyzja — nowy plan (odważny)
1. **0.6.0 = zamknięcie M5 „Fundament”** (zamiast samego M5b):
   - nadzorca w modułach (`uslugi`, `profile`, `kanaly`, `skarbiec`, `zasoby`, `instalacja`,
     `bateria`, `dyski`), `main.c` < 600 wierszy, **jawna maszyna stanów startu**;
   - **uevent (netlink) zamiast odpytywania**: wejście, dyski (K1-DANE), bateria/zasilacz — M5b
     wchodzi tutaj (także tablet USB w VM);
   - wspólny moduł odbioru komunikatów (`k1_kanal_odbierz_od`) i wspólna biblioteka testów QEMU
     (`testy/qemu/k1qemu.py`); testy QEMU równolegle (KVM w CI był już od M4 — krok „Włącz KVM”;
     lokalny kontener nie ma KVM, więc tam testy zostają po kolei);
   - historia baterii między uruchomieniami (skarbiec, `K1_OB_BATERIA`).
2. **M6 „Bezpieczne aktualizacje”** — z M10 i M5.2 wyciągnięte **teraz**, bo K1 jest na dysku:
   - podpisane wydania (własny klucz Ed25519, klucz publiczny w systemie); Instalator **odmawia**
     aktualizacji niepodpisanym albo starszym systemem;
   - dm-verity dla rdzenia (wykrycie podmiany = bezpieczne zatrzymanie);
   - A/B w partycji K1-EFI (512 MiB ma miejsce na dwa systemy) z powrotem do poprzedniego, gdy nowy
     nie wystartuje;
   - **Secure Boot z powrotem włączony**: własny klucz wpisany raz przez MokManager shima (krok dla
     właściciela przy klawiaturze) i podpisane jądro/UKI.
3. **M7 „Sieć”**: najpierw **kabel** (X230: Intel 82579LM, sterownik `e1000e`, bez firmware) i
   DHCP w osobnej klatce, czas z sieci (potrzebny do TLS); potem Wi-Fi (`iwlwifi` + firmware —
   osobny ADR). Bez sieci przeglądarka nie ma sensu dla właściciela.
4. **M8 „Przeglądarka”** (rozszerzone przez ADR 0020 do M8 „Strefy”: każda obca aplikacja w mikro-maszynie) = dawne M5.1 + M8: próba Firefox/WPE w strefie **z siecią**, wybór w ADR,
   sesje według decyzji właściciela.
5. **M9 „Pliki”** = Gatekeeper + dawne M6 (dekodery w izolacji): aplikacja Pliki w skarbcu (dysk jest
   duży od 0.5.9), podgląd obrazów przez jednorazowy dekoder, pobieranie przez Gatekeepera.
6. **M5.3 „Laptop” (małe kroki między etapami, gdy właściciel zgłosi):** `i915` (page flip, jasność),
   klawisze Fn, dźwięk (`snd_hda_intel`), touchpad z przewijaniem.
7. Bez zmian na końcu: M11 (mikro-VM dla przeglądarki), M12 (audyt).

## Konsekwencje
- Kolejność: 0.6.0 Fundament → M6 Bezpieczne aktualizacje → M7 Sieć → M8 Przeglądarka → M9 Pliki →
  M10 (to, co zostanie: recovery, licznik wersji w TPM) → M11 → M12. Numery dawnych etapów zostają
  w historii; `docs/DO_ZROBIENIA.txt` pokazuje nową kolejność.
- Pierwsza przeglądarka później niż w ADR 0011 (po sieci) — w zamian system, który właściciel ma
  na dysku, dostaje bezpieczne aktualizacje, zanim pojawi się w nim cokolwiek z internetu.
- Ryzyko: refaktoryzacja nadzorcy może wprowadzić błędy — dlatego w tym samym etapie co szybsze
  testy (KVM), a każdy krok przechodzi pełny zestaw.
