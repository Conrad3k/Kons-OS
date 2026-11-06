#!/bin/sh
# Sprawdza, czy każda opcja z fragmentu (jadro/k1.config) ma w wynikowym .config żądaną wartość.
# Kconfig po cichu pomija opcje z niespełnionymi zależnościami albo nieistniejące — tu to wyłapujemy.
# Użycie: sprawdz-konfiguracje-jadra.sh FRAGMENT .config
set -eu
frag=$1; cfg=$2; zle=0
while IFS= read -r linia; do
    case $linia in
        CONFIG_*=*)
            opcja=${linia%%=*}
            if ! grep -qxF -- "$linia" "$cfg"; then
                echo "  ROZBIEŻNOŚĆ: chcemy '$linia', jest: '$(grep -E "^$opcja=|^# $opcja is not set" "$cfg" || echo brak)'"
                zle=1
            fi ;;
        "# CONFIG_"*" is not set")
            opcja=${linia#\# }; opcja=${opcja%% *}
            if grep -qE "^$opcja=" "$cfg"; then
                echo "  ROZBIEŻNOŚĆ: chcemy wyłączone $opcja, jest: '$(grep -E "^$opcja=" "$cfg")'"
                zle=1
            fi ;;
    esac
done < "$frag"
if [ "$zle" -ne 0 ]; then echo "[k1] BŁĄD: konfiguracja jądra różni się od jadro/k1.config" >&2; exit 1; fi
echo "[k1] konfiguracja jądra zgodna z fragmentem ($(grep -cE '^(CONFIG_|# CONFIG_)' "$frag") opcji)"
