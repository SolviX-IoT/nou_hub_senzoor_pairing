#include "NetLink.h"
#include "SensorLink.h"

// Adresa MAC a hub-ului in reteaua locala. Declarata extern in Config.h
// si definita AICI, in singurul fisier care vorbeste cu ENC28J60.
byte HUB_MAC[6] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };

namespace NetLink {

  static bool          s_up = false;
  static unsigned long s_lastMaintain = 0;
  static unsigned long s_opened = 0;
  static unsigned long s_closed = 0;
  static unsigned long s_renewals = 0;
  static unsigned long s_dhcpTimeout = ETH_DHCP_TIMEOUT_MS;
  static unsigned long s_dhcpResponse = ETH_DHCP_RESPONSE_MS;

  bool isUp() { return s_up; }

  unsigned long connectionsOpened() { return s_opened; }
  unsigned long connectionsClosed() { return s_closed; }
  unsigned long dhcpRenewals()      { return s_renewals; }

#if HUB_NET_TRANSPORT == HUB_NET_ETHERNET

  // ===================================================================
  // ETHERNET - ENC28J60, pe magistrala SPI partajata cu LoRa
  // ===================================================================

  // Un singur client, static. Vezi acquireClient() in NetLink.h.
  static EthernetClient s_client;
  static bool           s_clientHeld = false;

  const char* transportName() { return "Ethernet ENC28J60"; }

  bool begin(unsigned long timeoutMs, unsigned long responseMs) {
    s_dhcpTimeout  = timeoutMs;
    s_dhcpResponse = responseMs;

    SpiBus::claimEthernet();
    SpiBus::resetEthernetModule();

    // Ethernet.init spune libariei ce pin foloseste drept CS. Nu apelam
    // SPI.begin aici: magistrala este deja pornita de SpiBus::begin().
    Ethernet.init(PIN_ETH_CS);

    Serial.println(F("Cer un IP prin DHCP..."));

    /*
     * Cei doi timeouts nu sunt doar pentru pornire. Ethernet.maintain()
     * ii refoloseste la reinnoirea lease-ului, iar acolo schimbul DHCP
     * este BLOCANT: valorile de aici marginesc cat poate sta hub-ul surd
     * la zile dupa pornire (vezi ETH_DHCP_TIMEOUT_MS in Config.h).
     */
    s_up = (Ethernet.begin(HUB_MAC, timeoutMs, responseMs) != 0);

    SpiBus::deselectAll();

    if (!s_up) {
      Serial.println(F("DHCP a esuat - hub-ul merge mai departe FARA retea."));
      Serial.println(F("Senzorii se inroleaza si se citesc normal; doar cloud-ul asteapta."));
      Serial.println(F("Verifica: cablul UTP, routerul, si alimentarea modulului ENC28J60."));
      return false;
    }

    printStatus();
    return true;
  }

  bool retry() {
    if (s_up) return true;
    return begin(s_dhcpTimeout, s_dhcpResponse);
  }

  IPAddress localIP() {
    if (!s_up) return IPAddress(0, 0, 0, 0);
    SpiBus::claimEthernet();
    IPAddress ip = Ethernet.localIP();
    SpiBus::deselectAll();
    return ip;
  }

  void printStatus() {
    Serial.print(F("Transport: "));
    Serial.println(transportName());

    if (!s_up) {
      Serial.println(F("Stare    : FARA ADRESA (DHCP nereusit)"));
      return;
    }

    SpiBus::claimEthernet();
    Serial.print(F("IP local : ")); Serial.println(Ethernet.localIP());
    Serial.print(F("Masca    : ")); Serial.println(Ethernet.subnetMask());
    Serial.print(F("Gateway  : ")); Serial.println(Ethernet.gatewayIP());
    Serial.print(F("DNS      : ")); Serial.println(Ethernet.dnsServerIP());
    SpiBus::deselectAll();

    Serial.print(F("Conexiuni: "));
    Serial.print(s_opened);
    Serial.print(F(" deschise, "));
    Serial.print(s_closed);
    Serial.print(F(" inchise"));
    if (s_opened - s_closed > 1) {
      Serial.print(F("  <-- SCURGERE! EthernetENC are doar 4 conexiuni."));
    }
    Serial.println();

    Serial.print(F("Reinnoiri DHCP: "));
    Serial.println(s_renewals);
  }

  bool resolve(const char* host, IPAddress& out) {
    if (!s_up) return false;
    SpiBus::claimEthernet();
    bool ok = (Ethernet.hostByName(host, out) == 1);
    SpiBus::deselectAll();
    return ok;
  }

  void maintain() {
    if (!s_up) return;

    if (millis() - s_lastMaintain < ETH_MAINTAIN_EVERY_MS) return;

    /*
     * Nu peste fereastra de downlink a unui senzor care tocmai a vorbit.
     * De obicei maintain() nu face nimic si costa microsecunde, dar la
     * expirarea lui T1 face un schimb DHCP blocant - si acela ar cadea
     * exact peste singurele 600 ms in care senzorul asculta.
     */
    if (millis() - SensorLink::lastRxMs() < HTTP_QUIET_AFTER_RX_MS) return;

    s_lastMaintain = millis();

    SpiBus::claimEthernet();
    unsigned long started = millis();
    int result = Ethernet.maintain();
    unsigned long elapsed = millis() - started;
    SpiBus::deselectAll();

    // 1 = renew esuat, 2 = renew reusit, 3 = rebind esuat, 4 = rebind ok
    if (result == 2 || result == 4) {
      s_renewals++;
      Serial.print(F("[NET] Lease DHCP reinnoit. IP: "));
      Serial.println(localIP());
    }
    else if (result == 1 || result == 3) {
      Serial.println(F("[NET] Reinnoirea lease-ului DHCP a esuat. Incerc mai departe."));
    }

    /*
     * Masuram, nu presupunem. Daca reinnoirea chiar blocheaza, vrem
     * linia asta in jurnal prima data cand se intampla pe hardware real,
     * nu o discutie despre cat AR PUTEA sa dureze.
     */
    if (elapsed >= ETH_MAINTAIN_WARN_MS) {
      Serial.print(F("[NET] ATENTIE: Ethernet.maintain() a blocat "));
      Serial.print(elapsed);
      Serial.println(F(" ms. Pachetele din intervalul asta s-au pierdut."));
    }
  }

  Client* acquireClient(unsigned long connectTimeoutMs) {
    if (!s_up) return NULL;

    if (s_clientHeld) {
      // Nu se poate intampla cu o singura cerere la un moment dat, dar
      // daca se intampla vreodata, o conexiune scursa este mai rea decat
      // un mesaj: EthernetENC are patru cu totul.
      Serial.println(F("[NET] BUG: acquireClient() chemat de doua ori fara release."));
      return NULL;
    }

    SpiBus::claimEthernet();

    // Fara asta, fiecare incercare catre o ruta moarta costa cele 5000 ms
    // implicite ale libariei, in care hub-ul este complet surd.
    s_client.setConnectionTimeout(connectTimeoutMs);

    s_clientHeld = true;
    s_opened++;
    return &s_client;
  }

  void releaseClient(Client* client) {
    if (client == NULL) return;

    // stop() OBLIGATORIU, pe orice cale de iesire: cele patru conexiuni
    // ale lui EthernetENC nu se elibereaza singure, iar a patra scursa
    // lasa hub-ul fara retea pana la repornire.
    client->stop();
    s_closed++;
    s_clientHeld = false;

    SpiBus::deselectAll();
  }

#else
  #error "HUB_NET_TRANSPORT: WiFi nu este inca implementat. Vezi NetLink.h."
#endif
}
