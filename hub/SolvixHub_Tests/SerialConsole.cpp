#include "SerialConsole.h"
#include "Console.h"
#include "SensorLink.h"
#include "DeviceRegistry.h"
#include "SensorPacket.h"
#include "NetLink.h"
#include "HubIdentity.h"
#include "HubCloud.h"
#include "HubHeartbeat.h"

namespace SerialConsole {

  // Linia in curs de tastare. 96 de octeti acopera cu mult cea mai lunga
  // comanda ("remove 534F4C5649580001 force" are 25).
  static const uint8_t CONSOLE_LINE_MAX = 96;

  /*
   * Cati octeti se citesc cel mult intr-un singur tick().
   *
   * Unul singur, cum era la inceput, insemna un octet la fiecare trecere
   * prin loop(), adica la fiecare ~5 ms: o comanda lipita din clipboard
   * intra literalmente caracter cu caracter. 32 este destul cat o linie
   * intreaga sa fie inghitita dintr-o data si suficient de putin cat sa
   * nu tina bucla ocupata - la 115200 baud, 32 de octeti inseamna 2,8 ms
   * de trafic, dar ei sunt deja in tamponul UART, deci citirea lor este
   * instantanee.
   */
  static const uint8_t CONSOLE_DRAIN_MAX = 32;

  /*
   * Dupa cata liniste se considera incheiata o linie care NU s-a terminat
   * cu Enter.
   *
   * DE CE EXISTA. Serial Monitor are o setare de terminator de linie, si
   * pe "No line ending" nu trimite nici \n, nici \r - doar caracterele
   * comenzii. Varianta veche a consolei folosea readStringUntil('\n'),
   * care se intorcea oricum dupa timeout-ul ei de o secunda, deci mergea
   * si asa (blocand bucla o secunda, F-040). Varianta care citeste octet
   * cu octet nu are timeout, deci fara ce urmeaza mai jos octetii s-ar
   * aduna in s_line la nesfarsit si NICIO comanda nu s-ar executa
   * vreodata - fara macar un mesaj de eroare, fiindca nu s-ar ajunge la
   * dispecerizare (F-045).
   *
   * 250 ms este ales pentru cum se comporta terminalele reale:
   *   - Serial Monitor trimite linia intreaga ca o RAFALA cand apesi
   *     Send, deci o pauza de 250 ms inseamna sigur "linia s-a terminat";
   *   - un terminal brut (PuTTY) trimite caracter cu caracter, dar acolo
   *     Enter trimite \r, deci terminatorul explicit rezolva oricum cazul.
   * Nu este deci un timeout care taie o comanda tastata rar.
   */
  static const unsigned long CONSOLE_IDLE_FLUSH_MS = 250;

  static char          s_line[CONSOLE_LINE_MAX];
  static uint8_t       s_len = 0;
  static bool          s_overflow = false;
  static unsigned long s_lastByteMs = 0;

  // S-a spus deja o data ca terminalul nu trimite terminator de linie?
  static bool s_idleFlushNoticed = false;

  // -------------------------------------------------------------------
  // Ajutoare de parsare
  // -------------------------------------------------------------------

  /*
   * Transforma 16 cifre hexazecimale intr-un DevEUI. Accepta si
   * separatori (-, :, spatiu, punct), ca sa se poata lipi din
   * documentatie.
   */
  static bool parseEui(const String& text, uint8_t* eui) {
    uint8_t nibbles[DEV_EUI_LEN * 2];
    uint8_t found = 0;

    for (unsigned int i = 0; i < text.length(); i++) {
      char c = text.charAt(i);
      if (c == '-' || c == ':' || c == ' ' || c == '.') continue;

      uint8_t value;
      if      (c >= '0' && c <= '9') value = (uint8_t)(c - '0');
      else if (c >= 'a' && c <= 'f') value = (uint8_t)(c - 'a' + 10);
      else if (c >= 'A' && c <= 'F') value = (uint8_t)(c - 'A' + 10);
      else return false;

      if (found >= sizeof(nibbles)) return false;   // prea multe cifre
      nibbles[found++] = value;
    }

    if (found != sizeof(nibbles)) return false;     // prea putine cifre

    for (uint8_t i = 0; i < DEV_EUI_LEN; i++) {
      eui[i] = (uint8_t)((nibbles[i * 2] << 4) | nibbles[i * 2 + 1]);
    }
    return true;
  }

  /*
   * Un argument scris ca "#3" sau ca "3" inseamna numarul senzorului.
   * Intoarce numarul, sau 0 daca argumentul nu are forma asta.
   *
   * Cu cinci placi in teren, DevEUI de 16 cifre este cel mai bun mod de a
   * gresi tocmai placa pe care nu voiai sa o scoti din retea. Numarul este
   * scurt, este scris pe cutie si apare in fiecare linie de jurnal.
   * DevEUI ramane acceptat: este identitatea adevarata si singura forma
   * care merge pentru un senzor provizionat dar neinrolat.
   */
  static uint8_t parseSensorNumber(const String& text) {
    if (text.length() == 0) return 0;

    unsigned int start = (text.charAt(0) == '#') ? 1 : 0;
    if (text.length() - start == 0) return 0;

    uint16_t value = 0;
    for (unsigned int i = start; i < text.length(); i++) {
      char c = text.charAt(i);
      if (c < '0' || c > '9') return 0;
      value = (uint16_t)(value * 10 + (c - '0'));
      if (value > HUB_MAX_SENSORS) return 0;
    }

    return (uint8_t)value;
  }

  // -------------------------------------------------------------------
  // status
  // -------------------------------------------------------------------

  /*
   * Tot ce trebuie vazut, intr-un singur loc: senzorii, reteaua, cloud-ul
   * si identitatea hub-ului.
   *
   * A luat locul comenzilor `sensors`, `list`, `provisioned`, `stats`,
   * `net`, `hub` si `cloud`. Fiecare dintre ele raspundea la o bucata din
   * "merge sau nu merge?", si trebuia sa le stii pe toate ca sa afli.
   */
  static void commandStatus() {
    Serial.println();
    printSeparator();
    Serial.println(F("  STARE"));
    printSeparator();

    // --- Senzorii ----------------------------------------------------
    DeviceRegistry::printSensorTable();

    Serial.print(F("Inrolati: "));
    Serial.print(DeviceRegistry::count());
    Serial.print(F(" din "));
    Serial.print(HUB_MAX_SENSORS);
    Serial.println(SensorLink::isPairingMode()
                   ? F("   (fereastra de inrolare este DESCHISA)")
                   : F(""));

    // --- Reteaua -----------------------------------------------------
    Serial.print(F("Retea   : "));
    Serial.print(NetLink::transportName());
    if (NetLink::isUp()) {
      Serial.print(F(", IP "));
      Serial.println(NetLink::localIP());
    } else {
      Serial.println(F(", FARA ADRESA (senzorii merg oricum)"));
    }

    // --- Cloud -------------------------------------------------------
    Serial.print(F("Cloud   : "));
    Serial.print(HubCloud::stateName());
    if (!HubIdentity::isProvisioned()) Serial.print(F(", NEPROVIZIONAT"));

    const char* error = HubCloud::lastError();
    if (error[0] != '\0') {
      Serial.print(F("  ["));
      Serial.print(error);
      Serial.print(']');
    }
    Serial.println();

    /*
     * --- Pulsul ------------------------------------------------------
     *
     * Linia de cloud de mai sus spune daca bootstrap-ul s-a incheiat -
     * o intrebare la care se raspunde o singura data, la pornire. Asta
     * spune daca serverul ne mai VEDE acum, care este intrebarea de zi
     * cu zi si singura defectiune din tot lantul invizibila altfel:
     * senzorii se aud, LED-urile clipesc, consola raspunde, si totusi in
     * aplicatie hub-ul apare mort.
     */
    HubHeartbeat::printStatus();

    // --- Identitatea -------------------------------------------------
    if (HubIdentity::isProvisioned()) {
      const HubIdentityData& id = HubIdentity::get();

      Serial.print(F("Hub     : "));
      Serial.print(id.hubGuid);
      Serial.print(F("  ("));
      Serial.print(id.lifecycleStatus);
      Serial.println(')');

      /*
       * pairingCode se afiseaza INTREG, si de asta a intrat aici.
       *
       * Rostul lui este sa fie citit de un om de pe ecran si tastat in
       * aplicatie ca sa revendice hub-ul. De cand comanda `hub` nu mai
       * exista, `status` este singurul loc din care se poate afla - iar
       * un cod de revendicare pe care nu il poate citi nimeni nu isi mai
       * face treaba.
       *
       * apiKey NU se afiseaza deloc, nici macar mascat: pe el nu are de
       * ce sa il citeasca nimeni. provisioningSecret cu atat mai putin -
       * el este compilat in firmware si nu apare nicaieri.
       */
      Serial.print(F("Cod     : "));
      Serial.println(id.pairingCode);
    }

    Serial.println();
  }

  // -------------------------------------------------------------------
  // remove
  // -------------------------------------------------------------------

  static void commandRemove(const String& argument) {
    // Ultimul cuvant poate fi "force".
    String euiText = argument;
    bool force = false;

    int space = euiText.indexOf(' ');
    if (space >= 0) {
      String tail = euiText.substring(space + 1);
      tail.trim();
      euiText = euiText.substring(0, space);
      euiText.trim();
      force = tail.equalsIgnoreCase("force");
    }

    // Intai forma scurta: "#3" sau "3".
    uint8_t eui[DEV_EUI_LEN];
    uint8_t number = parseSensorNumber(euiText);

    if (number != 0) {
      DeviceRecord* byNumber = DeviceRegistry::findByAddr(number);
      if (byNumber == nullptr) {
        Serial.print(F("Senzorul #"));
        Serial.print(number);
        Serial.println(F(" nu este inrolat. Vezi 'status'."));
        return;
      }
      memcpy(eui, byNumber->devEui, DEV_EUI_LEN);
    }
    else if (!parseEui(euiText, eui)) {
      Serial.println(F("Se asteapta 16 cifre hexazecimale (DevEUI) sau numarul"));
      Serial.print(F("senzorului, 1.."));
      Serial.print(HUB_MAX_SENSORS);
      Serial.println(F(", scris ca '#3' sau '3'."));
      return;
    }

    DeviceRecord* device = DeviceRegistry::findByEui(eui);
    if (device == nullptr) {
      Serial.print(F("Niciun device inrolat cu DevEUI "));
      SensorPacketCodec::printEui(eui);
      Serial.println();
      return;
    }

    if (force) {
      uint8_t removedNumber = device->devAddr;
      DeviceRegistry::removeByEui(eui);
      Serial.print(F("Sters imediat din registru: Senzor #"));
      Serial.println(removedNumber);
      Serial.println(F("ATENTIE: senzorul NU a fost anuntat. Se crede in continuare inrolat si"));
      Serial.println(F("va emite mai departe; starea lui sta in HEF, deci nici oprirea"));
      Serial.println(F("alimentarii nu ajuta. Recuperare: butonul 2 tinut 3 s pe senzor."));
      return;
    }

    /*
     * Un device care nu a trimis nimic de la inrolare nu are cum sa
     * primeasca RESET-ul: comanda calatoreste in fereastra de receptie pe
     * care senzorul o deschide DUPA fiecare pachet al lui. Daca l-am marca
     * oricum, inregistrarea ar ramane blocata in registru la nesfarsit.
     * Nu il stergem noi in tacere: ar fi exact un `force` nedeclarat, iar
     * diferenta dintre cele doua comenzi este tot rostul lor.
     */
    if (!device->hasUplink) {
      Serial.println(F("Device-ul nu a trimis niciun pachet de la inrolare, deci nu are cum sa"));
      Serial.println(F("primeasca RESET-ul: el pleaca doar ca raspuns la un pachet al lui."));
      Serial.println(F("Ori il pornesti si repeti comanda cat timp emite (curat), ori"));
      Serial.println(F("'remove ... force' ca sa il stergi doar local."));
      return;
    }

    device->pendingReset  = true;
    device->resetAttempts = 0;
    device->resetSentMs   = 0;
    DeviceRegistry::save();

    Serial.print(F("Marcat pentru dezinrolare: Senzor #"));
    Serial.println(device->devAddr);
    Serial.println(F("Primeste CMD_DOWN(RESET) la FIECARE pachet al lui; se insista cat timp"));
    Serial.println(F("se aude, fiindca un senzor care inca emite nu a primit comanda."));
    Serial.print(F("Se confirma abia dupa ce tace "));
    Serial.print(REMOVE_CONFIRM_SILENCE_MS / 1000UL);
    Serial.println(F(" s - atunci dispare din registru."));
  }

  // -------------------------------------------------------------------
  // Dispecerizare
  // -------------------------------------------------------------------

  // Intoarce true daca linia a fost o comanda cunoscuta.
  static bool dispatch(const String& line) {
    String command = line;
    String argument = "";

    int space = command.indexOf(' ');
    if (space >= 0) {
      argument = command.substring(space + 1);
      argument.trim();
      command = command.substring(0, space);
    }
    command.toLowerCase();

    if (command == "pair")   { SensorLink::enterPairingMode(); return true; }
    if (command == "status") { commandStatus();                return true; }
    if (command == "remove") { commandRemove(argument);        return true; }

    if (command == "help" || command == "?" || command == "h") {
      printHelp();
      return true;
    }

    return false;
  }

  // -------------------------------------------------------------------
  // Interfata
  // -------------------------------------------------------------------

  void printHelp() {
    Serial.println();
    printSeparator();
    Serial.println(F("  COMENZI"));
    printSeparator();
    Serial.println(F("  pair                deschide fereastra de inrolare"));
    Serial.println(F("  status              senzorii, reteaua, cloud-ul si identitatea hub-ului"));
    Serial.println(F("  remove <DevEUI|#n>  scoate un senzor din retea; RESET la primul contact"));
    Serial.println(F("  remove <...> force  il sterge imediat din registru, fara sa il anunte"));
    Serial.println(F("  help                acest text"));
    printSeparator();
    Serial.println(F("Serial Monitor: 115200 baud, terminator de linie \"Newline\"."));
    Serial.println(F("Merge si pe \"No line ending\" - comanda se preia dupa o scurta pauza."));
    Serial.println();
  }

  void begin() {
    s_len = 0;
    s_overflow = false;
    s_lastByteMs = millis();
    printHelp();
  }

  // Linia s-a incheiat: se executa si tamponul se goleste.
  static void executeLine() {
    s_line[s_len] = '\0';
    uint8_t len = s_len;
    s_len = 0;

    if (s_overflow) {
      s_overflow = false;
      Serial.println(F("Linie prea lunga, ignorata."));
      return;
    }

    if (len == 0) return;      // Enter pe gol, sau al doilea octet al unui CRLF

    String line(s_line);
    line.trim();
    if (line.length() == 0) return;

    if (dispatch(line)) return;

    Serial.print(F("Comanda necunoscuta: '"));
    Serial.print(line);
    Serial.println(F("'. Scrie 'help' pentru lista."));
  }

  void tick() {
    // Pasul 1: se ia ce a venit, marginit. Octetii sunt deja in tamponul
    // UART-ului, deci nu se asteapta dupa niciunul.
    uint8_t drained = 0;

    while (Serial.available() && drained < CONSOLE_DRAIN_MAX) {
      char c = (char)Serial.read();
      drained++;
      s_lastByteMs = millis();

      if (c == '\n' || c == '\r') {
        executeLine();
        return;                // restul, daca exista, la urmatorul tick
      }

      if (s_len < CONSOLE_LINE_MAX - 1) {
        s_line[s_len++] = c;
      } else {
        s_overflow = true;     // se raporteaza cand se incheie linia
      }
    }

    // Pasul 2: nu a venit niciun terminator, dar s-a facut liniste - pe
    // "No line ending" asta este singurul semn ca linia s-a terminat.
    if (s_len > 0 && (millis() - s_lastByteMs) >= CONSOLE_IDLE_FLUSH_MS) {

      /*
       * Se spune o singura data, la prima comanda de acest fel. Comanda
       * merge oricum, deci nu este o eroare - dar este exact informatia
       * pe care ar fi vrut sa o aiba cineva care tocmai a scris cinci
       * comenzi si nu a raspuns niciuna, inainte sa existe randurile
       * astea (F-045).
       */
      if (!s_idleFlushNoticed) {
        s_idleFlushNoticed = true;
        Serial.println();
        Serial.println(F("(Terminalul nu trimite terminator de linie. Comanda a fost preluata"));
        Serial.println(F(" oricum, dupa o pauza. Pentru raspuns instantaneu, pune Serial Monitor"));
        Serial.println(F(" pe \"Newline\" sau \"Both NL & CR\".)"));
      }

      executeLine();
    }
  }
}
