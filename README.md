☀️ ESP32 Qoltec MPPT to MQTT Gateway
Ten projekt to oprogramowanie dla mikrokontrolera ESP32 (szczególnie zoptymalizowane pod ESP32-C3), które pełni rolę bramki telemetrycznej (Gateway) pomiędzy solarnym regulatorem ładowania Qoltec MPPT (obsługującym technologię Bluetooth) a systemami Smart Home (np. Home Assistant, Node-RED, Grafana) poprzez protokół MQTT.

Projekt jest idealny do zastosowań typu Off-Grid, stacji meteorologicznych, trackerów OGN (Open Glider Network) oraz wszelkich instalacji zasilanych słońcem, gdzie liczy się niezawodność, oszczędność energii i precyzyjny monitoring "na żywo".

🚀 Główne cechy projektu
W 100% Pasywny Nasłuch BLE (Sniffer): Układ nie odpytuje aktywnie regulatora, a jedynie bezkolizyjnie "podsłuchuje" rozgłaszane pakiety telemetryczne na kanale FFE4. Dzięki temu nie zawiesza modułu Bluetooth w regulatorze i może działać równolegle z oryginalną aplikacją producenta.

Kompleksowa telemetria: Precyzyjne odczyty wprost z urządzenia:

☀️ Panele PV: Napięcie (V), Prąd (A), Moc (W)

🔋 Akumulator: Napięcie (V), SOC (Stan naładowania w %), Temperatura (°C)

⚡ Obciążenie (Load): Prąd (A)

🌡️ System: Temperatura radiatora/układu MOSFET (°C)

Zoptymalizowane Wi-Fi & RF Coexistence: Zaawansowane zarządzanie współdzieleniem pojedynczej anteny 2.4 GHz dla Wi-Fi i Bluetooth Low Energy. Szybkie łączenie z routerem z pominięciem skanowania kanałów (wymaga podania BSSID).

Niezawodny, nieblokujący klient MQTT: Kod jest odporny na zerwania połączeń z brokerem – nie blokuje odczytów Bluetooth ani działania serwera WWW w przypadku problemów z siecią (obsługa długich timeoutów i KeepAlive).

Wbudowany Serwer WWW: Bezpośredni, odświeżany co 5 sekund podgląd parametrów pracy stacji z poziomu przeglądarki lokalnej.

Precyzyjny znacznik czasu (NTP): Automatyczna synchronizacja czasu z serwerami internetowymi (format ISO 8601), z uwzględnieniem polskiej strefy czasowej (zmiany czasu lato/zima).

🛠️ Wymagania sprzętowe
Mikrokontroler: ESP32 (zalecany ESP32-C3 ze względu na niski pobór prądu i wbudowane BLE 5.0).

Regulator ładowania: Qoltec MPPT z wbudowanym modułem Bluetooth (np. modele z serii 5366x - 20A, 50A itp.) lub kompatybilne klony (oparte na chipie SRNE).

📡 Format Danych wyjściowych (JSON)
Moduł publikuje dane cyklicznie co 5 sekund pod wskazany temat MQTT (domyślnie ogn/zasilanie). Gotowy do parsowania (np. w Home Assistant) format JSON wygląda następująco:

JSON
{
  "timestamp": "2026-05-16T14:30:00",
  "vPV": 18.99,
  "iPV": 5.60,
  "vBat": 13.13,
  "soc": 50,
  "iLoad": 4.33,
  "tempBat": 21,
  "tempDev": 26
}
📚 Zależności (Biblioteki)
Do poprawnej kompilacji projektu wymagane jest zainstalowanie bibliotek:

WiFi.h, WebServer.h, time.h (wbudowane w pakiet ESP32)

NimBLE-Arduino (znacznie oszczędniejsza pamięciowo alternatywa dla klasycznego BLE)

PubSubClient (lekki klient MQTT)

📝 Oświadczenie
Oprogramowanie to zostało stworzone na drodze inżynierii odwrotnej (Reverse Engineering) protokołu rozgłoszeniowego i nie jest oficjalnym produktem marki Qoltec. Używasz na własną odpowiedzialność.

