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
# 0.13.0 step 4 (ADR 0059): the same images with k1-test.efi (the log on the serial port) for the tests in QEMU
"$K1_REPO/scripts/zbuduj-iso.sh" --test
"$K1_REPO/scripts/zbuduj-usb.sh" --test
