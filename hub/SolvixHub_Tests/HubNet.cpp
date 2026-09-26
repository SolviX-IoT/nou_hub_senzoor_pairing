/*
  HubNet.cpp - transportul catre server.
  ---------------------------------------------------------------------
  Module, in ordinea dependentelor, fiecare in namespace-ul lui:
    NetLink  legatura cu reteaua (Ethernet azi), granita magistralei SPI
    Http     cereri HTTP/1.1 peste un Client oarecare
*/

#include "HubNet.h"
#include "HubSensors.h"

// =====================================================================
//  NetLink
// =====================================================================

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

#if HUB_NET_TRANSPORT == HUB_NET_ETHERNET

  // ===================================================================
  // ETHERNET - ENC28J60, pe magistrala SPI partajata cu LoRa
  // ===================================================================

  // Un singur client, static. Vezi acquireClient() in HubNet.h.
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
  #error "HUB_NET_TRANSPORT: WiFi nu este inca implementat. Vezi HubNet.h."
#endif
}


// =====================================================================
//  Http
// =====================================================================

namespace Http {

  // Cel mai lung antet pe care il pastram intreg. Ne intereseaza doar
  // Content-Length si Transfer-Encoding; restul se citesc si se arunca,
  // dar tot trebuie consumate pana la sfarsitul liniei.
  static const size_t HEADER_LINE_MAX = 160;
  static const size_t STATUS_LINE_MAX = 64;

  // Un server stricat nu are voie sa ne tina intr-o bucla de antete.
  static const uint8_t HEADER_COUNT_MAX = 40;

  // -------------------------------------------------------------------
  // Ajutoare
  // -------------------------------------------------------------------

  static bool expired(unsigned long deadline) {
    return (long)(millis() - deadline) >= 0;
  }

  /*
   * Citeste o linie terminata cu \n, fara sa depaseasca bugetul.
   * \r final se taie. Intoarce numarul de octeti pusi in out, sau -1 la
   * timeout / inchidere fara linie completa.
   *
   * available() este chemat la fiecare trecere DINADINS: el este cel care
   * pompeaza stiva uIP. O bucla pe connected() singur nu avanseaza.
   */
  static int readLine(Client& c, char* out, size_t cap, unsigned long deadline) {
    size_t len = 0;

    while (!expired(deadline)) {
      if (c.available()) {
        int value = c.read();
        if (value < 0) continue;

        char ch = (char)value;
        if (ch == '\n') {
          if (len > 0 && out[len - 1] == '\r') len--;
          out[len] = '\0';
          return (int)len;
        }

        // Linia prea lunga se trunchiaza, dar se consuma pana la capat:
        // altfel restul ei ar fi citit drept antetul urmator.
        if (len < cap - 1) out[len++] = ch;
        continue;
      }

      if (!c.connected()) {
        // Peer-ul a inchis si nu mai e nimic de citit.
        out[len] = '\0';
        return (len > 0) ? (int)len : -1;
      }

      delay(1);
    }

    return -1;
  }

  // Comparatie fara diferenta de litera mare/mica, pe prefix.
  static bool startsWithNoCase(const char* text, const char* prefix) {
    while (*prefix) {
      if (tolower((unsigned char)*text) != tolower((unsigned char)*prefix)) return false;
      text++;
      prefix++;
    }
    return true;
  }

  static bool containsNoCase(const char* haystack, const char* needle) {
    for (const char* p = haystack; *p; p++) {
      if (startsWithNoCase(p, needle)) return true;
    }
    return false;
  }

  // Sare peste spatiile de dupa ':' intr-o valoare de antet.
  static const char* headerValue(const char* line) {
    const char* colon = strchr(line, ':');
    if (colon == NULL) return NULL;
    colon++;
    while (*colon == ' ' || *colon == '\t') colon++;
    return colon;
  }

  // -------------------------------------------------------------------
  // Trimiterea cererii
  // -------------------------------------------------------------------

  static bool sendRequest(Client& c, const IPAddress& ip, uint16_t port,
                          const char* method, const char* path,
                          const char* const* headers, uint8_t headerCount,
                          const char* requestBody, size_t requestLen) {
    c.print(method);
    c.print(' ');
    c.print(path);
    c.print(F(" HTTP/1.1\r\n"));

    // Host este obligatoriu in HTTP/1.1 chiar si catre un IP brut:
    // Kestrel raspunde 400 fara el.
    c.print(F("Host: "));
    c.print(ip);
    c.print(':');
    c.print(port);
    c.print(F("\r\n"));

    c.print(F("User-Agent: SolvixHub/" HUB_FIRMWARE_VERSION "\r\n"));
    c.print(F("Accept: application/json\r\n"));

    // Fara keep-alive: cu "close", peer-ul inchide si stim sigur unde se
    // termina corpul chiar daca nu declara nicio lungime.
    c.print(F("Connection: close\r\n"));

    for (uint8_t i = 0; i < headerCount; i++) {
      if (headers[i] == NULL) continue;
      c.print(headers[i]);
      c.print(F("\r\n"));
    }

    if (requestBody != NULL && requestLen > 0) {
      // Content-Length intotdeauna, chunked niciodata pe emisie.
      c.print(F("Content-Type: application/json\r\n"));
      c.print(F("Content-Length: "));
      c.print((unsigned)requestLen);
      c.print(F("\r\n\r\n"));
      c.write((const uint8_t*)requestBody, requestLen);
    } else {
      c.print(F("\r\n"));
    }

    return true;
  }

  // -------------------------------------------------------------------
  // Citirea raspunsului
  // -------------------------------------------------------------------

  // Citeste exact cati octeti se cer, in tampon. Intoarce cati a pus.
  static size_t readInto(Client& c, char* dest, size_t want, unsigned long deadline) {
    size_t got = 0;

    while (got < want && !expired(deadline)) {
      if (c.available()) {
        int value = c.read();
        if (value < 0) continue;
        dest[got++] = (char)value;
        continue;
      }
      if (!c.connected()) break;
      delay(1);
    }

    return got;
  }

  /*
   * Corp in codare chunked: o linie cu lungimea in hexazecimal, apoi
   * atatia octeti, apoi CRLF, pana la un chunk de lungime zero.
   */
  static bool readChunked(Client& c, char* body, size_t bodyCap,
                          Result& out, unsigned long deadline) {
    char line[HEADER_LINE_MAX];
    size_t total = 0;

    for (;;) {
      if (readLine(c, line, sizeof(line), deadline) < 0) {
        out.status = HTTP_ERR_CHUNKED;
        return false;
      }

      // Poate avea extensii dupa ';'; lungimea este pana acolo.
      unsigned long size = strtoul(line, NULL, 16);
      if (size == 0) break;                    // ultimul chunk

      while (size > 0) {
        size_t room = (total < bodyCap - 1) ? (bodyCap - 1 - total) : 0;
        size_t want = (size < room) ? (size_t)size : room;

        if (want == 0) {
          // Nu mai avem loc: consumam si aruncam, ca sa nu ramana
          // jumatate de raspuns in socket, dar marcam trunchierea.
          char sink[32];
          size_t drop = (size < sizeof(sink)) ? (size_t)size : sizeof(sink);
          size_t got = readInto(c, sink, drop, deadline);
          if (got == 0) { out.status = HTTP_ERR_TIMEOUT; return false; }
          size -= got;
          out.truncated = true;
          continue;
        }

        size_t got = readInto(c, body + total, want, deadline);
        if (got == 0) { out.status = HTTP_ERR_TIMEOUT; return false; }
        total += got;
        size  -= got;
      }

      // CRLF de dupa fiecare chunk.
      if (readLine(c, line, sizeof(line), deadline) < 0) {
        out.status = HTTP_ERR_CHUNKED;
        return false;
      }
    }

    body[total] = '\0';
    out.bodyLen = (uint16_t)total;
    return true;
  }

  static bool readResponse(Client& c, char* body, size_t bodyCap,
                           Result& out, unsigned long deadline) {
    char line[HEADER_LINE_MAX];

    // --- linia de status ---------------------------------------------
    char status[STATUS_LINE_MAX];
    if (readLine(c, status, sizeof(status), deadline) < 0) {
      out.status = HTTP_ERR_TIMEOUT;
      return false;
    }

    if (!startsWithNoCase(status, "HTTP/")) {
      out.status = HTTP_ERR_MALFORMED;
      return false;
    }

    const char* space = strchr(status, ' ');
    if (space == NULL) {
      out.status = HTTP_ERR_MALFORMED;
      return false;
    }

    int code = atoi(space + 1);
    if (code < 100 || code > 599) {
      out.status = HTTP_ERR_MALFORMED;
      return false;
    }
    out.status = code;

    // --- antetele ----------------------------------------------------
    long    contentLength = -1;
    bool    chunked = false;
    uint8_t seen = 0;

    for (;;) {
      int len = readLine(c, line, sizeof(line), deadline);
      if (len < 0) {
        out.status = HTTP_ERR_TIMEOUT;
        return false;
      }
      if (len == 0) break;                     // linia goala: urmeaza corpul

      if (++seen > HEADER_COUNT_MAX) {
        out.status = HTTP_ERR_MALFORMED;
        return false;
      }

      if (startsWithNoCase(line, "content-length:")) {
        const char* value = headerValue(line);
        if (value != NULL) contentLength = atol(value);
      }
      else if (startsWithNoCase(line, "transfer-encoding:")) {
        const char* value = headerValue(line);
        if (value != NULL && containsNoCase(value, "chunked")) chunked = true;
      }
      else if (startsWithNoCase(line, "retry-after:")) {
        const char* value = headerValue(line);
        // Doar forma numerica (secunde). Forma cu data HTTP ar cere un
        // ceas, pe care hub-ul nu il are.
        if (value != NULL && *value >= '0' && *value <= '9') {
          out.retryAfterS = (uint32_t)atol(value);
        }
      }
    }

    // --- corpul ------------------------------------------------------
    if (chunked) {
      return readChunked(c, body, bodyCap, out, deadline);
    }

    size_t total = 0;

    if (contentLength >= 0) {
      size_t want = (size_t)contentLength;
      if (want > bodyCap - 1) {
        want = bodyCap - 1;
        out.truncated = true;
      }
      total = readInto(c, body, want, deadline);

      // Restul, daca exista, se lasa in socket: releaseClient() face
      // stop() oricum si conexiunea se inchide de tot.
    }
    else {
      // Fara lungime declarata: se citeste pana cand peer-ul inchide.
      // "A inchis si am golit" este SUCCES, nu eroare de transport.
      while (!expired(deadline)) {
        if (c.available()) {
          int value = c.read();
          if (value < 0) continue;
          if (total < bodyCap - 1) body[total++] = (char)value;
          else out.truncated = true;
          continue;
        }
        if (!c.connected()) break;
        delay(1);
      }
    }

    body[total] = '\0';
    out.bodyLen = (uint16_t)total;
    return true;
  }

  // -------------------------------------------------------------------
  // Interfata
  // -------------------------------------------------------------------

  static bool request(Client& c, const IPAddress& ip, uint16_t port,
                      const char* method, const char* path,
                      const char* const* headers, uint8_t headerCount,
                      const char* requestBody, size_t requestLen,
                      char* body, size_t bodyCap, Result& out,
                      unsigned long budgetMs) {
    unsigned long started  = millis();
    unsigned long deadline = started + budgetMs;

    out.status      = HTTP_ERR_TRANSPORT;
    out.bodyLen     = 0;
    out.truncated   = false;
    out.elapsedMs   = 0;
    out.retryAfterS = 0;

    if (bodyCap < 2) {
      out.status = HTTP_ERR_TOO_LARGE;
      return false;
    }
    body[0] = '\0';

    // connect() blocheaza; timeout-ul lui a fost pus de
    // NetLink::acquireClient(), fiindca este o proprietate a clientului.
    if (c.connect(ip, port) != 1) {
      out.elapsedMs = millis() - started;
      return false;
    }

    if (!sendRequest(c, ip, port, method, path, headers, headerCount,
                     requestBody, requestLen)) {
      out.elapsedMs = millis() - started;
      return false;
    }

    bool ok = readResponse(c, body, bodyCap, out, deadline);
    out.elapsedMs = millis() - started;

    if (!ok) return false;
    return (out.status >= 200 && out.status < 300);
  }

  bool get(Client& client, const IPAddress& ip, uint16_t port,
           const char* path,
           const char* const* headers, uint8_t headerCount,
           char* body, size_t bodyCap, Result& out,
           unsigned long budgetMs) {
    return request(client, ip, port, "GET", path, headers, headerCount,
                   NULL, 0, body, bodyCap, out, budgetMs);
  }

  bool post(Client& client, const IPAddress& ip, uint16_t port,
            const char* path,
            const char* const* headers, uint8_t headerCount,
            const char* requestBody, size_t requestLen,
            char* body, size_t bodyCap, Result& out,
            unsigned long budgetMs) {
    return request(client, ip, port, "POST", path, headers, headerCount,
                   requestBody, requestLen, body, bodyCap, out, budgetMs);
  }

  const char* resultText(int status) {
    switch (status) {
      case HTTP_ERR_TRANSPORT: return "conexiunea nu s-a putut deschide";
      case HTTP_ERR_TIMEOUT:   return "s-a depasit bugetul de timp";
      case HTTP_ERR_TOO_LARGE: return "raspunsul nu incape in tampon";
      case HTTP_ERR_MALFORMED: return "raspuns care nu arata a HTTP";
      case HTTP_ERR_CHUNKED:   return "codare chunked stricata";
      default: break;
    }
    if (status >= 200 && status < 300) return "OK";
    if (status == 401 || status == 403) return "respins: cheie gresita sau lipsa";
    if (status == 404)                  return "endpoint inexistent";
    if (status == 429)                  return "prea multe cereri: server-ul ne-a limitat";
    if (status >= 400 && status < 500)  return "cerere respinsa de server";
    if (status >= 500)                  return "eroare pe server";
    return "status neasteptat";
  }

  void printResult(const Result& result) {
    Serial.print(F("  status "));
    Serial.print(result.status);
    Serial.print(F(" ("));
    Serial.print(resultText(result.status));
    Serial.print(F("), corp "));
    Serial.print(result.bodyLen);
    Serial.print(F(" B, "));
    Serial.print(result.elapsedMs);
    Serial.print(F(" ms"));
    if (result.retryAfterS != 0) {
      Serial.print(F(", Retry-After "));
      Serial.print(result.retryAfterS);
      Serial.print(F(" s"));
    }
    if (result.truncated) Serial.print(F("  [TRUNCHIAT]"));
    Serial.println();
  }
}
