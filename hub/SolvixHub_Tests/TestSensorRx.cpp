#include "TestSensorRx.h"

namespace TestSensorRx {

  // Cat timp de liniste inainte de un mesaj de semn de viata.
  static const unsigned long HEARTBEAT_MS = 15000;

  // Ceva mai mare decat SENSOR_PACKET_LEN, ca un pachet prea lung sa fie
  // citit si aratat, nu taiat in tacere.
  static const int RX_BUFFER_SIZE = 32;

  static bool s_ready = false;
  static unsigned long s_valid = 0;
  static unsigned long s_rejected = 0;
  static unsigned long s_lastActivity = 0;

  bool begin() {
    printTitle("SENZOR - RECEPTIE TEMPERATURA");
    Serial.println(F("Astept pachetele nodului senzor (PIC16LF1508)."));
    Serial.println(F("Pachet asteptat: 6 octeti, magic 0xA5, temperatura in sutimi de grad."));
    printSeparator();

    s_ready = LoRaRadio::begin();
    s_valid = 0;
    s_rejected = 0;
    s_lastActivity = millis();

    if (s_ready) {
      Serial.println(F("LoRa OK: 868.0 MHz, SF7, BW 125 kHz, CR 4/5, sync 0x12, CRC on."));
      Leds::set(PIN_LED_2, true);      // LED de stare: radioul asculta
    }

    return s_ready;
  }

  void tick() {
    if (!s_ready) return;

    Leds::service();                   // stinge pulsul precedent la scadenta

    uint8_t buffer[RX_BUFFER_SIZE];
    int   length = 0;
    int   rssi = 0;
    float snr = 0.0f;

    // Polling, nu callback pe DIO0: un callback ar accesa SPI din
    // context de intrerupere, posibil in mijlocul unui transfer Ethernet.
    if (LoRaRadio::receiveRaw(buffer, RX_BUFFER_SIZE, length, rssi, snr)) {
      SensorPacket packet;

      if (SensorPacketCodec::decode(buffer, length, packet)) {
        s_valid++;
        Leds::pulse(PIN_LED_1);        // LED de activitate

        Serial.print(F("[#"));
        Serial.print(s_valid);
        Serial.print(F("] "));
        SensorPacketCodec::print(packet);
        Serial.print(F("  RSSI: "));
        Serial.print(rssi);
        Serial.print(F(" dBm  SNR: "));
        Serial.print(snr, 1);
        Serial.println(F(" dB"));
      } else {
        // A venit ceva pe aceiasi parametri radio, dar nu este al
        // nostru sau este corupt. Il aratam ca sa se poata diagnostica.
        s_rejected++;
        SensorPacketCodec::printRaw(buffer, length);
        Serial.println();
      }

      s_lastActivity = millis();
    }
    else if (millis() - s_lastActivity >= HEARTBEAT_MS) {
      // Semn de viata: testul tace fiindca nu vine nimic, nu fiindca
      // s-a blocat.
      s_lastActivity = millis();
      Serial.print(F("...niciun pachet in ultimele 15 s.  (valide: "));
      Serial.print(s_valid);
      Serial.print(F(", respinse: "));
      Serial.print(s_rejected);
      Serial.println(F(")"));
    }

    delay(5);
  }

  void stop() {
    LoRaRadio::sleep();
    Leds::allOff();

    Serial.print(F("Receptie oprita. Pachete valide: "));
    Serial.print(s_valid);
    Serial.print(F(", respinse: "));
    Serial.println(s_rejected);
  }
}
