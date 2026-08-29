/*
 * =====================================================================
 *  SolviX - NOD SENZOR  (varianta FARA CRIPTARE)
 *  PIC16LF1508 @ 16 MHz (INTOSC), MPLAB X + XC8
 * =====================================================================
 *
 *  !!! RETEAUA NU ESTE AUTENTIFICATA !!!
 *  ---------------------------------------------------------------------
 *  Criptografia (XTEA-128 + CBC-MAC + CTR) a fost SCOASA din proiect,
 *  fiindca nu mai incapea in PIC16LF1508: ocupa ~1300 de cuvinte din cele
 *  3968 utilizabile, iar firmware-ul ajunsese la 97,7% ocupare. Este o
 *  masura TEMPORARA, pana la un microcontroller cu mai multa memorie.
 *
 *  Consecinte, care trebuie stiute inainte de a pune sistemul in
 *  exploatare - nu sunt teoretice, sunt la indemana oricui are un radio
 *  LoRa pe 868 MHz cu aceiasi parametri:
 *    - oricine poate injecta o temperatura falsa pentru orice senzor;
 *    - oricine poate dezinrola orice placa cu patru octeti
 *      (A5 13 <DevAddr> 02), iar placa iese din repaus doar cu trei
 *      secunde de buton, fizic, pe teren;
 *    - oricine poate inrola o placa falsa cat timp fereastra de pairing
 *      este deschisa (DevEUI este ghicibil: "SOLVIX" | 0x00 | numar);
 *    - orice pachet capturat poate fi rejucat; singura limitare ramasa
 *      pe calea de date este frame counter-ul strict crescator.
 *  Inrolarea de mai jos este o COMISIONARE (cine e in retea, ce numar
 *  are, de unde incep contoarele), NU un control de acces.
 *
 *  Ultima stare CU criptografie este commit-ul a710142 ("codul 3
 *  senzori"). De acolo se recupereaza cifrul cand se face upgrade-ul.
 *  ---------------------------------------------------------------------
 *
 *  CE FACE:
 *    - la prima pornire sta in REPAUS si tace. Fereastra de inrolare se
 *      deschide tinand butonul 2 (RC5) apasat ~3 secunde (F-030);
 *    - INROLAREA: trimite JOIN_REQ cu DevEUI si asteapta JOIN_ACCEPT, din
 *      care afla DevAddr. Il scrie in memoria ne-volatila (HEF), ca sa
 *      stie peste un reset ca este inrolat;
 *    - dupa inrolare citeste termistorul NTC 10K / B=3950 de pe RC2
 *      (canal AN6) si trimite temperatura IN CLAR (DATA_UP) la intervalul
 *      de somn (vezi SLEEP_WAKEUPS_BASE), sau imediat la apasarea
 *      butonului de pe RC4;
 *    - dupa fiecare transmisie deschide o fereastra scurta de receptie
 *      pentru un eventual CMD_DOWN (ACK sau RESET).
 *
 *  LED-uri:
 *    - LED 1 (RC3) = transmisie de date (aprins cat tine si fereastra de
 *      downlink - F-032);
 *    - LED 2 (RC6) = mod pairing / eroare de join.
 *
 *  CE S-A PASTRAT NEATINS din firmware-ul de temperatura:
 *    driverul LoRa de emisie, ADC-ul, tabelul NTC, debounce-ul de buton
 *    si pachetul de temperatura de 6 octeti. Pachetul de 6 octeti NU a
 *    fost modificat: el este exact ce se transporta in DATA_UP, iar hub-ul
 *    il da neschimbat aceluiasi SensorPacketCodec::decode() care exista
 *    deja. Asa raman testul 7 (TEMP_PLAIN) si testul 8 pe acelasi cod.
 *
 *  FORMATUL PACHETELOR este oglindit pe hub in
 *  hub/SolvixHub_Tests/SensorPacket.h.
 *  Orice modificare aici trebuie facuta si acolo, in ACELASI commit:
 *  fara MIC, o nepotrivire de format nu mai da "MIC gresit", ci o
 *  temperatura plauzibila si gresita, in tacere. Cele cinci lungimi de
 *  pachet (6/10/3/13/4) sunt DISTINCTE tocmai ca verificarea de lungime
 *  sa prinda orice desincronizare; nu le egaliza.
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
 *  9) DevEUI se provizioneaza per unitate din SENSOR_NODE_ID de mai jos:
 *     la prima pornire, daca regiunea de provisioning din HEF e goala,
 *     valoarea de compilare este scrisa acolo. FIECARE placa se compileaza
 *     cu alt SENSOR_NODE_ID, iar acelasi DevEUI trebuie sa apara, pe
 *     aceeasi pozitie, in PROVISIONED_DEVICES_INIT din
 *     hub/SolvixHub_Tests/Config.h: pozitia din tabel este numarul
 *     senzorului (DeviceRegistry::addressForEui, F-037).
 *
 *  CONFIGURATION BITS: sunt cele generate de MCC in
 *  mcc_generated_files/system/src/config_bits.c
 *  (FOSC=INTOSC, WDTE=SWDTEN, MCLRE=ON, BOREN=ON, LVP=ON, PWRTE=OFF).
 *  WDTE=SWDTEN tine watchdog-ul stins in veghe - transmisia, fereastra de
 *  receptie si scrierea in HEF sunt lungi si blocante - si il aprinde doar
 *  in jurul lui SLEEP, unde expirarea TREZESTE procesorul (F-034).
 *  IMPORTANT pentru HEF: WRT=OFF, adica memoria de program NU este
 *  protejata la scriere. Daca cineva pune WRT pe altceva, scrierile in
 *  HEF esueaza in tacere si senzorul reia pairing-ul la fiecare pornire.
 *  Fisierul e generat de MCC: o regenerare pune WDTE inapoi pe OFF si
 *  senzorul nu se mai trezeste din somn.
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
 * DATA_UP de 13 octeti sta pe aer ~41 ms, iar un senzor emite o data la
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

/* Fereastra de receptie deschisa dupa fiecare DATA_UP, pentru un
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
 * cele cinci placi: din el iese DevEUI, prin blocul de mai jos.
 *
 * Acelasi numar il primeste placa si ca DevAddr de la hub. Hub-ul NU
 * aloca prima adresa libera, ci POZITIA din tabelul de provisioning din
 * hub/SolvixHub_Tests/Config.h (DeviceRegistry::addressForEui). Senzorul
 * cu SENSOR_NODE_ID = 3 este deci intotdeauna "Senzor #3" in jurnalul
 * hub-ului, indiferent in ce ordine s-au inrolat placile si indiferent
 * de cate ori s-a golit registrul. Din DevAddr iese si slotul de somn de
 * mai sus, deci numarul chiar face doua treburi, nu este o eticheta.
 *
 * DE VERIFICAT LA PROGRAMARE: numarul de aici trebuie sa fie acelasi cu
 * pozitia DevEUI-ului in PROVISIONED_DEVICES_INIT. Cat timp exista MIC,
 * o nepotrivire dadea un sir de "MIC gresit" pe hub. Acum simptomul este
 * altul: JOIN_ACCEPT-ul vine cu alta adresa decat cea asteptata, senzorul
 * il refuza si LED2 da trei clipiri (vezi Join_Attempt). Doua placi
 * programate cu ACELASI numar nu mai sunt insa deosebite de hub - se vad
 * ca un singur senzor al carui contor merge inainte si inapoi.
 */
#define SENSOR_NODE_ID          3

#if (SENSOR_NODE_ID < 1) || (SENSOR_NODE_ID > 5)
#error "SENSOR_NODE_ID trebuie sa fie intre 1 si 5 (vezi HUB_MAX_SENSORS)."
#endif

/*
 * DevEUI, 8 octeti: "SOLVIX" in ASCII, apoi 0x00 si numarul placii.
 * PIC16LF1508 nu are un ID unic garantat, deci identitatea se
 * provizioneaza aici si se scrie in HEF la prima pornire.
 */
#define PROVISION_DEV_EUI       { 0x53U, 0x4FU, 0x4CU, 0x56U, \
                                  0x49U, 0x58U, 0x00U,        \
                                  (uint8_t)SENSOR_NODE_ID }


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

/*
 * Cel mai lung pachet pe care il PRIMESTE senzorul este acum CMD_DOWN,
 * cu 4 octeti (JOIN_ACCEPT are 3). Valoarea de aici ajunge in
 * RegMaxPayloadLength, deci modemul arunca SINGUR, in hardware, orice
 * pachet mai lung - adica DATA_UP (13) si JOIN_REQ (10) ale celorlalte
 * placi, inainte ca firmware-ul sa le vada.
 *
 * De ce 6 si nu 4: doi octeti de rezerva, ca un pachet abia mai lung sa
 * fie citit si respins de filtrul software, nu taiat de modem.
 *
 * ATENTIE LA RECALIBRARE. Cat timp DATA_ENC avea 17 octeti si limita era
 * 16, filtrul hardware exista din intamplare. Odata cu scurtarea
 * pachetelor el ar fi disparut in tacere, si odata cu el jumatate din
 * apararea ferestrei de downlink (F-035) - exact mecanismul de care
 * depinde dezinrolarea cu cinci placi. Cine mai schimba lungimile de
 * pachet trebuie sa reia calculul de aici.
 */
#define LORA_RX_BUFFER_LEN            6U

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
 *  NIMIC NU MAI ESTE SEMNAT SAU CIFRAT. Nu exista MIC, nu exista cheie,
 *  nu exista nonce. Vezi avertismentul din antetul fisierului.
 *
 *  CELE CINCI LUNGIMI SUNT DISTINCTE - 6 / 10 / 3 / 13 / 4 - si trebuie
 *  sa ramana asa. Este singura verificare ramasa impotriva unei
 *  desincronizari intre cele doua capete: perechea tip+lungime respinge
 *  un pachet de format vechi, in loc sa-l interpreteze la offset-uri
 *  gresite si sa scoata o temperatura plauzibila si gresita, in tacere.
 *
 *  TYPE 0x01 - TEMP_PLAIN, pachetul vechi de 6 octeti (NESCHIMBAT):
 *    [0] 0xA5  [1] 0x01  [2..3] temp*100  [4] motiv
 *    [5] checksum = XOR(0..4) ^ 0x5A
 *
 *  TYPE 0x10 - JOIN_REQ (senzor -> hub), 10 octeti:
 *    [0] 0xA5  [1] 0x10  [2..9] DevEUI
 *
 *  DevEUI ramane pe fir desi hub-ul stie oricum ce numere exista: el este
 *  cheia dupa care hub-ul verifica ca placa are voie, deriva numarul din
 *  POZITIA in tabelul de provisioning (F-037) si o identifica in
 *  "remove <DevEUI>". Daca JOIN_REQ ar purta doar numarul, senzorul si-ar
 *  declara singur adresa - exact ce a reparat F-037.
 *
 *  TYPE 0x11 - JOIN_ACCEPT (hub -> senzor), 3 octeti:
 *    [0] 0xA5  [1] 0x11  [2] DevAddr
 *
 *  DevAddr circula acum IN CLAR, si asta inchide ce F-035 lasase
 *  dinadins netratat: senzorul poate filtra fereastra de join pe adresa,
 *  fiindca stie de la compilare ce numar asteapta (SENSOR_NODE_ID). Doi
 *  senzori care se inroleaza in aceeasi secunda nu-si mai fura fereastra.
 *
 *  TYPE 0x12 - DATA_UP (senzor -> hub), 13 octeti:
 *    [0] 0xA5  [1] 0x12  [2] DevAddr  [3..6] FrameCounter
 *    [7..12] pachetul TEMP de 6 octeti, IN CLAR
 *
 *  Numele nu mai este DATA_ENC: nu mai exista niciun "Enc". Valoarea
 *  tipului ramane 0x12 - un pachet de firmware vechi are 17 octeti si
 *  cade oricum la verificarea de lungime, ceea ce este simptomul util.
 *  FrameCounter-ul ramane si devine singura aparare a caii de date; tot
 *  el da hub-ului pachetele pierdute si detectia de repornire (F-036).
 *
 *  TYPE 0x13 - CMD_DOWN (hub -> senzor), 4 octeti:
 *    [0] 0xA5  [1] 0x13  [2] DevAddr  [3] CmdType (0x01 = ACK, 0x02 = RESET)
 *
 *  Contorul downlink a fost scos de pe fir. Fara MIC nu apara nimic - un
 *  atacator nu are nevoie sa REIA un CMD_DOWN capturat, il fabrica din
 *  patru octeti constanti - iar senzorul nu l-a citit niciodata. Pe hub,
 *  downCounter ramane ca statistica locala in jurnal.
 *
 *  De ce ramane checksum-ul XOR in interiorul celor 6 octeti: pachetul
 *  ramane BIT CU BIT cel vechi, deci hub-ul il da neschimbat lui
 *  SensorPacketCodec::decode(). Nicio logica noua de parsare a
 *  temperaturii, pe niciunul din capete. Nu este apararea de integritate
 *  a pachetului - aceea este CRC-ul LoRa, activ pe ambele capete, care
 *  acopera TOT payload-ul, inclusiv adresa si contorul.
 * ================================================================== */
#define LORA_PACKET_MAGIC             0xA5U
#define CHECKSUM_SALT                 0x5AU

#define MSG_TYPE_TEMPERATURE          0x01U   /* TEMP_PLAIN */
#define MSG_TYPE_JOIN_REQ             0x10U
#define MSG_TYPE_JOIN_ACCEPT          0x11U
#define MSG_TYPE_DATA_UP              0x12U
#define MSG_TYPE_CMD_DOWN             0x13U

#define LORA_PACKET_LEN               6U      /* TEMP_PLAIN, neschimbat */
#define JOIN_REQ_LEN                  10U
#define JOIN_ACCEPT_LEN               3U
#define DATA_UP_LEN                   13U
#define CMD_DOWN_LEN                  4U

/* Cel mai lung pachet pe care il EMITE senzorul. */
#define TX_BUFFER_LEN                 DATA_UP_LEN

#define DEV_EUI_LEN                   8U

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
 *  adica DevEUI-ul odata cu ce mai statea in acelasi bloc.
 *
 *  In 128 de cuvinte incap deci EXACT 4 randuri, si asta a impus harta:
 *
 *  HARTA (4 randuri x 32 de cuvinte):
 *    rand 0  0x0F80  identitate : MAGIC(1) + DevEUI(8)
 *    rand 1  0x0FA0  sesiune    : MAGIC(1) + DevAddr(1)
 *    rand 2  0x0FC0  counter, slotul 0 : MAGIC(1) + FrameCounter(4)
 *    rand 3  0x0FE0  counter, slotul 1 : MAGIC(1) + FrameCounter(4)
 *
 *  Randul de sesiune s-a golit odata cu criptografia: nu mai exista nici
 *  SessKey, nici nonce-uri. Ce a ramas este exact bitul care conteaza -
 *  PREZENTA marcajului inseamna "sunt inrolat, am voie sa vorbesc", iar
 *  DevAddr il insoteste fiindca vine de la hub si fiindca din el iese
 *  slotul de somn.
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
#define HEF_OFF_DEV_ADDR        1U

/* Marcaje care spun ca o regiune a fost scrisa. Flash-ul sters citeste
 * 0xFF, deci orice valoare diferita de 0xFF merge ca marcaj.
 *
 * HEF_MAGIC_SESSION A FOST SCHIMBAT de la 0xC3 la 0xC4 odata cu
 * scoaterea criptografiei, si nu este o toaleta cosmetica. Randul de
 * sesiune vechi incepea cu 0xC3 urmat de DevAddr - adica EXACT formatul
 * nou, octet cu octet. Cu marcajul neschimbat, firmware-ul acesta ar fi
 * citit o sesiune veche ca valida si ar fi inceput sa emita catre un hub
 * al carui registru tocmai fusese golit de REGISTRY_BLOB_VERSION = 4:
 * cinci placi blocate in "DevAddr ... nu este inrolat", fiecare
 * recuperabila doar cu trei secunde de buton, pe teren. Cu marcajul
 * schimbat, ambele capete pornesc golite in acelasi commit si
 * recuperarea este cea normala: `pair` pe hub plus butonul 2. */
#define HEF_MAGIC_PROV          0xA7U
#define HEF_MAGIC_SESSION       0xC4U
#define HEF_MAGIC_FCNT          0xC5U

/*
 * Bufferul in care se pregateste un rand inainte de scriere.
 *
 * NU are toate cele 32 de cuvinte ale randului, ci doar atatea cate
 * folosim efectiv: cel mai plin rand este cel de identitate, cu
 * MAGIC(1) + DevEUI(8) = 9 octeti. Restul latch-urilor randului primesc
 * direct 0xFF in HEF_WriteRow, fara sa mai treaca prin RAM. Pe un device
 * cu 256 de octeti de RAM, octetii economisiti aici chiar conteaza
 * (F-025).
 */
#define HEF_ROW_BUFFER_LEN      (1U + DEV_EUI_LEN)                    /*  9 */

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
 * 6. CUVANTUL DE 32 DE BITI ACCESIBIL PE OCTETI
 * ---------------------------------------------------------------------
 *  Aici statea cifrul: XTEA-128, cu CBC-MAC pentru MIC si CTR pentru
 *  criptare (F-024). A fost scos - vezi avertismentul din antetul
 *  fisierului. Ultima versiune care il contine este commit-ul a710142.
 *
 *  Uniunea de mai jos NU a plecat cu el, desi fusese introdusa pentru
 *  cifru. O folosesc Nvm_LoadFrameCounter, Nvm_SaveFrameCounter si
 *  Packet_BuildDataUp - adica frame counter-ul, care nu are nicio
 *  legatura cu criptografia. Stearsa din reflex odata cu restul
 *  sectiunii, cele trei functii ar fi rescrise "cu shift-uri" si ar
 *  reintroduce ~300 de cuvinte de program (F-028): o cincime din tot ce
 *  s-a castigat scotand cifrul, pierduta fara ca nimeni sa observe,
 *  fiindca marja este acum mare si nimic nu mai doare.
 *
 *  SECTIUNEA ISI PASTREAZA NUMARUL 6 desi si-a schimbat continutul.
 *  Renumerotarea celor 16 sectiuni ar invalida fiecare referinta
 *  "sectiunea N" din CLAUDE.md si din comentariile de mai jos.
 * ================================================================== */

/*
 * PIC16 nu are decat un acumulator de 8 biti: fiecare deplasare a unui
 * uint32 cu un numar de pozitii devine o bucla din biblioteca XC8 si
 * costa zeci de cuvinte de program. Impachetarea si despachetarea
 * big-endian scrise "cu shift-uri" costau singure peste 300 de cuvinte,
 * pe un device care are 4096 in total.
 *
 * XC8 stocheaza intregii little-endian, deci octetul cel mai
 * semnificativ este byte[3]. Toate conversiile sunt simple mutari de
 * octeti, fara nicio deplasare.
 */
typedef union
{
    uint32_t word;
    uint8_t  byte[4];       /* byte[0] = cel mai putin semnificativ */
} Word32;

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
static uint8_t  devAddr = SENSOR_NODE_ID;

/*
 * devAddr porneste cu SENSOR_NODE_ID, nu cu 0, si ramane asa si dupa o
 * dezinrolare. Nu este o presupunere despre ce va spune hub-ul, ci
 * numarul pe care aceasta placa il ASTEAPTA: hub-ul il deriva din
 * pozitia DevEUI-ului in tabelul de provisioning (F-037), deci cele doua
 * trebuie sa coincida. Valoarea este folosita ca filtru in fereastra de
 * receptie inca din starea JOINING - vezi LoRa_Receive.
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

/* =====================================================================
 * 8. CITIREA SI SCRIEREA STARII IN HEF
 * ================================================================== */

/*
 * Identitatea: DevEUI, singur in randul lui. Daca randul este gol
 * (marcaj lipsa), se scrie valoarea de compilare din PROVISION_DEV_EUI.
 * Asa o placa noua se auto-provizioneaza la prima pornire.
 *
 * DACA RANDUL EXISTA DAR CONTINE ALT DevEUI, se rescrie. Cazul apare
 * exact cand o placa deja folosita este reprogramata cu alt
 * SENSOR_NODE_ID - de exemplu fiindca senzorul #2 s-a ars si i se ia
 * locul cu o placa de rezerva. Fara verificarea asta, HEF-ul ar pastra
 * identitatea VECHE si placa ar continua sa se prezinte cu numarul
 * vechi, in timp ce firmware-ul de pe ea spune altceva, iar nicio
 * cautare in cod nu ar duce nicaieri, fiindca sursa este corecta. Este
 * acelasi gen de capcana ca F-033, dar cu starea ne-volatila in loc de
 * directorul de build.
 *
 * VERIFICAREA ASTA A DEVENIT MAI IMPORTANTA decat era. Cat timp exista
 * MIC, o identitate desincronizata se vedea si pe hub, ca un sir de
 * "MIC gresit". Acum acel al doilea simptom nu mai exista, deci
 * rescrierea de aici este singurul lucru care mai prinde cazul.
 *
 * Odata cu identitatea se sterge si SESIUNEA: dreptul de a vorbi a fost
 * dat identitatii vechi, deci nu mai are ce cauta acolo. Placa porneste
 * in DEV_STATE_IDLE si asteapta o inrolare noua, ceea ce si trebuie.
 *
 * Verificarea nu costa o scriere in plus la fiecare pornire: se compara
 * doar, si se scrie exclusiv cand chiar difera.
 *
 * DevEUI ajunge in RAM, fiindca intra in fiecare JOIN_REQ.
 */
static void Nvm_LoadOrCreateProvisioning(void)
{
    static const uint8_t defaultEui[DEV_EUI_LEN] = PROVISION_DEV_EUI;

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

        /* Placa a fost reprogramata cu alt SENSOR_NODE_ID: inrolarea
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
    HEF_WriteRow(HEF_ROW_IDENTITY);
}

/*
 * Intoarce 1 daca in HEF exista o inrolare valida, si incarca DevAddr.
 *
 * Randul de sesiune s-a golit odata cu criptografia: ce a mai ramas este
 * PREZENTA marcajului, adica bitul "sunt inrolat, am voie sa vorbesc",
 * plus numarul primit de la hub.
 */
static uint8_t Nvm_LoadSession(void)
{
    if (HEF_ReadByte(HEF_ROW_SESSION) != HEF_MAGIC_SESSION)
    {
        return 0U;
    }

    devAddr = HEF_ReadByte((uint16_t)(HEF_ROW_SESSION + HEF_OFF_DEV_ADDR));

    /* Un DevAddr de 0x00 sau 0xFF inseamna rand corupt sau nescris. */
    if ((devAddr == 0x00U) || (devAddr == 0xFFU))
    {
        devAddr = SENSOR_NODE_ID;
        return 0U;
    }

    return 1U;
}

/* Inrolarea incape intr-un singur rand: o stergere + o scriere. */
static void Nvm_SaveSession(void)
{
    HEF_ClearRowBuffer();
    hefRowBuffer[0] = HEF_MAGIC_SESSION;
    hefRowBuffer[HEF_OFF_DEV_ADDR] = devAddr;
    HEF_WriteRow(HEF_ROW_SESSION);
}

/*
 * Sterge inrolarea: senzorul redevine ne-inrolat (comanda RESET).
 *
 * devAddr NU se pune pe 0, ci inapoi pe SENSOR_NODE_ID: este numarul pe
 * care placa il asteapta oricum de la hub, si ramane filtrul ferestrei
 * de receptie in starea JOINING (vezi LoRa_Receive).
 */
static void Nvm_EraseSession(void)
{
    HEF_EraseRow(HEF_ROW_SESSION);
    devAddr = SENSOR_NODE_ID;
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
 * ACEASTA FUNCTIE ESTE SINGURUL PUNCT DE VALIDARE A RECEPTIEI. Odata cu
 * criptografia au disparut si Packet_ParseJoinAccept si
 * Packet_ParseCommand, care nu mai aveau ce verifica in afara de magic,
 * tip, lungime si adresa - adica fix ce se face aici, dar mai devreme.
 * Cine muta validarea inapoi "unde ii e locul" reintroduce F-035: ca sa
 * apere fereastra, verificarea trebuie facuta INAINTE de a o inchide.
 *
 * FILTRUL ESTE PARTE DIN FUNCTIONALITATE, NU O OPTIMIZARE. Cu
 * HUB_MAX_SENSORS = 5 placi pe acelasi canal, in fereastra de 600 ms a
 * senzorului A poate intra la fel de bine un CMD_DOWN adresat lui B:
 * fara filtru, functia s-ar intoarce cu ACEL pachet, apelantul l-ar
 * respinge si fereastra s-ar fi INCHIS DEJA. Senzorul A si-ar rata
 * propriul raspuns, iar pe calea de dezinrolare asta inseamna exact
 * fundatura din F-031: un RESET are o singura sansa per ciclu. Pachetele
 * straine sunt deci aruncate SI receptia continua cu timpul ramas, exact
 * ca la un CRC gresit.
 *
 * Se verifica magic-ul, tipul, LUNGIMEA EXACTA si DevAddr. Adresa se
 * verifica acum pentru AMBELE tipuri primite, nu doar pentru CMD_DOWN:
 * de cand JOIN_ACCEPT nu mai este cifrat, DevAddr circula in clar si
 * acolo, iar senzorul stie de la compilare ce numar asteapta
 * (SENSOR_NODE_ID). Doi senzori care se inroleaza in aceeasi secunda nu
 * isi mai fura fereastra - lucrul pe care F-035 il lasase netratat
 * tocmai fiindca adresa era cifrata.
 *
 * Verificarea de lungime tine loc si de diagnostic pentru capetele
 * desincronizate: un pachet de firmware vechi are alta lungime si cade
 * aici, in loc sa fie citit la offset-uri gresite.
 *
 * Al doilea filtru, gratuit, este hardware: RegMaxPayloadLength =
 * LORA_RX_BUFFER_LEN (sectiunea 3). Cel mai lung pachet pe care il
 * PRIMESTE senzorul este CMD_DOWN, cu 4 octeti, deci modemul arunca
 * singur si DATA_UP (13), si JOIN_REQ (10) ale celorlalte placi, inainte
 * sa ajunga la noi.
 *
 * Intoarce 1 daca s-a primit un pachet cu CRC bun, de tipul si lungimea
 * cerute, adresat noua.
 */
static uint8_t LoRa_Receive(uint8_t *buffer, uint8_t maxLength,
                            uint8_t *length, uint16_t timeoutMs,
                            uint8_t wantType, uint8_t wantLen)
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
            if ((received != wantLen) ||
                (buffer[0] != LORA_PACKET_MAGIC) ||
                (buffer[1] != wantType) ||
                (buffer[2] != devAddr))
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
     * de pairing, si un sir de DATA_UP pe acelasi canal.
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
 * payload-ul pe care il transporta DATA_UP. */
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
 * JOIN_REQ: 10 octeti, in clar.
 *
 * Nu mai exista DevNonce: singurele lui roluri erau criptografice
 * (anti-replay pe JOIN_REQ, intrare in derivarea cheii, IV pentru
 * JOIN_ACCEPT). Un nonce care apara impotriva reluarii unui mesaj pe
 * care oricine il poate FABRICA nu apara nimic.
 */
static void Packet_BuildJoinRequest(void)
{
    uint8_t i;

    txBuffer[0] = LORA_PACKET_MAGIC;
    txBuffer[1] = MSG_TYPE_JOIN_REQ;

    for (i = 0U; i < DEV_EUI_LEN; i++)
    {
        txBuffer[2U + i] = devEui[i];
    }
}

/*
 * DATA_UP: 13 octeti. Antet, adresa, frame counter, apoi pachetul de
 * temperatura de 6 octeti IN CLAR - bit cu bit cel vechi, ca hub-ul sa-l
 * dea neschimbat lui SensorPacketCodec::decode().
 *
 * Frame counter-ul se impacheteaza big-endian prin uniunea Word32, nu cu
 * deplasari pe 32 de biti: pe PIC16 fiecare shift de uint32 este o bucla
 * din biblioteca XC8 (F-028).
 */
static void Packet_BuildDataUp(const uint8_t *tempPacket, uint32_t counter)
{
    Word32  packed;
    uint8_t i;

    packed.word = counter;

    txBuffer[0] = LORA_PACKET_MAGIC;
    txBuffer[1] = MSG_TYPE_DATA_UP;
    txBuffer[2] = devAddr;
    txBuffer[3] = packed.byte[3];       /* FrameCounter big-endian */
    txBuffer[4] = packed.byte[2];
    txBuffer[5] = packed.byte[1];
    txBuffer[6] = packed.byte[0];

    for (i = 0U; i < LORA_PACKET_LEN; i++)
    {
        txBuffer[7U + i] = tempPacket[i];
    }
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
 * Nu are nicio pretentie criptografica si nu are voie sa capete una -
 * cu atat mai putin acum, cand nu mai exista nimic criptografic in
 * firmware caruia sa i se para o sursa de entropie. Scopul este strict
 * sa imprastie momentul
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
 * O singura incercare de join: JOIN_REQ -> RX -> JOIN_ACCEPT. La succes
 * scrie inrolarea in HEF si intoarce 1.
 *
 * Nu mai exista nici nonce, nici derivare de cheie. Toata verificarea
 * raspunsului - magic, tip, lungime si adresa - se face in LoRa_Receive,
 * inca dinainte de inchiderea ferestrei, deci aici nu mai ramane nimic
 * de parsat: rxBuffer[2] este numarul confirmat de hub.
 *
 * Filtrul pe adresa din LoRa_Receive foloseste devAddr, care in starea
 * JOINING valoreaza SENSOR_NODE_ID. Asta face din fereastra de join si o
 * VERIFICARE INCRUCISATA: daca placa a fost programata cu un numar care
 * nu corespunde pozitiei ei din PROVISIONED_DEVICES_INIT, hub-ul
 * raspunde cu alta adresa, pachetul este aruncat, incercarea esueaza si
 * LED2 da trei clipiri. Este diagnosticul care inlocuieste sirul de "MIC
 * gresit" de dinainte (F-037).
 */
static uint8_t Join_Attempt(void)
{
    uint8_t length = 0U;

    Packet_BuildJoinRequest();

    LED2_LAT = 1;                       /* LED2 aprins = incercam sa ne inrolam */

    if (LoRa_SendBuffer(txBuffer, JOIN_REQ_LEN) == 0U)
    {
        LED2_LAT = 0;
        return 0U;
    }

    if (LoRa_Receive(rxBuffer, LORA_RX_BUFFER_LEN, &length,
                     JOIN_RX_TIMEOUT_MS,
                     MSG_TYPE_JOIN_ACCEPT, JOIN_ACCEPT_LEN) == 0U)
    {
        LED2_LAT = 0;
        return 0U;
    }

    devAddr = rxBuffer[2];

    /* Inrolare noua inseamna si counter de la zero: hub-ul reseteaza
     * lastFrameCounterUp la inrolare, deci cele doua capete pornesc de
     * la aceeasi valoare. */
    frameCounter = 0UL;
    fcntSinceCheckpoint = 0U;

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

        /* --- Transmisia --------------------------------------------- */
        if (loraReady != 0U)
        {
            Packet_BuildDataUp(tempPacket, frameCounter);

            if (LoRa_SendBuffer(txBuffer, DATA_UP_LEN) != 0U)
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
                                 DOWNLINK_WINDOW_MS,
                                 MSG_TYPE_CMD_DOWN, CMD_DOWN_LEN) != 0U)
                {
                    /* LoRa_Receive a validat deja magic, tip, lungime si
                     * adresa, deci ce ramane este chiar comanda. */
                    command = rxBuffer[3];

                    if (command == CMD_TYPE_RESET)
                    {
                        /* Hub-ul ne-a scos din retea: stergem inrolarea si
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
                        /* Comanda necunoscuta: ignorata. */
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
