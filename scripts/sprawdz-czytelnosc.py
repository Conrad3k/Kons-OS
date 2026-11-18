#!/usr/bin/env python3
"""Zasady czytelności K1 (AGENTS.md, decyzja właściciela z 2026-11-09), sprawdzane w testach:

  1. plik .c ma najwyżej 600 wierszy, funkcja najwyżej 80 (bez src/zewnetrzne/); od 0.9.0 pliki testów (testy/**/*.c)
     też najwyżej 600 wierszy (bez limitu funkcji);
     wyjątek: komentarz `/* k1: limit — <powód> */` w pierwszych 20 wierszach pliku (cały plik)
     albo w 5 wierszach nad funkcją (ta funkcja);
  2. każda usługa (K1_SYSTEM) ma opis docs/komponenty/<program>.md, każda aplikacja (K1_APLIKACJE)
     ma rozdział w docs/komponenty/aplikacje.md;
  3. każdy komunikat (K1_KOM_*) jest opisany w dokumencie swojego kanału w docs/komponenty/
     (nazwa w `…`, z przedrostkiem K1_KOM_ albo bez);
  4. każde rozwiązanie tymczasowe (`k1: tymczasowe — usunąć w …`) ma wpis w docs/DO_ZROBIENIA.txt;
  5. każdy ADR (docs/adr/NNNN-*.md) jest w spisie docs/adr/README.md (ADR 0035);
  6. każda ścieżka docs/… wymieniona w żywych dokumentach (AGENTS.md, CLAUDE.md, README.md, docs/*.md,
     docs/*.txt, docs/komponenty/, skill kons-dev) istnieje — bez docs/adr/, docs/archiwum/ i CHANGELOG.md
     (historia) i bez wzorów (`<wersja>`, `*`, `NNNN`);
  7. (0.10.2, ADR 0045) opis ekranu tylko dla testów: rolę `K1_ROLA_TEST` i pytanie `K1_KOM_OPIS_PYTANIE` zna tylko
     ekran (src/ekran/), wspólny protokół (src/wspolne/) i testy (testy/) — żaden inny program K1 nie może o nie pytać.

Użycie: python3 -I scripts/sprawdz-czytelnosc.py [KATALOG_REPO]. Kod wyjścia 0 = wszystko w porządku.
"""
import os
import re
import subprocess
import sys

PLIK_MAKS = 600
FUNKCJA_MAKS = 80
WYJATEK = re.compile(r"/\* k1: limit — \S")
TYMCZASOWE = re.compile(r"k1: tymczasowe — usunąć w ([^*()\n]+?)\s*(?:\(|\*/|$)")
# a top-level function definition: starts in column 0, has "(" before any "=", not a declaration
DEFINICJA = re.compile(r"^(?!typedef|extern|#|enum|struct\s+\w+\s*\{|union)[A-Za-z_][^=;]*\(")


def pliki_c(repo):
    for katalog, podkatalogi, pliki in os.walk(os.path.join(repo, "src")):
        podkatalogi[:] = sorted(p for p in podkatalogi if p != "zewnetrzne")
        for p in sorted(pliki):
            if p.endswith(".c"):
                yield os.path.join(katalog, p)


def pliki_tymczasowe(repo):
    """Where temporary solutions may be marked: C sources and headers, build and test scripts, tools."""
    for gora in ("src", "scripts", "narzedzia", "testy"):
        for katalog, podkatalogi, pliki in os.walk(os.path.join(repo, gora)):
            podkatalogi[:] = sorted(p for p in podkatalogi if p != "zewnetrzne")
            for p in sorted(pliki):
                if p.endswith((".c", ".h", ".sh", ".py")) and p != "sprawdz-czytelnosc.py":
                    yield os.path.join(katalog, p)


def funkcje(wiersze):
    """(nazwa, pierwszy wiersz od 0, liczba wierszy) każdej funkcji zdefiniowanej na najwyższym poziomie."""
    i = 0
    while i < len(wiersze):
        w = wiersze[i]
        if DEFINICJA.match(w) and not w.rstrip().endswith(";"):
            # the "{" ends this line or one of the next (a parameter list over several lines)
            j = i
            while j < len(wiersze) and j < i + 6 and not wiersze[j].rstrip().endswith("{") \
                    and not wiersze[j].rstrip().endswith(";"):
                j += 1
            if j < len(wiersze) and wiersze[j].rstrip().endswith("{") and "=" not in w.split("(")[0]:
                k = j
                while k < len(wiersze) and not wiersze[k].startswith("}"):
                    k += 1
                nazwa = re.findall(r"(\w+)\s*\(", w)
                yield (nazwa[0] if nazwa else "?"), i, k - i + 1
                i = k + 1
                continue
        i += 1


def pliki_testow(repo):
    """0.9.0: test sources (C) count for the file limit too — a test must be readable as well (not the function
    limit: a long list of checks reads well)."""
    for katalog, podkatalogi, pliki in os.walk(os.path.join(repo, "testy")):
        podkatalogi[:] = sorted(podkatalogi)
        for p in sorted(pliki):
            if p.endswith(".c"):
                yield os.path.join(katalog, p)


def sprawdz_limity(repo, bledy):
    for sciezka in pliki_testow(repo):
        wzgl = os.path.relpath(sciezka, repo)
        with open(sciezka, encoding="utf-8") as f:
            wiersze = f.read().rstrip("\n").split("\n")
        if len(wiersze) > PLIK_MAKS and not any(WYJATEK.search(w) for w in wiersze[:20]):
            bledy.append(f"{wzgl}: {len(wiersze)} wierszy (limit {PLIK_MAKS}; test)")
    for sciezka in pliki_c(repo):
        wzgl = os.path.relpath(sciezka, repo)
        with open(sciezka, encoding="utf-8") as f:
            wiersze = f.read().split("\n")
        if wiersze and wiersze[-1] == "":
            wiersze.pop()
        caly = any(WYJATEK.search(w) for w in wiersze[:20])
        if len(wiersze) > PLIK_MAKS and not caly:
            bledy.append(f"{wzgl}: {len(wiersze)} wierszy (limit {PLIK_MAKS})")
        for nazwa, start, dl in funkcje(wiersze):
            if dl > FUNKCJA_MAKS and not caly and \
                    not any(WYJATEK.search(w) for w in wiersze[max(0, start - 5):start + 1]):
                bledy.append(f"{wzgl}:{start + 1}: funkcja {nazwa} ma {dl} wierszy (limit {FUNKCJA_MAKS})")


def lista_x(tekst, makro):
    """Wiersze X(...) makra listy (k1_uslugi.h) jako listy pól (bez cudzysłowów). Pole może mieć nawiasy i przecinki
    w środku: napis "Wejście (klawiatura, mysz)" albo lista kanałów (KANAL_A, KANAL_B) — liczą się tylko przecinki
    na wierzchu wiersza."""
    m = re.search(r"#define " + makro + r"\(X\)(.*?)\n\n", tekst, re.S)
    cialo = re.sub(r"/\*.*?\*/", " ", m.group(1) if m else "", flags=re.S)
    wynik = []
    for start in re.finditer(r"\bX\(", cialo):
        pola, pole, glebokosc, napis, i = [], "", 1, False, start.end()
        while i < len(cialo) and glebokosc > 0:
            z = cialo[i]
            if z == '"':
                napis = not napis
            elif not napis and z == "(":
                glebokosc += 1
            elif not napis and z == ")":
                glebokosc -= 1
            if glebokosc == 1 and not napis and z == ",":
                pola.append(pole)
                pole = ""
            elif glebokosc > 0:
                pole += z
            i += 1
        pola.append(pole)
        wynik.append([p.replace("\\\n", " ").strip().strip('"') for p in pola])
    return wynik


def sprawdz_dokumenty(repo, bledy):
    kat = os.path.join(repo, "docs", "komponenty")
    dokumenty = ""
    for p in sorted(os.listdir(kat)):
        with open(os.path.join(kat, p), encoding="utf-8") as f:
            dokumenty += f.read() + "\n"
    with open(os.path.join(repo, "src/wspolne/k1_uslugi.h"), encoding="utf-8") as f:
        uslugi = f.read()
    system = lista_x(uslugi, "K1_SYSTEM")
    aplikacje = lista_x(uslugi, "K1_APLIKACJE")
    if not system or not aplikacje:
        bledy.append("k1_uslugi.h: nie odczytano list K1_SYSTEM / K1_APLIKACJE")
    for pola in system:
        if not os.path.exists(os.path.join(kat, pola[3] + ".md")):
            bledy.append(f"usługa {pola[3]}: brak opisu docs/komponenty/{pola[3]}.md")
    with open(os.path.join(kat, "aplikacje.md"), encoding="utf-8") as f:
        apl_md = f.read()
    for pola in aplikacje:
        if not re.search(r"^#+ .*\b" + re.escape(pola[6]) + r"\b", apl_md, re.M):
            bledy.append(f"aplikacja {pola[6]}: brak rozdziału w docs/komponenty/aplikacje.md")
    with open(os.path.join(repo, "src/wspolne/k1_komunikat.h"), encoding="utf-8") as f:
        komunikaty = re.findall(r"\bK1_KOM_(\w+) = \d+", f.read())
    if not komunikaty:
        bledy.append("k1_komunikat.h: nie odczytano typów K1_KOM_*")
    for k in komunikaty:
        if not re.search(r"`(K1_KOM_)?" + k + r"\b", dokumenty):
            bledy.append(f"komunikat K1_KOM_{k}: brak opisu w docs/komponenty/ (`{k}`)")


def sprawdz_tymczasowe(repo, bledy):
    with open(os.path.join(repo, "docs/DO_ZROBIENIA.txt"), encoding="utf-8") as f:
        plan = f.read()
    sekcja = plan.split("TYMCZASOWE", 1)[1] if "TYMCZASOWE" in plan else ""
    for sciezka in pliki_tymczasowe(repo):
        with open(sciezka, encoding="utf-8") as f:
            for nr, w in enumerate(f, 1):
                for termin in TYMCZASOWE.findall(w):
                    wzgl = os.path.relpath(sciezka, repo)
                    if wzgl not in sekcja:
                        bledy.append(f"{wzgl}:{nr}: tymczasowe (do {termin}) bez wpisu w DO_ZROBIENIA.txt (Tymczasowe)")


def sprawdz_opis_ekranu(repo, bledy):
    wolno = ("src/ekran/", "src/wspolne/", "testy/")
    for sciezka in list(pliki_c(repo)) + [os.path.join(d, f) for d, _, fs in os.walk(os.path.join(repo, "src")) for f in fs
                                     if f.endswith(".h")]:
        wzgl = os.path.relpath(sciezka, repo)
        if wzgl.startswith(wolno):
            continue
        with open(sciezka, encoding="utf-8") as f:
            tekst = f.read()
        for nazwa in ("K1_ROLA_TEST", "K1_KOM_OPIS_PYTANIE"):
            if nazwa in tekst:
                bledy.append(f"{wzgl}: {nazwa} poza ekranem i testami (opis ekranu tylko dla testów, ADR 0045)")


def sprawdz_adr(repo, bledy):
    kat = os.path.join(repo, "docs", "adr")
    with open(os.path.join(kat, "README.md"), encoding="utf-8") as f:
        spis = f.read()
    for p in sorted(os.listdir(kat)):
        if re.match(r"\d{4}-.*\.md$", p) and "(" + p + ")" not in spis:
            bledy.append(f"docs/adr/{p}: brak w spisie docs/adr/README.md")


# a path under docs/ that names a file (with an extension) or a directory (ends with "/")
SCIEZKA_DOCS = re.compile(r"(?<![\w/.-])docs/[\w./<>*{}…-]*?(?:\.(?:md|txt)\b|/(?![\w<*{]))")


def sciezki_docs(tekst):
    """Paths docs/… in a text, without patterns (<wersja>, *, NNNN)."""
    for m in SCIEZKA_DOCS.findall(tekst):
        if not re.search(r"[<>*{}…]|NNNN", m):
            yield m


def zywe_dokumenty(repo):
    for p in ("AGENTS.md", "CLAUDE.md", "README.md", ".claude/skills/kons-dev/SKILL.md"):
        yield p
    for kat in ("docs", "docs/komponenty"):
        for p in sorted(os.listdir(os.path.join(repo, kat))):
            if p.endswith((".md", ".txt")):
                yield kat + "/" + p


def sprawdz_sciezki(repo, bledy):
    for wzgl in zywe_dokumenty(repo):
        with open(os.path.join(repo, wzgl), encoding="utf-8") as f:
            tekst = f.read()
        for s in sorted(set(sciezki_docs(tekst))):
            if not os.path.exists(os.path.join(repo, s)):
                bledy.append(f"{wzgl}: ścieżka {s} nie istnieje")


def sprawdz_opisy_wersji(repo, bledy):
    """0.10.5 step 2 (ADR 0046, T18): every CHANGELOG entry from 0.10.5 has „Opis w K1:” (or the robot's mark)."""
    w = subprocess.run([sys.executable, "-I", os.path.join(repo, "scripts", "opisy-wersji.py"), "--sprawdz",
                        os.path.join(repo, "CHANGELOG.md")], capture_output=True, text=True)
    if w.returncode != 0:
        bledy.extend([l for l in w.stdout.splitlines() if l] or ["CHANGELOG.md: opisy wersji — " + w.stderr.strip()])


def samotest():
    """The checker itself: a too-long file and function, an exception, a test that catches nothing fails."""
    tekst = "/* top */\n" + "int a(void)\n{\n" + "    x();\n" * 90 + "}\n" + \
        "/* k1: limit — test */\nstatic int b(int c,\n          int d)\n{\n" + "    y();\n" * 90 + "}\n" + \
        "static const int t[] = {\n" + "    1,\n" * 90 + "};\n" + "int e(void) { return 0; }\n" * 400
    wiersze = tekst.split("\n")[:-1]
    f = {n: d for n, _, d in funkcje(wiersze)}
    assert f.get("a") == 93 and f.get("b") == 94 and "t" not in f, f
    assert len(wiersze) > PLIK_MAKS
    assert list(sciezki_docs("`docs/SPEC.md`, docs/komponenty/, docs/PLAN-<wersja>.txt, docs/CI, docs/adr/NNNN-*.md "
                             "(docs/archiwum/PLAN-0.8.1.txt).")) == ["docs/SPEC.md", "docs/komponenty/",
                                                                     "docs/archiwum/PLAN-0.8.1.txt"]
    assert lista_x("#define L(X) \\\n    X(A, 1, \"a\") \\\n    X(B, 2, \"b\")\n\n", "L") == [["A", "1", "a"], ["B", "2", "b"]]


def main():
    repo = sys.argv[1] if len(sys.argv) > 1 else os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    samotest()
    bledy = []
    sprawdz_limity(repo, bledy)
    sprawdz_dokumenty(repo, bledy)
    sprawdz_tymczasowe(repo, bledy)
    sprawdz_adr(repo, bledy)
    sprawdz_sciezki(repo, bledy)
    sprawdz_opis_ekranu(repo, bledy)
    sprawdz_opisy_wersji(repo, bledy)
    for b in bledy:
        print("  " + b)
    if bledy:
        print(f"[k1] BŁĄD: zasady czytelności — {len(bledy)} do poprawy (AGENTS.md, „Czytelność”)")
        return 1
    print("[k1] czytelność: pliki ≤ 600 wierszy, funkcje ≤ 80, opisy usług, aplikacji i komunikatów, "
          "spis ADR, ścieżki docs/ — OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
