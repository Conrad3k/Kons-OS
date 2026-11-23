# K1 Glina — motyw Claude Code w stylu Kons K1

Wygląd aplikacji Claude (komputer i telefon) według `docs/WYGLAD.md`. To nie jest część systemu K1: nie trafia do obrazu,
nie buduje go `make` i nie testuje CI.

- **Kolory motywów okien K1:** kremowy papier za dnia (`#FAF9F6`, tekst `#1F1E1C`), ciepła noc wieczorem (`#262523`,
  tekst `#EEEBE5`), akcent terakota (`#BC5A37` / `#D97757`).
- **Kolory znaczeń z K1:** szałwia (sukces), piasek (ostrzeżenie), wrzos, czerwień gliny — także dla zmian w git i kodu.
- **Tło:** tapeta „Glina” — warstwy papieru w terakocie schodzące w prawy dolny róg; chmury płyną bardzo wolno.
- **Wiadomości jak okna K1:** zaokrąglenie 12, włoskowaty obrys, miękki cień; nagłówki szeryfowe (jak Plex Serif).

Terminal i edytory motywów nie rysują.

## Jak włączyć

**Sesje na repo Kons-OS** — same: `.claude/settings.json` dodaje sklepik `kons-os` (`.claude-plugin/marketplace.json`)
i włącza wtyczkę `k1-glina`. Claude może raz zapytać, czy ufać sklepikowi z repo.

**Każda inna sesja** — raz, w aplikacji na komputerze: **+** obok pola wiadomości → **Plugins** → **Add marketplace**
→ `Conrad3k/Kons-OS` → **Add plugin** przy `k1-glina`. W terminalu Claude Code:

```
/plugin install k1-glina --marketplace Conrad3k/Kons-OS
```

albo z powłoki:

```
claude plugin install k1-glina --marketplace Conrad3k/Kons-OS
```

## Pliki

| Plik | Co robi |
|---|---|
| `.claude-plugin/plugin.json` | nazwa i opis wtyczki |
| `hooks/hooks.json` | wskazuje moduł `register.tsx` |
| `hooks/register.tsx` | przy starcie sesji ustawia motyw; rysuje tło |
| `hooks/palette.ts` | kolory K1 → paleta aplikacji (`makeColors`) i kolory wiadomości |
| `hooks/scene.ts` | tapeta „Glina” jako SVG (jasna i ciemna) |
| `hooks/make-colors.js`, `.d.ts` | pomocnik z Claude Code — nie edytować; przy nowej wersji Claude Code skopiować na nowo |

Zmiana kolorów: `hooks/palette.ts`, potem `claude plugin validate narzedzia/claude-motyw`. Kolory K1 zmienia się najpierw
w `docs/WYGLAD.md`, motyw idzie za nimi.
