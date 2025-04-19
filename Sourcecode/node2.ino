#include <SPI.h>
#include <LoRa.h>

#define LORA_SCK 18
#define LORA_MISO 19
#define LORA_MOSI 23
#define LORA_CS_PIN 5
#define LORA_RST_PIN 14
#define LORA_DIO0_PIN 26

void setup() {
  Serial.begin(115200);
  while (!Serial);

  Serial.println("Node 2 - LoRa Relay");

  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS_PIN);
  LoRa.setPins(LORA_CS_PIN, LORA_RST_PIN, LORA_DIO0_PIN);

  if (!LoRa.begin(433E6)) {
    Serial.println("LoRa init failed!");
    while (1);
  }
  Serial.println("LoRa Relay Ready");
}

void loop() {
  int packetSize = LoRa.parsePacket();
  if (packetSize) {
    String receivedMessage = "";
    while (LoRa.available()) {
      receivedMessage += (char)LoRa.read();
    }

    if (receivedMessage.startsWith("N1toN2")) {
      Serial.println("Node 2 Received from Node 1: " + receivedMessage);

      String relayMessage = "N2toN3 " + receivedMessage.substring(7);
      LoRa.beginPacket();
      LoRa.print(relayMessage);
      LoRa.endPacket();

      Serial.println("Node 2 Sent to Node 3: " + relayMessage);
    }
  }
}
