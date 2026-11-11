#!/bin/sh
# Test 0.8.1 krok 2 (ADR 0034, powtarzalny): skarbiec z trudnym dyskiem danych w działającym K1 (obraz produkcyjny).
#   1. „dziennik”: K1-DANE 190 MiB z długim dziennikiem (jeden obiekt zapisany 20 000 razy — 78 MiB starych wersji,
#      testy/host/magazyn_testowy.c) → skarbiec kompaktuje przy otwarciu, pulpit startuje; ponowny start — dziennik
#      krótki, bez kompaktowania
#   2. „pełny”: K1-DANE 64 MiB jak na pendrive'ach z 0.5.6–0.7.6, zajęta prawie do końca (6 MiB wolne) → Pliki: „Nowy
#      folder” odrzucony (brak miejsca na pliki, wynik −28), a ustawienie systemu (Ustawienia → Aplikacje) się zapisuje
# Dowody: build/testy/skarbiec-*.log, zrzuty build/testy/skarbiec-*.png. Wymaga: scripts/zbuduj.sh.
. "$(dirname "$0")/wspolne.sh"
TESTY=$K1_BUDOWA/testy
CZAS=${K1_TEST_CZAS:-240}
mkdir -p "$TESTY"; rm -f "$TESTY"/skarbiec-*
r=0

uruchom() {
    n=$1; shift
    rm -f "$TESTY/skarbiec-$n.log" "$TESTY/skarbiec-$n.sock"
    "$K1_REPO/scripts/uruchom-qemu.sh" --log "$TESTY/skarbiec-$n.log" --monitor "$TESTY/skarbiec-$n.sock" --czas "$CZAS" "$@" &
    QPID=$!
}
sprawdz() {
    K1_TESTY_PY="$K1_REPO/testy/qemu" python3 -I "$K1_BUDOWA/skarbiec.py" "$TESTY/skarbiec-$1.log" "$TESTY/skarbiec-$1.sock" \
        "$TESTY" "$CZAS" "$1" || r=1
    zamknij_qemu "$TESTY/skarbiec-$1.sock" $QPID
}
# dysk OBRAZ SEKTORY WERSJI ZOSTAW_MIB — a GPT disk with one K1-DANE partition holding magazyn_testowy's vault
dysk() {
    rm -f "$1" "$1.czesc"
    truncate -s $(( ($2 + 4096) * 512 )) "$1"
    printf 'label: gpt\nstart=2048, size=%s, name="K1-DANE"\n' "$2" | sfdisk -q "$1" >/dev/null
    truncate -s $(( $2 * 512 )) "$1.czesc"
    wynik=$("$K1_BUDOWA/obj/magazyn_testowy" "$1.czesc" "$3" "$4") || r=1
    echo "  $wynik"
    dd if="$1.czesc" of="$1" bs=512 seek=2048 conv=notrunc,sparse status=none; rm -f "$1.czesc"
}

cat > "$K1_BUDOWA/skarbiec.py" <<'PY'
import os, re, sys
sys.path.insert(0, os.environ["K1_TESTY_PY"])
from k1qemu import Maszyna
log, mon, out, limit, tryb = sys.argv[1], sys.argv[2], sys.argv[3], float(sys.argv[4]), sys.argv[5]
q = Maszyna(log, mon, out, limit, "skarbiec")
tekst, czekaj, zrzut, sprawdz, klik = q.tekst, q.czekaj, q.zrzut, q.sprawdz, q.klik
KOMP = r"K1-SKARBIEC: kompaktowanie \(przy otwarciu\): dziennik (\d+) → (\d+) MiB"


def po(wzor, akcja, czas=30):
    n = q.ile(wzor)
    akcja()
    return czekaj(wzor, od=n, czas=czas)


def okno(nazwa, akcja):
    m = po(r"K1-EKRAN: okno (\d+) \(" + nazwa + r"\) (\d+)x(\d+) w (\d+),(\d+)", akcja)
    return (int(m.group(4)), int(m.group(5))) if m else None


g = czekaj(r"K1-GUI: pulpit gotowy: Start (\d+),(\d+), aplikacje od (\d+),(\d+) co (\d+)", czas=limit * 0.7)
k0 = czekaj(r"K1-EKRAN: kursor (\d+),(\d+) ")
if not sprawdz(f"{tryb}: pulpit gotowy", g and k0): sys.exit(1)
q.kursor[:] = [int(k0.group(1)), int(k0.group(2))]
ax, ay, krok = int(g.group(3)), int(g.group(4)), int(g.group(5))
pasek = lambda i: (ax + i * krok, ay)  # 0 Zegar, 1 Notatki, 2 Ustawienia, 3 Zasoby, 4 Kalkulator, 5 Pliki

if tryb == "dziennik":
    m = czekaj(KOMP)
    sprawdz("długi dziennik (78 MiB starych wersji): skarbiec kompaktuje przy otwarciu",
            m and int(m.group(1)) >= 78 and int(m.group(2)) <= 1, m.group(0) if m else "brak")
elif tryb == "dziennik-ponownie":
    sprawdz("ponownie: dziennik już krótki — bez kompaktowania, magazyn otwarty",
            czekaj(r"K1-SKARBIEC: magazyn otwarty") and not re.search(KOMP, tekst()))
else:  # pelny
    p0 = okno("pliki", lambda: klik(*pasek(5)))
    u = czekaj(r"K1-PLIKI: układ: miejsca od (\d+),(\d+) co (\d+), Kosz")
    if not sprawdz("Pliki otwarte", p0 and u and czekaj(r"K1-PLIKI: pliki wczytane")): sys.exit(1)
    mx, my, mk = (int(u.group(i)) for i in range(1, 4))
    po(r"K1-PLIKI: widok: Dokumenty", lambda: klik(p0[0] + mx, p0[1] + my + mk))
    pm = list(re.finditer(r"K1-PLIKI: przyciski: (.*)", tekst()))
    p = {t.strip(): (p0[0] + int(x), p0[1] + int(y)) for t, x, y in re.findall(r"([^;]+?) (\d+),(\d+)", pm[-1].group(1))} if pm else {}
    sprawdz("prawie pełny skarbiec: „Nowy folder” odrzucony — brak miejsca na pliki (−28)",
            "Nowy folder" in p and po(r"K1-PLIKI: skarbiec odmówił \(komunikat 86, wynik -28\)", lambda: klik(*p["Nowy folder"])),
            str(p))
    zrzut("pelny-1-brak-miejsca", gdy=lambda w, h, px: True)
    u0 = okno("ustawienia", lambda: klik(*pasek(2)))
    sk = czekaj(r"K1-USTAWIENIA: układ: sekcje od (\d+),(\d+) co (\d+)", czas=30)
    ap = u0 and sk and po(r"K1-USTAWIENIA: aplikacje: Dokument tekstowy — 2 do wyboru, przyciski od (\d+),(\d+) \(szer\. (\d+), wys\. (\d+)\)",
                          lambda: klik(u0[0] + int(sk.group(1)), u0[1] + int(sk.group(2)) + 6 * int(sk.group(3))))
    if ap:
        bx, by, bw, bh = (int(ap.group(i)) for i in range(1, 5))
        sprawdz("…a ustawienie systemu się zapisuje (rezerwa na system)",
                po(r"K1-SKARBIEC: zapisano obiekt 1 właściciela 2 ", lambda: klik(u0[0] + bx + 3 * bw // 4, u0[1] + by + bh // 2)))
    else:
        sprawdz("Ustawienia → Aplikacje", False)
t_ = tekst()
sprawdz("brak 'Kernel panic', błędów, zablokowanych wywołań i odrzuconych komunikatów", "Kernel panic" not in t_ and
        "BŁĄD" not in t_ and "zabronione wywołanie" not in t_ and "odrzucono" not in t_)
sys.exit(0 if all(q.wyniki) else 1)
PY

echo "[k1] test skarbca (0.8.1 krok 2): długi dziennik, prawie pełny dysk danych"
[ -x "$K1_BUDOWA/obj/magazyn_testowy" ] || make -C "$K1_REPO" -s "$K1_BUDOWA/obj/magazyn_testowy" >/dev/null
DZ=$TESTY/skarbiec-dziennik.img
dysk "$DZ" 389120 20000 0
uruchom dziennik --qemu "-drive if=none,id=dane,file=$DZ,format=raw -device ide-hd,drive=dane,bus=ide.0"
sprawdz dziennik
uruchom dziennik-ponownie --qemu "-drive if=none,id=dane,file=$DZ,format=raw -device ide-hd,drive=dane,bus=ide.0"
sprawdz dziennik-ponownie
PE=$TESTY/skarbiec-pelny.img
dysk "$PE" 131072 0 6
uruchom pelny --qemu "-drive if=none,id=dane,file=$PE,format=raw -device ide-hd,drive=dane,bus=ide.0"
sprawdz pelny
rm -f "$DZ" "$PE"
if [ $r -eq 0 ]; then info "skarbiec: wszystko zaliczone"; else echo "[k1] skarbiec: BŁĄD (logi: $TESTY/skarbiec-*.log)"; fi
exit $r
