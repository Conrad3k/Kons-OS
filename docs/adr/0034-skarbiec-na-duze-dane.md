# 0034. Skarbiec na duże dane (0.8.1, krok 2)

- Status: Przyjęty
- Data: 2026-11-11
- Dotyczy: ADR 0015 (format skarbca — **bez zmian na dysku**), ADR 0029 (pliki), `docs/PLAN-0.8.1.txt` (luki L4c, L4d,
  L5, L6, L21), RYZYKA R57

## Kontekst
Od 0.8.0 skarbiec trzyma pliki użytkownika, a K1-DANE na pendrivie sięga do końca nośnika (ADR 0033). Przegląd 0.8.1
(próby T1, T3 planu) pokazał, że format wytrzyma, ale skarbiec nie:
- **limity** — 4096 obiektów (pliki są w kawałkach po 1 MiB: najwyżej ok. 4 GiB plików na dowolnie dużym dysku) i 1024
  wpisy plików i folderów razem z Koszem;
- **wyszukiwanie liniowe** — każdy obiekt, każdy wpis i każdy plik z pendrive'a szukany po kolei; po podniesieniu
  limitów start i import byłyby kwadratowe;
- **długi start** — przy otwarciu skarbiec czyta i sprawdza cały dziennik aktywnego obszaru (każdą starą wersję), a
  porządkuje go (kompaktowanie) dopiero, gdy obszar się zapełni: na pendrivie 32 GB to 16 GB historii; dziennik
  867 MiB = 2,2 s procesora, ok. 30 s z pendrive'a USB 2.0 (T3/C);
- **pełny dysk** — gdy pliki zajmą wszystko, nie zapisują się też ustawienia, ułożenie pulpitu, historia baterii.

## Decyzja
1. **Indeks zamiast szukania po kolei** (`src/wspolne/k1_indeks.c`): adresowanie otwarte z próbkowaniem liniowym, tablica
   o rozmiarze potęgi dwójki, co najmniej dwa razy większa niż liczba wpisów; usuwanie przesuwa następne wpisy w tył
   (bez „nagrobków”). Używają go: magazyn (obiekty po właścicielu i nazwie), obiekty w pamięci (bez K1-DANE), pliki
   w skarbcu (wpisy po numerze) i czytnik pendrive'a (wpisy po położeniu na nośniku). Test `test_indeks` (200 000
   losowych operacji, także przy wymuszonych kolizjach).
2. **Limity:** `K1_MAG_MAKS_OBIEKTOW` 4096 → **65 536** (ok. 64 GiB plików w kawałkach po 1 MiB; indeks w pamięci
   skarbca ok. 4,6 MiB), `K1_DOK_MAKS` 1024 → **16 384** wpisy (lista w memfd 2,9 MiB). Aplikacje mieszczą się
   w swoich 32 MiB (Pliki ok. 9 MiB, Notatki 11 MiB, Wybór pliku 9 MiB danych statycznych). Usuwanie drzewa folderów i
   opróżnianie Kosza w jednym przejściu (zaznacz, posortuj od najgłębszych, usuń) zamiast zaczynania od nowa po każdym
   usunięciu; sprawdzanie powtórzonych numerów na liście przez sortowanie.
3. **Kompaktowanie, zanim dziennik urośnie** (format bez zmian — przepisuje żywe obiekty do drugiego obszaru, jak
   dotąd przy pełnym obszarze):
   - **przy otwarciu** — gdy dziennik > 64 MiB i > 2 × żywe dane: od razu po wczytaniu (koszt nie większy niż samo
     czytanie dziennika, które i tak było); następny start czyta już tylko żywe dane;
   - **w pracy** — gdy stare wersje zajmują o 256 MiB więcej niż żywe dane: w chwili, gdy przez 2 s żaden klient o nic
     nie prosi (`main.c`, `ob_kompaktuj_w_przerwie`).
   W logu czas i rozmiary. Skutek: dziennik nigdy nie przekracza ok. 2 × żywe dane + 256 MiB (+ to, co dopisano przed
   chwilą przerwy).
4. **Rezerwa dla systemu:** nowy plik i nowa treść pliku (`DOK_NOWY`, `DOK_ZAPISZ`) są odrzucane, gdy zostałoby mniej
   niż 8 MiB albo 2 % obszaru (większe z nich) — nowy wynik `K1_DOK_BLAD_BRAK_MIEJSCA` (−28), w Plikach „Brak miejsca na
   pliki”. Obiekty systemu (ustawienia, pulpit, bateria, tapeta) zapisują się dalej. Zmiana nazwy, przeniesienie,
   Kosz i usuwanie nie potrzebują miejsca — użytkownik zawsze może zrobić miejsce.
5. **Format na dysku bez zmian** — magazyn z 0.8.0 otwiera się tak samo (najwyżej raz kompaktuje). Powrót do 0.8.0 to
   tylko wycofanie nieudanej aktualizacji przy pierwszym starcie (ADR 0021, miejsca A/B) — z danymi sprzed 0.8.1.
   Gdyby 0.8.0 dostała magazyn z ponad 4096 obiektami, nie otworzy go („uszkodzony” — partycja nietknięta, ADR 0015);
   z ponad 1024 wpisami usunęłaby treść tych, które się nie zmieściły (luka L2, naprawiona dopiero w 0.8.1) — dlatego
   Instalator nie instaluje starszych wersji, a ręczne cofanie do 0.8.0 nie jest przewidziane.

## Konsekwencje
- K1 mieści dziesiątki tysięcy plików; start nie zwalnia z czasem (dziennik z 20 000 starych wersji: pierwszy start
  kompaktuje, następne czytają jeden rekord — `test_magazyn`, `test-skarbiec.sh`).
- Kompaktowanie dużych danych to jedno długie, blokujące kopiowanie (żywe 700 MiB ≈ 4 s procesora, ok. 50 s z pendrive'a
  USB 2.0 — T3/E); dzieje się rzadko i w przerwie, ale w tym czasie skarbiec nie odpowiada (R57). Kompaktowanie po
  kawałku wymagałoby zmiany formatu — osobny ADR, jeśli okaże się potrzebne.
- Połowa K1-DANE zostaje na kompaktowanie (ADR 0015) — bez zmian; krok 3 planu pokaże to w Ustawieniach.
- Testy: `test_indeks`, `test_magazyn` (65 536 obiektów, długi dziennik, kompaktowanie przy otwarciu i w pracy, awaria
  w połowie kompaktowania i przy zapisie nagłówka), `test_dokumenty` (16 384 wpisy, drzewo tysięcy wpisów, rezerwa),
  `fuzz_magazyn`, `fuzz_dokument`; QEMU `test-skarbiec.sh` (długi dziennik, prawie pełna K1-DANE 64 MiB).
