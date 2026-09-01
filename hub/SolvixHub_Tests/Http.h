/*
  Http.h - cereri HTTP/1.1 peste un Client oarecare.
  ---------------------------------------------------------------------
  Nu stie ce transport foloseste si nu atinge niciodata SpiBus: primeste
  un Client& de la NetLink::acquireClient() si atat. Sub Ethernet acela
  este un EthernetClient si magistrala este deja rezervata; sub WiFi va
  fi un WiFiClient si nu exista nicio magistrala de rezervat. De aceea se
  numeste Http si nu EthernetHttp.

  NUMELE: fisierul se cheama Http.h si nu HttpClient.h ca sa nu ascunda
  antetul bibliotecii ArduinoHttpClient, care isi expune clasa exact cu
  acel nume - aceeasi capcana pentru care wrapper-ul de criptografie se
  numea candva HubCrypto.h si nu Crypto.h (F-021).

  ZERO String
  ---------------------------------------------------------------------
  Tot ce se citeste intra in char[] date de apelant. Motive:
    - readStringUntil() consuma Serial/Stream::_timeout (implicit O
      SECUNDA) per apel, chiar cand datele sunt deja acolo;
    - un String care creste caracter cu caracter fragmenteaza heap-ul, si
      hub-ul asta trebuie sa mearga luni de zile fara repornire;
    - ArduinoJson v7 parseaza un char* MUTABIL fara sa copieze sirurile,
      deci tamponul apelantului devine chiar memoria documentului. Un
      String ar dubla totul.
  Tamponul trebuie deci sa traiasca atat cat traieste JsonDocument-ul.

  CE ANUME MERGE PROST CU EthernetENC, SI DE CE E TRATAT AICI
  ---------------------------------------------------------------------
  1. connect() BLOCHEAZA si nu are varianta neblocanta. Implicit 5000 ms.
     De aceea NetLink cheama setConnectionTimeout() inainte sa dea
     clientul.
  2. MSS-ul este 44 de octeti (UIP_CONF_BUFFER_SIZE 98), iar fereastra de
     receptie este tot atat. Un raspuns de 700 de octeti inseamna ~16
     dus-intors, deci sute de milisecunde chiar pe o retea buna.
     available() nu intoarce niciodata mai mult de 132 (3 x 44) - asta NU
     este o eroare.
  3. available() este POMPA stivei TCP: el cheama UIPEthernetClass::tick().
     connected() nu. O bucla care testeaza doar connected() nu avanseaza
     niciodata si peer-ul retransmite pana renunta.
  4. Sunt patru conexiuni cu totul (UIP_CONNS) si nu se elibereaza
     singure. Un stop() uitat pe o cale de eroare, cu o reincercare la
     fiecare minut, lasa hub-ul definitiv fara retea dupa patru minute -
     tacut. De aceea NetLink::releaseClient() face stop() neconditionat,
     iar apelantii de aici nu au voie sa se intoarca fara el.

  DE CE EXISTA DE-CHUNKER
  ---------------------------------------------------------------------
  Cererile noastre trimit "Connection: close", deci citirea pana la
  inchiderea peer-ului acopera si Content-Length, si raspunsurile fara
  lungime declarata. Singurul caz ramas este Transfer-Encoding: chunked,
  pe care ASP.NET Core il foloseste ori de cate ori nu stie lungimea din
  start. Fara de-chunker, ArduinoJson ar primi "1a4\r\n{"status":..." si
  ar da InvalidInput, iar masina de stari ar raporta la nesfarsit "baza
  de date nu raspunde" - cu un server perfect sanatos. Douazeci si ceva
  de linii care scutesc o zi de cautat.
*/

#ifndef HTTP_H
#define HTTP_H

#include <Arduino.h>
#include <Client.h>
#include "Config.h"

namespace Http {

  // Coduri de esec, in afara domeniului HTTP 100..599.
  #define HTTP_ERR_TRANSPORT   (-1)   // conectarea sau scrierea a picat
  #define HTTP_ERR_TIMEOUT     (-2)   // s-a depasit bugetul
  #define HTTP_ERR_TOO_LARGE   (-3)   // corpul nu incape in tampon
  #define HTTP_ERR_MALFORMED   (-4)   // linia de status nu e HTTP
  #define HTTP_ERR_CHUNKED     (-5)   // codare chunked stricata

  struct Result {
    int      status;      // 100..599, sau unul dintre HTTP_ERR_*
    uint16_t bodyLen;
    bool     truncated;   // corpul a fost taiat: NU parsa JSON din el
    uint32_t elapsedMs;

    // Antetul Retry-After, in secunde, sau 0 daca serverul nu l-a trimis.
    // Insoteste de obicei un 429 sau un 503 si spune cat sa astepti. Se
    // accepta doar forma numerica; forma cu data HTTP este ignorata,
    // fiindca hub-ul nu are ceas.
    uint32_t retryAfterS;
  };

  /*
   * headers este un tablou de siruri complete, fiecare fara CRLF:
   *   const char* h[] = { "X-Solvix-AdminKey: abc", "Accept: application/json" };
   * Host, Content-Length, Content-Type, User-Agent si Connection sunt
   * puse de functie.
   *
   * Intoarce true doar daca statusul este 2xx. Result se completeaza in
   * toate cazurile.
   */
  bool get(Client& client, const IPAddress& ip, uint16_t port,
           const char* path,
           const char* const* headers, uint8_t headerCount,
           char* body, size_t bodyCap, Result& out,
           unsigned long budgetMs = HTTP_BUDGET_MS);

  bool post(Client& client, const IPAddress& ip, uint16_t port,
            const char* path,
            const char* const* headers, uint8_t headerCount,
            const char* requestBody, size_t requestLen,
            char* body, size_t bodyCap, Result& out,
            unsigned long budgetMs = HTTP_BUDGET_MS);

  // Explicatia unui cod, pentru jurnal.
  const char* resultText(int status);

  // O linie compacta cu ce s-a intamplat: status, lungime, durata.
  void printResult(const Result& result);
}

#endif // HTTP_H
