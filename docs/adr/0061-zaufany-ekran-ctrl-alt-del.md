# 0061. Zaufany ekran K1 (0.13.0): Ctrl+Alt+Del zawsze prowadzi do ekranu zabezpieczeń, hasło K1 tylko tam

- Status: Przyjęty, wdrożony (plan 0.13.0, krok 6)
- Data: 2026-11-27
- Dotyczy: ADR 0017 (hasło przy starcie — **uzupełniony**: hasło ustawia się i zmienia tylko na ekranie zabezpieczeń),
  ADR 0060 (okienko „Ustaw hasło” otwiera teraz ekran zabezpieczeń), ADR 0008 (protokół ekranu: nowy rodzaj powierzchni);
  plan `docs/PLAN-0.13.0.txt` (krok 6, D8, T11); Issue #236; RYZYKA R26, R40, R50, R54, R80; opisy:
  `docs/komponenty/ekran.md`, `docs/komponenty/gui.md`, `docs/komponenty/skarbiec.md`, `docs/komponenty/aplikacje.md`

## Kontekst
Okno aplikacji i strona w strefie rysują w swoim prostokącie dowolne piksele (R26, R80) — także coś, co wygląda jak
prośba K1 o hasło. Do 0.12.3 K1 sam pytał o hasło w oknie aplikacji (Ustawienia → Hasło), więc właściciel nie miał jak
odróżnić prawdziwej prośby od podróbki. Nie było też skrótu, którego żadne okno nie dostaje i który zawsze prowadzi do K1
(jak Ctrl+Alt+Del w Windows) — Alt+Tab i klawisz Start rozpoznaje ekran od 0.10.5, ale to nie jest ekran „tylko K1”.

Decyzja właściciela (2026-11-27, szkic D8): **A** — sam skrót Ctrl+Alt+Del i ekran zabezpieczeń; hasło ustawia się
i zmienia tylko tam; Ustawienia → Hasło pokazują stan i podpowiedź „Ctrl+Alt+Del → Hasło”; okno przeglądarki skrótu nie
dostaje. Bez znaku na górnym pasku przy fokusie strefy (wariant odrzucony w D8).

## Decyzja
1. **Skrót rozpoznaje ekran (`src/ekran/zabezpieczenia.c`).** Delete (także kropka klawiatury numerycznej) wciśnięty
   z Ctrl i Alt nie trafia do żadnego okna — ani aplikacji, ani strefy — tylko do powłoki (gui), która pokazuje ekran
   zabezpieczeń. Puszczenie tego Delete też nie trafia nigdzie. Pełny ekran strefy się kończy; strefa z fokusem dostaje
   puszczenie Ctrl i Alt (jej gość widział je wciśnięte — inaczej zostałyby „wciśnięte” w gościu). Na ekranie hasła przy
   starcie (logowanie) skrót jest zwykłym klawiszem — tam i tak jest tylko K1.
2. **Ekran zabezpieczeń to osobny rodzaj powierzchni** — `K1_POW_ZABEZPIECZENIA` (4): tylko powłoka, tylko jedna (ekran
   odmawia aplikacji i drugiej — test w systemie `t_okna`). Ekran składa ją **nad wszystkim**, także nad oknem strefy na
   pełnym ekranie (warstwa 4), a resztę ekranu przyciemnia (ćwierć obrazu na najciemniejszym brązie K1). Dopóki jest
   widoczna, każdy klawisz i każde kliknięcie idą do niej — nic, co tam wpisane (hasło), nie trafia do okna. Okno nie
   może jej narysować ani zasłonić, a otwiera ją tylko Ctrl+Alt+Del albo pulpit (okienko „Ustaw hasło”) — żadne okno.
   Poza limitem pamięci powierzchni powłoki (jedna, nie większa od ekranu — R31), żeby pokazała się zawsze.
3. **Co jest na ekranie zabezpieczeń (`src/gui/zabezpieczenia.c`, panel 480 × 560 na środku):** „Zablokuj” (tylko
   z hasłem), „Hasło” (ustaw, zmień, wyłącz), „Uruchom ponownie”, „Wyłącz”; Esc wraca do pulpitu. Wygląd zawsze ciemny
   (na przyciemnionym ekranie), w obu motywach jednakowy. Napis „Ten ekran rysuje tylko K1. Ctrl+Alt+Del zawsze tu
   prowadzi.” i „Hasło K1 wpisuj tylko tutaj albo przy włączaniu.” uczy właściciela, gdzie hasło jest prawdziwe.
4. **Hasło tylko tam.** Formularz hasła (ustaw, zmień, wyłącz) przeniesiony z Ustawień do gui (`src/gui/haslo.c`): hasło
   w zapieczętowanym memfd do skarbca, wyczyszczone zaraz po wysłaniu, nigdy w logu (jak dotąd). Skarbiec przyjmuje
   `SKARB_HASLO` **tylko od profilu gui** (do 0.12.3 — od Ustawień). Ustawienia → Hasło pokazują stan („Hasło przy
   włączaniu: włączone / wyłączone”) i podpowiedź „naciśnij Ctrl+Alt+Del i wybierz „Hasło””; okno, także Ustawień, nigdy
   nie prosi o hasło K1. Wiersz `K1_UST_MIEJSCA` „Hasło” z kroku 5 usunięty: okienko „Ustaw hasło” (ADR 0060) otwiera
   ekran zabezpieczeń od razu na formularzu.
5. **Ścieżka klawiszy hasła** (wejscie → ekran → gui) zostaje jak dla każdego klawisza (R40); jej przegląd — audyt 0.15
   (Issue #236 p. 3).

## Skutki
- Prośba o hasło K1 poza ekranem zabezpieczeń i ekranem przy włączaniu to zawsze podróbka — łatwa reguła dla właściciela.
- Zmiana hasła wymaga klawiatury (Ctrl+Alt+Del); okienko „Ustaw hasło” otwiera ekran zabezpieczeń także myszą.
- Strefa nie wie, że użytkownik nacisnął Ctrl+Alt+Del (nie dostaje Delete), więc nie może na to zareagować.

## Testy
- Na hoście: `test_komunikat` (`POWIERZCHNIA` rodzaj 4 z położeniem, poza zakresem — odrzucona).
- W systemie: `t_okna` — NEGATYWNE: aplikacja nie dostanie ekranu zabezpieczeń (brak ramki dla takiej powierzchni).
- QEMU `test-m4.sh`: Ustawienia → Hasło bez formularza; Ctrl+Alt+Del przy oknie Ustawień — ekran zabezpieczeń, okno nie
  dostaje Delete; „Hasło” → formularz; NEGATYWNE: różne hasła odrzucone; hasło ustawione; Esc zamyka; z hasłem
  Ctrl+Alt+Del → „Zablokuj” → ekran hasła; hasło nie trafia do logu.
- `test-strefa.sh`: Ctrl+Alt+Del przy fokusie strefy — gość dostaje tylko puszczenie Ctrl i Alt (nie Delete), ekran
  zabezpieczeń nad strefą, reszta przyciemniona (zrzut), kliknięcie na nim nie trafia do strefy, Esc — strefa dalej
  dostaje klawisze.
- `test-m5.sh`: okienko „Ustaw hasło” → ekran zabezpieczeń z formularzem.
- `test-wzorce.sh`: wzorce `ekran-zabezpieczen`, `ekran-zabezpieczen-haslo`, nowe `ustawienia-haslo-*`.
