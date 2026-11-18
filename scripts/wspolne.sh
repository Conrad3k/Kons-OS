# Wspólne ustawienia skryptów K1 (dołączane przez `. scripts/wspolne.sh`).
set -eu
K1_REPO=$(cd "$(dirname "$0")/.." && pwd)
K1_BUDOWA=${K1_BUDOWA:-$K1_REPO/build}
K1_WYNIK=$K1_BUDOWA/wynik
K1_WERSJA=$(cat "$K1_REPO/VERSION")
. "$K1_REPO/jadro/wersja.env"
K1_JADRO_ZRODLA=$K1_BUDOWA/zrodla/linux-$K1_JADRO_WERSJA
K1_JADRO_OBJ=$K1_BUDOWA/jadro-obj
K1_STREFA_OBJ=$K1_BUDOWA/jadro-strefy-obj  # 0.10.0 (ADR 0042): the zone kernel, the same sources
K1_NAGLOWKI=$K1_BUDOWA/naglowki
# Stały znacznik czasu = powtarzalne wyniki (data ostatniego commita, a bez gita: 0).
SOURCE_DATE_EPOCH=${SOURCE_DATE_EPOCH:-$(git -C "$K1_REPO" log -1 --format=%ct 2>/dev/null || echo 0)}
export SOURCE_DATE_EPOCH
K1_CPU=$(nproc 2>/dev/null || echo 2)
info() { printf '[k1] %s\n' "$*"; }
blad() { printf '[k1] BŁĄD: %s\n' "$*" >&2; exit 1; }
# zamknij_qemu GNIAZDO PID — maszyna testowa: „quit” przez monitor (albo kill, gdy nie odpowiada); czeka na koniec.
zamknij_qemu() {
    python3 -I "$K1_REPO/testy/qemu/k1qemu.py" zamknij "$1" || kill "$2" 2>/dev/null || true
    wait "$2" 2>/dev/null || true
}
# log_k1 PLIK — log z portu szeregowego bez komunikatów jądra wpisanych w środek wiersza K1 (jeden strumień; jak
# WTRACENIE w k1qemu.py): „K1-NADZORCA: partycja danych [ 2.79] sda: sda1 sda2⏎K1-DANE otwarta…” → jeden wiersz K1.
# Skrypty, które sprawdzają log grep-em, czytają go przez tę funkcję (test-m5, CI przebieg 214).
log_k1() { python3 -I "$K1_REPO/testy/qemu/k1qemu.py" log "$1" 2>/dev/null; }
# M6b (ADR 0021): dm-verity hash tree appended to a SquashFS image (data padded to 4 KiB blocks, the
# tree right after it, no superblock); the root hash goes to PLIK.skrot (k1.rdzen_skrot=… on the
# kernel command line). Fixed salt: the same image gives the same bytes and the same root hash.
K1_VERITY_SOL=4b31207264657a656e2056657269747920736f6c00000000000000000000000000
verity() { # $1 = plik SquashFS (rozszerzany w miejscu)
    command -v veritysetup >/dev/null || blad "brak veritysetup (scripts/przygotuj-srodowisko.sh: cryptsetup-bin)"
    _rozm=$(stat -c %s "$1"); _bloki=$(( (_rozm + 4095) / 4096 ))
    truncate -s $((_bloki * 4096)) "$1"
    veritysetup format --no-superblock --hash-offset=$((_bloki * 4096)) --data-blocks="$_bloki" \
        --data-block-size=4096 --hash-block-size=4096 --salt="$K1_VERITY_SOL" "$1" "$1" > "$1.verity.log" 2>&1 \
        || { cat "$1.verity.log"; blad "veritysetup: $1"; }
    sed -n 's/^Root hash:[[:space:]]*\([0-9a-f]\{64\}\)$/\1/p' "$1.verity.log" > "$1.skrot"
    rm -f "$1.verity.log"
    [ "$(wc -c < "$1.skrot")" -eq 65 ] || blad "veritysetup: brak skrótu głównego dla $1"
}

# Test battery for QEMU (testy/acpi/bateria.asl → ACPI table): $1 = state (1 discharging,
# 2 charging), $2 = AC adapter online (0/1), $3 = output .aml file.
bateria_aml() {
    sed "s/@STAN@/$1/; s/@ZASILACZ@/$2/" "$K1_REPO/testy/acpi/bateria.asl" > "${3%.aml}.asl"
    iasl -p "${3%.aml}" "${3%.aml}.asl" >/dev/null 2>&1 || blad "iasl: nie udało się skompilować ${3%.aml}.asl"
}
# The kernel's make (scripts/zbuduj-jadro.sh, zbuduj-k1efi.sh), reproducible: fixed user, host, version and
# time in the kernel; the latent_entropy plugin draws its constants on every build without -frandom-seed
# (scripts/gcc-plugins/latent_entropy_plugin.c: get_random_seed) — a fixed seed gives the same kernel.
make_jadra() {
    make_jadra_w "$K1_JADRO_OBJ" "$@"
}
# make_jadra_w DIR …: the kernel build in object directory DIR (the K1 kernel or the zone kernel)
make_jadra_w() {
    obj=$1; shift
    KBUILD_BUILD_USER=k1 KBUILD_BUILD_HOST=k1-budowa KBUILD_BUILD_VERSION=1 \
    KBUILD_BUILD_TIMESTAMP=$(date -u -d "@$SOURCE_DATE_EPOCH" '+%Y-%m-%d %H:%M:%S UTC') \
        make -s -C "$K1_JADRO_ZRODLA" O="$obj" ARCH=x86_64 KCFLAGS=-frandom-seed=k1-"$K1_JADRO_WERSJA" "$@"
}

# The start of every K1 grub.cfg (ISO and stick): no menu, the graphics mode, the boot screen settings.
grub_poczatek() {
    cat <<CFG
# Kons K1 $K1_WERSJA — start bez menu. Tryb graficzny ustawia GRUB (UEFI GOP albo VESA),
# jądro przejmuje go sterownikiem simpledrm. 1366x768 = ekran ThinkPada X230 (firmware laptopa
# nie oferuje 1280x800 — za dużo linii); nie jako pierwszy: karty emulowane (QEMU) pokazują ten
# tryb krzywo (szerokość zaokrąglona do 1360).
set timeout=0
set timeout_style=hidden
insmod all_video
insmod gfxterm
set gfxmode=1280x800,1366x768,1280x720,1024x768,auto
set gfxpayload=keep
insmod png
# Boot screen (0.5.8): the K1 mark on black; the kernel keeps it until the desktop's first frame.
# GRUB draws the picture only when something is written (echo); black text on black (set after
# terminal_output, which resets the colours) hides its cursor.
terminal_output gfxterm
set color_normal=black/black
CFG
}

# Signed EFI boot loader from Ubuntu (0.5.6): shim (signed by Microsoft) + GRUB for removable media
# (gcdx64, signed by Canonical; finds its medium by /.disk/info, reads /boot/grub/grub.cfg). Our own
# unsigned GRUB images were flagged by Windows Defender as "Trojan:Win32/Sabsik!ml". The packages
# are only unpacked (apt checks their hashes against the signed archive), never installed.
# MokManager (mmx64.efi, signed by Canonical) enrols the K1 key for Secure Boot (0.7.0, ADR 0022).
# Sets K1_EFI (directory with shimx64.efi.signed.latest, gcdx64.efi.signed, mmx64.efi and wersje.txt).
efi_podpisane() {
    K1_EFI=$K1_BUDOWA/pobrane/efi
    [ -f "$K1_EFI/gcdx64.efi.signed" ] && [ -f "$K1_EFI/shimx64.efi.signed.latest" ] && [ -f "$K1_EFI/mmx64.efi" ] && return 0
    mkdir -p "$K1_EFI"
    (cd "$K1_EFI" && rm -f ./*.deb && apt-get download shim-signed grub-efi-amd64-signed >/dev/null 2>&1) \
        || blad "nie udało się pobrać shim-signed / grub-efi-amd64-signed (apt)"
    rm -rf "$K1_EFI/x"; mkdir -p "$K1_EFI/x"
    for deb in "$K1_EFI"/*.deb; do dpkg-deb -x "$deb" "$K1_EFI/x"; done
    cp "$K1_EFI/x/usr/lib/shim/shimx64.efi.signed.latest" "$K1_EFI/x/usr/lib/grub/x86_64-efi-signed/gcdx64.efi.signed" \
        "$K1_EFI/x/usr/lib/shim/mmx64.efi" "$K1_EFI/"
    (cd "$K1_EFI" && ls ./*.deb > wersje.txt)
}

# A FAT image of `$2` MiB at `$1` holding the files given as "source:destination" pairs; created
# before mformat (otherwise mformat writes uninitialised memory into the last sector), fixed volume
# serial `$3`, file dates = SOURCE_DATE_EPOCH: the same inputs give the same image.
obraz_fat() {
    plik=$1 mib=$2 numer=$3; shift 3
    rm -f "$plik"; truncate -s "${mib}M" "$plik"
    # shellcheck disable=SC2086 # K1_FAT_OPCJE: extra mformat options (e.g. "-F -c 8": FAT32, 4 KiB clusters)
    mformat -i "$plik" -T $((mib * 2048)) -N "$numer" -v K1-EFI ${K1_FAT_OPCJE:-} :: || blad "mformat $plik"
    for para in "$@"; do
        zrodlo=${para%%:*} cel=${para#*:}
        katalog=$(dirname "$cel")
        d=; for czesc in $(echo "$katalog" | tr '/' ' '); do
            d=$d/$czesc; mmd -D s -i "$plik" "::$d" 2>/dev/null || true
        done
        cp "$zrodlo" "$K1_BUDOWA/fat.tmp" && touch -d "@$SOURCE_DATE_EPOCH" "$K1_BUDOWA/fat.tmp"
        mcopy -m -i "$plik" "$K1_BUDOWA/fat.tmp" "::$cel" || blad "mcopy $cel"
    done
    rm -f "$K1_BUDOWA/fat.tmp"
}
