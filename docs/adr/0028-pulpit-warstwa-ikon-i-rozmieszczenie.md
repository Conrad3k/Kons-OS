# 0028. Pulpit 0.7.5: warstwa ikon w ekranie, rozmieszczenie w skarbcu, menu kontekstowe

- Status: Przyjęty
- Data: 2026-11-09
- Dotyczy: ADR 0008 (pulpit i okna), ADR 0026 (zakres 0.7.5), ADR 0015 (skarbiec); wygląd: `docs/WYGLAD.md`

## Kontekst
ADR 0026 ustalił zakres 0.7.5: ikony aplikacji na pulpicie (zaznaczanie, przeciąganie, podwójne
kliknięcie), menu pod prawym przyciskiem myszy (pulpit, ikona, dolny pasek), przypinanie i odpinanie na
pasku ze zmianą kolejności, uruchomiona aplikacja spoza paska widoczna na nim, wszystko zapamiętane.
Właściciel wybrał szkic A „Kafelki” (jasne kafelki w kolorze okien, menu bez ikon) i dopowiedział, że
kafelki i menu mają zmieniać kolor z motywem (jasny / ciemny); przy pierwszym starcie na pulpicie są
wszystkie aplikacje oprócz Instalatora.

Do tej pory ekran znał dwa rodzaje powierzchni: okna aplikacji i panele powłoki (zawsze nad oknami).
Ikony muszą leżeć **pod** oknami, a kliknięcie w pustą tapetę musi dotrzeć do pulpitu (menu pulpitu,
odznaczenie). Rozmieszczenie musi przetrwać wyłączenie komputera, a gui do tej pory nie miało dostępu do
skarbca.

## Decyzja
1. **Warstwa pulpitu** — trzeci rodzaj powierzchni `K1_POW_PULPIT`: tylko dla powłoki, jedna, w punkcie
   0,0, w rozmiarze ekranu. Ekran składa ją zaraz nad tapetą i pod oknami; łapie wskaźnik w każdym
   pikselu (także przezroczystym), więc kliknięcia w pusty pulpit trafiają do gui. Gui rysuje i zatwierdza
   tylko zmieniony prostokąt (`k1_pow_zatwierdz_obszar` — pole już było w `ZATWIERDZ`), więc ruch myszy
   nad ikonami nie przerysowuje całego ekranu. Rozważone i odrzucone: osobna powierzchnia na każdą ikonę
   (limit 8 powierzchni na klienta, w 0.8.0 dojdą pliki), „wtapianie” warstwy w tapetę w ekranie (druga
   kopia tapety w pamięci ekranu, więcej kodu; do rozważenia, jeśli na X230 składanie okaże się wolne).
2. **Budżet pamięci** powłoki w ekranie: dwa pełne ekrany (jak aplikacja; warstwa + panele). Klatka gui:
   `RLIMIT_FSIZE` 72 MiB (dwie połówki 4096×2160 — limit dotyczy też memfd), pamięć 128 MiB; ekran hasła
   (jeden panel na cały ekran): 72 MiB / 96 MiB — wcześniejsze 16 MiB nie mieściło ekranu 1920×1200.
3. **Rozmieszczenie** (nowe pojęcie, słownik) — które aplikacje są przypięte do paska i w jakiej
   kolejności oraz w której komórce siatki leży każda ikona. Czyste reguły w `src/gui/rozmieszczenie.c`
   (test na hoście, fuzzing), zapis jako obiekt `K1_OB_ROZMIESZCZENIE` w skarbcu: **gui staje się klientem
   skarbca** (fd 6, właściciel = profil gui), więc przejęte gui widzi tylko swoje rozmieszczenie. Format:
   `"K1R1"`, liczba, aplikacje na pasku, liczba, (aplikacja, kolumna, wiersz) — czytany pole po polu; błąd =
   domyślne. Na mniejszym ekranie ikony spoza siatki idą na wolne miejsca. Słowo „układ” zostaje dla bloku
   układu K1-EFI.
4. **Menu kontekstowe** to panel gui nad wszystkimi innymi (tworzony ostatni): pulpit — „Ułóż ikony”,
   „Tapeta i wygląd…” (otwiera Ustawienia); ikona — „Otwórz”, „Przypnij do paska” / „Odepnij od paska”,
   „Usuń z pulpitu”; pasek — nazwa aplikacji, przypnij / odepnij, „Zamknij okno”; menu Start — „Otwórz”,
   przypnij / odepnij, „Dodaj do pulpitu” / „Usuń z pulpitu”.
5. **„Zamknij okno” z paska** — dwa nowe komunikaty: `ZAMKNIJ_OKNO` (tylko powłoka → ekran, numer okna)
   i `ZAMKNIJ` (ekran → aplikacja). Aplikacja zamyka się sama, jak przyciskiem × (Notatki najpierw zapisują
   tekst — nowy `zamykanie` w szkielecie aplikacji). Nadzorca nie zabija aplikacji na prośbę gui: zakres
   tego, co może przejęte gui (RYZYKA R27), rośnie tylko o „poproś okno o zamknięcie”.

## Konsekwencje
- Ekran: nowy rodzaj powierzchni i kolejność warstw (pulpit < okna < panele), testy negatywne w systemie
  (warstwa pulpitu od aplikacji, zamykanie cudzych okien przez aplikację — odrzucone).
- Przy każdym ruchu okna ekran składa także warstwę pulpitu w uszkodzonych prostokątach (przezroczyste
  piksele pomija jednym porównaniem). Wydajność na X230 — do sprawdzenia przez właściciela.
- Gui rośnie do 9 małych modułów (`gui.h`); każdy zgodny z limitami czytelności.
