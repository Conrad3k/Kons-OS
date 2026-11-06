# Kons K1 Sovereign — notatki dla Claude

Własny system operacyjny na jądrze Linux: nasz initramfs, init i (od M2) grafika w C, statycznie z musl.
Budowany skryptami w `scripts/`, testowany w QEMU, CI w GitHub Actions (tylko `main`).

- **Przy każdej pracy w repo użyj skilla `kons-dev`** (`.claude/skills/kons-dev/SKILL.md`) — architektura K1
  i niezmienniki bezpieczeństwa, przepisy (nowa usługa, protokół, opcja jądra, testy), proces, scalanie, raport.
- Specyfikacja nadrzędna: `docs/SPEC.md` (zmiany tylko przez ADR w `docs/adr/`).
- Architektura, mapa repo, konwencje, wersje: `docs/ARCHITEKTURA.md`. Testy: `docs/TESTY.md`.
- Historia zmian: `CHANGELOG.md`; wersja: `VERSION` (SemVer od 0.1.0, tagi `k1-vX.Y.Z`).
- Stara dystrybucja Kons-OS (Debian): gałąź `archiwum/debian-0.1.2` — nie rozwijamy.
- Właściciel pisze po polsku i nie jest programistą. Dokumentacja, PR-y i komunikaty w systemie — po polsku;
  commity i komentarze w kodzie — po angielsku.
- Repo jest prywatne: po zakończeniu pracy scalaj PR do `main` bez pytania (squash).
- Kontener: brak KVM (QEMU w TCG), sieć tylko HTTPS przez proxy (apt Ubuntu, deb.debian.org, GitHub,
  cdn/www/git.kernel.org). Start pracy: `scripts/przygotuj-srodowisko.sh && scripts/zbuduj.sh && scripts/testy.sh`
  (~6 min). Przed scaleniem zmian w systemie: pełny `scripts/testy.sh` lokalnie.
- Wygląd (ustalenia właściciela, paleta, znak K1, tapety): `docs/WYGLAD.md`. Zrzuty ekranu z testów: `build/testy/m2-*.png`,
  `build/testy/m4-*.png`. Obraz do maszyny wirtualnej: `build/wynik/k1-<wersja>.iso` (CI: Releases).
