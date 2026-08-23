/*
  EthernetLink.h - invelis peste EthernetENC / ENC28J60.
  ---------------------------------------------------------------------
  Exista ca sa nu se repete codul de DHCP si de cerere HTTP in mai multe
  teste, si mai ales ca sa existe un singur loc care se ocupa de
  partajarea magistralei SPI cu modulul LoRa.

  Fiecare functie publica incepe prin SpiBus::claimEthernet(), care ridica
  NSS-ul modulului LoRa. Chiar daca libraria EthernetENC coboara singura
  CS_ETH cand are nevoie, garantia ca LoRa este deselectat trebuie data
  de noi - libraria nu stie ca mai exista un modul pe bus.

  Despre "ping": modulele ENC28J60 nu raspund la ICMP fara o librarie
  suplimentara, adesea instabila. O cerere HTTP GET este un test
  echivalent si mai de incredere: daca vine inapoi o linie de raspuns,
  internetul functioneaza cap-la-cap (DNS, rutare si TCP inclusiv).
*/

#ifndef ETHERNET_LINK_H
#define ETHERNET_LINK_H

#include <Arduino.h>
#include <EthernetENC.h>
#include "Config.h"
#include "SpiBus.h"

namespace EthernetLink {

  // Reset hardware + DHCP. Intoarce true daca s-a obtinut un IP.
  // timeoutMs limiteaza cat se asteapta raspunsul serverului DHCP.
  bool begin(unsigned long timeoutMs = 15000);

  bool hasAddress();
  void printStatus();

  // Rezolva un nume de gazda. Intoarce true la succes.
  bool resolve(const char* host, IPAddress& out);

  // Cerere HTTP GET catre host, port 80. Afiseaza pe Serial ce se
  // intampla si intoarce true daca a venit o linie de raspuns.
  bool httpPing(const char* host);

  // Trebuie apelata periodic ca sa se reinnoiasca lease-ul DHCP.
  void maintain();
}

#endif // ETHERNET_LINK_H
