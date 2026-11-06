#!/bin/sh
# Pełna budowa K1: jądro + programy + obrazy + ISO (build/wynik/). Kolejne uruchomienia są przyrostowe.
. "$(dirname "$0")/wspolne.sh"
"$K1_REPO/scripts/zbuduj-jadro.sh"
"$K1_REPO/scripts/zbuduj-obraz.sh"
"$K1_REPO/scripts/zbuduj-iso.sh"
