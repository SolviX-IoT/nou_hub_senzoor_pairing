/*
  LoRaRadio.h - invelis peste libraria LoRa (Sandeep Mistry) pentru SX1276.
  ---------------------------------------------------------------------
  Reguli respectate aici, toate legate de faptul ca modulul imparte
  magistrala SPI cu ENC28J60:

  - inainte de orice operatie se apeleaza SpiBus::claimLoRa(), care ridica
    CS-ul modulului Ethernet;
  - dupa terminarea operatiei, NSS-ul este ridicat inapoi, ca Ethernet-ul
    sa poata folosi bus-ul;
  - NU se apeleaza LoRa.end(). Acea functie inchide SPI-ul intregului
    ESP32, iar modulul Ethernet ar ramane fara ceas pana la un nou
    SPI.begin(). Pentru oprire se foloseste sleep();
  - receptia se face prin polling cu LoRa.parsePacket(), nu prin
    LoRa.onReceive(). Un callback ar accesa SPI dintr-o intrerupere,
    posibil in mijlocul unui transfer Ethernet aflat in desfasurare.
*/

#ifndef LORA_RADIO_H
#define LORA_RADIO_H

#include <Arduino.h>
#include <LoRa.h>
#include "Config.h"
#include "SpiBus.h"

namespace LoRaRadio {

  // Reset hardware + LoRa.begin() pe frecventa din Config.h, urmat de
  // aplicarea parametrilor de modulatie (SF, BW, CR, sync word, CRC).
  // Parametrii sunt tot din Config.h si trebuie sa ramana identici cu
  // cei ai nodului senzor - altfel nu se receptioneaza nimic, si fara
  // niciun mesaj de eroare.
  bool begin(long frequency = LORA_FREQUENCY);

  bool isReady();

  // Trimite un text. Intoarce true daca pachetul a fost predat radioului.
  bool sendText(const String& text);

  // Varianta binara a lui sendText(). Obligatorie pentru pachetele de
  // pairing: JOIN_ACCEPT si CMD_DOWN pot contine octeti 0x00 (adresa,
  // contoare), iar String i-ar trata drept terminator de sir - exact
  // problema care a dus la receiveRaw() (F-019), acum si pe emisie.
  bool sendRaw(const uint8_t* data, uint8_t length);

  // Verifica daca a sosit un pachet. Daca da, il pune in out si
  // completeaza rssi/snr. Nu blocheaza.
  bool receive(String& out, int& rssi, float& snr);

  // Varianta binara a lui receive(). Obligatorie pentru pachetele
  // nodului senzor: acolo un octet poate fi 0x00, iar String l-ar trata
  // drept terminator de sir. Pune in "length" numarul de octeti cititi,
  // cel mult maxLength. Nu blocheaza.
  bool receiveRaw(uint8_t* buffer, int maxLength, int& length,
                  int& rssi, float& snr);

  // Trece radioul in consum minim, fara sa inchida magistrala SPI.
  void sleep();
}

#endif // LORA_RADIO_H
