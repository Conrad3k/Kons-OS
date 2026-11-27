# 0057. TPM i odwoływanie starszych wersji (0.13.0): generacja K1 w TPM 2.0 z blokadą danych, stan magazynu w TPM, SBAT w `k1.efi`

- Status: Przyjęty, wdrożony (plan 0.13.0, krok 2)
- Data: 2026-11-26
- Dotyczy: ADR 0021, 0022 (A/B, Secure Boot — **uzupełnione**: potwierdzenie startu zapisuje generację), ADR 0015
  (skarbiec — cofnięcie nośnika, RYZYKA R38), ADR 0007 (jeden właściciel zasobu); Issue #235 (p. 2); plan
  `docs/archiwum/PLAN-0.13.0.txt` (krok 2, T3–T6); opisy: `docs/komponenty/nadzorca.md`, `docs/komponenty/skarbiec.md`,
  `docs/komponenty/jadro.md`, `docs/komponenty/gui.md`, `docs/komponenty/aplikacje.md`

## Kontekst
Do 0.12.3 shim przyjmuje każdy starszy `k1.efi` podpisany kluczem K1 (R44): kto ma nośnik albo roota, może wgrać
starszą wersję z dziurą poprawioną od tamtej pory. Instalator i A/B nie zapiszą starszej wersji same, ale nic nie
sprawdza tego przy starcie. Skarbiec uwierzytelnia każdy rekord, ale **cały dysk danych cofnięty do starszej kopii**
otwiera się bez słowa (R38). Plan (L6–L11): K1 nie ma sterowników TPM, licznika wersji, sekcji `.sbat`, testu z TPM.

Z kodu i źródeł (2026-11-26, Linux 7.3.6):
- Starsze wersje K1 (do 0.12.3) nie znają TPM — odwołanie może działać tylko w wersjach od 0.13.0.
- TPM 1.2 (ThinkPad X230) w jądrze wymaga `PNP` (identyfikatory `PNP0C31`, `IFX0102`… — `drivers/acpi/acpi_pnp.c`,
  sterownik `tpm_tis_pnp`); TPM 2.0 (ACPI `MSFT0101`) — bez PNP: `tpm_tis` (FIFO) i `tpm_crb` (firmware Intela, PTT).
  Rozmowa z TPM 1.2 wymaga też właściciela TPM (TakeOwnership — szyfrowanie RSA kluczem EK), czego K1 nie ma.
- SBAT: `EFI_SBAT_FILE` (drivers/firmware/efi/Kconfig, od 6.16) dokłada sekcję `.sbat` do stubu EFI. Shim sprawdza
  SBAT obrazu weryfikowanego przez swój protokół (tak ładuje GRUB) **tylko, gdy sekcja jest** (brak = dozwolony), a
  minimalne generacje bierze ze zmiennej `SbatLevel`, którą pisze **wyłącznie sam shim** z listy wbudowanej przez jego
  wydawcę (Microsoft/Canonical) — wpisu „k1” tam nie będzie. SBAT nie odwoła więc K1 dziś; jest potrzebny, gdyby K1
  kiedyś miał własny shim albo był ładowany wprost przez shima (tam `.sbat` jest obowiązkowa).

## Decyzja
1. **Generacja K1** (`K1_GENERACJA`, `src/wspolne/k1_tpm.h`, tabela z powodami w komentarzu): liczba podnoszona tylko
   wtedy, gdy wydanie poprawia dziurę, której starsze wersje nie mogą mieć z powrotem (nie z każdą wersją — TPM ma
   ograniczoną liczbę zapisów, a wersja bez poważnej dziury nie musi odcinać poprzedniej). 0.13.0 = generacja 1. Ta sama
   liczba trafia do sekcji `.sbat` w `k1.efi` (`k1,<generacja>,Kons K1,k1,<wersja>,…`, `scripts/zbuduj-k1efi.sh`).
2. **TPM tylko 2.0, tylko nadzorca (T3).** Jądro: `TCG_TPM`, `TCG_TIS`, `TCG_CRB` (bez PNP, bez `HW_RANDOM_TPM`, bez
   `TCG_TPM2_HMAC`). `/dev/tpm0` otwiera nadzorca na jedno polecenie i zamyka (`src/nadzorca/tpm.c`) — żadna usługa
   ani aplikacja go nie dostaje (niezmiennik 7). Bez TSS i tpm2-tools: cztery polecenia w jednej tabeli
   (`K1_TPM_POLECENIA`: NV_ReadPublic, NV_DefineSpace, NV_Read, NV_Write) z sesją hasła `TPM_RS_PW` i pustym hasłem;
   odpowiedź czyta czysty parser (`k1_tpm.c`, `test_tpm`, `fuzz_tpm`), który przyjmuje tylko dokładny kształt odpowiedzi
   na dane polecenie — TPM to obcy kod w układzie. TPM 1.2, brak TPM, TPM 2.0 z hasłem właściciela ustawionym przez inny
   system albo z cudzym indeksem pod naszym numerem: K1 działa jak przed 0.13 i mówi to (log; Ustawienia → Aktualizacje:
   „bez TPM”, „TPM 1.2 nieużywany”, „TPM niedostępny”, „chroni TPM”).
3. **Zapis K1 w TPM:** jeden indeks NV `K1_TPM_INDEKS` = 0x01004B31 (zakres właściciela), 64 B, atrybuty AUTHWRITE |
   AUTHREAD | NO_DA (puste hasło indeksu, bez blokady słownikowej), SHA-256, bez polityki; zakłada go K1 przy pierwszym
   starcie (hasło właściciela puste — tak jest na nowym komputerze). Układ: „K1T1”, generacja (u32), 3 magazyny po
   16 B (8 B identyfikatora — początek uuid magazynu, generacja nagłówka u64 z bitem 63 „cofnięte”). Indeks z innymi
   atrybutami, rozmiarem albo nieczytelny zapis → K1 go nie rusza („TPM niedostępny”).
4. **Licznik wersji (T5) — blokada danych (decyzja właściciela 2026-11-26: „Blokada danych”).** Nadzorca czyta TPM na
   starcie, przed skarbcem. Generacja w TPM wyższa niż tej wersji → ta wersja jest starsza niż ta, którą miał ten
   komputer: skarbiec startuje **bez K1-DANE** (dane tylko w pamięci, dysk nietknięty), gui pokazuje okienko „Ta wersja
   K1 jest za stara” (z „Pokaż w Ustawieniach”), Ustawienia → Aktualizacje — wiersz pod kafelkiem; A/B tej wersji nie
   potwierdza. Wyjście: nowsza wersja (aktualizacja przez sieć albo Instalator z pendrive'a — nie potrzebują danych).
   Generację podnosi **potwierdzony start** (`rozruch.c`, 15 s po pulpicie albo ekranie hasła, ten sam moment co A/B):
   wersja o wyższej generacji, która naprawdę działa, zapisuje swoją — raz na generację. Także z pendrive'a (nowszy K1
   z pendrive'a odcina starszy na dysku tego komputera: ma znane dziury; aktualizacja go wyprowadza).
5. **Cofnięcie skarbca (R38, T6) — bez zmiany formatu.** Liczba „generacja nagłówka” magazynu rośnie z każdym zapisem
   nagłówka. Skarbiec mówi nadzorcy (`SKARB_EPOKA`) identyfikator magazynu i generację nagłówka, która **jest na dysku**
   (zapisana i zsynchronizowana — `k1_magazyn.na_dysku`; nigdy taka tylko w pamięci, bo nieudany zapis wyglądałby przy
   następnym starcie jak cofnięcie): zaraz po otwarciu i po pierwszym zapisie tej sesji (`src/skarbiec/epoka.c`).
   Nadzorca: przy otwarciu magazyn starszy niż pamięta TPM → „dane cofnięte”: znak w TPM (zostaje także po kolejnych
   zapisach i startach), okienko gui „Dane na dysku są starsze niż ostatnio” z „To moje dane” (kopia, którą właściciel
   przywrócił sam — nadzorca zapisuje stan od nowa) i „Później” (zapyta znowu przy następnym starcie). **Dane działają**
   (inaczej niż przy starszej wersji: to dane właściciela, a nie znana dziura). Po pierwszym zapisie (bez znaku) TPM
   pamięta nową generację. Zapis do TPM najwyżej raz na start (plus jeden przy decyzji). Magazyn, którego TPM nie zna
   (nowy, pendrive z innego komputera), nie jest porównywany — pendrive na innym komputerze nie daje fałszywego alarmu;
   TPM pamięta 3 ostatnie magazyny.
6. **Program testowy** (tylko obraz testowy) może wpisać wyższą generację (`OCHRONA` a = `K1_OCHR_TEST`), nigdy 0 — tak
   test udaje nowszą wersję. Komunikaty: `OCHRONA` (185: stan do gui i Ustawień, decyzja z gui, test), `SKARB_EPOKA`
   (118, skarbiec → nadzorca); sprawdzane pole po polu (`test_komunikat`, `fuzz_komunikat`).
7. **SBAT (T4):** sekcja `.sbat` z generacją w `k1.efi` (tylko w zamkniętym jądrze). Sprawdzone w OVMF z shimem i GRUB-em
   (`test-m6`): `k1.efi` z sekcją startuje jak dotąd. Odwołanie w praktyce robi pkt 4; SBAT czeka na własny shim.

## Czego to nie chroni
- **Wersje do 0.12.3** nie sprawdzają TPM — kto podsunie K1 0.12.x, ominie blokadę (dopóki shim nie odwoła klucza).
- **Inny system** uruchomiony z pendrive'a (Secure Boot z kluczami Microsoftu go dopuszcza) może z pustym hasłem
  właściciela skasować indeks K1 albo wyczyścić TPM — wtedy K1 zakłada indeks od nowa i nie wie o starszej wersji ani
  o cofnięciu. To samo robi „Clear TPM” w BIOS-ie. Mocniej: hasło właściciela TPM (K1 nie ma gdzie go trzymać) — „Później”.
- **Bez hasła magazynu** klucz nagłówka wynika z publicznych danych (ADR 0015), więc ktoś z dyskiem może podnieść
  generację w starej kopii i ominąć wykrycie cofnięcia; z hasłem — nie.
- **Kopia z tej samej sesji** (zrobiona po pierwszym zapisie, przed następnym startem) wygląda na nowszą niż TPM —
  wykrycie ma ziarno jednego startu.
- **X230 (TPM 1.2)** nie ma żadnej z tych ochron (K1 nie widzi TPM 1.2 bez PNP i nie umie zostać jego właścicielem).

## Skutki
- Laptop z TPM 2.0 (IdeaPad: Intel PTT — gdy włączony w BIOS-ie) dostaje licznik wersji i wykrywanie cofniętych danych
  bez żadnej czynności właściciela. Wydanie, które podnosi generację, odcina starsze wersje od danych na każdym
  laptopie, który je potwierdził — świadomy koszt (wyjście: aktualizacja). Generację zmienia człowiek w kroku planu
  albo wydaniu z poprawką bezpieczeństwa (robot nie).
- Nowe ryzyko: TPM to obcy kod i nowa droga danych do nadzorcy (roota) — parser czysty i z fuzzingiem, polecenia
  w jednej tabeli, węzeł otwierany na chwilę (RYZYKA R112).
- Testy: host `test_tpm`, `fuzz_tpm`, `test_komunikat`; w systemie `t_tpm` (bez TPM: odmowa, generacja 0 odrzucona);
  QEMU `test-tpm.sh` (swtpm: nowy TPM, drugi start, cofnięty dysk i „To moje dane”, starsza wersja — dysk nietknięty);
  `test-m6` (SBAT w `k1.efi` z Secure Boot).
