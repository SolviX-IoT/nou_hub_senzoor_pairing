/*
  SensorPacket.h - pachetele schimbate cu nodul senzor (PIC16LF1508).
  ---------------------------------------------------------------------
  ACEST FISIER ESTE OGLINDA sectiunii 4 de protocol din senzor/main.c.
  Orice modificare aici trebuie facuta si acolo, in acelasi commit
  (regula 10 din CLAUDE.md).

  Toate campurile multi-octet sunt big-endian. Primul octet ramane
  magic-ul 0xA5 din protocolul initial; ce s-a schimbat odata cu
  pairing-ul este ca octetul TYPE are acum mai multe valori.

  ---------------------------------------------------------------------
  0x01 - TEMP_PLAIN, pachetul initial de 6 octeti (NESCHIMBAT)
  ---------------------------------------------------------------------
    [0] MAGIC     = 0xA5
    [1] TYPE      = 0x01
    [2] TEMP_HI   } int16, temperatura in SUTIMI de grad Celsius
    [3] TEMP_LO   } (2350 inseamna 23.50 C)
    [4] REASON    = 0x00 la interval periodic, 0x01 la apasare de buton
    [5] CHECKSUM  = (b0 ^ b1 ^ b2 ^ b3 ^ b4) ^ 0x5A

  Acesta este si payload-ul care circula criptat in DATA_ENC: dupa
  decriptare se da neschimbat lui decode(), deci nu exista doua cai
  diferite de interpretare a temperaturii.

  ---------------------------------------------------------------------
  CIFRUL: XTEA-128, bloc de 8 octeti (vezi HubCrypto.h si F-024).
  MIC = primii 4 octeti din CBC-MAC-XTEA.
  ---------------------------------------------------------------------

  ---------------------------------------------------------------------
  0x10 - JOIN_REQ (senzor -> hub), 16 octeti
  ---------------------------------------------------------------------
    [0] 0xA5  [1] 0x10  [2..9] DevEUI  [10..11] DevNonce
    [12..15] MIC = MAC(AppKey, bytes[0..11])

  ---------------------------------------------------------------------
  0x11 - JOIN_ACCEPT (hub -> senzor), 10 octeti
  ---------------------------------------------------------------------
    IV_join (8B) = 0x11 | DevNonce(2) | zero(5)
    [0] 0xA5  [1] 0x11
    [2..5] Enc = XTEA-CTR(AppKey, IV_join, DevAddr(1) | JoinNonce(3))
    [6..9] MIC = MAC(AppKey,
                     0x11 | DevEUI(8) | DevNonce(2) | DevAddr(1) | JoinNonce(3))

  CTR este simetric, deci senzorul nu are nevoie de cod de descifrare -
  exact ce trebuia ca sa incapa in cei 4096 de cuvinte ai lui
  PIC16LF1508. IV-ul depinde de DevNonce, care e nou la fiecare
  incercare, deci un JOIN_ACCEPT rejucat se descifreaza in gunoi SI pica
  la MIC.

  Cheia de sesiune se deriva la fel pe ambele capete. MAC-ul da 8 octeti,
  cheia are 16, deci se cheama de doua ori peste acelasi bloc:
    B = <prefix> | DevNonce(2) | JoinNonce(3) | DevAddr(1) | 0x00
    SessKey[0..7]  = MAC(AppKey, B cu prefix 0x01)
    SessKey[8..15] = MAC(AppKey, B cu prefix 0x02)

  ---------------------------------------------------------------------
  0x12 - DATA_ENC (senzor -> hub), 17 octeti
  ---------------------------------------------------------------------
    [0] 0xA5  [1] 0x12  [2] DevAddr  [3..6] FrameCounter
    [7..12] EncPayload = XTEA-CTR(SessKey, IV, pachetul TEMP de 6 octeti)
            IV (8B) = DevAddr(1) | FrameCounter(4) | 0x00 (uplink) | zero(2)
    [13..16] MIC = MAC(SessKey, bytes[0..12])

  ---------------------------------------------------------------------
  0x13 - CMD_DOWN (hub -> senzor), 12 octeti
  ---------------------------------------------------------------------
    [0] 0xA5  [1] 0x13  [2] DevAddr  [3..6] FrameCounter downlink
    [7] CmdType (0x01 = ACK, 0x02 = RESET / dezinrolare)
    [8..11] MIC = MAC(SessKey, bytes[0..7])

  ---------------------------------------------------------------------
  De ce se pastreaza checksum-ul XOR in interiorul payload-ului criptat,
  cand MIC-ul acopera deja tot pachetul: pentru ca pachetul de 6 octeti
  sa ramana BIT CU BIT cel vechi. Asa hub-ul il poate da direct lui
  decode(), fara nicio logica noua de parsare a temperaturii, iar testul
  7 (temperatura in clar) si testul 8 (pairing) folosesc acelasi cod.
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
#define SENSOR_MSG_DATA_ENC     0x12
#define SENSOR_MSG_CMD_DOWN     0x13

// Lungimile fixe ale fiecarui tip.
#define JOIN_REQ_LEN            16
#define JOIN_ACCEPT_LEN         10
#define DATA_ENC_LEN            17
#define CMD_DOWN_LEN            12

// Zonele acoperite de MIC, in octeti. Aceleasi numere apar in
// senzor/main.c, sectiunea 4 - se schimba IMPREUNA.
#define JOIN_REQ_MIC_INPUT_LEN      12
#define JOIN_ACCEPT_MIC_INPUT_LEN   15
#define DATA_ENC_MIC_INPUT_LEN      13
#define CMD_DOWN_MIC_INPUT_LEN      8

// Campul cifrat din JOIN_ACCEPT: DevAddr(1) + JoinNonce(3).
#define JOIN_ACCEPT_ENC_LEN     4

// Dimensiuni comune. Blocul cifrului are 8 octeti (XTEA), cheia 16.
#define MIC_LEN                 4
#define XTEA_BLOCK_LEN          8
#define CRYPTO_KEY_LEN          16
#define DEV_EUI_LEN             8
#define DEV_NONCE_LEN           2
#define JOIN_NONCE_LEN          3

// Comenzile de downlink.
#define CMD_TYPE_ACK            0x01
#define CMD_TYPE_RESET          0x02

#define SENSOR_REASON_INTERVAL  0x00
#define SENSOR_REASON_BUTTON    0x01

// Trimis de senzor cand codul ADC iese din tabelul NTC: senzor in scurt,
// deconectat, sau temperatura in afara domeniului -20...+100 C.
#define SENSOR_TEMP_INVALID     ((int16_t)-30000)

// Adresele pe care hub-ul le poate aloca. 0x00 si 0xFF sunt rezervate
// (0xFF este si valoarea unei celule de flash nescrise pe senzor).
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
  uint8_t  devEui[DEV_EUI_LEN];
  uint16_t devNonce;
  const uint8_t* mic;   // pointer in bufferul primit, 4 octeti
};

// Campurile utile ale unui DATA_ENC, dupa despachetare. Payload-ul NU
// este inca decriptat aici: decriptarea are nevoie de cheia de sesiune,
// care se afla din registru dupa ce se stie DevAddr.
struct EncryptedData {
  uint8_t  devAddr;
  uint32_t frameCounter;
  const uint8_t* payload;   // 6 octeti, inca cifrati
  const uint8_t* mic;       // 4 octeti
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

  // Despacheteaza un JOIN_REQ. NU verifica MIC-ul: acela cere AppKey,
  // deci se face dupa ce DevEUI a fost cautat in lista de provisioning.
  bool parseJoinRequest(const uint8_t* buffer, int length, JoinRequest& out);

  // Despacheteaza un DATA_ENC. Nici acesta nu verifica MIC-ul, din
  // acelasi motiv: cheia se afla abia dupa ce se stie DevAddr.
  bool parseEncryptedData(const uint8_t* buffer, int length, EncryptedData& out);

  // Construieste JOIN_ACCEPT in "out" (JOIN_ACCEPT_LEN octeti).
  // Cifreaza blocul P cu AppKey si calculeaza MIC-ul.
  void buildJoinAccept(uint8_t* out,
                       const uint8_t* appKey,
                       const uint8_t* devEui,
                       uint16_t devNonce,
                       uint8_t devAddr,
                       const uint8_t* joinNonce);

  // Construieste CMD_DOWN in "out" (CMD_DOWN_LEN octeti).
  void buildCommand(uint8_t* out,
                    const uint8_t* sessKey,
                    uint8_t devAddr,
                    uint32_t downCounter,
                    uint8_t commandType);

  // Afiseaza un DevEUI ca 8 octeti hexazecimali lipiti, fara sfarsit de
  // linie. Aceeasi forma este acceptata de comanda `remove`.
  void printEui(const uint8_t* devEui);
}

#endif // SENSOR_PACKET_H
