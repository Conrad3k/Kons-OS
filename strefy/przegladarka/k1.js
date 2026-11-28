// K1 (0.10.0 krok 2, ADR 0043 T8): ustawienia przeglądarki w strefie — /etc/firefox-esr/k1.js w obrazie strefy.
// Zablokowane (locked): strona nie może ich zmienić, użytkownik też nie (about:config) — od nich zależy, którędy idą
// połączenia (tylko przez K1) i czy przeglądarka sama coś wysyła.
// Polski interfejs (pakiet firefox-esr-l10n-pl).
pref("intl.locale.requested", "pl", locked);
// Każde połączenie przez przekaźnik w strefie (SOCKS5 na 127.0.0.1:1080) do łącznika K1; nazwy stron zamienia na
// adresy K1 (socks_remote_dns), także dla adresów lokalnych (allow_hijacking_localhost) — strefa nie ma DNS ani sieci.
pref("network.proxy.type", 1, locked);
pref("network.proxy.socks", "127.0.0.1", locked);
pref("network.proxy.socks_port", 1080, locked);
pref("network.proxy.socks_version", 5, locked);
pref("network.proxy.socks_remote_dns", true, locked);
pref("network.proxy.no_proxies_on", "", locked);
pref("network.proxy.allow_hijacking_localhost", true, locked);
pref("network.proxy.share_proxy_settings", false, locked);
pref("network.trr.mode", 5, locked);
pref("network.dns.disableIPv6", true, locked);
pref("media.peerconnection.enabled", false, locked);
// Dźwięk (0.12.0 krok 3, ADR 0054 T6 = A): Firefox gra przez ALSA (libasound z obrazu) do karty virtio-snd strefy, którą
// udaje VMM K1 — bez serwera PulseAudio (najpierw próbowałby go szukać). Mikrofon (0.13.5 krok 2, ADR 0065): ta sama
// droga w drugą stronę — karta strefy ma wejście tylko w sesji z przełącznikiem „Mikrofon”; strona i tak pyta (okienko
// Firefoksa). Rozmowy przez WebRTC (wyżej: wyłączony) — dopiero z UDP w Raptor Bridge (plan 0.14.0, krok 4).
pref("media.cubeb.backend", "alsa", locked);
// Nic nie wysyła sama: bez aktualizacji (przychodzą z wydaniem K1), telemetrii, badań, raportów awarii.
pref("app.update.enabled", false, locked);
pref("extensions.update.enabled", false, locked);
pref("toolkit.telemetry.enabled", false, locked);
pref("toolkit.telemetry.unified", false, locked);
pref("toolkit.telemetry.archive.enabled", false, locked);
pref("datareporting.healthreport.uploadEnabled", false, locked);
pref("datareporting.policy.dataSubmissionEnabled", false, locked);
pref("app.normandy.enabled", false, locked);
pref("app.shield.optoutstudies.enabled", false, locked);
pref("browser.crashReports.unsubmittedCheck.enabled", false, locked);
pref("browser.discovery.enabled", false, locked);
pref("browser.safebrowsing.downloads.remote.enabled", false);
pref("network.captive-portal-service.enabled", false);
pref("network.connectivity-service.enabled", false);
// Strefa nie ma karty sieciowej (tylko pętlę zwrotną — wszystko idzie przez przekaźnik), więc Firefox, który sam ocenia
// sieć po kartach systemu, uznawał po chwili, że „sieć jest wyłączona”, i przestawał otwierać strony: Enter w pasku adresu
// nie robił nic (plan 0.10.0, krok 2, ustalenie 7). Firefox w strefie nigdy sam nie przechodzi w tryb bez sieci.
pref("network.manage-offline-status", false, locked);
pref("network.offline-mirrors-connectivity", false, locked);
pref("browser.newtabpage.activity-stream.feeds.telemetry", false, locked);
pref("browser.newtabpage.activity-stream.telemetry", false, locked);
pref("browser.ping-centre.telemetry", false, locked);
// Pierwsze uruchomienie bez powitań — każda sesja jednorazowa startuje „po raz pierwszy”.
pref("browser.aboutwelcome.enabled", false);
pref("browser.startup.homepage_override.mstone", "ignore");
pref("startup.homepage_welcome_url", "");
pref("startup.homepage_welcome_url.additional", "");
pref("browser.shell.checkDefaultBrowser", false);
pref("trailhead.firstrun.didSeeAboutWelcome", true);
pref("datareporting.policy.firstRunURL", "");
pref("browser.uitour.enabled", false);
// Rysowanie procesorem (strefa ma tylko obraz 2D, ADR 0042 T6) — bez prób z OpenGL/VA-API.
pref("gfx.webrender.software", true);
pref("widget.dmabuf.force-enabled", false);
pref("media.hardware-video-decoding.enabled", false);
// Filmy H.264 (0.13.2 krok 2, ADR 0063 pkt 7): Firefox rozpakowuje je naszym FFmpeg z obrazu strefy (libavcodec.so.61) —
// procesorem; media.ffmpeg.enabled jest na Linuksie domyślnie włączone. Kartą (VA-API) — tylko w sesji z filmami kartą
// (krok 4: user.js od gosc).
// Pamięć strefy (0.10.0 krok 3, S26): „dysk” przeglądarki w strefie to pamięć (katalog domowy na tmpfs), więc pamięć
// podręczna stron na dysku zajmowała do kilkuset MB pamięci strefy na stałe — zostaje tylko pamięć podręczna w pamięci
// (sama się zmniejsza). Zamknięcie ostatniej karty nie zamyka przeglądarki (koniec przeglądarki = koniec strefy);
// przy braku pamięci przeglądarka sama zwalnia nieużywane karty.
pref("browser.cache.disk.enable", false, locked);
pref("browser.tabs.closeWindowWithLastTab", false);
// Zamknięcie strefy (0.10.0 krok 5): K1 zamyka okno przeglądarki jak ręcznie — bez pytania „Zamknąć karty?”, żeby
// przeglądarka skończyła się sama i zapisała dane sesji (ciasteczka, logowania).
pref("browser.tabs.warnOnClose", false, locked);
pref("browser.warnOnQuit", false, locked);
pref("browser.warnOnQuitShortcut", false, locked);
pref("browser.tabs.unloadOnLowMemory", true);
// Pobrane pliki (0.11.0 krok 2, ADR 0052): zawsze do jednego folderu sesji, bez pytania, gdzie zapisać (P9) — agent K1
// w strefie widzi tylko ten folder i po skończonym pobraniu K1 pyta w swoim okienku „Zapisz w Pobranych”. Firefox pisze
// pobranie do „<nazwa>.part” i na końcu zmienia nazwę (P10) — nie w katalogu tymczasowym.
pref("browser.download.dir", "/home/k1/Pobrane", locked);
pref("browser.download.folderList", 2, locked);
pref("browser.download.useDownloadDir", true, locked);
pref("browser.download.start_downloads_in_tmp_dir", false, locked);
// Bez okna „Otwieranie <plik>” (co zrobić z plikiem): w strefie nie ma innych programów — plik zawsze się zapisuje.
pref("browser.download.always_ask_before_handling_new_types", false, locked);
