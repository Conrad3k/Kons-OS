#!/bin/sh
# Test 0.12.0 kroki 1–2 (ADR 0054, T7; powtarzalny): DŹWIĘK — karta Intel HD Audio w QEMU (ich9-intel-hda) z kodekiem
# hda-output, którego dźwięk QEMU zapisuje do pliku WAV (-audiodev wav). Obraz testowy z opisem ekranu (dysk z numerem
# „k1-test-opis-dzwieku”: opis dla skryptu i scenariusz dźwięku programu testowego — t_dzwiek_opis). Skrypt klika
# okienko „Dźwięk” po nazwach: próba przy 100 %, wyciszona próba, próba przy 50 %, potem wyciszenie i jego zdjęcie
# (program testowy zabija wtedy usługę dźwięku — nadzorca ją uruchamia, gui dostaje nowy kanał) i próba po restarcie.
# Krok 2: klawisze głośności (QEMU sendkey: ciszej ×2 → 40 %, pigułka ekranu), Ustawienia → Dźwięk (próba przy 40 %),
# wycisz i głośniej (45 %), drugie wyciszenie — program testowy zabija wtedy pulpit, nowy pulpit bierze głośność ze skarbca
# (45 %, bez wyciszenia, D5) i próba z Ustawień. Potem słucha pliku: dokładnie pięć dźwięków (wyciszona próba — cisza),
# w każdym nuty 660 i 880 Hz, 50 % ≈ 1/8 amplitudy 100 % (krzywa sześcienna), po restarcie tak samo jak przed, 40 % i 45 %
# w stosunku sześcianów do 50 %; węzły karty ma tylko usługa dźwięku.
# Komputer bez karty: test-m4 (okienko mówi „nie ma karty dźwiękowej”). Wymaga: scripts/zbuduj.sh.
. "$(dirname "$0")/wspolne.sh"
TESTY=$K1_BUDOWA/testy
CZAS=${K1_TEST_CZAS:-900}
WAV=$TESTY/dzwiek.wav
mkdir -p "$TESTY"
rm -f "$TESTY"/dzwiek-*.png "$TESTY/dzwiek.log" "$TESTY/dzwiek.sock" "$WAV"
r=0

cat > "$K1_BUDOWA/dzwiek.py" <<'PY'
import os, sys
sys.path.insert(0, os.environ["K1_TESTY_PY"])
from k1qemu import Maszyna
log, mon, out, limit = sys.argv[1], sys.argv[2], sys.argv[3], float(sys.argv[4])
q = Maszyna(log, mon, out, limit, "dzwiek")
OK = "Okienko paska"

if not q.sprawdz("pulpit gotowy, opis ekranu: „Dźwięk” na górnym pasku",
                 q.czekaj(r"K1-GUI: pulpit gotowy", czas=limit * 0.5) and
                 q.czekaj_na_element("Pasek górny", "Dźwięk", czas=120)):
    q.koniec()
q.sprawdz("nadzorca daje kartę usłudze dźwięku (węzeł odtwarzania i kontrolki)",
          q.czekaj(r"K1-NADZORCA: dźwięk: karta \d+, urządzenie \d+ dla usługi dźwięku", czas=60))
q.sprawdz("usługa dźwięku przyjmuje kartę i włącza wyjścia kodeka",
          q.czekaj(r"K1-DZWIEK: karta \d+, urządzenie \d+: odtwarzanie", czas=30) and
          q.czekaj(r"K1-DZWIEK: kontrolki karty: [1-9]\d* z \d+ ustawione", czas=30))
q.sprawdz("gui: kanał do usługi dźwięku, stan „karta jest”",
          q.czekaj(r"K1-GUI: dźwięk: kanał do usługi dźwięku", czas=30) and q.czekaj(r"K1-GUI: dźwięk: karta 2,", czas=30))

q.klik_w("Pasek górny", "Dźwięk")
q.sprawdz("okienko „Dźwięk”: wyciszenie, głośność, „Odtwórz próbę”",
          q.czekaj_na_element(OK, "Odtwórz próbę", czas=30) and q.element(OK, "Wycisz") and q.element(OK, "Głośność"))
q.zrzut("dzwiek-okienko", gdy=lambda w, h, px: True)
q.sprawdz("NEGATYWNY/węzły: /dev/snd ma tylko usługa dźwięku (program testowy patrzy na deskryptory)",
          q.czekaj(r"K1-TEST: dźwięk: węzły karty ma tylko usługa dźwięku", czas=30))


def glosnosc(proc):
    """A click on the slider at `proc` % (its ends: 0 and 100)."""
    e = q.element(OK, "Głośność")  # the element is the track itself (gui/dzwiek.c); a press 10 px past its end counts
    wzor = rf"K1-DZWIEK: głośność {proc}%(?!,)"
    n = q.ile(wzor)
    q.klik(e["x"] + e["w"] * proc // 100 + (4 if proc == 100 else 0), e["y"] + e["h"] // 2)
    return q.czekaj(wzor, od=n, czas=30)


def proba(opis):
    n = q.ile(r"K1-GUI: próba: zagrana")
    q.klik_w(OK, "Odtwórz próbę")
    return q.sprawdz(f"próba zagrana ({opis})", q.czekaj(r"K1-GUI: próba: zagrana", od=n, czas=120))


def wycisz(tak):
    q.klik_w(OK, "Wycisz")
    return q.czekaj_na(lambda: bool((q.element(OK, "Wycisz") or {}).get("stan", 0) & 1) == tak, czas=30)


q.sprawdz("suwak na końcu: głośność 100%", glosnosc(100))
proba("100 %")
q.sprawdz("„Wycisz”: wyciszone", wycisz(True) and q.czekaj(r"K1-DZWIEK: głośność 100%, wyciszone", czas=30))
proba("wyciszona — w pliku cisza")
q.sprawdz("„Wycisz” jeszcze raz: znów gra", wycisz(False))
q.sprawdz("suwak w połowie: głośność 50%", glosnosc(50))
proba("50 %")
n_start = q.ile(r"K1-NADZORCA: uruchomiono dzwiek")
n_kanal = q.ile(r"K1-GUI: dźwięk: kanał do usługi dźwięku")
n_klient = q.ile(r"K1-DZWIEK: klient pulpit: kanał")
wycisz(True)
wycisz(False)  # the test program kills the service now (the second unmute — t_dzwiek_opis)
q.sprawdz("usługa dźwięku zabita (program testowy, SIGKILL)", q.czekaj(r"K1-TEST: dźwięk: usługa dźwięku zabita", czas=30))
q.sprawdz("nadzorca uruchamia usługę dźwięku od nowa, daje jej kartę, gui dostaje nowy kanał",
          q.czekaj(r"K1-NADZORCA: uruchomiono dzwiek", od=n_start, czas=60) and
          q.czekaj(r"K1-GUI: dźwięk: kanał do usługi dźwięku", od=n_kanal, czas=60) and
          q.czekaj(r"K1-DZWIEK: klient pulpit: kanał", od=n_klient, czas=60))  # its volume: the WAV (the third sound)
n_cisza = q.ile(r"K1-DZWIEK: cisza: karta uśpiona")
proba("po restarcie usługi, 50 %")
q.sprawdz("5 s ciszy po próbie: karta uśpiona (T8)", q.czekaj(r"K1-DZWIEK: cisza: karta uśpiona", od=n_cisza, czas=60))

# ---- 0.12.0 step 2: the volume keys (D4: A — ekran's pill), Ustawienia → Dźwięk, the remembered volume (D5: A) ----
q.klik_w("Pasek górny", "Dźwięk")  # the popover closed: the keys go to the shell anyway
q.czekaj_na(lambda: q.element(OK) is None, czas=30)


def klawisz(nazwa, wzor):
    """A key of QEMU (sendkey: down and up) and the service's line it must bring."""
    n = q.ile(wzor)
    q.pisz(nazwa)
    return q.czekaj(wzor, od=n, czas=30)


q.sprawdz("klawisz „ciszej” dwa razy: 40 % (głośniki), pigułka głośności ekranu",
          klawisz("volumedown", r"K1-DZWIEK: głośność 45% \(Głośniki\)") and
          klawisz("volumedown", r"K1-DZWIEK: głośność 40% \(Głośniki\)") and
          q.czekaj(r"K1-EKRAN: pigułka głośności: 40%", czas=30) and
          q.czekaj_na_element("Pigułka głośności", "Głośność", czas=10))
q.zrzut("pigulka", gdy=lambda w, h, px: True)
q.sprawdz("pigułka znika sama (ok. 1,5 s)", q.czekaj_na(lambda: q.element("Pigułka głośności") is None, czas=30))
q.sprawdz("gui zapamiętuje głośność w skarbcu (D5)", q.czekaj(r"K1-GUI: dźwięk: głośność zapamiętana", czas=30))

q.klik_w("Pasek", "Ustawienia", "ikona")
q.sprawdz("Ustawienia → Dźwięk: wyjście „Głośniki”, wyciszenie, głośność, próba",
          q.czekaj_na_element("Ustawienia", "Dźwięk", "zakladka", czas=120) and
          (q.klik_w("Ustawienia", "Dźwięk", "zakladka") or True) and
          q.czekaj_na_element("Ustawienia", "Odtwórz próbę", czas=30) and q.element("Ustawienia", "Głośność") and
          q.czekaj(r"K1-USTAWIENIA: dźwięk: karta 2, wyjście 1, głośność 40%", czas=30))
q.zrzut("ustawienia", gdy=lambda w, h, px: True)
n = q.ile(r"K1-USTAWIENIA: próba: zagrana")
q.klik_w("Ustawienia", "Odtwórz próbę")
q.sprawdz("próba z Ustawień zagrana (40 %)", q.czekaj(r"K1-USTAWIENIA: próba: zagrana", od=n, czas=120))

n_zap = q.ile(r"K1-GUI: dźwięk: głośność zapamiętana")
q.sprawdz("klawisz „wycisz”: wyciszone; „głośniej”: 45 %, nie wyciszone; gui zapamiętuje",
          klawisz("audiomute", r"K1-DZWIEK: głośność 40%, wyciszone \(Głośniki\)") and
          klawisz("volumeup", r"K1-DZWIEK: głośność 45% \(Głośniki\)") and
          q.czekaj(r"K1-GUI: dźwięk: głośność zapamiętana", od=n_zap, czas=30))
n_gui = q.ile(r"K1-GUI: dźwięk: zapamiętana głośność — głośniki 45%")
n_45 = q.ile(r"K1-DZWIEK: głośność 45% \(Głośniki\)")
q.pisz("audiomute")  # the second muted pill: the test program kills gui before it writes the mute (t_dzwiek_opis)
q.sprawdz("pulpit zabity (program testowy, SIGKILL) i uruchomiony od nowa: głośność z skarbca (45 %, nie wyciszone)",
          q.czekaj(r"K1-TEST: dźwięk: pulpit zabity", czas=30) and
          q.czekaj(r"K1-GUI: dźwięk: zapamiętana głośność — głośniki 45%(?! \(wyc)", od=n_gui, czas=120) and
          q.czekaj(r"K1-DZWIEK: głośność 45% \(Głośniki\)", od=n_45, czas=60))
n = q.ile(r"K1-USTAWIENIA: próba: zagrana")
q.klik_w("Ustawienia", "Odtwórz próbę")
q.sprawdz("próba z Ustawień po nowym pulpicie (45 %)", q.czekaj(r"K1-USTAWIENIA: próba: zagrana", od=n, czas=120))
q.czysty_log()
q.koniec()
PY

info "test dźwięku: QEMU z kartą Intel HD Audio, dźwięk do pliku WAV"
ZNAK=$TESTY/dzwiek-znak.img
dd if=/dev/zero of="$ZNAK" bs=1M count=1 status=none
SPRZET="-drive if=none,id=znak,file=$ZNAK,format=raw,readonly=on -device virtio-blk-pci,drive=znak,serial=k1-test-opis-dzwieku,addr=0x1e"
SPRZET="$SPRZET -audiodev wav,id=dz,path=$WAV,out.frequency=48000,out.channels=2,out.format=s16"
SPRZET="$SPRZET -device ich9-intel-hda,id=hda -device hda-output,audiodev=dz,bus=hda.0"
"$K1_REPO/scripts/uruchom-qemu.sh" --log "$TESTY/dzwiek.log" --monitor "$TESTY/dzwiek.sock" --czas "$CZAS" \
    --obraz "$K1_WYNIK/rdzen-test.squashfs" --qemu "$SPRZET" &
QPID=$!
K1_TESTY_PY="$K1_REPO/testy/qemu" python3 -I "$K1_BUDOWA/dzwiek.py" "$TESTY/dzwiek.log" "$TESTY/dzwiek.sock" "$TESTY" "$CZAS" || r=1
zamknij_qemu "$TESTY/dzwiek.sock" $QPID

info "test dźwięku: plik WAV — co zagrała karta"
python3 -I "$K1_REPO/testy/qemu/k1dzwiek.py" "$WAV" || r=1
if [ $r -eq 0 ]; then
    info "dźwięk: OK"
else
    echo "[k1] dźwięk: BŁĄD — log $TESTY/dzwiek.log, plik $WAV"
fi
exit $r
