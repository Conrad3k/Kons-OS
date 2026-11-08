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
- Historia zmian: `CHANGELOG.md`; wersja: `VERSION` (SemVer, tagi `k1-vX.Y.Z`).
- Stara dystrybucja Kons-OS (Debian): gałąź `archiwum/debian-0.1.2` — nie rozwijamy.
- **Teraz:** przed M7 najpierw `docs/PRZED-M7.md` (porządki po 0.7.1, wersja 0.7.2).

## Właściciel
- Pisze po polsku i nie jest programistą. Dokumentacja, PR-y, komunikaty w systemie i raporty — po
  polsku, prostym językiem; commity i komentarze w kodzie — po angielsku.
- Po każdej nowej wersji podaj bezpośredni link do ISO i obrazu USB z wydania:
  `https://github.com/Conrad3k/Kons-OS/releases/download/k1-v<wersja>/k1-<wersja>.iso` oraz
  `…/k1-<wersja>-usb.img.xz` — dopiero gdy wydanie naprawdę istnieje.
- Testuje na ThinkPadzie X230 (UEFI, Secure Boot na razie wyłączony).

## Bezpieczeństwo (bez wyjątków)
- Sekretu `K1_KLUCZ_PODPISU` nie wpisuj nigdy do repo ani do rozmowy i nie proś o niego — właściciel
  dodaje go sam w GitHub Actions. Prywatny klucz Secure Boot (`build/gen/sb/k1-sb.key`) jest kasowany po
  podpisaniu i nie trafia nigdzie. Klucza deweloperskiego nie proponujemy do wpisania w MOK.
- Niezmienniki z `SKILL.md` (klatki usług, kanały, brak ścieżek dla aplikacji, dm-verity, podpisy) zmienia
  się tylko przez ADR.

## Praca we dwóch (Claude i Codex)
- **Jedno zadanie — jeden asystent.** Zanim zaczniesz punkt z `docs/PRZED-M7.md` (albo z
  `docs/DO_ZROBIENIA.txt`), sprawdź otwarte PR-y i oznacz punkt `[w toku: Claude|Codex, gałąź …]` w tym
  samym pierwszym commicie; po scaleniu zmień na `[x]`. Nie ruszaj punktu oznaczonego przez drugiego.
- Własne gałęzie: Claude `claude/…`, Codex `codex/…`. Zawsze od świeżego `main`; przed PR-em zaktualizuj
  gałąź z `main` (merge, bez przepisywania historii cudzych gałęzi).
- Małe PR-y, jeden temat każdy. Pliki, które zmieniają prawie wszyscy (`VERSION`, `CHANGELOG.md`,
  `docs/DO_ZROBIENIA.txt`), zmienia ten, kto robi wydanie — inni dopisują swoje zmiany w opisie PR-a.
- Repo jest prywatne: po zakończeniu pracy i zielonych testach scalaj PR do `main` sam (squash, z
  wyraźnym opisem commita). CI na `main` tworzy wydanie, gdy `VERSION` ma nową wersję.

## Środowisko i testy
- Start pracy: `scripts/przygotuj-srodowisko.sh && scripts/zbuduj.sh && scripts/testy.sh` (bez KVM ok.
  6–15 min). Szybko: `make && make testy-host`.
- Przed scaleniem zmian w systemie (kod, skrypty, jądro): pełny `scripts/testy.sh` lokalnie, wszystko
  `[OK]`. Sama dokumentacja — bez testów.
- Nie edytuj skryptów ani kodu, gdy budowanie albo testy działają w tle.
- Zrzuty ekranu z testów: `build/testy/m2-*.png`, `build/testy/m4-*.png`. Obraz do maszyny wirtualnej:
  `build/wynik/k1-<wersja>.iso` (CI: Releases).
