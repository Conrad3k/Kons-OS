# Kons K1 Sovereign — notatki dla Claude

Wspólne zasady dla Claude i Codexa są w `AGENTS.md` (dołączone niżej) — tam je zmieniaj, nie tutaj.

@AGENTS.md

## Tylko dla Claude
- **Przy każdej pracy w repo użyj skilla `kons-dev`** (`.claude/skills/kons-dev/SKILL.md`).
- Kontener Claude Code: brak KVM (QEMU w TCG), sieć tylko HTTPS przez proxy (apt Ubuntu, deb.debian.org,
  GitHub, cdn/www/git.kernel.org). GitHub przez narzędzia MCP; po scaleniu gałąź `claude/…` odtwarzana
  od `main` (force-push jest zablokowany: `checkout -B … origin/main` + `merge -s ours` starej gałęzi).
