#!/bin/bash
# Robot aktualizacji (0.10.5 krok 3, ADR 0047): co nowego w jądrze Linux i w przeglądarce strefy. Uruchamia go
# .github/workflows/robot.yml (raz dziennie), człowiek może też: scripts/robot.sh — w bieżącym drzewie.
#   (a) jądro: kernel.org/releases.json — najnowsza wersja tej samej linii; nowsza linia długoterminowa („longterm”, T11)
#       tylko gdy ROBOT_NOWA_LINIA=1 (robot.yml: 7 dni spokoju po ostatnim wydaniu robota) i gdy porównanie konfiguracji
#       (scripts/porownaj-jadro.sh, T19) nie ma pytań; suma z sha256sums.asc, którego podpis sprawdza gpg kluczami
#       z jadro/klucze/ (T8) → jadro/wersja.env;
#   (b) przeglądarka: scripts/odswiez-strefe.py (podpis InRelease Debiana) → strefy/przegladarka.txt;
#   (c) coś nowego → VERSION +1 PATCH, wpis w CHANGELOG.md ze znacznikiem robota (K1 pokazuje swoje stałe zdanie, D13).
# Zmienia wyłącznie: jadro/wersja.env, strefy/przegladarka.txt, VERSION, CHANGELOG.md (robot.yml to sprawdza, T8).
# Wynik (dla robot.yml, także w $GITHUB_OUTPUT, gdy jest): co=nic|zmiana|pytanie, tytul=…, galaz=…, a przy „pytanie”
# plik build/robot-pytanie.md (treść Issue: czego robot nie umie rozstrzygnąć sam). Kod ≠ 0: błąd źródła (zła suma,
# zły podpis) — robot.yml zakłada Issue.
set -eu
REPO=$(cd "$(dirname "$0")/.." && pwd)
cd "$REPO"
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
mkdir -p build
WYJ=${GITHUB_OUTPUT:-/dev/null}
wyjscie() { echo "$1"; echo "$1" >> "$WYJ"; }
. jadro/wersja.env
STARA=$K1_JADRO_WERSJA
LINIA=${STARA%.*}
pobierz() { curl -fsSL --retry 3 --connect-timeout 30 --speed-limit 2048 --speed-time 60 --max-time 1800 -o "$2" "$1"; }

# --- (a) the kernel ---
pobierz https://www.kernel.org/releases.json "$T/releases.json"
NOWA=$(python3 -I - "$T/releases.json" "$LINIA" "${ROBOT_NOWA_LINIA:-0}" <<'PY'
import json, sys
r = json.load(open(sys.argv[1]))["releases"]
linia, nowa_linia = sys.argv[2], sys.argv[3] == "1"
def w(v): return tuple(int(x) for x in v.split("."))
ta = [x["version"] for x in r if x["moniker"] in ("stable", "longterm") and x["version"].rsplit(".", 1)[0] == linia]
lt = [x["version"] for x in r if x["moniker"] == "longterm" and w(x["version"].rsplit(".", 1)[0]) > w(linia)]
kand = sorted(ta, key=w)[-1:]
if nowa_linia and lt:
    kand = [sorted(lt, key=w)[-1]]
print(kand[0] if kand else "")
PY
)
[ -n "$NOWA" ] || NOWA=$STARA
ZMIANY= OPISY= PYTANIE=
if [ "$NOWA" != "$STARA" ] && python3 -I -c 'import sys; a,b=(tuple(map(int,x.split("."))) for x in sys.argv[1:]); sys.exit(0 if b>a else 1)' "$STARA" "$NOWA"; then
    GLOWNA=${NOWA%%.*}
    URL=https://cdn.kernel.org/pub/linux/kernel/v$GLOWNA.x
    pobierz "$URL/sha256sums.asc" "$T/sha256sums.asc"
    export GNUPGHOME="$T/gpg"; mkdir -m 700 "$GNUPGHOME"
    gpg -q --import jadro/klucze/*.asc 2>/dev/null
    gpg -q --decrypt "$T/sha256sums.asc" > "$T/sumy" 2>/dev/null || true
    gpg -q --status-fd 3 --verify "$T/sha256sums.asc" 3> "$T/status" 2>/dev/null || true
    grep -qE '^\[GNUPG:\] VALIDSIG (647F28654894E3BD457199BE38DBBDC86092693E|B8868C80BA62A1FFFAF5FDA9632D3A06589DA6B1) ' "$T/status" ||
        { echo "robot: sha256sums.asc z $URL bez ważnego podpisu kluczem z jadro/klucze/" >&2; exit 3; }
    SUMA=$(awk -v p="linux-$NOWA.tar.xz" '$2 == p { print $1 }' "$T/sumy")
    echo "$SUMA" | grep -qE '^[0-9a-f]{64}$' || { echo "robot: brak sumy linux-$NOWA.tar.xz w podpisanym sha256sums.asc" >&2; exit 3; }
    if [ "${NOWA%.*}" != "$LINIA" ]; then # a new line: the configuration compared first (T19)
        pobierz "$URL/linux-$NOWA.tar.xz" "$T/nowe.tar.xz"
        echo "$SUMA  $T/nowe.tar.xz" | sha256sum -c --quiet - || { echo "robot: zła suma linux-$NOWA.tar.xz" >&2; exit 3; }
        mkdir "$T/zr" && tar -C "$T/zr" -xf "$T/nowe.tar.xz"
        { scripts/porownaj-jadro.sh "$T/zr/linux-$NOWA" "${ROBOT_STARY_CONFIG:-build/wynik/jadro.config}" \
              "${ROBOT_FRAGMENT:-build/jadro-fragment.config}" jadro/k1-odrzucone.txt &&
          scripts/porownaj-jadro.sh "$T/zr/linux-$NOWA" "${ROBOT_STARY_STREFY:-build/wynik/strefa-jadro.config}" \
              jadro/strefa.config jadro/strefa-odrzucone.txt; } > "$T/porownanie" 2>&1 || PYTANIE=jadro
    fi
    if [ -z "$PYTANIE" ]; then
        sed -i "s|^K1_JADRO_WERSJA=.*|K1_JADRO_WERSJA=$NOWA|; s|^K1_JADRO_SHA256=.*|K1_JADRO_SHA256=$SUMA|; s|^K1_JADRO_URL=.*|K1_JADRO_URL=$URL|" \
            jadro/wersja.env
        ZMIANY="jadro-$NOWA"
        if [ "${NOWA%.*}" != "$LINIA" ]; then
            OPISY="- **Jądro Linux ${NOWA%.*} — nowa linia długoterminowa** ($NOWA; było $STARA). Opcje konfiguracji porównane (scripts/porownaj-jadro.sh): bez nowych pytań.
<!-- k1: nowa linia jądra -->"
        else
            OPISY="- **Jądro Linux $NOWA** — poprawki bezpieczeństwa i błędów (było $STARA)."
        fi
    else
        { echo "Robot nie przechodzi sam na jądro Linux $NOWA (nowa linia, było $STARA): porównanie konfiguracji (T19)"
          echo "znalazło opcje do rozstrzygnięcia — każdą do fragmentu (\`jadro/k1.config\` / \`jadro/strefa.config\`) albo"
          echo "do listy odrzuconych (\`jadro/k1-odrzucone.txt\` / \`jadro/strefa-odrzucone.txt\`, z powodem). Następny bieg robota"
          echo "ruszy dalej. Wynik \`scripts/porownaj-jadro.sh\`:"; echo; echo '```'; cat "$T/porownanie"; echo '```'; } > build/robot-pytanie.md
    fi
fi

# T20 (0.10.5 step 7): K1 may be on a short line (stable, not longterm — 7.3 since 0.10.5). When it ends (end of life)
# and no longterm line as new as it exists, the robot does not pick a short line itself: an Issue for a person.
if [ -z "$PYTANIE" ] && [ "$NOWA" = "$STARA" ] && python3 -I - "$T/releases.json" "$LINIA" <<'PY'
import json, sys
r = json.load(open(sys.argv[1]))["releases"]
def w(v): return tuple(int(x) for x in v.split("."))
linia = sys.argv[2]
ta = [x for x in r if x["moniker"] in ("stable", "longterm") and x["version"].rsplit(".", 1)[0] == linia]
lt = [x for x in r if x["moniker"] == "longterm" and w(x["version"].rsplit(".", 1)[0]) >= w(linia)]
sys.exit(0 if all(x.get("iseol") for x in ta) and not lt else 1)
PY
then
    PYTANIE=linia
    wyjscie "pytanie_tytul=Robot: linia jądra $LINIA kończy wsparcie — wybór następnej linii"
    { echo "Linia jądra Linux $LINIA, na której jest K1 ($STARA), kończy wsparcie na kernel.org, a nowszej linii"
      echo "długoterminowej jeszcze nie ma. Robot sam linii krótkiej nie wybiera (T20, plan 0.10.5): przejście na najnowszą"
      echo "stabilną linię robi człowiek według jadro/README.md (porównanie konfiguracji, pełne testy)."; } > build/robot-pytanie.md
fi

# --- (b) the browser's packages ---
cp strefy/przegladarka.txt "$T/przegladarka.txt"
python3 -I scripts/odswiez-strefe.py >/dev/null
if diff <(grep -v '^#' "$T/przegladarka.txt") <(grep -v '^#' strefy/przegladarka.txt) >/dev/null 2>&1; then
    cp "$T/przegladarka.txt" strefy/przegladarka.txt # only the snapshot line moved: nothing new
else
    FF=$(awk '$1 == "firefox-esr" { print $2 }' strefy/przegladarka.txt)
    FF_STARY=$(awk '$1 == "firefox-esr" { print $2 }' "$T/przegladarka.txt")
    N=$(diff <(grep -v '^#' "$T/przegladarka.txt") <(grep -v '^#' strefy/przegladarka.txt) | grep -c '^>' || true)
    ZMIANY="${ZMIANY:+$ZMIANY-}przegladarka"
    if [ "$FF" != "$FF_STARY" ]; then
        OPISY="${OPISY:+$OPISY
}- **Przeglądarka: Firefox ESR $FF** i biblioteki z Debiana 13 — poprawki bezpieczeństwa (pakietów zmienionych: $N)."
    else
        OPISY="${OPISY:+$OPISY
}- **Przeglądarka:** biblioteki z Debiana 13 — poprawki bezpieczeństwa (pakietów zmienionych: $N)."
    fi
fi

# --- (c) a new PATCH version ---
if [ -z "$ZMIANY" ]; then
    wyjscie "co=$([ -n "$PYTANIE" ] && echo pytanie || echo nic)"
    exit 0
fi
W=$(cat VERSION)
NW=${W%.*}.$((${W##*.} + 1))
echo "$NW" > VERSION
DZIS=$(date -u +%Y-%m-%d)
python3 -I - CHANGELOG.md "$NW" "$DZIS" "$OPISY" <<'PY'
import sys
p, w, d, opisy = sys.argv[1:5]
s = open(p, encoding="utf-8").read()
i = s.index("\n## [")
wpis = (f"\n## [{w}] — {d}\n\n<!-- k1: poprawki -->\n**Poprawki** (robot, ADR 0047): nowe wersje tego, co K1 bierze z zewnątrz — bez zmian w kodzie K1.\n\n"
        f"### Zmienione\n{opisy}\n")
open(p, "w", encoding="utf-8").write(s[:i] + wpis + s[i:])
PY
wyjscie "co=$([ -n "$PYTANIE" ] && echo pytanie-i-zmiana || echo zmiana)"
wyjscie "galaz=robot/$ZMIANY-$NW"
wyjscie "tytul=Robot: $(echo "$OPISY" | sed -n 's/^- \*\*\([^*]*\)\*\*.*/\1/p' | paste -sd ';' | sed 's/;/; /g') — K1 $NW"
wyjscie "wersja=$NW"
