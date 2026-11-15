# 0043. Przeglądarka w strefie: Firefox z Debiana, nasz kompozytor, połączenia stref przez K1

- Status: Przyjęty; wdrożony w 0.10.0 krok 2 (sesje jednorazowe); krok 3 — sesje, które pamiętają (dysk strefy)
- Data: 2026-11-15
- Dotyczy: ADR 0042 (strefy), ADR 0039 (gniazda sieciowe tylko u nadzorcy), ADR 0020 p. 2 i 3, ADR 0025 (Przeglądarka K1
  — później), ADR 0027 (pliki — 0.11), SPEC §8 (dopisek [korekta: ADR 0043]); plan `docs/PLAN-0.10.0.txt`: T7–T9,
  S12–S20, decyzje właściciela D2, D3, D6; RYZYKA R81–R85

## Kontekst
Krok 1 dał strefę próbną: mikro-maszynę z naszym programem w środku, okno w zaufanej ramce, klawiaturę i mysz. Krok 2
ma dać w niej prawdziwą przeglądarkę z internetem — obcy, duży program (Firefox), jego biblioteki i grafikę, a strefa nie
ma karty sieciowej ani adresu (ADR 0042 T2), a gniazda sieciowe otwiera tylko nadzorca (ADR 0039).

## Decyzja

**Obraz strefy z pakietów Debiana 13 (S12, S13, S14).** Jedna tabela `strefy/przegladarka.txt`: pakiet, wersja, archiwum,
SHA-256, ścieżka (117 pakietów: Firefox ESR, polski interfejs, GTK 3 i biblioteki). Tabelę pisze tylko
`scripts/odswiez-strefe.py`: `InRelease` gałęzi trixie, trixie-updates i trixie-security sprawdzany `gpgv` kluczami
Debiana 13 z repo (`strefy/klucze/debian-trixie.gpg` — skąd: `strefy/README.md`), spis pakietów sumą z podpisanego
`InRelease`, zależności — pierwsza z alternatyw. Budowa (`scripts/zbuduj-strefe.sh`) pobiera każdy pakiet raz (zapas:
snapshot.debian.org z chwilą tabeli), sprawdza sumę, rozpakowuje **bez skryptów pakietów**; to, co by zrobiły (spis typów
plików, spis modułów obrazków GTK), robią programy z tych samych pakietów przez ich własny `ld.so` (bez chroot, bez
roota). Usunięte: dokumentacja, podręczniki, tłumaczenia poza polskim, raporty awarii Firefoksa. **Bez powłoki** (S14:
Firefox z Debiana to program, nie skrypt; budowa przerywa, gdy w obrazie jest `sh`, `bash` albo `dash`) i bez demonów
systemu (systemd, dbus — pominięte w zależnościach). SquashFS xz z filtrem x86, powtarzalnie: **104 MB** (S12: budżet
≤ ok. 105 MiB spełniony; k1.efi rośnie z 17,6 do 121 MiB w miejscu 128 MiB — RYZYKA R81).

**T7 — własny kompozytor Wayland w C** (`src/strefa/kompozytor/`, w strefie `/usr/lib/k1/kompozytor`), **bez
libwayland**: protokół na gnieździe (wiadomości, deskryptory) i obsługiwane interfejsy to tabele (`K1_INTERFEJSY`,
sygnatury jak w `wayland.xml` 1.23 i `xdg-shell.xml` z wayland-protocols 1.44): `wl_compositor` 4, `wl_shm` (argb8888,
xrgb8888), `wl_subcompositor`, `wl_seat` 5 (wskaźnik, klawiatura z polską mapą XKB z `strefy/klawiatura-pl.xkb`),
`wl_output`, `wl_data_device_manager` (bez schowka — 0.11), `xdg_wm_base` 2 (okno na cały wyświetlacz, okna dialogowe
na środku, wyskakujące menu z pozycjonerem). Każda wiadomość sprawdzana według sygnatury; zła kończy tego klienta,
nie kompozytor. Bufor klienta kopiowany od razu do własnej kopii powierzchni (tylko uszkodzony prostokąt), więc
skrócona pod nami pula (SIGBUS) kończy tylko klienta. Cage i weston odrzucone: w Debianie ciągną mesa z LLVM (dziesiątki
MB), a nam wystarcza jedno okno. Wyświetlacz — `kms.c`, wspólny z programem `gosc` (bufor „dumb”, DIRTYFB z prostokątem).

**T8 — Firefox ESR z Debiana 13**, niezmieniony. `gosc` (init strefy) w trybie przeglądarki: tmpfs na `/tmp`, `/run`,
`/home`, `/dev/shm`, pętla zwrotna, nowy `machine-id` na każdą maszynę, potem przekaźnik, kompozytor i Firefox jako
użytkownik 1000 (nie root strefy). Ustawienia zablokowane (`/etc/firefox-esr/k1.js`, `policies.json`): polski interfejs,
każde połączenie przez SOCKS5 na 127.0.0.1:1080 z nazwami po stronie K1, bez DNS-over-HTTPS, IPv6, WebRTC, aktualizacji,
telemetrii, badań, raportów; rysowanie procesorem. Sandbox Firefoksa działa na przestrzeniach nazw i seccomp **jądra
strefy** (`jadro/strefa.config`: USER_NS, PID_NS, NET_NS, IPC_NS, UTS_NS, SECCOMP_FILTER); jądro K1 dalej bez USER_NS.
Nazwa i logo Firefoksa nie są używane przez K1: w menu „Przeglądarka” z ikoną K1 (globus), program bez zmian (S18).

**T9 — sieć stref, z jedną zmianą względem planu.** Strefa nie ma karty sieciowej. Firefox → SOCKS5 na 127.0.0.1:1080
→ **przekaźnik** w strefie (`src/strefa/przekaznik`, każde połączenie = jedno połączenie vsock do portu 2 hosta) →
**VMM** (`vsock.c`: wiele połączeń, każde z własnym „kredytem” 64 KiB w obie strony; gość, który wyśle ponad kredyt, jest
wrogi — koniec maszyny) → **łącznik** (`lacznik.c`: SOCKS5 sprawdzany pole po polu — tylko CONNECT, IPv4 albo nazwa;
BIND, UDP i IPv6 odrzucane kodem SOCKS; DNS: pytanie A z losowym identyfikatorem, odpowiedź przyjęta tylko z tym
identyfikatorem i tym samym pytaniem, wskaźniki kompresji tylko wstecz i w liczbie ograniczonej) → prośba
`STREFA_POLACZ` do **nadzorcy** (`strefy_siec.c`), który sprawdza adres regułą D3 (`k1_siec_dla_strefy`), liczy limit
(400 połączeń na 10 s na strefę), otwiera gniazdo TCP (nieblokujące) albo UDP do serwera DNS z DHCP (opcja 6, nowe pole
`e` komunikatu `SIEC_ADRES`) i daje deskryptor (`STREFA_GNIAZDO`) albo powód odmowy (`STREFA_ODMOWA`). Bajty płyną
VMM ↔ gniazdo. TLS robi przeglądarka w strefie; K1 widzi tylko adres i port.
*Zmiana:* plan zakładał osobną usługę `lacznik` (jedna na strefę, osobny UID). Łącznik jest modułem **VMM** strefy:
VMM już jest osobnym procesem z osobnym UID i klatką dla każdej sesji, więc błąd w łączniku jednej sesji nadal nie widzi
ruchu innej — a odpada przekazywanie każdego połączenia między procesami (pary gniazd, deskryptory przez kanał)
i druga klatka na sesję. Klatka VMM dostaje tylko `shutdown` i `getsockopt(SO_ERROR)`; gniazda nadal otwiera wyłącznie
nadzorca (VMM nie ma `socket`, `connect`, `bind`). Koszt: więcej kodu w procesie VMM (SOCKS5 i DNS są małe, czyste,
testowane i fuzzowane — `test_lacznik`, `fuzz_lacznik`).

**D3 (właściciel, rekomendacja przyjęta do potwierdzenia):** wolno do internetu (publiczne adresy) i do samego routera;
nie wolno do reszty sieci domowej (nasza podsieć, adresy prywatne, współdzielone 100.64/10, link-local), pętli zwrotnej,
multicastu, rozgłoszeń, zakresów dokumentacji i zarezerwowanych. Tylko TCP (bez UDP i WebRTC).

**D2 (właściciel, rekomendacja przyjęta do potwierdzenia):** sesje „Ogólna” (szałwia), „Bank” (łupek), „Prywatna”
(wrzos) — trzy wiersze `K1_STREFY`, każda osobna maszyna, kolor i UID. W kroku 2 wszystkie jednorazowe; w menu Start
„Przeglądarka” otwiera „Ogólną”. Pozostałe sesje pod prawym przyciskiem — krok 3.

**D6 / pamięć.** Wiersz sesji: 1536 MiB pamięci gościa, 2 procesory. Nadzorca nie uruchomi sesji, gdy zabrakłoby pamięci
(okienko „za mało pamięci” z kroku 1). W emulacji QEMU (testy bez KVM; sygnatura „TCGTCGTCGTCG” w CPUID) VMM daje
strefie 1 procesor — emulowane SVM psuje drugi procesor gościa (ADR 0042 T10).

**Logi.** Gość przeglądarki nie pisze do logu K1 (tylko liczba bajtów konsoli — niezmiennik 8); VMM i nadzorca piszą tylko
liczniki połączeń, nazw i odmów oraz powód pierwszej odmowy — nigdy adres, nazwę strony ani treść.

## Konsekwencje
- W menu Start jest Przeglądarka; strony z internetu działają w strefie, sesja zapomina wszystko po zamknięciu.
- Firefox dostaje poprawki tylko z nowym wydaniem K1 (S19): `scripts/odswiez-strefe.py` i wersja PATCH (RYZYKA R82).
- Rdzeń (z obrazem przeglądarki) jest w pamięci — +104 MB RAM na stałe; k1.efi 121 MiB z 128 MiB miejsca (R81).
- DNS bez szyfrowania (router widzi nazwy stron, jak w każdym systemie bez DoH) — R83.
- VMM ma w sobie łącznik — R84. Kompozytor w strefie jest nasz, ale działa po stronie gościa (jego błąd = koniec strefy).
- Test: `scripts/test-przegladarka.sh` (strona przez łącznik, formularz, odmowa D3, logi bez adresów); nazwy (DNS) —
  testy na hoście (S17).

## Odrzucone
- Osobna usługa `lacznik` na strefę — patrz T9 (ten sam podział zaufania, mniej części).
- virtio-net w strefie z NAT w K1 — strefa miałaby adres i całą sieć IP; K1 musiałby filtrować pakiety (zapora — ADR 0039:
  bez zapory w jądrze).
- cage / weston / sway jako kompozytor — mesa i LLVM w obrazie (S12).
- Pośrednik HTTP zamiast SOCKS5 — tekstowy, większy, trudniejszy do sprawdzenia.
