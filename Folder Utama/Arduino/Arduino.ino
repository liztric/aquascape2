#include <Arduino.h>
#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <WiFiManager.h> // Gunakan WiFiManager untuk koneksi Wi-Fi

#define API_KEY "AIzaSyBgD_196K9e0NmyVbGtHxlyVAdgpGu5Yyo"
#define DATABASE_URL "https://aquascape-ffef6-default-rtdb.asia-southeast1.firebasedatabase.app/"
#define PH_SENSOR_PIN 34
#define ONE_WIRE_BUS 4

#define RELAY_LED_PIN 14
#define RELAY_FAN_PIN 12

#define RED_PIN 27
#define GREEN_PIN 26
#define BLUE_PIN 25

FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

LiquidCrystal_I2C lcd(0x27, 20, 4);

float PH4 = 3.2992;
float PH9 = 2.7856;
int nilai_analog_PH;
double TeganganPh;
float Po = 0;
float PH_step;

unsigned long sendDataPrevMillis = 0;
bool signupOK = false;

bool ledStatus = false;
bool fanStatus = false;

void connectToWiFi() {
    lcd.setCursor(0, 0);
    lcd.print(F("Hubungkan Wi-Fi"));
    lcd.setCursor(0, 2);
    lcd.print(F("Aquascape-AP"));
    lcd.setCursor(0, 3);
    lcd.print(F("PW: 11223344"));
    WiFiManager wifiManager;
    if (!wifiManager.autoConnect("Aquascape-AP", "11223344")) {
        Serial.println(F("Gagal terhubung dan tidak ada timeout."));
        delay(3000);
        ESP.restart();
    }
    Serial.println(F("Terhubung ke Wi-Fi!"));
    Serial.println(WiFi.localIP());
    lcd.clear();
    lcd.setCursor(0, 1);
    lcd.print(F("Terhubung ke Wi-Fi"));
}

void handleFirebaseConnection() {
    if (Firebase.signUp(&config, &auth, "", "")) {
        signupOK = true;
        Firebase.begin(&config, &auth);
        Firebase.reconnectWiFi(true);
    } else {
        Serial.printf("Kesalahan Pendaftaran Firebase: %s\n", config.signer.signupError.message.c_str());
        delay(5000);
        handleFirebaseConnection();
    }
}

void updateRelayStatus() {
    if (Firebase.RTDB.getBool(&fbdo, "relays/led")) {
        ledStatus = fbdo.boolData();
        digitalWrite(RELAY_LED_PIN, ledStatus ? HIGH : LOW);
        Serial.println(ledStatus ? "Lampu HIDUP" : "Lampu MATI");
    } else {
        Serial.println("Gagal mendapatkan status lampu dari Firebase.");
    }

    if (Firebase.RTDB.getBool(&fbdo, "relays/fan")) {
        fanStatus = fbdo.boolData();
        digitalWrite(RELAY_FAN_PIN, fanStatus ? HIGH : LOW);
        Serial.println(fanStatus ? "Kipas HIDUP" : "Kipas MATI");
    } else {
        Serial.println("Gagal mendapatkan status kipas dari Firebase.");
    }
}

void setRGBColor(float temperature) {
    if (temperature < 20) {
        analogWrite(RED_PIN, 0);
        analogWrite(GREEN_PIN, 0);
        analogWrite(BLUE_PIN, 255);
    } else if (temperature < 32) { // Suhu sedang (hijau)
        analogWrite(RED_PIN, 0);
        analogWrite(GREEN_PIN, 255);
        analogWrite(BLUE_PIN, 0);
    } else {
        analogWrite(RED_PIN, 255);
        analogWrite(GREEN_PIN, 0);
        analogWrite(BLUE_PIN, 0);
    }
}

void displayData(int temperature, int pH, bool ledStatus, bool fanStatus) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(F("Suhu :"));
    lcd.print(temperature);
    lcd.write(0xDF);
    lcd.print(F("C"));

    lcd.setCursor(0, 1);
    lcd.print(F("pH   : "));
    lcd.print(pH);
    lcd.print(F("     "));

    lcd.setCursor(0, 2);
    lcd.print(F("Lampu: "));
    lcd.print(ledStatus ? F("ON  ") : F("OFF "));
    
    lcd.setCursor(0, 3);
    lcd.print(F("Kipas: "));
    lcd.print(fanStatus ? F("ON  ") : F("OFF "));

    delay(2000);
}

void setup() {
    Serial.begin(115200);
    
    pinMode(PH_SENSOR_PIN, INPUT);
    pinMode(RELAY_LED_PIN, OUTPUT);
    pinMode(RELAY_FAN_PIN, OUTPUT);
    digitalWrite(RELAY_LED_PIN, LOW);
    digitalWrite(RELAY_FAN_PIN, LOW);

    pinMode(RED_PIN, OUTPUT);
    pinMode(GREEN_PIN, OUTPUT);
    pinMode(BLUE_PIN, OUTPUT);
    
    lcd.init();
    lcd.backlight();

    lcd.clear();
    lcd.setCursor(3, 0);
    lcd.print(F("Selamat datang"));
    lcd.setCursor(2, 1);
    lcd.print(F("Smart Aquascape!"));
    delay(2000);
    lcd.clear();

    connectToWiFi();

    config.api_key = API_KEY;
    config.database_url = DATABASE_URL;
    // Hapus atau definisikan tokenStatusCallback jika tidak digunakan
    // config.token_status_callback = tokenStatusCallback;

    handleFirebaseConnection();

    sensors.begin();
}

void loop() {
    if (Firebase.ready() && signupOK && (millis() - sendDataPrevMillis > 2000 || sendDataPrevMillis == 0)) {
        sendDataPrevMillis = millis();
        
        nilai_analog_PH = analogRead(PH_SENSOR_PIN);
        TeganganPh = 3.3 / 4096.0 * nilai_analog_PH;
        PH_step = (PH4 - PH9) / 5.17;
        Po = 7.00 + ((PH9 - TeganganPh) / PH_step);

        Serial.print(F("Nilai ADC PH = "));
        Serial.println(nilai_analog_PH);
        Serial.print(F("TeganganPh = "));
        Serial.println(TeganganPh, 3);
        Serial.print(F("Nilai PH cairan = "));
        Serial.println(Po, 2);

        sensors.requestTemperatures();
        float temperature = sensors.getTempCByIndex(0);

        setRGBColor(temperature);

        updateRelayStatus();

        int tempInt = static_cast<int>(temperature);
        int pHInt = static_cast<int>(Po);

        displayData(tempInt, pHInt, ledStatus, fanStatus);

        bool dataSent = false;
        int retryCount = 0;

        while (!dataSent && retryCount < 3) {
            if (Firebase.RTDB.setInt(&fbdo, "sensors/ph", pHInt) &&
                Firebase.RTDB.setInt(&fbdo, "sensors/temperature", tempInt)) {
                Serial.println(F("Nilai pH dan suhu telah dikirim ke Firebase."));
                dataSent = true;
            } else {
                // Konversi MB_String ke String atau const char* sebelum mencetak
                Serial.println(String(F("GAGAL mengirim data. Alasan: ")) + fbdo.errorReason().c_str());
                retryCount++;
                delay(1000);
            }
        }

        delay(3000);
    }
}
