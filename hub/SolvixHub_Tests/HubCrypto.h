/*
  HubCrypto.h - primitivele criptografice ale hub-ului.
  ---------------------------------------------------------------------
  DE CE SE NUMESTE "HubCrypto" SI NU "Crypto":
  numele scurt ar fi umbrit antetul <Crypto.h> al unei biblioteci
  Arduino. Arduino IDE pune folderul sketch-ului INAINTEA folderelor de
  biblioteci in calea de include, deci un Crypto.h al nostru ar fi gasit
  primul si biblioteca nu s-ar mai putea include pe ea insasi (F-021).
  Numele diferit inlatura complet problema si ramane valabil chiar acum,
  cand nu mai depindem de nicio biblioteca de criptografie.

  CIFRUL: XTEA-128 (bloc de 64 de biti, cheie de 128 de biti, 32 de
  runde, DELTA = 0x9E3779B9). Nu AES.

  DE CE NU AES (F-024): nodul senzor este un PIC16LF1508 cu 4096 de
  cuvinte de program si 256 de octeti de RAM. Masurat cu XC8, varianta pe
  AES-128 cerea 5250 de cuvinte si 286 de octeti - nu incapea nici cu
  toate solutiile de rezerva aplicate. Doar tabelele de substitutie ale
  AES ocupau 512 cuvinte. XTEA nu are niciun tabel si are nevoie de o
  singura directie (cifrare), fiindca atat MIC-ul cat si criptarea se
  construiesc peste ea. Hub-ul nu are constrangerea asta, dar cele doua
  capete TREBUIE sa faca exact acelasi lucru, deci si el foloseste XTEA.

  CE OFERA:
    - encryptBlock()   - un bloc de 8 octeti, cifrat cu XTEA-128;
    - mac()            - CBC-MAC-XTEA, cei 8 octeti intregi;
    - macMic()         - acelasi lucru, dar scrie doar primii 4 octeti
                         (MIC-ul protocolului nostru);
    - macVerify()      - compara MIC-ul primit cu cel calculat;
    - ctr()            - XTEA-CTR, aceeasi operatie la criptare si la
                         decriptare;
    - buildDataIv()    - blocul contor al lui DATA_ENC;
    - buildJoinIv()    - blocul contor al lui JOIN_ACCEPT;
    - deriveSessionKey() - SessKey din AppKey, DevNonce, JoinNonce si
                         DevAddr, exact ca pe senzor.

  Totul este scris aici, explicit, in loc sa fie luat de-a gata dintr-o
  biblioteca: asa formatul blocului contor si al padding-ului este
  vizibil in cod si poate fi comparat, octet cu octet, cu ce face
  senzorul in senzor/main.c, sectiunea 6. O nepotrivire de o pozitie
  intr-un IV nu da niciun mesaj de eroare, doar pachete care "nu se
  valideaza".

  NICIO BIBLIOTECA EXTERNA nu mai este necesara pentru criptografie.
*/

#ifndef HUB_CRYPTO_H
#define HUB_CRYPTO_H

#include <Arduino.h>
#include "SensorPacket.h"

namespace HubCrypto {

  // Un bloc de 8 octeti, cifrat cu XTEA-128. "in" si "out" pot fi
  // acelasi pointer. Octetii se interpreteaza big-endian, ca peste tot
  // in protocol.
  void encryptBlock(const uint8_t* key, const uint8_t* in, uint8_t* out);

  // CBC-MAC-XTEA peste "length" octeti, ultimul bloc completat cu
  // zerouri. "mac" primeste XTEA_BLOCK_LEN octeti.
  //
  // De ce este sigur un CBC-MAC simplu aici: toate mesajele protocolului
  // au lungime FIXA per tip, iar octetul TYPE se afla in primul bloc
  // acoperit. Vezi comentariul lung din senzor/main.c, sectiunea 6.
  void mac(const uint8_t* key, const uint8_t* message, size_t length,
           uint8_t* mac);

  // Ca mac(), dar scrie doar primii MIC_LEN octeti - MIC-ul nostru.
  void macMic(const uint8_t* key, const uint8_t* message, size_t length,
              uint8_t* mic);

  // true daca MIC-ul din "receivedMic" corespunde mesajului.
  bool macVerify(const uint8_t* key, const uint8_t* message, size_t length,
                 const uint8_t* receivedMic);

  // XTEA-CTR peste "data", pe loc. Aceeasi functie cripteaza si
  // decripteaza. "iv" este blocul contor initial, XTEA_BLOCK_LEN octeti.
  void ctr(const uint8_t* key, const uint8_t* iv, uint8_t* data,
           size_t length);

  // Blocul contor folosit de DATA_ENC:
  //   IV = DevAddr(1) | FrameCounter(4) | directie(1) | zero(2)
  // directie = 0x00 pentru uplink (singura folosita azi); campul exista
  // ca un downlink criptat sa nu poata refolosi acelasi flux de chei.
  void buildDataIv(uint8_t* iv, uint8_t devAddr, uint32_t frameCounter,
                   uint8_t direction = 0x00);

  // Blocul contor folosit de JOIN_ACCEPT:
  //   IV = 0x11 | DevNonce(2) | zero(5)
  // DevNonce este diferit la fiecare incercare de inrolare, deci fluxul
  // de chei nu se repeta intre doua join-uri ale aceluiasi senzor.
  void buildJoinIv(uint8_t* iv, uint16_t devNonce);

  // SessKey, identica pe cele doua capete. MAC-ul da 8 octeti, cheia are
  // 16, deci se cheama de doua ori peste acelasi bloc, cu prefix diferit:
  //   B = <prefix> | DevNonce(2) | JoinNonce(3) | DevAddr(1) | 0x00
  //   SessKey[0..7]  = MAC(AppKey, B cu prefix 0x01)
  //   SessKey[8..15] = MAC(AppKey, B cu prefix 0x02)
  // Blocul are exact 8 octeti, cat blocul cifrului, deci nu apare
  // padding - la fel ca Session_DeriveKey() din senzor/main.c.
  void deriveSessionKey(const uint8_t* appKey, uint16_t devNonce,
                        const uint8_t* joinNonce, uint8_t devAddr,
                        uint8_t* sessKeyOut);
}

#endif // HUB_CRYPTO_H
