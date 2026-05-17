#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <PubSubClient.h>
#include <NimBLEDevice.h>
#include <time.h> // Biblioteka do obsługi czasu

// --- KONFIGURACJA WI-FI ---
//const char* ssid = "DariuszFly";
//const char* password = "anuLRrats793%";
const char* ssid = "Orange Airbox-9CB8";
const char* password = "26200600";
const int wifi_channel = 1;
//const uint8_t bssid[] = {0x0A, 0xA7, 0xC0, 0x65, 0x46, 0x12};  
const uint8_t bssid[] = {0x26, 0x7F, 0x3C, 0xA9, 0x9C, 0xB9};   // AirBox 24:7F:3C:A9:9C:B8

// --- KONFIGURACJA MQTT ---
//const char* mqtt_server = "192.168.33.23"; // Aitbox ??
//const char* mqtt_server = "DarekFly.local"; // Aitbox ??
const char* mqtt_server = "Pi4-EPWC.local"; // = HOSTNAME.local
const char* mqtt_user = "darekfly";
const char* mqtt_pass = "DariuszFly%";
const int   mqtt_port = 1883;

// --- KONFIGURACJA CZASU (NTP) ---
const char* ntpServer = "pool.ntp.org";
const char* tzInfo = "CET-1CEST,M3.5.0,M10.5.0/3"; // Polska strefa czasowa z auto-zmianą lato/zima

WiFiClient espClient;
PubSubClient mqttClient(espClient);
unsigned long lastMqttReconnectAttempt = 0;

// --- KONFIGURACJA SERWERA WWW ---
WebServer server(80);

// --- KONFIGURACJA BLE QOLTEC ---
const char* targetMacAddress = "dc:32:62:72:60:e6";
static NimBLEUUID serviceUUID("FFE0");
static NimBLEUUID charNotifyUUID("FFE4");

static NimBLEAdvertisedDevice* advDevice = nullptr;
bool doConnect = false;
bool connectedBLE = false;

// --- GLOBALNA STRUKTURA DANYCH TELEMETRYCZNYCH ---
struct QoltecData {
    float vBat = 0.0;
    uint8_t soc = 0;
    int8_t tempBat = 0;
    float vPV = 0.0;
    float iPV = 0.0;
    float iLoad = 0.0;
    int8_t tempDev = 0;
    bool newDataFlag = false; 
} sensorData;

static uint8_t rxBuffer[128];
static size_t rxIndex = 0;
static uint32_t lastRxTime = 0;

// --- FUNKCJA POBIERAJĄCA AKTUALNY CZAS ---
String getTimestamp() {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        return "\"Brak synchr. NTP\""; // Zwraca to, jeśli ESP32 jeszcze nie pobrało czasu
    }
    char timeStringBuff[35];
    // Format ISO 8601 używany standardowo w IoT: "YYYY-MM-DDTHH:MM:SS"
    strftime(timeStringBuff, sizeof(timeStringBuff), "\"%Y-%m-%dT%H:%M:%S\"", &timeinfo);
    return String(timeStringBuff);
}

// --- STRONA WWW ---
void handleRoot() {
    String html = "<html><head><meta charset='UTF-8'><meta http-equiv='refresh' content='5'>";
    html += "<style>body { font-family: Arial; text-align: center; margin-top: 50px; background-color: #f4f4f4;} ";
    html += ".card { background: white; padding: 20px; border-radius: 10px; box-shadow: 0 4px 8px rgba(0,0,0,0.1); display: inline-block; text-align: left;}</style>";
    html += "<title>Stacja OGN - Zasilanie</title></head><body>";
    html += "<h2>Stacja Telemetryczna OGN - Qoltec MPPT</h2>";
    html += "<div class='card'>";
    
    // Zastępujemy cudzysłowy generowane dla JSON-a, by ładnie wyglądało w HTML
    String displayTime = getTimestamp();
    displayTime.replace("\"", ""); 
    
    html += "<p style='color: gray; font-size: 0.9em;'>Ostatnia aktualizacja: <b>" + displayTime + "</b></p>";
    html += "<hr>";

    html += "<h3>Panele Fotowoltaiczne</h3>";
    html += "<p>Napięcie: <b>" + String(sensorData.vPV, 2) + " V</b></p>";
    html += "<p>Prąd: <b>" + String(sensorData.iPV, 2) + " A</b></p>";
    html += "<p>Moc: <b>" + String(sensorData.vPV * sensorData.iPV, 1) + " W</b></p>";
    
    html += "<h3>Akumulator</h3>";
    html += "<p>Napięcie: <b>" + String(sensorData.vBat, 2) + " V</b></p>";
    html += "<p>SOC: <b>" + String(sensorData.soc) + " %</b></p>";
    html += "<p>Temperatura: <b>" + String(sensorData.tempBat) + " &deg;C</b></p>";
    
    html += "<h3>Obciążenie (Load)</h3>";
    html += "<p>Prąd zasilania sprzętu: <b>" + String(sensorData.iLoad, 2) + " A</b></p>";
    
    html += "<h3>System</h3>";
    html += "<p>Temp. radiatora: <b>" + String(sensorData.tempDev) + " &deg;C</b></p>";
    html += "<p>Status BLE: <b>" + String(connectedBLE ? "POŁĄCZONO" : "SZUKAM...") + "</b></p>";
    html += "<p>Status MQTT: <b>" + String(mqttClient.connected() ? "POŁĄCZONO" : "ROZŁĄCZONY") + "</b></p>";
    html += "</div></body></html>";

    server.send(200, "text/html", html);
}

// --- FUNKCJA NIEBLOKUJĄCEGO ŁĄCZENIA Z MQTT ---
void reconnectMQTT() {
    if (!mqttClient.connected()) {
        unsigned long now = millis();
        if (now - lastMqttReconnectAttempt > 10000) {
            lastMqttReconnectAttempt = now;
            Serial.print("Próba połączenia z brokerem MQTT... ");
            
            String clientId = "ESP32-OGN-Station-";
            clientId += String(random(0xffff), HEX);
            
            if (mqttClient.connect(clientId.c_str(), mqtt_user, mqtt_pass)) {
                Serial.println("SUKCES!");
            } else {
                Serial.print("BŁĄD, kod: ");
                Serial.println(mqttClient.state());
            }
        }
    }
}

// --- PARSER DANYCH QOLTEC ---
void notifyCallback(NimBLERemoteCharacteristic* pChar, uint8_t* pData, size_t length, bool isNotify) {
    uint32_t now = millis();
    
    if (now - lastRxTime > 150) {
        rxIndex = 0; 
    }
    lastRxTime = now;

    if (rxIndex + length <= sizeof(rxBuffer)) {
        memcpy(&rxBuffer[rxIndex], pData, length);
        rxIndex += length;
    }

    if (rxIndex == 92) {
        uint16_t pvVRaw = (rxBuffer[5] << 8) | rxBuffer[4]; 
        uint16_t pvIRaw = (rxBuffer[7] << 8) | rxBuffer[6]; 
        uint16_t vBatRaw = (rxBuffer[9] << 8) | rxBuffer[8]; 
        uint16_t loadIRaw = (rxBuffer[15] << 8) | rxBuffer[14];

        sensorData.soc      = rxBuffer[10];                     
        sensorData.tempBat  = rxBuffer[12]; 
        sensorData.tempDev  = rxBuffer[13]; 
        sensorData.vPV      = pvVRaw / 100.0;
        sensorData.iPV      = pvIRaw / 100.0;
        sensorData.vBat     = vBatRaw / 100.0;
        sensorData.iLoad    = loadIRaw / 100.0;
        
        sensorData.newDataFlag = true;
        rxIndex = 0; 
    }
}

// --- CALLBACKI BLE ---
class MyClientCallbacks : public NimBLEClientCallbacks {
    void onConnect(NimBLEClient* pClient) override {
        Serial.println(">>> POŁĄCZONO BLE: Sesja z regulatorem zestawiona.");
    }
    void onDisconnect(NimBLEClient* pClient, int reason) override {
        connectedBLE = false;
        doConnect = false;
        Serial.printf(">>> ROZŁĄCZONO BLE (Kod: %d). Powrót do skanowania...\n", reason);
    }
};

class MyScanCallbacks : public NimBLEScanCallbacks {
    void onResult(const NimBLEAdvertisedDevice* advertisedDevice) override {
        if (advertisedDevice->getAddress().toString() == targetMacAddress) {
            NimBLEDevice::getScan()->stop();
            if (advDevice) delete advDevice;
            advDevice = new NimBLEAdvertisedDevice(*advertisedDevice);
            doConnect = true;
        }
    }
};

bool connectToBLE() {
    if (advDevice == nullptr) return false;
    NimBLEClient* pClient = NimBLEDevice::createClient();
    pClient->setClientCallbacks(new MyClientCallbacks(), false);

    if (!pClient->connect(advDevice)) return false;

    NimBLERemoteService* pService = pClient->getService(serviceUUID);
    if (pService == nullptr) {
        pClient->disconnect();
        return false;
    }

    NimBLERemoteCharacteristic* pNotifyChar = pService->getCharacteristic(charNotifyUUID);
    if (pNotifyChar != nullptr && pNotifyChar->canNotify()) {
        pNotifyChar->subscribe(true, notifyCallback);
        Serial.println("Kanał BLE zasubskrybowany.");
    } else {
        pClient->disconnect();
        return false;
    }
    return true;
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n--- Start ESP32-C3 OGN Tracker Power Station ---");

    Serial.print("Szybkie łączenie z Wi-Fi: ");
    Serial.println(ssid);

    WiFi.disconnect(true);
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);

    WiFi.begin(ssid, password, wifi_channel, bssid); // , bssid
    
    int wifiAttempts = 0;
    while (WiFi.status() != WL_CONNECTED && wifiAttempts < 20) {
        delay(500);
        Serial.print(".");
        wifiAttempts++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\nPołączono błyskawicznie!");
        Serial.print("Adres IP WebServera: ");
        Serial.println(WiFi.localIP());
        delay(10000); 
        
        // --- INICJALIZACJA CZASU (Tylko po połączeniu z internetem) ---
        Serial.print("Pobieranie czasu z NTP... ");
        configTzTime(tzInfo, ntpServer);
        Serial.println("Skonfigurowano.");
    } else {
        Serial.println("\nBŁĄD: Timeout Wi-Fi. Restartuję układ...");
        ESP.restart();
    }

    // KONFIGURACJA MQTT
    mqttClient.setServer(mqtt_server, mqtt_port);
    mqttClient.setSocketTimeout(5);     
    mqttClient.setKeepAlive(60);        
    mqttClient.setBufferSize(512); // Zwiększony bufor, by zmieścić JSONa z długim znacznikiem czasu

    // KONFIGURACJA WWW
    server.on("/", handleRoot);
    server.begin();
    Serial.println("Serwer HTTP uruchomiony.");

    // INICJALIZACJA BLE
    NimBLEDevice::init("ESP-OGN-Pwr");
    NimBLEDevice::setPower(ESP_PWR_LVL_P3); 

    NimBLEScan* pScan = NimBLEDevice::getScan();
    pScan->setScanCallbacks(new MyScanCallbacks(), false);
    
    pScan->setInterval(100); 
    pScan->setWindow(40);    
    pScan->setActiveScan(true);
    
    pScan->start(0, false); 
}

void loop() {
    // 1. Obsługa Serwera WWW 
    server.handleClient();

    // 2. Obsługa MQTT 
    if (!mqttClient.connected()) {
        reconnectMQTT();
    } else {
        mqttClient.loop();
    }

    // 3. Sprawdzenie flagi i wysyłka nowych danych
    if (sensorData.newDataFlag) {
        sensorData.newDataFlag = false; 
        
        Serial.println("\n====== TELEMETRIA QOLTEC (OGN STATION) ======");
        Serial.printf("Czas       : %s\n", getTimestamp().c_str());
        Serial.printf("Panele PV  : %.2f V  |  Prąd: %.2f A\n", sensorData.vPV, sensorData.iPV);
        Serial.printf("Bateria    : %.2f V  |  SOC : %d %%  |  Temp: %d C\n", sensorData.vBat, sensorData.soc, sensorData.tempBat);
        Serial.printf("Obciążenie : Prąd: %.2f A\n", sensorData.iLoad);
        Serial.printf("Regulator  : Temp: %d C\n", sensorData.tempDev);
        Serial.println("=============================================");

        // Publikacja w MQTT co 5 sekund
        static unsigned long lastPublish = 0;
        if (millis() - lastPublish > 5000) {
            if (mqttClient.connected()) {
                char payload[384]; // Zwiększony bufor dla pewności, że długi timestamp wejdzie
                
                // Generujemy JSON z nowym polem timestamp na samym początku
                snprintf(payload, sizeof(payload), 
                    "{\"timestamp\":%s, \"vPV\":%.2f, \"iPV\":%.2f, \"vBat\":%.2f, \"soc\":%d, \"iLoad\":%.2f, \"tempBat\":%d, \"tempDev\":%d}",
                    getTimestamp().c_str(), sensorData.vPV, sensorData.iPV, sensorData.vBat, sensorData.soc, sensorData.iLoad, sensorData.tempBat, sensorData.tempDev
                );
                mqttClient.publish("ogn/zasilanie", payload);
                Serial.println("--> MQTT: Wysłano JSON (" + String(payload) + ")");
            }
            lastPublish = millis();
        }
    }

    // 4. Stabilność BLE
    if (doConnect) {
        if (connectToBLE()) connectedBLE = true;
        doConnect = false;
    }

    if (!connectedBLE && !doConnect && !NimBLEDevice::getScan()->isScanning()) {
        static unsigned long lastScanResume = 0;
        if (millis() - lastScanResume > 2000) {
            NimBLEDevice::getScan()->start(0, false);
            lastScanResume = millis();
        }
    }
    
    delay(10); 
}