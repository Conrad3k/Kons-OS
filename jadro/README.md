# Jądro K1

| Plik | Co to |
|---|---|
| `wersja.env` | wersja jądra, przypięta suma SHA-256 tarballa, adres pobierania |
| `k1.config` | konfiguracja: fragment nakładany na `make allnoconfig` (wszystko spoza fragmentu wyłączone) |
| `cmdline` | linia poleceń jądra testów (`bzImage` w QEMU; z `console=ttyS0` — log na porcie szeregowym) |
| `cmdline-wydanie` | linia poleceń wydania, wbudowana w podpisany `k1.efi` (ADR 0022; od 0.13.0 krok 4, ADR 0059: `console=ttynull` — nic na port szeregowy; testowa kopia `k1-test.efi` dostaje na końcu `console=ttyS0 panic=-1`) |
| `k1-zamkniete.config` | to, czym `k1.efi` różni się od jądra testów: wbudowana linia poleceń i rdzeń, SBAT, bez `mac80211_hwsim` |
| `klucze/` | publiczne klucze PGP do sprawdzania podpisów kernel.org |

Budowa: `scripts/zbuduj-jadro.sh` (pobiera, sprawdza sumę i podpis, konfiguruje, sprawdza fragment,
kompiluje). Decyzje i uzasadnienia: `docs/adr/0003-jadro-wersja-i-konfiguracja.md`,
`docs/komponenty/jadro.md`.

## Robot (od 0.10.5, ADR 0047)
Kroki 1–2 i 6 poniżej robi codziennie **robot** (`scripts/robot.sh` w `.github/workflows/robot.yml`; opis
`docs/komponenty/robot.md`): ta sama linia — sam; nowsza linia długoterminowa — po 7 dniach spokoju i tylko gdy
`scripts/porownaj-jadro.sh` nie ma pytań (opcje, które nowe jądro włączyłoby samo, a fragment nie: każda musi być we
fragmencie albo w `k1-odrzucone.txt` / `strefa-odrzucone.txt` z powodem — rozstrzyga człowiek, raz). Ręcznie według tej
procedury tylko w kroku planu albo gdy robot założył Issue „robot” — nigdy równolegle z otwartym PR-em robota.

| Plik | Co to |
|---|---|
| `k1-odrzucone.txt`, `strefa-odrzucone.txt` | opcje „domyślnie tak”, których K1 świadomie nie chce (CONFIG_X, dwie spacje, powód); zmienia tylko człowiek |

## Aktualizacja jądra (procedura)
Poprawki bezpieczeństwa jądra mają najwyższy priorytet (SPEC §10).
1. Sprawdź najnowsze wydanie linii, na której jest K1 (od 0.10.5: 7.3 — linia „stable”, nie długoterminowa; T20 planu
   0.10.5: zmiana linii — najnowsza długoterminowa, a gdy jej nie ma, najnowsza stabilna, nigdy rc):
   `curl -s https://www.kernel.org/releases.json`. Nowa linia: najpierw `scripts/porownaj-jadro.sh` dla obu jąder.
2. Pobierz `sha256sums.asc` z `https://cdn.kernel.org/pub/linux/kernel/v<N>.x/` (7.x: `v7.x`), sprawdź podpis
   (`gpg --verify`, klucz autosignera z `klucze/`) i przepisz sumę nowego `linux-X.Y.Z.tar.xz`
   do `wersja.env`.
3. `scripts/zbuduj-jadro.sh` — sprawdzi sumę i podpis PGP wydania (klucz Grega Kroah-Hartmana)
   oraz zgodność konfiguracji z `k1.config`.
4. `scripts/sprawdz-kspp.sh` — nowe zalecenia KSPP? Uzupełnij `docs/komponenty/jadro.md`.
5. `scripts/zbuduj-obraz.sh && scripts/test-m1.sh` (oraz testy kolejnych etapów).
6. Wpis w `CHANGELOG.md`, podbicie PATCH w `VERSION`.
