#include "SerialConsole.h"
#include "Console.h"
#include "SensorLink.h"
#include "DeviceRegistry.h"
#include "SensorPacket.h"
#include "NetLink.h"
#include "HubIdentity.h"
#include "HubCloud.h"

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
  // Comenzile
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
        Serial.println(F(" nu este inrolat. Vezi 'sensors' pentru locurile ocupate."));
        return;
      }
      memcpy(eui, byNumber->devEui, DEV_EUI_LEN);
    }
    else if (!parseEui(euiText, eui)) {
      Serial.println(F("Argument invalid. Se asteapta fie 16 cifre hexazecimale (DevEUI),"));
      Serial.print(F("fie numarul senzorului, 1.."));
      Serial.print(HUB_MAX_SENSORS);
      Serial.println(F(", scris ca '#3' sau '3'."));
      return;
    }

    DeviceRecord* device = DeviceRegistry::findByEui(eui);
    if (device == nullptr) {
      Serial.print(F("Nu exista niciun device inrolat cu DevEUI "));
      SensorPacketCodec::printEui(eui);
      Serial.println();
      return;
    }

    if (force) {
      uint8_t removedNumber = device->devAddr;
      DeviceRegistry::removeByEui(eui);
      Serial.print(F("Sters imediat din registru: Senzor #"));
      Serial.print(removedNumber);
      Serial.print(F(", DevEUI "));
      SensorPacketCodec::printEui(eui);
      Serial.println();
      Serial.println(F("ATENTIE: senzorul NU a fost anuntat, deci se crede in continuare inrolat"));
      Serial.println(F("si va emite mai departe. Hub-ul ii va vedea pachetele ca DATA_UP de la o"));
      Serial.println(F("adresa necunoscuta. Oprirea si repornirea alimentarii nu ajuta - starea"));
      Serial.println(F("sta in HEF. Curatarea corecta: tine butonul 2 apasat trei secunde pe"));
      Serial.println(F("senzor (revine in repaus), sau, data viitoare, 'remove <DevEUI>' fara"));
      Serial.println(F("'force' cat timp senzorul inca emite."));
      return;
    }

    // Un device care nu a trimis nimic de la inrolare nu are cum sa
    // primeasca RESET-ul: comanda calatoreste in fereastra de receptie pe
    // care senzorul o deschide DUPA fiecare pachet al lui. Daca l-am marca
    // oricum, inregistrarea ar ramane blocata in registru la nesfarsit, cu
    // adresa ocupata, asteptand un pachet care poate nu vine niciodata.
    // Nu il stergem noi in tacere: asta ar fi exact un `force` nedeclarat,
    // iar diferenta dintre cele doua comenzi este tot rostul lor. Decizia
    // ramane a omului, care stie daca senzorul este pornit sau nu.
    if (!device->hasUplink) {
      Serial.print(F("Device-ul "));
      SensorPacketCodec::printEui(eui);
      Serial.println(F(" nu a trimis niciun pachet de la inrolare."));
      Serial.println(F("Probabil este oprit sau in afara razei. CMD_DOWN(RESET) pleaca doar ca"));
      Serial.println(F("raspuns la un pachet al lui, deci o dezinrolare curata este imposibila acum."));
      Serial.println(F("Ai doua variante:"));
      Serial.println(F("  - porneste senzorul si repeta 'remove <DevEUI>' cat timp emite (curat), sau"));
      Serial.println(F("  - 'remove <DevEUI> force' ca sa il stergi doar local; senzorul pastreaza"));
      Serial.println(F("    inrolarea si va trebui recuperat de la butonul 2."));
      return;
    }

    device->pendingReset  = true;
    device->resetAttempts = 0;
    device->resetSentMs   = 0;
    DeviceRegistry::save();

    Serial.print(F("Marcat pentru dezinrolare: Senzor #"));
    Serial.print(device->devAddr);
    Serial.print(F(", DevEUI "));
    SensorPacketCodec::printEui(eui);
    Serial.println();
    Serial.println(F("La FIECARE pachet al lui primeste cate un CMD_DOWN(RESET); se insista cat"));
    Serial.println(F("timp se aude, fiindca un senzor care inca emite nu a primit comanda."));
    Serial.print(F("Dezinrolarea se confirma abia dupa ce senzorul tace "));
    Serial.print(REMOVE_CONFIRM_SILENCE_MS / 1000UL);
    Serial.println(F(" s - abia atunci dispare"));
    Serial.println(F("din registru. Pana atunci pachetele lui nu mai sunt afisate ca masuratori."));
  }

  static void commandReboot() {
    Serial.println(F("Repornesc. Salvez registrul si adorm radioul..."));
    SensorLink::stop();          // face si DeviceRegistry::save()
    Serial.flush();
    ESP.restart();
  }

  static void commandNet() {
    NetLink::printStatus();

    if (!NetLink::isUp()) {
      Serial.println(F("Incerc din nou sa iau un IP..."));
      if (NetLink::retry()) {
        Serial.println(F("Legatura este acum sus."));
      }
    }
  }

  /*
   * Stergerea identitatii cere confirmare explicita, ca `remove ... force`.
   *
   * Nu este doar curatenie locala: dupa ea hub-ul cere din nou
   * /api/device/provision pentru acelasi deviceUid. Daca endpoint-ul nu
   * este idempotent - lucru neconfirmat cu backend-ul - asta inseamna un
   * hub nou pe server si istoricul vechi orfan.
   */
  static void commandForget(const String& argument) {
    if (!argument.equalsIgnoreCase("yes")) {
      Serial.println(F("'forget' sterge identitatea hub-ului din flash: hubGuid, apiKey,"));
      Serial.println(F("pairingCode si tot config-ul primit de la server."));
      Serial.println(F("Dupa ea hub-ul va cere DIN NOU /api/device/provision pentru acelasi"));
      Serial.println(F("deviceUid. Daca serverul nu trateaza asta idempotent, se poate crea un"));
      Serial.println(F("hub nou si istoricul vechi ramane orfan."));
      Serial.println(F("Senzorii inrolati NU sunt afectati - registrul lor este alt spatiu NVS."));
      Serial.println(F("Daca chiar vrei asta, scrie: forget yes"));
      return;
    }

    HubIdentity::clear();
    Serial.println(F("Identitatea a fost stearsa din flash."));
    Serial.println(F("Scrie 'provision' ca sa o ceri acum, sau 'reboot' ca sa o ceara la pornire."));
  }

  static void commandMem() {
    Serial.print(F("Heap liber: "));
    Serial.print(ESP.getFreeHeap());
    Serial.print(F(" B   minim atins de la pornire: "));
    Serial.print(ESP.getMinFreeHeap());
    Serial.print(F(" B   uptime: "));
    Serial.print(millis() / 1000UL);
    Serial.println(F(" s"));
  }

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

    if (command == "pair")        { SensorLink::enterPairingMode();     return true; }
    if (command == "sensors")     { DeviceRegistry::printSensorTable(); return true; }
    if (command == "list")        { DeviceRegistry::printAll();         return true; }
    if (command == "provisioned") { DeviceRegistry::printProvisioned(); return true; }
    if (command == "stats")       { SensorLink::printStats();
                                    DeviceRegistry::printSensorTable(); return true; }
    if (command == "remove")      { commandRemove(argument);            return true; }
    if (command == "reboot")      { commandReboot();                    return true; }
    if (command == "mem")         { commandMem();                       return true; }
    if (command == "net")         { commandNet();                       return true; }
    if (command == "hub")         { HubIdentity::print();               return true; }
    if (command == "cloud")       { HubCloud::printStatus();            return true; }
    if (command == "health")      { HubCloud::forceHealth();            return true; }
    if (command == "provision")   { HubCloud::forceProvision();         return true; }
    if (command == "forget")      { commandForget(argument);            return true; }

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
    Serial.println(F("  sensors             tabelul locurilor: temperatura, varsta, RSSI, pierderi"));
    Serial.println(F("  list                senzorii inrolati (registrul din NVS)"));
    Serial.println(F("  provisioned         senzorii care au voie sa se inroleze (Config.h)"));
    Serial.println(F("  remove <DevEUI>     il scoate din retea; ii trimite RESET la primul contact"));
    Serial.println(F("  remove #3           acelasi lucru, dupa numarul senzorului"));
    Serial.println(F("  remove <...> force  il sterge imediat din registru, fara sa il anunte"));
    Serial.println(F("  stats               contoarele legaturii radio"));
    Serial.println(F("  net                 starea retelei; reincearca DHCP daca legatura e jos"));
    Serial.println(F("  hub                 identitatea hub-ului (secretele mascate)"));
    Serial.println(F("  cloud               starea bootstrap-ului: sanatate, provisioning, erori"));
    Serial.println(F("  health              verifica acum serverul si baza de date"));
    Serial.println(F("  provision           cere acum provisioning-ul (refuza daca e deja facut)"));
    Serial.println(F("  forget yes          sterge identitatea din flash; senzorii NU sunt afectati"));
    Serial.println(F("  mem                 heap liber si minimul atins de la pornire"));
    Serial.println(F("  reboot              salveaza registrul si reporneste hub-ul"));
    Serial.println(F("  help                acest text"));
    printSeparator();
    Serial.println(F("Serial Monitor: 115200 baud, terminator de linie \"Newline\"."));
    Serial.println(F("Merge si pe \"No line ending\" - comanda se preia dupa o scurta pauza."));
    Serial.println();
    Serial.println(F("DevEUI se scrie ca 16 cifre hexazecimale, ex: 534F4C5649580001."));
    Serial.println(F("Numarul senzorului ('#3' sau '3') este acelasi lucru cu DevAddr si este"));
    Serial.println(F("pozitia placii in tabelul de provisioning din Config.h."));
    Serial.println();
  }

  void begin() {
    s_len = 0;
    s_overflow = false;
    s_lastByteMs = millis();
    printHelp();
  }

  /*
   * Un octet per apel, si atat. Vezi regula 1 din antetul fisierului:
   * nimic de aici nu are voie sa astepte tastatura.
   */
  // Linia s-a incheiat: se executa si tamponul se goleste.
  static void executeLine() {
    s_line[s_len] = '\0';
    uint8_t len = s_len;
    s_len = 0;

    if (s_overflow) {
      s_overflow = false;
      Serial.println(F("Linie prea lunga, ignorata. Cea mai lunga comanda are 25 de caractere."));
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
