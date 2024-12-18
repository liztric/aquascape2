#include <Arduino.h>
#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <WiFiManager.h>
#include <BH1750.h>

#define API_KEY "AIzaSyBgD_196K9e0NmyVbGtHxlyVAdgpGu5Yyo"
#define DATABASE_URL "https://aquascape-ffef6-default-rtdb.asia-southeast1.firebasedatabase.app/"
#define PH_SENSOR_PIN 34
#define TURBIDITY_PIN 35
#define ONE_WIRE_BUS 32

#define RELAY_LED_PIN 5  // GPIO untuk relay LED
#define RELAY_FAN_PIN 18  // GPIO untuk relay kipas
#define RELAY_AERATOR_PIN 19  // GPIO untuk relay aerator

#define RED_PIN 27
#define GREEN_PIN 26
#define BLUE_PIN 25

#define SDA_PIN 21
#define SCL_PIN 22

#define LCD_I2C_ADDR 0x27  // Alamat default LCD I2C
#define BH1750_I2C_ADDR 0x23  // Alamat default BH1750 (bisa juga 0x5C)

#define BTN_MODE_PIN 14
#define BTN_LED_PIN 4
#define BTN_AERATORS_PIN 2
#define BTN_FAN_PIN 15

FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

LiquidCrystal_I2C lcd(0x27, 20, 4);
BH1750 lightMeter;

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
bool aeratorStatus = false;

unsigned long timerDuration = 0; // Inisialisasi tanpa nilai default
unsigned long previousTimerDuration = 0; // Untuk menyimpan nilai sebelumnya
unsigned long startTime = 0;
bool isLightOn = false;

// Tambahkan di bagian deklarasi variabel global
unsigned long lastResetTime = 0;  // Waktu terakhir reset
const unsigned long RESET_INTERVAL = 24 * 60 * 60 * 1000; // 24 jam dalam milidetik
bool isAutoMode = true; // Default mode otomatis

// Tambahkan variabel global untuk batas suhu
int batasSuhuRendah = 30;  // default value
int batasSuhuTinggi = 40;  // default value

// Fungsi untuk membaca kejernihan berdasarkan nilai tegangan turbidity
int readClarity() {
    int nilai_analog_turbidity = analogRead(TURBIDITY_PIN);
    int tegangan_turbidity = map(nilai_analog_turbidity, 0, 4095, 0, 3300);
    int clarity_percentage = map(tegangan_turbidity, 400, 3000, 0, 100);
    
    // Batasi nilai antara 0-100
    if (clarity_percentage < 0) clarity_percentage = 0;
    if (clarity_percentage > 100) clarity_percentage = 100;
    
    return clarity_percentage;
}

// Tambahkan fungsi untuk mendapatkan status kejernihan berdasarkan persentase
int getClarityStatus(int clarity_percentage) {
    return clarity_percentage; // Mengembalikan nilai persentase kejernihan
}

// Fungsi untuk koneksi Wi-Fi
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

// Fungsi untuk koneksi ke Firebase
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

// Fungsi untuk memperbarui status relay dari Firebase
void updateRelayStatus() {
    // Cek mode otomatis/manual dari Firebase
    if (Firebase.RTDB.getBool(&fbdo, "mode/otomatis")) {
        isAutoMode = fbdo.boolData();
        Serial.println(isAutoMode ? "Mode: Otomatis" : "Mode: Manual");
    }

    // Jika mode manual, ambil status relay dari Firebase
    if (!isAutoMode) {
        if (Firebase.RTDB.getBool(&fbdo, "relays/led")) {
            ledStatus = fbdo.boolData();
            digitalWrite(RELAY_LED_PIN, ledStatus ? LOW : HIGH);
            Serial.println(ledStatus ? "Lampu HIDUP" : "Lampu MATI");
        }

        if (Firebase.RTDB.getBool(&fbdo, "relays/fan")) {
            fanStatus = fbdo.boolData();
            digitalWrite(RELAY_FAN_PIN, fanStatus ? LOW : HIGH);
            Serial.println(fanStatus ? "Kipas HIDUP" : "Kipas MATI");
        }
    }
}

// Fungsi untuk mengatur warna RGB dan kipas berdasarkan suhu
void setRGBColor(int temperature) {
    if (temperature < batasSuhuRendah) {
        // Suhu dingin - LED biru - Kipas mati
        analogWrite(RED_PIN, 0);
        analogWrite(GREEN_PIN, 0);
        analogWrite(BLUE_PIN, 255);
        if (isAutoMode) {
            fanStatus = false;
            digitalWrite(RELAY_FAN_PIN, HIGH);
            Firebase.RTDB.setBool(&fbdo, "relays/fan", fanStatus);
        }
    } else if (temperature < batasSuhuTinggi) {
        // Suhu normal - LED hijau - Kipas mati
        analogWrite(RED_PIN, 0);
        analogWrite(GREEN_PIN, 255);
        analogWrite(BLUE_PIN, 0);
        if (isAutoMode) {
            fanStatus = false;
            digitalWrite(RELAY_FAN_PIN, HIGH);
            Firebase.RTDB.setBool(&fbdo, "relays/fan", fanStatus);
        }
    } else {
        // Suhu panas - LED merah - Kipas hidup
        analogWrite(RED_PIN, 255);
        analogWrite(GREEN_PIN, 0);
        analogWrite(BLUE_PIN, 0);
        if (isAutoMode) {
            fanStatus = true;
            digitalWrite(RELAY_FAN_PIN, LOW);
            Firebase.RTDB.setBool(&fbdo, "relays/fan", fanStatus);
        }
    }
}

// Fungsi untuk menampilkan data di LCD
void displayData(int temperature, int pH, int lux, String clarity, bool ledStatus, bool fanStatus, unsigned long remainingTime) {
    unsigned long hours = remainingTime / (60 * 60 * 1000);
    unsigned long minutes = (remainingTime % (60 * 60 * 1000)) / (60 * 1000);
    unsigned long seconds = (remainingTime % (60 * 1000)) / 1000;

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(F("T: "));
    lcd.print(hours);
    lcd.print(F("h "));
    lcd.print(minutes);
    lcd.print(F("m "));
    lcd.print(seconds);
    lcd.print(F("s"));

    lcd.setCursor(0, 1);
    lcd.print(F("Air :"));
    lcd.print(clarity);
    lcd.print(F("%"));

    lcd.setCursor(11, 1);
    lcd.print(F("| pH:"));
    lcd.print(pH);

    lcd.setCursor(0, 2);
    lcd.print(F("Suhu:"));
    lcd.print(temperature);
    lcd.write(0xDF);
    lcd.print(F("C"));

    lcd.setCursor(0, 3);
    lcd.print(F("Lux :"));
    lcd.print(lux);

    lcd.setCursor(11, 2);
    lcd.print(F("| Led:"));
    lcd.print(ledStatus ? F("ON") : F("OFF"));

    lcd.setCursor(11, 3);
    lcd.print(F("| Fan:"));
    lcd.print(fanStatus ? F("ON") : F("OFF"));

    // Tambahkan indikator mode
    lcd.setCursor(19, 0);
    lcd.print(isAutoMode ? F("A") : F("M")); // A untuk Otomatis, M untuk Manual

    delay(1000);
}

void scanI2CDevices() {
    byte error, address;
    int nDevices = 0;
    
    Serial.println("\nMemindai perangkat I2C...");
    
    for(address = 1; address < 127; address++) {
        Wire.beginTransmission(address);
        error = Wire.endTransmission();
        
        if (error == 0) {
            Serial.print("Perangkat I2C ditemukan di alamat 0x");
            if (address < 16) {
                Serial.print("0");
            }
            Serial.println(address, HEX);
            nDevices++;
        }
    }
    
    if (nDevices == 0) {
        Serial.println("Tidak ada perangkat I2C yang ditemukan\n");
    } else {
        Serial.printf("Ditemukan %d perangkat I2C\n\n", nDevices);
    }
}

void setup() {
    Serial.begin(115200);

    pinMode(PH_SENSOR_PIN, INPUT);
    pinMode(TURBIDITY_PIN, INPUT);
    pinMode(RELAY_LED_PIN, OUTPUT);
    pinMode(RELAY_FAN_PIN, OUTPUT);
    pinMode(RELAY_AERATOR_PIN, OUTPUT);
    digitalWrite(RELAY_LED_PIN, HIGH);
    digitalWrite(RELAY_FAN_PIN, HIGH);
    digitalWrite(RELAY_AERATOR_PIN, HIGH);

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

    handleFirebaseConnection();

    sensors.begin();
    Wire.begin(SDA_PIN, SCL_PIN);
    scanI2CDevices();
    
    byte retries = 0;
    while (retries < 3) {
        if (lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE)) {
            Serial.println("BH1750 berhasil diinisialisasi!");
            break;
        }
        Serial.println("Error inisialisasi BH1750, mencoba lagi...");
        delay(1000);
        retries++;
    }
    
    if (retries == 3) {
        Serial.println("BH1750 gagal diinisialisasi setelah 3 kali percobaan");
    }

    // Ambil nilai timer dari Firebase
    if (Firebase.RTDB.getInt(&fbdo, "settings/timerDuration")) {
        timerDuration = fbdo.intData() * 60 * 60 * 1000; // Konversi jam ke milidetik
        previousTimerDuration = timerDuration; // Simpan nilai awal
    }

    // Ambil pengaturan batas suhu dari Firebase
    if (Firebase.RTDB.getString(&fbdo, "settings/batasSuhu")) {
        String batasSuhu = fbdo.stringData();
        sscanf(batasSuhu.c_str(), "%d,%d", &batasSuhuRendah, &batasSuhuTinggi);
        Serial.printf("Batas suhu diatur: %d-%d\n", batasSuhuRendah, batasSuhuTinggi);
    }

    // Inisialisasi pin tombol
    pinMode(BTN_MODE_PIN, INPUT_PULLUP);
    pinMode(BTN_LED_PIN, INPUT_PULLUP);
    pinMode(BTN_AERATORS_PIN, INPUT_PULLUP);
    pinMode(BTN_FAN_PIN, INPUT_PULLUP);
}

void loop() {
    if (Firebase.ready() && signupOK && (millis() - sendDataPrevMillis > 2000 || sendDataPrevMillis == 0)) {
        sendDataPrevMillis = millis();

        // Baca batas suhu dari Firebase secara realtime
        if (Firebase.RTDB.getString(&fbdo, "settings/batasSuhu")) {
            String batasSuhu = fbdo.stringData();
            int suhuRendah, suhuTinggi;
            if (sscanf(batasSuhu.c_str(), "%d,%d", &suhuRendah, &suhuTinggi) == 2) {
                if (batasSuhuRendah != suhuRendah || batasSuhuTinggi != suhuTinggi) {
                    batasSuhuRendah = suhuRendah;
                    batasSuhuTinggi = suhuTinggi;
                    Serial.printf("Batas suhu diperbarui: %d-%d\n", batasSuhuRendah, batasSuhuTinggi);
                }
            }
        }

        nilai_analog_PH = analogRead(PH_SENSOR_PIN);
        TeganganPh = 3.3 / 4096.0 * nilai_analog_PH;
        PH_step = (PH4 - PH9) / 5.0;
        Po = 7.00 + ((PH9 - TeganganPh) / PH_step);

        // Batasi nilai pH antara 0-14
        Po = constrain(Po, 0, 14);

        Serial.print("Nilai ADC PH = ");
        Serial.println(nilai_analog_PH);
        Serial.print("TeganganPh = ");
        Serial.println(TeganganPh, 3);
        Serial.print("Nilai PH cairan = ");
        Serial.println(Po, 2);

        sensors.requestTemperatures();
        int temperature = static_cast<int>(sensors.getTempCByIndex(0));
        int lux = 0;
        if (lightMeter.begin()) {
            delay(10); // Tambahkan delay kecil
            float luxReading = lightMeter.readLightLevel();
            if (luxReading >= 0) {
                lux = static_cast<int>(luxReading);
            }
            Serial.print("Nilai Lux: ");
            Serial.println(lux);
        } else {
            Serial.println("Error membaca BH1750");
        }
        
        // Dapatkan nilai persentase kejernihan
        int clarity_percentage = readClarity();
        // Dapatkan status kejernihan untuk display LCD
        String clarityStatus = String(getClarityStatus(clarity_percentage));

        // Hitung waktu yang tersisa
        unsigned long elapsedTime = millis() - startTime;
        unsigned long remainingTime = (timerDuration > elapsedTime) ? (timerDuration - elapsedTime) : 0;

        // Cek reset harian
        if (millis() - lastResetTime >= RESET_INTERVAL) {
            startTime = millis();
            lastResetTime = millis();
            Serial.println("Timer di-reset harian");
        }

        // Update mode otomatis/manual
        updateRelayStatus();

        if (isAutoMode) {
            // Mode Otomatis
            // Kontrol lampu berdasarkan timer dan cahaya
            if (timerDuration > 0 && remainingTime > 0) {
                // Timer aktif, cek intensitas cahaya
                if (lux < 300) {  // Jika cahaya kurang
                    if (!isLightOn) {
                        isLightOn = true;
                        ledStatus = true;
                        digitalWrite(RELAY_LED_PIN, LOW);
                        Firebase.RTDB.setBool(&fbdo, "relays/led", true);
                        Serial.println("Cahaya kurang - Lampu HIDUP");
                    }
                } else {  // Jika cahaya cukup
                    if (isLightOn) {
                        isLightOn = false;
                        ledStatus = false;
                        digitalWrite(RELAY_LED_PIN, HIGH);
                        Firebase.RTDB.setBool(&fbdo, "relays/led", false);
                        Serial.println("Cahaya cukup - Lampu MATI");
                    }
                }
            } else {
                // Timer habis atau tidak aktif
                if (isLightOn) {
                    isLightOn = false;
                    ledStatus = false;
                    digitalWrite(RELAY_LED_PIN, HIGH);
                    Firebase.RTDB.setBool(&fbdo, "relays/led", false);
                    Serial.println("Timer habis - Lampu MATI");
                }
            }

            // Periksa pembaruan timer dari Firebase (menggunakan satu field)
            if (Firebase.RTDB.getString(&fbdo, "settings/timer")) {
                String timerStr = fbdo.stringData();
                int hours = 0, minutes = 0;
                
                // Format yang diharapkan: "HH:mm"
                if (sscanf(timerStr.c_str(), "%d:%d", &hours, &minutes) == 2) {
                    unsigned long newTimerDuration = (hours * 60 + minutes) * 60 * 1000; // Konversi ke milidetik
                    
                    if (newTimerDuration != previousTimerDuration) {
                        timerDuration = newTimerDuration;
                        previousTimerDuration = newTimerDuration;
                        startTime = millis();
                        lastResetTime = millis();
                        
                        if (timerDuration == 0) {
                            isLightOn = false;
                            ledStatus = false;
                            digitalWrite(RELAY_LED_PIN, HIGH);
                            Firebase.RTDB.setBool(&fbdo, "relays/led", false);
                            Serial.println("Timer diatur ke 0 - Lampu MATI");
                        } else {
                            Serial.print("Timer diperbarui: ");
                            Serial.print(hours);
                            Serial.print(":");
                            Serial.print(minutes);
                            Serial.println();
                        }
                    }
                }
            }
        }

        setRGBColor(temperature);
        updateRelayStatus();

        int pHInt = static_cast<int>(Po);
        displayData(temperature, pHInt, lux, clarityStatus, ledStatus, fanStatus, remainingTime);

        bool dataSent = false;
        int retryCount = 0;

        while (!dataSent && retryCount < 3) {
            if (Firebase.RTDB.setInt(&fbdo, "sensors/clarity", clarity_percentage) && // Kirim persentase kejernihan
                Firebase.RTDB.setInt(&fbdo, "sensors/ph", pHInt) &&
                Firebase.RTDB.setInt(&fbdo, "sensors/temperature", temperature) &&
                Firebase.RTDB.setInt(&fbdo, "sensors/lux", lux)) {
                Serial.println(F("Data sensor telah dikirim ke Firebase."));
                dataSent = true;
            } else {
                Serial.printf("Kesalahan saat mengirim data ke Firebase: %s\n", fbdo.errorReason().c_str());
                retryCount++;
                delay(1000);
            }
        }
    }

    // Pembaruan status relay dari Firebase
    updateRelayStatus();

    // Cek status tombol
    if (digitalRead(BTN_MODE_PIN) == LOW) {
        // Ubah mode otomatis/manual
        isAutoMode = !isAutoMode;
        Firebase.RTDB.setBool(&fbdo, "mode/otomatis", isAutoMode);
        Serial.println(isAutoMode ? "Mode: Otomatis" : "Mode: Manual");
        delay(300); // Debounce delay
    }

    if (digitalRead(BTN_LED_PIN) == LOW) {
        // Ubah status relay LED
        ledStatus = !ledStatus;
        digitalWrite(RELAY_LED_PIN, ledStatus ? LOW : HIGH);
        Firebase.RTDB.setBool(&fbdo, "relays/led", ledStatus);
        Serial.println(ledStatus ? "LED: HIDUP" : "LED: MATI");
        delay(300); // Debounce delay
    }

    if (digitalRead(BTN_AERATORS_PIN) == LOW) {
        // Ubah status relay aerator
        aeratorStatus = !aeratorStatus; // Pastikan Anda mendeklarasikan aeratorStatus
        digitalWrite(RELAY_AERATOR_PIN, aeratorStatus ? LOW : HIGH);
        Firebase.RTDB.setBool(&fbdo, "relays/aerator", aeratorStatus);
        Serial.println(aeratorStatus ? "Aerator: HIDUP" : "Aerator: MATI");
        delay(300); // Debounce delay
    }

    if (digitalRead(BTN_FAN_PIN) == LOW) {
        // Ubah status relay fan
        fanStatus = !fanStatus;
        digitalWrite(RELAY_FAN_PIN, fanStatus ? LOW : HIGH);
        Firebase.RTDB.setBool(&fbdo, "relays/fan", fanStatus);
        Serial.println(fanStatus ? "Fan: HIDUP" : "Fan: MATI");
        delay(300); // Debounce delay
    }
}
