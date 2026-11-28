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
# Krok 3: zamknięcie Ustawień — program testowy uruchamia strefę próbną z tonem 480 Hz (karta virtio-snd VMM-a → usługa
# dźwięku): głośnik na pasku okna sesji i „Grają teraz” w okienku, kliknięcie głośnika — sesja wyciszona (4 s ciszy), drugie
# — znów gra; potem gość złośliwy dla karty (VMM kończy jego maszynę). W pliku po pięciu próbach — ton 480 Hz z przerwą.
# 0.12.3 krok 1 (ADR 0055): usługa liczy w liczbach zmiennoprzecinkowych i wybiera najlepszy format karty (karta QEMU
# przyjmuje tylko 16 bitów — log); pierwsza próba (100 %) jest w pliku próbka w próbkę taka jak w programie
# („przezroczystość”, k1dzwiek.py); strefa próbna gra ton w 32 bitach — VMM przelicza je do pierścienia
# zmiennoprzecinkowego.
# 0.12.3 krok 2 (korektor, D1: A, D2: A): Ustawienia → Dźwięk → Korektor — dziesięć pasm, pasmo 1 kHz na +12 dB (usługa:
# „Własne”, pulpit zapamiętuje), szósta próba z korektorem (w pliku: nuta 660 Hz o 7,0 dB, 880 Hz o 1,7 dB ciszej niż
# piąta — krzywa liczona w k1dzwiek.py jak w k1_korektor.c), wiersz „Korektor” w okienku „Dźwięk” otwiera Ustawienia na
# korektorze, „Płaski” — z powrotem płaski.
# 0.12.3 krok 3 (Kons Spatial Audio, D3: A, D5, D7: A): Ustawienia → Dźwięk → Kons Spatial Audio — przełącznik i gotowe
# sceny po nazwach; „Kino” na głośnikach (usługa gra, pulpit zapamiętuje), potem program testowy stawia wszystkie pasma
# z lewej (Ustawienia pokazują „Własna”), siódma próba — w pliku lewy kanał co najmniej 20 dB głośniej niż prawy i tak
# głośny jak piąta próba (scena nie podnosi poziomu).
# 0.12.3 krok 4 (scena do przeciągania, D4: A, D6: A): punkty pasm, lista pasm i „Posłuchaj” po nazwach; „Posłuchaj” —
# szum po kolei w czterech pasmach (w pliku: bas w obu kanałach równo, pozostałe pasma z lewej, każde wyżej od
# poprzedniego); przeciągnięcie punktu „Środek” robi „Własną” (usługa ją trzyma, pulpit zapamiętuje), „Kino” i z powrotem
# „Własna” — punkt wraca tam, gdzie go postawiono; okienko „Dźwięk”: przełącznik Kons Spatial Audio — wyłączony, wiersz
# „Scena” otwiera Ustawienia na scenie.
# 0.13.0 (zgłoszenie właściciela: „cały dźwięk dużo cichszy, spłaszczony”; ADR 0055 punkt 21): Kons Spatial Audio
# domyślnie włączony („Koncert”, bas na 4 %) — test wyłącza go przełącznikiem okienka przed pierwszą próbą; korektor i scena
# zachowują głośność (szósta próba: nuty w proporcji nowego poziomu; siódma: moc obu kanałów jak w piątej, ±1,5 dB).
# 0.13.5 krok 1 (mikrofon, ADR 0065; D2: A, D3: A): drugi kodek QEMU z wejściem (hda-micro, nagrywa ciszę), program testowy
# daje do mikrofonu ton (−12 dB); Ustawienia → Dźwięk → „Powiedz coś”: usługa nagrywa, pasek poziomu rośnie, znak
# „mikrofon włączony” na górnym pasku i jego okienko („Wycisz mikrofon”), Ctrl+Alt+Del — cisza dla słuchaczy (T4),
# „Zatrzymaj” — karta nie nagrywa, znak znika. Krok 2 (mikrofon w przeglądarce, D1: A): strefa próbna jak sesja
# z przełącznikiem „Mikrofon” — karta virtio-snd gościa ma wejście, gość słyszy ton testowy, znak na pasku okna sesji
# (kliknięcie: mikrofon sesji wyciszony — gość słyszy ciszę, drugie — znów ton); NEGATYWNY: strefa bez przełącznika —
# karta gościa bez wejścia.
# Komputer bez karty: test-m4 (okienko mówi „nie ma karty dźwiękowej”). Wymaga: scripts/zbuduj.sh.
. "$(dirname "$0")/wspolne.sh"
TESTY=$K1_BUDOWA/testy
CZAS=${K1_TEST_CZAS:-1500}
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
# 0.13.0 (the owner's choice): Kons Spatial Audio on from the start — Koncert, its bass at 4 %; the samples below are
# compared sample for sample with the program's, so the switch turns it off first (the steps 3–4 below switch it on)
q.sprawdz("0.13.0: Kons Spatial Audio domyślnie włączony (przełącznik w okienku)",
          q.czekaj_na(lambda: (q.element(OK, "Kons Spatial Audio") or {}).get("stan", 0) & 1, czas=30))
n_sc = q.ile(r"K1-DZWIEK: scena \(Głośniki\): Wyłączony")
q.klik_w(OK, "Kons Spatial Audio")
q.sprawdz("przełącznik w okienku: scena głośników wyłączona (próby niżej bez sceny)",
          q.czekaj(r"K1-DZWIEK: scena \(Głośniki\): Wyłączony", od=n_sc, czas=30) and
          q.czekaj_na(lambda: not (q.element(OK, "Kons Spatial Audio") or {}).get("stan", 1) & 1, czas=30))
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
q.sprawdz("0.12.3: karta QEMU przyjmuje tylko 16 bitów — usługa wybiera je sama (24 i 20 bitów odmówione)",
          q.czekaj(r"K1-DZWIEK: karta gra 16 bitów \(2 bajty na próbkę\)", czas=30))
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
PIG_WYC = lambda: bool((q.element("Pigułka głośności", "Głośność") or {}).get("stan", 0) & 1)
q.sprawdz("klawisz „wycisz”: wyciszone; „głośniej”: 45 %, nie wyciszone; gui zapamiętuje",
          klawisz("audiomute", r"K1-DZWIEK: głośność 40%, wyciszone \(Głośniki\)") and
          # the test program counts the muted pills (the second one is its cue): it must see this one too — with KVM
          # the next key came before the description showed it (CI run 352)
          q.czekaj_na(PIG_WYC, czas=30) and
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

# ---- 0.13.5 step 1 (ADR 0065; D2: A, D3: A): the microphone — the second codec (hda-micro) records silence, the test
# program puts its tone into the microphone (t_mikrofon_opis); Ustawienia's "Powiedz coś" hears the level only ----
q.sprawdz("nadzorca daje usłudze węzeł nagrywania tej samej karty; usługa go przyjmuje (nie nagrywa, póki nikt nie słucha)",
          q.czekaj(r"K1-NADZORCA: dźwięk: mikrofon — karta 0, urządzenie \d+", czas=30) and
          q.czekaj(r"K1-DZWIEK: karta 0, urządzenie \d+: nagrywanie", czas=30) and
          q.ile(r"K1-DZWIEK: mikrofon nagrywa") == 0)
q.sprawdz("Ustawienia → Dźwięk: część „Mikrofon” — źródło, wyciszenie, poziom, „Powiedz coś”; ton testowy w mikrofonie",
          q.czekaj_na_element("Ustawienia", "Powiedz coś", czas=30) and q.element("Ustawienia", "Wycisz mikrofon") and
          q.element("Ustawienia", "Poziom mikrofonu") and q.czekaj(r"K1-USTAWIENIA: dźwięk: .*mikrofon: (Wbudowany|W słuchawkach)", czas=5) and
          q.czekaj(r"K1-TEST: mikrofon: ton testowy włączony", czas=30))
q.sprawdz("NEGATYWNY: nikt nie słucha — brak znaku mikrofonu na pasku", q.element("Pasek górny", "Mikrofon") is None)
q.klik_w("Ustawienia", "Powiedz coś")
q.sprawdz("„Powiedz coś”: usługa nagrywa, Ustawienia słyszą tylko poziom — pasek rośnie ponad połowę (ton −12 dB)",
          q.czekaj(r"K1-DZWIEK: mikrofon: słucha Ustawienia \(poziom\)", czas=30) and
          q.czekaj(r"K1-DZWIEK: mikrofon nagrywa", czas=30) and
          q.czekaj(r"K1-USTAWIENIA: mikrofon: słychać \(poziom ponad połowę\)", czas=60))
q.sprawdz("znak „mikrofon włączony” na górnym pasku (pulpit wie od usługi dźwięku)",
          q.czekaj(r"K1-GUI: mikrofon: znak pokazany", czas=30) and q.czekaj_na_element("Pasek górny", "Mikrofon", czas=30))
q.zrzut("mikrofon-ustawienia", gdy=lambda w, h, px: True)
q.klik_w("Pasek górny", "Mikrofon")
q.sprawdz("okienko „Mikrofon”: kto słucha, „Wycisz mikrofon”", q.czekaj_na_element(OK, "Wycisz mikrofon", czas=30))
q.zrzut("mikrofon-okienko", gdy=lambda w, h, px: True)
q.klik_w(OK, "Wycisz mikrofon")
q.sprawdz("„Wycisz mikrofon” w okienku: usługa daje ciszę, znak przekreślony, Ustawienia to widzą",
          q.czekaj(r"K1-DZWIEK: mikrofon: poziom 50%, wyciszony", czas=30) and
          q.czekaj_na(lambda: (q.element("Ustawienia", "Wycisz mikrofon") or {}).get("stan", 0) & 1, czas=30))
q.klik_w(OK, "Wycisz mikrofon")
q.sprawdz("drugie kliknięcie: znów słychać", q.czekaj(r"K1-DZWIEK: mikrofon: poziom 50%(?!,)", czas=30))
q.klik_w("Pasek górny", "Mikrofon")
q.czekaj_na(lambda: q.element(OK) is None, czas=30)
n_wst = q.ile(r"K1-DZWIEK: mikrofon: wstrzymany")
q.monitor("sendkey ctrl-alt-delete")
q.sprawdz("T4: ekran zabezpieczeń na wierzchu — usługa daje słuchaczom ciszę; po Esc znów słychać",
          q.czekaj(r"K1-DZWIEK: mikrofon: wstrzymany \(ekran zabezpieczeń\)", od=n_wst, czas=30) and
          (q.pisz("esc") or True) and q.czekaj(r"K1-DZWIEK: mikrofon: znów słychać", czas=30))
q.klik_w("Ustawienia", "Zatrzymaj")
q.sprawdz("„Zatrzymaj”: nikt nie słucha — karta nie nagrywa, znak znika",
          q.czekaj(r"K1-USTAWIENIA: mikrofon: próba zakończona, najgłośniej (79|[89]\d)%", czas=30) and
          q.czekaj(r"K1-DZWIEK: mikrofon nie nagrywa", czas=30) and q.czekaj(r"K1-GUI: mikrofon: znak zniknął", czas=30) and
          q.czekaj_na(lambda: q.element("Pasek górny", "Mikrofon") is None, czas=30))

# ---- 0.12.3 step 2: the equalizer (D2: A — ten bands in Ustawienia → Dźwięk → Korektor; D1: A — the popover's row) ----
q.klik_w("Ustawienia", "Korektor", "zakladka")
q.sprawdz("Ustawienia → Dźwięk → Korektor: gotowe ustawienia i dziesięć pasm (głośniki, płaski — nowy pulpit dał "
          "zapamiętany)", q.czekaj_na_element("Ustawienia", "Pasmo 1k", czas=30) and
          q.element("Ustawienia", "Więcej basu") and q.element("Ustawienia", "Pasmo 16k") and
          q.czekaj(r"K1-USTAWIENIA: korektor \(Głośniki\): Płaski", czas=30))
q.zrzut("korektor", gdy=lambda w, h, px: True)
n_kor, n_zap = q.ile(r"K1-DZWIEK: korektor \(Głośniki\): Własne"), q.ile(r"K1-GUI: dźwięk: głośność zapamiętana")
e = q.element("Ustawienia", "Pasmo 1k")
q.klik(e["x"] + e["w"] // 2, e["y"] + 10)  # the top of its track (the element reaches 10 px past it): +12 dB
q.sprawdz("pasmo 1k na +12 dB: usługa gra korektor „Własne”, pulpit go zapamiętuje w skarbcu (K1G2)",
          q.czekaj(r"K1-DZWIEK: korektor \(Głośniki\): Własne", od=n_kor, czas=30) and
          q.czekaj(r"K1-USTAWIENIA: korektor \(Głośniki\): Własne", czas=30) and
          q.czekaj(r"K1-GUI: dźwięk: głośność zapamiętana", od=n_zap, czas=30))
q.zrzut("korektor-1k", gdy=lambda w, h, px: True)
q.klik_w("Ustawienia", "Głośność", "zakladka")
n = q.ile(r"K1-USTAWIENIA: próba: zagrana")
q.klik_w("Ustawienia", "Odtwórz próbę")
q.sprawdz("próba z korektorem (45 %, 1 kHz +12 dB) zagrana — w pliku nuty w proporcji krzywej korektora",
          q.czekaj(r"K1-USTAWIENIA: próba: zagrana", od=n, czas=120))
q.klik_w("Pasek górny", "Dźwięk")
q.sprawdz("okienko „Dźwięk”: wiersz „Korektor” (D1: A)", q.czekaj_na_element(OK, "Korektor", czas=30))
q.zrzut("okienko-korektor", gdy=lambda w, h, px: True)
q.klik_w(OK, "Korektor")
q.sprawdz("wiersz „Korektor” otwiera Ustawienia na korektorze (nadzorca), okienko się zamyka",
          q.czekaj(r"K1-GUI: okienko dźwięku: Korektor", czas=30) and
          q.czekaj(r"K1-NADZORCA: otwieram Ustawienia → Dźwięk → Korektor", czas=30) and
          q.czekaj_na(lambda: q.element(OK) is None, czas=30) and
          q.czekaj_na_element("Ustawienia", "Pasmo 1k", czas=30))
n_kor = q.ile(r"K1-DZWIEK: korektor \(Głośniki\): Płaski")
q.klik_w("Ustawienia", "Płaski")
q.sprawdz("„Płaski”: korektor głośników znów płaski", q.czekaj(r"K1-DZWIEK: korektor \(Głośniki\): Płaski", od=n_kor, czas=30))

# ---- 0.12.3 step 3: Kons Spatial Audio (D3: A — on the speakers a wider stereo; D5: the ready scenes; D7: A) ----
q.klik_w("Ustawienia", "Kons Spatial Audio", "zakladka")
q.sprawdz("Ustawienia → Dźwięk → Kons Spatial Audio: przełącznik i gotowe sceny (głośniki, wyłączony)",
          q.czekaj_na_element("Ustawienia", "Kino", czas=30) and q.element("Ustawienia", "Naturalny") and
          q.element("Ustawienia", "Kons Spatial Audio", "przycisk") and
          q.czekaj(r"K1-USTAWIENIA: scena \(Głośniki\): Wyłączony", czas=30))
q.zrzut("przestrzen", gdy=lambda w, h, px: True)
n_sc, n_zap = q.ile(r"K1-DZWIEK: scena \(Głośniki\): Kino"), q.ile(r"K1-GUI: dźwięk: głośność zapamiętana")
q.klik_w("Ustawienia", "Kino")
q.sprawdz("„Kino”: usługa gra scenę na głośnikach, pulpit ją zapamiętuje w skarbcu (K1G4)",
          q.czekaj(r"K1-DZWIEK: scena \(Głośniki\): Kino", od=n_sc, czas=30) and
          q.czekaj(r"K1-GUI: dźwięk: głośność zapamiętana", od=n_zap, czas=30))
q.sprawdz("program testowy stawia wszystkie pasma z lewej (klient sterujący): usługa gra „Własna”, Ustawienia ją pokazują",
          q.czekaj(r"K1-TEST: dźwięk: scena z lewej — ustawiona", czas=30) and
          q.czekaj(r"K1-DZWIEK: scena \(Głośniki\): Własna", czas=30) and
          q.czekaj_na(lambda: (q.element("Ustawienia", "Własna") or {}).get("stan", 0) & 1, czas=30))
q.zrzut("przestrzen-wlasna", gdy=lambda w, h, px: True)
q.klik_w("Ustawienia", "Głośność", "zakladka")
n = q.ile(r"K1-USTAWIENIA: próba: zagrana")
q.klik_w("Ustawienia", "Odtwórz próbę")
q.sprawdz("próba ze sceną z lewej (45 %) zagrana — w pliku lewy kanał głośniej",
          q.czekaj(r"K1-USTAWIENIA: próba: zagrana", od=n, czas=120))
q.klik_w("Ustawienia", "Kons Spatial Audio", "zakladka")

# ---- 0.12.3 step 4: the scene to drag (D4: A), "Własna" (D6: A), "Posłuchaj", the popover's row ----
q.sprawdz("scena: punkty pasm, lista pasm i „Posłuchaj” po nazwach (opis ekranu)",
          q.czekaj_na_element("Ustawienia", "Środek", czas=30) and q.element("Ustawienia", "Góra") and
          q.element("Ustawienia", "Bas") and q.element("Ustawienia", "pasmo Niski środek") and
          q.element("Ustawienia", "Posłuchaj"))
n = q.ile(r"K1-USTAWIENIA: posłuchaj: zagrana")
q.klik_w("Ustawienia", "Posłuchaj")
q.sprawdz("„Posłuchaj”: szum po kolei w pasmach przez scenę (Ustawienia pokazują pasmo, które gra) — w pliku ósmy dźwięk",
          q.czekaj(r"K1-USTAWIENIA: posłuchaj: pasmo Bas", czas=60) and
          q.czekaj(r"K1-USTAWIENIA: posłuchaj: pasmo Góra", czas=120) and
          q.czekaj(r"K1-USTAWIENIA: posłuchaj: zagrana", od=n, czas=120))
bas, sr = q.element("Ustawienia", "Bas"), q.element("Ustawienia", "Środek")
cx, cy = bas["x"] + bas["w"] // 2, bas["y"] + 3 + bas["w"] // 2  # the head: the bass's ring is around it
n_wl, n_zap = q.ile(r"K1-USTAWIENIA: scena \(Głośniki\): Własna"), q.ile(r"K1-GUI: dźwięk: głośność zapamiętana")
q.przeciagnij(sr["x"] + sr["w"] // 2, sr["y"] + sr["h"] // 2, cx + 80, cy - 40, krokow=8)
q.sprawdz("przeciągnięty punkt „Środek”: przed głową z prawej, scena „Własna” (Ustawienia, usługa), pulpit zapamiętuje (K1G4)",
          q.czekaj(r"K1-USTAWIENIA: scena \(Głośniki\): Własna", od=n_wl, czas=30) and
          q.czekaj(r"K1-GUI: dźwięk: głośność zapamiętana", od=n_zap, czas=30) and
          q.czekaj_na(lambda: (q.element("Ustawienia", "Środek") or {"x": 0})["x"] > cx + 40, czas=30))
po = q.element("Ustawienia", "Środek")
n_sc = q.ile(r"K1-DZWIEK: scena \(Głośniki\): Kino")
q.klik_w("Ustawienia", "Kino")
q.sprawdz("„Kino”: gotowa scena (punkt „Środek” wraca przed głowę)",
          q.czekaj(r"K1-DZWIEK: scena \(Głośniki\): Kino", od=n_sc, czas=30) and
          q.czekaj_na(lambda: abs((q.element("Ustawienia", "Środek") or {"x": 0})["x"] - po["x"]) > 20, czas=30))
n_sc = q.ile(r"K1-DZWIEK: scena \(Głośniki\): Własna")
q.klik_w("Ustawienia", "Własna")
q.sprawdz("D6: „Własna” wraca — punkt „Środek” tam, gdzie go postawiono",
          q.czekaj(r"K1-DZWIEK: scena \(Głośniki\): Własna", od=n_sc, czas=30) and
          q.czekaj_na(lambda: (q.element("Ustawienia", "Środek") or {"x": 0})["x"] == po["x"] and
                      q.element("Ustawienia", "Środek")["y"] == po["y"], czas=30))
q.zrzut("przestrzen-scena", gdy=lambda w, h, px: True)
q.klik_w("Ustawienia", "Głośność", "zakladka")
q.czekaj_na(lambda: q.element("Ustawienia", "Posłuchaj") is None, czas=30)
q.klik_w("Pasek górny", "Dźwięk")
q.sprawdz("okienko „Dźwięk”: wiersz Kons Spatial Audio (przełącznik włączony, „Scena”)",
          q.czekaj_na_element(OK, "Scena", czas=30) and
          q.czekaj_na(lambda: (q.element(OK, "Kons Spatial Audio") or {}).get("stan", 0) & 1, czas=30))
n_sc = q.ile(r"K1-DZWIEK: scena \(Głośniki\): Wyłączony")
q.klik_w(OK, "Kons Spatial Audio")
q.sprawdz("przełącznik Kons Spatial Audio w okienku: scena głośników wyłączona",
          q.czekaj(r"K1-GUI: okienko dźwięku: Kons Spatial Audio wyłączony \(Głośniki\)", czas=30) and
          q.czekaj(r"K1-DZWIEK: scena \(Głośniki\): Wyłączony", od=n_sc, czas=30) and
          q.czekaj_na(lambda: not (q.element(OK, "Kons Spatial Audio") or {}).get("stan", 1) & 1, czas=30))
q.klik_w(OK, "Scena")
q.sprawdz("wiersz „Scena” okienka: Ustawienia na zakładce Kons Spatial Audio, przełącznik wyłączony",
          q.czekaj(r"K1-GUI: okienko dźwięku: Kons Spatial Audio(?! )", czas=30) and
          q.czekaj_na_element("Ustawienia", "Posłuchaj", czas=30) and
          not (q.element("Ustawienia", "Kons Spatial Audio", "przycisk") or {}).get("stan", 1) & 1)

# ---- 0.12.0 step 3: a zone plays (D6: A — the speaker on its bar, the list "Grają teraz"); the hostile one ----
q.klik_w("Ustawienia", "Zamknij", "przycisk")  # the cue: the test program starts the test zone with a tone
SZ = "Strefa próbna"
q.sprawdz("strefa próbna gra ton przez kartę virtio-snd swojego VMM-a (kanał do usługi dźwięku od nadzorcy)",
          q.czekaj(r"K1-TEST: dźwięk: strefa próbna gra ton — uruchomiona", czas=120) and
          q.czekaj(r"K1-STREFA: dźwięk: kanał do usługi dźwięku", czas=240) and
          q.czekaj(r"K1-GOSC: dźwięk: gram ton 480 Hz", czas=240) and
          q.czekaj(r"K1-DZWIEK: klient strefa: strumień otwarty \(zmiennoprzecinkowy\)", czas=120))
q.sprawdz("0.12.3: gość gra w 32 bitach (S32) — karta virtio-snd je przyjmuje, VMM daje usłudze liczby zmiennoprzecinkowe",
          q.czekaj(r"K1-STREFA: dźwięk: gość gra w formacie S32", czas=60))
q.sprawdz("sesja gra: głośnik na pasku okna strefy (opis ekranu: „Wycisz sesję”), ekran wie od pulpitu",
          q.czekaj(r"K1-EKRAN: strefa 1: gra(?!,)", czas=120) and q.czekaj_na_element(SZ, "Wycisz sesję", czas=60))
q.zrzut("strefa-gra", gdy=lambda w, h, px: True)
q.klik_w("Pasek górny", "Dźwięk")
q.sprawdz("okienko „Dźwięk”: „Grają teraz” z sesją (wycisz, głośność sesji)",
          q.czekaj_na_element(OK, "Głośność sesji 1", czas=30) and q.element(OK, "Wycisz sesję 1"))
q.zrzut("grajace-sesje", gdy=lambda w, h, px: True)
q.klik_w("Pasek górny", "Dźwięk")
q.czekaj_na(lambda: q.element(OK) is None, czas=30)
q.klik_w(SZ, "Wycisz sesję")
q.sprawdz("głośnik na pasku: sesja wyciszona (usługa dźwięku, nie strefa), znak przekreślony",
          q.czekaj(r"K1-DZWIEK: sesja 1: głośność 100%, wyciszona", czas=60) and
          q.czekaj(r"K1-EKRAN: strefa 1: gra, wyciszona", czas=60) and
          q.czekaj_na(lambda: (q.element(SZ, "Wycisz sesję") or {}).get("stan", 0) & 1, czas=30))
q.cisza(r"K1-DZWIEK: sesja 1: głośność 100%(?!,)", 4)  # four seconds of the muted session: silence in the WAV
q.klik_w(SZ, "Wycisz sesję")
q.sprawdz("drugie kliknięcie: sesja znów gra", q.czekaj(r"K1-DZWIEK: sesja 1: głośność 100%(?!,)", czas=60))
q.sprawdz("strefa kończy ton i maszynę; sesja przestaje grać — znak znika",
          q.czekaj(r"K1-GOSC: dźwięk: koniec", czas=240) and q.czekaj(r"K1-EKRAN: strefa 1: nie gra", czas=120))
q.sprawdz("NEGATYWNY: gość złośliwy dla karty — format, którego K1 nie gra: „nieobsługiwane”, maszyna żyje; prośba bez "
          "miejsca na odpowiedź — VMM kończy tylko tę maszynę",
          q.czekaj(r"K1-TEST: dźwięk: strefa złośliwa dla karty — uruchomiona", czas=240) and
          q.czekaj(r"K1-GOSC: tryb 22: dźwięk — format, którego K1 nie gra: odpowiedź 0x8002", czas=240) and
          q.czekaj(r"K1-STREFA: strefa 1: koniec — dźwięk: prośba bez miejsca na odpowiedź", czas=120))

# ---- 0.13.5 step 2 (ADR 0065; D1: A): the microphone in a zone — the test zone as a session with the switch "Mikrofon" ----
q.sprawdz("strefa próbna z mikrofonem: VMM dostaje zgodę, karta gościa ma wejście, usługa daje mu mikrofon (ton testowy)",
          q.czekaj(r"K1-TEST: mikrofon: strefa próbna słucha — uruchomiona", czas=240) and
          q.czekaj(r"K1-NADZORCA: strefa 1 \(.*\): .*, mikrofon, gość testowy", czas=60) and
          q.czekaj(r"K1-DZWIEK: mikrofon: słucha strefa \(próbki\)", czas=240) and
          q.czekaj(r"K1-GOSC: mikrofon: ton", czas=240))
q.sprawdz("znak mikrofonu na pasku okna sesji (ekran od pulpitu) i na górnym pasku",
          q.czekaj(r"K1-EKRAN: strefa 1: mikrofon słucha(?!,)", czas=60) and
          q.czekaj_na_element(SZ, "Wycisz mikrofon sesji", czas=60) and q.element("Pasek górny", "Mikrofon") is not None)
q.zrzut("strefa-mikrofon", gdy=lambda w, h, px: True)
n_ton = q.ile(r"K1-GOSC: mikrofon: ton")
q.klik_w(SZ, "Wycisz mikrofon sesji")
q.sprawdz("znak na pasku okna: mikrofon tej sesji wyciszony w usłudze dźwięku — gość słyszy ciszę",
          q.czekaj(r"K1-DZWIEK: mikrofon: sesja 1 wyciszona", czas=60) and
          q.czekaj(r"K1-GOSC: mikrofon: cisza", czas=120) and
          q.czekaj_na(lambda: (q.element(SZ, "Wycisz mikrofon sesji") or {}).get("stan", 0) & 1, czas=30))
q.klik_w(SZ, "Wycisz mikrofon sesji")
q.sprawdz("drugie kliknięcie: gość znów słyszy ton",
          q.czekaj(r"K1-DZWIEK: mikrofon: sesja 1 słychać", czas=60) and
          q.czekaj(r"K1-GOSC: mikrofon: ton", od=n_ton, czas=120))
q.sprawdz("strefa kończy nagrywanie i maszynę; nikt nie słucha — znak znika",
          q.czekaj(r"K1-GOSC: mikrofon: koniec", czas=240) and q.czekaj(r"K1-DZWIEK: mikrofon: strefa nie słucha", czas=120) and
          q.czekaj(r"K1-GUI: mikrofon: znak zniknął", czas=120))
n_sl = q.ile(r"K1-DZWIEK: mikrofon: słucha strefa")
q.sprawdz("NEGATYWNY: strefa bez przełącznika „Mikrofon” — karta gościa bez wejścia, nikt nie słucha",
          q.czekaj(r"K1-TEST: mikrofon: strefa próbna bez przełącznika — uruchomiona", czas=240) and
          q.czekaj(r"K1-GOSC: mikrofon: karta bez wejścia \(dobrze\)", czas=240) and
          q.ile(r"K1-DZWIEK: mikrofon: słucha strefa") == n_sl)
q.czysty_log()
q.koniec()
PY

info "test dźwięku: QEMU z kartą Intel HD Audio, dźwięk do pliku WAV"
ZNAK=$TESTY/dzwiek-znak.img
dd if=/dev/zero of="$ZNAK" bs=1M count=1 status=none
SPRZET="-drive if=none,id=znak,file=$ZNAK,format=raw,readonly=on -device virtio-blk-pci,drive=znak,serial=k1-test-opis-dzwieku,addr=0x1e"
SPRZET="$SPRZET -audiodev wav,id=dz,path=$WAV,out.frequency=48000,out.channels=2,out.format=s16"
SPRZET="$SPRZET -device ich9-intel-hda,id=hda -device hda-output,audiodev=dz,bus=hda.0"
# 0.13.5 step 1: the microphone — a second codec with an input; QEMU's "none" records silence (the tone: the test program)
SPRZET="$SPRZET -audiodev none,id=mik -device hda-micro,audiodev=mik,bus=hda.0"
# 0.12.0 step 3: the test zone runs in K1 (its KVM: nested, else emulated — as test-strefa.sh)
EMULACJA=
if [ -w /dev/kvm ]; then
    grep -qsE '^(1|Y)$' /sys/module/kvm_intel/parameters/nested /sys/module/kvm_amd/parameters/nested || EMULACJA=--emulacja
fi
"$K1_REPO/scripts/uruchom-qemu.sh" --log "$TESTY/dzwiek.log" --monitor "$TESTY/dzwiek.sock" --czas "$CZAS" \
    --obraz "$K1_WYNIK/rdzen-test.squashfs" --pamiec 1024 --qemu "$SPRZET" $EMULACJA &
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
