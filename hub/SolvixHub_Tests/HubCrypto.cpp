#include "HubCrypto.h"

// Nicio biblioteca externa: XTEA incape in cateva zeci de linii si
// trebuie oricum sa fie identic, bit cu bit, cu implementarea de pe
// senzor (senzor/main.c, sectiunea 6).

namespace HubCrypto {

  static const uint8_t  XTEA_ROUNDS = 32;
  static const uint32_t XTEA_DELTA  = 0x9E3779B9UL;

  // Despacheteaza cheia de 16 octeti in 4 cuvinte de 32 de biti,
  // big-endian. Pe ESP32 deplasarile pe 32 de biti sunt gratuite, deci
  // aici le scriem explicit; senzorul obtine acelasi rezultat mutand
  // octeti printr-o uniune, fiindca pe PIC16 fiecare deplasare ar fi o
  // bucla din biblioteca.
  static void loadKey(const uint8_t* key, uint32_t* k) {
    for (int i = 0; i < 4; i++) {
      k[i] = ((uint32_t)key[(i * 4) + 0] << 24) |
             ((uint32_t)key[(i * 4) + 1] << 16) |
             ((uint32_t)key[(i * 4) + 2] << 8)  |
              (uint32_t)key[(i * 4) + 3];
    }
  }

  static void encryptWords(uint32_t* v0io, uint32_t* v1io, const uint32_t* k) {
    uint32_t v0 = *v0io;
    uint32_t v1 = *v1io;
    uint32_t sum = 0;

    for (uint8_t round = 0; round < XTEA_ROUNDS; round++) {
      v0 += ((((v1 << 4) ^ (v1 >> 5)) + v1) ^ (sum + k[sum & 3]));
      sum += XTEA_DELTA;
      v1 += ((((v0 << 4) ^ (v0 >> 5)) + v0) ^ (sum + k[(sum >> 11) & 3]));
    }

    *v0io = v0;
    *v1io = v1;
  }

  void encryptBlock(const uint8_t* key, const uint8_t* in, uint8_t* out) {
    uint32_t k[4];
    loadKey(key, k);

    uint32_t v0 = ((uint32_t)in[0] << 24) | ((uint32_t)in[1] << 16) |
                  ((uint32_t)in[2] << 8)  |  (uint32_t)in[3];
    uint32_t v1 = ((uint32_t)in[4] << 24) | ((uint32_t)in[5] << 16) |
                  ((uint32_t)in[6] << 8)  |  (uint32_t)in[7];

    encryptWords(&v0, &v1, k);

    out[0] = (uint8_t)(v0 >> 24);
    out[1] = (uint8_t)(v0 >> 16);
    out[2] = (uint8_t)(v0 >> 8);
    out[3] = (uint8_t)(v0);
    out[4] = (uint8_t)(v1 >> 24);
    out[5] = (uint8_t)(v1 >> 16);
    out[6] = (uint8_t)(v1 >> 8);
    out[7] = (uint8_t)(v1);
  }

  void mac(const uint8_t* key, const uint8_t* message, size_t length,
           uint8_t* out) {
    uint8_t x[XTEA_BLOCK_LEN];
    memset(x, 0, XTEA_BLOCK_LEN);

    for (size_t offset = 0; offset < length; offset += XTEA_BLOCK_LEN) {
      for (size_t i = 0; i < XTEA_BLOCK_LEN; i++) {
        // Ultimul bloc se completeaza cu zerouri, exact ca pe senzor.
        if (offset + i < length) x[i] ^= message[offset + i];
      }
      encryptBlock(key, x, x);
    }

    memcpy(out, x, XTEA_BLOCK_LEN);
  }

  void macMic(const uint8_t* key, const uint8_t* message, size_t length,
              uint8_t* mic) {
    uint8_t full[XTEA_BLOCK_LEN];
    mac(key, message, length, full);
    memcpy(mic, full, MIC_LEN);
  }

  bool macVerify(const uint8_t* key, const uint8_t* message, size_t length,
                 const uint8_t* receivedMic) {
    uint8_t mine[MIC_LEN];
    macMic(key, message, length, mine);

    // Comparatie fara iesire timpurie: nu are cine sa masoare timpi pe un
    // link LoRa, dar obiceiul bun nu costa nimic aici.
    uint8_t diff = 0;
    for (int i = 0; i < MIC_LEN; i++) {
      diff |= (uint8_t)(mine[i] ^ receivedMic[i]);
    }
    return diff == 0;
  }

  void ctr(const uint8_t* key, const uint8_t* iv, uint8_t* data,
           size_t length) {
    uint8_t counter[XTEA_BLOCK_LEN];
    uint8_t stream[XTEA_BLOCK_LEN];
    memcpy(counter, iv, XTEA_BLOCK_LEN);

    size_t done = 0;
    while (done < length) {
      encryptBlock(key, counter, stream);

      size_t chunk = length - done;
      if (chunk > XTEA_BLOCK_LEN) chunk = XTEA_BLOCK_LEN;

      for (size_t i = 0; i < chunk; i++) {
        data[done + i] ^= stream[i];
      }
      done += chunk;

      // Incrementare pe ultimul octet, ca pe senzor: pachetele noastre
      // nu depasesc niciodata un bloc, deci nu exista carry de propagat.
      counter[XTEA_BLOCK_LEN - 1]++;
    }
  }

  void buildDataIv(uint8_t* iv, uint8_t devAddr, uint32_t frameCounter,
                   uint8_t direction) {
    memset(iv, 0, XTEA_BLOCK_LEN);
    iv[0] = devAddr;
    iv[1] = (uint8_t)((frameCounter >> 24) & 0xFF);
    iv[2] = (uint8_t)((frameCounter >> 16) & 0xFF);
    iv[3] = (uint8_t)((frameCounter >> 8) & 0xFF);
    iv[4] = (uint8_t)(frameCounter & 0xFF);
    iv[5] = direction;
  }

  void buildJoinIv(uint8_t* iv, uint16_t devNonce) {
    memset(iv, 0, XTEA_BLOCK_LEN);
    iv[0] = SENSOR_MSG_JOIN_ACCEPT;
    iv[1] = (uint8_t)((devNonce >> 8) & 0xFF);
    iv[2] = (uint8_t)(devNonce & 0xFF);
  }

  void deriveSessionKey(const uint8_t* appKey, uint16_t devNonce,
                        const uint8_t* joinNonce, uint8_t devAddr,
                        uint8_t* sessKeyOut) {
    uint8_t block[XTEA_BLOCK_LEN];

    block[0] = 0x01;
    block[1] = (uint8_t)((devNonce >> 8) & 0xFF);
    block[2] = (uint8_t)(devNonce & 0xFF);
    block[3] = joinNonce[0];
    block[4] = joinNonce[1];
    block[5] = joinNonce[2];
    block[6] = devAddr;
    block[7] = 0x00;

    mac(appKey, block, XTEA_BLOCK_LEN, &sessKeyOut[0]);

    block[0] = 0x02;
    mac(appKey, block, XTEA_BLOCK_LEN, &sessKeyOut[XTEA_BLOCK_LEN]);
  }
}
