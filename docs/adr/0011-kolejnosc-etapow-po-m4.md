# 0011. Kolejność etapów po M4

- Status: Zastąpiony przez 0019 (kolejność etapów) i 0035 (mapa drogi według wersji)
- Data: 2026-11-06
- Dotyczy: SPEC §14 (kamienie milowe), RYZYKA R1, R6, R15; właściciel poprosił o zmianę planu
  według oceny Claude

## Kontekst
Po M4 K1 ma pulpit i aplikacje, ale:
- **nic nie przetrwa wyłączenia** (`/dane_uzytkownika` w RAM, brak sterownika dysku) — system jest
  pokazem, a nie narzędziem (R6);
- na prawdziwym sprzęcie brakuje USB i dysków, a w maszynach wirtualnych często domyślna jest mysz
  USB (tablet) (R15, R22);
- **obraz rdzenia nie jest weryfikowany** — ryzyko wysokie (R1), a dm-verity jest stosunkowo małym
  krokiem, gdy obraz i tak trafia na dysk/pendrive;
- **największe ryzyko całego planu to przeglądarka** (M7–M8, M11): czy Firefox albo WPE da się
  uruchomić w strefie bez przestrzeni nazw użytkownika, z renderowaniem programowym i oddawaniem
  klatek do K1. Dziś to założenie, nie fakt;
- kod urósł (ok. 8 tys. linii); części z danymi od innych procesów (ekran, nadzorca) warto
  sprawdzić, zanim dojdą hasła i klucze szyfrowania (M5).

## Decyzja
Numery etapów zostają (dokumenty i testy się do nich odwołują), zmienia się **kolejność**
i dochodzą trzy krótkie etapy pośrednie:

1. **M4.1 Porządki i audyt** — celowany przegląd bezpieczeństwa (nadzorca, ekran, kanały, klatka)
   z poprawkami; podział największych plików (`src/ekran/main.c`, program testowy), usunięcie
   powtórzeń; CI uruchamiane etykietą na PR-ach.
2. **M5 Dane i sprzęt** (rozszerzony): sterowniki dysków (virtio, AHCI/SATA, NVMe), partycja danych,
   Blob Vault z szyfrowaniem, opcjonalne hasło, trwałe notatki i ustawienia; USB HID (klawiatura,
   mysz, tablet z położeniem bezwzględnym), podłączanie w trakcie pracy; pierwszy start z pendrive'a
   na laptopie właściciela.
3. **M5.1 Próba przeglądarki** (wycinek M7/M8, bez sieci): minimalna strefa zgodności, Firefox
   i WPE WebKit uruchomione w izolacji, strona testowa wyrenderowana i pokazana w oknie K1. Wynik →
   ADR (Firefox czy WPE, przestrzenie nazw użytkownika tak/nie). Jeśli żadna droga nie działa —
   zmiana planu M7–M8 i M11, zanim w nie zainwestujemy.
4. **M5.2 dm-verity** (wycinek M10): obraz rdzenia weryfikowany przy starcie; podmieniony obraz =
   kontrolowane wyłączenie. Secure Boot, UKI i aktualizacje A/B zostają w M10.
5. Dalej jak w SPEC: M6 dekodery → M7 sieć i strefa → M8 przeglądarka → M9 Gatekeeper →
   M10 (Secure Boot, UKI, A/B, recovery) → M11 mikro-VM → M12 audyt.

## Konsekwencje
- Po M5 K1 nadaje się do prostego, codziennego testowania (notatki zostają) i startuje na sprzęcie.
- Najwyższe ryzyko (R1) maleje wcześniej; największa niewiadoma (przeglądarka) zostaje sprawdzona,
  zanim powstanie sieć i Gatekeeper.
- Pełny audyt nadal w M12; M4.1 to przegląd celowany, nie zastępstwo.
- KVM: M5.1 da się zrobić bez KVM (zaplecze A); M11 wymaga KVM — u właściciela albo w CI.

## Rozważane alternatywy
- Kolejność z SPEC bez zmian — długo bez trwałych danych i z niesprawdzonym założeniem o przeglądarce.
- Nowa numeracja etapów — zamieszanie w dokumentach, testach i rozmowach.
