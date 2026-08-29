/*
  SensorPacket.h - pachetele schimbate cu nodul senzor (PIC16LF1508).
  ---------------------------------------------------------------------
  ACEST FISIER ESTE OGLINDA sectiunii 4 de protocol din senzor/main.c.
  Orice modificare aici trebuie facuta si acolo, in acelasi commit
  (regula 10 din CLAUDE.md).

  !!! RETEAUA NU ESTE AUTENTIFICATA !!!
  ---------------------------------------------------------------------
  Criptografia (XTEA-128 + CBC-MAC + CTR) a fost scoasa din proiect: nu
  mai incapea in PIC16LF1508, unde ocupa ~1300 din cele 3968 de cuvinte
  utilizabile. Este o masura TEMPORARA, pana la un microcontroller cu mai
  multa memorie. Ultima versiune care o contine este commit-ul a710142.

  Nu mai exista MIC, cheie sau nonce. Prin urmare oricine are un radio
  LoRa cu aceiasi parametri poate injecta o temperatura falsa pentru
  orice senzor, poate dezinrola orice placa cu patru octeti, si poate
  rejuca orice pachet capturat. Inrolarea de mai jos este o COMISIONARE -
  cine e in retea, ce numar are, de unde incep contoarele - NU un control
  de acces. Nu pune sistemul in exploatare in aceasta forma.

  Toate campurile multi-octet sunt big-endian. Primul octet ramane
  magic-ul 0xA5 din protocolul initial.

  CELE CINCI LUNGIMI SUNT DISTINCTE - 6 / 10 / 3 / 13 / 4. De cand nu mai
  exista MIC, perechea tip+lungime este singura verificare impotriva unei
  desincronizari intre capete: fara ea, un pachet de format vechi ar fi
  citit la offset-uri gresite si ar da o temperatura plauzibila si
  gresita, in tacere. Nu egala doua lungimi.

  ---------------------------------------------------------------------
  0x01 - TEMP_PLAIN, pachetul initial de 6 octeti (NESCHIMBAT)
  ---------------------------------------------------------------------
    [0] MAGIC     = 0xA5
    [1] TYPE      = 0x01
    [2] TEMP_HI   } int16, temperatura in SUTIMI de grad Celsius
    [3] TEMP_LO   } (2350 inseamna 23.50 C)
    [4] REASON    = 0x00 la interval periodic, 0x01 la apasare de buton
    [5] CHECKSUM  = (b0 ^ b1 ^ b2 ^ b3 ^ b4) ^ 0x5A

  Acesta este si payload-ul transportat de DATA_UP: se da neschimbat lui
  decode(), deci nu exista doua cai diferite de interpretare a
  temperaturii, iar testul 7 si testul 8 folosesc acelasi cod.

  ---------------------------------------------------------------------
  0x10 - JOIN_REQ (senzor -> hub), 10 octeti
  ---------------------------------------------------------------------
    [0] 0xA5  [1] 0x10  [2..9] DevEUI

  DevEUI ramane pe fir desi hub-ul stie oricum ce numere exista: el este
  cheia dupa care hub-ul verifica ca placa are voie, deriva numarul din
  POZITIA in tabelul de provisioning (F-037) si o identifica in comanda
  `remove <DevEUI>`. Daca JOIN_REQ ar purta doar numarul, senzorul si-ar
  declara singur adresa - exact ce a reparat F-037.

  ---------------------------------------------------------------------
  0x11 - JOIN_ACCEPT (hub -> senzor), 3 octeti
  ---------------------------------------------------------------------
    [0] 0xA5  [1] 0x11  [2] DevAddr

  DevAddr circula acum in clar si aici, ceea ce inchide ce F-035 lasase
  netratat: senzorul poate filtra fereastra de join pe adresa, fiindca
  stie de la compilare ce numar asteapta (SENSOR_NODE_ID). Doi senzori
  care se inroleaza in aceeasi secunda nu isi mai fura fereastra. Ca
  efect secundar, o placa programata cu un numar care nu corespunde
  pozitiei ei din tabel isi refuza singura JOIN_ACCEPT-ul - diagnosticul
  care inlocuieste sirul de "MIC gresit" de dinainte.

  ---------------------------------------------------------------------
  0x12 - DATA_UP (senzor -> hub), 13 octeti
  ---------------------------------------------------------------------
    [0] 0xA5  [1] 0x12  [2] DevAddr  [3..6] FrameCounter
    [7..12] pachetul TEMP de 6 octeti, IN CLAR

  Numele nu mai este DATA_ENC: nu mai exista niciun "Enc". Valoarea
  tipului ramane 0x12 - un pachet de firmware vechi are 17 octeti si cade
  la verificarea de lungime, ceea ce este exact simptomul util.

  DevAddr DIN OCTETUL [2] ESTE NUMARUL SENZORULUI, si el este singurul
  raspuns de care are nevoie intrebarea "de la cine vine data" cand pe
  canal sunt mai multe placi. ATENTIE: raspunsul acela a devenit
  DECLARATIV. Cat timp exista MIC, adresa intra in zona semnata cu cheia
  de sesiune a acelui senzor, deci nu putea fi falsificata. Acum orice
  emitator poate pretinde orice numar.

  FrameCounter-ul ramane si devine singura aparare a caii de date: hub-ul
  cere strict crescator. Tot el da pachetele pierdute si detectia de
  repornire (F-036).

  Un pachet suprapus peste altul in aer se pierde de tot - nu ajunge la
  hub in nicio forma - deci nu exista cazul "date amestecate intre
  senzori": ori un pachet ajunge intreg si atribuit corect, ori nu ajunge
  deloc si se vede ca un gol in frame counter.

  ---------------------------------------------------------------------
  0x13 - CMD_DOWN (hub -> senzor), 4 octeti
  ---------------------------------------------------------------------
    [0] 0xA5  [1] 0x13  [2] DevAddr  [3] CmdType (0x01 ACK, 0x02 RESET)

  Contorul downlink a fost scos de pe fir. Fara MIC nu apara nimic - un
  atacator nu are nevoie sa REIA un CMD_DOWN capturat, il fabrica din
  patru octeti - iar senzorul nu l-a citit niciodata. downCounter ramane
  in DeviceRecord ca statistica locala, vizibila in jurnal.

  ---------------------------------------------------------------------
  De ce se pastreaza checksum-ul XOR in interiorul celor 6 octeti: ca
  pachetul de temperatura sa ramana BIT CU BIT cel vechi si sa poata fi
  dat direct lui decode(). NU este apararea de integritate a pachetului -
  aceea este CRC-ul LoRa, activ pe ambele capete, care acopera TOT
  payload-ul, inclusiv adresa si contorul.
*/

#ifndef SENSOR_PACKET_H
#define SENSOR_PACKET_H

#include <Arduino.h>

#define SENSOR_PACKET_MAGIC     0xA5
#define SENSOR_PACKET_LEN       6
#define SENSOR_CHECKSUM_SALT    0x5A

// Tipurile de mesaj (octetul 1).
#define SENSOR_MSG_TEMPERATURE  0x01   // TEMP_PLAIN
#define SENSOR_MSG_JOIN_REQ     0x10
#define SENSOR_MSG_JOIN_ACCEPT  0x11
#define SENSOR_MSG_DATA_UP      0x12
#define SENSOR_MSG_CMD_DOWN     0x13

// Lungimile fixe ale fiecarui tip.
//
// CELE CINCI LUNGIMI SUNT DISTINCTE - 6 / 10 / 3 / 13 / 4 - si trebuie
// sa ramana asa. De cand nu mai exista MIC, perechea tip+lungime este
// SINGURA verificare impotriva unei desincronizari intre cele doua
// capete: un pachet de format vechi este respins de messageType(), in
// loc sa fie citit la offset-uri gresite si sa scoata o temperatura
// plauzibila si gresita, in tacere. Nu egala doua lungimi.
#define JOIN_REQ_LEN            10
#define JOIN_ACCEPT_LEN         3
#define DATA_UP_LEN             13
#define CMD_DOWN_LEN            4

#define DEV_EUI_LEN             8

// Comenzile de downlink.
#define CMD_TYPE_ACK            0x01
#define CMD_TYPE_RESET          0x02

#define SENSOR_REASON_INTERVAL  0x00
#define SENSOR_REASON_BUTTON    0x01

// Trimis de senzor cand codul ADC iese din tabelul NTC: senzor in scurt,
// deconectat, sau temperatura in afara domeniului -20...+100 C.
#define SENSOR_TEMP_INVALID     ((int16_t)-30000)

// Domeniul de adrese permis de PROTOCOL. 0x00 si 0xFF sunt rezervate
// (0xFF este si valoarea unei celule de flash nescrise pe senzor).
//
// Ce ALOCA hub-ul este mult mai ingust si nu se decide aici: DevAddr
// este numarul senzorului, 1..HUB_MAX_SENSORS, dat de pozitia lui in
// tabelul de provisioning din Config.h (DeviceRegistry::addressForEui).
// Limitele de mai jos raman ca verificare de sanitate a pachetelor si ca
// domeniu de valori valide daca reteaua creste vreodata.
#define DEV_ADDR_MIN            0x01
#define DEV_ADDR_MAX            0xFE

struct SensorPacket {
  uint8_t type;
  int16_t tempX100;   // temperatura in sutimi de grad
  uint8_t reason;

  bool isTemperature() const { return type == SENSOR_MSG_TEMPERATURE; }
  bool hasValidTemp()  const { return tempX100 != SENSOR_TEMP_INVALID; }
};

// Campurile utile ale unui JOIN_REQ, dupa despachetare.
struct JoinRequest {
  uint8_t devEui[DEV_EUI_LEN];
};

// Campurile utile ale unui DATA_UP, dupa despachetare. Payload-ul este
// pachetul de temperatura de 6 octeti, in clar, bit cu bit cel vechi:
// se da neschimbat lui decode().
struct SensorData {
  uint8_t  devAddr;
  uint32_t frameCounter;
  const uint8_t* payload;   // 6 octeti, in clar
};

namespace SensorPacketCodec {

  // --- Pachetul de temperatura (neschimbat) --------------------------

  // Verifica lungimea, magic-ul si checksum-ul, apoi completeaza "out".
  // Intoarce false pentru orice pachet care nu este al nostru sau este
  // corupt; in acest caz "out" ramane neatins.
  bool decode(const uint8_t* buffer, int length, SensorPacket& out);

  // Afiseaza pachetul pe Serial, pe o singura linie, fara sfarsit de
  // linie: apelantul adauga ce mai vrea (RSSI, SNR) si incheie.
  void print(const SensorPacket& packet);

  // Afiseaza octetii bruti in hexazecimal. Pentru pachetele respinse,
  // ca sa se vada CE a venit de fapt.
  void printRaw(const uint8_t* buffer, int length);

  // --- Pairing --------------------------------------------------------

  // Ce fel de pachet este, dupa magic + lungime + tip. Intoarce 0 daca
  // nu este niciunul dintre ale noastre.
  uint8_t messageType(const uint8_t* buffer, int length);

  // Despacheteaza un JOIN_REQ.
  bool parseJoinRequest(const uint8_t* buffer, int length, JoinRequest& out);

  // Despacheteaza un DATA_UP.
  bool parseData(const uint8_t* buffer, int length, SensorData& out);

  // Construieste JOIN_ACCEPT in "out" (JOIN_ACCEPT_LEN octeti).
  void buildJoinAccept(uint8_t* out, uint8_t devAddr);

  // Construieste CMD_DOWN in "out" (CMD_DOWN_LEN octeti).
  void buildCommand(uint8_t* out, uint8_t devAddr, uint8_t commandType);

  // Afiseaza un DevEUI ca 8 octeti hexazecimali lipiti, fara sfarsit de
  // linie. Aceeasi forma este acceptata de comanda `remove`.
  void printEui(const uint8_t* devEui);
}

#endif // SENSOR_PACKET_H
