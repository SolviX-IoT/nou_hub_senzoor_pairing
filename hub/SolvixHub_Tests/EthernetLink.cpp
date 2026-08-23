#include "EthernetLink.h"

byte HUB_MAC[6] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };

namespace EthernetLink {

  static bool s_hasAddress = false;

  bool hasAddress() { return s_hasAddress; }

  bool begin(unsigned long timeoutMs) {
    SpiBus::claimEthernet();
    SpiBus::resetEthernetModule();

    // Ethernet.init spune libariei ce pin foloseste drept CS. Nu apelam
    // SPI.begin aici: magistrala este deja pornita de SpiBus::begin().
    Ethernet.init(PIN_ETH_CS);

    Serial.println(F("Cer un IP prin DHCP..."));
    // Al doilea parametru este timeout-ul; fara el, apelul poate bloca
    // mult timp si meniul serial devine inutilizabil.
    s_hasAddress = (Ethernet.begin(HUB_MAC, timeoutMs) != 0);

    if (!s_hasAddress) {
      Serial.println(F("EROARE: DHCP a esuat."));
      Serial.println(F("Verifica: cablul UTP, routerul, si daca testul 1 da EREVID valid."));
      return false;
    }

    printStatus();
    return true;
  }

  void printStatus() {
    SpiBus::claimEthernet();
    Serial.print(F("IP local: ")); Serial.println(Ethernet.localIP());
    Serial.print(F("Masca   : ")); Serial.println(Ethernet.subnetMask());
    Serial.print(F("Gateway : ")); Serial.println(Ethernet.gatewayIP());
    Serial.print(F("DNS     : ")); Serial.println(Ethernet.dnsServerIP());
  }

  bool resolve(const char* host, IPAddress& out) {
    SpiBus::claimEthernet();
    return Ethernet.hostByName(host, out) == 1;
  }

  bool httpPing(const char* host) {
    SpiBus::claimEthernet();

    Serial.print(F("["));
    Serial.print(millis() / 1000);
    Serial.print(F("s] Rezolv DNS pentru "));
    Serial.print(host);
    Serial.println(F(" ..."));

    IPAddress resolved;
    if (!resolve(host, resolved)) {
      Serial.println(F("  -> ESEC: numele nu a putut fi rezolvat."));
      Serial.println(F("     Verifica adresa de DNS primita prin DHCP."));
      return false;
    }

    Serial.print(F("  -> IP rezolvat: "));
    Serial.println(resolved);

    EthernetClient client;
    unsigned long start = millis();

    if (!client.connect(resolved, 80)) {
      Serial.print(F("  -> ESEC: conexiunea TCP a picat dupa "));
      Serial.print(millis() - start);
      Serial.println(F(" ms."));
      Serial.println(F(">>> REZULTAT: FARA INTERNET <<<"));
      Serial.println(F("    Are gateway-ul el insusi acces la internet?"));
      client.stop();
      return false;
    }

    Serial.print(F("  -> Conectat in "));
    Serial.print(millis() - start);
    Serial.println(F(" ms. Trimit cererea HTTP..."));

    client.print(F("GET / HTTP/1.1\r\nHost: "));
    client.print(host);
    client.print(F("\r\nConnection: close\r\n\r\n"));

    unsigned long waitStart = millis();
    while (!client.available() && millis() - waitStart < 5000) {
      delay(10);
    }

    bool ok = false;
    if (client.available()) {
      String firstLine = client.readStringUntil('\n');
      Serial.print(F("  -> Raspuns: "));
      Serial.println(firstLine);
      Serial.println(F(">>> REZULTAT: INTERNET FUNCTIONAL <<<"));
      ok = true;
    } else {
      Serial.println(F("  -> Conectat, dar niciun raspuns in 5 secunde."));
      Serial.println(F(">>> REZULTAT: PROBLEMA (posibil firewall sau DNS) <<<"));
    }

    client.stop();
    return ok;
  }

  void maintain() {
    if (!s_hasAddress) return;
    SpiBus::claimEthernet();
    Ethernet.maintain();
  }
}
