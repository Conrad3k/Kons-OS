# Jądro K1

| Plik | Co to |
|---|---|
| `wersja.env` | wersja jądra, przypięta suma SHA-256 tarballa, adres pobierania |
| `k1.config` | konfiguracja: fragment nakładany na `make allnoconfig` (wszystko spoza fragmentu wyłączone) |
| `cmdline` | linia poleceń jądra (od M10 wbudowana w podpisany UKI) |
| `klucze/` | publiczne klucze PGP do sprawdzania podpisów kernel.org |

Budowa: `scripts/zbuduj-jadro.sh` (pobiera, sprawdza sumę i podpis, konfiguruje, sprawdza fragment,
kompiluje). Decyzje i uzasadnienia: `docs/adr/0003-jadro-wersja-i-konfiguracja.md`,
`docs/komponenty/jadro.md`.

## Aktualizacja jądra (procedura)
Poprawki bezpieczeństwa jądra mają najwyższy priorytet (SPEC §10).
1. Sprawdź najnowsze wydanie linii LTS: `curl -s https://www.kernel.org/releases.json`.
2. Pobierz `sha256sums.asc` z `https://cdn.kernel.org/pub/linux/kernel/v6.x/`, sprawdź podpis
   (`gpg --verify`, klucz autosignera z `klucze/`) i przepisz sumę nowego `linux-X.Y.Z.tar.xz`
   do `wersja.env`.
3. `scripts/zbuduj-jadro.sh` — sprawdzi sumę i podpis PGP wydania (klucz Grega Kroah-Hartmana)
   oraz zgodność konfiguracji z `k1.config`.
4. `scripts/sprawdz-kspp.sh` — nowe zalecenia KSPP? Uzupełnij `docs/komponenty/jadro.md`.
5. `scripts/zbuduj-obraz.sh && scripts/test-m1.sh` (oraz testy kolejnych etapów).
6. Wpis w `CHANGELOG.md`, podbicie PATCH w `VERSION`.
