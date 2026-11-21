# K1 — the host stand's scenario for 0.11.0 step 1 (D1: A): the full screen. F11 in Firefox — it asks the compositor
# (xdg_toplevel.set_fullscreen), the compositor reports "pelny 1" (its log: "pełny ekran: okno prosi") and configures
# the window in full screen; then K1 ends it (SIGUSR2, as gosc after "pelny 0" from the VMM) — the window leaves it
# without asking; F11 once more — Firefox asks again (it knows it left). Run inside stanowisko.py (its functions).
import signal

KEY_F11 = 87


def log_kompozytora():
    log_k.flush()
    return open(f"{praca}/kompozytor.log", errors="replace").read()


def prosby():
    return log_kompozytora().count("pełny ekran: okno prosi")


klik(*SRODEK)
time.sleep(1)
klawisz(KEY_F11)
time.sleep(4)
sprawdz("F11: Firefox prosi o pełny ekran, kompozytor zgłasza „pelny 1”", prosby() == 1)
zrzut("pelny")
komp.send_signal(signal.SIGUSR2)
time.sleep(4)
sprawdz("K1 kończy pełny ekran (SIGUSR2): kompozytor zdejmuje stan oknu, Firefox nie prosi znowu sam",
        "K1 zakończył pełny ekran: okien 1" in log_kompozytora() and prosby() == 1)
klik(*SRODEK)
time.sleep(1)
klawisz(KEY_F11)
time.sleep(4)
sprawdz("F11 jeszcze raz: Firefox wie, że wyszedł — prosi znowu", prosby() == 2)
klawisz(KEY_F11)
time.sleep(4)
sprawdz("F11 w pełnym ekranie: Firefox wychodzi sam, kompozytor zgłasza „pelny 0”",
        log_kompozytora().count("pełny ekran: koniec") >= 1)
