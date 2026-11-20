#!/bin/sh
# Mesa dla strefy przeglądarki — szybka grafika (0.10.9 krok 6, ADR 0049 pkt 7, plan S36, S43): przeglądarka w sesji
# z szybką grafiką rysuje kartą przez Mesę z opcją intel-virtio-experimental (sterownik iris, karty Intela od Gen8) —
# jej prośby idą mostem native context do pośrednika grafiki K1 (src/posrednik), który sprawdza każdą.
# Dwa etapy (Mesa 26 kompiluje swoje małe programy karty Intela z OpenCL C — a to wymaga LLVM):
#   1. narzędzia na komputer budowy: mesa_clc i vtn_bindgen2 (z LLVM 18 Ubuntu, libclc, tłumaczem SPIR-V) —
#      tylko do budowy, nie trafiają do strefy;
#   2. Mesa dla strefy: tylko iris (+ intel-virtio), EGL (Wayland, bez GLX), GBM, OpenGL i GLES 2, bez LLVM, VA-API,
#      Vulkan i narzędzi. Linkowana z bibliotekami komputera budowy (Ubuntu 24.04, glibc 2.39), w strefie działa
#      z bibliotekami Debiana 13 (glibc 2.41 i nowszy libstdc++ — zgodne wstecz; libdrm2 z tabeli strefy).
# Źródła i sumy: src/zewnetrzne/mesa/zrodla.txt. Wynik: build/mesa/inst/usr/lib/x86_64-linux-gnu/ (okrojone),
# scripts/zbuduj-strefe.sh kopiuje je do obrazu strefy. Pomija budowę, gdy build/mesa/gotowe ma ten sam znak (skrót
# tabeli i tego skryptu) — CI na PR-ze przywraca build/mesa z pamięci podręcznej (.github/workflows/k1.yml).
# Kod obcy: jego własne flagi i ostrzeżenia (jak Wuffs, libva); powtarzalnie: SOURCE_DATE_EPOCH, build-id z treści.
. "$(dirname "$0")/wspolne.sh"

B=$K1_BUDOWA/mesa
POB=$K1_BUDOWA/pobrane/mesa
TABELA=$K1_REPO/src/zewnetrzne/mesa/zrodla.txt
ZNAK=$(cat "$TABELA" "$0" | sha256sum | cut -d' ' -f1)
if [ -f "$B/gotowe" ] && [ "$(cat "$B/gotowe")" = "$ZNAK" ]; then
    info "mesa: gotowa (bez zmian w tabeli i skrypcie)"
    exit 0
fi
rm -rf "$B"
mkdir -p "$B/zr" "$B/meson-py" "$POB"

while read -r adres suma reszta; do
    case $adres in '' | '#'*) continue ;; esac
    [ -n "$suma" ] && [ -z "$reszta" ] || blad "src/zewnetrzne/mesa/zrodla.txt: zły wiersz „$adres”"
    plik=$POB/$(basename "$adres")
    if [ ! -f "$plik" ]; then
        curl -fsSL --retry 3 --connect-timeout 30 --speed-limit 2048 --speed-time 60 --max-time 900 -o "$plik.tmp" \
            "$adres" || blad "nie pobrano $(basename "$adres")"
        mv "$plik.tmp" "$plik"
    fi
    echo "$suma  $plik" | sha256sum -c --quiet - || { rm -f "$plik"; blad "zła suma kontrolna $(basename "$plik")"; }
    case $plik in
        *.whl) python3 -I -m zipfile -e "$plik" "$B/meson-py" ;;
        *) tar xf "$plik" -C "$B/zr" ;;
    esac
done <"$TABELA"

ZR=$(find "$B/zr" -mindepth 1 -maxdepth 1 -type d -name 'mesa-*')
[ -d "$ZR" ] || blad "brak źródeł Mesy"
# meson from the wheel, run by the system's Python 3 (python3-mako, -packaging, -yaml from apt); ninja from apt
PY=/usr/bin/python3
meson() { PYTHONPATH="$B/meson-py" "$PY" -m mesonbuild.mesonmain "$@"; }
export PYTHONPATH="$B/meson-py" PATH="/usr/bin:$PATH"
LLVM=/usr/lib/llvm-18/bin
[ -x "$LLVM/llvm-config" ] || blad "brak LLVM 18 (scripts/przygotuj-srodowisko.sh)"

info "mesa: etap 1 — narzędzia budowy (mesa_clc, vtn_bindgen2, z LLVM 18)"
(cd "$ZR" && PATH="$LLVM:$PATH" meson setup "$B/narzedzia" -Dbuildtype=release -Dplatforms= -Dgallium-drivers= \
    -Dvulkan-drivers= -Dglx=disabled -Degl=disabled -Dgbm=disabled -Dopengl=false -Dgles1=disabled -Dgles2=disabled \
    -Dllvm=enabled -Dshared-llvm=enabled -Dmesa-clc=enabled -Dinstall-mesa-clc=true -Dvalgrind=disabled \
    -Dlibunwind=disabled -Dzstd=disabled -Dxmlconfig=disabled -Dexpat=disabled) >"$B/narzedzia.log" 2>&1 ||
    { tail -20 "$B/narzedzia.log"; blad "mesa: konfiguracja narzędzi nieudana"; }
ninja -C "$B/narzedzia" src/compiler/clc/mesa_clc src/compiler/spirv/vtn_bindgen2 >>"$B/narzedzia.log" 2>&1 ||
    { tail -20 "$B/narzedzia.log"; blad "mesa: narzędzia się nie budują"; }
cat >"$B/narzedzia.ini" <<EOF
[binaries]
mesa_clc = '$B/narzedzia/src/compiler/clc/mesa_clc'
vtn_bindgen2 = '$B/narzedzia/src/compiler/spirv/vtn_bindgen2'
EOF

info "mesa: etap 2 — Mesa dla strefy (iris + intel-virtio, EGL na Waylandzie, GBM, bez LLVM)"
(cd "$ZR" && meson setup "$B/strefa" --native-file "$B/narzedzia.ini" --prefix=/usr --libdir=lib/x86_64-linux-gnu \
    -Dbuildtype=release -Db_ndebug=true -Dplatforms=wayland -Dgallium-drivers=iris -Dvulkan-drivers= \
    -Dintel-virtio-experimental=true -Dmesa-clc=system -Dprecomp-compiler=system -Dllvm=disabled -Dglx=disabled \
    -Dglvnd=disabled -Degl=enabled -Dgbm=enabled -Dopengl=true -Dgles1=disabled -Dgles2=enabled -Dgallium-va=disabled \
    -Dvideo-codecs= -Dxmlconfig=disabled -Dvalgrind=disabled -Dlibunwind=disabled -Dzstd=disabled -Dintel-rt=disabled \
    -Dtools= -Dbuild-tests=false -Dshader-cache=enabled -Dgallium-extra-hud=false -Dlmsensors=disabled \
    -Dperfetto=false -Dc_args="-ffile-prefix-map=$ZR=." -Dcpp_args="-ffile-prefix-map=$ZR=.") >"$B/strefa.log" 2>&1 ||
    { tail -20 "$B/strefa.log"; blad "mesa: konfiguracja nieudana"; }
ninja -C "$B/strefa" >>"$B/strefa.log" 2>&1 || { tail -20 "$B/strefa.log"; blad "mesa: budowa nieudana"; }
DESTDIR="$B/inst" ninja -C "$B/strefa" install >>"$B/strefa.log" 2>&1 || blad "mesa: instalacja nieudana"
L=$B/inst/usr/lib/x86_64-linux-gnu
rm -rf "$B/inst/usr/include" "$L/pkgconfig" "$B/inst/usr/share"
find "$L" -maxdepth 1 -type l -name '*.so' -delete # the links for linking (libEGL.so…); libgallium-*.so stays
find "$L" -name '*.so*' -type f -exec strip --strip-unneeded {} +
for f in libEGL.so.1 libGLESv2.so.2 libgbm.so.1 gbm/dri_gbm.so; do [ -e "$L/$f" ] || blad "mesa: brak $f"; done
ls "$L"/libgallium-*.so >/dev/null 2>&1 || blad "mesa: brak libgallium"
echo "$ZNAK" >"$B/gotowe"
info "mesa: gotowa — $(du -sk "$L" | cut -f1) KiB (bibliotek: $(find "$L" -name '*.so*' -type f | wc -l))"
