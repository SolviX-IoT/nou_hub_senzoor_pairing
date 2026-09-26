/*
  HubNet.h - transportul catre server.
  ---------------------------------------------------------------------
  Module, in ordinea dependentelor, fiecare in namespace-ul lui:
    NetLink  legatura cu reteaua (Ethernet azi), granita magistralei SPI
    Http     cereri HTTP/1.1 peste un Client oarecare

  Fiecare modul isi pastreaza mai jos descrierea completa.
*/

#ifndef HUB_NET_H
#define HUB_NET_H

#include <Arduino.h>
#include <Client.h>
#include "Config.h"
#include "HubBoard.h"

// =====================================================================
//  NetLink
// =====================================================================

/*
  NetLink - legatura hub-ului cu reteaua.
  ---------------------------------------------------------------------
  Fisierul se numea EthernetLink.h si vorbea despre ENC28J60 in fiecare
  semnatura. Acum suprafata publica nu mai numeste transportul nicaieri:
  astazi este Ethernet, urmeaza WiFi, si codul care face cereri nu are de
  ce sa afle care dintre ele.

  CUM SE ADAUGA WiFi MAI TARZIU
  ---------------------------------------------------------------------
  Se schimba HUB_NET_TRANSPORT in Config.h si se scrie ramura #elif din
  HubNet.cpp, cu un WiFiClient static in loc de EthernetClient. NICIUN
  apelant nu se modifica, fiindca acquireClient() intoarce un Client*, iar
  EthernetClient si WiFiClient deriva amandoua din Client.

  DE CE REZERVAREA SPI STA AICI, SI NU IN Http
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

}


// =====================================================================
//  Http
// =====================================================================

/*
  Http - cereri HTTP/1.1 peste un Client oarecare.
  ---------------------------------------------------------------------
  Nu stie ce transport foloseste si nu atinge niciodata SpiBus: primeste
  un Client& de la NetLink::acquireClient() si atat. Sub Ethernet acela
  este un EthernetClient si magistrala este deja rezervata; sub WiFi va
  fi un WiFiClient si nu exista nicio magistrala de rezervat. De aceea se
  numeste Http si nu EthernetHttp.

  NUMELE: niciun fisier din sketch nu se cheama HttpClient.h, ca sa nu
  ascunda antetul bibliotecii ArduinoHttpClient, care isi expune clasa
  exact cu acel nume - aceeasi capcana pentru care wrapper-ul de
  criptografie se numea candva HubCrypto.h si nu Crypto.h (F-021). Din
  acelasi motiv acest fisier se cheama HubNet.h si nu Network.h: nucleul
  ESP32 3.x are o biblioteca proprie cu antetul Network.h.

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

#endif // HUB_NET_H
