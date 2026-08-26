/*
  Config.h - toate pinii si constantele hardware ale hub-ului, intr-un
  singur loc. Nimic din restul proiectului nu are voie sa scrie un numar
  de pin "in clar" - totul se refera aici.
*/

#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// ---------------------------------------------------------------------
// MAGISTRALA SPI (COMUNA pentru Ethernet ENC28J60 si LoRa SX127x)
// ---------------------------------------------------------------------
// Ambele module stau pe ACELEASI trei fire. Singurul lucru care le
// separa este pinul CS/NSS: doar un modul are voie sa fie selectat
// (CS = LOW) la un moment dat.
#define PIN_SPI_SCK    18
#define PIN_SPI_MISO   19
#define PIN_SPI_MOSI   23

// ---------------------------------------------------------------------
// ETHERNET - ENC28J60
// ---------------------------------------------------------------------
#define PIN_ETH_CS     4    // CS_ETH (atentie: NU este GPIO5)
#define PIN_ETH_RESET  32   // Reset_eth, activ pe LOW

// ---------------------------------------------------------------------
// LoRa - SX1276/78
// ---------------------------------------------------------------------
#define PIN_LORA_NSS   5    // chip select LoRa
#define PIN_LORA_RST   14
#define PIN_LORA_DIO0  26

#define LORA_FREQUENCY 868E6  // banda europeana

// Parametrii de modulatie. Trebuie sa fie IDENTICI cu cei scrisi in
// registrele SX1276 de firmware-ul nodului senzor (PIC16LF1508), altfel
// pachetele nu se vad deloc - si NU apare niciun mesaj de eroare, pur si
// simplu nu vine nimic. Corespondenta, registru cu registru:
//
//   868.0 MHz    <- RegFrf          = 0xD9 0x00 0x00
//   BW 125 kHz   <- RegModemConfig1 biti 7:4 = 0111   (0x72)
//   CR 4/5       <- RegModemConfig1 biti 3:1 = 001    (0x72)
//   header expl. <- RegModemConfig1 bit 0    = 0      (0x72)
//   SF7          <- RegModemConfig2 biti 7:4 = 0111   (0x74)
//   CRC activ    <- RegModemConfig2 bit 2    = 1      (0x74)
//   sync word    <- RegSyncWord ramane la valoarea de RESET 0x12,
//                   fiindca senzorul nu il scrie explicit
//   ~14 dBm      <- RegPaConfig     = 0x8F (PA_BOOST)
//
// PAIRING-UL NU SCHIMBA NICIUNUL DINTRE ACESTI PARAMETRI. Inrolarea si
// datele criptate circula pe exact aceeasi modulatie ca pachetul de
// temperatura in clar; se schimba doar continutul pachetelor.
#define LORA_SPREADING_FACTOR  7
#define LORA_BANDWIDTH_HZ      125E3
#define LORA_CODING_RATE_4     5      // adica 4/5
#define LORA_SYNC_WORD         0x12
#define LORA_PREAMBLE_LENGTH   8
#define LORA_TX_POWER_DBM      14

// ---------------------------------------------------------------------
// BUTOANE
// ---------------------------------------------------------------------
// GPIO34 si GPIO35 sunt pini DOAR de intrare si NU au pull-up sau
// pull-down intern. Este obligatoriu un rezistor extern pe placa.
#define PIN_BUTTON_1   34
#define PIN_BUTTON_2   35

// ---------------------------------------------------------------------
// LED-uri
// ---------------------------------------------------------------------
// Etichetele D22 / D21 de pe placa inseamna GPIO22 si GPIO21. Niciunul
// nu intra in conflict cu SPI (18/19/23), ENC28J60 (4/32), LoRa
// (5/14/26) sau butoanele (34/35).
#define PIN_LED_1      22   // D22 - LED de activitate (ex: pachet primit)
#define PIN_LED_2      21   // D21 - LED de stare (ex: radio pornit)

// Nivelul logic care APRINDE un LED. Anod la GPIO si catod la GND prin
// rezistor inseamna HIGH; daca pe placa sunt cablate invers, se pune LOW.
#define LED_ON_LEVEL   HIGH

// Cat sta aprins un LED la un puls de activitate, in milisecunde.
#define LED_PULSE_MS   120

// ---------------------------------------------------------------------
// DIVERSE
// ---------------------------------------------------------------------
#define SERIAL_BAUD    115200

// Viteza SPI folosita cand vorbim cu ENC28J60.
// Datasheet-ul admite pana la 20 MHz; 8 MHz este un compromis sigur pe
// cablaje de prototip. Pentru diagnostic se coboara la 1 MHz.
#define ETH_SPI_HZ         8000000
#define ETH_SPI_HZ_DEBUG   1000000

// Adresa MAC folosita de placa in retea (poate fi orice, dar trebuie
// sa fie unica in reteaua locala).
extern byte HUB_MAC[6];

// =====================================================================
// PAIRING CRIPTAT
// =====================================================================
// Constantele de mai jos privesc inrolarea senzorilor, criptarea datelor
// si registrul de device-uri. Formatul pachetelor NU este aici, ci in
// SensorPacket.h, care este oglinda sectiunii 4 din senzor/main.c.

// Cat timp ramane hub-ul in "mod pairing" dupa comanda `pair` sau dupa
// apasarea butonului, in milisecunde. Dupa expirare, orice JOIN_REQ este
// refuzat: un senzor nu se poate inrola pe furis, cand nimeni nu se uita.
#define PAIRING_MODE_TIMEOUT_MS   120000UL

// Cat de repede clipeste LED 2 cat timp hub-ul este in mod pairing.
// Aprins continuu inseamna doar "radioul asculta".
#define PAIRING_BLINK_MS          250UL

// 1 = payload-ul de temperatura din DATA_ENC este criptat cu XTEA-CTR.
// 0 = solutia de rezerva pentru cazul in care nici XTEA nu ar mai incapea
//     in flash-ul senzorului: payload-ul circula in clar, dar ramane
//     autentificat cu MIC. TREBUIE sa aiba aceeasi valoare ca
//     PAIRING_ENCRYPT_PAYLOAD din senzor/main.c - altfel hub-ul
//     "decripteaza" un text clar si obtine gunoi (F-023).
//
// Cu XTEA, senzorul incape in PIC16LF1508 cu payload-ul criptat, deci
// valoarea normala este 1.
#define PAIRING_ENCRYPT_PAYLOAD   1

// 1 = dupa fiecare DATA_ENC valid, hub-ul trimite un CMD_DOWN de tip ACK.
//     Senzorul deschide oricum o fereastra de receptie dupa transmisie,
//     deci nu costa nimic in plus la el; costa insa timp de emisie pe
//     hub. Se poate lasa pe 0 fara nicio consecinta functionala.
#define PAIRING_SEND_ACK          1

// Cat timp trebuie sa taca un senzor marcat cu `remove` inainte ca
// dezinrolarea sa fie considerata CONFIRMATA si inregistrarea lui sa fie
// stearsa din registru.
//
// Tacerea este singurul semnal pe care il avem ca senzorul a primit
// CMD_DOWN(RESET): dupa el isi sterge sesiunea din HEF si intra in
// repaus, de unde nu mai emite nimic (F-030). Cat timp inca se aude,
// inseamna ca nu a primit comanda si i se retrimite (F-031).
//
// **ACEASTA CONSTANTA ESTE LEGATA DE INTERVALUL DE SOMN AL SENZORULUI.**
// Senzorul nu mai este in veghe continua: intre doua pachete doarme
// SLEEP_WAKEUPS x o perioada WDT (senzor/main.c), adica ~29,6 s nominal,
// si 25-34 s in realitate, fiindca WDT-ul merge pe LFINTOSC cu toleranta
// larga. Un senzor adormit TACE - exact semnalul pe care hub-ul il
// foloseste ca dovada ca a primit RESET-ul.
//
// Valoarea trebuie deci sa acopere confortabil mai multe cicluri de somn:
// 120 s inseamna patru cicluri nominale, sau ~3,5 in cazul cel mai lent.
// Prea mica este PERICULOS, nu doar incomod: hub-ul ar declara
// dezinrolarea confirmata in timp ce senzorul doar doarme, ar sterge
// inregistrarea SI cheia de sesiune, iar la trezire senzorul ar emite cu
// cheia veche fara ca hub-ul sa-l mai poata opri vreodata - fundatura din
// F-031, de data asta fara iesire. Prea mare doar intarzie reinrolarea.
//
// Daca schimbi SLEEP_WAKEUPS pe senzor, schimbi si valoarea de aici
// (regula 11 din CLAUDE.md, sectiunea 10).
#define REMOVE_CONFIRM_SILENCE_MS 120000UL

// 1 = dupa o dezinrolare CONFIRMATA, hub-ul redeschide singur fereastra
//     de pairing, ca operatorul sa nu alerge inapoi la tastatura.
// 0 = fereastra se deschide manual, cu `pair`.
//
// Fereastra se redeschide DOAR ca urmare a unei comenzi `remove` data de
// un om, deci regula "un senzor nu se poate inrola pe furis, cand nimeni
// nu se uita" ramane respectata. Nu este insa suficienta singura: dupa
// RESET senzorul sta in DEV_STATE_IDLE si intra in inrolare numai daca i
// se tine butonul 2 apasat trei secunde (F-030).
#define PAIRING_REOPEN_AFTER_REMOVE 1

// La cate pachete de la un DevAddr neinrolat se repeta sfatul de
// recuperare. Un senzor ramas cu o cheie veche emite la fiecare 5 s;
// sfatul la fiecare pachet ar ineca Serial-ul.
#define PAIRING_UNKNOWN_HINT_EVERY 10

// Cati senzori incap in registru. Fiecare inregistrare are ~40 de octeti,
// deci limita este data de bunul simt, nu de memorie.
#define REGISTRY_MAX_DEVICES      8

// Numele spatiului NVS (Preferences) in care se salveaza registrul.
// Maximum 15 caractere - asa cere NVS.
#define REGISTRY_NVS_NAMESPACE    "solvix-pair"

// La cate pachete de date se rescrie registrul in NVS. Frame counter-ul
// se tine in RAM intre salvari: NVS-ul este tot flash si nu are rost sa
// fie scris la fiecare pachet. Dupa o repornire a hub-ului, contorul
// asteptat poate fi in urma cu cel mult atat, ceea ce nu strica nimic -
// verificarea anti-replay cere doar sa fie STRICT CRESCATOR.
#define REGISTRY_SAVE_EVERY       20

// ---------------------------------------------------------------------
// LISTA DE PROVISIONING: DevEUI -> AppKey
// ---------------------------------------------------------------------
// Numai senzorii de aici se pot inrola. AppKey nu circula niciodata prin
// aer: serveste la verificarea JOIN_REQ, la cifrarea JOIN_ACCEPT si la
// derivarea cheii de sesiune.
//
// Fiecare rand trebuie sa corespunda EXACT cu PROVISION_DEV_EUI si
// PROVISION_APP_KEY din senzor/main.c ale placii respective. O singura
// diferenta si senzorul este respins cu "MIC gresit", fara alt indiciu.
//
// Tabelul este instantiat in DeviceRegistry.cpp; aici stau doar valorile,
// ca sa ramana adevarata regula "constantele traiesc in Config.h".
#define PROVISIONED_DEVICES_INIT { \
  { { 0x53, 0x4F, 0x4C, 0x56, 0x49, 0x58, 0x00, 0x01 },                     \
    { 0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,                       \
      0x88, 0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF } },                   \
}

#endif // CONFIG_H
