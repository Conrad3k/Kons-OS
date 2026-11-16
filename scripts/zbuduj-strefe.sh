#!/bin/sh
# Buduje obraz strefy przeglądarki (0.10.0 krok 2, ADR 0043 T5/T8): build/wynik/strefa-przegladarka.img (SquashFS xz).
#   - pakiety Debiana 13 z tabeli strefy/przegladarka.txt (wersja i suma SHA-256 każdego; odświeżenie tabeli:
#     scripts/odswiez-strefe.py); pobrane raz do build/strefa-pakiety, sprawdzane sumą przy każdej budowie;
#   - rozpakowane bez uruchamiania skryptów pakietów; to, co robiłyby skrypty, robimy tu (spis typów plików, moduły
#     obrazków GTK) — programami z tych samych pakietów, przez ich własny ld.so (bez chroot, bez roota);
#   - bez dokumentacji, podręczników, tłumaczeń poza polskim, raportów awarii Firefoksa;
#   - nasze programy: /init (gosc), /usr/lib/k1/{kompozytor,przekaznik}, mapa klawiatury, ustawienia przeglądarki
#     i GTK (bez przycisków okna Firefoksa — okno strefy zamyka i minimalizuje pasek K1).
# Powtarzalnie: stała kolejność i czas plików (SOURCE_DATE_EPOCH), właściciel root.
. "$(dirname "$0")/wspolne.sh"

TABELA=$K1_REPO/strefy/przegladarka.txt
PAKIETY=$K1_BUDOWA/strefa-pakiety
D=$K1_BUDOWA/strefa-przegladarka
WYNIK=$K1_WYNIK/strefa-przegladarka.img
for p in kompozytor przekaznik gosc; do [ -x "$K1_BUDOWA/bin/$p" ] || blad "brak $p — najpierw make"; done

info "pakiety strefy przeglądarki ($(grep -vc '^#' "$TABELA") z tabeli)"
mkdir -p "$PAKIETY"
MIGAWKA=$(sed -n 's/^# migawka \([0-9TZ]*\).*/\1/p' "$TABELA")
grep -v '^#' "$TABELA" | while read -r pakiet wersja archiwum suma plik; do
    cel=$PAKIETY/$(basename "$plik")
    if [ ! -f "$cel" ] || ! echo "$suma  $cel" | sha256sum -c --status; then
        case $archiwum in
            debian) adres=https://deb.debian.org/debian ;;
            debian-security) adres=https://security.debian.org/debian-security ;;
            *) blad "nieznane archiwum $archiwum ($pakiet)" ;;
        esac
        # Debian keeps only the newest versions: an older one is on snapshot.debian.org at the table's moment
        curl -fsS --retry 3 --connect-timeout 30 --speed-limit 2048 --speed-time 60 --max-time 900 -o "$cel.part" "$adres/$plik" ||
            curl -fsS --retry 3 --connect-timeout 30 --speed-limit 2048 --speed-time 60 --max-time 900 -o "$cel.part" "https://snapshot.debian.org/archive/$archiwum/$MIGAWKA/$plik" ||
            blad "nie pobrano $pakiet $wersja"
        mv "$cel.part" "$cel"
    fi
    echo "$suma  $cel" | sha256sum -c --status || blad "zła suma pakietu $pakiet $wersja"
done || exit 1

info "drzewo strefy"
rm -rf "$D"; mkdir -p "$D"
grep -v '^#' "$TABELA" | while read -r pakiet wersja archiwum suma plik; do
    dpkg-deb -x "$PAKIETY/$(basename "$plik")" "$D"
done
# Debian 13: /bin, /lib, /lib64, /sbin are links into /usr (base-files makes them; it is not in the image)
for l in bin lib lib64 sbin; do ln -sfn "usr/$l" "$D/$l"; done
LIB=$D/usr/lib/x86_64-linux-gnu
# What the packages' scripts would do, with their own programs and the image's ld.so
w_strefie() { "$D/usr/lib64/ld-linux-x86-64.so.2" --library-path "$LIB" "$@"; }
w_strefie "$D/usr/bin/update-mime-database" "$D/usr/share/mime" 2>/dev/null
w_strefie "$LIB/gdk-pixbuf-2.0/gdk-pixbuf-query-loaders" "$LIB"/gdk-pixbuf-2.0/2.10.0/loaders/*.so |
    sed "s#$D##g" > "$LIB/gdk-pixbuf-2.0/2.10.0/loaders.cache"
# Leave out what the browser never reads in K1
rm -rf "$D/usr/share/doc" "$D/usr/share/man" "$D/usr/share/info" "$D/usr/share/lintian" "$D/usr/share/bug" \
    "$D/usr/share/doc-base" "$D/usr/share/gdb" "$D/usr/share/gcc" "$D/usr/share/bash-completion" \
    "$D/usr/lib/firefox-esr/crashreporter" "$D/usr/lib/firefox-esr/crashhelper" "$D/usr/lib/firefox-esr/pingsender" \
    "$D/usr/lib/firefox-esr/minidump-analyzer" "$D/etc/init.d" "$D/etc/cron.daily" "$D/usr/sbin"
find "$D/usr/share/locale" -mindepth 1 -maxdepth 1 ! -name pl ! -name locale.alias -exec rm -rf {} +
find "$LIB/gconv" -name '*.so' ! -name 'UTF-*' ! -name 'UNICODE*' ! -name 'ISO8859-1*' -delete
# S14: no shell in the zone — nothing may be left that runs scripts
for s in sh bash dash; do [ ! -e "$D/usr/bin/$s" ] || blad "powłoka w obrazie strefy: /usr/bin/$s"; done

# Ours
mkdir -p "$D/usr/lib/k1" "$D/usr/share/k1" "$D/etc/firefox-esr" "$D/usr/share/firefox-esr/distribution" \
    "$D/dev" "$D/proc" "$D/sys" "$D/tmp" "$D/run" "$D/home"
install -m 0555 "$K1_BUDOWA/bin/gosc" "$D/init"
install -m 0555 "$K1_BUDOWA/bin/kompozytor" "$K1_BUDOWA/bin/przekaznik" "$D/usr/lib/k1/"
install -m 0444 "$K1_REPO/strefy/klawiatura-pl.xkb" "$D/usr/share/k1/klawiatura.xkb"
install -m 0444 "$K1_REPO/strefy/przegladarka/k1.js" "$D/etc/firefox-esr/k1.js"
install -m 0444 "$K1_REPO/strefy/przegladarka/policies.json" "$D/usr/share/firefox-esr/distribution/policies.json"
install -m 0444 "$K1_REPO/strefy/przegladarka/gtk-settings.ini" "$D/etc/gtk-3.0/settings.ini" # S28: no window buttons
printf 'root:x:0:0::/:/nonexistent\nk1:x:1000:1000::/home/k1:/nonexistent\n' > "$D/etc/passwd"
printf 'root:x:0:\nk1:x:1000:\n' > "$D/etc/group"
chmod 0444 "$D/etc/passwd" "$D/etc/group"
ln -sfn /run/machine-id "$D/etc/machine-id" # gosc writes a new one for every machine

rm -f "$WYNIK"
mksquashfs "$D" "$WYNIK" -quiet -no-progress -noappend -comp xz -Xbcj x86 -b 1M -all-root -no-xattrs \
    >/dev/null # SOURCE_DATE_EPOCH (wspolne.sh): the time of every file and of the image
chmod 0444 "$WYNIK"
info "gotowe: $WYNIK ($(du -k "$WYNIK" | cut -f1) KiB)"
