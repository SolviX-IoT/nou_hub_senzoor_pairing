#include "LoRaRadio.h"

namespace LoRaRadio {

  static bool s_ready = false;

  bool isReady() { return s_ready; }

  bool begin(long frequency) {
    SpiBus::claimLoRa();
    SpiBus::resetLoRaModule();

    LoRa.setPins(PIN_LORA_NSS, PIN_LORA_RST, PIN_LORA_DIO0);

    // LoRa.begin() apeleaza intern SPI.begin() fara argumente. Pe ESP32,
    // apelul este ignorat daca magistrala a fost deja initializata de
    // SpiBus::begin(), deci maparea noastra de pini ramane valabila.
    s_ready = LoRa.begin(frequency);

    if (!s_ready) {
      Serial.println(F("EROARE: modulul LoRa nu a fost gasit."));
      Serial.println(F("Verifica NSS (GPIO5), RST (GPIO14), DIO0 (GPIO26) si alimentarea."));
      SpiBus::deselectAll();
      return false;
    }

    // Parametrii de modulatie, din Config.h. Sunt aplicati explicit, nu
    // lasati pe seama valorilor implicite ale librariei: nodul senzor ii
    // scrie direct in registrele SX1276, iar cele doua capete trebuie sa
    // coincida exact. Vezi comentariul detaliat din Config.h.
    LoRa.setSpreadingFactor(LORA_SPREADING_FACTOR);
    LoRa.setSignalBandwidth(LORA_BANDWIDTH_HZ);
    LoRa.setCodingRate4(LORA_CODING_RATE_4);
    LoRa.setPreambleLength(LORA_PREAMBLE_LENGTH);
    LoRa.setSyncWord(LORA_SYNC_WORD);
    LoRa.enableCrc();                 // senzorul emite cu CRC activ
    LoRa.setTxPower(LORA_TX_POWER_DBM, PA_OUTPUT_PA_BOOST_PIN);

    SpiBus::deselectAll();
    return s_ready;
  }

  bool sendText(const String& text) {
    if (!s_ready) return false;

    SpiBus::claimLoRa();
    bool ok = false;
    if (LoRa.beginPacket()) {
      LoRa.print(text);
      ok = (LoRa.endPacket() == 1);
    }
    SpiBus::deselectAll();
    return ok;
  }

  bool sendRaw(const uint8_t* data, uint8_t length) {
    if (!s_ready) return false;

    SpiBus::claimLoRa();
    bool ok = false;
    if (LoRa.beginPacket()) {
      // LoRa.write(buffer, size) scrie octetii asa cum sunt, inclusiv
      // 0x00 - spre deosebire de LoRa.print(String).
      LoRa.write(data, length);
      ok = (LoRa.endPacket() == 1);
    }
    SpiBus::deselectAll();

    // Dupa emisie, libraria lasa radioul in standby. Testele care asculta
    // reintra in receptie la urmatorul parsePacket(), deci nu e nevoie de
    // nimic aici.
    return ok;
  }

  bool receive(String& out, int& rssi, float& snr) {
    if (!s_ready) return false;

    SpiBus::claimLoRa();
    bool got = false;
    int size = LoRa.parsePacket();
    if (size > 0) {
      out = "";
      while (LoRa.available()) {
        out += (char)LoRa.read();
      }
      rssi = LoRa.packetRssi();
      snr  = LoRa.packetSnr();
      got = true;
    }
    SpiBus::deselectAll();
    return got;
  }

  bool receiveRaw(uint8_t* buffer, int maxLength, int& length,
                  int& rssi, float& snr) {
    if (!s_ready) return false;

    SpiBus::claimLoRa();
    bool got = false;
    int size = LoRa.parsePacket();
    if (size > 0) {
      length = 0;
      // Citim tot ce a venit, dar nu peste marginea bufferului. Un
      // pachet mai lung decat maxLength ramane cu octetii in plus
      // necititi; apelantul vede o lungime egala cu maxLength si il
      // respinge oricum la validare.
      while (LoRa.available() && length < maxLength) {
        buffer[length++] = (uint8_t)LoRa.read();
      }
      rssi = LoRa.packetRssi();
      snr  = LoRa.packetSnr();
      got = true;
    }
    SpiBus::deselectAll();
    return got;
  }

  void sleep() {
    if (!s_ready) return;
    SpiBus::claimLoRa();
    LoRa.sleep();          // NU LoRa.end(): acela ar inchide SPI-ul comun
    SpiBus::deselectAll();
  }
}
