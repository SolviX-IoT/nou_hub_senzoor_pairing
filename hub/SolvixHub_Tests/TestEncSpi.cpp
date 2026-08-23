#include "TestEncSpi.h"

namespace TestEncSpi {

  // Comenzi SPI ale ENC28J60
  static const uint8_t CMD_RCR = 0x00;  // Read Control Register
  static const uint8_t CMD_BFS = 0x80;  // Bit Field Set
  static const uint8_t CMD_BFC = 0xA0;  // Bit Field Clear

  // Registre "comune" - vizibile din orice banca
  static const uint8_t REG_EIE    = 0x1B;
  static const uint8_t REG_EIR    = 0x1C;
  static const uint8_t REG_ESTAT  = 0x1D;
  static const uint8_t REG_ECON2  = 0x1E;
  static const uint8_t REG_ECON1  = 0x1F;

  // EREVID se afla in Banca 3: trebuie comutata banca inainte de citire.
  static const uint8_t REG_EREVID = 0x12;

  static unsigned long s_lastRead = 0;

  static uint8_t readReg(uint8_t address) {
    SpiGuard guard(PIN_ETH_CS, ETH_SPI_HZ_DEBUG);
    SPI.transfer(CMD_RCR | (address & 0x1F));
    return SPI.transfer(0x00);
  }

  static void selectBank(uint8_t bank) {
    {
      SpiGuard guard(PIN_ETH_CS, ETH_SPI_HZ_DEBUG);
      SPI.transfer(CMD_BFC | REG_ECON1);
      SPI.transfer(0x03);              // sterge BSEL1:BSEL0
    }
    {
      SpiGuard guard(PIN_ETH_CS, ETH_SPI_HZ_DEBUG);
      SPI.transfer(CMD_BFS | REG_ECON1);
      SPI.transfer(bank & 0x03);       // scrie noua banca
    }
  }

  static void wiringTest() {
    Serial.println();
    Serial.println(F("--- PARTEA A: test cablaj (ambele CS = HIGH) ---"));
    Serial.println(F("Daca MISO repeta exact ce trimit pe MOSI => fire scurtcircuitate."));
    Serial.println();

    const uint8_t patterns[] = { 0x00, 0xFF, 0xAA, 0x55, 0x0F, 0xF0 };
    uint8_t echoes = 0;

    // Deschidem o tranzactie fara sa coboram niciun CS: nimeni nu asculta.
    SpiBus::deselectAll();
    SPI.beginTransaction(SPISettings(ETH_SPI_HZ_DEBUG, MSBFIRST, SPI_MODE0));
    for (uint8_t i = 0; i < sizeof(patterns); i++) {
      uint8_t sent = patterns[i];
      uint8_t received = SPI.transfer(sent);
      if (sent == received && sent != 0x00 && sent != 0xFF) echoes++;

      Serial.print(F("Trimit 0x"));
      if (sent < 0x10) Serial.print('0');
      Serial.print(sent, HEX);
      Serial.print(F("  ->  Primesc 0x"));
      if (received < 0x10) Serial.print('0');
      Serial.println(received, HEX);
    }
    SPI.endTransaction();

    Serial.println();
    if (echoes >= 2) {
      Serial.println(F(">> ALERTA: MISO reproduce MOSI. Verifica daca cele doua"));
      Serial.println(F(">> trasee nu sunt lipite intre ele pe placa."));
    } else {
      Serial.println(F(">> Cablaj: fara ecou evident. Trec la partea B."));
    }
  }

  bool begin() {
    printTitle("TEST 1 - DIAGNOSTIC SPI ENC28J60");
    Serial.println(F("Modulul LoRa este tinut deselectat (NSS = HIGH) pe toata durata."));

    SpiBus::claimEthernet();

    Serial.println(F("Reset hardware pe pinul de RESET al ENC28J60..."));
    SpiBus::resetEthernetModule();
    Serial.println(F("Reset terminat."));

    wiringTest();

    Serial.println();
    Serial.println(F("--- PARTEA B: citire registre reale (comanda RCR) ---"));
    Serial.println(F("Valori asteptate pe un ENC28J60 sanatos:"));
    Serial.println(F("  EREVID intre 0x01 si 0x07 (niciodata 0x00 sau 0xFF)"));
    Serial.println(F("  ESTAT bit0 (CLKRDY) = 1"));
    Serial.println();

    s_lastRead = 0;
    return true;
  }

  void tick() {
    if (millis() - s_lastRead < 2000 && s_lastRead != 0) return;
    s_lastRead = millis();

    uint8_t eie   = readReg(REG_EIE);
    uint8_t eir   = readReg(REG_EIR);
    uint8_t estat = readReg(REG_ESTAT);
    uint8_t econ2 = readReg(REG_ECON2);
    uint8_t econ1 = readReg(REG_ECON1);

    selectBank(3);
    uint8_t erevid = readReg(REG_EREVID);
    selectBank(0);

    printSeparator();
    printHexByte("EIE   ", eie);
    printHexByte("EIR   ", eir);
    printHexByte("ESTAT ", estat);
    printHexByte("ECON2 ", econ2);
    printHexByte("ECON1 ", econ1);
    printHexByte("EREVID", erevid);

    if (erevid == 0x00 || erevid == 0xFF) {
      Serial.println(F(">> EREVID invalid: chipul NU raspunde corect."));
      Serial.println(F(">> Verifica VCC, GND, CS_ETH si daca SI/SO nu sunt inversate."));
    } else {
      Serial.println(F(">> EREVID valid: comunicatia SPI functioneaza."));
    }

    if (estat & 0x01) {
      Serial.println(F(">> CLKRDY = 1: oscilatorul chipului este stabil."));
    } else {
      Serial.println(F(">> CLKRDY = 0: oscilatorul nu a pornit inca."));
    }
    Serial.println();
  }

  void stop() {
    SpiBus::deselectAll();
    Serial.println(F("Diagnostic SPI oprit, ambele CS-uri pe HIGH."));
  }
}
