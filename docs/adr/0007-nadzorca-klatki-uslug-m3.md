# 0007. Nadzorca, klatki usług i kanały między nimi (M3)

- Status: Przyjęty
- Data: 2026-11-06
- Dotyczy: SPEC §7 (uprawnienia, nadzorca, restart), §12 (IPC, kontrola nadawcy), §14 (M3); ADR 0002 (fortify)

## Kontekst
Do M2 wszystkie usługi działały jako root ze wszystkimi uprawnieniami (RYZYKA R3), kanał wejscie→ekran
był pipe'em bez kontroli nadawcy (R19), a ekran po awarii nie wracał (R20). SPEC §7 wymaga: osobnych
UID/GID, minimalnych capabilities, `PR_SET_NO_NEW_PRIVS`, seccomp-BPF (biała lista), cgroups v2,
ograniczonego dostępu do urządzeń, Landlock; §12 — kontroli nadawcy (`SO_PASSCRED`/`SO_PEERCRED`).
Sprawdzone w źródłach 6.18.60 i man-pages:
- `kernel/cred.c` (`commit_creds`): zmiana euid czyści `pdeath_signal` — `PR_SET_PDEATHSIG` trzeba
  ustawić po `setresuid`. Zwykły UID bez `keep_caps` ma pusty zbiór capabilities (capabilities(7)).
- `SECBIT_NO_SETUID_FIXUP` wyłączyłby czyszczenie capabilities przy zmianie UID — dlatego go nie używamy.
- Landlock (`include/uapi/linux/landlock.h`): ABI 7; prawo `IOCTL_DEV` i ograniczenia ścieżek dotyczą
  plików otwieranych *po* nałożeniu reguł — deskryptory otwarte wcześniej (przez nadzorcę) działają.
  Zakresy `LANDLOCK_SCOPE_SIGNAL` i `ABSTRACT_UNIX_SOCKET` (ABI ≥ 6).
- `net/unix`: dla `socketpair` `SO_PEERCRED` zwraca dane procesu, który utworzył parę (tu: nadzorcy),
  więc do kontroli nadawcy służy `SO_PASSCRED` po stronie odbiorcy — jądro dołącza do **każdego**
  pakietu prawdziwe dane nadawcy (`SCM_CREDENTIALS`), których proces bez `CAP_SETUID` nie podrobi.
- cgroup v2: procesy mogą być w korzeniu hierarchii, a kontrolery włącza się w `cgroup.subtree_control`;
  zapis „0” do `cgroup.procs` przenosi piszący proces.
- seccomp: filtr nałożony przed `execve` obowiązuje też w nowym programie, więc musi dopuścić start musl
  (`arch_prctl(ARCH_SET_FS)`, `set_tid_address`) i jedno `execve`.

## Decyzja
- **Klatka** (`src/wspolne/k1_klatka.c`), nakładana przez nadzorcę w procesie potomnym, w tej kolejności:
  cgroup usługi (`memory.max`, `memory.oom.group=1`, `pids.max`), zamknięcie zbędnych deskryptorów,
  `RLIMIT_NOFILE/CORE=0/MEMLOCK=0/FSIZE`, securebits (`NOROOT`, `KEEP_CAPS_LOCKED`,
  `NO_CAP_AMBIENT_RAISE`, z blokadami), wyczyszczenie ambient i **całego zbioru granicznego**,
  `setgroups(0)`, `setresgid`, `setresuid` (sprawdzenie `capget`: wszystko 0), `PR_SET_PDEATHSIG`,
  `PR_SET_NO_NEW_PRIVS`, Landlock (wszystkie prawa FS ABI 5 + sieć TCP + oba zakresy; dozwolone tylko
  uruchomienie własnego programu i — opcjonalnie — jeden katalog do zapisu), seccomp, `execve`.
- **Usługa na starcie** (`k1_klatka_w_usludze`): `PR_SET_DUMPABLE=0`, obsługa `SIGSYS`, drugi filtr
  seccomp zakazujący `execve`/`execveat`.
- **seccomp:** biała lista (wspólna baza ~30 wywołań + lista profilu), zła architektura → zabicie
  procesu, numery x32 i wszystko spoza listy → `SECCOMP_RET_TRAP`: wywołanie **nie wykonuje się**,
  usługa zapisuje w logu numer i kończy się kodem 159. (Wybrane zamiast `KILL_PROCESS`, bo daje
  diagnostykę; bezpieczeństwo jest to samo — zablokowane wywołanie nigdy nie dochodzi do jądra.)
  Warunki na argumenty: `mmap`/`mprotect` bez `PROT_EXEC`, `ioctl` tylko typu `'d'` (DRM, ekran) albo
  `'E'` (evdev, wejscie), `prctl` tylko `PR_SET_DUMPABLE`, `arch_prctl` tylko `ARCH_SET_FS`.
- **UID/GID** stałe (`src/wspolne/k1_uslugi.h`): wejscie 100, ekran 101, (M4: gui 102, aplikacje 200+).
- **Urządzenia otwiera nadzorca** i przekazuje deskryptory (ekran: karta jako fd 4 — to otwarcie jest
  DRM masterem; wejscie: urządzenia `event*` jako fd 4…). Usługi nie mają dostępu do żadnej ścieżki.
- **Kanały:** `AF_UNIX SOCK_SEQPACKET` (`socketpair`, `CONFIG_UNIX=y`), bez gniazd w systemie plików.
  Strona odbiorcy ma `SO_PASSCRED`; odbiorca porównuje UID nadawcy każdego pakietu z tożsamością
  nadaną przez nadzorcę. Ekran dostaje kanał sterujący (fd 3), a nim — końce kanałów klientów
  z komunikatem `KLIENT(rola, aplikacja, uid)` i `SCM_RIGHTS`. Wspólny komunikat 32 B
  (`src/wspolne/k1_komunikat.h`, testy + fuzzing) — także dla protokołu okien w M4 (ADR 0008).
- **Logi:** stderr usługi to pipe do nadzorcy; nadzorca usuwa znaki sterujące, sprawdza prefiks usługi
  (obcy prefiks jest oznaczany), ogranicza do 20 wierszy/s i dopiero wtedy pisze na konsolę. fd 0 i 1
  usług to `/dev/null` — usługi nie mają już dostępu do konsoli.
- **Restarty:** po 0,2 / 1 / 2 / 4 / 4 s; 6. zakończenie w ciągu 60 s wyłącza usługę (bez pętli awarii).
  Gdy kończy się ekran, nadzorca zatrzymuje jego klientów (bez liczenia jako awarię) i uruchamia ich
  ponownie z nowym ekranem.
- **Obraz testowy:** program testowy (root, bez klatki) dostaje fd 3 = kanał do nadzorcy:
  `TEST_POLACZ` (kanał do ekranu z dowolną rolą i oczekiwanym UID — do testów złośliwego IPC
  i obcego nadawcy) i `TEST_PROBA` (uruchomienie programu testowego **w klatce danej usługi**, który
  próbuje zabronionych rzeczy). Kanału nie ma w obrazie produkcyjnym.
- **`_FORTIFY_SOURCE` (decyzja odłożona w ADR 0002): na razie nie.** `fortify-headers` to zewnętrzny
  kod spoza Ubuntu/Debiana; zyski pokrywają dziś: jawne limity długości, `-Wformat=2`, ASan/UBSan
  i fuzzing na hoście, a w systemie — klatki (przejęta usługa i tak nie ma uprawnień). Wracamy do tego
  przy własnym buildzie musl (M12 albo wcześniej).

## Konsekwencje
- Przejęcie `ekran` albo `wejscie` nie daje już roota: brak capabilities, plików, nowych procesów,
  gniazd, sygnałów do innych procesów, pamięci wykonywalnej i innych typów `ioctl` (testy-próby).
- Ekran wraca po awarii; system działa dalej także wtedy, gdy usługa zostanie wyłączona.
- Nadzorca pozostaje rootem bez klatki (musi zmieniać UID, tworzyć cgroups, otwierać urządzenia) —
  jest częścią bazy zaufania (RYZYKA R23). Nie parsuje danych od usług poza komunikatami 32 B
  (z testów i — od M4 — od gui) i wierszami logu.
- Zakazane wywołanie kończy usługę — po aktualizacji musl/jądra lista może wymagać uzupełnienia
  (wykryje to test: `zabronione wywołanie systemowe` w logu = błąd testu).

## Rozważane alternatywy
- `SECCOMP_RET_KILL_PROCESS` jako domyślna akcja — brak informacji, które wywołanie zablokowano.
- Uprawnienia do węzłów urządzeń (chown na UID usługi) zamiast przekazywania deskryptorów — usługa
  musiałaby widzieć `/rdzen/jadro/dev`.
- `SO_PEERCRED` — dla `socketpair` wskazuje twórcę pary (nadzorcę), nie faktycznego nadawcę.
- Osobny proces logów — więcej kodu; nadzorca i tak obsługuje pętlę zdarzeń.
