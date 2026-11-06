#!/bin/sh
# Porównuje konfigurację jądra K1 z zaleceniami KSPP (Kernel Self Protection Project).
# Źródło listy: plik danych projektu kernel-hardening-checker (przypięty commit), odczytany jako
# tekst — kod narzędzia NIE jest uruchamiany. Wynik: opcje, w których się różnimy; każdą różnicę
# trzeba uzasadnić w docs/komponenty/jadro.md. Uruchamiać przy każdej zmianie wersji jądra.
. "$(dirname "$0")/wspolne.sh"
KHC_URL=https://github.com/a13xp0p0v/kernel-hardening-checker
KHC_TAG=v0.6.17.1
KHC_COMMIT=afc376f2a935994793343cfeb05953583cc30191
KHC=$K1_BUDOWA/narzedzia/khc
CFG=${1:-$K1_WYNIK/jadro.config}
[ -f "$CFG" ] || blad "brak $CFG — najpierw scripts/zbuduj-jadro.sh"
if [ ! -d "$KHC" ]; then
    git clone -q --depth 1 --branch "$KHC_TAG" "$KHC_URL" "$KHC"
fi
[ "$(git -C "$KHC" rev-parse HEAD)" = "$KHC_COMMIT" ] || blad "nieoczekiwany commit kernel-hardening-checker"
grep -oE "KconfigCheck\('[a-z_]+', 'kspp', '[A-Z0-9_]+', '[^']*'\)" "$KHC/kernel_hardening_checker/checks.py" \
  | sed -E "s/KconfigCheck\('([a-z_]+)', 'kspp', '([A-Z0-9_]+)', '([^']*)'\)/\1 \2 \3/" | sort -u \
  | while read -r typ opcja chce; do
        mamy=$(grep -E "^CONFIG_$opcja=" "$CFG" | cut -d= -f2-); [ -n "$mamy" ] || mamy="is not set"
        if [ "$chce" = "is not set" ]; then [ "$mamy" = "is not set" ] && continue
        elif [ "$chce" = "is present" ]; then continue
        else [ "$mamy" = "$chce" ] && continue; fi
        case $chce in \*?*\*) w=${chce#\*}; w=${w%\*}; case $mamy in *"$w"*) continue ;; esac ;; esac
        printf '%-20s CONFIG_%-40s KSPP: %-12s K1: %s\n' "$typ" "$opcja" "$chce" "$mamy"
    done
