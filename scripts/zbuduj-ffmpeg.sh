#!/bin/sh
# FFmpeg dla strefy przeglądarki — filmy H.264 (0.13.2 krok 2, ADR 0049 pkt 8, ADR 0063 pkt 7): Firefox w strefie
# rozpakowuje H.264 tylko systemowym FFmpeg (wbudowane ffvpx ma VP8, VP9, AV1) — bez niego filmy H.264 nie grały wcale.
# Mała własna budowa zamiast pakietu Debiana (ten ciągnie kilkadziesiąt bibliotek): tylko libavcodec i libavutil,
# dekoder i parser H.264, VA-API (h264_vaapi — w sesji z filmami kartą, krok 4), LGPL (bez --enable-gpl), bez
# programów, z --enable-small (ADR 0049 pkt 8) i asemblerem x86 (nasm — szybkie rozpakowanie procesorem).
# Linkowana z bibliotekami komputera budowy (Ubuntu 24.04, glibc 2.39; nagłówki libva z libva-dev), w strefie działa
# z bibliotekami Debiana 13 (glibc 2.41, libva2 i libva-drm2 z tabeli strefy) — jak nasza Mesa.
# Źródła i suma: src/zewnetrzne/ffmpeg/zrodla.txt. Wynik: build/ffmpeg/inst/usr/lib/x86_64-linux-gnu/ (libavcodec.so.61,
# libavutil.so.59), scripts/zbuduj-strefe.sh kopiuje je do obrazu strefy. Pomija budowę, gdy build/ffmpeg/gotowe ma ten
# sam znak (skrót tabeli i tego skryptu); trwa ok. 1–2 min.
# Powtarzalnie: budowa w drzewie źródeł (ścieżki w __FILE__ względne, w opisie konfiguracji żadnej ścieżki budowy),
# SOURCE_DATE_EPOCH, build-id z treści (ld), bez informacji dla debuggera.
. "$(dirname "$0")/wspolne.sh"

B=$K1_BUDOWA/ffmpeg
POB=$K1_BUDOWA/pobrane/ffmpeg
TABELA=$K1_REPO/src/zewnetrzne/ffmpeg/zrodla.txt
ZNAK=$(cat "$TABELA" "$0" | sha256sum | cut -d' ' -f1)
if [ -f "$B/gotowe" ] && [ "$(cat "$B/gotowe")" = "$ZNAK" ]; then
    info "ffmpeg: gotowy (bez zmian w tabeli i skrypcie)"
    exit 0
fi
rm -rf "$B"
mkdir -p "$B/zr" "$POB"
pkg-config --exists libva libva-drm || blad "brak nagłówków libva (libva-dev — scripts/przygotuj-srodowisko.sh)"
command -v nasm >/dev/null || blad "brak nasm (scripts/przygotuj-srodowisko.sh)"

while read -r adres suma reszta; do
    case $adres in '' | '#'*) continue ;; esac
    [ -n "$suma" ] && [ -z "$reszta" ] || blad "src/zewnetrzne/ffmpeg/zrodla.txt: zły wiersz „$adres”"
    plik=$POB/$(basename "$adres")
    if [ ! -f "$plik" ]; then
        curl -fsSL --retry 3 --connect-timeout 30 --speed-limit 2048 --speed-time 60 --max-time 900 -o "$plik.tmp" \
            "$adres" || blad "nie pobrano $(basename "$adres")"
        mv "$plik.tmp" "$plik"
    fi
    echo "$suma  $plik" | sha256sum -c --quiet - || { rm -f "$plik"; blad "zła suma kontrolna $(basename "$plik")"; }
    tar xf "$plik" -C "$B/zr"
done <"$TABELA"

ZR=$(find "$B/zr" -mindepth 1 -maxdepth 1 -type d -name 'ffmpeg-*')
[ -d "$ZR" ] || blad "brak źródeł FFmpeg"

info "ffmpeg: konfiguracja (libavcodec + libavutil, H.264 i VA-API, LGPL)"
# --disable-autodetect: nothing from the build computer gets in by itself (X11, libdrm, zlib…); VA-API named explicitly.
(cd "$ZR" && ./configure --prefix=/usr --libdir=/usr/lib/x86_64-linux-gnu --shlibdir=/usr/lib/x86_64-linux-gnu \
    --enable-shared --disable-static --disable-programs --disable-doc --disable-debug --disable-autodetect \
    --disable-everything --disable-network --disable-avformat --disable-avdevice --disable-avfilter --disable-swscale \
    --disable-swresample --enable-small --enable-vaapi --enable-decoder=h264 --enable-parser=h264 \
    --enable-hwaccel=h264_vaapi) >"$B/konfiguracja.log" 2>&1 ||
    { tail -20 "$B/konfiguracja.log"; tail -30 "$ZR/ffbuild/config.log"; blad "ffmpeg: konfiguracja nieudana"; }
grep -q "^#define CONFIG_H264_VAAPI_HWACCEL 1" "$ZR/config_components.h" || blad "ffmpeg: bez h264_vaapi (libva?)"
grep -q '^#define CONFIG_GPL 0' "$ZR/config.h" || blad "ffmpeg: włączone części GPL"
info "ffmpeg: budowa"
make -C "$ZR" -j"$(nproc)" >"$B/budowa.log" 2>&1 || { tail -20 "$B/budowa.log"; blad "ffmpeg: budowa nieudana"; }
make -C "$ZR" install DESTDIR="$B/inst" >>"$B/budowa.log" 2>&1 || blad "ffmpeg: instalacja nieudana"

L=$B/inst/usr/lib/x86_64-linux-gnu
rm -rf "$B/inst/usr/include" "$B/inst/usr/share" "$L/pkgconfig"
find "$L" -maxdepth 1 -type l -name '*.so' -delete # the links for linking; libav*.so.NN (Firefox's names) stay
find "$L" -name '*.so*' -type f -exec strip --strip-unneeded {} +
for f in libavcodec.so.61 libavutil.so.59; do [ -e "$L/$f" ] || blad "ffmpeg: brak $f"; done
# What the libraries need: the zone's glibc and libva — nothing else from the build computer (--disable-autodetect)
for f in "$L"/libavcodec.so.61 "$L"/libavutil.so.59; do
    obce=$(readelf -d "$f" | sed -n 's/.*(NEEDED).*\[\(.*\)\]/\1/p' |
        grep -v -x -e 'libc.so.6' -e 'libm.so.6' -e 'libva.so.2' -e 'libva-drm.so.2' -e 'libavutil.so.59' || true)
    [ -z "$obce" ] || blad "ffmpeg: $(basename "$f") potrzebuje $obce"
done
echo "$ZNAK" >"$B/gotowe"
info "ffmpeg: gotowy — $(du -sk "$L" | cut -f1) KiB"
