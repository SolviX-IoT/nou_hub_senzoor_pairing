/*
  SpiBus.h - arbitrajul magistralei SPI partajate.
  ---------------------------------------------------------------------
  PROBLEMA:
  Modulul Ethernet (ENC28J60) si modulul LoRa (SX1276) sunt legate pe
  aceiasi trei pini: SCK (18), MISO (19), MOSI (23). Fiecare are propriul
  chip select: CS_ETH = GPIO4, NSS_LoRa = GPIO5.

  Un slave SPI isi pune iesirea MISO in "high impedance" (adica se
  deconecteaza electric de pe fir) doar cat timp CS-ul lui este HIGH.
  Daca ambele CS-uri ajung LOW simultan, ambele module incearca sa
  scrie pe MISO in acelasi timp: datele sunt gunoi, iar pe termen lung
  se pot deteriora iesirile.

  REGULILE respectate de tot proiectul:
  1. SPI.begin(...) se apeleaza O SINGURA DATA, aici, la pornire.
     Niciun test nu reinitializeaza magistrala.
  2. Amandoua CS-urile sunt configurate ca OUTPUT si duse pe HIGH
     INAINTE de a exista trafic pe bus. Astfel niciun modul nu este
     selectat la boot, cand pinii ar fi altfel flotanti.
  3. Inainte ca un test sa preia un modul, se apeleaza claimEthernet()
     sau claimLoRa(). Functia ridica CS-ul celuilalt modul, garantat.
  4. Fiecare acces la bus se face in interiorul unei tranzactii SPI
     (SPI.beginTransaction / endTransaction). Asa fiecare modul isi
     impune propria viteza si propriul mod SPI fara sa il incurce pe
     celalalt. Librariile EthernetENC si LoRa fac deja acest lucru
     intern; clasa SpiGuard de mai jos face acelasi lucru pentru codul
     scris de noi (testul de registre ENC28J60).
  5. Nu se apeleaza NICIODATA LoRa.end() sau SPI.end(). LoRa.end()
     inchide magistrala SPI a intregului ESP32, iar modulul Ethernet ar
     ramane fara ceas. Pentru a "opri" LoRa se foloseste LoRa.sleep().
  6. Intreruperea DIO0 a modulului LoRa nu este folosita cu callback
     (LoRa.onReceive). Un callback ar declansa acces SPI din context de
     intrerupere, posibil chiar in mijlocul unui transfer Ethernet.
     Peste tot se citeste prin polling, cu LoRa.parsePacket().
*/

#ifndef SPI_BUS_H
#define SPI_BUS_H

#include <Arduino.h>
#include <SPI.h>
#include "Config.h"

namespace SpiBus {

  // Configureaza pinii CS, ii duce pe HIGH si porneste magistrala.
  // Apelata o singura data, din setup(). Apelurile ulterioare nu fac nimic.
  void begin();

  // Ridica ambele chip select-uri: niciun modul nu este selectat.
  void deselectAll();

  // Da magistrala unui singur modul, deselectandu-l explicit pe celalalt.
  void claimEthernet();
  void claimLoRa();

  // Reset hardware al ENC28J60 (puls LOW pe PIN_ETH_RESET).
  // Deselecteaza LoRa inainte, ca modulul sa nu prinda zgomot pe bus.
  void resetEthernetModule();

  // Reset hardware al SX1276 (puls LOW pe PIN_LORA_RST).
  void resetLoRaModule();
}

/*
  SpiGuard - deschide o tranzactie SPI si coboara CS-ul modulului dorit
  in constructor, iar in destructor ridica CS-ul si inchide tranzactia.
  Fiindca destructorul ruleaza automat la iesirea din bloc, este
  imposibil sa "uiti" CS-ul jos, inclusiv pe caile de return timpuriu.

  Folosire:
    {
      SpiGuard g(PIN_ETH_CS, ETH_SPI_HZ);
      SPI.transfer(...);
    }   // aici CS-ul urca si tranzactia se inchide, automat
*/
class SpiGuard {
public:
  SpiGuard(uint8_t csPin, uint32_t clockHz, uint8_t spiMode = SPI_MODE0)
    : _csPin(csPin) {
    SpiBus::deselectAll();
    SPI.beginTransaction(SPISettings(clockHz, MSBFIRST, spiMode));
    digitalWrite(_csPin, LOW);
  }

  ~SpiGuard() {
    digitalWrite(_csPin, HIGH);
    SPI.endTransaction();
  }

  // Ne asiguram ca nimeni nu copiaza obiectul: ar duce la doua
  // destructoare pentru un singur CS coborat.
  SpiGuard(const SpiGuard&) = delete;
  SpiGuard& operator=(const SpiGuard&) = delete;

private:
  uint8_t _csPin;
};

#endif // SPI_BUS_H
