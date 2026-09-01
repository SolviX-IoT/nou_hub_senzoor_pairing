#include "Http.h"

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
