# Komponent: Raptor Bridge (most strefy do świata przez K1)

| | |
|---|---|
| **Status** | ZAIMPLEMENTOWANE (0.10.0 krok 2, ADR 0043; strefa pobierania — 0.10.5 krok 2, ADR 0046); PRZETESTOWANE (0.10.5 krok 5): na hoście (`test_lacznik`, `fuzz_lacznik`, `fuzz_virtq`, `fuzz_vsock` — cała droga pakietu ze stanem połączeń), w QEMU **w strefie** (`scripts/test-most.sh`: prawdziwe bajty przez cały most do serwera na „routerze”, także w emulacji bez KVM) |
| **Części** | przekaźnik `src/strefa/przekaznik/main.c` (w strefie) · vsock `src/strefa/vsock.c` · łącznik `src/strefa/lacznik.c` (protokoły, czyste) + `src/strefa/polaczenia.c` (stan połączeń) · nadzorca `src/nadzorca/strefy_siec.c` (zasada D3, gniazda) · reguła D3 `k1_siec_dla_strefy` (`src/wspolne/k1_siec.c`) |
| **Decyzje** | ADR 0043 (łącznik w VMM, gniazda tylko u nadzorcy), ADR 0039 (sieć: IPv4, gniazdo otwiera nadzorca), D3 planu 0.10.0 (internet i router, bez reszty sieci domowej), D9 planu 0.10.5 (nazwa) |

## W skrócie (dla właściciela)
Strefa — mikro-maszyna z przeglądarką albo z pobieraniem aktualizacji — **nie ma karty sieciowej**. Jedyna jej droga do
internetu to Raptor Bridge: przeglądarka w strefie mówi „połącz mnie z taką stroną”, a K1 sprawdza, czy wolno, sam otwiera
połączenie i przenosi bajty w obie strony. Strefa nigdy nie dotyka sieci domowej (drukarki, routera poza nim samym, innych
komputerów) i nie widzi, co K1 robi dalej. K1 nie czyta treści stron (to i tak zaszyfrowane HTTPS), patrzy tylko, **dokąd**
strefa chce się połączyć. Każda część mostu zakłada, że strefa może być wroga: zły pakiet kończy tylko tę jedną maszynę,
nigdy K1.

## Droga bajtu
```
 STREFA (mikro-maszyna, wroga)                     K1: VMM tej strefy (klatka, własny UID)            K1: nadzorca (root)
 ┌───────────────────────────┐                     ┌──────────────────────────────────────┐          ┌──────────────────┐
 │ Firefox / curl            │                     │ vsock.c  — pakiety, kredyt, porty    │          │ strefy_siec.c    │
 │   │ SOCKS5 127.0.0.1:1080 │                     │   │                                  │          │  reguła D3,      │
 │ przekaźnik ──────────────────── vsock, port 2 ──► polaczenia.c — stan każdego połączenia │ fd 7     │  limit 400/10 s, │
 │  (jedno połączenie vsock  │  (virtio-vsock,     │   │  lacznik.c — SOCKS5, DNS (czyste) ├─────────►│  socket()+connect│
 │   na każde połączenie)    │   kolejki w pamięci │   │                                  │ POLACZ   │                  │
 └───────────────────────────┘   gościa)           │   ◄── gniazdo TCP / UDP (DNS) ──────────────────┤ GNIAZDO / ODMOWA │
                                                   │   bajty ⇄ gniazdo ⇄ internet         │          └──────────────────┘
                                                   └──────────────────────────────────────┘
```
1. Program w strefie łączy się z przekaźnikiem (127.0.0.1:1080, SOCKS5 — Firefox ma to w zablokowanych ustawieniach,
   `strefy/przegladarka/k1.js`; curl — `--socks5-hostname`).
2. Przekaźnik otwiera **jedno połączenie vsock** do VMM (port 2) na każde połączenie programu i przepycha bajty w obie strony,
   bez czytania ich. Łączy się bez czekania (od 0.10.5 krok 5).
3. VMM (`vsock.c`) przyjmuje połączenie w jednym ze 128 miejsc (`polaczenia.c`), każde z kredytem 64 KiB w obie strony.
4. Łącznik (`lacznik.c`) czyta SOCKS5: tylko CONNECT, adres IPv4 albo nazwa. Nazwę zamienia na adres sam (DNS, rekord A)
   przez gniazdo UDP od nadzorcy do serwera DNS z DHCP. Strefa pobierania: tylko nazwy z `K1_HOSTY_POBIERANIA` na porcie 443
   (`lacznik_cel`); `pobrane.k1:1` — koniec pary do programu zapisującego (`K1_STREFA_POBRANE`).
5. VMM prosi nadzorcę (`STREFA_POLACZ` na fd 7) o gniazdo TCP do adresu i portu. Nadzorca sprawdza **regułę D3** (internet
   i router — nigdy reszta sieci domowej, pętla zwrotna, adresy specjalne), limit 400 połączeń na 10 s, otwiera gniazdo
   (nieblokujące `connect`) i oddaje deskryptor (`STREFA_GNIAZDO`) albo powód odmowy (`STREFA_ODMOWA`).
6. Gdy połączenie TCP dojdzie, łącznik odpowiada SOCKS5 „0” i od tej chwili tylko przenosi bajty: od gościa do gniazda
   i od gniazda do gościa, ile pozwala kredyt.
7. Koniec: serwer kończy wysyłanie → gość dostaje `SHUTDOWN` tylko „wysyłanie” (półzamknięcie — gość może dalej wysyłać);
   błąd gniazda albo odmowa → `SHUTDOWN` w obie strony; gość zamyka → miejsce wolne, gniazdo zamknięte.

## Granice zaufania i kto co sprawdza
| Granica | Kto sprawdza | Co |
|---|---|---|
| pamięć i kolejki gościa | `virtq.c`, `mmio.c` | każdy deskryptor w pamięci gościa, bez pętli, długości (`fuzz_virtq`) |
| pakiet vsock | `vsock.c` | adresy (CID 3 → 2), typ STREAM, długość w łańcuchu i ≤ 64 KiB, **kredyt** — więcej bajtów, niż VMM ogłosił, kończy maszynę; port bez połączenia → RST; nieznany rodzaj — pominięty (`fuzz_vsock`) |
| SOCKS5 i DNS | `lacznik.c` | wersja, metoda, polecenie, typ adresu, bajt zarezerwowany, port ≠ 0, nazwa (litery, cyfry, `-`, `_`, kropki, ≤ 253), adresy nigdy do osiągnięcia (0.0.0.0, pętla zwrotna, multicast); odpowiedź DNS do naszego pytania (id, nazwa, A/IN, wskaźniki tylko wstecz) (`fuzz_lacznik`) |
| adres docelowy | nadzorca (`k1_siec_dla_strefy`) | reguła D3: router tak; sieć K1 (nawet publiczna), sieci prywatne, CGNAT, pętla, lokalne łącza, TEST-NET, multicast — nie |
| liczba połączeń | VMM (128 naraz), nadzorca (400 na 10 s) | ponad — odmowa (RST / `ODM_LIMIT`) |
| deskryptory | `polaczenia.c` | każde miejsce zaczyna z fd −1 (błąd „fd 0”, PR #119); gniazdo ma jednego właściciela — miejsce; zwolnienie miejsca zamyka je; odpowiedź nadzorcy dla miejsca już zwolnionego — zamknięta |

VMM nie otwiera gniazd (klatka: `shutdown`, `getsockopt SO_ERROR` — nic więcej), gniazda otwiera tylko nadzorca (ADR 0039).

## Komunikaty i porty
| Co | Kierunek | Pola |
|---|---|---|
| vsock port 1 | gość ↔ VMM | kanał sterujący (`strefa.md`; od 0.11.0 także „pelny 1/0” — pełny ekran), nie część mostu |
| vsock port 2 | gość → VMM | połączenia mostu, do 128 naraz (`K1_STREFA_POLACZENIA_MAKS`), kredyt 64 KiB (`POL_BUFOR`) |
| vsock port 3 (0.11.0 kroki 2, 3, ADR 0052) | gość ↔ VMM | **port plików**, nie część mostu: jedno połączenie agenta strefy, ramki `k1_pliki_strefy.h` (zgłoszenie pliku, bajty po zgodzie, wynik; od kroku 3 też plik z K1 do „Z K1” i jego zmieniona wersja), kredyt 64 KiB (`K1_PS_BUFOR`) — `strefa.md`, „Droga plików” |
| `STREFA_POLACZ` | VMM → nadzorca (fd 7) | obiekt = miejsce 1…128; TCP: a = adres, b = port, c = `K1_STREFA_TCP`; DNS: c = `K1_STREFA_DNS`; pobieranie: c = `K1_STREFA_POBRANE` |
| `STREFA_GNIAZDO` | nadzorca → VMM (+ deskryptor) | obiekt = miejsce, a = rodzaj (TCP, DNS, POBRANE) |
| `STREFA_ODMOWA` | nadzorca → VMM | obiekt = miejsce, a = `K1_STREFA_ODM_*` (adres poza D3 → SOCKS 2, brak sieci → 3, limit, błąd → 1) |

Pełne pola z zakresami — `docs/komponenty/strefa.md` („Komunikaty”).

## Limity i czasy
| | Wartość | Gdzie |
|---|---|---|
| połączenia naraz w strefie | 128 (przekaźnik ma tyle samo miejsc) | `K1_STREFA_POLACZENIA_MAKS` |
| kredyt (bufor) każdego kierunku | 64 KiB w VMM, 16 KiB w przekaźniku | `POL_BUFOR`, `BUFOR` |
| małe pakiety czekające na bufor gościa | odpowiedzi, kredyt, koniec — stan miejsca (nie giną); odmowy (RST bez połączenia) — kolejka 128, pełna gubi odmowę (licznik „odrzucone”) | `vsock.c` |
| połączenia u nadzorcy | 400 na 10 s na strefę | `strefy_siec.c` |
| DNS | 2 pytania po 2,5 s, potem „brak hosta” | `DNS_CZEKAJ` |
| łączenie TCP | limit jądra K1 (ok. 2 min); program w strefie zwykle rezygnuje wcześniej i zwalnia miejsce | — |
| łączenie przekaźnika z VMM | bez czekania; limit jądra strefy (2 s) | `przekaznik/main.c` |

## Liczniki w logu (niezmiennik 8: żadnych adresów ani nazw)
- VMM co 5 s: `K1-STREFA: liczniki: … połączenia P; vsock czeka Q, odrzucone R` (zajęte miejsca, kolejka odmów, zgubione odmowy).
- VMM na końcu strefy: `połączenia przeglądarki: N, nazwy N, odmowy N (poza listą hostów N); bez wolnego miejsca N, port
  w użyciu N, bez SOCKS5 N; najwięcej naraz N, zajęte na końcu N` — „zajęte na końcu” powinno być 0; jeśli nie, następny
  wiersz mówi, w jakim stanie zostało pierwsze takie miejsce (liczby).
- Nadzorca na końcu kanału strefy: `strefa N: połączenia N, odmowy N`; pierwsza odmowa — z numerem powodu.

## Pomiary (0.10.5 krok 5, `scripts/test-most.sh`)
Strefa próbna (1 procesor, 128 MiB), serwer na „routerze” QEMU (sieć `-netdev user`, ten sam komputer).
| | Emulacja (kontener, bez KVM) | CI z KVM |
|---|---|---|
| jedno połączenie, 3 MiB w dół | 11–14 MiB/s | (z przebiegu CI PR-a kroku 5) |
| echo 1 MiB (w górę i w dół) | 6–9 MiB/s | |
| 48 połączeń naraz (ok. 13 MiB) | 6–8 MiB/s razem | |
| całe połączenie z 1 bajtem (SOCKS5, nadzorca, TCP, koniec) | ok. 40–50 ms | |

W emulacji wąskim gardłem jest emulowany procesor (każdy pakiet to wyjście z maszyny). Dla filmów (0.10.9) liczy się
wynik z KVM; tanie poprawki — gdy pomiar pokaże potrzebę (wpis w `DO_ZROBIENIA.txt`, „Później”).

## Pułapki (już raz zapłacone)
- **fd 0** (PR #119): wyzerowana tablica miejsc = fd 0; pierwsze zwolnienie każdego miejsca zamykało cudze gniazdo (DNS,
  strony). Miejsca startują z fd −1 (`poczatek()` w `polaczenia.c`).
- **Firefox i stan sieci** (0.10.0 krok 2, ustalenie 7): strefa ma tylko pętlę zwrotną, więc Firefox po chwili uznawał,
  że „sieć jest wyłączona” — `network.manage-offline-status = false` (`strefy/przegladarka/k1.js`).
- **Zgubione odpowiedzi** (0.10.5 krok 5): kolejka 64 małych pakietów gubiła pod obciążeniem odpowiedzi na połączenia
  i aktualizacje kredytu (połączenie wisiało) — teraz to stan miejsca, a nagłówek (z kredytem) robi się przy wysyłce.
- **Półzamknięcie** (0.10.5 krok 5): koniec wysyłania serwera VMM zgłaszał jako koniec w obie strony — gość nie mógł już
  nic wysłać. Teraz tylko „wysyłanie”; w obie strony — przy błędzie.
- **Klient bez bajtu** (0.10.5 krok 5): połączenie z przekaźnikiem zamknięte przed SOCKS5 trzymało miejsce w VMM do końca
  strefy (po 128 takich — żadnej nowej strony). Teraz koniec wysyłania gościa przed SOCKS5 zamyka połączenie.
- **Reset urządzenia vsock** w gościu kończy wszystkie jego połączenia (`pol_wyczysc`).
- DNS sieci QEMU nie działa w kontenerze bez resolvera — test sprawdza nazwy tylko „nie ma takiej” (odpowiedź albo czas).

## Jak podłączyć nową strefę
1. Wiersz w `K1_STREFY` (`k1_uslugi.h`) — nadzorca daje każdej strefie kanał połączeń (fd 7), nic więcej nie trzeba.
2. W obrazie strefy przekaźnik `/usr/lib/k1/przekaznik` (jak w `scripts/zbuduj-strefe.sh`), pętla zwrotna włączona
   (`petla_zwrotna()` w `gosc`), program strefy ustawiony na SOCKS5 127.0.0.1:1080 z nazwami po stronie K1
   (`socks5h`, „remote DNS”) — inaczej będzie próbował DNS sam i nie znajdzie sieci.
3. Gdy strefa ma mieć węższy dostęp niż D3 (jak strefa pobierania) — reguła w `lacznik_cel` (tabela hostów, jak
   `K1_HOSTY_POBIERANIA`), z testem w `test_lacznik`.
4. Test: tryb gościa w `test-most.sh` albo własny scenariusz na `testy/qemu/k1qemu.py` z serwerem na „routerze”.

## Testy
| Warstwa | Co |
|---|---|
| host | `test_lacznik` (SOCKS5, DNS, D3, cele strefy pobierania), `fuzz_lacznik`, `fuzz_virtq`, **`fuzz_vsock`** (0.10.5 krok 5: pakiety gościa z dowolnymi nagłówkami, odpowiedzi nadzorcy, gniazda — para gniazd zamiast TCP, reset urządzenia; nic nie wychodzi poza bufor, miejsc nie więcej niż 128) |
| w systemie | `t_most.c` — scenariusz `test-most.sh` (dysk `k1-test-mostu`) |
| QEMU | **`scripts/test-most.sh`** (0.10.5 krok 5, T15): strefa próbna jest własnym klientem (`gosc/most.c`, `most_fazy.c`): 1 bajt, 3 MiB, echo 1 MiB, 48 naraz, półzamknięcia w obie strony, przerwane w połowie (klient, reset serwera, klient bez bajtu), odmowy D3 i łącznika obok udanych, nazwy; NEGATYWNE: zły SOCKS5 wprost do VMM, 140 połączeń na 128 miejsc, złośliwy gość vsock (ponad kredyt, cudzy adres — `gosc/zlosliwy.c`); liczniki gościa, serwera, VMM i nadzorcy zgodne; drugi przebieg po złośliwych; żadnego adresu w logu K1. `test-aktualizacja-siec.sh` — strefa pobierania przez most (HTTPS) |
