# 0017. Hasło i ekran logowania (M5e)

- Status: Przyjęty; od 0.13.0 hasło ustawia się i zmienia tylko na ekranie zabezpieczeń (Ctrl+Alt+Del), nie w Ustawieniach
  — ADR 0061
- Data: 2026-11-07
- Dotyczy: ADR 0015 (klucz owinięty Argon2id), ADR 0016, ADR 0008 (role w ekranie), SPEC §7, etap M5e

## Kontekst
Decyzje właściciela: jedno konto; hasło opcjonalne, włączane w Ustawieniach; zapomniane hasło = nowe
konto; ekran hasła „Claude'owy” — wybrany szkic B „Zegar” (`testy/host/szkice_logowania.c`).
Magazyn (ADR 0015) już umie owinąć klucz hasłem; brakowało: kto przyjmuje hasło przy starcie, kto je
zmienia i jak system ma wystartować z zablokowanym magazynem.

## Decyzja
1. **Osobny program `logowanie`** (UID 104, klatka bez ścieżek) — jedyny, który przy starcie
   przyjmuje hasło. Nadzorca uruchamia go **zamiast** `gui`, gdy skarbiec zgłosi stan „zablokowany”
   (`SKARB_STAN` na kanale sterującym, nadawca UID 103 sprawdzany). Bez słowa od skarbca: pulpit
   najpóźniej 20 s po starcie.
2. **Nowa rola w ekranie `K1_ROLA_LOGOWANIE`:** panele jak powłoka, a dopóki jest połączona, dostaje
   **każdy klawisz** (także Super). Pulpitu w tym czasie nie ma.
3. Hasło idzie **tylko do skarbca** (zapieczętowany memfd, `SKARB_ODBLOKUJ`, tylko od właściciela
   „logowanie”). **Skarbiec sam ogranicza próby**: od trzeciej złej — 5 s, potem 10, 20 … do 5 min
   (nie ufa klientowi). Po poprawnym haśle zgłasza nadzorcy „odblokowany” → nadzorca zatrzymuje
   ekran logowania i restartuje ekran (ustawienia są już czytelne), potem pulpit.
4. **Ustawienia → Hasło** (`SKARB_HASLO`, tylko od Ustawień): włącz (nowe + powtórz), zmień (obecne +
   nowe + powtórz), wyłącz (obecne). Skarbiec sprawdza obecne hasło (Argon2id) z tym samym limitem
   prób; nowe ≥ 4 znaki. Zmiana zapisuje nagłówek w obu kopiach (stare hasło przestaje działać).
5. Hasło nigdy nie trafia do logu; bufory zerowane po wysłaniu.

## Konsekwencje
- Bez hasła — jak w 0.5.6 (szyfrowanie bez ochrony przed kradzieżą nośnika, napis w Ustawieniach).
- Klawiatura w drodze do ekranu hasła przechodzi przez `wejscie` i `ekran` (jak każdy klawisz) —
  baza zaufania obejmuje te usługi (RYZYKA R40).
- Argon2id: pamięć ≈ RAM/16 (X230: 256 MiB) — odblokowanie trwa ok. 1 s na X230, dłużej w QEMU.
- Zablokowany magazyn: ekran startuje z ustawieniami domyślnymi (tapeta ekranu hasła — „Zmierzch”,
  jak w szkicu), Notatek i aplikacji nie da się uruchomić.

## Rozważane alternatywy
- **Ekran hasła w `gui`** — powłoka ma więcej kodu i uprawnień (uruchamianie aplikacji); osobny,
  mały program to mniejsza powierzchnia dla hasła.
- **Okno aplikacji zamiast panelu** — okna mają ramkę, nie zasłaniają paska i nie dostają klawiatury
  bez fokusu.
- **Limit prób w programie logowania** — przejęty program mógłby go pominąć; limit jest w skarbcu.
