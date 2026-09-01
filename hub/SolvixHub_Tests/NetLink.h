/*
  NetLink.h - legatura hub-ului cu reteaua.
  ---------------------------------------------------------------------
  Fisierul se numea EthernetLink.h si vorbea despre ENC28J60 in fiecare
  semnatura. Acum suprafata publica nu mai numeste transportul nicaieri:
  astazi este Ethernet, urmeaza WiFi, si codul care face cereri nu are de
  ce sa afle care dintre ele.

  CUM SE ADAUGA WiFi MAI TARZIU
  ---------------------------------------------------------------------
  Se schimba HUB_NET_TRANSPORT in Config.h si se scrie ramura #elif din
  NetLink.cpp, cu un WiFiClient static in loc de EthernetClient. NICIUN
  apelant nu se modifica, fiindca acquireClient() intoarce un Client*, iar
  EthernetClient si WiFiClient deriva amandoua din Client.

  DE CE REZERVAREA SPI STA AICI, SI NU IN Http.cpp
  ---------------------------------------------------------------------
  ENC28J60 imparte magistrala SPI cu modulul LoRa, deci inainte de orice
  atingere a lui trebuie chemat SpiBus::claimEthernet(). WiFi, in schimb,
  nu are nicio legatura cu SPI. Daca modulul de HTTP ar face claim-ul, ar
  trebui sa stie pe ce transport merge - adica exact ce incearca sa evite
  abstractia.

  Asa ca perechea acquireClient()/releaseClient() este si granita
  magistralei: prima rezerva bus-ul, a doua il elibereaza. Sub WiFi
  amandoua devin operatii goale. Consecinta, care trebuie respectata:
  INTREAGA cerere se desfasoara intre cele doua apeluri, fara sa se
  intoarca in loop() la mijloc. O sesiune intinsa pe mai multe tick()-uri
  ar lasa LoRa sa vorbeasca peste un transfer Ethernet inceput.

  CE A DISPARUT DE AICI
  ---------------------------------------------------------------------
  httpPing(), care facea un GET / pe portul 80 ca sa vada daca exista
  internet. Era singurul cod HTTP din proiect si era scris cu
  readStringUntil() si asteptari fixe de cate cinci secunde. A fost
  inlocuit de Http.*, care are timeouts, coduri de status si un buget.

  resolve() a ramas, desi API-ul de astazi este pe un IP brut si nu are
  nevoie de DNS. Va avea, in ziua in care serverul primeste un nume.
*/

#ifndef NET_LINK_H
#define NET_LINK_H

#include <Arduino.h>
#include <Client.h>
#include "Config.h"
#include "SpiBus.h"

#if HUB_NET_TRANSPORT == HUB_NET_ETHERNET
  #include <EthernetENC.h>
#endif

namespace NetLink {

  // Reset hardware + DHCP (pe Ethernet). Intoarce true daca s-a obtinut
  // o adresa. UN ESEC NU ESTE FATAL: hub-ul trebuie sa poata inrola
  // senzori si fara retea.
  bool begin(unsigned long timeoutMs  = ETH_DHCP_TIMEOUT_MS,
             unsigned long responseMs = ETH_DHCP_RESPONSE_MS);

  // true daca legatura are o adresa utilizabila.
  bool isUp();

  // Reincearca ridicarea legaturii daca begin() a esuat. Nu face nimic
  // daca legatura este deja sus.
  bool retry();

  /*
   * Intretinerea legaturii - pe Ethernet, reinnoirea lease-ului DHCP.
   *
   * Se autolimiteaza la ETH_MAINTAIN_EVERY_MS, deci poate fi chemata din
   * loop() la fiecare trecere. Nu se cheama in timpul unei ferestre de
   * downlink: la expirarea lui T1, Ethernet.maintain() face un schimb
   * DHCP BLOCANT (vezi ETH_DHCP_TIMEOUT_MS in Config.h). Chemarile care
   * depasesc ETH_MAINTAIN_WARN_MS se raporteaza pe Serial cu durata.
   */
  void maintain();

  IPAddress   localIP();
  void        printStatus();
  const char* transportName();

  bool resolve(const char* host, IPAddress& out);

  /*
   * Rezerva magistrala pentru transportul curent si intoarce clientul de
   * folosit. Intoarce NULL daca legatura nu este sus.
   *
   * Clientul este UNUL SINGUR si static: asa "exista cel mult o
   * conexiune la un moment dat" devine o proprietate a codului, nu o
   * speranta. Fiecare acquireClient() trebuie sa aiba perechea lui
   * releaseClient(), pe TOATE caile de iesire - EthernetENC are doar
   * patru conexiuni (UIP_CONNS) si nu le elibereaza singur.
   */
  Client* acquireClient(unsigned long connectTimeoutMs = HTTP_CONNECT_TIMEOUT_MS);

  // Inchide conexiunea si elibereaza magistrala.
  void releaseClient(Client* client);

  // Contoare pentru comanda `net`. Diferenta dintre deschise si inchise
  // trebuie sa fie 0 sau 1; orice altceva inseamna o conexiune scursa,
  // iar a patra scursa lasa hub-ul fara retea pana la repornire.
  unsigned long connectionsOpened();
  unsigned long connectionsClosed();
  unsigned long dhcpRenewals();
}

#endif // NET_LINK_H
