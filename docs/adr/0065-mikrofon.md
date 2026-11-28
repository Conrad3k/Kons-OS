# 0065. Mikrofon w K1 i w przeglądarce (0.13.5)

- Status: Przyjęty; wdrożenie — plan `docs/PLAN-0.13.5.txt`, krok 1 (mikrofon w K1) i krok 2 (mikrofon w przeglądarce)
- Data: 2026-11-28
- Dotyczy: ADR 0054 (usługa dźwięku — jedyny właściciel karty; tu: także jej nagrywania), ADR 0055 (tor dźwięku), ADR 0061
  (ekran zabezpieczeń), ADR 0042 (VMM strefy, karta virtio-snd), ADR 0064 (Discord z głosem — ta sama droga),
  `docs/komponenty/dzwiek.md`, `strefa.md`, `gui.md`, `aplikacje.md`, `docs/RYZYKA.md`

## Kontekst
Mikrofon był „Później” od 0.12.0 (ADR 0054, D1 = A), potem krokiem 4 a–b planu 0.14.0. Właściciel 2026-11-28: „napisz plan
na 0.13.5, w którym będzie dodanie obsługi mikrofonu”, „raczej cyfrowa obsługa” (mikrofon wbudowany laptopa jest cyfrowy).
Mikrofon to podsłuch, gdy coś pójdzie źle: kto go słyszy, musi być jasne i ograniczone, a użytkownik musi widzieć, że ktoś
słucha — znakiem, którego żadna strona nie ukryje.

Z kodu po 0.13.2: usługa dźwięku tylko odtwarza (węzeł odtwarzania karty, 14 ioctl ALSA w klatce), nadzorca szuka tylko
węzła odtwarzania, wszystkie 255 numerów typów `k1_komunikat` są zajęte (ustalenie planu 0.13.2), karta virtio-snd strefy
ma jeden strumień wyjścia.

## Decyzja
Właściciel (2026-11-28): **D1 = A** (przełącznik „Mikrofon” przy każdej sesji przeglądarki, domyślnie wyłączony; strona
i tak pyta), **D2 = A** (znak — osobna terakotowa pigułka na górnym pasku i na pasku okna sesji; wyciszony: szara,
przekreślona), **D3 = A** (Ustawienia → Dźwięk: źródło, poziom, „Wycisz”, „Powiedz coś” z paskiem poziomu). Techniczne:

1. **T1 — nagrywa ta sama usługa dźwięku** (jeden właściciel karty, ADR 0054 pkt 1). Nadzorca daje jej z kartą jej pierwszy
   węzeł nagrywania (`pcmC<k>D<u>c`, `DZWIEK_KARTA` NAGRYWANIE). 48 kHz, S32_LE (HDA: 20 albo 24 bity w słowie), inaczej
   S16_LE; dwa kanały, inaczej jeden (do obu). Bez przetwarzania (bez tłumienia echa i szumu — „Później”). Jedyny nowy ioctl
   klatki: `READI_FRAMES`; start nagrywania — pierwszy odczyt (bez `START`). Kontrolki kodeka: „Capture Switch” włączony,
   „Capture Volume” na 0 dB; wzmocnienia („Boost”) zostają, jak ustawi jądro (NIEPEWNE na laptopach — test właściciela).
   Poziom mikrofonu K1 0…100 to wzmocnienie usługi: 50 = 0 dB, 100 = +20 dB, 0 = cisza (0,4 dB na krok).
2. **Mikrofon wbudowany a w słuchawkach** przełącza samo jądro (parser HDA, auto-mic); usługa tylko czyta gniazdo mikrofonu
   („Mic Jack”, „Headset Mic Jack” — nigdy „… Phantom Jack”) i mówi, który nagrywa. L5 (mikrofon cyfrowy): na X230 i
   IdeaPadzie oczekujemy wariantu (a) — mikrofon w kodeku HD Audio; D4 (procesor dźwięku Intela, pliki od Intela) tylko,
   jeśli raport z laptopa pokaże, że mikrofonu w kodeku nie ma.
3. **T2 — pierścień w drugą stronę.** Słuchacz próbek daje swój pierścień (memfd dokładnego rozmiaru, z pieczęciami
   przeciw zmniejszaniu i powiększaniu, bez pieczęci zapisu); usługa mapuje go **tylko do zapisu**, pisze ramki i mówi, ile
   (`DZWIEK_NAGRANO` a), sama liczy pozycję — nie bierze od klienta żadnej. Wolny słuchacz traci swoje najstarsze ramki,
   niczyje inne. Słuchacz poziomu (Ustawienia) nie ma pierścienia: dostaje sam poziom 0…100 dziesięć razy na sekundę.
4. **Kto może słuchać — kolumna tabeli `K1_DZW_KLIENCI`** (zasada 5): pulpit — nigdy (rysuje znak, wycisza), Ustawienia —
   tylko poziom, program testowy — próbki, strefa — próbki, ale tylko gdy nadzorca na to pozwoli (`DZWIEK_KLIENT` d = 1:
   sesja z włączonym przełącznikiem „Mikrofon”, krok 2). Klient, który prosi o więcej, traci kanał.
5. **T3 — komunikaty bez szerszego typu.** Pary `DZWIEK_KOREKTOR`/`_STAN` i `DZWIEK_SCENA`/`_STAN` miały te same pola —
   teraz jeden typ chodzi w obie strony kanału klienta (ustawienie i jego stan), a zwolnione numery 49 i 251 to
   `DZWIEK_MIKROFON` (klient → usługa: USTAW poziom i wyciszenie, POZIOM — słucham poziomu, WSTRZYMAJ — tylko pulpit,
   SESJA — wyciszenie mikrofonu sesji, TESTOWY — tylko program testowy) i `DZWIEK_NAGRANO` (usługa → słuchacz). Kierunek
   strumienia w polu b `DZWIEK_OTWORZ`/`ZAMKNIJ`; stan mikrofonu dla klientów sterujących w `DZWIEK_STAN` e (źródło,
   poziom, wyciszony, wstrzymany, „K1 słucha” — `k1_dzw_mik_pole`); sesja, która słucha, w `DZWIEK_SESJA` e. Koszt
   szerszego typu (16 bitów: nowa wersja komunikatu w każdym programie i teście) — za duży na jeden krok; zostaje w
   „Później”. Zasada „typ = jeden kierunek” ma wyjątek: para przeciwnych kierunków jednego kanału z tymi samymi polami.
6. **T4 — prywatność.** Karta nagrywa tylko, gdy ktoś słucha (nie „na zapas”). Gdy na wierzchu jest ekran zabezpieczeń
   pulpitu (Ctrl+Alt+Del), usługa daje wszystkim słuchaczom ciszę (`DZWIEK_MIKROFON` WSTRZYMAJ od pulpitu; koniec kanału
   pulpitu zdejmuje wstrzymanie). Przy ekranie hasła na starcie słuchać nie ma kto (nie ma pulpitu, aplikacji ani stref).
   W logu tylko kto słucha, ile przepełnień i — w Ustawieniach — najgłośniejszy poziom jednej próby; nigdy próbki ani
   poziomy w czasie (niezmiennik 8).
7. **Znak „mikrofon włączony” (D2: A)** rysuje pulpit na swoim górnym pasku ze stanu od usługi (nie od słuchacza):
   terakotowa pigułka po lewej od sieci, baterii i dźwięku, wyciszony — szara, przekreślona. Kliknięcie — okienko
   „Mikrofon”: kto słucha, „Wycisz mikrofon”. Klawisz laptopa „wyłącz mikrofon” (KEY_MICMUTE) wycisza zawsze. Pamięć poziomu
   i wyciszenia — rekord pulpitu „K1G5” w skarbcu. Krok 2: ten sam znak na pasku okna sesji (rysuje ekran, z pulpitu).
8. **T5 — mikrofon strefy (krok 2).** Karta virtio-snd VMM-a dostaje drugi strumień — nagrywanie — tylko w sesji
   z przełącznikiem: bez niego urządzenie ma jeden strumień i gość nie wie o mikrofonie. Prośby gościa o wejście pole po
   polu jak dziś o wyjście; VMM jest słuchaczem próbek (pierścień F32).
9. **T6 — test bez prawdziwego mikrofonu.** QEMU: drugi kodek z wejściem (`hda-micro` z `-audiodev none` — nagrywa ciszę),
   do mikrofonu program testowy dokłada ton 1 kHz −12 dB (`DZWIEK_MIKROFON` TESTOWY, tylko jego wiersz). Test w systemie
   bez karty: kto może słuchać i czego (testy negatywne). Prawdziwy mikrofon — właściciel na X230 i IdeaPadzie.

## Konsekwencje
- Nowe części: `src/dzwiek/nagrywanie.c`, `mikrofon.c`, `src/wspolne/k1_mikrofon.c` (czyste: `test_mikrofon`,
  `fuzz_mikrofon`), Ustawienia `mikrofon.c`, pulpit `mikrofon.c`, `t_mikrofon.c`; `src/dzwiek/main.c` podzielony przed
  rozbudową (`klienci.c`).
- Ryzyko nowe: mikrofon jako podsłuch (`RYZYKA.md`) — wejście tylko przy słuchaczu z prawem z tabeli, znak rysowany przez
  K1, wyciszenie jednym kliknięciem i klawiszem, cisza przy ekranie zabezpieczeń.
- Plan 0.14.0, krok 4 (Discord z głosem) idzie tą drogą: wiersz aplikacji dostanie prawo słuchania jak sesja przeglądarki.
