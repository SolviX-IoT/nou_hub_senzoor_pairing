/*
 * =====================================================================
 *  SolviX - NOD SENZOR  (varianta cu PAIRING CRIPTAT)
 *  PIC16LF1508 @ 16 MHz (INTOSC), MPLAB X + XC8
 * =====================================================================
 *
 *  CE FACE:
 *    - la prima pornire (sau dupa un RESET primit de la hub) se
 *      INROLEAZA: trimite JOIN_REQ semnat cu AppKey si asteapta
 *      JOIN_ACCEPT, din care afla DevAddr si JoinNonce;
 *    - din DevNonce + JoinNonce + DevAddr deriva SessKey si o scrie,
 *      impreuna cu DevAddr, in memoria ne-volatila (HEF);
 *    - dupa inrolare citeste termistorul NTC 10K / B=3950 de pe RC2
 *      (canal AN6) si trimite temperatura CRIPTATA (DATA_ENC) la
 *      intervalul de somn (vezi SLEEP_WAKEUPS), sau imediat la apasarea
 *      butonului de pe RC4;
 *    - dupa fiecare transmisie deschide o fereastra scurta de receptie
 *      pentru un eventual CMD_DOWN (ACK sau RESET).
 *
 *  LED-uri:
 *    - LED 1 (RC3) = transmisie de date (un puls la fiecare DATA_ENC);
 *    - LED 2 (RC6) = mod pairing / eroare de join.
 *
 *  CE S-A PASTRAT NEATINS din firmware-ul de temperatura:
 *    driverul LoRa de emisie, ADC-ul, tabelul NTC, debounce-ul de buton
 *    si pachetul de temperatura de 6 octeti. Pachetul de 6 octeti NU a
 *    fost modificat: el este exact ce se cripteaza in DATA_ENC, iar
 *    hub-ul il da, dupa decriptare, aceluiasi SensorPacketCodec::decode()
 *    care exista deja.
 *
 *  CE ESTE NOU:
 *    - receptie LoRa (LoRa_Receive), necesara pentru JOIN_ACCEPT si
 *      CMD_DOWN. Firmware-ul anterior era doar TX;
 *    - XTEA-128, cu CBC-MAC pentru MIC si CTR pentru criptare
 *      (NU AES: nu incape in acest device - vezi sectiunea 6 si F-024);
 *    - memorie ne-volatila pe HEF (High-Endurance Flash);
 *    - masina de stari JOINING / OPERATING.
 *
 *  FORMATUL PACHETELOR este oglindit pe hub in
 *  hub/SolvixHub_Tests/SensorPacket.h.
 *  Orice modificare aici trebuie facuta si acolo, in acelasi commit.
 *
 * ---------------------------------------------------------------------
 *  PRESUPUNERI (nu sunt deduse din codul existent - verifica pe placa!)
 * ---------------------------------------------------------------------
 *  1) NTC: termistorul este intre RC2 si GND, iar intre RC2 si VDD exista
 *     un rezistor FIX de 10 kOhm. Daca pe placa e invers (fix spre GND,
 *     NTC spre VDD), pune NTC_PULLUP_TO_VDD pe 0.
 *  2) LoRa RESET nu apare in codul existent -> se presupune legat la VDD
 *     sau lasat in aer. Se foloseste doar soft-reset prin RegOpMode.
 *  3) LoRa DIO0 nu apare in codul existent -> TxDone SI RxDone se afla
 *     prin polling pe RegIrqFlags, nu prin intrerupere.
 *  4) Butoanele sunt ACTIVE HIGH cu pull-down EXTERN (PORTC nu are
 *     weak pull-up pe acest device). Asa erau tratate si in codul vechi.
 *  5) Butonul "de fortare" este cel de pe RC4; RC5 deschide fereastra de
 *     pairing daca este tinut apasat ~3 secunde (F-030).
 *  6) RC1 este LIBER si neconectat. Nu primeste cod: ramane pe
 *     configuratia MCC din pins.c, adica intrare analogica, ceea ce
 *     pentru un pin nefolosit este exact starea buna - bufferul digital
 *     de intrare este dezactivat, deci un nivel flotant nu consuma
 *     curent. Senzorul este alimentat permanent.
 *  7) Nu exista niciun pin de "switch" in proiectul existent.
 *  8) HEF: blocul de stergere si grupul de latch-uri au 32 de cuvinte.
 *     NU mai este o presupunere - este citit din fisierul de device
 *     support al lui XC8 (FLASH_ERASE=20, FLASH_WRITE=20, hexazecimal).
 *     Vezi sectiunea 5 si F-026.
 *  9) DevEUI si AppKey se provizioneaza per unitate din blocul
 *     PROVISION_* de mai jos: la prima pornire, daca regiunea de
 *     provisioning din HEF e goala, valorile de compilare sunt scrise
 *     acolo. FIECARE placa trebuie compilata cu alt DevEUI si alta
 *     AppKey, iar aceeasi pereche trebuie trecuta in tabelul din
 *     hub/SolvixHub_Tests/Config.h.
 * 10) PIC16LF1508 nu are generator de numere aleatoare. DevNonce se
 *     obtine din bitii cei mai putin semnificativi ai unor citiri ADC
 *     succesive de pe NTC, amestecati cu frame counter-ul salvat in HEF.
 *     Este suficient ca doua incercari de join sa nu foloseasca acelasi
 *     nonce, dar NU este o sursa criptografica de entropie.
 *
 *  CONFIGURATION BITS: sunt cele generate de MCC in
 *  mcc_generated_files/system/src/config_bits.c
 *  (FOSC=INTOSC, WDTE=OFF, MCLRE=ON, BOREN=ON, LVP=ON, PWRTE=OFF).
 *  IMPORTANT pentru HEF: WRT=OFF, adica memoria de program NU este
 *  protejata la scriere. Daca cineva pune WRT pe altceva, scrierile in
 *  HEF esueaza in tacere si senzorul reia pairing-ul la fiecare pornire.
 * =====================================================================
 */

#include "mcc_generated_files/system/system.h"
#include <stdint.h>

/* ---------------------------------------------------------------------
 * Configuration bits - DOAR daca nu folosesti config_bits.c generat.
 * In acest proiect, config_bits.c ii furnizeaza deja, deci blocul ramane
 * inactiv.
 * ------------------------------------------------------------------ */
#if 0
#pragma config FOSC = INTOSC, WDTE = OFF, PWRTE = OFF, MCLRE = ON
#pragma config CP = OFF, BOREN = ON, CLKOUTEN = OFF, IESO = ON, FCMEN = ON
#pragma config WRT = OFF, STVREN = ON, BORV = LO, LVP = ON, LPBOR = OFF
#endif

/* =====================================================================
 * 1. PARAMETRI USOR DE MODIFICAT
 * ================================================================== */

/*
 * Intervalul dintre doua transmisii NU mai este in milisecunde, fiindca
 * nu mai este o asteptare activa: intre pachete senzorul DOARME, iar
 * trezirea o da watchdog-ul. Durata se exprima deci in numar de treziri
 * WDT, nu intr-o valoare de timp.
 *
 * De ce fragmentat si nu un singur somn lung: butonul 2 este pe RC5,
 * adica pe PORTC, iar acest device are interrupt-on-change DOAR pe PORTA
 * si PORTB (in pic16lf1508.h exista IOCAF/IOCAN/IOCAP si IOCBF/IOCBN/
 * IOCBP, dar niciun registru IOCC*). Un senzor adormit nu poate fi deci
 * trezit de buton, si cele trei secunde de apasare pentru re-pairing
 * (F-030) ar deveni inutilizabile. Asa ca doarme in reprize scurte si
 * verifica butoanele la fiecare trezire: doarme in continuare ~99% din
 * timp, dar butonul raspunde in cel mult o perioada WDT.
 *
 * WDTPS = 0b01011 inseamna 1:65536 pe LFINTOSC (~31 kHz), adica
 * 65536/31000 = ~2,11 s pe trezire. Este si valoarea de reset a lui
 * WDTCON, dar se scrie explicit ca sa nu depinda de ea.
 */
#define SLEEP_WDT_WDTPS         0x0BU   /* 1:65536 -> ~2,11 s */

/*
 * --- Durata somnului: NU este aceeasi pe toti senzorii ---------------
 *
 * Pe canal sunt acum pana la HUB_MAX_SENSORS = 5 placi si un singur hub.
 * Radioul este half-duplex si nu are captura garantata la puteri
 * apropiate: doua pachete care se suprapun in aer se pierd amandoua. Un
 * DATA_ENC de 17 octeti sta pe aer ~46 ms, iar un senzor emite o data la
 * ~30 s, deci ocuparea este de 0,15% per placa - o coliziune
 * INTAMPLATOARE este rara si nu deranjeaza pe nimeni, fiindca
 * urmatoarea masuratoare vine oricum peste jumatate de minut.
 *
 * Pericolul real nu este coliziunea intamplatoare, ci COLIZIUNEA
 * BLOCATA: doi senzori cu EXACT acelasi interval care s-au ciocnit o
 * data raman ciocniti la nesfarsit, fiindca amandoi se deplaseaza cu
 * acelasi pas. Cu un interval fix de 14 treziri pe toate placile, exact
 * asta s-ar fi intamplat, iar cele doua ar fi disparut in perechi din
 * jurnalul hub-ului fara nicio eroare vizibila.
 *
 * Se rup deci amandoua conditiile, cu doua masuri care costa impreuna
 * cateva zeci de cuvinte de program:
 *
 *  1) INTERVAL PROPRIU FIECARUI SENZOR. La baza se adauga (DevAddr - 1)
 *     treziri. DevAddr este numarul senzorului, alocat de hub din
 *     pozitia in tabelul de provisioning (1..5), deci fiecare placa are
 *     alt interval nominal: 23,2 / 25,3 / 27,4 / 29,6 / 31,7 s. Doi
 *     senzori ciocniti se despart de la sine dupa o singura perioada.
 *
 *  2) JITTER ALEATOR PE FIECARE CICLU. Peste asta se adauga 0..3 treziri
 *     dintr-un LFSR de 8 biti (Rand8), semanat din DevEUI si din frame
 *     counter-ul citit din HEF. Chiar daca doua placi ar nimeri acelasi
 *     numar de treziri intr-un ciclu, in ciclul urmator nu mai sunt la
 *     fel. Jitter-ul rezolva si pornirea simultana dupa o pana de
 *     curent, cand toate placile inrolate emit prima data in acelasi
 *     moment.
 *
 * Media pe cele 5 adrese iese ~30,6 s, adica exact ritmul dinainte: s-a
 * schimbat imprastierea, nu debitul.
 *
 * LFINTOSC are toleranta larga (ordinul a +-15%), care se adauga peste
 * jitter: intervalul real al senzorului cu adresa 5 urca pana la ~44 s.
 * Cine schimba oricare dintre cele trei constante de mai jos trebuie sa
 * schimbe si REMOVE_CONFIRM_SILENCE_MS din hub/SolvixHub_Tests/Config.h:
 * hub-ul confirma dezinrolarea prin tacere, iar un senzor care doarme
 * tace si el (F-031, F-034, regula 11 din CLAUDE.md).
 */
#define SLEEP_WAKEUPS_BASE      11U     /* 11 x ~2,11 s = ~23,2 s      */
#define SLEEP_SLOT_MASK         0x07U   /* (DevAddr-1) & 7 -> 0..7     */
#define SLEEP_JITTER_MASK       0x03U   /* Rand8() & 3    -> 0..3      */

/* Pasul buclelor de asteptare din veghe (asteptarea eliberarii unui
 * buton, fereastra de receptie). Cu cat e mai mic, cu atat butonul e
 * citit mai des; 10 ms este un compromis bun. */
#define TX_TICK_MS              10U

/* Debounce buton, in milisecunde (identic cu codul provizoriu). */
#define BUTTON_DEBOUNCE_MS      20U

/* Cat sta aprins un LED la o transmisie. */
#define LED_PULSE_MS            150U

/* Numarul de citiri ADC mediate pentru o masuratoare. Puterea lui 2, ca
 * impartirea sa se faca prin deplasare. */
#define ADC_SAMPLES             8U
#define ADC_SAMPLES_SHIFT       3U

/* Topologia divizorului. 1 = rezistor fix spre VDD si NTC spre GND
 * (cazul descris in cerinta: NTC intre RC2 si GND). */
#define NTC_PULLUP_TO_VDD       1

/* --- Parametrii pairing-ului --------------------------------------- */

/* Cat asteapta senzorul JOIN_ACCEPT dupa un JOIN_REQ, in milisecunde. */
#define JOIN_RX_TIMEOUT_MS      2000U

/* Pauza dintre doua incercari de join. Creste cu JOIN_BACKOFF_STEP_MS la
 * fiecare esec, pana la JOIN_BACKOFF_MAX_MS, ca doua placi pornite in
 * acelasi timp sa nu se calce reciproc la nesfarsit si ca un senzor
 * uitat langa hub sa nu ocupe canalul cu JOIN_REQ-uri. */
#define JOIN_BACKOFF_START_MS   3000U
#define JOIN_BACKOFF_STEP_MS    3000U
#define JOIN_BACKOFF_MAX_MS     30000U

/* Fereastra de receptie deschisa dupa fiecare DATA_ENC, pentru un
 * eventual CMD_DOWN (ACK sau RESET). Trebuie sa fie destul de lunga cat
 * hub-ul sa proceseze pachetul si sa raspunda, dar scurta, ca senzorul
 * sa nu piarda timp in RX. */
#define DOWNLINK_WINDOW_MS      600U

/* La cate transmisii se salveaza frame counter-ul in HEF. NU se salveaza
 * la fiecare pachet: HEF suporta ~100.000 de cicluri pe rand, iar la un
 * pachet la 5 secunde s-ar consuma in cateva luni.
 * Pretul: dupa o cadere de tensiune se pierd cel mult N-1 valori de
 * counter, si de aceea la pornire se sare inainte cu N (sectiunea 5). */
#define FCNT_CHECKPOINT_EVERY   50UL

/* 1 = payload-ul temperaturii este criptat cu XTEA-CTR (schema completa).
 * 0 = SOLUTIE DE REZERVA daca vreodata nu mai incape nici asa: pachetul
 *     de temperatura circula in clar in interiorul DATA_ENC, dar ramane
 *     autentificat cu MIC. Hub-ul are acelasi comutator
 *     (PAIRING_ENCRYPT_PAYLOAD in Config.h) si cele doua trebuie sa fie
 *     IDENTICE, altfel unul "decripteaza" un text deja clar (F-023).
 *     Cu XTEA firmware-ul incape cu payload-ul criptat, deci valoarea
 *     normala este 1. */
#define PAIRING_ENCRYPT_PAYLOAD 1

/* --- Pairing manual, din butonul 2 (RC5) --------------------------- */

/* Cat trebuie tinut apasat butonul 2 ca sa se deschida fereastra de
 * pairing. Se numara in pasi de PAIR_HOLD_TICK_MS cu un contor de un
 * octet: 30 x 100 ms = 3 s. Pasi de 10 ms ar fi cerut 300 de pasi, deci
 * un uint16_t si aritmetica pe 16 biti platita degeaba (F-028).
 * Acelasi pas este si perioada de clipire a lui LED2 cat timp fereastra
 * de pairing este deschisa: fiecare __delay_ms() cu o constanta noua
 * genereaza inca o bucla de intarziere inline, deci refolosirea aceleiasi
 * valori peste tot economiseste cuvinte de program. */
#define PAIR_HOLD_TICK_MS       100U
#define PAIR_HOLD_TICKS         30U

/* Cate incercari de join incape fereastra de pairing a senzorului.
 * Fereastra se masoara in INCERCARI, nu in milisecunde: cele 120 s ale
 * hub-ului nu intra intr-un uint16_t, iar un uint32_t nou in codul
 * fierbinte costa zeci-sute de cuvinte de program pe PIC16 (F-028).
 * Cu backoff-ul de mai jos (3 s, +3 s dupa fiecare esec, plafonat la
 * 30 s) cele 10 incercari insumeaza 3+6+...+30 = 165 s de asteptare,
 * deci acopera confortabil PAIRING_MODE_TIMEOUT_MS = 120 s din
 * hub/SolvixHub_Tests/Config.h: fereastra hub-ului se inchide prima,
 * ceea ce este ordinea dorita. */
#define PAIRING_MAX_ATTEMPTS    10U

/* 1 = cat timp senzorul NU este inrolat, temperatura se trimite si in
 *     clar, ca pachetul vechi de 6 octeti (TEMP_PLAIN). Util la bring-up,
 *     cu testul 7 al hub-ului. In exploatare se lasa pe 0: un senzor
 *     ne-inrolat nu are ce cauta pe frecventa cu date in clar. */
#define ENABLE_PLAIN_TEMP       0

/* --- Provisioning: SINGURUL LUCRU DE SCHIMBAT LA FIECARE PLACA ------ */

/*
 * NUMARUL SENZORULUI, 1..5. Este singura linie care se modifica intre
 * cele cinci placi: din el ies si DevEUI, si AppKey, prin blocul de mai
 * jos. Inainte se editau doua tabele de octeti la fiecare placa, iar o
 * singura cifra gresita intr-unul din ele dadea acelasi simptom ca o
 * cheie complet gresita: "MIC gresit", fara alt indiciu.
 *
 * Acelasi numar il primeste placa si ca DevAddr de la hub. Hub-ul NU mai
 * aloca prima adresa libera, ci POZITIA din tabelul de provisioning din
 * hub/SolvixHub_Tests/Config.h (DeviceRegistry::addressForEui). Senzorul
 * cu SENSOR_NODE_ID = 3 este deci intotdeauna "Senzor #3" in jurnalul
 * hub-ului, indiferent in ce ordine s-au inrolat placile si indiferent
 * de cate ori s-a golit registrul. Din DevAddr iese si slotul de somn de
 * mai sus, deci numarul chiar face doua treburi, nu este o eticheta.
 */
#define SENSOR_NODE_ID          3

#if (SENSOR_NODE_ID < 1) || (SENSOR_NODE_ID > 5)
#error "SENSOR_NODE_ID trebuie sa fie intre 1 si 5 (vezi HUB_MAX_SENSORS)."
#endif

/*
 * DevEUI, 8 octeti: "SOLVIX" in ASCII, apoi 0x00 si numarul placii.
 * PIC16LF1508 nu are un ID unic garantat, deci identitatea se
 * provizioneaza aici si se scrie in HEF la prima pornire.
 *
 * AppKey, 16 octeti: DIFERITA la fiecare placa. Nu circula niciodata
 * prin aer - serveste doar la semnarea JOIN_REQ, la cifrarea
 * JOIN_ACCEPT si la derivarea SessKey. Cheile de mai jos sunt cele de
 * DEZVOLTARE, aceleasi ca in PROVISIONED_DEVICES_INIT din Config.h;
 * inainte de punerea in exploatare se inlocuiesc, tot in pereche.
 *
 * Doar ramura placii curente se compileaza, deci cele patru chei
 * nefolosite nu costa niciun cuvant de program.
 */
#define PROVISION_DEV_EUI       { 0x53U, 0x4FU, 0x4CU, 0x56U, \
                                  0x49U, 0x58U, 0x00U,        \
                                  (uint8_t)SENSOR_NODE_ID }

#if   SENSOR_NODE_ID == 1
#define PROVISION_APP_KEY       { 0x00U, 0x11U, 0x22U, 0x33U, \
                                  0x44U, 0x55U, 0x66U, 0x77U, \
                                  0x88U, 0x99U, 0xAAU, 0xBBU, \
                                  0xCCU, 0xDDU, 0xEEU, 0xFFU }
#elif SENSOR_NODE_ID == 2
#define PROVISION_APP_KEY       { 0x2AU, 0x7FU, 0x13U, 0xC4U, \
                                  0x9EU, 0x06U, 0xB8U, 0x51U, \
                                  0x3DU, 0xE2U, 0x74U, 0xAFU, \
                                  0x60U, 0x1CU, 0x95U, 0xD8U }
#elif SENSOR_NODE_ID == 3
#define PROVISION_APP_KEY       { 0x5BU, 0x08U, 0xE1U, 0x96U, \
                                  0x34U, 0xCDU, 0x72U, 0xAFU, \
                                  0x1EU, 0x60U, 0xB5U, 0x27U, \
                                  0xD9U, 0x43U, 0x8CU, 0xF0U }
#elif SENSOR_NODE_ID == 4
#define PROVISION_APP_KEY       { 0x91U, 0x4CU, 0x26U, 0xD3U, \
                                  0x5FU, 0xA8U, 0x07U, 0xEBU, \
                                  0x62U, 0x1DU, 0xB4U, 0x78U, \
                                  0x3AU, 0xC5U, 0xE9U, 0x20U }
#else
#define PROVISION_APP_KEY       { 0xC7U, 0x3EU, 0x8AU, 0x15U, \
                                  0xD0U, 0x6BU, 0xF2U, 0x49U, \
                                  0xA3U, 0x5CU, 0x91U, 0x2EU, \
                                  0x87U, 0xF6U, 0x04U, 0xBDU }
#endif

/* =====================================================================
 * 2. MAPAREA PINILOR
 *    Extrasa din PIN_MANAGER_Initialize(). Pairing-ul NU adauga pini.
 *    Nicaieri mai jos nu apare un numar de pin "in clar".
 * ================================================================== */

/* --- LoRa RFM96 (SX1276) pe MSSP1 ---------------------------------- */
/* SCK  = RB6  (fix hardware pe PIC16F1508)                            */
/* MISO = RB4  (SDI la PIC, fix hardware)                              */
/* MOSI = RC7  (SDO la PIC, fix hardware)                              */
/* NSS  = RB5  (controlat manual mai jos)                              */
#define LORA_NSS_LAT            LATBbits.LATB5
#define LORA_NSS_TRIS           TRISBbits.TRISB5
#define LORA_NSS_ANSEL          ANSELBbits.ANSB5

/* --- Senzorul NTC --------------------------------------------------- */
/* RC2 = AN6. ANSELC bit2 este deja setat de PIN_MANAGER_Initialize. */
#define NTC_ADC_CHANNEL         6U
#define NTC_ANSEL               ANSELCbits.ANSC2
#define NTC_TRIS                TRISCbits.TRISC2

/* --- Butoane (active HIGH, pull-down extern) ----------------------- */
#define BUTTON_FORCE_PORT       PORTCbits.RC4   /* butonul 1 */
#define BUTTON_SPARE_PORT       PORTCbits.RC5   /* butonul 2, pairing    */

/* --- LED-uri -------------------------------------------------------- */
#define LED1_LAT                LATCbits.LATC3  /* transmisie de date    */
#define LED2_LAT                LATCbits.LATC6  /* pairing / eroare join */

/* =====================================================================
 * 3. REGISTRELE SX1276
 *    Blocul de emisie este neschimbat fata de firmware-ul de
 *    temperatura. Randurile marcate NOU sunt cele adaugate pentru
 *    receptie (JOIN_ACCEPT si CMD_DOWN).
 * ================================================================== */
#define LORA_REG_FIFO                 0x00U
#define LORA_REG_OP_MODE              0x01U
#define LORA_REG_FRF_MSB              0x06U
#define LORA_REG_FRF_MID              0x07U
#define LORA_REG_FRF_LSB              0x08U
#define LORA_REG_PA_CONFIG            0x09U
#define LORA_REG_FIFO_ADDR_PTR        0x0DU
#define LORA_REG_FIFO_TX_BASE_ADDR    0x0EU
#define LORA_REG_FIFO_RX_BASE_ADDR    0x0FU
#define LORA_REG_FIFO_RX_CURRENT_ADDR 0x10U   /* NOU: unde incepe pachetul primit */
#define LORA_REG_IRQ_FLAGS            0x12U
#define LORA_REG_RX_NB_BYTES          0x13U   /* NOU: cati octeti a primit */
#define LORA_REG_MODEM_CONFIG_1       0x1DU
#define LORA_REG_MODEM_CONFIG_2       0x1EU
#define LORA_REG_MODEM_CONFIG_3       0x26U
#define LORA_REG_PAYLOAD_LENGTH       0x22U
#define LORA_REG_MAX_PAYLOAD_LENGTH   0x23U   /* NOU: limita la receptie */
#define LORA_REG_VERSION              0x42U

#define LORA_LONG_RANGE_MODE          0x80U
#define LORA_MODE_SLEEP               0x00U
#define LORA_MODE_STDBY               0x01U
#define LORA_MODE_TX                  0x03U
#define LORA_MODE_RX_CONTINUOUS       0x05U   /* NOU */

#define LORA_IRQ_TX_DONE              0x08U
#define LORA_IRQ_RX_DONE              0x40U   /* NOU */
#define LORA_IRQ_PAYLOAD_CRC_ERROR    0x20U   /* NOU */

/* Cel mai lung pachet pe care il PRIMESTE senzorul este CMD_DOWN, 12
 * octeti (JOIN_ACCEPT are 10). 16 lasa loc si pentru cateva octete de
 * gunoi, ca un pachet strain sa fie citit si respins, nu taiat. */
#define LORA_RX_BUFFER_LEN            16U

/* =====================================================================
 * 4. PROTOCOLUL DE APLICATIE
 * ---------------------------------------------------------------------
 *  OGLINDIT in hub/SolvixHub_Tests/SensorPacket.h - cele doua fisiere se
 *  modifica IMPREUNA, in acelasi commit (regula 10 din CLAUDE.md).
 *
 *  Toate campurile multi-octet sunt big-endian. Primul octet ramane
 *  magic-ul 0xA5 din protocolul initial; ce s-a schimbat este ca octetul
 *  TYPE are acum mai multe valori.
 *
 *  TYPE 0x01 - TEMP_PLAIN, pachetul vechi de 6 octeti (NESCHIMBAT):
 *    [0] 0xA5  [1] 0x01  [2..3] temp*100  [4] motiv
 *    [5] checksum = XOR(0..4) ^ 0x5A
 *
 *  MIC = primii 4 octeti din CBC-MAC-XTEA (sectiunea 6). Cifrul este
 *  XTEA-128, cu bloc de 8 octeti - vezi F-024 pentru motiv.
 *
 *  TYPE 0x10 - JOIN_REQ (senzor -> hub), 16 octeti:
 *    [0] 0xA5  [1] 0x10  [2..9] DevEUI  [10..11] DevNonce
 *    [12..15] MIC = MAC(AppKey, [0..11])
 *
 *  TYPE 0x11 - JOIN_ACCEPT (hub -> senzor), 10 octeti:
 *    IV_join (8B) = 0x11 | DevNonce(2) | zero(5)
 *    [0] 0xA5  [1] 0x11
 *    [2..5] Enc = XTEA-CTR(AppKey, IV_join, DevAddr(1) | JoinNonce(3))
 *    [6..9] MIC = MAC(AppKey,
 *                     0x11 | DevEUI(8) | DevNonce(2) | DevAddr(1) | JoinNonce(3))
 *
 *  DevNonce este cel trimis chiar acum de senzor, deci un JOIN_ACCEPT
 *  rejucat dintr-o inrolare veche pica la verificarea MIC-ului.
 *
 *  SessKey (ambele capete, identic). MAC-ul da 8 octeti, cheia are 16,
 *  deci se cheama de doua ori, cu prefixe diferite:
 *    B = <prefix> | DevNonce(2) | JoinNonce(3) | DevAddr(1) | 0x00
 *    SessKey[0..7]  = MAC(AppKey, B cu prefix 0x01)
 *    SessKey[8..15] = MAC(AppKey, B cu prefix 0x02)
 *
 *  TYPE 0x12 - DATA_ENC (senzor -> hub), 17 octeti:
 *    [0] 0xA5  [1] 0x12  [2] DevAddr  [3..6] FrameCounter
 *    [7..12] EncPayload = XTEA-CTR(SessKey, IV, pachetul TEMP de 6 octeti)
 *            IV (8B) = DevAddr(1) | FrameCounter(4) | 0x00 (uplink) | zero(2)
 *    [13..16] MIC = MAC(SessKey, [0..12])
 *
 *  TYPE 0x13 - CMD_DOWN (hub -> senzor), 12 octeti:
 *    [0] 0xA5  [1] 0x13  [2] DevAddr  [3..6] FrameCounter downlink
 *    [7] CmdType (0x01 = ACK, 0x02 = RESET)
 *    [8..11] MIC = MAC(SessKey, [0..7])
 *
 *  De ce ramane checksum-ul in interiorul payload-ului criptat, cand
 *  exista deja MIC: pachetul de 6 octeti ramane BIT CU BIT cel vechi,
 *  deci hub-ul il poate da neschimbat lui SensorPacketCodec::decode().
 *  Nicio logica noua de parsare a temperaturii, pe niciunul din capete.
 * ================================================================== */
#define LORA_PACKET_MAGIC             0xA5U
#define CHECKSUM_SALT                 0x5AU

#define MSG_TYPE_TEMPERATURE          0x01U   /* TEMP_PLAIN */
#define MSG_TYPE_JOIN_REQ             0x10U
#define MSG_TYPE_JOIN_ACCEPT          0x11U
#define MSG_TYPE_DATA_ENC             0x12U
#define MSG_TYPE_CMD_DOWN             0x13U

#define LORA_PACKET_LEN               6U      /* TEMP_PLAIN, neschimbat */
#define JOIN_REQ_LEN                  16U
#define JOIN_ACCEPT_LEN               10U
#define DATA_ENC_LEN                  17U
#define CMD_DOWN_LEN                  12U

/* Cel mai lung pachet pe care il EMITE senzorul. */
#define TX_BUFFER_LEN                 DATA_ENC_LEN

/* Zonele acoperite de MIC, in octeti. Aceleasi numere apar in
 * hub/SolvixHub_Tests/SensorPacket.cpp si TestPairing.cpp. */
#define JOIN_REQ_MIC_INPUT_LEN        12U
#define JOIN_ACCEPT_MIC_INPUT_LEN     15U
#define DATA_ENC_MIC_INPUT_LEN        13U
#define CMD_DOWN_MIC_INPUT_LEN        8U

/* Campul cifrat din JOIN_ACCEPT: DevAddr(1) + JoinNonce(3). */
#define JOIN_ACCEPT_ENC_LEN           4U

#define MIC_LEN                       4U
#define CRYPTO_KEY_LEN                16U
#define DEV_EUI_LEN                   8U
#define DEV_NONCE_LEN                 2U
#define JOIN_NONCE_LEN                3U

#define CMD_TYPE_ACK                  0x01U
#define CMD_TYPE_RESET                0x02U

#define REASON_INTERVAL               0x00U
#define REASON_BUTTON                 0x01U

/* Valoarea trimisa cand citirea ADC iese din domeniul tabelului. */
#define TEMP_INVALID                  ((int16_t)-30000)

/* Vizibil in fereastra Watch din MPLAB; util cand radioul nu raspunde. */
static volatile uint8_t loraVersion = 0xFFU;

/* =====================================================================
 * 5. MEMORIA NE-VOLATILA (HEF - High-Endurance Flash)
 * ---------------------------------------------------------------------
 *  PIC16LF1508 NU are EEPROM. Singura memorie ne-volatila scriibila din
 *  program este HEF: ultimele 128 de cuvinte ale memoriei de program,
 *  garantate la ~100.000 de cicluri de stergere/scriere (restul
 *  flash-ului are doar ~10.000).
 *
 *  Pe acest device memoria de program are 4096 de cuvinte
 *  (0x0000-0x0FFF), deci HEF incepe la 0x0F80. Fiecare cuvant are 14
 *  biti; noi folosim doar cei 8 de jos, deci un cuvant = un octet util.
 *
 *  GRANULATIA: flash-ul se sterge pe RANDURI si se scrie tot pe randuri
 *  (grupul de latch-uri are aceeasi dimensiune ca blocul de stergere).
 *  Nu se poate rescrie un singur cuvant fara sa fie atins restul
 *  randului, de aceea fiecare "regiune" de mai jos ocupa un rand intreg.
 *
 *  MARIMEA RANDULUI: 32 de cuvinte (F-026). NU este o presupunere -
 *  este citita din fisierul de device support al lui XC8,
 *  PIC12-16F1xxx_DFP/.../dat/ini/16lf1508.ini, unde scrie
 *  FLASH_ERASE=20 si FLASH_WRITE=20 (hexazecimal, deci 32 de cuvinte).
 *  Prima versiune a acestui fisier presupunea 16 si imparte HEF-ul in 8
 *  regiuni; ar fi fost o eroare tacuta si distructiva - scrierea
 *  "randului" de la 0x0F90 ar fi sters de fapt tot blocul 0x0F80-0x0F9F,
 *  adica DevEUI-ul odata cu AppKey.
 *
 *  In 128 de cuvinte incap deci EXACT 4 randuri, si asta a impus harta:
 *
 *  HARTA (4 randuri x 32 de cuvinte):
 *    rand 0  0x0F80  identitate : MAGIC(1) + DevEUI(8) + AppKey(16)
 *    rand 1  0x0FA0  sesiune    : MAGIC(1) + DevAddr(1) + JoinNonce(3) +
 *                                 DevNonce(2) + SessKey(16)
 *    rand 2  0x0FC0  counter, slotul 0 : MAGIC(1) + FrameCounter(4)
 *    rand 3  0x0FE0  counter, slotul 1 : MAGIC(1) + FrameCounter(4)
 *
 *  Identitatea si sesiunea incap fiecare intr-un singur rand, ceea ce
 *  este chiar mai bine decat inainte: o inrolare inseamna acum o singura
 *  stergere/scriere, nu doua, iar DevEUI si AppKey nu mai pot fi
 *  desincronizate de o cadere de tensiune intre doua scrieri.
 *
 *  De ce un INEL pentru counter: se scrie prin rotatie in cele 2 randuri,
 *  deci uzura se imparte la 2. La citire se ia valoarea cea mai mare
 *  dintre sloturile valide - counter-ul creste strict, deci maximul este
 *  intotdeauna cel mai recent, indiferent unde a ramas rotatia.
 *  Cu FCNT_CHECKPOINT_EVERY = 50 si un pachet la 5 secunde rezulta ~345
 *  de scrieri pe zi impartite la 2 randuri: peste 500 de zile pe rand.
 *
 *  Tot acest calcul sta pe un fapt: senzorul este ALIMENTAT PERMANENT,
 *  deci RAM-ul se pastreaza intre transmisii si counter-ul poate trai
 *  acolo intre doua checkpoint-uri. Daca s-ar introduce vreodata un
 *  regim in care alimentarea se taie intre transmisii, fiecare trezire ar
 *  fi un cold boot cu RAM-ul pierdut: counter-ul ar trebui scris la
 *  FIECARE ciclu, iar inelul de mai sus ar trebui marit (sau HEF-ul
 *  inlocuit cu un FRAM extern).
 * ================================================================== */

#define HEF_ROW_WORDS           32U
#define HEF_BASE                0x0F80U

#define HEF_ROW_IDENTITY        (HEF_BASE + (0U * HEF_ROW_WORDS))
#define HEF_ROW_SESSION         (HEF_BASE + (1U * HEF_ROW_WORDS))
#define HEF_ROW_FCNT_FIRST      (HEF_BASE + (2U * HEF_ROW_WORDS))
#define HEF_FCNT_SLOTS          2U

/* Pozitiile campurilor in randul de identitate si in cel de sesiune. */
#define HEF_OFF_EUI             1U
#define HEF_OFF_APP_KEY         (HEF_OFF_EUI + DEV_EUI_LEN)          /*  9 */
#define HEF_OFF_DEV_ADDR        1U
#define HEF_OFF_JOIN_NONCE      2U
#define HEF_OFF_DEV_NONCE       (HEF_OFF_JOIN_NONCE + JOIN_NONCE_LEN) /*  5 */
#define HEF_OFF_SESS_KEY        (HEF_OFF_DEV_NONCE + DEV_NONCE_LEN)   /*  7 */

/* Marcaje care spun ca o regiune a fost scrisa. Flash-ul sters citeste
 * 0xFF, deci orice valoare diferita de 0xFF merge ca marcaj. */
#define HEF_MAGIC_PROV          0xA7U
#define HEF_MAGIC_SESSION       0xC3U
#define HEF_MAGIC_FCNT          0xC5U

/*
 * Bufferul in care se pregateste un rand inainte de scriere.
 *
 * NU are toate cele 32 de cuvinte ale randului, ci doar atatea cate
 * folosim efectiv: cel mai plin rand este cel de identitate, cu
 * MAGIC(1) + DevEUI(8) + AppKey(16) = 25 de octeti. Restul latch-urilor
 * randului primesc direct 0xFF in HEF_WriteRow, fara sa mai treaca prin
 * RAM. Pe un device cu 256 de octeti de RAM, cei 7 octeti economisiti
 * aici chiar conteaza (F-025).
 */
#define HEF_ROW_BUFFER_LEN      (1U + DEV_EUI_LEN + CRYPTO_KEY_LEN)   /* 25 */

static uint8_t hefRowBuffer[HEF_ROW_BUFFER_LEN];

/* Citeste un octet din memoria de program (bitii 7:0 ai cuvantului). */
static uint8_t HEF_ReadByte(uint16_t wordAddress)
{
    uint8_t value;

    PMADRL = (uint8_t)(wordAddress & 0x00FFU);
    PMADRH = (uint8_t)((wordAddress >> 8) & 0x00FFU);

    PMCON1bits.CFGS = 0;        /* memorie de program, nu configuration */
    PMCON1bits.RD   = 1;
    NOP();                      /* cele doua NOP-uri sunt obligatorii:  */
    NOP();                      /* citirea consuma doua cicluri         */

    value = PMDATL;

    return value;
}

/* Sterge un rand intreg. Adresa TREBUIE sa fie aliniata la rand. */
static void HEF_EraseRow(uint16_t rowAddress)
{
    uint8_t gieWasOn = (uint8_t)(INTCONbits.GIE);

    PMADRL = (uint8_t)(rowAddress & 0x00FFU);
    PMADRH = (uint8_t)((rowAddress >> 8) & 0x00FFU);

    PMCON1bits.CFGS = 0;
    PMCON1bits.FREE = 1;        /* operatia este o STERGERE */
    PMCON1bits.WREN = 1;

    /* Secventa de deblocare nu are voie sa fie intrerupta. */
    INTCONbits.GIE = 0;
    PMCON2 = 0x55U;
    PMCON2 = 0xAAU;
    PMCON1bits.WR = 1;
    NOP();                      /* procesorul sta oprit aici ~2 ms */
    NOP();
    if (gieWasOn != 0U)
    {
        INTCONbits.GIE = 1;
    }

    PMCON1bits.WREN = 0;
    PMCON1bits.FREE = 0;
}

/*
 * Scrie continutul lui hefRowBuffer intr-un rand, dupa ce il sterge.
 * Se incarca TOATE cele HEF_ROW_WORDS latch-uri ale randului - cele de
 * dincolo de hefRowBuffer primesc 0xFF, adica exact ce ar citi flash-ul
 * sters. Scrierea propriu-zisa se declanseaza o singura data, la ultimul
 * latch (LWLO trecut pe 0).
 * Cei 6 biti superiori ai fiecarui cuvant raman 1 (0x3F): sunt
 * nefolositi si asa arata oricum flash-ul sters.
 */
static void HEF_WriteRow(uint16_t rowAddress)
{
    uint8_t i;
    uint8_t gieWasOn;

    HEF_EraseRow(rowAddress);

    gieWasOn = (uint8_t)(INTCONbits.GIE);

    PMADRL = (uint8_t)(rowAddress & 0x00FFU);
    PMADRH = (uint8_t)((rowAddress >> 8) & 0x00FFU);

    PMCON1bits.CFGS = 0;
    PMCON1bits.FREE = 0;
    PMCON1bits.WREN = 1;
    PMCON1bits.LWLO = 1;        /* deocamdata doar incarcam latch-uri */

    for (i = 0U; i < HEF_ROW_WORDS; i++)
    {
        PMDATL = (i < HEF_ROW_BUFFER_LEN) ? hefRowBuffer[i] : 0xFFU;
        PMDATH = 0x3FU;

        if (i == (HEF_ROW_WORDS - 1U))
        {
            /* Ultimul latch: acum scrierea chiar se executa. */
            PMCON1bits.LWLO = 0;
        }

        INTCONbits.GIE = 0;
        PMCON2 = 0x55U;
        PMCON2 = 0xAAU;
        PMCON1bits.WR = 1;
        NOP();
        NOP();
        if (gieWasOn != 0U)
        {
            INTCONbits.GIE = 1;
        }

        /* Adresa avanseaza spre latch-ul urmator. Randul este aliniat,
         * deci incrementarea nu poate iesi din el. */
        PMADRL++;
    }

    PMCON1bits.WREN = 0;
    PMCON1bits.LWLO = 0;
}

/* Umple hefRowBuffer cu 0xFF (valoarea flash-ului sters). */
static void HEF_ClearRowBuffer(void)
{
    uint8_t i;

    for (i = 0U; i < HEF_ROW_BUFFER_LEN; i++)
    {
        hefRowBuffer[i] = 0xFFU;
    }
}

/* =====================================================================
 * 6. XTEA-128: CIFRUL DE BLOC, CBC-MAC (MIC) SI CTR
 * ---------------------------------------------------------------------
 *  DE CE XTEA SI NU AES (F-024)
 *  ------------------------------
 *  Prima versiune a acestui fisier folosea AES-128 cu AES-CMAC si
 *  AES-CTR. Masurat cu XC8 pe PIC16LF1508, firmware-ul complet cerea
 *  5250 de cuvinte de program si 286 de octeti de RAM. Device-ul are
 *  4096 de cuvinte si 256 de octeti. Nici macar cu toate cele trei
 *  solutii de rezerva aplicate simultan (fara criptarea payload-ului,
 *  fara descifrare, program de chei calculat din mers) nu se cobora sub
 *  4325 de cuvinte. AES pur si simplu NU incape aici.
 *
 *  Doar tabelele de substitutie ale AES ocupa 512 de cuvinte de program:
 *  un sfert din tot flash-ul disponibil, inainte de orice linie de cod.
 *
 *  XTEA rezolva exact aceasta problema:
 *    - NU are niciun tabel: totul este adunare, XOR si deplasari pe 32
 *      de biti, deci nu consuma flash pe date constante;
 *    - are nevoie de o singura directie (cifrare), fiindca MIC-ul si
 *      criptarea sunt construite amandoua peste ea (CBC-MAC si CTR);
 *    - bloc de 64 de biti, cheie de 128 de biti - aceeasi dimensiune de
 *      cheie ca inainte, deci AppKey si SessKey raman de 16 octeti si
 *      provisioning-ul nu se schimba.
 *
 *  CE SE PIERDE fata de AES: XTEA are blocul de 64 de biti, nu 128, si
 *  nu are statutul de standard al AES. Pentru traficul acestui proiect -
 *  cateva zeci de mii de pachete de 6 octeti pe an, fiecare cu contor
 *  strict crescator - marginea este confortabila. Nu folosi acest cod
 *  pentru volume mari de date sub aceeasi cheie.
 *
 *  CE NU SE PIERDE: inrolarea, cheia de sesiune derivata, MIC-ul pe
 *  fiecare pachet, anti-replay-ul si confidentialitatea payload-ului
 *  raman toate exact ca in proiectarea initiala.
 *
 *  Numarul de runde este cel standard, 32 (adica 64 de "jumatati de
 *  runda"), cu DELTA = 0x9E3779B9.
 *
 *  OGLINDIT pe hub in hub/SolvixHub_Tests/HubCrypto.cpp, care face
 *  aceleasi trei operatii, octet cu octet.
 * ================================================================== */

#define XTEA_ROUNDS             32U
#define XTEA_BLOCK_LEN          8U
#define XTEA_DELTA              0x9E3779B9UL

/*
 * Cuvant de 32 de biti accesibil si pe octeti (F-028).
 *
 * PIC16 nu are decat un acumulator de 8 biti: fiecare deplasare a unui
 * uint32 cu un numar de pozitii devine o bucla din biblioteca XC8 si
 * costa zeci de cuvinte de program. Impachetarea si despachetarea
 * big-endian scrise "cu shift-uri" ne costau singure peste 300 de
 * cuvinte, pe un device care are 4096 in total.
 *
 * XC8 stocheaza intregii little-endian, deci octetul cel mai
 * semnificativ este byte[3]. Toate conversiile de mai jos sunt simple
 * mutari de octeti, fara nicio deplasare.
 */
typedef union
{
    uint32_t word;
    uint8_t  byte[4];       /* byte[0] = cel mai putin semnificativ */
} Word32;

/* Cheia activa, despachetata in cuvinte de 32 de biti (big-endian).
 * Se reincarca la fiecare comutare AppKey <-> SessKey. */
static Word32 xteaKey[4];

/* Incarca o cheie de 16 octeti. Octetii se citesc big-endian, ca peste
 * tot in protocol. */
static void Xtea_LoadKey(const uint8_t *key)
{
    uint8_t i;

    for (i = 0U; i < 4U; i++)
    {
        xteaKey[i].byte[3] = key[(i * 4U) + 0U];
        xteaKey[i].byte[2] = key[(i * 4U) + 1U];
        xteaKey[i].byte[1] = key[(i * 4U) + 2U];
        xteaKey[i].byte[0] = key[(i * 4U) + 3U];
    }
}

/*
 * Cifreaza un bloc de 8 octeti, pe loc, cu cheia deja incarcata.
 * Este SINGURA primitiva criptografica din firmware: MIC-ul (CBC-MAC) si
 * criptarea (CTR) se construiesc amandoua peste ea, deci nu exista cod
 * de descifrare nicaieri.
 */
static void Xtea_EncryptBlock(uint8_t *block)
{
    Word32  v0;
    Word32  v1;
    Word32  sum;
    uint8_t round;

    v0.byte[3] = block[0];
    v0.byte[2] = block[1];
    v0.byte[1] = block[2];
    v0.byte[0] = block[3];
    v1.byte[3] = block[4];
    v1.byte[2] = block[5];
    v1.byte[1] = block[6];
    v1.byte[0] = block[7];
    sum.word   = 0UL;

    for (round = 0U; round < XTEA_ROUNDS; round++)
    {
        /* sum & 3 = cei doi biti de jos, deci doar octetul de jos. */
        v0.word += ((((v1.word << 4) ^ (v1.word >> 5)) + v1.word) ^
                    (sum.word + xteaKey[sum.byte[0] & 3U].word));

        sum.word += XTEA_DELTA;

        /* (sum >> 11) & 3 = bitii 11 si 12, adica bitii 3 si 4 din
         * octetul 1 - tot fara nicio deplasare pe 32 de biti. */
        v1.word += ((((v0.word << 4) ^ (v0.word >> 5)) + v0.word) ^
                    (sum.word +
                     xteaKey[(uint8_t)(sum.byte[1] >> 3) & 3U].word));
    }

    block[0] = v0.byte[3];
    block[1] = v0.byte[2];
    block[2] = v0.byte[1];
    block[3] = v0.byte[0];
    block[4] = v1.byte[3];
    block[5] = v1.byte[2];
    block[6] = v1.byte[1];
    block[7] = v1.byte[0];
}

/*
 * CBC-MAC peste "length" octeti, cu cheia deja incarcata. Ultimul bloc
 * se completeaza cu zerouri. Rezultatul are 8 octeti; MIC-ul
 * protocolului este format din primii MIC_LEN dintre ei.
 *
 * DE CE ESTE SIGUR UN CBC-MAC SIMPLU AICI, fara subchei ca la CMAC:
 * CBC-MAC este nesigur doar pentru mesaje de lungime VARIABILA, unde un
 * atacator poate combina doua mesaje valide. In protocolul nostru:
 *   - fiecare tip de mesaj are lungime FIXA (JOIN_REQ 12, DATA_ENC 13,
 *     CMD_DOWN 8, JOIN_ACCEPT 15 octeti acoperiti);
 *   - octetul TYPE, care distinge tipurile, se afla in PRIMUL bloc al
 *     zonei acoperite, deci doua tipuri diferite nu pot avea acelasi
 *     prefix;
 *   - JOIN_* folosesc AppKey, iar DATA/CMD folosesc SessKey, deci cele
 *     doua familii nici macar nu impart cheia.
 * Daca vreodata se adauga un mesaj de lungime variabila, aceasta
 * constructie TREBUIE inlocuita cu una cu prefix de lungime.
 */
static void Xtea_MacWithLoadedKey(const uint8_t *message, uint8_t length,
                                  uint8_t *mac)
{
    uint8_t i;
    uint8_t offset;

    for (i = 0U; i < XTEA_BLOCK_LEN; i++)
    {
        mac[i] = 0U;
    }

    for (offset = 0U; offset < length;
         offset = (uint8_t)(offset + XTEA_BLOCK_LEN))
    {
        for (i = 0U; i < XTEA_BLOCK_LEN; i++)
        {
            if ((uint8_t)(offset + i) < length)
            {
                mac[i] ^= message[offset + i];
            }
        }
        Xtea_EncryptBlock(mac);
    }
}

/*
 * XTEA-CTR peste "length" octeti. Blocul contor este dat de apelant;
 * aceeasi functie cripteaza si decripteaza.
 * Payload-ul nostru are 6 octeti si campul cifrat din JOIN_ACCEPT are 4,
 * deci se consuma un singur bloc de flux - dar bucla trateaza corect si
 * cazul general.
 */
static void Xtea_CtrWithLoadedKey(const uint8_t *iv, uint8_t *data,
                                  uint8_t length)
{
    uint8_t counter[XTEA_BLOCK_LEN];
    uint8_t stream[XTEA_BLOCK_LEN];
    uint8_t i;
    uint8_t done = 0U;
    uint8_t chunk;

    for (i = 0U; i < XTEA_BLOCK_LEN; i++)
    {
        counter[i] = iv[i];
    }

    while (done < length)
    {
        for (i = 0U; i < XTEA_BLOCK_LEN; i++)
        {
            stream[i] = counter[i];
        }
        Xtea_EncryptBlock(stream);

        chunk = (uint8_t)(length - done);
        if (chunk > XTEA_BLOCK_LEN)
        {
            chunk = XTEA_BLOCK_LEN;
        }

        for (i = 0U; i < chunk; i++)
        {
            data[done + i] ^= stream[i];
        }

        done = (uint8_t)(done + chunk);

        /* Incrementare pe ultimul octet: pachetele noastre nu depasesc
         * niciodata un bloc, deci nu e nevoie de propagarea carry-ului. */
        counter[XTEA_BLOCK_LEN - 1U]++;
    }
}

/* =====================================================================
 * 7. STAREA DEVICE-ULUI (identitate, sesiune, contoare)
 * ================================================================== */

/*
 * DEV_STATE_IDLE este starea implicita: un senzor fara sesiune NU se mai
 * inroleaza singur, ci tace pana cand utilizatorul tine butonul 2 apasat
 * trei secunde. Motivul este simetria cu hub-ul, care accepta JOIN_REQ
 * doar in fereastra deschisa manual cu 'pair': un senzor care emitea la
 * nesfarsit ocupa canalul fara sa aiba cine sa-i raspunda.
 * DEV_STATE_JOINING inseamna de acum "fereastra de pairing este
 * deschisa", nu "incerc la nesfarsit".
 */
#define DEV_STATE_JOINING       0U
#define DEV_STATE_OPERATING     1U
#define DEV_STATE_IDLE          2U

static uint8_t  devEui[DEV_EUI_LEN];
static uint8_t  sessKey[CRYPTO_KEY_LEN];
static uint8_t  devAddr;
static uint8_t  joinNonce[JOIN_NONCE_LEN];
static uint8_t  devNonce[DEV_NONCE_LEN];

/*
 * AppKey NU se tine in RAM (F-029). Sta permanent in HEF si se citeste
 * de acolo direct in cifru, in Key_UseApp(). Cei 16 octeti economisiti
 * sunt ce face diferenta intre "intra" si "nu intra" pe configuratia de
 * DEBUG, unde depanatorul isi rezerva el insusi 16 octeti de RAM.
 * Costul este de 16 citiri din memoria de program per comutare de cheie,
 * si numai pe calea de inrolare - calea de date foloseste SessKey.
 */

static uint8_t  deviceState = DEV_STATE_IDLE;
static uint32_t frameCounter = 0UL;

/* Cate transmisii s-au facut de la ultimul checkpoint in HEF. Numara
 * doar pana la FCNT_CHECKPOINT_EVERY (50), deci un octet ajunge. */
static uint8_t  fcntSinceCheckpoint = 0U;

/* Slotul din inelul de counter in care s-a scris ultima data. */
static uint8_t  fcntSlot = 0U;

/* Buffere de lucru. Sunt globale, nu locale, ca sa nu se adune pe stiva
 * compilata a XC8: cei 256 de octeti de RAM ai lui PIC16LF1508 nu iarta
 * nicio risipa (F-025). */
static uint8_t  txBuffer[TX_BUFFER_LEN];
static uint8_t  rxBuffer[LORA_RX_BUFFER_LEN];
static uint8_t  cryptoBlock[XTEA_BLOCK_LEN];
static uint8_t  macBuffer[XTEA_BLOCK_LEN];
static uint8_t  micInput[JOIN_ACCEPT_MIC_INPUT_LEN];

/* Ce cheie este incarcata acum in xteaKey, ca sa nu o reincarcam degeaba
 * (despachetarea celor 16 octeti in 4 cuvinte costa). */
#define KEY_NONE                0U
#define KEY_APP                 1U
#define KEY_SESSION             2U
static uint8_t loadedKeyId = KEY_NONE;

/*
 * Trece cifrul pe AppKey, citind-o DIRECT DIN HEF (F-029). Nu exista o
 * copie in RAM: cheia nu se schimba niciodata dupa provisioning, iar
 * cele 16 citiri din memoria de program se fac doar cand se comuta
 * cheia, adica pe calea de inrolare.
 */
static void Key_UseApp(void)
{
    uint8_t i;
    uint16_t base;

    if (loadedKeyId == KEY_APP)
    {
        return;
    }

    base = (uint16_t)(HEF_ROW_IDENTITY + HEF_OFF_APP_KEY);

    for (i = 0U; i < 4U; i++)
    {
        xteaKey[i].byte[3] = HEF_ReadByte((uint16_t)(base + (i * 4U) + 0U));
        xteaKey[i].byte[2] = HEF_ReadByte((uint16_t)(base + (i * 4U) + 1U));
        xteaKey[i].byte[1] = HEF_ReadByte((uint16_t)(base + (i * 4U) + 2U));
        xteaKey[i].byte[0] = HEF_ReadByte((uint16_t)(base + (i * 4U) + 3U));
    }

    loadedKeyId = KEY_APP;
}

/* Trece cifrul pe SessKey. */
static void Key_UseSession(void)
{
    if (loadedKeyId != KEY_SESSION)
    {
        Xtea_LoadKey(sessKey);
        loadedKeyId = KEY_SESSION;
    }
}

/* Compara MIC-ul primit cu cel calculat, pe MIC_LEN octeti. */
static uint8_t Mic_Matches(const uint8_t *received, const uint8_t *computed)
{
    uint8_t i;
    uint8_t diff = 0U;

    for (i = 0U; i < MIC_LEN; i++)
    {
        diff |= (uint8_t)(received[i] ^ computed[i]);
    }

    return (uint8_t)((diff == 0U) ? 1U : 0U);
}

/* =====================================================================
 * 8. CITIREA SI SCRIEREA STARII IN HEF
 * ================================================================== */

/*
 * Identitatea: DevEUI + AppKey, amandoua in acelasi rand. Daca randul
 * este gol (marcaj lipsa), se scriu valorile de compilare din
 * PROVISION_*. Asa o placa noua se auto-provizioneaza la prima pornire.
 *
 * DACA RANDUL EXISTA DAR CONTINE ALT DevEUI, se rescrie tot. Cazul apare
 * exact cand o placa deja folosita este reprogramata cu alt
 * SENSOR_NODE_ID - de exemplu fiindca senzorul #2 s-a ars si i se ia
 * locul cu o placa de rezerva. Fara verificarea asta, HEF-ul ar pastra
 * identitatea VECHE si placa ar continua sa se prezinte cu numarul
 * vechi, in timp ce firmware-ul de pe ea spune altceva: pe hub s-ar
 * vedea "MIC gresit" (cheia compilata nu se mai potriveste cu DevEUI-ul
 * din HEF) si nicio cautare in cod nu ar duce nicaieri, fiindca sursa
 * este corecta. Este acelasi gen de capcana ca F-033, dar cu starea
 * ne-volatila in loc de directorul de build.
 *
 * Odata cu identitatea se sterge si SESIUNEA: o cheie de sesiune este
 * legata de identitatea cu care a fost negociata, deci nu mai are ce
 * cauta acolo. Placa porneste in DEV_STATE_IDLE si asteapta o inrolare
 * noua, ceea ce si trebuie.
 *
 * Verificarea nu costa o scriere in plus la fiecare pornire: se compara
 * doar, si se scrie exclusiv cand chiar difera.
 *
 * DevEUI ajunge in RAM, fiindca intra in fiecare JOIN_REQ. AppKey NU:
 * ramane doar in HEF si se citeste de acolo direct in cifru, in
 * Key_UseApp() (F-029).
 */
static void Nvm_LoadOrCreateProvisioning(void)
{
    static const uint8_t defaultEui[DEV_EUI_LEN] = PROVISION_DEV_EUI;
    static const uint8_t defaultKey[CRYPTO_KEY_LEN] = PROVISION_APP_KEY;

    uint8_t i;
    uint8_t same;

    if (HEF_ReadByte(HEF_ROW_IDENTITY) == HEF_MAGIC_PROV)
    {
        same = 1U;

        for (i = 0U; i < DEV_EUI_LEN; i++)
        {
            devEui[i] = HEF_ReadByte((uint16_t)(HEF_ROW_IDENTITY +
                                                HEF_OFF_EUI + i));
            if (devEui[i] != defaultEui[i])
            {
                same = 0U;
            }
        }

        if (same != 0U)
        {
            return;
        }

        /* Placa a fost reprogramata cu alt SENSOR_NODE_ID: sesiunea
         * veche apartine identitatii vechi. */
        HEF_EraseRow(HEF_ROW_SESSION);
    }

    /* Rand gol, sau identitate schimbata: il umplem cu valorile de
     * compilare. */
    for (i = 0U; i < DEV_EUI_LEN; i++)
    {
        devEui[i] = defaultEui[i];
    }

    HEF_ClearRowBuffer();
    hefRowBuffer[0] = HEF_MAGIC_PROV;
    for (i = 0U; i < DEV_EUI_LEN; i++)
    {
        hefRowBuffer[HEF_OFF_EUI + i] = devEui[i];
    }
    for (i = 0U; i < CRYPTO_KEY_LEN; i++)
    {
        hefRowBuffer[HEF_OFF_APP_KEY + i] = defaultKey[i];
    }
    HEF_WriteRow(HEF_ROW_IDENTITY);
}

/* Intoarce 1 daca in HEF exista o sesiune valida (deci senzorul este
 * deja inrolat) si o incarca in RAM. */
static uint8_t Nvm_LoadSession(void)
{
    uint8_t i;

    if (HEF_ReadByte(HEF_ROW_SESSION) != HEF_MAGIC_SESSION)
    {
        return 0U;
    }

    devAddr = HEF_ReadByte((uint16_t)(HEF_ROW_SESSION + HEF_OFF_DEV_ADDR));

    for (i = 0U; i < JOIN_NONCE_LEN; i++)
    {
        joinNonce[i] = HEF_ReadByte((uint16_t)(HEF_ROW_SESSION +
                                               HEF_OFF_JOIN_NONCE + i));
    }
    for (i = 0U; i < DEV_NONCE_LEN; i++)
    {
        devNonce[i] = HEF_ReadByte((uint16_t)(HEF_ROW_SESSION +
                                              HEF_OFF_DEV_NONCE + i));
    }
    for (i = 0U; i < CRYPTO_KEY_LEN; i++)
    {
        sessKey[i] = HEF_ReadByte((uint16_t)(HEF_ROW_SESSION +
                                             HEF_OFF_SESS_KEY + i));
    }

    /* Un DevAddr de 0x00 sau 0xFF inseamna rand corupt sau nescris. */
    if ((devAddr == 0x00U) || (devAddr == 0xFFU))
    {
        return 0U;
    }

    return 1U;
}

/* Toata sesiunea incape intr-un singur rand, deci se scrie dintr-o
 * singura stergere+scriere: o cadere de tensiune nu mai poate lasa
 * DevAddr salvat fara SessKey. */
static void Nvm_SaveSession(void)
{
    uint8_t i;

    HEF_ClearRowBuffer();
    hefRowBuffer[0] = HEF_MAGIC_SESSION;
    hefRowBuffer[HEF_OFF_DEV_ADDR] = devAddr;
    for (i = 0U; i < JOIN_NONCE_LEN; i++)
    {
        hefRowBuffer[HEF_OFF_JOIN_NONCE + i] = joinNonce[i];
    }
    for (i = 0U; i < DEV_NONCE_LEN; i++)
    {
        hefRowBuffer[HEF_OFF_DEV_NONCE + i] = devNonce[i];
    }
    for (i = 0U; i < CRYPTO_KEY_LEN; i++)
    {
        hefRowBuffer[HEF_OFF_SESS_KEY + i] = sessKey[i];
    }
    HEF_WriteRow(HEF_ROW_SESSION);
}

/* Sterge sesiunea: senzorul redevine ne-inrolat (comanda RESET). */
static void Nvm_EraseSession(void)
{
    uint8_t i;

    HEF_EraseRow(HEF_ROW_SESSION);

    devAddr = 0U;
    for (i = 0U; i < CRYPTO_KEY_LEN; i++)
    {
        sessKey[i] = 0U;
    }

    /* Cheia incarcata in cifru nu mai are voie sa fie considerata valida. */
    loadedKeyId = KEY_NONE;
}

/*
 * Citeste inelul de frame counter si intoarce cea mai mare valoare
 * valida. Counter-ul creste strict, deci maximul este si cel mai recent.
 * Lasa in fcntSlot slotul in care a fost gasit, ca urmatoarea scriere sa
 * mearga in slotul de dupa el.
 */
static uint32_t Nvm_LoadFrameCounter(void)
{
    uint32_t best = 0UL;
    Word32   value;
    uint16_t rowAddress;
    uint8_t  slot;
    uint8_t  found = 0U;

    for (slot = 0U; slot < HEF_FCNT_SLOTS; slot++)
    {
        rowAddress = (uint16_t)(HEF_ROW_FCNT_FIRST + ((uint16_t)slot * HEF_ROW_WORDS));

        if (HEF_ReadByte(rowAddress) != HEF_MAGIC_FCNT)
        {
            continue;
        }

        /* In HEF counter-ul este scris big-endian, ca pe fir. XC8 tine
         * intregii little-endian, deci octetul cel mai semnificativ este
         * byte[3]: despachetarea devine mutare de octeti, fara nicio
         * deplasare pe 32 de biti (F-028). */
        value.byte[3] = HEF_ReadByte((uint16_t)(rowAddress + 1U));
        value.byte[2] = HEF_ReadByte((uint16_t)(rowAddress + 2U));
        value.byte[1] = HEF_ReadByte((uint16_t)(rowAddress + 3U));
        value.byte[0] = HEF_ReadByte((uint16_t)(rowAddress + 4U));

        if ((found == 0U) || (value.word > best))
        {
            best = value.word;
            fcntSlot = slot;
            found = 1U;
        }
    }

    return best;
}

static void Nvm_SaveFrameCounter(uint32_t value)
{
    Word32   packed;
    uint16_t rowAddress;

    fcntSlot = (uint8_t)((fcntSlot + 1U) % HEF_FCNT_SLOTS);
    rowAddress = (uint16_t)(HEF_ROW_FCNT_FIRST + ((uint16_t)fcntSlot * HEF_ROW_WORDS));

    packed.word = value;

    HEF_ClearRowBuffer();
    hefRowBuffer[0] = HEF_MAGIC_FCNT;
    hefRowBuffer[1] = packed.byte[3];   /* big-endian pe fir si in HEF */
    hefRowBuffer[2] = packed.byte[2];
    hefRowBuffer[3] = packed.byte[1];
    hefRowBuffer[4] = packed.byte[0];

    HEF_WriteRow(rowAddress);
}

/* =====================================================================
 * 9. DRIVER LoRa
 *    Emisia este identica cu cea din firmware-ul de temperatura.
 *    Receptia (LoRa_Receive) este noua si e ceruta de pairing.
 * ================================================================== */

static void LoRa_Select(void)
{
    LORA_NSS_LAT = 0;
}

static void LoRa_Deselect(void)
{
    LORA_NSS_LAT = 1;
}

static void LoRa_WriteRegister(uint8_t address, uint8_t value)
{
    LoRa_Select();
    (void)SPI1_ByteExchange(address | 0x80U);       /* bit7 = 1 -> scriere */
    (void)SPI1_ByteExchange(value);
    LoRa_Deselect();
}

static uint8_t LoRa_ReadRegister(uint8_t address)
{
    uint8_t value;

    LoRa_Select();
    (void)SPI1_ByteExchange(address & 0x7FU);       /* bit7 = 0 -> citire */
    value = SPI1_ByteExchange(0x00U);
    LoRa_Deselect();

    return value;
}

/*
 * Trece radioul in SLEEP. SX1276 isi pastreaza registrele in acest mod -
 * se pierde doar FIFO-ul, care oricum se rescrie la fiecare pachet -
 * deci la trezire NU se reia LoRa_Initialize(). LoRa_SendBuffer() trece
 * oricum radioul prin STDBY inainte de a scrie FIFO.
 */
static void LoRa_Sleep(void)
{
    LoRa_WriteRegister(LORA_REG_OP_MODE,
                       LORA_LONG_RANGE_MODE | LORA_MODE_SLEEP);
}

/*
 * Initializare radio. Parametrii de aici trebuie sa fie IDENTICI cu cei
 * din sketch-ul hub-ului, altfel pachetele nu se vad deloc:
 *   868.0 MHz, BW 125 kHz, CR 4/5, SF7, CRC activ, header explicit,
 *   sync word 0x12 (valoarea de reset - nu se scrie explicit).
 * Pairing-ul NU schimba niciunul dintre ei.
 * Intoarce 1 daca radioul a raspuns cu RegVersion == 0x12.
 */
static uint8_t LoRa_Initialize(void)
{
    /* Lasam cristalul RF96 si POR-ul sa se aseze. */
    __delay_ms(10);

    /*
     * MSSP-ul se deschide scriind direct registrele, nu prin
     * Lora_SPI.Open(0). Open() indexeaza tabelul spi1_configuration[] din
     * driverul MCC si costa 106 cuvinte de program pentru o singura
     * configuratie folosita - pe un device cu 4096 in total, nu merita.
     *
     * Valorile sunt exact spi1_configuration[0] din
     * mcc_generated_files/spi/src/mssp.c: stat 0x00, con1 0x0A,
     * con3 0x10, baud 0x1F (adica 125 kHz la FOSC = 16 MHz). Daca cineva
     * schimba configuratia din MCC, trebuie schimbate si aici - de aceea
     * sunt scrise cu tot cu numele campurilor.
     *
     * Fisierele generate de MCC raman NEATINSE, ca o regenerare sa nu
     * strice nimic; toata abaterea sta in main.c.
     */
    SSP1STAT = 0x00U;                   /* stat */
    SSP1CON1 = 0x0AU;                   /* con1: SPI master, FOSC/(4*(SSP1ADD+1)) */
    SSP1CON3 = 0x10U;                   /* con3 */
    SSP1ADD  = 0x1FU;                   /* baud -> 125 kHz */
    SSP1CON1bits.SSPEN = 1U;

    /* F-009: SX1276/RFM96 cere SPI mode 0 - ceas idle LOW, esantionare
     * pe frontul crescator. MSSP-ul nu porneste asa implicit. */
    SSP1CON1bits.CKP = 0;
    SSP1STATbits.CKE = 1;

    /* RFM96W foloseste un SX1276: RegVersion trebuie sa fie 0x12. */
    loraVersion = LoRa_ReadRegister(LORA_REG_VERSION);
    if (loraVersion != 0x12U)
    {
        return 0U;
    }

    /* SLEEP este singurul mod in care se poate comuta pe LoRa long range. */
    LoRa_WriteRegister(LORA_REG_OP_MODE, LORA_LONG_RANGE_MODE | LORA_MODE_SLEEP);

    LoRa_WriteRegister(LORA_REG_FRF_MSB, 0xD9U);        /* 868.000 MHz */
    LoRa_WriteRegister(LORA_REG_FRF_MID, 0x00U);
    LoRa_WriteRegister(LORA_REG_FRF_LSB, 0x00U);

    LoRa_WriteRegister(LORA_REG_PA_CONFIG, 0x8FU);      /* PA_BOOST, ~14 dBm */
    LoRa_WriteRegister(LORA_REG_MODEM_CONFIG_1, 0x72U); /* BW 125 kHz, CR 4/5, header explicit */
    LoRa_WriteRegister(LORA_REG_MODEM_CONFIG_2, 0x74U); /* SF7, CRC payload activ */
    LoRa_WriteRegister(LORA_REG_MODEM_CONFIG_3, 0x04U); /* AGC automat */

    LoRa_WriteRegister(LORA_REG_FIFO_TX_BASE_ADDR, 0x00U);
    LoRa_WriteRegister(LORA_REG_FIFO_RX_BASE_ADDR, 0x00U);

    /* NOU: la receptie cu header explicit, un pachet mai lung decat
     * aceasta limita este abandonat de modem. Il punem putin peste cel
     * mai lung pachet al protocolului. */
    LoRa_WriteRegister(LORA_REG_MAX_PAYLOAD_LENGTH, LORA_RX_BUFFER_LEN);

    LoRa_WriteRegister(LORA_REG_IRQ_FLAGS, 0xFFU);      /* stinge flagurile ramase */

    LoRa_WriteRegister(LORA_REG_OP_MODE, LORA_LONG_RANGE_MODE | LORA_MODE_STDBY);

    return 1U;
}

/*
 * Trimite un buffer de "length" octeti. Aceeasi secventa ca in codul de
 * temperatura; TxDone se asteapta prin polling pe RegIrqFlags, cu timeout
 * de 200 ms, fiindca DIO0 nu este cablat (presupunerea 3).
 */
static uint8_t LoRa_SendBuffer(const uint8_t *data, uint8_t length)
{
    uint8_t i;
    uint8_t timeout;

    LoRa_WriteRegister(LORA_REG_OP_MODE, LORA_LONG_RANGE_MODE | LORA_MODE_STDBY);
    LoRa_WriteRegister(LORA_REG_FIFO_ADDR_PTR, 0x00U);

    for (i = 0U; i < length; i++)
    {
        LoRa_WriteRegister(LORA_REG_FIFO, data[i]);
    }

    LoRa_WriteRegister(LORA_REG_PAYLOAD_LENGTH, length);
    LoRa_WriteRegister(LORA_REG_IRQ_FLAGS, 0xFFU);
    LoRa_WriteRegister(LORA_REG_OP_MODE, LORA_LONG_RANGE_MODE | LORA_MODE_TX);

    for (timeout = 0U; timeout < 200U; timeout++)
    {
        if ((LoRa_ReadRegister(LORA_REG_IRQ_FLAGS) & LORA_IRQ_TX_DONE) != 0U)
        {
            LoRa_WriteRegister(LORA_REG_IRQ_FLAGS, LORA_IRQ_TX_DONE);
            LoRa_WriteRegister(LORA_REG_OP_MODE,
                               LORA_LONG_RANGE_MODE | LORA_MODE_STDBY);
            return 1U;
        }
        __delay_ms(1);
    }

    /* Timeout: scoatem radioul din TX ca sa nu ramana blocat acolo. */
    LoRa_WriteRegister(LORA_REG_OP_MODE, LORA_LONG_RANGE_MODE | LORA_MODE_STDBY);
    return 0U;
}

/*
 * NOU: receptie cu timeout, prin polling.
 * ---------------------------------------------------------------------
 * Firmware-ul anterior era doar emitator. Pairing-ul are nevoie de
 * receptie pentru JOIN_ACCEPT si CMD_DOWN, deci aici se adauga:
 *   - trecerea in RX continuu (RX single ar expira singur dupa un numar
 *     de simboluri; cu RX continuu controlam noi timpul de asteptare);
 *   - polling pe RegIrqFlags dupa RxDone, ca peste tot in proiect (DIO0
 *     nu este cablat - presupunerea 3);
 *   - citirea din FIFO de la FifoRxCurrentAddr, cu lungimea din
 *     RegRxNbBytes.
 *
 * "wantType" ESTE OBLIGATORIU SI NU ESTE O COMODITATE. Cat timp exista
 * un singur senzor, orice pachet auzit in fereastra proprie era, prin
 * constructie, raspunsul hub-ului. Cu HUB_MAX_SENSORS = 5 placi pe
 * acelasi canal, in fereastra de 600 ms a senzorului A poate intra la
 * fel de bine un CMD_DOWN adresat lui B: fara filtru, functia s-ar
 * intoarce cu ACEL pachet, apelantul l-ar respinge (MIC-ul sau adresa nu
 * se potrivesc) si fereastra s-ar fi INCHIS DEJA. Senzorul A si-ar rata
 * propriul raspuns, iar pe calea de dezinrolare asta inseamna exact
 * fundatura din F-031: un RESET are o singura sansa per ciclu.
 *
 * Filtrul este deci parte din functionalitate, nu o optimizare:
 * pachetele care nu ne apartin sunt aruncate SI receptia continua cu
 * timpul ramas, exact ca la un CRC gresit. Se verifica magic-ul, tipul
 * si - pentru CMD_DOWN, singurul tip care poarta adresa in clar -
 * DevAddr. MIC-ul ramane treaba apelantului: el are cheia.
 *
 * Al doilea filtru, gratuit, este hardware: RegMaxPayloadLength = 16
 * (sectiunea 3), iar DATA_ENC are 17 octeti. Modemul arunca singur
 * pachetele de date ale celorlalti senzori, inainte sa ajunga la noi.
 *
 * Intoarce 1 daca s-a primit un pachet cu CRC bun SI de tipul cerut, iar
 * "*length" primeste numarul de octeti cititi (cel mult maxLength).
 */
static uint8_t LoRa_Receive(uint8_t *buffer, uint8_t maxLength,
                            uint8_t *length, uint16_t timeoutMs,
                            uint8_t wantType)
{
    uint16_t waited;
    uint8_t  flags;
    uint8_t  received;
    uint8_t  current;
    uint8_t  i;

    *length = 0U;

    LoRa_WriteRegister(LORA_REG_OP_MODE, LORA_LONG_RANGE_MODE | LORA_MODE_STDBY);
    LoRa_WriteRegister(LORA_REG_FIFO_ADDR_PTR, 0x00U);
    LoRa_WriteRegister(LORA_REG_IRQ_FLAGS, 0xFFU);
    LoRa_WriteRegister(LORA_REG_OP_MODE,
                       LORA_LONG_RANGE_MODE | LORA_MODE_RX_CONTINUOUS);

    for (waited = 0U; waited < timeoutMs; waited++)
    {
        flags = LoRa_ReadRegister(LORA_REG_IRQ_FLAGS);

        if ((flags & LORA_IRQ_RX_DONE) != 0U)
        {
            if ((flags & LORA_IRQ_PAYLOAD_CRC_ERROR) != 0U)
            {
                /* Pachet corupt: il aruncam si ramanem in RX. */
                LoRa_WriteRegister(LORA_REG_IRQ_FLAGS, 0xFFU);
                __delay_ms(1);
                continue;
            }

            received = LoRa_ReadRegister(LORA_REG_RX_NB_BYTES);
            current  = LoRa_ReadRegister(LORA_REG_FIFO_RX_CURRENT_ADDR);

            LoRa_WriteRegister(LORA_REG_FIFO_ADDR_PTR, current);

            if (received > maxLength)
            {
                received = maxLength;
            }

            for (i = 0U; i < received; i++)
            {
                buffer[i] = LoRa_ReadRegister(LORA_REG_FIFO);
            }

            /* Filtrul multi-senzor. Un pachet strain este aruncat si
             * receptia CONTINUA, cu timpul ramas: fereastra nu are voie
             * sa fie consumata de vorba altcuiva. */
            if ((received < 3U) ||
                (buffer[0] != LORA_PACKET_MAGIC) ||
                (buffer[1] != wantType) ||
                ((wantType == MSG_TYPE_CMD_DOWN) && (buffer[2] != devAddr)))
            {
                LoRa_WriteRegister(LORA_REG_IRQ_FLAGS, 0xFFU);
                __delay_ms(1);
                continue;
            }

            *length = received;

            LoRa_WriteRegister(LORA_REG_IRQ_FLAGS, 0xFFU);
            LoRa_WriteRegister(LORA_REG_OP_MODE,
                               LORA_LONG_RANGE_MODE | LORA_MODE_STDBY);
            return 1U;
        }

        __delay_ms(1);
    }

    LoRa_WriteRegister(LORA_REG_OP_MODE, LORA_LONG_RANGE_MODE | LORA_MODE_STDBY);
    return 0U;
}

/* Clipeste RegVersion pe LED2, cifra hexa inalta prima. Diagnostic
 * preluat din codul provizoriu (F-010). */
static void LoRa_ShowNibble(uint8_t value)
{
    uint8_t count;

    if (value == 0U)
    {
        /* Un puls lung ON = cifra hexa 0. */
        LED2_LAT = 1;
        __delay_ms(600);
        LED2_LAT = 0;
        __delay_ms(300);
        return;
    }

    for (count = 0U; count < value; count++)
    {
        LED2_LAT = 1;
        __delay_ms(120);
        LED2_LAT = 0;
        __delay_ms(120);
    }
    __delay_ms(500);
}

static void LoRa_ShowVersionError(void)
{
    LoRa_ShowNibble((uint8_t)((loraVersion >> 4) & 0x0FU));
    LoRa_ShowNibble((uint8_t)(loraVersion & 0x0FU));
    __delay_ms(800);
}

/* =====================================================================
 * 10. ADC + CONVERSIA NTC   (neschimbate fata de firmware-ul anterior)
 * ================================================================== */

/*
 * ADC: 10 biti, referinta VDD, rezultat aliniat la dreapta.
 * ADCON1 = 0xD0 -> ADFM=1, ADCS=101 (FOSC/16 => TAD = 1 us la 16 MHz),
 *                  ADNREF=0 (Vss), ADPREF=00 (VDD).
 */
static void ADC_Initialize(void)
{
    NTC_ANSEL = 1;      /* RC2 analogic */
    NTC_TRIS  = 1;      /* si intrare   */

    ADCON1 = 0xD0U;
    ADCON2 = 0x00U;     /* fara trigger automat */
    ADCON0 = (uint8_t)((NTC_ADC_CHANNEL << 2) | 0x01U);  /* canal + ADON */

    __delay_us(50);     /* stabilizarea circuitului de sample&hold */
}

static uint16_t ADC_ReadRaw(void)
{
    ADCON0 = (uint8_t)((NTC_ADC_CHANNEL << 2) | 0x01U);

    __delay_us(20);     /* timp de achizitie */

    ADCON0bits.GO_nDONE = 1;
    while (ADCON0bits.GO_nDONE)
    {
        /* conversia dureaza ~11 TAD */
    }

    return (uint16_t)(((uint16_t)ADRESH << 8) | ADRESL);
}

/* Media a ADC_SAMPLES citiri, ca sa taiem zgomotul de pe divizor. */
static uint16_t ADC_ReadAveraged(void)
{
    uint16_t sum = 0U;
    uint8_t  i;

    for (i = 0U; i < ADC_SAMPLES; i++)
    {
        sum += ADC_ReadRaw();
        __delay_ms(1);
    }

    return (uint16_t)(sum >> ADC_SAMPLES_SHIFT);
}

/*
 * Tabel NTC 10K / B25/50 = 3950, cu rezistor fix de 10 kOhm spre VDD.
 * Valorile sunt codul ADC pe 10 biti pentru fiecare temperatura, de la
 * -20 C la +100 C, din 5 in 5 grade. Calculate cu:
 *     R(T)  = 10000 * exp(3950 * (1/T[K] - 1/298.15))
 *     ADC(T)= 1023 * R(T) / (10000 + R(T))
 *
 * F-016: nu folosim log()/exp() la runtime - libraria in virgula mobila
 * nu incape in cei 4K words ai PIC16LF1508 alaturi de driverul LoRa si
 * de cifru. Interpolare liniara pe intregi, in schimb - si chiar aceea
 * pe 16 biti, nu pe 32 (F-028).
 *
 * Tabelul este DESCRESCATOR in cod ADC pe masura ce temperatura creste.
 */
#define NTC_TABLE_SIZE      25U
#define NTC_TEMP_MIN_C      (-20)   /* temperatura primei intrari */
#define NTC_TEMP_STEP_C     5       /* pasul intre doua intrari    */

static const uint16_t ntcAdcTable[NTC_TABLE_SIZE] = {
    934U,  /* -20 C */
    907U,  /* -15 C */
    873U,  /* -10 C */
    834U,  /*  -5 C */
    788U,  /*   0 C */
    738U,  /*   5 C */
    684U,  /*  10 C */
    627U,  /*  15 C */
    569U,  /*  20 C */
    512U,  /*  25 C */
    456U,  /*  30 C */
    403U,  /*  35 C */
    354U,  /*  40 C */
    310U,  /*  45 C */
    270U,  /*  50 C */
    235U,  /*  55 C */
    204U,  /*  60 C */
    177U,  /*  65 C */
    153U,  /*  70 C */
    133U,  /*  75 C */
    115U,  /*  80 C */
    100U,  /*  85 C */
     87U,  /*  90 C */
     76U,  /*  95 C */
     67U   /* 100 C */
};

/*
 * Converteste un cod ADC in temperatura x 100 (deci 2350 = 23.50 C).
 * Intoarce TEMP_INVALID daca valoarea cade in afara tabelului, adica
 * senzorul este in scurt, deconectat, sau in afara domeniului.
 */
static int16_t NTC_AdcToTempX100(uint16_t adc)
{
    uint8_t  i;
    uint16_t high;
    uint16_t low;
    uint16_t span;
    uint16_t frac;
    int16_t  temp;

#if NTC_PULLUP_TO_VDD == 0
    /* Divizor inversat (rezistor fix spre GND, NTC spre VDD): codul ADC
     * este complementul celui din tabel. */
    adc = (adc > 1023U) ? 0U : (uint16_t)(1023U - adc);
#endif

    /* In afara capetelor tabelului -> citire nevalida. */
    if ((adc > ntcAdcTable[0]) || (adc < ntcAdcTable[NTC_TABLE_SIZE - 1U]))
    {
        return TEMP_INVALID;
    }

    /* Cautam intervalul [i, i+1] care incadreaza valoarea. */
    for (i = 0U; i < (NTC_TABLE_SIZE - 1U); i++)
    {
        high = ntcAdcTable[i];       /* cod mai mare = temperatura mai mica */
        low  = ntcAdcTable[i + 1U];

        if ((adc <= high) && (adc >= low))
        {
            /*
             * Interpolare liniara intre cele doua puncte, in sutimi de
             * grad, TOATA in aritmetica pe 16 biti (F-028).
             *
             * Varianta directa - (high-adc) * 500 / (high-low) - ar cere
             * un intermediar de pana la 511.500, deci int32 si, odata cu
             * el, rutinele de inmultire si IMPARTIRE pe 32 de biti din
             * biblioteca XC8: 172 de cuvinte de program pentru o singura
             * impartire, pe un device care are 4096 in total.
             *
             * In loc de asta impartim in doi pasi de 16 biti:
             *   frac = (high-adc) * 64 / (high-low)   -> 0..64
             *   sutimi = frac * 500 / 64
             * Intermediarul cel mai mare este 1023*64 = 65.472, adica
             * exact sub limita lui uint16. Cuantizarea rezultata este
             * 500/64 = 7,8 sutimi de grad, adica sub 0,08 C - cu un ordin
             * de marime sub toleranta unui NTC de 1% si sub zgomotul
             * celor 8 citiri ADC mediate.
             */
            span = (uint16_t)(high - low);       /* mereu > 0 in tabel */
            frac = (uint16_t)(((uint16_t)(high - adc) * 64U) / span);

            temp = (int16_t)(((int16_t)NTC_TEMP_MIN_C +
                              ((int16_t)i * (int16_t)NTC_TEMP_STEP_C)) * 100);
            temp = (int16_t)(temp +
                             (int16_t)((frac * (NTC_TEMP_STEP_C * 100U)) / 64U));

            return temp;
        }
    }

    return TEMP_INVALID;
}

/* =====================================================================
 * 11. BUTOANE   (neschimbate fata de firmware-ul anterior)
 * ================================================================== */

/* Nivelul brut al butonului de fortare (activ HIGH, pull-down extern). */
static uint8_t Button_RawPressed(void)
{
    if (BUTTON_FORCE_PORT != 0U)
    {
        return 1U;
    }

    /* RC5 NU se citeste aici, si blocul de mai jos trebuie sa ramana
     * comentat: butonul 2 are acum o functie proprie (pairing manual,
     * ButtonPair_HeldLong). Daca ar forta si transmisii, cele trei
     * secunde de tinut apasat ar declansa in acelasi timp si fereastra
     * de pairing, si un sir de DATA_ENC pe acelasi canal.
     * if (BUTTON_SPARE_PORT != 0U) { return 1U; }
     */

    return 0U;
}

/*
 * Citire cu debounce (F-015): citeste, asteapta, reciteste. O
 * nepotrivire inseamna bounce si se ignora.
 */
static uint8_t Button_Pressed(void)
{
    if (Button_RawPressed() == 0U)
    {
        return 0U;
    }

    __delay_ms(BUTTON_DEBOUNCE_MS);

    return Button_RawPressed();
}

/* Asteapta eliberarea butonului, ca o apasare lunga sa nu genereze un
 * sir de transmisii. */
static void Button_WaitRelease(void)
{
    while (Button_RawPressed() != 0U)
    {
        __delay_ms(TX_TICK_MS);
    }
    __delay_ms(BUTTON_DEBOUNCE_MS);
}

/* --- Butonul 2 (RC5): pairing manual -------------------------------- */

/*
 * Un pas de asteptare cu LED2 comutat. Este SINGURUL loc din firmware
 * unde exista o intarziere de PAIR_HOLD_TICK_MS, si de aceea este o
 * functie si nu doua randuri copiate: __delay_ms() genereaza cod inline
 * la fiecare loc de apel, iar o bucla de intarziere de 100 ms costa vreo
 * 25 de cuvinte. Cele doua bucle de pairing (numaratoarea butonului si
 * asteptarea dintre incercarile de join) o impart pe aceasta.
 */
static void Pairing_BlinkStep(void)
{
    LED2_LAT ^= 1;
    __delay_ms(PAIR_HOLD_TICK_MS);
}

/* Nivelul brut al butonului 2. Tinut separat de Button_RawPressed()
 * dinadins: RC5 deschide fereastra de pairing, nu forteaza o
 * transmisie. */
static uint8_t ButtonPair_RawPressed(void)
{
    return (uint8_t)((BUTTON_SPARE_PORT != 0U) ? 1U : 0U);
}

/*
 * Intoarce 1 daca butonul 2 a fost tinut apasat PAIR_HOLD_TICKS pasi de
 * PAIR_HOLD_TICK_MS, adica trei secunde. O apasare scurta nu face nimic:
 * intrarea in pairing sterge sesiunea unui senzor deja inrolat, deci nu
 * are voie sa poata fi declansata dintr-o atingere.
 *
 * Debounce-ul cerut de F-015 este chiar bucla de mai jos: pinul se
 * reciteste de 30 de ori, la 100 ms distanta, si prima citire LOW
 * abandoneaza numaratoarea. Un bounce nu are cum sa treaca de ea, deci o
 * recitire separata la 20 ms, ca in Button_Pressed(), ar costa cuvinte de
 * program fara sa adauge nimic.
 *
 * Bucla acopera si asteptarea eliberarii: dupa atingerea pragului
 * continua sa se invarta pana cand butonul este dat drumul, altfel
 * aceeasi apasare ar fi citita a doua oara de bucla apelanta. Contorul se
 * opreste la prag, deci nu se poate rasuci.
 *
 * Functia se cheama la fiecare pas de asteptare din bucla de date, deci
 * calea "buton neapasat" este ieftina dinadins: o citire de port si o
 * comparatie.
 */
static uint8_t ButtonPair_HeldLong(void)
{
    uint8_t ticks = 0U;

    while (ButtonPair_RawPressed() != 0U)
    {
        if (ticks < PAIR_HOLD_TICKS)
        {
            ticks++;
        }

        Pairing_BlinkStep();
    }

    LED2_LAT = 0;

    return (uint8_t)((ticks >= PAIR_HOLD_TICKS) ? 1U : 0U);
}

/* =====================================================================
 * 12. LED-URI
 *     LED1 (RC3) = transmisie de date, LED2 (RC6) = pairing / eroare.
 * ================================================================== */

/* LED1 (transmisie de date) NU are o functie de puls: pulsul ar fi o
 * intarziere blocanta chiar in fereastra de downlink (F-032). Se aprinde
 * si se stinge direct, in jurul receptiei. */

static void Led_PulsePairing(void)
{
    LED2_LAT = 1;
    __delay_ms(LED_PULSE_MS);
    LED2_LAT = 0;
}

/* Trei clipiri scurte pe LED2: o incercare de join a esuat. */
static void Led_ShowJoinFailure(void)
{
    uint8_t i;

    for (i = 0U; i < 3U; i++)
    {
        LED2_LAT = 1;
        __delay_ms(80);
        LED2_LAT = 0;
        __delay_ms(80);
    }
}

/* =====================================================================
 * 13. CONSTRUIREA PACHETELOR
 * ================================================================== */

/* Pachetul de temperatura de 6 octeti - EXACT cel dinainte. Este si
 * payload-ul care se cripteaza in DATA_ENC. */
static void Packet_BuildTemperature(uint8_t *packet, int16_t tempX100,
                                    uint8_t reason)
{
    uint8_t checksum;
    uint8_t i;

    packet[0] = LORA_PACKET_MAGIC;
    packet[1] = MSG_TYPE_TEMPERATURE;
    packet[2] = (uint8_t)(((uint16_t)tempX100 >> 8) & 0xFFU);   /* big-endian */
    packet[3] = (uint8_t)((uint16_t)tempX100 & 0xFFU);
    packet[4] = reason;

    checksum = 0U;
    for (i = 0U; i < (LORA_PACKET_LEN - 1U); i++)
    {
        checksum ^= packet[i];
    }
    packet[5] = (uint8_t)(checksum ^ CHECKSUM_SALT);
}

/*
 * DevNonce nou. PIC-ul nu are RNG (presupunerea 10): amestecam bitii cei
 * mai putin semnificativi ai unor citiri ADC succesive - care variaza cu
 * zgomotul de pe divizor - cu frame counter-ul, care este diferit la
 * fiecare incercare. Scopul este strict sa nu repetam un nonce, nu sa
 * obtinem entropie criptografica.
 */
static void Nonce_Generate(void)
{
    uint16_t value = 0U;
    uint8_t  i;

    for (i = 0U; i < 16U; i++)
    {
        value = (uint16_t)((value << 1) | (uint16_t)(ADC_ReadRaw() & 0x0001U));
        __delay_ms(1);
    }

    value ^= (uint16_t)(frameCounter & 0xFFFFUL);

    /* 0x0000 ar arata ca un camp nescris; il evitam. */
    if (value == 0U)
    {
        value = 1U;
    }

    devNonce[0] = (uint8_t)((value >> 8) & 0xFFU);
    devNonce[1] = (uint8_t)(value & 0xFFU);
}

/*
 * Cheia de sesiune, derivata din AppKey si din cele doua nonce-uri.
 *
 * MAC-ul da 8 octeti, iar cheia are 16, deci se cheama de doua ori,
 * peste acelasi bloc dar cu prefix diferit:
 *   B = <prefix> | DevNonce(2) | JoinNonce(3) | DevAddr(1) | 0x00
 *   SessKey[0..7]  = MAC(AppKey, B cu prefix 0x01)
 *   SessKey[8..15] = MAC(AppKey, B cu prefix 0x02)
 * Blocul are exact 8 octeti, cat blocul cifrului, deci nu apare padding.
 * Hub-ul face aceeasi operatie, octet cu octet.
 */
static void Session_DeriveKey(void)
{
    uint8_t i;

    cryptoBlock[0] = 0x01U;
    cryptoBlock[1] = devNonce[0];
    cryptoBlock[2] = devNonce[1];
    cryptoBlock[3] = joinNonce[0];
    cryptoBlock[4] = joinNonce[1];
    cryptoBlock[5] = joinNonce[2];
    cryptoBlock[6] = devAddr;
    cryptoBlock[7] = 0x00U;

    Key_UseApp();
    Xtea_MacWithLoadedKey(cryptoBlock, XTEA_BLOCK_LEN, macBuffer);
    for (i = 0U; i < XTEA_BLOCK_LEN; i++)
    {
        sessKey[i] = macBuffer[i];
    }

    cryptoBlock[0] = 0x02U;
    Xtea_MacWithLoadedKey(cryptoBlock, XTEA_BLOCK_LEN, macBuffer);
    for (i = 0U; i < XTEA_BLOCK_LEN; i++)
    {
        sessKey[XTEA_BLOCK_LEN + i] = macBuffer[i];
    }

    /* Cifrul are inca AppKey incarcata - nu am atins-o. */
}

/* JOIN_REQ: 16 octeti, semnat cu AppKey. */
static void Packet_BuildJoinRequest(void)
{
    uint8_t i;

    txBuffer[0] = LORA_PACKET_MAGIC;
    txBuffer[1] = MSG_TYPE_JOIN_REQ;

    for (i = 0U; i < DEV_EUI_LEN; i++)
    {
        txBuffer[2U + i] = devEui[i];
    }

    txBuffer[10] = devNonce[0];
    txBuffer[11] = devNonce[1];

    Key_UseApp();
    Xtea_MacWithLoadedKey(txBuffer, JOIN_REQ_MIC_INPUT_LEN, macBuffer);

    for (i = 0U; i < MIC_LEN; i++)
    {
        txBuffer[JOIN_REQ_MIC_INPUT_LEN + i] = macBuffer[i];
    }
}

/*
 * Verifica un JOIN_ACCEPT si, daca este bun, umple devAddr + joinNonce.
 * Pasii sunt exact inversul a ce face hub-ul:
 *   1) cei 4 octeti cifrati se trec prin XTEA-CTR cu AppKey si cu
 *      IV_join = 0x11 | DevNonce(2) | zero(5) -> DevAddr + JoinNonce.
 *      CTR este simetric, deci NU exista cod de descifrare in firmware;
 *   2) se recalculeaza MIC-ul peste 0x11 | DevEUI | DevNonce | DevAddr |
 *      JoinNonce si se compara cu cel primit.
 * DevNonce este cel trimis de noi in JOIN_REQ chiar acum, deci un
 * JOIN_ACCEPT rejucat dintr-o inrolare veche pica la pasul 2 - si, in
 * plus, s-ar descifra in gunoi, fiindca IV-ul depinde tot de DevNonce.
 */
static uint8_t Packet_ParseJoinAccept(const uint8_t *packet, uint8_t length)
{
    uint8_t i;
    uint8_t candidateAddr;
    uint8_t plain[JOIN_ACCEPT_ENC_LEN];

    if (length != JOIN_ACCEPT_LEN)                 return 0U;
    if (packet[0] != LORA_PACKET_MAGIC)            return 0U;
    if (packet[1] != MSG_TYPE_JOIN_ACCEPT)         return 0U;

    Key_UseApp();

    /* IV_join = 0x11 | DevNonce(2) | zero(5). */
    for (i = 0U; i < XTEA_BLOCK_LEN; i++)
    {
        cryptoBlock[i] = 0U;
    }
    cryptoBlock[0] = MSG_TYPE_JOIN_ACCEPT;
    cryptoBlock[1] = devNonce[0];
    cryptoBlock[2] = devNonce[1];

    for (i = 0U; i < JOIN_ACCEPT_ENC_LEN; i++)
    {
        plain[i] = packet[2U + i];
    }
    Xtea_CtrWithLoadedKey(cryptoBlock, plain, JOIN_ACCEPT_ENC_LEN);

    candidateAddr = plain[0];
    if ((candidateAddr == 0x00U) || (candidateAddr == 0xFFU))
    {
        /* Hub-ul aloca doar 0x01..0xFE. */
        return 0U;
    }

    /* MIC peste 0x11 | DevEUI(8) | DevNonce(2) | DevAddr(1) | JoinNonce(3). */
    micInput[0] = MSG_TYPE_JOIN_ACCEPT;
    for (i = 0U; i < DEV_EUI_LEN; i++)
    {
        micInput[1U + i] = devEui[i];
    }
    micInput[9]  = devNonce[0];
    micInput[10] = devNonce[1];
    micInput[11] = candidateAddr;
    micInput[12] = plain[1];
    micInput[13] = plain[2];
    micInput[14] = plain[3];

    Xtea_MacWithLoadedKey(micInput, JOIN_ACCEPT_MIC_INPUT_LEN, macBuffer);

    if (Mic_Matches(&packet[JOIN_ACCEPT_ENC_LEN + 2U], macBuffer) == 0U)
    {
        return 0U;
    }

    devAddr      = candidateAddr;
    joinNonce[0] = plain[1];
    joinNonce[1] = plain[2];
    joinNonce[2] = plain[3];

    return 1U;
}

/*
 * DATA_ENC: 17 octeti. Payload-ul este pachetul de temperatura de 6
 * octeti, criptat cu XTEA-CTR (sau lasat in clar daca
 * PAIRING_ENCRYPT_PAYLOAD este 0), iar totul este semnat cu SessKey.
 */
static void Packet_BuildDataEnc(const uint8_t *tempPacket, uint32_t counter)
{
    Word32  packed;
    uint8_t i;

    packed.word = counter;

    txBuffer[0] = LORA_PACKET_MAGIC;
    txBuffer[1] = MSG_TYPE_DATA_ENC;
    txBuffer[2] = devAddr;
    txBuffer[3] = packed.byte[3];       /* FrameCounter big-endian */
    txBuffer[4] = packed.byte[2];
    txBuffer[5] = packed.byte[1];
    txBuffer[6] = packed.byte[0];

    for (i = 0U; i < LORA_PACKET_LEN; i++)
    {
        txBuffer[7U + i] = tempPacket[i];
    }

    Key_UseSession();

#if PAIRING_ENCRYPT_PAYLOAD
    /* IV = DevAddr(1) | FrameCounter(4) | 0x00 (uplink) | zero(2).
     * Are exact 8 octeti, cat blocul cifrului. */
    cryptoBlock[0] = devAddr;
    cryptoBlock[1] = txBuffer[3];
    cryptoBlock[2] = txBuffer[4];
    cryptoBlock[3] = txBuffer[5];
    cryptoBlock[4] = txBuffer[6];
    cryptoBlock[5] = 0x00U;
    cryptoBlock[6] = 0x00U;
    cryptoBlock[7] = 0x00U;

    Xtea_CtrWithLoadedKey(cryptoBlock, &txBuffer[7], LORA_PACKET_LEN);
#endif

    Xtea_MacWithLoadedKey(txBuffer, DATA_ENC_MIC_INPUT_LEN, macBuffer);

    for (i = 0U; i < MIC_LEN; i++)
    {
        txBuffer[DATA_ENC_MIC_INPUT_LEN + i] = macBuffer[i];
    }
}

/*
 * Verifica un CMD_DOWN si intoarce tipul comenzii, sau 0 daca pachetul
 * nu este pentru noi / nu este autentic.
 */
static uint8_t Packet_ParseCommand(const uint8_t *packet, uint8_t length)
{
    if (length != CMD_DOWN_LEN)             return 0U;
    if (packet[0] != LORA_PACKET_MAGIC)     return 0U;
    if (packet[1] != MSG_TYPE_CMD_DOWN)     return 0U;
    if (packet[2] != devAddr)               return 0U;

    Key_UseSession();
    Xtea_MacWithLoadedKey(packet, CMD_DOWN_MIC_INPUT_LEN, macBuffer);

    if (Mic_Matches(&packet[CMD_DOWN_MIC_INPUT_LEN], macBuffer) == 0U)
    {
        return 0U;
    }

    return packet[7];
}

/* =====================================================================
 * 14. INITIALIZAREA PINILOR
 * ================================================================== */

static void Board_Initialize(void)
{
    /* --- Watchdog: doar ca sursa de trezire din somn --------------- */
    /* Perioada se scrie explicit, desi 0x0B este si valoarea de reset a
     * lui WDTCON: asa nu depinde de ea nimic. SWDTEN ramane 0, deci in
     * veghe watchdog-ul este oprit si nu poate reseta placa in mijlocul
     * unei transmisii sau al unei scrieri in HEF. */
    WDTCONbits.WDTPS = SLEEP_WDT_WDTPS;
    WDTCONbits.SWDTEN = 0;

    /* RC1 nu apare aici: este liber si neconectat, deci ramane pe
     * configuratia MCC din pins.c (intrare analogica). Un pin nefolosit
     * nu are nevoie de cod. */

    /* --- Butoane pe RC4 / RC5 -------------------------------------- */
    /* RC4 si RC5 nu au bit ANSEL pe acest device, deci doar TRIS. */
    TRISCbits.TRISC4 = 1;
    TRISCbits.TRISC5 = 1;

    /* --- LED-uri pe RC3 (LED1) si RC6 (LED2) ----------------------- */
    /* Intai scoase din analogic, apoi stinse, apoi facute iesiri:
     * asa nu apare niciun impuls parazit la pornire. */
    ANSELCbits.ANSC3 = 0;
    ANSELCbits.ANSC6 = 0;
    LATCbits.LATC3   = 0;
    LATCbits.LATC6   = 0;
    TRISCbits.TRISC3 = 0;
    TRISCbits.TRISC6 = 0;

    /* --- LoRa NSS pe RB5: iesire digitala, inactiv HIGH ------------ */
    LORA_NSS_ANSEL = 0;
    LORA_NSS_TRIS  = 0;
    LORA_NSS_LAT   = 1;

    /* --- Senzorul NTC pe RC2 (AN6) --------------------------------- */
    ADC_Initialize();
}

/* Semnal scurt de pornire: ambele LED-uri clipesc o data. Confirma ca
 * firmware-ul a pornit si ca ambele LED-uri sunt cablate corect. */
static void Board_StartupBlink(void)
{
    LED1_LAT = 1;
    LED2_LAT = 1;
    __delay_ms(300);
    LED1_LAT = 0;
    LED2_LAT = 0;
    __delay_ms(300);
}

/*
 * Asteptare in pasi de TX_TICK_MS, cu LED2 clipind: fereastra de pairing
 * este deschisa si trebuie sa se vada de la distanta ca senzorul asteapta
 * un JOIN_ACCEPT. Nu exista timer hardware (F-017) si nu se adauga unul
 * acum, deci clipirea se obtine comutand LED2 la fiecare pas al buclei de
 * asteptare - nu o a treia bucla de temporizare.
 *
 * Pasul este PAIR_HOLD_TICK_MS, nu TX_TICK_MS: da direct clipirea de
 * 5 Hz, scuteste contorul separat de pasi si, mai ales, imparte cu
 * ButtonPair_HeldLong singura bucla de intarziere de 100 ms din firmware
 * (Pairing_BlinkStep). Precizia pierduta este de cel mult 100 ms peste un
 * backoff masurat in secunde.
 *
 * Butonul 1 nu se citeste aici: in fereastra de pairing o transmisie
 * fortata nu are ce sa forteze, iar butonul 2 se asculta doar dupa
 * inchiderea ferestrei, ca o apasare lunga sa nu reporneasca fereastra
 * peste ea insasi.
 */
static void Wait_PairingBlink(uint16_t milliseconds)
{
    uint16_t waited;

    for (waited = 0U; waited < milliseconds;
         waited = (uint16_t)(waited + PAIR_HOLD_TICK_MS))
    {
        Pairing_BlinkStep();
    }
}

/*
 * Generator pseudo-aleator de un octet: LFSR Galois cu polinomul
 * x^8 + x^6 + x^5 + x^4 + 1 (masca 0xB8), perioada 255.
 *
 * Nu are nicio pretentie criptografica si nu are voie sa capete una:
 * DevNonce-ul de la inrolare se face in continuare din zgomotul ADC
 * (Nonce_Generate), fiindca acolo repetarea unei valori chiar
 * inseamna ceva. Aici scopul este strict sa imprastie momentul
 * transmisiei intre cei 5 senzori, si pentru asta o secventa
 * previzibila dar DIFERITA de la placa la placa este exact ce trebuie.
 *
 * Costa 1 octet de RAM si o mana de instructiuni; o varianta pe 16 biti
 * ar fi fost mai lunga fara sa imprastie mai bine cele 4 valori de
 * jitter de care avem nevoie.
 */
static uint8_t rngState = 0xA5U;

static uint8_t Rand8(void)
{
    if ((rngState & 0x01U) != 0U)
    {
        rngState = (uint8_t)((uint8_t)(rngState >> 1) ^ 0xB8U);
    }
    else
    {
        rngState = (uint8_t)(rngState >> 1);
    }

    return rngState;
}

/*
 * Semanarea generatorului, chemata o singura data la pornire, dupa ce
 * DevEUI si frame counter-ul au fost citite din HEF.
 *
 * Cele doua surse se completeaza: DevEUI face ca doua placi pornite in
 * aceeasi secunda sa nu produca aceeasi secventa, iar frame counter-ul
 * face ca ACEEASI placa sa nu reia aceeasi secventa dupa fiecare reset.
 * Zero este singura stare interzisa a unui LFSR (ramane blocata), deci
 * se inlocuieste.
 */
static void Rand_Seed(void)
{
    rngState = (uint8_t)(devEui[DEV_EUI_LEN - 1U] ^ (uint8_t)frameCounter);

    if (rngState == 0U)
    {
        rngState = 0xA5U;
    }
}

/*
 * Somnul dintre doua transmisii, valabil DOAR in DEV_STATE_OPERATING.
 *
 * Trezirea o da watchdog-ul: la expirare in timpul lui SLEEP, WDT-ul
 * TREZESTE procesorul in loc sa il reseteze, iar cu GIE = 0 executia
 * continua de la instructiunea urmatoare - deci nu este nevoie de nicio
 * rutina de intrerupere si de niciun timer (F-017).
 *
 * Watchdog-ul se aprinde doar in jurul lui SLEEP: in veghe, transmisia,
 * fereastra de receptie si scrierea in HEF sunt lungi si blocante, iar un
 * WDT pornit le-ar reseta. De aceea config bit-ul este WDTE = SWDTEN, nu
 * WDTE = ON.
 *
 * Somnul este fragmentat fiindca RC5 este pe PORTC, iar acest device are
 * interrupt-on-change doar pe PORTA si PORTB: un senzor adormit nu poate
 * fi trezit de buton, deci il verificam noi la fiecare trezire.
 *
 * Intoarce:
 *   0 = somnul s-a incheiat normal;
 *   1 = butonul 1 (RC4) este apasat -> transmisie fortata;
 *   2 = butonul 2 (RC5) este apasat -> posibil pairing.
 */
#define SLEEP_WOKE_NORMAL       0U
#define SLEEP_WOKE_FORCE        1U
#define SLEEP_WOKE_PAIR         2U

static uint8_t Sleep_Cycle(void)
{
    uint8_t ticks;
    uint8_t wakeups;

    /*
     * Durata somnului se recalculeaza la FIECARE ciclu, si de aceea
     * calculul sta aici si nu la inrolare:
     *   baza      - comuna tuturor placilor;
     *   slotul    - (DevAddr-1), fix pentru placa asta, deci fiecare
     *               senzor are alt interval nominal;
     *   jitter-ul - 0..3 treziri, altul la fiecare ciclu.
     * Masca de pe slot nu este cosmetica: daca cineva reduce
     * HUB_MAX_SENSORS sau ramane in registru o adresa mare dintr-o
     * versiune veche a hub-ului, ea tine somnul in domeniul util in loc
     * sa il faca de zeci de minute.
     */
    wakeups = (uint8_t)(SLEEP_WAKEUPS_BASE
                        + ((uint8_t)(devAddr - 1U) & SLEEP_SLOT_MASK)
                        + (uint8_t)(Rand8() & SLEEP_JITTER_MASK));

    for (ticks = 0U; ticks < wakeups; ticks++)
    {
        CLRWDT();
        WDTCONbits.SWDTEN = 1;
        SLEEP();
        NOP();                  /* instructiunea preluata deja in pipeline */
        WDTCONbits.SWDTEN = 0;

        /* Citiri brute: debounce-ul si numaratoarea le fac apelantii.
         * RC5 primul, ca o apasare pe el sa nu fie citita ca RC4. */
        if (ButtonPair_RawPressed() != 0U)
        {
            return SLEEP_WOKE_PAIR;
        }

        if (Button_RawPressed() != 0U)
        {
            return SLEEP_WOKE_FORCE;
        }
    }

    return SLEEP_WOKE_NORMAL;
}

/* =====================================================================
 * 15. INROLAREA (JOINING)
 * ================================================================== */

/*
 * O singura incercare de join: DevNonce nou -> JOIN_REQ -> RX ->
 * JOIN_ACCEPT. La succes deriva SessKey, o salveaza in HEF impreuna cu
 * DevAddr si intoarce 1.
 */
static uint8_t Join_Attempt(void)
{
    uint8_t length = 0U;

    Nonce_Generate();
    Packet_BuildJoinRequest();

    LED2_LAT = 1;                       /* LED2 aprins = incercam sa ne inrolam */

    if (LoRa_SendBuffer(txBuffer, JOIN_REQ_LEN) == 0U)
    {
        LED2_LAT = 0;
        return 0U;
    }

    /* Fereastra de receptie pentru JOIN_ACCEPT. Filtrul pe tip arunca
     * JOIN_REQ-urile altor senzori care se inroleaza in aceeasi
     * fereastra a hub-ului; un JOIN_ACCEPT adresat altcuiva nu poate fi
     * filtrat aici, fiindca DevAddr circula cifrat, dar pica la MIC in
     * Packet_ParseJoinAccept. */
    if (LoRa_Receive(rxBuffer, LORA_RX_BUFFER_LEN, &length,
                     JOIN_RX_TIMEOUT_MS, MSG_TYPE_JOIN_ACCEPT) == 0U)
    {
        LED2_LAT = 0;
        return 0U;
    }

    if (Packet_ParseJoinAccept(rxBuffer, length) == 0U)
    {
        LED2_LAT = 0;
        return 0U;
    }

    Session_DeriveKey();

    /* Sesiune noua inseamna si counter de la zero: hub-ul reseteaza
     * lastFrameCounterUp la inrolare, deci cele doua capete pornesc de
     * la aceeasi valoare. */
    frameCounter = 0UL;
    fcntSinceCheckpoint = 0UL;

    Nvm_SaveSession();
    Nvm_SaveFrameCounter(frameCounter);

    LED2_LAT = 0;
    return 1U;
}

/* =====================================================================
 * 16. PROGRAMUL PRINCIPAL
 * ================================================================== */

int main(void)
{
    uint16_t joinBackoffMs;
    uint8_t  loraReady;
    uint8_t  forcedByButton;
    uint8_t  rePair;            /* butonul 2 tinut apasat in exploatare */
    uint8_t  wokeBy;            /* ce a incheiat somnul dintre transmisii */
    uint8_t  joinAttempts;      /* incercari in fereastra curenta        */
    uint8_t  rxLength;
    uint8_t  command;
    uint8_t  tempPacket[LORA_PACKET_LEN];
    int16_t  tempX100;

    SYSTEM_Initialize();        /* clock 16 MHz + pini MCC + SPI1 + IRQ */
    Board_Initialize();         /* peste config-ul MCC, ce ne trebuie noua */
    Board_StartupBlink();

    /* --- Identitatea si sesiunea, din HEF --------------------------- */
    Nvm_LoadOrCreateProvisioning();

    frameCounter = Nvm_LoadFrameCounter();

    /*
     * Counter-ul se salveaza doar la fiecare FCNT_CHECKPOINT_EVERY
     * pachete, deci valoarea din HEF poate fi in urma cu pana la
     * FCNT_CHECKPOINT_EVERY-1. La un cold boot sarim inainte cu tot
     * intervalul: asa nu reutilizam niciodata o valoare deja emisa, iar
     * hub-ul (care accepta doar counter strict crescator) nu ne respinge.
     * Costul este o "gaura" in numerotare dupa fiecare reset, ceea ce nu
     * deranjeaza pe nimeni.
     */
    frameCounter += FCNT_CHECKPOINT_EVERY;
    fcntSinceCheckpoint = 0UL;

    /* Acum avem si DevEUI, si counter-ul: putem semana jitter-ul de somn
     * cu ceva ce difera si intre placi, si intre porniri. */
    Rand_Seed();

    if (Nvm_LoadSession() != 0U)
    {
        deviceState = DEV_STATE_OPERATING;
    }
    else
    {
        /* Fara sesiune senzorul NU se mai inroleaza singur: tace pana
         * cand utilizatorul tine butonul 2 apasat trei secunde. */
        deviceState = DEV_STATE_IDLE;
    }

    loraReady = LoRa_Initialize();

    if (loraReady == 0U)
    {
        /* F-010: radioul nu a raspuns. Clipim RegVersion pe LED2 ca sa
         * se vada CE s-a citit, apoi continuam: masuratoarea si LED-urile
         * raman utile pentru diagnosticul restului placii. */
        LoRa_ShowVersionError();
    }

    joinBackoffMs  = JOIN_BACKOFF_START_MS;
    joinAttempts   = 0U;
    forcedByButton = 0U;

    while (1)
    {
        /* =============================================================
         * A0. Repaus: nu emitem nimic si asteptam butonul 2.
         *     Se ajunge aici la pornirea fara sesiune, la epuizarea
         *     ferestrei de pairing si dupa un CMD_DOWN(RESET).
         * ========================================================== */
        if (deviceState == DEV_STATE_IDLE)
        {
            /* Fara __delay_ms() aici: ButtonPair_HeldLong() se intoarce
             * imediat cand RC5 este LOW, deci bucla doar citeste portul in
             * continuu. Placa este alimentata permanent, asa ca o
             * asteptare nu ar economisi nimic, in schimb inca o
             * intarziere inline ar costa vreo 10 cuvinte. */
            if (ButtonPair_HeldLong() != 0U)
            {
                deviceState   = DEV_STATE_JOINING;
                joinBackoffMs = JOIN_BACKOFF_START_MS;
                joinAttempts  = 0U;
            }

            continue;
        }

        /* =============================================================
         * A. Fereastra de pairing deschisa de utilizator: incercam sa ne
         *    inrolam de cel mult PAIRING_MAX_ATTEMPTS ori, apoi ne
         *    intoarcem in repaus. Nu se mai incearca la nesfarsit:
         *    hub-ul asculta doar in fereastra lui de 120 s.
         * ========================================================== */
        if (deviceState == DEV_STATE_JOINING)
        {
            /* F-013: niciun acces SPI daca MSSP-ul nu a fost deschis -
             * SPI1_ByteExchange asteapta SSP1IF la nesfarsit si ar bloca
             * firmware-ul aici. Cu radioul mut nu are rost sa tinem
             * fereastra deschisa, deci semnalam esecul si ne intoarcem in
             * repaus in loc sa ne invartim degeaba. */
            if (loraReady == 0U)
            {
                Led_ShowJoinFailure();
                LED2_LAT    = 0;
                deviceState = DEV_STATE_IDLE;
                continue;
            }

#if ENABLE_PLAIN_TEMP
            /* Bring-up: trimitem si temperatura in clar, ca sa se poata
             * folosi testul 7 al hub-ului fara pairing. De cand
             * inrolarea este manuala, asta se intampla doar in fereastra
             * deschisa explicit de utilizator, nu la nesfarsit. */
            tempX100 = NTC_AdcToTempX100(ADC_ReadAveraged());
            Packet_BuildTemperature(tempPacket, tempX100, REASON_INTERVAL);
            (void)LoRa_SendBuffer(tempPacket, LORA_PACKET_LEN);
#endif

            if (Join_Attempt() != 0U)
            {
                deviceState = DEV_STATE_OPERATING;
                joinBackoffMs = JOIN_BACKOFF_START_MS;

                /* Doua clipiri lungi pe LED2 = inrolare reusita. */
                Led_PulsePairing();
                __delay_ms(150);
                Led_PulsePairing();

                continue;                     /* prima masuratoare imediat */
            }

            Led_ShowJoinFailure();

            joinAttempts++;

            if (joinAttempts >= PAIRING_MAX_ATTEMPTS)
            {
                /* Fereastra s-a inchis fara raspuns: inapoi in repaus,
                 * LED2 stins. Utilizatorul reia cu inca trei secunde pe
                 * butonul 2, dupa ce da 'pair' pe hub. */
                LED2_LAT    = 0;
                deviceState = DEV_STATE_IDLE;
                continue;
            }

            /* LED2 clipeste cat asteptam: se vede de la distanta ca
             * fereastra este inca deschisa. */
            Wait_PairingBlink(joinBackoffMs);
            LED2_LAT = 0;

            if (joinBackoffMs < JOIN_BACKOFF_MAX_MS)
            {
                joinBackoffMs = (uint16_t)(joinBackoffMs + JOIN_BACKOFF_STEP_MS);
                if (joinBackoffMs > JOIN_BACKOFF_MAX_MS)
                {
                    joinBackoffMs = JOIN_BACKOFF_MAX_MS;
                }
            }

            continue;
        }

        /* =============================================================
         * B. Inrolati: masoara, transmite, asculta fereastra de downlink,
         *    apoi DOARME pana la ciclul urmator.
         * ========================================================== */

        /* --- Masuratoarea ------------------------------------------- */
        tempX100 = NTC_AdcToTempX100(ADC_ReadAveraged());
        Packet_BuildTemperature(tempPacket, tempX100,
                                (forcedByButton != 0U) ? REASON_BUTTON
                                                       : REASON_INTERVAL);

        /* --- Transmisia criptata ------------------------------------ */
        if (loraReady != 0U)
        {
            Packet_BuildDataEnc(tempPacket, frameCounter);

            if (LoRa_SendBuffer(txBuffer, DATA_ENC_LEN) != 0U)
            {
                /* LED1 se aprinde acum si se stinge dupa fereastra de
                 * downlink. Aici NU are voie sa stea un puls blocant:
                 * hub-ul raspunde in zeci de milisecunde de la sfarsitul
                 * transmisiei noastre, iar orice __delay_ms() intre TX si
                 * deschiderea receptiei ne face surzi exact cand vorbeste
                 * el (F-032). */
                LED1_LAT = 1;

                /* --- Fereastra de downlink --------------------------- */
                /* Hub-ul raspunde imediat dupa ce a validat pachetul,
                 * deci fereastra poate fi scurta. */
                rxLength = 0U;
                if (LoRa_Receive(rxBuffer, LORA_RX_BUFFER_LEN, &rxLength,
                                 DOWNLINK_WINDOW_MS, MSG_TYPE_CMD_DOWN) != 0U)
                {
                    command = Packet_ParseCommand(rxBuffer, rxLength);

                    if (command == CMD_TYPE_RESET)
                    {
                        /* Hub-ul ne-a scos din retea: stergem sesiunea si
                         * ne intoarcem in REPAUS, nu direct in pairing.
                         * O dezinrolare este o decizie a hub-ului;
                         * reintrarea in retea ramane o decizie a
                         * utilizatorului, cu aceleasi trei secunde pe
                         * butonul 2. Altfel un senzor tocmai scos ar
                         * incepe imediat sa bata la usa inapoi.
                         * Counter-ul din HEF ramane - nu strica
                         * nimanui, iar la o inrolare noua se porneste
                         * oricum de la zero. */
                        Nvm_EraseSession();
                        deviceState = DEV_STATE_IDLE;
                        Led_ShowJoinFailure();
                        LED2_LAT = 0;
                    }
                    else if (command == CMD_TYPE_ACK)
                    {
                        Led_PulsePairing();
                    }
                    else
                    {
                        /* Pachet strain sau MIC gresit: ignorat. */
                    }
                }

                LED1_LAT = 0;
            }

            /* Counter-ul creste chiar daca transmisia a esuat: o valoare
             * folosita o data nu se mai reutilizeaza niciodata. */
            frameCounter++;
            fcntSinceCheckpoint++;

            if (fcntSinceCheckpoint >= FCNT_CHECKPOINT_EVERY)
            {
                Nvm_SaveFrameCounter(frameCounter);
                fcntSinceCheckpoint = 0UL;
            }
        }

        if (forcedByButton != 0U)
        {
            /* Fara asta, tinerea butonului apasat ar trimite continuu. */
            Button_WaitRelease();
            forcedByButton = 0U;
        }

        /*
         * Daca fereastra de downlink tocmai a adus un CMD_DOWN(RESET),
         * nu mai dormim: ne intoarcem la inceputul buclei, TREJI, in
         * DEV_STATE_IDLE. Un senzor care doarme 30 de secunde ar fi
         * incomod de pus manual in pairing, iar calea normala de
         * re-inrolare este tocmai `remove` pe hub urmat de apasarea
         * butonului pe un senzor deja trezit.
         */
        if (deviceState != DEV_STATE_OPERATING)
        {
            continue;
        }

        /*
         * --- Somnul pana la ciclul urmator --------------------------
         * Abia AICI, dupa ce fereastra de downlink s-a inchis si
         * eventualul CMD_DOWN a fost tratat. Somnul asezat intre
         * transmisie si fereastra ar repeta exact F-032, si ar fi si mai
         * grav: fereastra este singurul moment la ~30 de secunde in care
         * hub-ul poate vorbi cu senzorul.
         *
         * Frame counter-ul ramane in RAM: SLEEP pastreaza RAM-ul si
         * registrele, deci schema din F-022 este neatinsa - checkpoint o
         * data la FCNT_CHECKPOINT_EVERY pachete, nu la fiecare ciclu.
         */
        if (loraReady != 0U)
        {
            LoRa_Sleep();
        }

        wokeBy = Sleep_Cycle();

        if (wokeBy == SLEEP_WOKE_PAIR)
        {
            /*
             * Trei secunde pe butonul 2 la un senzor DEJA inrolat inseamna
             * "vreau sa ma inrolez din nou": stergem sesiunea si deschidem
             * fereastra de pairing. Este comod la mutarea placii pe alt
             * hub, dar inseamna si ca trei secunde de apasare accidentala
             * scot senzorul din retea - de aceea pragul este de trei
             * secunde si nu de o atingere.
             */
            if (ButtonPair_HeldLong() != 0U)
            {
                Nvm_EraseSession();
                deviceState   = DEV_STATE_JOINING;
                joinBackoffMs = JOIN_BACKOFF_START_MS;
                joinAttempts  = 0U;
            }
        }
        else if (wokeBy == SLEEP_WOKE_FORCE)
        {
            /* Butonul 1: transmitem imediat, cu REASON_BUTTON. */
            if (Button_Pressed() != 0U)
            {
                forcedByButton = 1U;
            }
        }
        else
        {
            /* Somn dus pana la capat: urmeaza o transmisie periodica. */
        }
    }

    /* Nu se ajunge aici. */
}
