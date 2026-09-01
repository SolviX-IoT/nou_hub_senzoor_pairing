/*
  =====================================================================
  SOLVIX HUB - ESP32 + RFM96 (SX1276) + ENC28J60
  =====================================================================
  Placa: ESP32 Dev Module.
  Librarii (Library Manager): EthernetENC (Juraj Andrassy),
                              LoRa (Sandeep Mistry),
                              ArduinoJson v7 (Benoit Blanchon).

  CE ESTE ACEST PROGRAM
  ---------------------------------------------------------------------
  Pana acum a fost o SUITA DE TESTE: un meniu pe Serial din care se
  pornea cate un test o data, si nimic nu rula pana cand nu tasta cineva
  o cifra. Inrolarea senzorilor si receptia temperaturilor - adica tot
  produsul - traiau in "testul 8".

  Acum hub-ul porneste singur si ruleaza permanent. Nu mai exista meniu
  si nu mai exista teste; au ramas comenzile in cuvinte, in
  SerialConsole.cpp.

  MAGISTRALA SPI ESTE PARTAJATA - REGULILE NU S-AU SCHIMBAT
  ---------------------------------------------------------------------
  ENC28J60 si LoRa stau pe aceleasi trei fire (SCK 18, MISO 19,
  MOSI 23) si se deosebesc doar prin CS: GPIO4 pentru Ethernet, GPIO5
  pentru LoRa. De aici cinci reguli care nu se incalca:

    1. SPI.begin() se cheama O SINGURA DATA, din SpiBus::begin().
    2. Ambele CS sunt OUTPUT si HIGH inainte de orice trafic - altfel
       ambele module trag de MISO in acelasi timp (F-001).
    3. Nu se apeleaza NICIODATA LoRa.end() sau SPI.end(): ar inchide
       magistrala intregului ESP32 si ar amuti si Ethernet-ul (F-003).
    4. Receptia LoRa se face prin POLLING, nu prin callback pe DIO0. Un
       callback ar accesa SPI din context de intrerupere, posibil in
       mijlocul unui transfer Ethernet (F-004).
    5. Un singur modul vorbeste la un moment dat. Programul are un
       singur fir de executie si nicio rutina de intrerupere care sa
       atinga SPI, deci regula se respecta prin constructie - atat timp
       cat nimeni nu adauga un task FreeRTOS.

  FEREASTRA DE DOWNLINK - DE CE CONTEAZA CE SE PUNE IN loop()
  ---------------------------------------------------------------------
  Senzorul isi deschide fereastra de receptie imediat dupa ce a emis si o
  tine deschisa doar 600 ms; hub-ul ii raspunde in ~55 ms. In plus,
  LoRa.parsePacket() pune modemul in RX_SINGLE, care expira dupa ~102 ms.
  Prin urmare orice lucru lung pus in loop() nu INTARZIE receptia, ci o
  DISTRUGE: pachetele nu se acumuleaza nicaieri.

  Regula practica: nimic din loop() nu blocheaza. Consola citeste un
  octet per apel, iar orice viitoare cerere de retea se da la o parte
  cat timp un senzor tocmai a vorbit (SensorLink::lastRxMs()).

  FISIERELE
  ---------------------------------------------------------------------
    Config.h            - toti pinii si toate constantele, intr-un loc
    Console.*           - ajutoare de afisare pe Serial
    SpiBus.*            - arbitrajul magistralei partajate
    Leds.*              - cele doua LED-uri, fara delay()
    LoRaRadio.*         - invelis peste libraria LoRa
    SensorPacket.*      - formatul pachetelor (oglinda senzor/main.c)
    DeviceRegistry.*    - registrul senzorilor inrolati, in NVS
    SensorLink.*        - RUNTIME: inrolare, date, dezinrolare, tacere
    NetLink.*           - reteaua (azi Ethernet; WiFi mai tarziu)
    Http.*              - cereri HTTP peste un Client oarecare
    HubIdentity.*       - identitatea primita de la cloud, in NVS
    HubCloud.*          - bootstrap: /api/health si /api/device/provision
    SerialConsole.*     - comenzile de pe Serial
*/

#include "Config.h"
#include "SpiBus.h"
#include "Leds.h"
#include "Console.h"
#include "DeviceRegistry.h"
#include "SensorLink.h"
#include "NetLink.h"
#include "HubIdentity.h"
#include "HubCloud.h"
#include "SerialConsole.h"

// Butonul 1 ca declansator de pairing: se retine starea precedenta ca sa
// se reactioneze pe FRONT, nu pe nivel. GPIO34 nu are pull intern, deci
// fara rezistor extern linia este zgomot (F-008) - de aceea comanda
// seriala `pair` ramane calea sigura.
static int s_lastButton1 = LOW;

/*
 * Butonul 1 deschide fereastra de pairing, ca sa nu fie nevoie de un
 * calculator langa hub.
 *
 * Cat timp exista suita de teste, butonul era ascultat doar in afara
 * testelor sau in testul de pairing: o apasare in timpul testului de
 * butoane ar fi oprit exact testul care le masura, iar zgomotul de pe
 * GPIO34 ar fi comutat testele singur (F-008). Testele au disparut, deci
 * conditia a disparut si ea - butonul face acum un singur lucru, si
 * cel mai rau pe care il poate provoca zgomotul este o fereastra de
 * inrolare deschisa degeaba, care se inchide singura.
 */
static void handleButton() {
  int level = digitalRead(PIN_BUTTON_1);

  if (level == HIGH && s_lastButton1 == LOW) {
    Serial.println();
    Serial.println(F("Buton 1 apasat -> deschid fereastra de pairing."));
    SensorLink::enterPairingMode();
  }

  s_lastButton1 = level;
}

void setup() {
  Serial.begin(SERIAL_BAUD);
  delay(1500);

  Serial.println();
  printSeparator();
  Serial.println(F("  SOLVIX HUB"));
  printSeparator();

  // LED-urile intai: asa se vede ca placa traieste chiar daca ceva de
  // mai jos esueaza.
  Leds::begin();

  // Magistrala SPI se initializeaza o singura data, aici. Functia duce
  // ambele CS-uri pe HIGH inainte sa existe orice trafic, deci niciun
  // modul nu este selectat la boot.
  SpiBus::begin();
  Serial.println(F("SPI pornit: SCK 18, MISO 19, MOSI 23."));
  Serial.println(F("CS_ETH (4) si NSS_LoRa (5) sunt pe HIGH - bus liber."));

  // GPIO34 este input-only si fara pull intern: rezistorul extern este
  // obligatoriu (F-008).
  pinMode(PIN_BUTTON_1, INPUT);
  s_lastButton1 = digitalRead(PIN_BUTTON_1);

  Serial.print(F("Firmware "));
  Serial.print(F(HUB_FIRMWARE_VERSION));
  Serial.print(F(", hub id "));
  Serial.println(HUB_ID);

  // Registrul senzorilor inrolati, din NVS.
  if (DeviceRegistry::begin()) {
    Serial.print(F("Registru incarcat: "));
    Serial.print(DeviceRegistry::count());
    Serial.print(F(" senzor(i) inrolati din "));
    Serial.print(HUB_MAX_SENSORS);
    Serial.println(F(" locuri. Scrie 'sensors' pentru tabel."));
  }

  // Identitatea din cloud, din NVS. Goala = hub neprovizionat, si atunci
  // HubCloud o va cere singur cand reteaua si serverul sunt disponibile.
  HubIdentity::begin();

  // Radioul PORNESTE SI ASCULTA. Legatura cu senzorii este produsul;
  // orice altceva (reteaua, cloud-ul) este un canal de raportare peste
  // ea si nu are voie sa o conditioneze. Un hub fara cablu de retea
  // trebuie sa poata fi pus in functiune pe teren.
  SensorLink::begin();

  // Reteaua vine DUPA radio, si esecul ei nu opreste nimic. Ordinea are
  // si un motiv practic: daca DHCP-ul isi arde cele 8 secunde, modemul
  // LoRa asculta deja si registrul este incarcat.
  NetLink::begin();

  // Masina de stari a bootstrap-ului. Nu face niciun I/O aici: tot ce
  // inseamna retea se intampla in tick(), unde exista portile care il tin
  // departe de ferestrele de downlink ale senzorilor.
  HubCloud::begin();

  SerialConsole::begin();
}

void loop() {
  // Neblocanta: un octet per apel.
  SerialConsole::tick();

  handleButton();

  // Inima programului: LED-uri, fereastra de pairing, dezinrolari,
  // supravegherea tacerii, un receiveRaw() si dispecerizarea lui.
  // Contine si singurul delay() din loop, care da ritmul intregii bucle.
  SensorLink::tick();

  // Intretinerea legaturii, autolimitata la o data pe secunda si data la
  // o parte cat timp un senzor tocmai a vorbit.
  NetLink::maintain();

  // Bootstrap-ul in cloud: sanatatea serverului, apoi provisioning-ul.
  // Costa 0 ms cat timp nu are nimic de facut.
  HubCloud::tick();
}
