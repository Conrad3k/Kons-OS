# Kons-OS — notatki dla Claude

Dystrybucja Linuksa na bazie Debiana 13 (trixie), budowana przez `live-build` w GitHub Actions.

- **Przy każdej pracy w repo użyj skilla `kons-dev`** (`.claude/skills/kons-dev/SKILL.md`) — proces zmian,
  wersjonowanie, scalanie, pilnowanie buildu, feedback.
- Architektura, mapa repo, konwencje, zasady wersji: `docs/ARCHITEKTURA.md`.
- Historia zmian: `CHANGELOG.md`; wersja: `VERSION`.
- Właściciel pisze po polsku i nie jest programistą. Dokumentacja, PR-y i komunikaty w systemie — po polsku.
- Repo jest prywatne: po zakończeniu pracy scalaj PR do `main` bez pytania (squash).
- Ten kontener nie ma dostępu do deb.debian.org — ISO buduje się tylko w GitHub Actions (tylko na `main`).
