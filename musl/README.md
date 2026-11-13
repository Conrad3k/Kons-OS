# Biblioteka C (musl) dla programów K1

Od 0.9.0 (krok 4 planu 0.9.0, korekta ADR 0007) programy K1 są budowane z **naszego** musl, a nie z pakietu
`musl-dev` Ubuntu: przypięta wersja, sprawdzone źródło, poprawki bezpieczeństwa wydane po tej wersji.
Buduje go `scripts/zbuduj-musl.sh` do `build/musl/` (z niego `make` bierze kompilator `build/musl/bin/musl-gcc`).

| Plik | Co jest w środku |
|---|---|
| `wersja.env` | wersja, SHA-256 archiwum, odcisk klucza PGP wydawcy, źródła (po kolei) |
| `klucze/musl.asc` | klucz publiczny wydań musl („musl libc &lt;musl@libc.org&gt;”, `8364 8929 0BB6 B70F 99FF DA05 56BC DB59 3020 450F`) — z pakietu źródłowego Debiana/Ubuntu (`debian/upstream/signing-key.asc`) |
| `latki/` | łatki nakładane po kolei (`patch -p1`) — niżej |

## Źródło i sprawdzenie
- Archiwum: oryginalne wydanie `musl-1.2.5.tar.gz`. Źródła po kolei: archiwum Ubuntu (pakiet źródłowy `musl`, plik
  `musl_1.2.5.orig.tar.gz` — to samo wydanie bajt w bajt), lustro Ubuntu na kernel.org (gdy nazwy Ubuntu nie odpowiadają),
  `musl.libc.org` (kontener deweloperski widzi tylko pierwsze; CI — wszystkie).
- Skrypt sprawdza **SHA-256** z `wersja.env` i **podpis PGP wydawcy** (`.asc` z tego samego źródła) — tylko klucz
  o odcisku z `wersja.env`. Inna suma albo zły podpis = budowa przerwana.
- Sprawdzone 2026-11-13: suma `a9a118bb…c75e4` zgodna z plikiem `.dsc` Ubuntu (1.2.5-3build1), podpis
  `VALIDSIG 836489290BB6B70F99FFDA0556BCDB593020450F` (wydanie z 2024-03-01).

## Łatki (`latki/`)
| Łatka | Skąd | Po co |
|---|---|---|
| `0001-iconv-euc-kr-CVE-2025-26519.patch` | musl, commit `e5adcd97` (Rich Felker, 2025-02-09), w pakiecie Debiana 1.2.5-2 | CVE-2025-26519: zła granica w dekoderze EUC-KR (`iconv`) — odczyt i zapis poza tablicą |
| `0002-iconv-utf8-CVE-2025-26519.patch` | musl, commit `c47ad25e` (2025-02-12), Debian 1.2.5-2 | to samo CVE: wzmocnienie wyjścia UTF-8 w `iconv` |
| `0003-musl-gcc-static-pie.patch` | Debian (`static-pie.patch`, od 1.2.4-1; propozycja z listy musl 2020) | `musl-gcc` umie `-static-pie` (rcrt1.o) — K1 buduje tylko statyczne PIE (SPEC §11) |

Poprawki bezpieczeństwa wydane po 1.2.5 — według pakietu Debiana/Ubuntu (dziennik zmian do 1.2.5-3build1, 2026-02):
**tylko CVE-2025-26519**. Programy K1 i tak nie używają `iconv`. Łatki `renameat2` z Debiana (nowa funkcja) nie bierzemy.

## Zmiana wersji albo łatki
1. Nowe archiwum i jego `.asc` (archiwum Ubuntu albo musl.libc.org); `gpg --verify` kluczem z `klucze/`.
2. `wersja.env`: wersja, SHA-256, źródła; łatki: usuń te, które weszły do wydania, dodaj nowe (z nagłówkiem: skąd).
3. `scripts/zbuduj.sh` przebuduje musl (znacznik wejścia) i wszystkie programy; pełny `scripts/testy.sh` — nowa wersja
   może wołać jądro inaczej (RYZYKA R24: klatki); `scripts/sprawdz-powtarzalnosc.sh`.
4. `docs/ZALEZNOSCI.md` (wersja, łatki), ten plik.
