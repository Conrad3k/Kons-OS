# 0015. Blob Vault: własny format na surowej partycji, szyfrowanie w przestrzeni użytkownika

- Status: Przyjęty
- Data: 2026-11-07
- Dotyczy: SPEC §5 (`/dane_uzytkownika`), §9 (Blob Vault), ADR 0004, ADR 0013, etapy M5c–M5e

## Kontekst
SPEC §5 przewidywał zaszyfrowane `/dane_uzytkownika` przez fscrypt albo dm-crypt (decyzja w ADR).
Rozpoznanie M5 (`docs/M5-ROZPOZNANIE.txt`, pkt 5) porównało trzy warianty: A) dm-crypt + ext4,
B) fscrypt na ext4, C) własny format na surowej partycji z szyfrowaniem w przestrzeni użytkownika.

Decyzje właściciela (2026-11-07): jedno konto; hasło opcjonalne (zapomniane = nowe konto); bez
hasła dane szyfrowane, ale bez ochrony przed kradzieżą nośnika — zaakceptowane; dane na X230 na
pendrivie (obraz z partycją na dane). ThinkPad X230 ma TPM 1.2 — na razie bez TPM.

## Decyzja
**Wariant C.** Partycja GPT „K1-DANE” (ADR 0013) bez systemu plików; jej deskryptor ma tylko usługa
`skarbiec` (od nadzorcy). Format (`src/skarbiec/magazyn.c`, opis bajt po bajcie:
`docs/komponenty/skarbiec.md`):
- **Nagłówek** w dwóch kopiach (bloki 0 i 1, generacja: nowsza wygrywa, uszkodzona → poprzednia).
  Pola sprawdzane bez klucza (zakresy, zera w rezerwie), potem **MAC** (BLAKE2b z kluczem
  pochodnym od klucza głównego).
- **Klucz główny** 256 bit z `getrandom`, w nagłówku **owinięty** (XChaCha20-Poly1305) kluczem
  z **Argon2id(hasło, sól)** albo — bez hasła — z BLAKE2b(stała, uuid, sól) (szyfrowanie bez ochrony
  przed kradzieżą, uczciwie opisane w Ustawieniach). Zmiana/wyłączenie hasła = nowa sól i nowe
  owinięcie zapisane w **obu** kopiach nagłówka (stare hasło przestaje działać).
- **Rekordy** (nowa wersja obiektu albo usunięcie) dopisywane w aktywnym obszarze, każdy
  zaszyfrowany i uwierzytelniony (XChaCha20-Poly1305, losowy nonce), a jako dane dodatkowe:
  identyfikator magazynu, epoka obszaru, numer obszaru, pozycja i długość — rekordu nie da się
  zmienić, przesunąć ani wkleić z innego magazynu. Treść: typ, właściciel (profil usługi), nazwa,
  czas, długość, BLAKE2b treści, dane. Obiekty są niezmienne (nowa wersja zastępuje starą).
- **Kolejność zapisu:** rekord → `fdatasync` → nagłówek (następna generacja) → `fdatasync`.
  Przerwany zapis = poprzedni stan (test). **Kompaktowanie:** gdy obszar się zapełni, żywe obiekty
  kopiowane są do drugiego obszaru z nową epoką, potem przełączenie nagłówka.
- **Formatowanie tylko pustej partycji** (pierwszy MiB z samych zer) — obcych danych nigdy nie
  nadpisujemy (test).
- Kryptografia: **Monocypher 4.0.3** (jeden plik C, BSD-2/CC0, w repozytorium
  `src/zewnetrzne/monocypher/`, sumy w `docs/ZALEZNOSCI.md`). Własnych prymitywów nie piszemy.
- Argon2id: pamięć dobierana do RAM (≈ RAM/16, 16–256 MiB; X230 z 8 GB → 256 MiB), 3 przejścia;
  parametry zapisane w nagłówku.

## Konsekwencje
- Jądro nie parsuje danych z nośnika (bez ext4, device-mappera) — mniej kodu jądra wystawionego na
  podmieniony pendrive (RYZYKA R34); cały parser jest nasz, testowany na hoście (ASan/UBSan,
  `test_magazyn`, `fuzz_magazyn` — także nagłówki z poprawnym MAC, jak u napastnika bez hasła).
- Zmieniony bajt na dysku = wykryty błąd (dm-crypt dałby po cichu śmieci).
- **Bez hasła** ktoś z nośnikiem odczyta i może podmienić dane (zna klucz) — decyzja właściciela;
  z hasłem: poufność i integralność, z wyjątkiem **cofnięcia całego nośnika** do starszej kopii
  (bez TPM/licznika sprzętowego nie do wykrycia) — RYZYKA R38.
- Widoczny jest rozmiar zajętej części dziennika (nie nazwy ani liczba obiektów w treści).
- Nasza odpowiedzialność: spójność po awarii zasilania, kompaktowanie, wydajność (dla notatek
  i ustawień bez znaczenia). Jeden obiekt ≤ 1 MiB, ≤ 256 żywych obiektów (na M5 wystarczy).
- SPEC §5: `/dane_uzytkownika` nie jest już zaszyfrowanym systemem plików — dostęp do danych tylko
  przez usługę `skarbiec` (obiekty), zgodnie z §9. Korekta SPEC: `[korekta: ADR 0015]`.

## Rozważane alternatywy
- **A) dm-crypt + ext4:** szybkie i sprawdzone, ale jądro parsuje ext4 z obcego nośnika, brak
  wykrywania zmian (bez dm-integrity), potrzebny mkfs i konfiguracja device-mappera bez narzędzi.
- **B) fscrypt:** to samo ryzyko ext4, widać liczbę i rozmiary plików.
- **Własna kryptografia / libsodium:** własnej nie piszemy; libsodium jest dużo większe niż
  potrzebujemy (Monocypher ma dokładnie XChaCha20-Poly1305, BLAKE2b i Argon2id).
