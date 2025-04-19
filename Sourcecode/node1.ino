#include <SPI.h>
#include <LoRa.h>
#include <esp_task_wdt.h>

#define SOUND_SENSOR_PIN      39
#define LORA_SCK              18
#define LORA_MISO             19
#define LORA_MOSI             23
#define LORA_CS_PIN           5
#define LORA_RST_PIN          14
#define LORA_DIO0_PIN         26
#define WATCHDOG_TIMEOUT_MS    150000
#define SAMPLE_BUFFER_SIZE    1000
#define resetCooldown         10*60*1000UL
#define loraRetryInterval     60000
#define samplingDuration      1000
#define interval              5000
#define resetInterval         1*60*60*1000UL

unsigned long previousMillis = 0;
unsigned long lastReset = 0;
unsigned long samplingStart = 0;
unsigned long lastLoRaRetry = 0;
unsigned long lastLoopCheck = 0;
unsigned long lastResetAttempt = 0;

int LoRaFailures = 0;
int resetCount = 0;
uint16_t maxADC = 0;
bool isSampling = false;

uint16_t sampleBuffer[SAMPLE_BUFFER_SIZE];
uint16_t sampleIndex = 0;
uint16_t sampleCount = 0;

void setupLoRa() {
  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS_PIN);
  LoRa.setPins(LORA_CS_PIN, LORA_RST_PIN, LORA_DIO0_PIN);
  
  digitalWrite(LORA_RST_PIN, LOW);
  delay(10);
  digitalWrite(LORA_RST_PIN, HIGH);
  delay(10);
  
  int attempts = 0;
  while (!LoRa.begin(433E6) && attempts < 5) {
    Serial.println("LoRa init failed! Retrying in 1s...");
    delay(1000);
    attempts++;
    esp_task_wdt_reset();
  }
  
  if (attempts >= 5) {
    Serial.println("LoRa init failed after 5 attempts, forcing restart...");
    ESP.restart();
  }
  
  Serial.println("LoRa Transmitter Ready");
  LoRaFailures = 0;
}

void setup() {
  Serial.begin(115200);
  while (!Serial);
  Serial.println("Node 1 - LoRa Transmitter - Noise Sensor");
  
  esp_task_wdt_config_t wdt_config = {
    .timeout_ms = WATCHDOG_TIMEOUT_MS,
    .idle_core_mask = 0,
    .trigger_panic = true
  };
  
  esp_task_wdt_init(&wdt_config);
  esp_task_wdt_add(NULL);
  
  pinMode(SOUND_SENSOR_PIN, INPUT);
  pinMode(LORA_RST_PIN, OUTPUT);
  analogSetAttenuation(ADC_11db);
  
  setupLoRa();
  
  lastReset = millis();
  lastLoRaRetry = millis();
  lastLoopCheck = millis();
  lastResetAttempt = millis();
}

int getPeakADC() {
  if (sampleCount == 0) return 1375;
  
  int peakValue = 0;
  for (int i = 0; i < sampleCount; i++) {
    if (sampleBuffer[i] >= 100 && sampleBuffer[i] <= 4000) {
      if (sampleBuffer[i] > peakValue) peakValue = sampleBuffer[i];
    }
  }
  
  Serial.print("Total samples collected: ");
  Serial.println(sampleCount);
  
  sampleCount = 0;
  sampleIndex = 0;
  
  if (peakValue < 800) peakValue = 800;
  return peakValue ? peakValue : 1375;
}

void loop() {
  esp_task_wdt_reset();
  unsigned long currentMillis = millis();
  
  if (currentMillis - lastLoopCheck > 10000) {
    Serial.println("Loop stuck detected, restarting...");
    ESP.restart();
  }
  
  lastLoopCheck = currentMillis;
  
  if (currentMillis - lastReset >= resetInterval || currentMillis < lastReset) {
    Serial.println("Performing scheduled restart after 1 hour...");
    ESP.restart();
  }
  
  if (currentMillis - previousMillis >= interval - samplingDuration && !isSampling) {
    isSampling = true;
    samplingStart = currentMillis;
    maxADC = 0;
    sampleCount = 0;
    sampleIndex = 0;
    Serial.println("Starting sampling...");
  }
  
  static unsigned long lastSample = 0;
  if (isSampling && currentMillis - samplingStart < samplingDuration && currentMillis - lastSample >= 1) {
    int value = analogRead(SOUND_SENSOR_PIN);
    if (sampleIndex < SAMPLE_BUFFER_SIZE) {
      sampleBuffer[sampleIndex++] = value;
      sampleCount++;
    }
    lastSample = currentMillis;
  }
  
  if (isSampling && currentMillis - samplingStart >= samplingDuration) {
    Serial.println("Sampling finished.");
  }
  
  if (currentMillis - previousMillis >= interval) {
    previousMillis = currentMillis;
    isSampling = false;
    
    int adcValue = getPeakADC();
    Serial.print("Peak ADC Value: ");
    Serial.println(adcValue);
    
    float dB = 40.0;
    if (adcValue < 1250) dB = 40 + (adcValue - 800) * (50.0 - 40.0) / (1250 - 800);
    else if (adcValue < 1750) dB = 50 + (adcValue - 1250) * (60.0 - 50.0) / (1750 - 1250);
    else if (adcValue < 2250) dB = 60 + (adcValue - 1750) * (70.0 - 60.0) / (2250 - 1750);
    else if (adcValue < 2750) dB = 70 + (adcValue - 2250) * (80.0 - 70.0) / (2750 - 2250);
    else if (adcValue < 3100) dB = 80 + (adcValue - 2750) * (90.0 - 80.0) / (3100 - 2750);
    else dB = 90;
    
    Serial.print("Calculated dB before sending: ");
    Serial.println(dB, 2);
    
    char LoRaMessage[50] = {0};
    snprintf(LoRaMessage, sizeof(LoRaMessage), "N1toN2 Noise: %.2f", dB);
    
    Serial.print("Sending: ");
    Serial.println(LoRaMessage);
    
    if (LoRa.beginPacket()) {
      LoRa.print(LoRaMessage);
      LoRa.endPacket();
      Serial.println("Sent successfully");
      LoRaFailures = 0;
    } else {
      Serial.println("LoRa send failed, will retry later...");
      LoRaFailures++;
      
      if (currentMillis - lastLoRaRetry >= loraRetryInterval && LoRaFailures < 5) {
        lastLoRaRetry = currentMillis;
        setupLoRa();
      } else if (LoRaFailures >= 5) {
        if (resetCount < 5 || (currentMillis - lastResetAttempt >= resetCooldown)) {
          Serial.println("Too many LoRa failures, restarting...");
          resetCount++;
          lastResetAttempt = currentMillis;
          ESP.restart();
        } else {
          Serial.println("Too many resets, waiting for cooldown...");
          LoRaFailures = 0;
        }
      }
    }
  }
}
