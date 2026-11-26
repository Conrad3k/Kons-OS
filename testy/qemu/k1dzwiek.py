"""K1 — what QEMU's sound card played (0.12.0 step 1, scripts/test-dzwiek.sh): a WAV file from -audiodev wav, 48 kHz,
16 bits, stereo. Python only (the standard library): the loud parts ("dźwięki"), the frequency of each half by zero
crossings, the loudest sample. The checks are those of test-dzwiek: exactly five sounds (the muted sample is silence),
in each the notes 660 Hz then 880 Hz, the second and third at 50 % ≈ 1/8 of the first (100 %; the cubic curve of
k1_dzw_wzmocnienie), the third (after the service restarted) as loud as the second; step 2: the fourth at 40 % (after the
volume keys, from Ustawienia), the fifth at 45 % (the volume a new gui took from skarbiec) — (0.8)³ and (0.9)³ of 50 %;
step 3: after them the test zone's tone, 480 Hz, at least a second of it, with a gap of at least 2 s (its session muted
from the speaker on its window's bar). 0.12.3 step 1 (ADR 0055, "przezroczystość"): the first sound (the sample at 100 %, one
16-bit stream; QEMU's card takes 16 bits only) is the sample itself, sample for sample — the same numbers as
k1_dzw_proba (src/wspolne/k1_dzwiek.c, computed here in the same order: Python's floats are IEEE doubles as C's).
0.12.3 step 2 (the equalizer): the sixth sound is the sample at 45 % with the band 1 kHz at +12 dB — each note as loud as
the equalizer's curve says against the fifth (the same sample flat): its filters and the level before them computed here
as in k1_korektor.c (660 Hz −7.0 dB, 880 Hz −1.7 dB). 0.12.3 step 3 (Kons Spatial Audio): the seventh sound is the
sample at 45 % with every band of the speakers' scene on the left — the right channel at least 20 dB under the left, the
power of both channels as in the fifth (0.13.0, ADR 0055 point 21: a scene keeps the loudness, ±1.5 dB; until then the left
as loud as the fifth — a scene never louder than 100 %). 0.12.3 step 4: the eighth sound is "Posłuchaj" through
that scene — four parts, a noise in one band after another: the bass the same in both channels (the scene leaves it in the
middle), the other three on the left (the right channel at least 20 dB under; Niski środek 12 dB — its slope below 120 Hz
is bass), each higher than the one before (zero crossings); the zone's tone comes after it.

    python3 -I testy/qemu/k1dzwiek.py PLIK.wav   — exit 0 when all hold"""
import array
import struct
import sys

CZESTOTLIWOSC = 48000
PROG = 60          # a sample above this (of 32767) is sound, not silence
BLOK = 240         # 5 ms
PRZERWA = 0.25     # a gap shorter than this does not end a sound (an underrun in emulation)


def probki(sciezka, kanal=0):
    """One channel (0 left, 1 right) as a list of ints. QEMU writes the header's sizes only when it closes the file: the data is
    read up to the end of the file whatever the header says."""
    b = open(sciezka, "rb").read()
    if len(b) < 44 or b[:4] != b"RIFF" or b[8:12] != b"WAVE":
        return None
    i, kanaly, bity, dane = 12, 2, 16, None
    while i + 8 <= len(b):
        nazwa, dl = b[i:i + 4], struct.unpack_from("<I", b, i + 4)[0]
        if nazwa == b"fmt ":
            kanaly, czest, bity = struct.unpack_from("<H", b, i + 10)[0], struct.unpack_from("<I", b, i + 12)[0], \
                struct.unpack_from("<H", b, i + 22)[0]
            if czest != CZESTOTLIWOSC or bity != 16:
                print(f"  [BŁĄD]  WAV: {czest} Hz, {bity} bitów — oczekiwano {CZESTOTLIWOSC} Hz, 16 bitów")
                return None
        elif nazwa == b"data":
            dane = b[i + 8:]
            break
        i += 8 + dl
    if dane is None:
        return None
    n = len(dane) // (2 * kanaly)
    return list(struct.unpack_from(f"<{n * kanaly}h", dane))[kanal::kanaly]


def dzwieki(s):
    """The loud parts: [(start, end)] in samples, joined across gaps shorter than PRZERWA."""
    czesci, start, koniec = [], None, None
    for b in range(0, len(s), BLOK):
        glosny = max((abs(v) for v in s[b:b + BLOK]), default=0) > PROG
        if glosny:
            if start is None or b - koniec > PRZERWA * CZESTOTLIWOSC:
                if start is not None:
                    czesci.append((start, koniec))
                start = b
            koniec = b + BLOK
    if start is not None:
        czesci.append((start, koniec))
    return [(a, z) for a, z in czesci if z - a > 0.1 * CZESTOTLIWOSC]  # a click is not a sound


def _sinus(x):
    """sinus() of k1_dzwiek.c, operation for operation"""
    pi = 3.14159265358979323846
    if x >= 0.5:
        return -_sinus(x - 0.5)
    t = (0.5 - x if x > 0.25 else x) * 2.0 * pi
    t2 = t * t
    return t * (1 - t2 / 6 * (1 - t2 / 20 * (1 - t2 / 42 * (1 - t2 / 72 * (1 - t2 / 110)))))


def _nuta(i, hz):
    obw = i / 480.0 if i < 480 else 1.0
    obw *= 1.0 - i / 16800
    v = 11000 * obw * _sinus((i * hz % CZESTOTLIWOSC) / CZESTOTLIWOSC)
    return int(v + 0.5) if v >= 0 else int(v - 0.5)  # C's (int16_t) cuts toward zero, as int()


def proba():
    """k1_dzw_proba: 660 Hz, then 880 Hz, 16800 frames each"""
    return [_nuta(i, 660) for i in range(16800)] + [_nuta(i, 880) for i in range(16800)]


def przezroczystosc(s, a, z, wzor):
    """How many samples of s[a:z] match the sample `wzor` exactly, in runs of at least 16 in its order (the emulation
    may leave gaps of silence, or lose a period at an underrun — the sample goes on later), and how many loud samples
    match nothing (each such is a sample K1 changed)."""
    w = array.array("h", wzor).tobytes()
    dopasowane, obce, i, p = 0, 0, a, 0
    while i < z:
        if s[i] == 0:
            i += 1
            continue
        kawalek = array.array("h", s[i:i + 16]).tobytes()
        k = w.find(kawalek, 2 * p)
        while k >= 0 and k % 2:
            k = w.find(kawalek, k + 1)
        if k < 0 or z - i < 16:
            obce += abs(s[i]) > PROG
            i += 1
            continue
        k //= 2
        while i < z and k < len(wzor) and s[i] == wzor[k]:
            i, k, dopasowane = i + 1, k + 1, dopasowane + 1
        p = k
    return dopasowane, obce


def korektor_db(hz, pasmo_hz, pol):
    """The equalizer of one band at `pol` half-decibels (k1_korektor.c: the Audio EQ Cookbook's peaking filter, Q = √2;
    since 0.13.0 — ADR 0055 point 21 — the level before it keeps the K-weighted loudness of a pink sound on its grid, its
    loudest point at most +6 dB, K1_DZW_ZAPAS) at `hz`: dB."""
    import math
    fs = CZESTOTLIWOSC

    def moc(f, h):
        b0, b1, b2, a1, a2 = f
        c = math.cos(2 * math.pi * h / fs)
        c2 = 2 * c * c - 1
        return (b0 * b0 + b1 * b1 + b2 * b2 + 2 * (b0 * b1 + b1 * b2) * c + 2 * b0 * b2 * c2) / \
            (1 + a1 * a1 + a2 * a2 + 2 * (a1 + a1 * a2) * c + 2 * a2 * c2)
    waga = ((1.53512485958697, -2.69169618940638, 1.19839281085285, -1.69065929318241, 0.73248077421585),
            (1.0, -2.0, 1.0, -1.99004745483398, 0.99007225036621))  # BS.1770's K weighting at 48 kHz
    w0, a = 2 * math.pi * pasmo_hz / fs, 10 ** (pol / 2 / 40)
    al, c = math.sin(w0) / (2 * math.sqrt(2)), math.cos(w0)
    a0 = 1 + al / a
    f = ((1 + al * a) / a0, -2 * c / a0, (1 - al * a) / a0, -2 * c / a0, (1 - al / a) / a0)
    siatka = [20 * 1000 ** (k / 240) for k in range(241)]
    m = max([moc(f, h) for h in siatka] + [moc(f, pasmo_hz)] + [moc(f, 20000 + 500 * k) for k in range(1, 9)])
    wagi = [moc(waga[0], h) * moc(waga[1], h) for h in siatka]
    wzm2 = sum(wagi) / sum(w * moc(f, h) for w, h in zip(wagi, siatka))
    wzm2 = min(wzm2, 4.0 / m)  # K1_DZW_ZAPAS²
    return 10 * math.log10(moc(f, hz) * wzm2)


def rms(s):
    return (sum(v * v for v in s) / max(len(s), 1)) ** 0.5


def czestotliwosc(s):
    przejscia = sum(1 for i in range(1, len(s)) if (s[i - 1] < 0) != (s[i] < 0))
    return przejscia / 2 * CZESTOTLIWOSC / max(len(s), 1)


def main():
    wyniki = []

    def sprawdz(nazwa, ok, opis=""):
        print(f"  [{'OK' if ok else 'BŁĄD'}]{' ' * (6 - len('OK' if ok else 'BŁĄD'))}{nazwa}" + (f" — {opis}" if opis else ""))
        wyniki.append(bool(ok))

    s = probki(sys.argv[1])
    if not s:
        sprawdz("plik WAV od QEMU (48 kHz, 16 bitów)", False, sys.argv[1])
        return 1
    d = dzwieki(s)
    sprawdz("siedem prób (100 %, 50 %, 50 % po restarcie usługi, 40 %, 45 %, 45 % z korektorem, 45 % ze sceną z lewej; "
            "wyciszona — cisza), „Posłuchaj”, po nich ton strefy", len(d) >= 9,
            f"{len(d)}: " + ", ".join(f"{a / CZESTOTLIWOSC:.2f}–{z / CZESTOTLIWOSC:.2f} s" for a, z in d))
    szczyty = []
    for nr, (a, z) in enumerate(d[:7], 1):
        pol = (a + z) // 2
        f1, f2 = czestotliwosc(s[a:pol]), czestotliwosc(s[pol:z])
        szczyty.append(max(abs(v) for v in s[a:z]))
        sprawdz(f"dźwięk {nr}: nuty 660 Hz, potem 880 Hz", 630 < f1 < 690 and 840 < f2 < 920,
                f"{f1:.0f} Hz, {f2:.0f} Hz, szczyt {szczyty[-1]}, {(z - a) / CZESTOTLIWOSC:.2f} s")
    if len(szczyty) >= 3:
        sprawdz("50 % ≈ 1/8 amplitudy 100 % (krzywa sześcienna)", 0.09 < szczyty[1] / szczyty[0] < 0.17,
                f"{szczyty[1] / szczyty[0]:.3f}")
        sprawdz("po restarcie usługi ta sama głośność (gui podała ją nowej usłudze)", 0.8 < szczyty[2] / szczyty[1] < 1.25,
                f"{szczyty[2] / szczyty[1]:.3f}")
        sprawdz("100 %: próba bez przesterowania (szczyt poniżej 32767)", 3000 < szczyty[0] < 32767, f"{szczyty[0]}")
    if d:  # 0.12.3: one stream at 100 % — K1 changes nothing
        wzor = proba()
        a, z = d[0]
        dop, obce = przezroczystosc(s, max(0, a - BLOK), min(len(s), z + BLOK), wzor)
        sprawdz("100 %: próba w pliku próbka w próbkę taka jak w programie (przezroczystość, 0.12.3)",
                obce == 0 and dop >= 0.9 * len(wzor), f"zgodne {dop} z {len(wzor)}, inne głośne {obce}")
    if len(d) >= 6:  # 0.12.3 step 2: the sixth (1 kHz +12 dB) against the fifth (flat), note by note
        for i, hz in enumerate((660, 880)):
            czesci = []
            for a, z in d[4:6]:
                pol = (a + z) // 2
                a, z = (a, pol) if i == 0 else (pol, z)
                czesci.append(rms(s[a + (z - a) // 5:z - (z - a) // 5]))  # the middle of the note, its envelope aside
            db, cel = 20 * __import__("math").log10(max(czesci[1], 1e-9) / max(czesci[0], 1e-9)), korektor_db(hz, 1000, 24)
            sprawdz(f"korektor (1 kHz +12 dB): nuta {hz} Hz o {cel:+.1f} dB wobec próby bez korektora", abs(db - cel) < 0.75,
                    f"{db:+.2f} dB")
    if len(d) >= 7:  # 0.12.3 step 3: the seventh — the scene on the left: the right channel quiet, the left as the fifth
        prawy = probki(sys.argv[1], 1)
        for i, hz in enumerate((660, 880)):
            lewy_db, prawy_db = [], []
            for a, z in (d[4], d[6]):
                pol = (a + z) // 2
                a, z = (a, pol) if i == 0 else (pol, z)
                a, z = a + (z - a) // 5, z - (z - a) // 5
                lewy_db.append(rms(s[a:z]))
                prawy_db.append(rms(prawy[a:z]))
            log = __import__("math").log10
            roznica, wobec = 20 * log(max(lewy_db[1], 1e-9) / max(prawy_db[1], 1e-9)), \
                20 * log(max(lewy_db[1], 1e-9) / max(lewy_db[0], 1e-9))
            # 0.13.0: the power of both channels as in the fifth (the right one's moved to the left)
            moc = 10 * log((lewy_db[1] ** 2 + prawy_db[1] ** 2) / max(lewy_db[0] ** 2 + prawy_db[0] ** 2, 1e-18))
            sprawdz(f"scena z lewej (głośniki): nuta {hz} Hz — prawy kanał co najmniej 20 dB ciszej, moc jak bez sceny",
                    roznica >= 20.0 and abs(moc) < 1.5, f"lewy − prawy {roznica:+.1f} dB, lewy wobec piątej {wobec:+.2f} dB, "
                    f"moc obu kanałów wobec piątej {moc:+.2f} dB")
    if len(d) >= 8:  # 0.12.3 step 4: "Posłuchaj" through the scene on the left, its four bands in quarters
        prawy, (a, z) = probki(sys.argv[1], 1), d[7]
        log, cz, ok_l, opis = __import__("math").log10, [], True, []
        for k in range(4):
            p0, p1 = a + (z - a) * k // 4, a + (z - a) * (k + 1) // 4
            p0, p1 = p0 + (p1 - p0) // 4, p1 - (p1 - p0) // 4  # the middle of the quarter
            roznica = 20 * log(max(rms(s[p0:p1]), 1e-9) / max(rms(prawy[p0:p1]), 1e-9))
            cz.append(czestotliwosc(s[p0:p1]))
            # Niski środek: its noise below 120 Hz (the crossover's slope, ~11 dB under it — test_przestrzen) is bass,
            # which the scene leaves in the middle — the right channel 12 dB under; the higher bands 20 dB
            ok_l &= abs(roznica) < 1.0 if k == 0 else roznica >= (12.0 if k == 1 else 20.0)
            opis.append(f"{cz[-1]:.0f} Hz, lewy − prawy {roznica:+.1f} dB")
        sprawdz("„Posłuchaj” przez scenę z lewej: bas w obu kanałach równo, pozostałe pasma z lewej, każde wyżej",
                ok_l and cz[0] < cz[1] < cz[2] < cz[3] and (z - a) / CZESTOTLIWOSC > 3.0,
                f"{(z - a) / CZESTOTLIWOSC:.1f} s; " + "; ".join(opis))
    if len(d) >= 9:  # step 3 of 0.12.0: the zone's tone (in emulation it may come in pieces), one gap of the muted session
        reszta = d[8:]
        ton = [v for a, z in reszta for v in s[a:z]]
        przerwa = max((reszta[i + 1][0] - reszta[i][1]) / CZESTOTLIWOSC for i in range(len(reszta) - 1)) \
            if len(reszta) > 1 else 0
        sprawdz("strefa: ton 480 Hz, co najmniej 1 s (karta virtio-snd → usługa dźwięku)",
                len(ton) >= CZESTOTLIWOSC and 450 < czestotliwosc(ton) < 510,
                f"{len(ton) / CZESTOTLIWOSC:.1f} s, {czestotliwosc(ton):.0f} Hz, części {len(reszta)}")
        sprawdz("strefa: sesja wyciszona głośnikiem na pasku — przerwa co najmniej 2 s", przerwa >= 2.0, f"{przerwa:.1f} s")
    if len(szczyty) >= 5:  # step 2: the keys (40 %) and the remembered volume (45 %), against 50 %: (0.8)³ = 0.512, (0.9)³ = 0.729
        sprawdz("40 % (klawisze, próba z Ustawień) ≈ 0,51 amplitudy 50 %", 0.42 < szczyty[3] / szczyty[1] < 0.62,
                f"{szczyty[3] / szczyty[1]:.3f}")
        sprawdz("45 % (nowy pulpit, głośność ze skarbca) ≈ 0,73 amplitudy 50 %", 0.62 < szczyty[4] / szczyty[1] < 0.85,
                f"{szczyty[4] / szczyty[1]:.3f}")
    return 0 if wyniki and all(wyniki) else 1


if __name__ == "__main__":
    sys.exit(main())
