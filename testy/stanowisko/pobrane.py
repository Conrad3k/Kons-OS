# K1 — the host stand's scenario for 0.11.0 step 2 (ADR 0052, P9, P10): a download in the real Firefox of the zone. A page
# over HTTP (through a SOCKS5 server on 127.0.0.1:1080, as the zone's relay) with a link to a file of 3 MiB sent as an
# attachment: Firefox saves it without asking where, into the locked folder of
# downloads (strefy/przegladarka/k1.js); how it ends — a ".part" renamed (IN_MOVED_TO: what the agent waits for) — is
# seen by a watcher on the host (PATRZ: an inotify program, its lines in zdarzenia.log). Then the owner's report of
# 2026-11-21 ("opening »Pobrane« crashed the browser"): the downloads panel (the toolbar's button after a download),
# Library → Downloads (Ctrl+Shift+Y) and about:downloads — Firefox and the compositor must live through each.
# Run inside stanowisko.py (its functions).
import http.server, socket, socketserver, subprocess, threading

POBRANE = f"{ROOT}/home/k1/Pobrane"
PLIK = bytes((i * 7 + 13) & 255 for i in range(3 << 20))
STRONA_P = ("<!doctype html><html><head><meta charset=utf-8><title>pobierz</title></head><body style='margin:0'>"
            "<a href='/plik.bin' style='display:block;width:100vw;height:100vh;background:#5a8'>pobierz</a></body></html>")


class Http(http.server.BaseHTTPRequestHandler):  # the web: a page and a file sent as an attachment
    def do_GET(self):
        tresc, typ = (PLIK, "application/octet-stream") if self.path == "/plik.bin" else (STRONA_P.encode(), "text/html")
        self.send_response(200)
        self.send_header("Content-Type", typ)
        self.send_header("Content-Length", str(len(tresc)))
        if self.path == "/plik.bin":
            self.send_header("Content-Disposition", 'attachment; filename="raport.bin"')
        self.end_headers()
        self.wfile.write(tresc)

    def log_message(self, *a):
        pass


serwer_http = socketserver.ThreadingTCPServer(("127.0.0.1", 0), Http)
threading.Thread(target=serwer_http.serve_forever, daemon=True).start()


class Socks(socketserver.BaseRequestHandler):  # the relay and K1's connector, as Firefox sees them: SOCKS5 on :1080
    def handle(self):
        s = self.request
        s.recv(3)
        s.sendall(b"\x05\x00")
        g = s.recv(4)
        if g[3] == 3:
            s.recv(s.recv(1)[0])
        else:
            s.recv(4)
        s.recv(2)
        d = socket.create_connection(("127.0.0.1", serwer_http.server_address[1]))
        s.sendall(b"\x05\x00\x00\x01\x7f\x00\x00\x01\x00\x00")
        def w_dol():
            try:
                while (b := d.recv(65536)):
                    s.sendall(b)
            except OSError:
                pass
            s.close()
        threading.Thread(target=w_dol, daemon=True).start()
        try:
            while (b := s.recv(65536)):
                d.sendall(b)
        except OSError:
            pass


socketserver.ThreadingTCPServer.allow_reuse_address = True
serwer_socks = socketserver.ThreadingTCPServer(("127.0.0.1", 1080), Socks)
threading.Thread(target=serwer_socks.serve_forever, daemon=True).start()
zdarzenia = open(f"{praca}/zdarzenia.log", "w")
patrz = subprocess.Popen([os.environ["PATRZ"], POBRANE], stdout=zdarzenia)


def zyje():
    log_k.flush()
    return ff.poll() is None and komp.poll() is None and "błąd protokołu" not in open(f"{praca}/kompozytor.log").read()


def adres(a):
    klawisz(KLAWISZE["l"], ctrl=True)
    time.sleep(0.5)
    pisz(a + "\n")
    time.sleep(4)


adres("http://k1.test/")
klik(*SRODEK)
time.sleep(8)
zdarzenia.flush()
ev_ = open(f"{praca}/zdarzenia.log").read()
print(ev_, flush=True)
jest = os.path.exists(f"{POBRANE}/raport.bin") and open(f"{POBRANE}/raport.bin", "rb").read() == PLIK
sprawdz("pobranie: plik w folderze pobranych sesji, 3 MiB, bez pytania gdzie", jest)
sprawdz("pobranie kończy się zmianą nazwy (IN_MOVED_TO raport.bin) — na to czeka agent", "MOVED_TO raport.bin" in ev_)
sprawdz("po pobraniu nie zostaje plik .part", not [n for n in os.listdir(POBRANE) if n.endswith(".part")])
zrzut("pobrane-panel")
sprawdz("panel pobranych (otwiera się sam po pobraniu): Firefox i kompozytor żyją", zyje())
klik(*SRODEK)  # the panel closes (and a second download of the same file: "raport(1).bin")
time.sleep(6)
zrzut("pobrane-drugie")
sprawdz("drugie pobranie tego samego pliku: Firefox i kompozytor żyją", zyje())
klawisz(KLAWISZE["y"], shift=True, ctrl=True)  # Library → Downloads, a window of its own
time.sleep(6)
zrzut("pobrane-biblioteka")
sprawdz("Biblioteka → Pobrane (Ctrl+Shift+Y): Firefox i kompozytor żyją", zyje())
klik(995, 117)  # the folder at the newest download: "Pokaż w folderze" (a file manager — the zone has none)
time.sleep(5)
zrzut("pobrane-folder")
sprawdz("Biblioteka: „Pokaż w folderze”: Firefox i kompozytor żyją", zyje())
klik(400, 117)
time.sleep(0.15)
klik(400, 117)  # a double click: open the file (no program for it in the zone)
time.sleep(5)
zrzut("pobrane-otworz")
sprawdz("Biblioteka: podwójne kliknięcie pobranego pliku (otwórz): Firefox i kompozytor żyją", zyje())
klawisz(KLAWISZE["w"], ctrl=True)  # the Library window closes
time.sleep(2)
adres("about:downloads")
zrzut("pobrane-about")
sprawdz("about:downloads w karcie: Firefox i kompozytor żyją", zyje())
patrz.terminate()
