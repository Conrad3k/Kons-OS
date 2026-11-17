# 0044. Pamięć stref: balon

- Status: Przyjęty; wdrożony w 0.10.1 krok 2 (mechanika); zasada podziału pamięci komputera — krok 3
- Data: 2026-11-16
- Dotyczy: ADR 0042 T4 (pamięć strefy z wiersza `K1_STREFY`, memfd bez prawa wykonania), SPEC §8 (dopisek
  [korekta: ADR 0044]); plan `docs/PLAN-0.10.1.txt`: T1–T6, S7–S11, decyzja właściciela D2; RYZYKA R88, R89

## Kontekst
Do 0.10.0 każda sesja przeglądarki dostawała przy starcie stały kawałek pamięci komputera (1536 MiB) i ani bajta więcej,
także na X230 z 8 GB. Raz dotknięta strona pamięci gościa zostawała w K1 na zawsze: pamięć gościa to memfd, K1 nie ma
wymiany (`jadro/k1.config`), a gość niczego nie oddaje (S2). Na 4 GB mieściła się jedna sesja. Krok 1 pokazał, że awarie
z obserwacji właściciela to nie pamięć (kompozytor strefy) — ale stała pamięć i tak jest za ciasna dla ciężkich stron
i za rozrzutna dla lekkich.

## Decyzja

**T1 — co: klasyczny balon virtio** (virtio 1.2 §5.5, sterownik `drivers/virtio/virtio_balloon.c` w jądrze strefy:
`CONFIG_VIRTIO_BALLOON`, który wybiera `MEMORY_BALLOON` i `PAGE_REPORTING`) jako nowe urządzenie virtio-mmio VMM
(`src/strefa/balon.c`, wiersz `BALON` w `K1_URZADZENIA`) z trzema cechami:
- `REPORTING` — **zgłaszanie wolnych stron**: jądro gościa samo zgłasza wolne bloki (2 MiB, ok. 2 s po zwolnieniu —
  `mm/page_reporting.c`) i czeka, aż VMM odda bufor; VMM oddaje je K1. Bierne — nie odpytuje gościa, nie spowalnia go;
  rozwiązuje „raz zajęta pamięć nigdy nie wraca”. Nie oddaje pamięci podręcznej gościa — od tego jest balon.
- `STATS_VQ` — statystyki gościa (pamięć dostępna, pamięć podręczna…) co kilka sekund: **podpowiedź**, nie prawda.
- `DEFLATE_ON_OOM` — zanim jądro gościa zabije proces, bierze strony z balonu (najwyżej 1 MiB naraz) — siatka.
Bez `FREE_PAGE_HINT` (służy migracji maszyn), `PAGE_POISON`, `MUST_TELL_HOST` (VMM i tak może dać zera). Kolejki
(ZE ŹRÓDEŁ, `init_vqs`, `vm_find_vqs` w `virtio_mmio.c` numeruje tylko nazwane): 0 pompowanie, 1 spuszczanie,
2 statystyki, 3 zgłaszanie (≥ 32 miejsc — `PAGE_REPORTING_CAPACITY`; VMM daje 128). **Balon jest domyślnie pusty**: gość
ma od startu całą swoją górną granicę (strony dochodzą z memfd, gdy ich dotknie) — nagły skok (ciężka strona,
przywrócenie okna) nie czeka na K1. Odrzucone: virtio-mem (dokładanie pamięci „na gorąco” blokami — więcej kodu w VMM
i gościu, potrzebne dopiero przy dużo większych granicach; „Nie w tej wersji”).

**T2 — jak pamięć wraca do K1: `fallocate(PUNCH_HOLE | KEEP_SIZE)`** na memfd pamięci gościa (strony z balonu
i zgłoszone bloki): tmpfs zwalnia strony, KVM przez powiadomienia MMU zapomina ich odwzorowanie, następny dostęp gościa
dostaje stronę zer (i jest znów liczony w klatce VMM). Nie `MADV_DONTNEED` — na współdzielonym odwzorowaniu memfd tylko
odmapowuje, stron tmpfs nie zwalnia. Klatka VMM (`profile.c`) dostaje `fallocate` **tylko** z trybem
`PUNCH_HOLE | KEEP_SIZE` (warunek seccomp na argumencie) — ani rosnąć, ani przydzielać. Jądro K1 nie ma hugetlbfs ani
przezroczystych dużych stron — balon 4 KiB nie trafia na niepodzielne bloki.

**T3 — kto decyduje: nadzorca.** Widzi pamięć całego komputera i wszystkie strefy; VMM tylko wykonuje cel balonu
i przekazuje liczby; gość tylko zgłasza. Kanał: istniejący VMM ↔ nadzorca (fd 7 VMM), dwa nowe komunikaty:
`STREFA_PAMIEC` (od VMM, najwyżej raz na sekundę: pamięć gościa trzymana w K1 — strony memfd, dostępna wg statystyk
gościa, balon, cel, oddane od startu) i `STREFA_BALON` (do VMM: cel w MiB), sprawdzane pole po polu
(`k1_kom_strefa.c`). W kroku 2 nadzorca trzyma balon pusty, a pompuje go tylko na prośbę programu testowego
(mechanika); zasada dla wielu stref i braku pamięci komputera — krok 3 (T6).

**T4 — twarda granica: memory.max klatki VMM = górna granica sesji + zapas VMM (184 MiB), stała przez całe życie
strefy;** RLIMIT_FSIZE = górna granica (memfd). Balon to sterowanie „miękkie”: gość, który go ignoruje albo kłamie
w statystykach, dojdzie najwyżej do swojej granicy. Odrzucone: memory.max zmieniane w locie razem z balonem (wyścig: gość
dotyka stron, zanim nadzorca podniesie limit → zabity VMM — dokładnie to, co psuło sesje).

**Górna granica (D2, właściciel 2026-11-16): pamięć komputera minus rezerwa K1** — jedna tabela progów
(`src/nadzorca/pamiec_stref.c`): rezerwa 768 MiB do 6 GiB pamięci komputera, 1 GiB od 6 GiB (rekomendacja D3); nigdy
mniej niż pamięć na start z wiersza `K1_STREFY` (kolumna pamięci = „pamięć na start”), całe 2 MiB, najwyżej 64 GiB.
IdeaPad 110 (ok. 3,7 GiB dla systemu): ok. 2,9 GiB; X230 (ok. 7,6 GiB): ok. 6,6 GiB. VMM dostaje ją w `argv[3]`.

**T5 — pamięć gościa powyżej 4 GiB:** jeden memfd, dwie części — do 3 GiB pod urządzeniami (MMIO od 0xd0000000), reszta
od 4 GiB: drugi wpis RAM w e820, drugie miejsce pamięci KVM (ten sam memfd, przesunięcie 3 GiB). `struct gosc_pamiec`
ma `dol` (bajty od 0); `gp_wsk` tłumaczy adres gościa na miejsce w memfd i nie przepuszcza zakresu przez dziurę. Kolejki
virtio czytane tylko przez `gp_wsk` (gość może je położyć ponad 4 GiB). Urządzenia virtio-mmio zostają pod 4 GiB.

**Bezpieczeństwo.** Każdy numer strony i każdy zgłoszony blok sprawdzany przed dotknięciem pamięci (`src/strefa/strony.c`,
czyste: `test_balon`, `fuzz_balon`): strona w RAM gościa (nie w dziurze, nie poza końcem), blok z całych stron, lista
z całych numerów i najwyżej 256 (tyle daje sterownik), statystyki z całych wpisów o znanych znacznikach, zapis
konfiguracji tylko do `actual`. Zły = koniec tej maszyny. Złośliwy gość testu (tryby 6–8: strona poza pamięcią, blok
nie z całych stron, za długa lista) — kończy się tylko jego strefa.

## Konsekwencje
- Sesja bierze tyle pamięci, ile używa, do górnej granicy; zwolniona (zamknięta karta) wraca do K1 w kilka sekund.
- Suma granic kilku sesji może przekroczyć pamięć komputera — **dopóki krok 3 nie doda rozdzielacza**, ciężkie strony
  w dwóch sesjach naraz na 4 GB mogą wyczerpać pamięć komputera (wtedy jądro K1 zabija VMM, okienko mówi „pamięć
  komputera”) — RYZYKA R88. Pamięć podręczna gościa (pliki obrazu) zostaje w K1, póki balon jej nie wypchnie (krok 3).
- Gość, który dotknie oddanej strony, dostaje zera — tak działa każdy balon; jądro gościa wie o tym (strony z balonu
  i zgłoszone wolne strony nie mają treści) — RYZYKA R89.
- Liczby: linia liczników VMM (`pamięć w K1`, `oddane`, `balon`), nadzorca przy końcu strefy.
