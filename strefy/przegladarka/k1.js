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
pref("media.ffmpeg.vaapi.enabled", false);
// k1: tymczasowe — diagnostyka tej sesji (usunąć przed scaleniem)
pref("devtools.console.stdout.chrome", true);
pref("browser.dom.window.dump.enabled", true);
