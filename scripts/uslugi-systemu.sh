#!/bin/sh
# Usługi systemu K1: nazwy programów z K1_SYSTEM (src/wspolne/k1_uslugi.h — jedyna lista), po jednej w wierszu.
# Używają: Makefile (programy do zbudowania), scripts/zbuduj-obraz.sh (pliki w /rdzen/system).
exec sed -n '/^#define K1_SYSTEM(X)/,/^$/s/.*X([A-Z_]*, *[0-9]*, *[0-9]*, *"\([a-z_]*\)".*/\1/p' \
    "$(dirname "$0")/../src/wspolne/k1_uslugi.h"
