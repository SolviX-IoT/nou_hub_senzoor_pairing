#include "SensorPacket.h"
#include "HubCrypto.h"

namespace SensorPacketCodec {

  bool decode(const uint8_t* buffer, int length, SensorPacket& out) {
    if (length != SENSOR_PACKET_LEN)          return false;
    if (buffer[0] != SENSOR_PACKET_MAGIC)     return false;

    uint8_t checksum = 0;
    for (int i = 0; i < SENSOR_PACKET_LEN - 1; i++) {
      checksum ^= buffer[i];
    }
    checksum ^= SENSOR_CHECKSUM_SALT;

    if (checksum != buffer[SENSOR_PACKET_LEN - 1]) return false;

    out.type = buffer[1];
    // int16 big-endian: octetul inalt intai, exact cum il pune senzorul.
    out.tempX100 = (int16_t)(((uint16_t)buffer[2] << 8) | (uint16_t)buffer[3]);
    out.reason = buffer[4];

    return true;
  }

  void print(const SensorPacket& packet) {
    if (!packet.isTemperature()) {
      Serial.print(F("tip necunoscut 0x"));
      Serial.print(packet.type, HEX);
    } else if (!packet.hasValidTemp()) {
      Serial.print(F("Temperatura: EROARE DE CITIRE (senzor in scurt, "));
      Serial.print(F("deconectat, sau in afara domeniului)"));
    } else {
      Serial.print(F("Temperatura: "));
      Serial.print(packet.tempX100 / 100.0f, 2);
      Serial.print(F(" C"));
    }

    Serial.print(F("  Motiv: "));
    switch (packet.reason) {
      case SENSOR_REASON_BUTTON:   Serial.print(F("BUTON"));    break;
      case SENSOR_REASON_INTERVAL: Serial.print(F("interval")); break;
      default:
        Serial.print(F("necunoscut ("));
        Serial.print(packet.reason);
        Serial.print(F(")"));
        break;
    }
  }

  void printRaw(const uint8_t* buffer, int length) {
    Serial.print(F("Pachet ignorat ("));
    Serial.print(length);
    Serial.print(F(" octeti):"));
    for (int i = 0; i < length; i++) {
      Serial.print(F(" 0x"));
      if (buffer[i] < 0x10) Serial.print('0');
      Serial.print(buffer[i], HEX);
    }
  }

  // -------------------------------------------------------------------
  // Pairing
  // -------------------------------------------------------------------

  uint8_t messageType(const uint8_t* buffer, int length) {
    if (length < 2)                       return 0;
    if (buffer[0] != SENSOR_PACKET_MAGIC) return 0;

    // Lungimea trebuie sa se potriveasca cu tipul: un pachet de alta
    // lungime nu este al nostru, oricat de bine ar incepe.
    switch (buffer[1]) {
      case SENSOR_MSG_TEMPERATURE:
        return (length == SENSOR_PACKET_LEN) ? SENSOR_MSG_TEMPERATURE : 0;
      case SENSOR_MSG_JOIN_REQ:
        return (length == JOIN_REQ_LEN) ? SENSOR_MSG_JOIN_REQ : 0;
      case SENSOR_MSG_JOIN_ACCEPT:
        return (length == JOIN_ACCEPT_LEN) ? SENSOR_MSG_JOIN_ACCEPT : 0;
      case SENSOR_MSG_DATA_ENC:
        return (length == DATA_ENC_LEN) ? SENSOR_MSG_DATA_ENC : 0;
      case SENSOR_MSG_CMD_DOWN:
        return (length == CMD_DOWN_LEN) ? SENSOR_MSG_CMD_DOWN : 0;
      default:
        return 0;
    }
  }

  bool parseJoinRequest(const uint8_t* buffer, int length, JoinRequest& out) {
    if (messageType(buffer, length) != SENSOR_MSG_JOIN_REQ) return false;

    for (int i = 0; i < DEV_EUI_LEN; i++) {
      out.devEui[i] = buffer[2 + i];
    }
    out.devNonce = (uint16_t)(((uint16_t)buffer[10] << 8) | (uint16_t)buffer[11]);
    out.mic = &buffer[12];

    return true;
  }

  bool parseEncryptedData(const uint8_t* buffer, int length, EncryptedData& out) {
    if (messageType(buffer, length) != SENSOR_MSG_DATA_ENC) return false;

    out.devAddr = buffer[2];
    out.frameCounter = ((uint32_t)buffer[3] << 24) |
                       ((uint32_t)buffer[4] << 16) |
                       ((uint32_t)buffer[5] << 8)  |
                       ((uint32_t)buffer[6]);
    out.payload = &buffer[7];
    out.mic     = &buffer[13];

    return true;
  }

  void buildJoinAccept(uint8_t* out,
                       const uint8_t* appKey,
                       const uint8_t* devEui,
                       uint16_t devNonce,
                       uint8_t devAddr,
                       const uint8_t* joinNonce) {
    out[0] = SENSOR_PACKET_MAGIC;
    out[1] = SENSOR_MSG_JOIN_ACCEPT;

    // DevAddr + JoinNonce, cifrate cu XTEA-CTR. IV-ul contine DevNonce,
    // deci fluxul de chei este altul la fiecare incercare de inrolare.
    out[2] = devAddr;
    out[3] = joinNonce[0];
    out[4] = joinNonce[1];
    out[5] = joinNonce[2];

    uint8_t iv[XTEA_BLOCK_LEN];
    HubCrypto::buildJoinIv(iv, devNonce);
    HubCrypto::ctr(appKey, iv, &out[2], JOIN_ACCEPT_ENC_LEN);

    // MIC peste campurile in CLAR, cu 0x11 in fata pentru separarea de
    // domeniu: senzorul il poate recalcula abia dupa ce a descifrat, deci
    // el confirma si ca descifrarea a reusit.
    uint8_t micInput[JOIN_ACCEPT_MIC_INPUT_LEN];
    micInput[0] = SENSOR_MSG_JOIN_ACCEPT;
    memcpy(&micInput[1], devEui, DEV_EUI_LEN);
    micInput[9]  = (uint8_t)((devNonce >> 8) & 0xFF);
    micInput[10] = (uint8_t)(devNonce & 0xFF);
    micInput[11] = devAddr;
    micInput[12] = joinNonce[0];
    micInput[13] = joinNonce[1];
    micInput[14] = joinNonce[2];

    HubCrypto::macMic(appKey, micInput, sizeof(micInput),
                      &out[2 + JOIN_ACCEPT_ENC_LEN]);
  }

  void buildCommand(uint8_t* out,
                    const uint8_t* sessKey,
                    uint8_t devAddr,
                    uint32_t downCounter,
                    uint8_t commandType) {
    out[0] = SENSOR_PACKET_MAGIC;
    out[1] = SENSOR_MSG_CMD_DOWN;
    out[2] = devAddr;
    out[3] = (uint8_t)((downCounter >> 24) & 0xFF);
    out[4] = (uint8_t)((downCounter >> 16) & 0xFF);
    out[5] = (uint8_t)((downCounter >> 8) & 0xFF);
    out[6] = (uint8_t)(downCounter & 0xFF);
    out[7] = commandType;

    HubCrypto::macMic(sessKey, out, CMD_DOWN_MIC_INPUT_LEN,
                      &out[CMD_DOWN_MIC_INPUT_LEN]);
  }

  void printEui(const uint8_t* devEui) {
    for (int i = 0; i < DEV_EUI_LEN; i++) {
      if (devEui[i] < 0x10) Serial.print('0');
      Serial.print(devEui[i], HEX);
    }
  }
}
