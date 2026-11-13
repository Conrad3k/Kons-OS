#!/bin/sh
# Pełna budowa K1: jądro + biblioteka C (musl) + programy + obrazy + ISO + obraz USB (build/wynik/). Kolejne
# uruchomienia są przyrostowe.
. "$(dirname "$0")/wspolne.sh"
"$K1_REPO/scripts/zbuduj-jadro.sh"
"$K1_REPO/scripts/zbuduj-musl.sh"
"$K1_REPO/scripts/zbuduj-obraz.sh"
"$K1_REPO/scripts/zbuduj-k1efi.sh"
"$K1_REPO/scripts/zbuduj-iso.sh"
"$K1_REPO/scripts/zbuduj-usb.sh"
