#!/bin/sh
# Buduje programy K1 i składa z nich obrazy startowe:
#   build/wynik/initramfs.cpio     — /init (k1-initramfs) + /dev/console, bez kompresji
#   build/wynik/rdzen.squashfs     — obraz /rdzen: init, system/{nadzorca,wejscie,ekran,gui,skarbiec,logowanie,instalator,czytnik}, katalogi jadro/
#   build/wynik/rdzen-test.squashfs — to samo + program testowy jako /rdzen/system/test-systemu
# Wymaga zbudowanego jądra (scripts/zbuduj-jadro.sh: nagłówki i gen_init_cpio).
. "$(dirname "$0")/wspolne.sh"

GEN_CPIO=$K1_JADRO_OBJ/usr/gen_init_cpio
[ -x "$GEN_CPIO" ] && [ -d "$K1_NAGLOWKI/include" ] || blad "najpierw zbuduj jądro: scripts/zbuduj-jadro.sh"

info "programy K1 (musl, statycznie)"
make -s -C "$K1_REPO" BUDOWA="$K1_BUDOWA" NAGLOWKI="$K1_NAGLOWKI/include"
"$K1_REPO/scripts/sprawdz-binaria.sh" "$K1_BUDOWA"/bin/*

info "initramfs.cpio"
cat > "$K1_BUDOWA/initramfs.lista" <<LISTA
dir /dev 0755 0 0
nod /dev/console 0600 0 0 c 5 1
dir /proc 0755 0 0
dir /nowy_korzen 0755 0 0
file /init $K1_BUDOWA/bin/k1-initramfs 0500 0 0
LISTA
"$GEN_CPIO" -t "$SOURCE_DATE_EPOCH" "$K1_BUDOWA/initramfs.lista" > "$K1_WYNIK/initramfs.cpio"

# Drzewo /rdzen: tylko to, co potrzebne. Uprawnienia ustawiane jawnie, właściciel root (-all-root).
przygotuj_rdzen() { # $1 = katalog docelowy
    rm -rf "$1"; mkdir -p "$1/jadro/dev" "$1/jadro/proc" "$1/jadro/sys" "$1/system" "$1/aplikacje"
    install -m 0555 "$K1_BUDOWA/bin/init" "$1/init"
    for p in nadzorca wejscie ekran gui skarbiec logowanie instalator czytnik; do install -m 0555 "$K1_BUDOWA/bin/$p" "$1/system/$p"; done
    # Built-in native applications (M4); signed application images come with M10 (/aplikacje).
    for d in "$K1_REPO"/src/aplikacje/*/; do p=$(basename "$d"); install -m 0555 "$K1_BUDOWA/bin/$p" "$1/aplikacje/$p"; done
    chmod 0755 "$1" "$1/jadro" "$1/jadro/dev" "$1/jadro/proc" "$1/jadro/sys" "$1/system" "$1/aplikacje"
}
# The build id (0.7.0, ADR 0022): 128 bits of SHA-256 over the names and contents of every file of the core.
# The slot manifest carries the same id: nadzorca confirms a new slot only when it is the very build it runs.
wydanie() { # $1 = katalog rdzenia
    (cd "$1" && find . -type f ! -path ./system/wydanie | LC_ALL=C sort | while IFS= read -r f; do
        printf '%s\n' "$f"; cat "$f"; done) | sha256sum | cut -c1-32 > "$1/system/wydanie"
    chmod 0444 "$1/system/wydanie"
}
squashfs() { # $1 = katalog, $2 = plik wynikowy
    wydanie "$1"
    rm -f "$2"
    mksquashfs "$1" "$2" -quiet -no-progress -noappend -comp zstd -all-root -no-xattrs >/dev/null
    verity "$2" # M6b: hash tree appended, root hash in $2.skrot
}

info "rdzen.squashfs"
przygotuj_rdzen "$K1_BUDOWA/rdzen"
squashfs "$K1_BUDOWA/rdzen" "$K1_WYNIK/rdzen.squashfs"
cp "$K1_BUDOWA/rdzen/system/wydanie" "$K1_WYNIK/wydanie"

info "rdzen-test.squashfs (z programem testowym)"
przygotuj_rdzen "$K1_BUDOWA/rdzen-test"
install -m 0555 "$K1_BUDOWA/bin/k1-test-systemu" "$K1_BUDOWA/rdzen-test/system/test-systemu"
# Plik ze „#!” — test sprawdza, że jądro NIE uruchamia skryptów (brak CONFIG_BINFMT_SCRIPT).
printf '#!/rdzen/system/test-systemu --echo\n' > "$K1_BUDOWA/rdzen-test/system/skrypt-testowy"
chmod 0555 "$K1_BUDOWA/rdzen-test/system/skrypt-testowy"
squashfs "$K1_BUDOWA/rdzen-test" "$K1_WYNIK/rdzen-test.squashfs"

cp "$K1_REPO/jadro/cmdline" "$K1_WYNIK/cmdline"
(cd "$K1_WYNIK" && sha256sum bzImage initramfs.cpio rdzen.squashfs rdzen-test.squashfs rdzen.squashfs.skrot wydanie cmdline > SHA256SUMS)
{
    echo "K1 $K1_WERSJA, jądro $K1_JADRO_WERSJA, SOURCE_DATE_EPOCH=$SOURCE_DATE_EPOCH"
    dpkg-query -W -f '${Package} ${Version}\n' gcc-13 musl-tools squashfs-tools 2>/dev/null || true
} > "$K1_WYNIK/srodowisko.txt"
info "gotowe:"; sed 's/^/    /' "$K1_WYNIK/SHA256SUMS"
