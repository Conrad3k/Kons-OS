# 0062. Jądro (0.13.0): bez RANDSTRUCT, kCFI w audycie 0.15, ochrona jądra „piętro niżej” po 1.0

- Status: Przyjęty (plan 0.13.0, krok 7) — decyzja bez zmiany w jądrze
- Data: 2026-11-27
- Dotyczy: ADR 0003 (jądro: konfiguracja — **uzupełniony**: „świadomie bez RANDSTRUCT” rozstrzygnięte, kCFI z terminem),
  ADR 0002 (kompilator — GCC zostaje dla jądra do 0.15), niezmiennik 9 z `.claude/skills/kons-dev/SKILL.md` (powtarzalny
  build); plan `docs/PLAN-0.13.0.txt` (krok 7, T12, T13); Issue #231 (p. 3); RYZYKA R4, R11; opis: `docs/komponenty/jadro.md`

## Kontekst
Wszystkie klatki K1 stoją na jednym jądrze (R4, #231). Jądro K1 ma już większość wzmocnień z listy KSPP (ADR 0003,
`docs/komponenty/jadro.md`), ale nie ma trzech rzeczy, o które pytał plan:
- **RANDSTRUCT** — losowy układ pól wrażliwych struktur jądra; exploit, który zna przesunięcia pól (np. wskaźnika funkcji
  w strukturze), musiałby je najpierw odgadnąć. Do 0.12.3 „świadomie bez — losowe ziarno kłóci się z powtarzalnym
  buildem, decyzja przy 0.13” (ADR 0003).
- **kCFI** — sprawdzenie przy każdym wywołaniu przez wskaźnik funkcji, że cel ma właściwy typ (podmieniony wskaźnik nie
  prowadzi do dowolnej funkcji). Jądro K1 budujemy GCC, a kCFI daje tylko Clang.
- **Ochrona jądra „piętro niżej”** (pomysł właściciela z 2026-11-08) — mały hiperwizor pod jądrem, który pilnuje, że kod
  jądra się nie zmienia i że jądro nie widzi wybranych sekretów, nawet gdy samo zostało przejęte (jak VBS/HVCI w Windows
  albo pKVM w Androidzie).

Sprawdzone w źródłach Linux 7.3.6 i na komputerze budowy (Ubuntu 24.04, GCC 13.3, Clang 18.1), 2026-11-27:
- `RANDSTRUCT_FULL` i `RANDSTRUCT_PERFORMANCE` działają z wtyczką GCC (`GCC_PLUGINS` K1 już ma — `KSTACK_ERASE`,
  `LATENT_ENTROPY`); `RANDSTRUCT_FULL` także z Clangiem (`-frandomize-layout-seed-file`). Ziarno losuje
  `scripts/gen-randstruct-seed.sh` z `/dev/urandom` przy pierwszej budowie w danym katalogu; zmienną `gen-randstruct-seed`
  da się nadpisać w wywołaniu `make` (stałe ziarno).
- `CFI` (`depends on $(cc-option,-fsanitize=kcfi)`): GCC 13 — nie; Clang 18 — tak (próba). `FINEIBT` (kCFI + IBT) wymaga
  procesora z IBT (Intel od 11. generacji) — X230 (Ivy Bridge) i IdeaPad 110 (Skylake) go nie mają, więc u nich kCFI
  byłoby jedyną ochroną wywołań przez wskaźnik.
- `KSTACK_ERASE` (czyszczenie stosu jądra po każdym wywołaniu systemowym) wymaga wtyczki GCC albo
  `-fsanitize-coverage-stack-depth-callback-min` — Clang 18 tego nie ma (próba). `LATENT_ENTROPY` jest tylko w GCC.
- pKVM jest w tych źródłach tylko dla arm64 (`arch/arm64/kvm/hyp/nvhe`); dla x86 nie ma ani pKVM, ani ochrony jądra przez
  hiperwizor (prace Microsoftu „Heki”/LVBS i Google'a nad pKVM na x86 nie weszły do jądra).

## Decyzja
1. **RANDSTRUCT — nie** (T12). Wydanie K1 jest publiczne i powtarzalne: każdy może pobrać `k1.efi` z Releases albo
   zbudować to samo jądro z repo. Układ struktur takiego jądra zna więc każdy atakujący — z jawnego ziarna w repo, a przy
   ziarnie tajnym (sekret CI) z samego pliku `k1.efi` (przesunięcia pól widać w kodzie maszynowym). RANDSTRUCT pomaga
   tam, gdzie atakujący nie ma binarki (jądro budowane u siebie), a K1 nie buduje jądra na laptopie. Tajne ziarno
   kosztowałoby za to drugi sekret w CI i koniec sprawdzalnej powtarzalności (niezmiennik 9 — nikt poza CI nie
   odtworzyłby wydania). Pomiar (niżej, „Testy”) pokazuje, że technicznie się da i kosztuje mało (stałe ziarno daje
   powtarzalne jądro, +8 KiB, ta sama długość budowy) — ale wtyczka zmienia układ pamięci w całym jądrze, także
   w sterownikach laptopów, których CI nie uruchamia, a przy korzyści bliskiej zeru każde takie ryzyko przeważa. Gdyby K1
   kiedyś budował jądro na komputerze użytkownika (dziś nie planowane), wraca do rozważenia.
2. **kCFI — w audycie 0.15, nie w 0.13** (T13). Daje realną ochronę obu laptopom (bez IBT), ale:
   - wymaga zmiany kompilatora jądra na Clang (`LLVM=1`): nowa obca zależność budowy (ADR 0002, `docs/ZALEZNOSCI.md`),
     inny klucz pamięci podręcznej CI, robot (ADR 0047) i `porownaj-jadro.sh` na nowym kompilatorze;
   - Clang 18 z Ubuntu 24.04 zabrałby `KSTACK_ERASE` i `LATENT_ENTROPY` — kCFI za cenę dwóch innych wzmocnień; dopiero
     Clang z `-fsanitize-coverage-stack-depth-callback-min` (NIEPEWNE od której wersji — sprawdzi 0.15) zachowa oba
     bez wtyczek GCC (`LATENT_ENTROPY` i tak odpadnie — ocena w 0.15);
   - błąd typu wywołania wychodzi dopiero na ścieżce, która się wykonuje: sterowniki laptopów (i915, e1000e, ath10k,
     HD Audio, xHCI) QEMU w CI nie uruchamia, a kCFI kończy taki błąd paniką jądra — na laptopie, do którego robot wydaje
     sam. Kolejność w 0.15: Clang z zachowanym `KSTACK_ERASE`, najpierw wersja z `CFI_PERMISSIVE` (ostrzeżenie zamiast
     paniki) na X230 i IdeaPadzie z raportem na pendrive (ADR 0058, 0059), potem pełne kCFI. Plan 0.15 dostaje ten
     punkt; `UBSAN_LOCAL_BOUNDS` (też tylko Clang) — razem z nim.
3. **„Piętro niżej” — później, po 1.0, jako osobny temat** (nie: „nigdy”). Co by dało: kod jądra nie do zmiany nawet
   przez przejęte jądro (strony kodu tylko do wykonania w tablicach EPT hiperwizora), klucze skarbca i znak TPM poza
   zasięgiem jądra, mniejsza waga R4. Czego wymaga: VT-x z EPT i VT-d (X230 i IdeaPad mają VT-x z EPT — bez tego strefy
   by nie działały; VT-d IdeaPada — NIEPEWNE), a przede wszystkim nowego, zaufanego kodu pod jądrem — własnego
   hiperwizora, który musi też obsłużyć strefy (dziś KVM w jądrze K1: albo zagnieżdżanie, albo strefy przechodzą do
   hiperwizora — przebudowa ADR 0042). To praca na kilka wersji, a dla x86 nie ma gotowej części w jądrze, na której
   można by się oprzeć. Wracamy do tego, gdy jedno z dwóch: (a) do jądra wejdzie pKVM albo ochrona przez hiperwizor
   dla x86 (robot przy nowej linii jądra — `porownaj-jadro.sh` pokaże nowe opcje), (b) po 1.0. Wpis w
   `docs/DO_ZROBIENIA.txt` („Później”).
4. Jądro bez zmian. W `jadro/k1.config` zmienia się tylko komentarz przy wtyczkach GCC (odsyła tu — z wydaniem 0.13.0,
   krok 8 planu). `jadro/k1-odrzucone.txt` bez zmian (RANDSTRUCT nie jest „domyślnie tak” — robot o nie nie pyta).

## Konsekwencje
- R11 zostaje otwarte, ale z rozstrzygnięciem: RANDSTRUCT zamknięte („nie” z powodem), kCFI — termin 0.15 i kolejność
  wdrożenia. Ochrona skoków pośrednich do 0.15 jak dziś: retpoline / `MITIGATION_*`, IBT na procesorach, które go mają.
- Powtarzalność jądra, CI i robot bez zmian.
- Ochrona jądra „piętro niżej” nie jest w żadnym planie wersji — tylko w „Później” z warunkami powrotu.

## Testy
- Pomiar RANDSTRUCT (2026-11-27, kontener bez KVM, 4 wątki, ten sam fragment co wydanie + `RANDSTRUCT_FULL`, stałe ziarno
  przez `gen-randstruct-seed=` w wywołaniu `make`), każde jądro we własnym, pustym katalogu budowy:
  | | bez RANDSTRUCT | `RANDSTRUCT_FULL` |
  |---|---|---|
  | budowa `bzImage` | 381 s | 377 s (drugi raz — w innym katalogu — to samo) |
  | `bzImage` | 15 418 880 B | 15 427 072 B (+8 KiB, +0,05 %) |
  | kod (`text` w `vmlinux`) | 24 300 664 B | 24 317 524 B (+16 KiB) |
  | powtarzalność | — | dwie budowy z tym samym ziarnem: ten sam SHA-256 `bzImage` |
  Start jądra z RANDSTRUCT nie był sprawdzany (decyzja „nie”); wydajności w emulacji i tak nie widać (TCG).
- Próby kompilatorów (`-fsanitize=kcfi`, `-fsanitize-coverage-stack-depth-callback-min=1`,
  `-frandomize-layout-seed-file=`) — wyniki w „Kontekst”.
- Jądro wydania bez zmian: `test-m1` (start, testy w systemie) na gałęzi kroku; `scripts/sprawdz-kspp.sh` — te same
  odstępstwa co w `docs/komponenty/jadro.md`.
