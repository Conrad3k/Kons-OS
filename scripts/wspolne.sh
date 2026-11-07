# Wspólne ustawienia skryptów K1 (dołączane przez `. scripts/wspolne.sh`).
set -eu
K1_REPO=$(cd "$(dirname "$0")/.." && pwd)
K1_BUDOWA=${K1_BUDOWA:-$K1_REPO/build}
K1_WYNIK=$K1_BUDOWA/wynik
K1_WERSJA=$(cat "$K1_REPO/VERSION")
. "$K1_REPO/jadro/wersja.env"
K1_JADRO_ZRODLA=$K1_BUDOWA/zrodla/linux-$K1_JADRO_WERSJA
K1_JADRO_OBJ=$K1_BUDOWA/jadro-obj
K1_NAGLOWKI=$K1_BUDOWA/naglowki
# Stały znacznik czasu = powtarzalne wyniki (data ostatniego commita, a bez gita: 0).
SOURCE_DATE_EPOCH=${SOURCE_DATE_EPOCH:-$(git -C "$K1_REPO" log -1 --format=%ct 2>/dev/null || echo 0)}
export SOURCE_DATE_EPOCH
K1_CPU=$(nproc 2>/dev/null || echo 2)
info() { printf '[k1] %s\n' "$*"; }
blad() { printf '[k1] BŁĄD: %s\n' "$*" >&2; exit 1; }
# Test battery for QEMU (testy/acpi/bateria.asl → ACPI table): $1 = state (1 discharging,
# 2 charging), $2 = AC adapter online (0/1), $3 = output .aml file.
bateria_aml() {
    sed "s/@STAN@/$1/; s/@ZASILACZ@/$2/" "$K1_REPO/testy/acpi/bateria.asl" > "${3%.aml}.asl"
    iasl -p "${3%.aml}" "${3%.aml}.asl" >/dev/null 2>&1 || blad "iasl: nie udało się skompilować ${3%.aml}.asl"
}
