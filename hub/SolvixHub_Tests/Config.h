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
// datele de temperatura circula pe exact aceeasi modulatie ca pachetul de
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

// 1 = dupa fiecare DATA_UP valid, hub-ul trimite un CMD_DOWN de tip ACK.
//     Senzorul deschide oricum o fereastra de receptie dupa transmisie,
//     deci nu costa nimic in plus la el; costa insa timp de emisie pe
//     hub. Se poate lasa pe 0 fara nicio consecinta functionala.
//
// Cu mai multi senzori merita stiut ce inseamna: cat timp hub-ul emite
// un ACK (12 octeti, ~41 ms pe aer la SF7/BW125/CR4-5) el este SURD.
// Cu 5 senzori care emit fiecare o data la ~30 s, hub-ul sta in emisie
// 5 x 41 ms la 30 s, adica 0,7% din timp - probabilitatea ca exact
// atunci sa vorbeasca altcineva este neglijabila, si oricum ar fi o
// singura masuratoare pierduta. ACK-ul ramane deci pornit; daca vreodata
// numarul de senzori creste mult, aici se taie primul.
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
// Senzorul nu este in veghe continua: intre doua pachete doarme, iar un
// senzor adormit TACE - exact semnalul pe care hub-ul il foloseste ca
// dovada ca a primit RESET-ul.
//
// Odata cu trecerea la mai multi senzori, somnul nu mai are aceeasi
// durata pe toate placile (vezi SLEEP_WAKEUPS_BASE / SLEEP_SLOT_MASK /
// SLEEP_JITTER_MASK in senzor/main.c). Fiecare ciclu dureaza
//     (11 + (DevAddr-1) + jitter 0..3) x ~2,11 s,
// deci intre ~23 s pentru senzorul #1 fara jitter si ~38 s pentru
// senzorul #5 cu jitter maxim. Peste asta se aduna toleranta LFINTOSC,
// de ordinul a +-15%: cel mai lung ciclu REAL este de ~44 s.
//
// Fereastra trebuie sa acopere confortabil mai multe astfel de cicluri:
// 180 s inseamna patru cicluri in cazul cel mai lent si peste sapte
// pentru senzorul #1. Prea mica este PERICULOS, nu doar incomod: hub-ul
// ar declara dezinrolarea confirmata in timp ce senzorul doar doarme, ar
// sterge inregistrarea, iar la trezire senzorul ar
// emite cu cheia veche fara ca hub-ul sa-l mai poata opri vreodata -
// fundatura din F-031, de data asta fara iesire. Prea mare doar
// intarzie reinrolarea.
//
// Daca schimbi constantele de somn ale senzorului, sau daca cresti
// HUB_MAX_SENSORS (ultimul senzor primeste automat cel mai lung
// interval!), schimbi si valoarea de aici - regula 11 din CLAUDE.md,
// sectiunea 10.
#define REMOVE_CONFIRM_SILENCE_MS 180000UL

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

// ---------------------------------------------------------------------
// CATI SENZORI, SI CUM SE NUMEROTEAZA
// ---------------------------------------------------------------------
// Numarul maxim de senzori din retea. Este si limita registrului, si
// domeniul de adrese pe care hub-ul le aloca: DevAddr merge de la 1 la
// HUB_MAX_SENSORS.
//
// **DevAddr NU MAI ESTE "prima adresa libera".** Este POZITIA senzorului
// in tabelul PROVISIONED_DEVICES_INIT de mai jos, plus unu. Consecinta,
// care este tot rostul schimbarii: senzorul de pe randul 3 al tabelului
// este "Senzor #3" de fiecare data, indiferent in ce ordine s-au inrolat
// placile, indiferent de cate ori a fost dezinrolat si reinrolat, si
// indiferent daca registrul din NVS a fost golit intre timp. Numarul
// este o proprietate a placii, nu un accident al ordinii de pornire.
//
// Acelasi numar se scrie si pe senzor, ca SENSOR_NODE_ID in
// senzor/main.c: el determina acolo DevEUI si slotul de somn.
// Cele doua trebuie sa corespunda - randul N din tabel <-> placa cu
// SENSOR_NODE_ID = N.
//
// Daca se creste peste 5: se adauga randuri in PROVISIONED_DEVICES_INIT,
// se ridica limita lui SENSOR_NODE_ID din senzor/main.c, si se
// recalculeaza REMOVE_CONFIRM_SILENCE_MS - senzorul cu adresa cea mai
// mare are automat cel mai lung interval de somn.
#define HUB_MAX_SENSORS           5

// Registrul are exact atatea locuri cati senzori pot exista. Fiecare
// inregistrare are cateva zeci de octeti, deci limita vine din
// numerotare, nu din memorie.
#define REGISTRY_MAX_DEVICES      HUB_MAX_SENSORS

// ---------------------------------------------------------------------
// SUPRAVEGHEREA SENZORILOR TACUTI
// ---------------------------------------------------------------------
// Dupa cat timp fara niciun pachet valid este anuntat un senzor ca
// "nu se mai aude". Cu un ciclu de somn de cel mult ~44 s in cazul cel
// mai lent (vezi REMOVE_CONFIRM_SILENCE_MS), 150 s inseamna trei-patru
// masuratori ratate la rand - destul ca sa nu dea alarme false la o
// singura coliziune, si destul de putin cat operatorul sa afle repede ca
// o placa a cazut.
//
// Anuntul se face O SINGURA DATA la trecerea in tacere si o singura data
// la revenire; altfel Serial-ul s-ar umple cu aceeasi linie.
#define SENSOR_OFFLINE_MS         150000UL

// De la ce salt in frame counter hub-ul spune "senzorul a repornit" in
// loc de "s-au pierdut N pachete".
//
// Golurile mici din numerotare chiar sunt pachete pierdute: senzorul isi
// incrementeaza contorul la fiecare transmisie, deci 41 -> 44 inseamna
// doua pachete care nu au ajuns. Un salt MARE inseamna insa altceva: la
// fiecare pornire la rece senzorul sare inainte cu FCNT_CHECKPOINT_EVERY
// (50) tocmai ca sa nu reutilizeze o valoare deja emisa (senzor/main.c,
// sectiunea 16). Numarate ca pierderi, cele 50 ar strica exact cifra
// dupa care se judeca daca senzorii se ciocnesc intre ei.
//
// 20 este pragul: douazeci de pachete pierdute la rand inseamna zece
// minute de tacere continua, iar la atata SENSOR_OFFLINE_MS ar fi
// raportat deja senzorul ca disparut. Peste prag, explicatia de departe
// cea mai probabila este o repornire - si aceea merita spusa, nu
// numarata.
#define SENSOR_FCNT_GAP_RESTART   20UL

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
// LISTA DE PROVISIONING: DevEUI-urile care au voie in retea
// ---------------------------------------------------------------------
// Numai senzorii de aici se pot inrola. AppKey-urile au disparut odata
// cu criptografia; ce a ramas este lista de identitati admise si -
// mai important - ORDINEA lor.
//
// **ORDINEA RANDURILOR ESTE SEMNIFICATIVA.** Pozitia din tabel, plus
// unu, este DevAddr-ul pe care hub-ul il da la inrolare, adica numarul
// sub care apare senzorul peste tot in jurnal si in comenzi.
// Randul 1 <-> Senzor #1 <-> placa programata cu SENSOR_NODE_ID = 1.
// Nu se rearanjeaza randurile intr-o retea deja instalata: senzorii si-ar
// schimba numerele intre ei.
//
// Fiecare rand trebuie sa corespunda EXACT cu PROVISION_DEV_EUI din
// senzor/main.c al placii respective - adica, in practica, cu
// SENSOR_NODE_ID, din care iese DevEUI-ul.
//
// CE SE INTAMPLA DACA NU CORESPUND: pana acum, o placa programata cu un
// numar care nu se potrivea cu randul ei era respinsa cu "MIC gresit".
// Acum simptomul este altul si se vede pe SENZOR, nu pe hub: hub-ul
// raspunde cu numarul din tabel, placa asteapta numarul ei compilat,
// nu se potrivesc, JOIN_ACCEPT-ul este aruncat si LED2 da trei clipiri.
//
// Tabelul este instantiat in DeviceRegistry.cpp; aici stau doar valorile,
// ca sa ramana adevarata regula "constantele traiesc in Config.h".
#define PROVISIONED_DEVICES_INIT {   /* #1 */ { { 0x53, 0x4F, 0x4C, 0x56, 0x49, 0x58, 0x00, 0x01 } },            /* #2 */ { { 0x53, 0x4F, 0x4C, 0x56, 0x49, 0x58, 0x00, 0x02 } },            /* #3 */ { { 0x53, 0x4F, 0x4C, 0x56, 0x49, 0x58, 0x00, 0x03 } },            /* #4 */ { { 0x53, 0x4F, 0x4C, 0x56, 0x49, 0x58, 0x00, 0x04 } },            /* #5 */ { { 0x53, 0x4F, 0x4C, 0x56, 0x49, 0x58, 0x00, 0x05 } },          }


// =====================================================================
// RETEAUA
// =====================================================================
// Hub-ul are nevoie de o singura cale spre exterior la un moment dat.
// Astazi este Ethernet; WiFi urmeaza. Comutatorul de mai jos alege ce se
// compileaza in NetLink.cpp.
//
// De ce un comutator de compilare si nu o clasa abstracta cu doua
// implementari: pe un aparat care are exact un transport, polimorfismul
// la rulare costa o tabela virtuala si o indirectare la fiecare apel si
// nu cumpara nimic. Cusatura de care chiar este nevoie este mai sus, la
// nivelul HTTP: NetLink::acquireClient() intoarce un Client*, iar
// EthernetClient si WiFiClient deriva amandoua din Client. Codul care
// face cereri nu afla niciodata pe ce transport merge - si, mai
// important, nici pe ce magistrala.
#define HUB_NET_ETHERNET   0
#define HUB_NET_WIFI       1
#define HUB_NET_TRANSPORT  HUB_NET_ETHERNET

// Cat se asteapta un IP de la serverul DHCP, si cat se asteapta fiecare
// raspuns in parte.
//
// AL DOILEA PARAMETRU NU ESTE COSMETIC. Ethernet.maintain(), chemata
// periodic ca sa reinnoiasca lease-ul, face la expirarea lui T1 un
// schimb DHCP BLOCANT, marginit exact de valorile date aici lui
// Ethernet.begin(). Cu vechea valoare de 15000 ms si niciun timeout de
// raspuns, o reinnoire putea bloca hub-ul cincisprezece secunde - la
// zile dupa pornire, fara niciun avertisment, si peste ~25 de ferestre
// de downlink. 8 s / 2 s marginesc cazul cel mai rau la ceva ce se poate
// suporta si, mai ales, la ceva CUNOSCUT.
#define ETH_DHCP_TIMEOUT_MS     8000UL
#define ETH_DHCP_RESPONSE_MS    2000UL

// Cat de des se cheama Ethernet.maintain(). Lease-urile DHCP se masoara
// in ore; o data pe secunda este deja de mii de ori mai des decat e
// nevoie, si tine costul in loop() la zero cand nu e nimic de facut.
#define ETH_MAINTAIN_EVERY_MS   1000UL

// Peste cat timp o singura chemare a lui Ethernet.maintain() este
// considerata anormala si se raporteaza pe Serial, cu durata.
// Masuram, nu presupunem: daca reinnoirea DHCP chiar blocheaza, vrem
// linia aceea in jurnal prima data cand se intampla pe hardware real.
#define ETH_MAINTAIN_WARN_MS    200UL

// 1 = SpiBus tine minte cine a cerut ultima data magistrala si se plange
//     daca celalalt modul o cere fara ca primul sa fi eliberat-o.
//
// NU este un lacat si nu poate impiedica nimic: bibliotecile isi coboara
// singure CS-ul, din propriul cod. Este un assert, si prinde exact clasa
// de greseala pe care o face codul nou de retea - un deselectAll() uitat
// pe o cale de return timpuriu. Se lasa pe 1 cat timp se lucreaza la
// retea; costa un octet de stare si un test compilat conditionat.
#define SPI_BUS_ASSERT          1

// =====================================================================
// CLOUD - identitatea hub-ului si API-ul
// =====================================================================
// PASUL 1 din diagrama de pornire: parametrii de fabrica, compilati in
// firmware. Ei nu se schimba niciodata la rulare si nu se salveaza in
// NVS - sunt identitatea placii, nu o stare a ei.
#define HUB_ID                    4
#define HUB_DEVICE_UID            "fa2c305b-f0e8-49a8-9985-633c47914d70"
#define HUB_SERIAL_NUMBER         "PrimaV3HUB2026"
#define HUB_PROVISIONING_SECRET   "kiJaXUxD-ngzOITvJQFnWegjo66Gw3sFvz8XdAnVI2k"

// Versiunea de firmware. Se trimite la provisioning SI se afiseaza in
// bannerul de pornire, dintr-un singur loc: doua literale identice in
// doua fisiere se desincronizeaza la prima modificare.
#define HUB_FIRMWARE_VERSION      "1.0.1"

// Serverul. Adresa este un IP brut, deci NU este nevoie de DNS - se
// economisesc si rezolvarea, si esecul ei ca mod de defectare.
#define CLOUD_HOST_IP_0           84
#define CLOUD_HOST_IP_1           117
#define CLOUD_HOST_IP_2           97
#define CLOUD_HOST_IP_3           136
#define CLOUD_PORT                7039

// Antetul de autentificare comun.
#define CLOUD_ADMIN_KEY_HEADER    "X-Solvix-AdminKey"
#define CLOUD_ADMIN_KEY           "bli009t664p53JZYeRJ8y5cjoe9J2MK1E8BJTAXE7aWr"

// MASURAT: serverul raspunde cu "Transfer-Encoding: chunked", FARA
// Content-Length (Kestrel). De-chunker-ul din Http.cpp nu este deci o
// precautie teoretica - fara el, ArduinoJson ar primi antetul de chunk
// lipit de JSON, ar da InvalidInput la fiecare cerere, iar hub-ul ar
// raporta la nesfarsit "baza de date nu raspunde" cu un server sanatos.
#define CLOUD_PATH_HEALTH         "/api/health"
#define CLOUD_PATH_PROVISION      "/api/device/provision"

// 1 = se trimite X-Solvix-AdminKey si la POST /api/device/provision.
//
// MASURAT LA 2026-09-01, si rezultatul spune ca NU este ceruta: aceeasi
// cerere cu un deviceUid inexistent, trimisa o data cu cheia de admin si
// o data fara ea, a primit de ambele dati exact acelasi raspuns -
// 401 cu mesajul de PROVISIONING ("Identitatea trimisa nu este valida
// sau device-ul nu poate fi provisionat"), nu o eroare generica de
// autentificare. Cu alte cuvinte cererea a ajuns la handler in ambele
// cazuri, deci cheia nu este verificata acolo; credentialul care conteaza
// este provisioningSecret, cel per device.
//
// Ramane pe 1 fiindca asa s-a decis si fiindca nu strica. Trecerea pe 0
// este insa o singura linie, si are un castig real: cheia globala de
// admin nu ar mai ajunge in fiecare hub din teren, de unde oricine tine
// placa in mana o poate scoate cu esptool read_flash.
#define CLOUD_PROVISION_SENDS_ADMIN_KEY 1

// Cat de mare poate fi corpul unui raspuns. Raspunsul de provisioning
// are ~700 de octeti; 1536 lasa loc de crestere si ramane un tampon
// static rezonabil in .bss.
#define CLOUD_BODY_MAX            1536

// Cat asteapta o conexiune TCP sa se stabileasca.
//
// EthernetENC are implicit 5000 ms si NU are connect neblocant. Fara
// valoarea de aici, fiecare incercare catre o ruta moarta ar costa cinci
// secunde intregi, in care hub-ul este surd.
#define HTTP_CONNECT_TIMEOUT_MS   1200UL

// Bugetul total al unei cereri, din momentul in care incepe pana cand se
// renunta: conectare, trimitere, citirea raspunsului.
#define HTTP_BUDGET_MS            2500UL

// Cat timp dupa ultimul pachet auzit pe radio NU se porneste nicio
// cerere de retea.
//
// Senzorul isi tine fereastra de downlink deschisa 600 ms de la sfarsitul
// propriei transmisii - singura ocazie in care hub-ul poate sa ii
// raspunda. O cerere HTTP pornita fix atunci ii mananca fereastra.
// O secunda acopera fereastra cu marja si costa doar o intarziere de un
// ciclu a bootstrap-ului, care oricum nu se grabeste.
#define HTTP_QUIET_AFTER_RX_MS    1000UL

// Cat se asteapta intre incercarile de a ajunge la server, in secunde.
// Ultima valoare se repeta la nesfarsit.
//
// Nu este plat la 60 s dinadins: un hub care porneste cu trei secunde
// inaintea switch-ului nu are de ce sa stea un minut degeaba, iar un
// server care chiar este cazut nu merita interogat des.
#define CLOUD_RETRY_BACKOFF_S     { 5UL, 10UL, 30UL, 60UL }

// Cat asteapta hub-ul dupa un 429 ("prea multe cereri"), daca serverul
// nu trimite un antet Retry-After.
//
// UN 429 NU ESTE O EROARE CA CELELALTE. Celelalte esecuri sunt
// independente intre ele: reincerci si poate merge. Un 429 spune ca
// TOCMAI REINCERCARILE sunt problema, deci fiecare incercare in plus
// hraneste exact contorul care tine usa inchisa. Reincercarea deasa nu
// scurteaza pedeapsa, o prelungeste.
//
// De aici si valoarea mare, si faptul ca este separata de backoff-ul
// obisnuit: 15 minute inseamna patru incercari pe ora, adica destul de
// rar cat sa se stinga orice fereastra rezonabila de rate limiting.
#define CLOUD_RATELIMIT_COOLDOWN_MS  900000UL

// Dupa cate esecuri CONSECUTIVE de provisioning hub-ul se opreste din
// incercat si intra in starea Blocked.
//
// Un provisioning care esueaza de cinci ori la rand nu se repara de la
// sine: ori secretul nu este bun, ori device-ul este deja inregistrat,
// ori serverul ne-a inchis usa. In toate cazurile mai trebuie un om.
// Hub-ul spune ce a incercat si se opreste; `provision` il porneste din
// nou dupa ce s-a lamurit cauza. Radioul ramane pornit tot timpul.
#define CLOUD_PROVISION_MAX_ATTEMPTS 5

// La cat timp se reaminteste pe Serial ca hub-ul inca nu e provizionat.
#define CLOUD_NAG_EVERY_MS        600000UL

// Spatiul NVS pentru identitatea hub-ului. SEPARAT de "solvix-pair", al
// registrului de senzori: cele doua se versioneaza independent, iar o
// stergere a unuia nu are voie sa il atinga pe celalalt.
// Maximum 15 caractere, asa cere NVS.
#define IDENTITY_NVS_NAMESPACE    "solvix-hub"

// Versiunea structurii salvate. Ca la REGISTRY_BLOB_VERSION, o valoare
// diferita face ca ce e in NVS sa fie ignorat.
//
// ATENTIE, PRETUL ESTE ALTUL AICI: un hub care porneste neprovizionat
// cere din nou /api/device/provision pentru acelasi deviceUid. Daca
// endpoint-ul NU este idempotent, asta inseamna un hub nou pe server si
// istoricul vechi orfan. De confirmat cu backend-ul inainte de a creste
// vreodata numarul.
#define IDENTITY_BLOB_VERSION     1

#endif // CONFIG_H
