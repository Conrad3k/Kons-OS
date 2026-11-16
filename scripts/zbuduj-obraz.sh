#!/bin/sh
# Buduje programy K1 i składa z nich obrazy startowe:
#   build/wynik/initramfs.cpio     — /init (k1-initramfs) + /dev/console, bez kompresji
#   build/wynik/rdzen.squashfs     — obraz /rdzen: init, system/ (nadzorca i usługi z K1_SYSTEM), katalogi jadro/
#   build/wynik/rdzen-test.squashfs — to samo + program testowy jako /rdzen/system/test-systemu i obraz strefy próbnej
#                                     (/rdzen/strefy/probna.img: /init = gosc; 0.10.0, ADR 0042)
#   w obu: obraz strefy przeglądarki /rdzen/strefy/przegladarka.img (scripts/zbuduj-strefe.sh; 0.10.0 krok 2, ADR 0043)
#          i pusty dysk strefy /rdzen/strefy/dysk.img (ext4 2 GiB, prawie same zera; 0.10.0 krok 5 — dysk sesji, która
#          pamięta: kawałek, którego strefa jeszcze nie zapisała, VMM czyta stąd)
# Wymaga zbudowanego jądra (scripts/zbuduj-jadro.sh: nagłówki i gen_init_cpio) i biblioteki C (scripts/zbuduj-musl.sh;
# make zbuduje ją sam, gdy jej brak).
. "$(dirname "$0")/wspolne.sh"

GEN_CPIO=$K1_JADRO_OBJ/usr/gen_init_cpio
[ -x "$GEN_CPIO" ] && [ -d "$K1_NAGLOWKI/include" ] || blad "najpierw zbuduj jądro: scripts/zbuduj-jadro.sh"

info "programy K1 (musl, statycznie)"
make -s -C "$K1_REPO" BUDOWA="$K1_BUDOWA" NAGLOWKI="$K1_NAGLOWKI/include"
"$K1_REPO/scripts/sprawdz-binaria.sh" --fortify "$K1_BUDOWA/obj/proba_fortify" "$K1_BUDOWA"/bin/*
# 0.10.0 step 2 (ADR 0043): the browser zone's image (Debian's packages + our gosc, kompozytor, przekaznik)
"$K1_REPO/scripts/zbuduj-strefe.sh"

info "initramfs.cpio"
cat > "$K1_BUDOWA/initramfs.lista" <<LISTA
dir /dev 0755 0 0
nod /dev/console 0600 0 0 c 5 1
dir /proc 0755 0 0
dir /nowy_korzen 0755 0 0
file /init $K1_BUDOWA/bin/k1-initramfs 0500 0 0
LISTA
"$GEN_CPIO" -t "$SOURCE_DATE_EPOCH" "$K1_BUDOWA/initramfs.lista" > "$K1_WYNIK/initramfs.cpio"

# 0.10.0 step 5 (T11): the empty disk of a zone that remembers — ext4 of 2 GiB (K1_STREFA_DYSK_KAWALKI pieces of 64 KiB),
# made here so that no zone needs mkfs; reproducible: a fixed UUID and hash seed, the time from SOURCE_DATE_EPOCH, no
# mke2fs.conf of the build computer (every feature named), inode tables and the journal written now (zeros).
pusty_dysk_strefy() { # $1 = plik wynikowy
    rm -f "$1"; truncate -s 2G "$1"
    MKE2FS_CONFIG=/dev/null E2FSPROGS_FAKE_TIME=$SOURCE_DATE_EPOCH mke2fs -q -F -b 4096 -I 256 -i 16384 -m 0 -J size=16 \
        -L k1-dane -U 4b315f64-616e-6500-0000-000000000001 \
        -O has_journal,extent,huge_file,flex_bg,metadata_csum,64bit,dir_nlink,extra_isize,sparse_super,large_file,filetype,dir_index,ext_attr,^resize_inode \
        -E hash_seed=4b315f64-616e-6500-0000-000000000002,lazy_itable_init=0,lazy_journal_init=0,root_owner=0:0,nodiscard \
        "$1" 2>/dev/null || blad "mke2fs: pusty dysk strefy"
    chmod 0444 "$1"
}
pusty_dysk_strefy "$K1_WYNIK/strefa-dysk.img"

# Drzewo /rdzen: tylko to, co potrzebne. Uprawnienia ustawiane jawnie, właściciel root (-all-root).
przygotuj_rdzen() { # $1 = katalog docelowy
    rm -rf "$1"; mkdir -p "$1/jadro/dev" "$1/jadro/proc" "$1/jadro/sys" "$1/system" "$1/aplikacje" "$1/strefy"
    install -m 0555 "$K1_BUDOWA/bin/init" "$1/init"
    for p in nadzorca strefa $(sh "$K1_REPO/scripts/uslugi-systemu.sh"); do install -m 0555 "$K1_BUDOWA/bin/$p" "$1/system/$p"; done
    # 0.10.0 (ADR 0042 T5): the zone kernel — data for the VMM (read, never executed by K1); the zone images beside it
    install -m 0444 "$K1_WYNIK/strefa-jadro" "$1/strefy/jadro"
    install -m 0444 "$K1_WYNIK/strefa-przegladarka.img" "$1/strefy/przegladarka.img"
    cp --sparse=always "$K1_WYNIK/strefa-dysk.img" "$1/strefy/dysk.img" && chmod 0444 "$1/strefy/dysk.img"
    # Built-in native applications (M4); signed application images come with M10 (/aplikacje).
    for d in "$K1_REPO"/src/aplikacje/*/; do p=$(basename "$d"); install -m 0555 "$K1_BUDOWA/bin/$p" "$1/aplikacje/$p"; done
    chmod 0755 "$1" "$1/jadro" "$1/jadro/dev" "$1/jadro/proc" "$1/jadro/sys" "$1/system" "$1/aplikacje" "$1/strefy"
}
# The image of the test zone (0.10.0 step 1): gosc as /init and the mount points it uses; SquashFS like the core.
obraz_strefy_probnej() { # $1 = plik wynikowy
    d=$K1_BUDOWA/strefa-probna
    rm -rf "$d"; mkdir -p "$d/dev" "$d/proc" "$d/sys" "$d/tmp" "$d/dane"
    install -m 0555 "$K1_BUDOWA/bin/gosc" "$d/init"
    chmod 0755 "$d" "$d/dev" "$d/proc" "$d/sys" "$d/tmp" "$d/dane"
    rm -f "$1"
    mksquashfs "$d" "$1" -quiet -no-progress -noappend -comp zstd -all-root -no-xattrs >/dev/null
    chmod 0444 "$1"
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
obraz_strefy_probnej "$K1_BUDOWA/rdzen-test/strefy/probna.img"
squashfs "$K1_BUDOWA/rdzen-test" "$K1_WYNIK/rdzen-test.squashfs"

cp "$K1_REPO/jadro/cmdline" "$K1_WYNIK/cmdline"
(cd "$K1_WYNIK" && sha256sum bzImage initramfs.cpio rdzen.squashfs rdzen-test.squashfs rdzen.squashfs.skrot wydanie cmdline > SHA256SUMS)
{
    . "$K1_REPO/musl/wersja.env"
    echo "K1 $K1_WERSJA, jądro $K1_JADRO_WERSJA, musl $K1_MUSL_WERSJA + $(ls "$K1_REPO"/musl/latki | wc -l) łatki, fortify-headers 3.0.1, SOURCE_DATE_EPOCH=$SOURCE_DATE_EPOCH"
    dpkg-query -W -f '${Package} ${Version}\n' gcc-13 squashfs-tools 2>/dev/null || true
} > "$K1_WYNIK/srodowisko.txt"
info "gotowe:"; sed 's/^/    /' "$K1_WYNIK/SHA256SUMS"
