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
     ekran (src/ekran/), wspólny protokół (src/wspolne/) i testy (testy/) — żaden inny program K1 nie może o nie pytać;
  8. (0.10.5 krok 8) każda ścieżka do pliku repo (scripts/…, src/…, testy/…, jadro/…, narzedzia/…, musl/…, strefy/…,
     .github/…) w żywych dokumentach istnieje — także bez rozszerzenia (`src/siec/wifi` → wifi.c); bez planów
     (docs/PLAN-*.txt nazywają pliki, które dopiero powstaną) i bez wierszy o historii („historia git”, „usunięt…”);
  9. (0.10.5 krok 8) każdy plik w scripts/ ma opis w docs/ARCHITEKTURA.md albo docs/TESTY.md, a każdy test na hoście
     i fuzzer z Makefile (TESTY_HOST, TESTY_HOST_PLIKI, FUZZ) — w docs/TESTY.md;
 10. (0.10.5 krok 8) stan nie zostaje w tyle: najnowsza wersja od ludzi w CHANGELOG.md (wpis bez znacznika robota)
     jest w wierszu „Stan:” docs/DO_ZROBIENIA.txt i w „Co już jest (…)” README.md;
 11. (0.10.7 krok 1, zasada Z3 właściciela) w napisach okien K1 nie ma półpauzy (—): literały w src/aplikacje/,
     src/gui/, src/przybornik/, src/logowanie/, src/ekran/ i w tekstach Instalatora (src/wspolne/k1_inst_teksty.h);
     bez komentarzy i bez napisów do logu (instrukcja z wywołaniem *log*( ); napis budowany dla logu poza nią —
     komentarz `/* k1: napis do logu */` w jego wierszu).

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


POLPAUZA_GDZIE = ("src/aplikacje", "src/gui", "src/przybornik", "src/logowanie", "src/ekran",
                  "src/wspolne/k1_inst_teksty.h")
DO_LOGU = re.compile(r"\b\w*log\w*\s*\(")
NAPIS_DO_LOGU = "/* k1: napis do logu */"


def literaly(tekst):
    """C string literals of a source: (offset, its text, the code before it with comments removed)."""
    kod, i, n = [], 0, len(tekst)
    while i < n:
        if tekst.startswith("/*", i):
            j = tekst.find("*/", i + 2)
            j = n if j < 0 else j + 2
            kod.append(" " * (j - i))
            i = j
        elif tekst.startswith("//", i):
            j = tekst.find("\n", i)
            j = n if j < 0 else j
            kod.append(" " * (j - i))
            i = j
        elif tekst[i] in "\"'":
            j = i + 1
            while j < n and tekst[j] != tekst[i] and tekst[j] != "\n":
                j += 2 if tekst[j] == "\\" else 1
            if tekst[i] == '"':
                yield i, tekst[i + 1:j], "".join(kod)
            kod.append(tekst[i:j + 1])
            i = j + 1
        else:
            kod.append(tekst[i])
            i += 1


def polpauzy(tekst):
    """Line numbers of literals with an em dash that are not for the log."""
    for poz, napis, przed in literaly(tekst):
        if "—" not in napis:
            continue
        instrukcja = przed[max(przed.rfind(";"), przed.rfind("{"), przed.rfind("}")) + 1:]
        wiersz = tekst[tekst.rfind("\n", 0, poz) + 1:tekst.find("\n", poz) if tekst.find("\n", poz) >= 0 else None]
        if not DO_LOGU.search(instrukcja) and NAPIS_DO_LOGU not in wiersz:
            yield tekst.count("\n", 0, poz) + 1


def sprawdz_polpauzy(repo, bledy):
    for gdzie in POLPAUZA_GDZIE:
        sciezka = os.path.join(repo, gdzie)
        pliki = [sciezka] if os.path.isfile(sciezka) else \
            sorted(os.path.join(d, f) for d, _, fs in os.walk(sciezka) for f in fs if f.endswith((".c", ".h")))
        for p in pliki:
            with open(p, encoding="utf-8") as f:
                for nr in polpauzy(f.read()):
                    bledy.append(f"{os.path.relpath(p, repo)}:{nr}: półpauza w napisie na ekranie (zasada Z3: zdanie "
                                 "podziel albo użyj przecinka, dwukropka)")


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


# a path to a file or directory of the repository: scripts/…, src/…; not a pattern (src/wspolne/k1_kom_<kanał>.c)
SCIEZKA_REPO = re.compile(r"(?<![\w/.~-])((?:scripts|src|testy|jadro|narzedzia|musl|strefy|\.github)/[\w./+-]*[\w/])"
                          r"(?![\w<*{…/-])")
HISTORIA = re.compile(r"historia git|usunięt")


def sciezki_repo(tekst):
    """Paths to repository files in a text, without lines about history (a removed file may be named there)."""
    for wiersz in tekst.split("\n"):
        if not HISTORIA.search(wiersz):
            yield from SCIEZKA_REPO.findall(wiersz)


def istnieje(repo, s):
    """A path names a file or directory, or a stem of one (`src/siec/wifi` for wifi.c and wifi.h)."""
    pelna = os.path.join(repo, s)
    if os.path.exists(pelna):
        return True
    katalog, nazwa = os.path.split(pelna)
    return os.path.isdir(katalog) and any(p.startswith(nazwa + ".") for p in os.listdir(katalog))


def sprawdz_sciezki_repo(repo, bledy):
    for wzgl in zywe_dokumenty(repo):
        if re.match(r"docs/PLAN-", wzgl):
            continue
        with open(os.path.join(repo, wzgl), encoding="utf-8") as f:
            tekst = f.read()
        for s in sorted(set(sciezki_repo(tekst))):
            if not istnieje(repo, s):
                bledy.append(f"{wzgl}: ścieżka {s} nie istnieje")


def lista_make(makefile, nazwa):
    """Words of a Makefile list `NAZWA := a b \\ c` (with continued lines)."""
    m = re.search(r"^" + nazwa + r" :=((?:[^\n]*\\\n)*[^\n]*)", makefile, re.M)
    return m.group(1).replace("\\", " ").split() if m else []


def sprawdz_spisy(repo, bledy):
    def czytaj(wzgl):
        with open(os.path.join(repo, wzgl), encoding="utf-8") as f:
            return f.read()
    architektura, testy, makefile = czytaj("docs/ARCHITEKTURA.md"), czytaj("docs/TESTY.md"), czytaj("Makefile")
    for p in sorted(os.listdir(os.path.join(repo, "scripts"))):
        if not os.path.isfile(os.path.join(repo, "scripts", p)):
            continue  # __pycache__ of a test run (ignored by git)
        if not re.search(r"(?<![\w-])" + re.escape(p) + r"(?![\w-])", architektura + testy):
            bledy.append(f"scripts/{p}: brak opisu w docs/ARCHITEKTURA.md (Skrypty) ani w docs/TESTY.md")
    testy_host = lista_make(makefile, "TESTY_HOST") + lista_make(makefile, "TESTY_HOST_PLIKI")
    fuzz = ["fuzz_" + n for n in lista_make(makefile, "FUZZ")]
    if not testy_host or len(fuzz) < 2:
        bledy.append("Makefile: nie odczytano list TESTY_HOST / FUZZ")
    for t in testy_host + fuzz:
        if not re.search(r"\b" + t + r"\b", testy):
            bledy.append(f"{t} (Makefile): brak w docs/TESTY.md")


def sprawdz_stan(repo, bledy):
    """The newest human version (a CHANGELOG entry without the robot's mark) is where the state is said."""
    with open(os.path.join(repo, "CHANGELOG.md"), encoding="utf-8") as f:
        czesci = re.split(r"^## \[(\d+\.\d+\.\d+)\][^\n]*\n", f.read(), flags=re.M)
    ludzie = [czesci[i] for i in range(1, len(czesci) - 1, 2) if "<!-- k1: poprawki -->" not in czesci[i + 1]]
    if not ludzie:
        bledy.append("CHANGELOG.md: nie odczytano wersji")
        return
    w = re.escape(ludzie[0])
    with open(os.path.join(repo, "docs/DO_ZROBIENIA.txt"), encoding="utf-8") as f:
        if not re.search(r"^Stan: " + w + r"\b", f.read(), re.M):
            bledy.append(f"docs/DO_ZROBIENIA.txt: wiersz „Stan:” nie zaczyna się od {ludzie[0]} (CHANGELOG.md)")
    with open(os.path.join(repo, "README.md"), encoding="utf-8") as f:
        if not re.search(r"Co już jest \(" + w + r"\)", f.read()):
            bledy.append(f"README.md: „Co już jest ({ludzie[0]})” — nie ta wersja co w CHANGELOG.md")


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
    assert list(sciezki_repo("`scripts/test-m4.sh`, src/siec/wifi, src/wspolne/k1_kom_<kanał>.c, testy/host/fuzz_*.c\n"
                             "dawny `scripts/test-m2.sh` (historia git)")) == ["scripts/test-m4.sh", "src/siec/wifi"]
    c = ('/* "a — b" */ k1_log(TAG, "x — %d",\n    1);\nk1_tekst(o, "Nie możesz — tak", \'"\');\n'
         'snprintf(b, n, "%s — %s", x); /* k1: napis do logu */\n// "c — d"\nt = "e\\" — f";\n')
    assert list(polpauzy(c)) == [3, 6], list(polpauzy(c))
    assert lista_make("A := x y \\\n\tz\nB := q\n", "A") == ["x", "y", "z"]
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
    sprawdz_sciezki_repo(repo, bledy)
    sprawdz_spisy(repo, bledy)
    sprawdz_stan(repo, bledy)
    sprawdz_polpauzy(repo, bledy)
    for b in bledy:
        print("  " + b)
    if bledy:
        print(f"[k1] BŁĄD: zasady czytelności — {len(bledy)} do poprawy (AGENTS.md, „Czytelność”)")
        return 1
    print("[k1] czytelność: pliki ≤ 600 wierszy, funkcje ≤ 80, opisy usług, aplikacji i komunikatów, "
          "spis ADR, ścieżki, spisy skryptów i testów, stan wersji, napisy bez półpauz — OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
