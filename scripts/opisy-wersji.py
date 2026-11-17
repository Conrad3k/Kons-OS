#!/usr/bin/env python3
# Opisy wersji do ogłoszenia wydania (0.10.5 krok 2, ADR 0046, decyzja D13, zasady T18) — z CHANGELOG.md:
#
#   python3 -I scripts/opisy-wersji.py CHANGELOG.md WERSJA            → wiersze „wersja<TAB>opis<TAB>tekst” albo
#                                                                       „wersja<TAB>poprawki” (od najnowszej, do 8)
#   python3 -I scripts/opisy-wersji.py --nowa-linia CHANGELOG.md WERSJA → kod 0, gdy wpis WERSJI ma znacznik nowej
#                                                                       linii jądra
#   python3 -I scripts/opisy-wersji.py --sprawdz CHANGELOG.md          → kod 0, gdy każdy wpis od 0.10.5 ma opis
#
# Wpis wersji od ludzi ma wiersz „Opis w K1: …” (jeden akapit prostym językiem, bez numerów kroków i ADR; może iść
# w kilku wierszach — do pustego wiersza). Wpis robota (krok 3) zamiast niego ma znacznik `<!-- k1: poprawki -->` —
# K1 pokazuje wtedy swoje stałe zdanie. Znacznik `<!-- k1: nowa linia jądra -->` dodaje dopisek w pytaniu o instalację.
# Tekst sprawdza jeszcze narzędzie podpisu (narzedzia/wydanie.c) tą samą funkcją co K1.
import re, sys

OD = (0, 10, 5)  # wcześniejsze wpisy nie mają opisu w K1
MAKS = 8


def wersja(t):
    return tuple(int(x) for x in t.split("."))


def wpisy(sciezka):
    """[(wersja, tekst wpisu)] od najnowszego."""
    tekst = open(sciezka, encoding="utf-8").read()
    czesci = re.split(r"^## \[(\d+\.\d+\.\d+)\][^\n]*\n", tekst, flags=re.M)
    return [(wersja(czesci[i]), czesci[i + 1]) for i in range(1, len(czesci) - 1, 2)]


def opis(tresc):
    """('opis', tekst) | ('poprawki', None) | None."""
    if "<!-- k1: poprawki -->" in tresc:
        return ("poprawki", None)
    m = re.search(r"^Opis w K1:[ \t]*(.*?)(?:\n[ \t]*\n|\Z)", tresc, flags=re.M | re.S)
    if m is None:
        return None
    return ("opis", " ".join(m.group(1).split()))


def main(a):
    if len(a) == 2 and a[0] == "--sprawdz":
        zle = [".".join(map(str, w)) for w, t in wpisy(a[1]) if w >= OD and opis(t) is None]
        for w in zle:
            print(f"CHANGELOG.md: wpis {w} bez „Opis w K1:” (ani znacznika robota)")
        return 1 if zle else 0
    if len(a) == 3 and a[0] == "--nowa-linia":
        for w, t in wpisy(a[1]):
            if w == wersja(a[2]):
                return 0 if "<!-- k1: nowa linia jądra -->" in t else 1
        return 1
    if len(a) != 2:
        print(__doc__ or "użycie: opisy-wersji.py CHANGELOG WERSJA", file=sys.stderr)
        return 2
    n = 0
    for w, t in wpisy(a[0]):
        if w > wersja(a[1]):
            continue
        if w < OD or n == MAKS:
            break
        o = opis(t)
        if o is None:
            print(f"opisy-wersji: wpis {'.'.join(map(str, w))} bez „Opis w K1:”", file=sys.stderr)
            return 1
        print(".".join(map(str, w)) + ("\tpoprawki" if o[0] == "poprawki" else "\topis\t" + o[1]))
        n += 1
    return 0


sys.exit(main(sys.argv[1:]))
