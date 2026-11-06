# Kons K1 Sovereign — notatki dla Claude

Własny system operacyjny na jądrze Linux: nasz initramfs, init i (od M2) grafika w C, statycznie z musl.
Budowany skryptami w `scripts/`, testowany w QEMU, CI w GitHub Actions (tylko `main`).

- **Przy każdej pracy w repo użyj skilla `kons-dev`** (`.claude/skills/kons-dev/SKILL.md`) — proces,
  etapy (milestones), testy, scalanie, raport dla właściciela.
- Specyfikacja nadrzędna: `docs/SPEC.md` (zmiany tylko przez ADR w `docs/adr/`).
- Architektura, mapa repo, konwencje, wersje: `docs/ARCHITEKTURA.md`. Testy: `docs/TESTY.md`.
- Historia zmian: `CHANGELOG.md`; wersja: `VERSION` (SemVer od 0.1.0, tagi `k1-vX.Y.Z`).
- Stara dystrybucja Kons-OS (Debian): gałąź `archiwum/debian-0.1.2` — nie rozwijamy.
- Właściciel pisze po polsku i nie jest programistą. Dokumentacja, PR-y i komunikaty w systemie — po polsku;
  commity i komentarze w kodzie — po angielsku.
- Repo jest prywatne: po zakończeniu pracy scalaj PR do `main` bez pytania (squash).
- Kontener: brak KVM (QEMU w TCG), sieć tylko HTTPS przez proxy (apt Ubuntu, deb.debian.org, GitHub,
  cdn/www/git.kernel.org). Start pracy: `scripts/przygotuj-srodowisko.sh && scripts/zbuduj.sh && scripts/test-m1.sh`
  (~5 min). Przed scaleniem zmian w systemie: pełny `scripts/test-m1.sh` lokalnie (i testy bieżącego etapu).
