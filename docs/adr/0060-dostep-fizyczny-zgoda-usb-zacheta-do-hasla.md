# 0060. Dostęp fizyczny (0.13.0): zgoda na nowe urządzenie wejścia z USB w K1 (nadzorca nie daje go usłudze wejścia), zachęta do hasła przy pierwszym starcie

- Status: Przyjęty, wdrożony (plan 0.13.0, krok 5)
- Data: 2026-11-27
- Dotyczy: ADR 0013 (USB: klawiatury, myszy, pamięci — **uzupełniony**: urządzenie wejścia z USB po pierwszym pulpicie
  czeka na zgodę), ADR 0017 (hasło przy starcie), ADR 0007 (jeden właściciel zasobu: urządzenia wejścia otwiera nadzorca);
  plan `docs/archiwum/PLAN-0.13.0.txt` (krok 5, D6, D7, T10); Issue #235 (p. 3, 4); RYZYKA R33, R41; opisy:
  `docs/komponenty/nadzorca.md`, `docs/komponenty/wejscie.md`, `docs/komponenty/gui.md`

## Kontekst
Do 0.12.3 każde urządzenie USB działa od razu (R33): „pendrive”, który udaje klawiaturę (BadUSB), może w kilka sekund
napisać coś w otwartym oknie — także pod nieobecność właściciela. Bez hasła klucz skarbca wynika ze stałej etykiety: kto
weźmie laptopa albo pendrive z K1, czyta pliki (R41), a pierwszy start do hasła nie zachęca (L20–L22).

Decyzje właściciela (2026-11-27, szkice D6, D7): **D6: A** — nowa klawiatura albo mysz z USB: okienko „Pozwolić?”;
**D7: A** — przy pierwszym starcie bez hasła zachęta z „Później”.

## Decyzja
1. **Zgoda w K1, nie w jądrze (T10).** Usługa `wejscie` czyta tylko te urządzenia (`/dev/input/eventN`), które dostanie od
   nadzorcy (ADR 0007) — w K1 nie ma innego czytelnika klawiszy: bez VT, SysRq, `hidraw`, `hiddev`, `RFKILL_INPUT`
   (`jadro/k1.config`). Wystarczy więc, że nadzorca **nie da** usłudze `wejscie` urządzenia czekającego na zgodę: żaden jego
   klawisz nie dociera do K1. Tryb jądra `USB_DEFAULT_AUTHORIZATION_MODE=2` (zgoda przed sterownikiem) odpada: sysfs jest
   w K1 tylko do odczytu (init montuje go z `MS_RDONLY`), zgoda działałaby też na pendrive'y z plikami i karty sieciowe
   USB, a „wewnętrzny” port zależy od tablic ACPI producenta (NIEPEWNE na X230 i IdeaPadzie). Sterownik HID jądra dalej
   czyta opis urządzenia przed zgodą (R33 — audyt 0.14).
2. **Kiedy pyta (`src/nadzorca/wejscia.c`).** Bez pytania: urządzenia nie na USB (klawiatura i touchpad laptopa — PS/2,
   I2C, przyciski ACPI), urządzenia USB widziane przed pierwszym pulpitem tego startu (ekran hasła ich potrzebuje; złośliwe
   urządzenie podłączone przed włączeniem komputera wymaga, by ktoś włączył komputer — to już pełny dostęp fizyczny),
   urządzenie, na które właściciel w tym starcie kliknął „Pozwól” (znak: identyfikatory i nazwa z sysfs, `k1_urz_znak`),
   i każde urządzenie, gdy nie ma już żadnej dozwolonej klawiatury ani myszy (komputer stacjonarny z jedną klawiaturą nie
   może utknąć). Pamiętane są tylko odpowiedzi właściciela, nie urządzenia ze startu: kopia identyfikatorów myszy
   podłączonej przy starcie pyta. Reszta czeka. „Nie pozwalaj” — urządzenie zostaje bez dostępu, także po ponownym
   podłączeniu, do wyłączenia. Okienko zamknięte bez odpowiedzi — urządzenie czeka; ponowne podłączenie pyta znowu.
   Zgoda nie jest zapisywana w skarbcu (każdy start od nowa — mniej do podrobienia).
3. **Co to za urządzenie** — z bitmap sysfs (`capabilities/ev`, `key`, `rel`, `abs`, `properties`), według tych samych
   reguł co `wejscie` (litery i spacja — klawiatura; ruch względny — mysz; dotyk bez `INPUT_PROP_DIRECT` — touchpad = mysz;
   reszta — „inne”). „Mysz”, która też pisze, jest klawiaturą. Czy na USB — ścieżka sysfs urządzenia przechodzi przez
   magistralę `usbN`. Czyste funkcje: `src/wspolne/k1_urz_wejscia.c` (test `test_urz_wejscia`).
4. **Pytanie i odpowiedź** — `OCHRONA` (nowe `a`, bo wolnych numerów typów nie ma): nadzorca → gui (fd 5) `USB` b = co
   czeka (bity `K1_URZ_*`), c = ile (0: nic — okienko znika), d = numer pytania; gui → nadzorca `USB_ZGODA` b = 1/0,
   c = numer pytania. Odpowiedź do starszego pytania (w międzyczasie coś podłączono albo odłączono) jest pomijana — gui
   dostaje nowe pytanie. Odpowiedź przyjmowana tylko od UID pulpitu. „Pozwól” dotyczy wszystkich czekających urządzeń
   z pytania. Okienko rysuje pulpit (tabela okienek ochrony `src/gui/ochrona.c`), w panelu menu kontekstowego — jak
   okienka TPM; kliknąć może tylko urządzenie już dozwolone. Podrobione okienko w strefie niczego nie daje (zgoda idzie
   tylko z gui). Przed pierwszym pulpitem (ekran hasła przy starcie, tryb ratunkowy — bez pulpitu i bez danych) nikt nie
   pyta: urządzenie działa od razu. Po pierwszym pulpicie, przy zablokowanym ekranie, urządzenie czeka do odblokowania.
5. **Zachęta do hasła (D7: A).** Nadzorca w `SESJA` b = 1, gdy magazyn jest trwały (dysk, pendrive z K1-DANE) i bez
   hasła. Pulpit pokazuje okienko „Ustaw hasło” (z wyjaśnieniem, „Ustaw hasło” → Ustawienia → Hasło — nowy wiersz
   `K1_UST_MIEJSCA` „Hasło”, „Później”) **raz**: przy pierwszym starcie (skarbiec nie ma jeszcze rozmieszczenia pulpitu)
   i od razu zapisuje rozmieszczenie — następny start nie jest pierwszy. Przypomnienie zostaje w Ustawieniach → Hasło
   („Bez hasła dane są zaszyfrowane, ale bez ochrony przed kradzieżą komputera”). K1 z ISO (dane w pamięci) nie pyta.
6. **Kopia testowa nie zachęca** — chyba że test o to prosi dyskiem-znakiem „k1-test-haslo”. Testy QEMU klikają w środek
   ekranu po pierwszym starcie; okienko zasłoniłoby im okna. Kopię testową rozpoznaje nadzorca po konsoli jądra
   (`/sys/class/tty/console/active`: port szeregowy — `k1-test.efi`, `bzImage` testów; wydanie ma `console=ttynull` —
   ADR 0059). Obraz wydania (test `test-wydanie`) pokazuje okienko jak u właściciela.

## Skutki
- Klawiatura i mysz USB podłączone po włączeniu pytają raz na start (ta sama — bez pytania, póki K1 działa). Pendrive
  z plikami i przejściówki USB–Ethernet — bez zmian (R66, audyt 0.14).
- Okienko obsługuje się myszą albo touchpadem (jak inne okienka pulpitu).
- Bez hasła pierwszy start mówi wprost, co to znaczy; hasła nie wymuszamy (D7).

## Testy
- Na hoście: `test_urz_wejscia` (bitmapy klawiatury, myszy, tabletu i touchpada; NEGATYWNE: zepsute bitmapy, za długie,
  ścieżki PS/2 i I2C), `test_komunikat` (`OCHRONA USB`, `USB_ZGODA`, `SESJA` b).
- QEMU `test-m5.sh`: mysz USB po pulpicie czeka (wejscie jej nie dostaje), okienko „Podłączono nową mysz” (zrzut),
  „Pozwól” → wejscie z nią; ta sama mysz ponownie — bez pytania; NEGATYWNE: klawiatura USB → okienko, „Nie pozwalaj” →
  wejscie bez niej, jej klawisze nie ruszają kursora, odłączona — zapomniana, pierwsza klawiatura działa. Pendrive z
  K1-DANE po starcie (pierwszy start na trwałym magazynie) → okienko „Ustaw hasło” (zrzut) → Ustawienia → Hasło.
- `test-wydanie.sh`: pulpit obrazu wydania pokazuje okienko „Ustaw hasło” (zrzut, bez logu).
