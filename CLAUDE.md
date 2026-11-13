# Kons K1 Sovereign — notatki dla Claude

Wspólne zasady dla Claude i Codexa są w `AGENTS.md` (dołączone niżej) — tam je zmieniaj, nie tutaj.

@AGENTS.md

## Tylko dla Claude
- **Przy każdej pracy w repo użyj skilla `kons-dev`** (`.claude/skills/kons-dev/SKILL.md`).
- Kontener Claude Code: brak KVM (QEMU w TCG), sieć tylko HTTPS przez proxy (apt Ubuntu, deb.debian.org,
  GitHub, cdn/www/git.kernel.org). GitHub przez narzędzia MCP; po scaleniu gałąź `claude/…` odtwarzana
  od `main` (force-push jest zablokowany: `checkout -B … origin/main` + `merge -s ours` starej gałęzi).
- Kontener stoi między wywołaniami narzędzi: budowa i testy w tle posuwają się tylko wtedy, gdy czekasz poleceniem
  (`timeout 570 sh -c "until grep -q 'KONIEC' log; do sleep 5; done"`, kilka razy pod rząd). W trakcie pełnych
  testów nie rób innych wywołań — raz dało to niepowtarzalny błąd w `test-m4` (audyt 0.8.6).
