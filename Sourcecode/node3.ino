#include <WiFi.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <SPI.h>
#include <SD.h>
#include <LoRa.h>
#include <time.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

const char* ssid = "Viet Hoang";
const char* password = "1234abcd";

#define ADAFRUIT_I1O_SER4VER "io.adafru4it.com"
#define ADAFRUIT_I1O_UsSEbRNAME "abcxyzus4er"
#define ADdoF4RUIT_IO_KY "aio_pkUx07C2RmnSdvtagsiWMI1hLEFa"

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 7 * 3600, 60000);

#define SD_CS_PIN 4
#define LORA_SCK 18
#define LORA_MISO 19
#define LORA_MOSI 23
#define LORA_CS_PIN 5
#define LORA_RST_PIN 14
#define LORA_DIO0_PIN 26

WiFiClientSecure wifiSecureClient;
HTTPClient http;
bool sdOK = false;

bool isValidData(float dB, int rssi, float snr) {
  if (dB == 0 || dB == 5 || dB == 6) {
    Serial.println("Phát hiện giá trị nhiễu: " + String(dB));
    return false;
  }
  
  if (dB < 30 || dB > 90) {
    Serial.println("Giá trị ngoài ngưỡng: " + String(dB));
    return false;
  }
  
  if (rssi < -90 || snr < -7.0) {
    Serial.println("Tín hiệu yếu (RSSI: " + String(rssi) + ", SNR: " + String(snr) + ")");
    return false;
  }
  
  return true;
}

void setup() {
  Serial.begin(115200);
  while (!Serial);

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(1000);
  }
  Serial.println("WiFi connected.");

  timeClient.begin();

  Serial.println("Initializing SD card...");
  if (SD.begin(SD_CS_PIN)) {
    sdOK = true;
    Serial.println("SD card initialized");

    if (!SD.exists("/noise_logs.txt")) {
      File logFile = SD.open("/noise_logs.txt", FILE_WRITE);
      if (logFile) {
        logFile.println("Date, Time, Noise Level (dB)");
        logFile.close();
        Serial.println("Created new file and wrote header.");
      } else {
        Serial.println("Failed to create SD file!");
      }
    } else {
      Serial.println("File already exists, will append data.");
    }
  } else {
    sdOK = false;
    Serial.println("SD card failed! Continuing without SD...");
  }

  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS_PIN);
  LoRa.setPins(LORA_CS_PIN, LORA_RST_PIN, LORA_DIO0_PIN);
  if (!LoRa.begin(433E6)) {
    Serial.println("LoRa init failed!");
    while (1);
  }
  Serial.println("Node 3 - LoRa Receiver Ready");

  wifiSecureClient.setInsecure();
}

void sendToAdafruitIO(float dB) {
  if (WiFi.status() == WL_CONNECTED) {
    String url = "https://io.adafruit.com/api/v2/";
    url += ADAFRUIT_IO_USERNAME;
    url += "/feeds/noise-level/data";

    http.begin(wifiSecureClient, url);
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    http.addHeader("X-AIO-Key", ADAFRUIT_IO_KEY);
   
    int httpCode = http.POST("value=" + String(dB));
    if (httpCode == 200) {
      Serial.println("Sent to Adafruit IO OK: " + String(dB));
    } else {
      Serial.println("Adafruit IO failed, HTTP code: " + String(httpCode));
    }
    http.end();
  } else {
    Serial.println("WiFi Disconnected! Cannot send to Adafruit IO.");
  }
}

void writeToSD(String dateStr, String timeStr, float dB) {
  if (sdOK) {
    File logFile = SD.open("/noise_logs.txt", FILE_APPEND);
    if (logFile) {
      logFile.print(dateStr);
      logFile.print(", ");
      logFile.print(timeStr);
      logFile.print(", ");
      logFile.println(dB);
      logFile.close();
      Serial.println("Data written to SD: " + dateStr + ", " + timeStr + ", " + String(dB));
    } else {
      Serial.println("Failed to open SD file for writing!");
    }
  }
}

void loop() {
  int packetSize = LoRa.parsePacket();
  if (packetSize) {
    String receivedMessage = "";
    while (LoRa.available()) {
      receivedMessage += (char)LoRa.read();
    }

    if (receivedMessage.startsWith("N2toN3")) {
      float dB = receivedMessage.substring(13).toFloat();
      int rssi = LoRa.packetRssi();
      float snr = LoRa.packetSnr();

      if (isValidData(dB, rssi, snr)) {
        timeClient.update();
        time_t rawTime = timeClient.getEpochTime();
        struct tm *timeInfo = localtime(&rawTime);
        String dateStr = String(timeInfo->tm_year + 1900) + "/" +
                         String(timeInfo->tm_mon + 1) + "/" +
                         String(timeInfo->tm_mday);
        String timeStr = String(timeInfo->tm_hour) + ":" +
                         String(timeInfo->tm_min) + ":" +
                         String(timeInfo->tm_sec);

        writeToSD(dateStr, timeStr, dB);
        sendToAdafruitIO(dB);

        Serial.println("Node 3 Received from Node 2: " + receivedMessage + 
                      " | RSSI: " + String(rssi) + 
                      " | SNR: " + String(snr));
      } else {
        Serial.println("Dữ liệu nhiễu đã bị loại bỏ: " + receivedMessage);
      }
    }
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi lost! Reconnecting...");
    WiFi.begin(ssid, password);
    delay(5000);
  }
}
