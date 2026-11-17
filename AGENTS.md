# Kons K1 Sovereign — zasady dla asystentów (Claude, Codex)

Jedno źródło zasad dla każdego asystenta pracującego w tym repo. `CLAUDE.md` dołącza ten plik;
Codex czyta go bezpośrednio. Zmieniasz zasadę — zmieniasz ją tutaj.

## Projekt
Własny system operacyjny na jądrze Linux: nasz initramfs, init, nadzorca usług i grafika w C,
statycznie z musl. Budowany skryptami w `scripts/`, testowany w QEMU, CI w GitHub Actions (tylko `main`).

- Architektura, niezmienniki bezpieczeństwa, przepisy (nowa usługa, aplikacja, protokół, opcja jądra,
  testy), proces i raport: **`.claude/skills/kons-dev/SKILL.md`** — przeczytaj przed pracą (Codex: jako
  zwykły dokument).
- Specyfikacja nadrzędna: `docs/SPEC.md` (zmiany tylko przez ADR w `docs/adr/`).
- Mapa repo, konwencje, wersje: `docs/ARCHITEKTURA.md`. Testy: `docs/TESTY.md`. Wygląd: `docs/WYGLAD.md`.
  Pojęcia (jedno słowo = jedno znaczenie): `docs/SLOWNIK.md`.
- Historia zmian: `CHANGELOG.md`; wersja: `VERSION` (SemVer, tagi `k1-vX.Y.Z`).
- Stara dystrybucja Kons-OS (Debian): gałąź `archiwum/debian-0.1.2` — nie rozwijamy.
- **Plan — jedno miejsce (ADR 0035):** co teraz, mapa drogi (0.9 Sieć, 0.10 Strefy… — wersje zamiast etapów M),
  otwarte decyzje: `docs/DO_ZROBIENIA.txt`; kroki bieżącej wersji: jej plan `docs/PLAN-<wersja>.txt` (jeden krok =
  jeden czat = jeden PR, zakres zamrożony). Tu stanu nie powtarzamy.

## Właściciel
- Pisze po polsku i nie jest programistą. Dokumentacja, PR-y, komunikaty w systemie i raporty — po
  polsku, prostym językiem; commity i komentarze w kodzie — po angielsku.
- Po każdej nowej wersji podaj bezpośredni link do ISO i obrazu USB z wydania:
  `https://github.com/Conrad3k/Kons-OS/releases/download/k1-v<wersja>/k1-<wersja>.iso` oraz
  `…/k1-<wersja>-usb.img.xz` — dopiero gdy wydanie naprawdę istnieje.
- Testuje na ThinkPadzie X230 (UEFI, Secure Boot na razie wyłączony) i — od 2026-11-13 — na Lenovo IdeaPad 110-15ISK.

## Czytelność (decyzja właściciela, 2026-11-09)
System ma być czytelny i intuicyjny od środka, także gdy rośnie. Części może być dużo (izolacja tego
wymaga), ale każda jest mała, robi jedną rzecz i wygląda jak pozostałe.
1. **Limity wielkości:** plik `.c` ≤ 600 wierszy, funkcja ≤ 80 wierszy (bez `src/zewnetrzne/`); od 0.9.0 pliki testów
   (`testy/**/*.c`) też ≤ 600 wierszy (bez limitu funkcji — długa lista sprawdzeń czyta się dobrze). Większe —
   podziel; wyjątek tylko z komentarzem `/* k1: limit — <powód> */` i wpisem w opisie PR-a. Od 0.7.5
   sprawdza to skrypt w testach.
2. **Każda wersja MINOR kończy się porządkami:** co usunąć, scalić, uprościć — wynik w CHANGELOG
   („Porządki”). Małe dawki zamiast dużej akcji raz na kilka miesięcy.
3. **Rozwiązania tymczasowe mają termin:** komentarz `/* k1: tymczasowe — usunąć w <wersja/etap> */`
   i wpis w `docs/DO_ZROBIENIA.txt` (sekcja „Tymczasowe”); przy porządkach sprawdzamy terminy.
4. **Słownik:** nowe pojęcie = wiersz w `docs/SLOWNIK.md`; kod, dokumenty i komunikaty w systemie używają
   tych samych słów. Nie wprowadzaj synonimów.
5. **Nowe rzeczy jako wiersz w tabeli:** jak aplikacje (`K1_APLIKACJE`) — typy plików, strefy, kanały.
   Jeśli dodanie czegoś wymaga zmian w wielu miejscach, najpierw zrób z tego jedną tabelę.
6. **Dokumenty nie mogą zostać w tyle:** każda usługa i aplikacja ma opis (`docs/komponenty/`), każdy
   komunikat — wpis w dokumencie swojego kanału; od 0.7.5 sprawdza to test.
7. **Dziel przed rozbudową:** zanim dodasz dużą funkcję do pliku blisko limitu, najpierw podziel go
   (osobny commit), potem dodawaj.

## Bezpieczeństwo (bez wyjątków)
- Sekretu `K1_KLUCZ_PODPISU` nie wpisuj nigdy do repo ani do rozmowy i nie proś o niego — właściciel
  dodaje go sam w GitHub Actions. Prywatny klucz Secure Boot (`build/gen/sb/k1-sb.key`) jest kasowany po
  podpisaniu i nie trafia nigdzie. Klucza deweloperskiego nie proponujemy do wpisania w MOK.
- Niezmienniki z `SKILL.md` (klatki usług, kanały, brak ścieżek dla aplikacji, dm-verity, podpisy) zmienia
  się tylko przez ADR.

## Praca we dwóch (Claude i Codex)
- **Jedno zadanie — jeden asystent.** Zanim zaczniesz punkt z `docs/DO_ZROBIENIA.txt`, sprawdź otwarte
  PR-y i oznacz punkt `[w toku: Claude|Codex, gałąź …]` w tym samym pierwszym commicie; po scaleniu zmień na `[x]`. Nie ruszaj punktu oznaczonego przez drugiego.
- Własne gałęzie: Claude `claude/…`, Codex `codex/…`. Zawsze od świeżego `main`; przed PR-em zaktualizuj
  gałąź z `main` (merge, bez przepisywania historii cudzych gałęzi).
- Małe PR-y, jeden temat każdy. Pliki, które zmieniają prawie wszyscy (`VERSION`, `CHANGELOG.md`,
  `docs/DO_ZROBIENIA.txt`), zmienia ten, kto robi wydanie — inni dopisują swoje zmiany w opisie PR-a.
- Repo jest publiczne (od 2026-11-14, ADR 0040 — nigdy żadnych sekretów w repo): po zakończeniu pracy i zielonych
  testach scalaj PR do `main` sam (squash, z wyraźnym opisem commita). CI na `main` tylko buduje i tworzy wydanie, gdy `VERSION` ma nową
  wersję — bez testów (decyzja właściciela 2026-11-17: testy idą na PR-ze, przed scaleniem).

## Środowisko i testy
- Start pracy: `scripts/przygotuj-srodowisko.sh && scripts/zbuduj.sh` (bez KVM ok. 5 min budowy, z jądrem dłużej).
  Szybko: `make && make -j4 -O testy-host` (ok. 1,5 min).
- Przed scaleniem zmian w systemie (kod, skrypty, jądro) — decyzja właściciela 2026-11-14 (repo publiczne, CI bez
  limitu minut): **lokalnie** `scripts/test-m1.sh` (czytelność, testy na hoście, fuzzing, start, testy w systemie)
  i skrypty QEMU zmienianej części (`scripts/test-<część>.sh`, mapa w `docs/TESTY.md`); **pełny komplet** — CI na
  PR-ze (etykieta `testy`, z KVM ok. 15 min testów). Scalasz dopiero, gdy CI na PR-ze jest zielone. **Przeglądarki
  (Firefox w strefie) nie testuje CI** (decyzja właściciela 2026-11-17): sprawdza ją właściciel na sprzęcie i zgłasza
  błędy, a Claude odtwarza je na stanowisku (`scripts/stanowisko.sh`) i poprawia; testy części K1 pod nią (strefa, łącznik,
  dysk sesji, kompozytor) zostają w CI. Nie dodawaj z powrotem testu QEMU z Firefoksem bez decyzji właściciela. Pełny
  `scripts/testy.sh` w kontenerze (bez KVM ok. 40 min) — tylko gdy CI nie działa. Sama dokumentacja — bez testów.
- Nie edytuj skryptów ani kodu, gdy budowanie albo testy działają w tle.
- Zrzuty ekranu z testów: `build/testy/m4-*.png` (pulpit i aplikacje) i `build/testy/<część>-*.png`. Obraz do maszyny wirtualnej:
  `build/wynik/k1-<wersja>.iso` (CI: Releases).
