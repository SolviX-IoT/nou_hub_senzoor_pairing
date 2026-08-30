# CLAUDE.md — SolviX HUB + SENZOR (pairing fara criptare)

> **Referinta activa a proiectului**: ce este, cum arata hardware-ul si
> protocolul, si **regulile** dupa care se lucreaza. Se incarca automat in
> fiecare sesiune, deci ramane scurt: tine doar ce trebuie stiut **inainte**
> de a atinge codul.
>
> Celelalte doua fisiere se deschid la nevoie:
> - **[MEMORY.md](MEMORY.md)** — starea de acum: cifrele de incadrare,
>   versiunile de format, ce se schimba la fiecare placa. Citeste-l cand
>   intrebi "unde am ramas" sau cand verifici ce s-a programat pe cip.
> - **[ISTORIC.md](ISTORIC.md)** — arhiva `F-001…F-038`: simptom, cauza, fix.
>   Citeste-l cand un mesaj de commit citeaza o eticheta `F-0xx`, cand vrei
>   motivul din spatele unei decizii, sau cand un simptom pare cunoscut.
>
> **Regula de actualizare este in §8.** Se aplica la fiecare commit.

---

## 1. Prezentare generala

Un sistem cu **doua noduri** care comunica radio prin **LoRa** in banda
europeana de **868 MHz**.

| Nod | Hardware | Toolchain | Rol |
|-----|----------|-----------|-----|
| **Senzor** | PIC16LF1508 + RFM96 (SX1276) + NTC 10K 3950 | MPLAB X, XC8, drivere MCC Melody | Se inroleaza la hub, apoi masoara temperatura si o trimite **in clar** prin LoRa. Inrolat, **doarme intre transmisii**, un interval propriu numarului lui (~23–38 s) |
| **Hub** | ESP32 Dev Module + RFM96 (SX1276) + ENC28J60 | Arduino IDE | Inroleaza si tine **pana la 5 senzori**, cu registrul lor in NVS; primeste datele fiecaruia, poate dezinrola un device |

Ambele module radio sunt SX1276, deci parametrii radio trebuie sa fie
**identici bit cu bit** pe cele doua capete, altfel pachetele nu se vad.

### Fluxul complet

**Inrolare**, si cere interventie umana la ambele capete: senzorul trimite
`JOIN_REQ` doar dupa ~3 secunde pe butonul 2 (RC5); hub-ul il asculta doar in
fereastra deschisa cu `pair` sau cu butonul 1, si doar daca `DevEUI`-ul este in
`PROVISIONED_DEVICES_INIT`. Raspunde cu `JOIN_ACCEPT`, care duce **numarul**
placii (`DevAddr` = pozitia in tabel, deci acelasi de fiecare data).

**Date:** senzorul salveaza inrolarea in HEF si trimite temperatura ca
`DATA_UP`, cu frame counter strict crescator. Intre pachete doarme si se
trezeste pe watchdog; butonul e citit la fiecare trezire (~2 s latenta).

**Dezinrolare:** `remove <DevEUI>` trimite `CMD_DOWN(RESET)` la **fiecare**
pachet si sterge inregistrarea abia dupa ce senzorul tace — tacerea este dovada
ca a primit comanda (F-031).

Payload-ul de temperatura este **exact acelasi pachet de 6 octeti** ca inainte,
deci trece prin acelasi cod de interpretare ca testul 7.

### Avertisment: reteaua NU este autentificata

Criptografia a fost scoasa la 2026-08-29 (**F-038**) fiindca nu mai incapea in
PIC16LF1508. Nu exista MIC, cheie sau nonce: oricine cu un radio pe aceiasi
parametri poate injecta o temperatura falsa, poate dezinrola orice placa cu
patru octeti (`A5 13 <DevAddr> 02`), poate inrola o placa falsa cat fereastra
e deschisa, si poate rejuca orice pachet. Singura aparare ramasa pe calea de
date este **frame counter-ul strict crescator**.

Inrolarea a ramas o **comisionare**, nu un control de acces. **Nu compensa cu
nimic facut in casa** — cifrul se reintroduce din `a710142` dupa upgrade-ul de
microcontroller. Detalii: [ISTORIC.md](ISTORIC.md), F-038.

### Structura folderelor

```
nou_hub_senzoor_pairing/
├── CLAUDE.md · MEMORY.md · ISTORIC.md · README.md
├── PINOUT_config.pdf        <- schema de conexiuni
├── senzor/                  <- proiect MPLAB X: firmware-ul nodului senzor
└── hub/SolvixHub_Tests/     <- sketch Arduino: suita de teste, meniu pe Serial
```

Numele folderului `SolvixHub_Tests` **nu este optional**: Arduino IDE cere ca
folderul sketch-ului si fisierul `.ino` principal sa aiba acelasi nume. Toate
fisierele hub-ului stau inauntru, plat, fara subfoldere.

---

## 2. Referinta hardware

### 2.1. Nod SENZOR — PIC16LF1508 (20 pini; TRIS/ANSEL din `PIN_MANAGER_Initialize`)

| Pin | Functie | Directie | Configurare | Sursa in cod |
|-----|---------|----------|-------------|--------------|
| **RB4** | **LoRa MISO** (SDI la PIC) | intrare | `TRISB=0xB0` bit4=1, digital | fix hardware |
| **RB5** | **LoRa NSS / CS** | iesire | `ANSELBbits.ANSB5=0`, `TRISB5=0`, inactiv HIGH | `main.c`, `LoRa_Select()` |
| **RB6** | **LoRa SCK** | iesire | fix hardware | MSSP1 |
| **RC7** | **LoRa MOSI** (SDO la PIC) | iesire | `TRISC=0x37` bit7=0 | fix hardware |
| **RC2** | **NTC 10K 3950** -> **AN6** | intrare analogica | `ANSELC=0x06`, `TRISC2=1` | `pins.c` |
| **RC4** | **Buton 1** (activ HIGH, pull-down extern) | intrare digitala | `TRISC4=1` | `main.c` |
| **RC5** | **Buton 2** — tinut ~3 s deschide pairing-ul (activ HIGH, pull-down extern) | intrare digitala | `TRISC5=1` | `ButtonPair_HeldLong()` |
| **RC3** | **LED 1** — transmisie de date; aprins cat dureaza si fereastra de downlink (F-032) | iesire | `ANSC3=0`, `TRISC3=0` | `main.c` |
| **RC6** | **LED 2** — pairing / eroare de join; clipeste cat fereastra de pairing e deschisa | iesire | `ANSC6=0`, `TRISC6=0` | `main.c` |
| **RC1** | **liber / neconectat** | intrare analogica (implicit MCC) | `ANSC1=1`, `TRISC1=1`; **fara cod in `main.c`** | `pins.c` |
| RA0 / RA1 | ICSPDAT / ICSPCLK | — | `LVP=ON` | `config_bits.c` |
| RA3 | MCLR / VPP | intrare | `MCLRE=ON` | `config_bits.c` |

**LED1** se aprinde la fiecare `DATA_UP` si se stinge dupa inchiderea
ferestrei de downlink — **NU este un puls blocant**, ar face senzorul surd
(F-032). **LED2** clipeste la 5 Hz cat butonul 2 e apasat si cat fereastra de
pairing e deschisa, continuu in timpul unui join, trei clipiri la esec, doua
pulsuri la reusita, un puls la un ACK.

**Butonul 2 (RC5) nu mai este liber.** `Button_RawPressed()` citeste in
continuare DOAR RC4, si blocul comentat pentru RC5 din interiorul ei trebuie sa
ramana comentat: altfel cele trei secunde de tinut apasat ar declansa in acelasi
timp si fereastra de pairing, si un sir de `DATA_UP`.

**Neconectate / nedefinite in cod (presupuneri documentate):** LoRa **RESET** nu
apare in cod (se presupune la VDD sau in aer — RFM96 are POR intern; se
foloseste doar soft-reset prin `RegOpMode`); **DIO0/IRQ** nu apare in cod —
`TxDone` **si** `RxDone` se afla prin **polling pe `RegIrqFlags`**; nu exista
niciun switch, RC4/RC5 sunt singurele intrari; **Timer0/1/2 nu sunt configurate
deloc** (F-017), singura temporizare este `__delay_ms()` la `_XTAL_FREQ` = 16 MHz.

### 2.2. Nod HUB — ESP32 Dev Module (`hub/SolvixHub_Tests/Config.h`)

| GPIO | Semnal | Modul | Observatie |
|------|--------|-------|------------|
| **18 / 19 / 23** | SCK / MISO / MOSI | ENC28J60 **si** LoRa | magistrala SPI comuna |
| **4** | CS_ETH | ENC28J60 | **NU este GPIO5** (F-005) |
| **32** | RESET_ETH | ENC28J60 | activ pe LOW |
| **5** | NSS | LoRa SX1276 | |
| **14** | RST | LoRa SX1276 | activ pe LOW |
| **26** | DIO0 | LoRa SX1276 | dat librariei, dar `onReceive()` nu se foloseste (F-004) |
| **34** | Buton 1 | — | **input-only**, rezistor extern obligatoriu; deschide fereastra de pairing |
| **35** | Buton 2 | — | **input-only**, rezistor extern obligatoriu |
| **22** | LED 1 (`PIN_LED_1`) | — | activitate: pachet valid |
| **21** | LED 2 (`PIN_LED_2`) | — | stare: aprins cat asculta, clipeste in mod pairing |

Butonul 1 este ascultat **doar** cand nu ruleaza niciun test sau cand ruleaza
testul 8 — altfel zgomotul de pe o linie flotanta ar comuta testele singur
(F-008). Polaritatea LED-urilor este presupusa **activa HIGH**; daca sunt
cablate invers, se schimba `LED_ON_LEVEL` in `Config.h`, nicaieri altundeva.

### 2.3. Parametrii radio LoRa — identici pe ambele capete

| Parametru | Valoare | Registru SX1276 (senzor) | API Arduino (hub) |
|-----------|---------|--------------------------|-------------------|
| Frecventa | **868.0 MHz** | `RegFrf = 0xD9 00 00` | `LoRa.begin(868E6)` |
| Bandwidth | **125 kHz** | `RegModemConfig1 = 0x72`, biti 7:4 | `setSignalBandwidth(125E3)` |
| Coding rate | **4/5** | `RegModemConfig1`, biti 3:1 | `setCodingRate4(5)` |
| Header | **explicit** | `RegModemConfig1`, bit0 = 0 | implicit |
| Spreading factor | **SF7** | `RegModemConfig2 = 0x74` | `setSpreadingFactor(7)` |
| CRC payload | **activ** | `RegModemConfig2`, bit2 = 1 | `enableCrc()` |
| AGC automat | activ | `RegModemConfig3 = 0x04` | implicit |
| Sync word | **0x12** (valoarea de reset, nescrisa explicit pe senzor) | `RegSyncWord` neatins | `setSyncWord(0x12)` |
| Preambul | 8 simboluri (reset) | neatins | `setPreambleLength(8)` |
| Putere PA | ~14 dBm PA_BOOST | `RegPaConfig = 0x8F` | `setTxPower(14, PA_BOOST)` |
| Lungime max. la RX | **6 octeti** (`LORA_RX_BUFFER_LEN`) | `RegMaxPayloadLength = 0x06` | implicit in librarie |

**`RegMaxPayloadLength` este si un filtru util pe gratis.** Cel mai lung pachet
pe care senzorul il PRIMESTE are 4 octeti (`CMD_DOWN`); `DATA_UP` are 13 si
`JOIN_REQ` 10, deci modemul arunca singur, in hardware, pachetele celorlalti
senzori inainte ca firmware-ul sa le vada (F-035). **SE RECALCULEAZA ORI DE
CATE ORI SE SCHIMBA LUNGIMILE PACHETELOR** — ridicat peste cel mai lung pachet
primit, filtrul dispare in tacere (capcana 3 din F-038).

**Sync word:** senzorul nu scrie `RegSyncWord`, deci ramane la valoarea de reset
`0x12` — exact valoarea implicita a librariei. Schimbat pe un capat, trebuie
schimbat si pe celalalt.

---

## 3. Protocolul de aplicatie

Toate campurile multi-octet sunt **big-endian**. Primul octet este magic-ul
`0xA5`. Protocolul este scris in **doua locuri care se modifica impreuna**:
sectiunea 4 din `senzor/main.c` si `hub/SolvixHub_Tests/SensorPacket.h`.

| TYPE | Nume | Directie | Lungime |
|------|------|----------|---------|
| `0x01` | TEMP_PLAIN | senzor -> hub | 6 |
| `0x10` | JOIN_REQ | senzor -> hub | 10 |
| `0x11` | JOIN_ACCEPT | hub -> senzor | 3 |
| `0x12` | DATA_UP | senzor -> hub | 13 |
| `0x13` | CMD_DOWN | hub -> senzor | 4 |

**Cele cinci lungimi sunt DISTINCTE si trebuie sa ramana asa.** De cand nu mai
exista MIC, perechea tip+lungime este **singura** verificare impotriva unei
desincronizari intre capete: un pachet de format vechi este respins de
`messageType()` pe hub si de `LoRa_Receive()` pe senzor, in loc sa fie citit la
offset-uri gresite si sa scoata o temperatura plauzibila si gresita, in tacere.
**Nu egala doua lungimi.**

```
TEMP_PLAIN (0x01), 6 octeti — payload-ul transportat si de DATA_UP
[0] 0xA5   [1] 0x01
[2] TEMP_HI, [3] TEMP_LO   int16 = temperatura_C * 100
                           -30000 (0x8AD0) = eroare de citire ADC
[4] REASON   0x00 = interval periodic, 0x01 = buton apasat
[5] CHECKSUM (b0^b1^b2^b3^b4) ^ 0x5A

JOIN_REQ (0x10), senzor -> hub, 10 octeti
[0] 0xA5   [1] 0x10   [2..9] DevEUI (8B)

JOIN_ACCEPT (0x11), hub -> senzor, 3 octeti
[0] 0xA5   [1] 0x11   [2] DevAddr

DATA_UP (0x12), senzor -> hub, 13 octeti
[0] 0xA5   [1] 0x12   [2] DevAddr
[3..6]  FrameCounter (4B, big-endian, strict crescator)
[7..12] pachetul TEMP de 6 octeti, IN CLAR

CMD_DOWN (0x13), hub -> senzor, 4 octeti
[0] 0xA5   [1] 0x13   [2] DevAddr
[3] CmdType: 0x01 = ACK, 0x02 = RESET (dezinrolare)
```

**Identificatori.** `DevEUI` = 8 octeti, `"SOLVIX" | 0x00 | SENSOR_NODE_ID`;
PIC16LF1508 nu garanteaza un ID unic, deci se **provizioneaza** din
`SENSOR_NODE_ID` si se scrie in HEF la prima pornire. `DevAddr` = 1 octet,
numarul senzorului 1..`HUB_MAX_SENSORS`, din **pozitia** DevEUI-ului in
`PROVISIONED_DEVICES_INIT` (`DeviceRegistry::addressForEui`, F-037) — stabil
peste reinrolari si peste golirea registrului, deci se poate scrie pe cutie.
**Nu mai exista AppKey, SessKey, DevNonce sau JoinNonce** (F-038).

**Note care nu se pot deduce din layout:**
- `JOIN_REQ` poarta `DevEUI` desi hub-ul stie ce numere exista: el este cheia
  dupa care hub-ul verifica provisioning-ul si deriva numarul. Daca ar purta
  doar numarul, senzorul si-ar declara singur adresa (F-037).
- `DevAddr` in clar in `JOIN_ACCEPT` lasa senzorul sa filtreze fereastra de join
  pe adresa. Efect secundar util: o placa programata cu un numar care nu
  corespunde pozitiei ei din tabel **isi refuza singura JOIN_ACCEPT-ul**, cu
  trei clipiri pe LED2.
- `DevAddr` din `DATA_UP[2]` raspunde la "de la cine vine data", dar raspunsul
  este **declarativ**, nu dovedit (F-038).
- **Checksum-ul XOR nu este apararea de integritate** — aceea este CRC-ul LoRa.
  El exista ca pachetul de temperatura sa ramana bit cu bit cel vechi. Daca nu
  trece desi CRC-ul LoRa a fost bun: ori un emitator strain pe aceiasi
  parametri, ori un capat ramas pe firmware vechi.
- `CMD_DOWN` se **retrimite** la fiecare pachet al unui device marcat: un
  downlink are o singura sansa, fiindca senzorul asculta doar
  `DOWNLINK_WINDOW_MS` = 600 ms dupa fiecare transmisie (F-031). Din acelasi
  motiv, intre transmisie si fereastra de receptie **nu are voie sa stea nimic
  blocant** (F-032). La `RESET` senzorul trece in `DEV_STATE_IDLE`; reintrarea
  cere `pair` pe hub **plus** trei secunde pe butonul 2 (F-030).
- **Cei 5 senzori nu vorbesc odata.** Fara arbitraj si fara sloturi: fiecare
  doarme `SLEEP_WAKEUPS_BASE (11) + (DevAddr - 1) + jitter 0..3` treziri
  (intervalele, in [MEMORY.md](MEMORY.md)). Intervalul propriu desparte doi
  senzori ciocniti, jitter-ul rupe pornirea simultana dupa o pana de curent
  (F-036). Coliziunile se deduc din **golurile de frame counter**, in coloana
  `pierd.` a comenzii `sensors`; un salt peste `SENSOR_FCNT_GAP_RESTART` este
  raportat ca repornire, nu ca pierderi.

---

## 4. Memoria ne-volatila

### 4.1. Senzor — HEF (High-Endurance Flash)

PIC16LF1508 **nu are EEPROM**. Se folosesc ultimele 128 de cuvinte ale memoriei
de program (`0x0F80`–`0x0FFF`), garantate la ~100.000 de cicluri; se foloseste
doar octetul de jos al fiecarui cuvant. Flash-ul se sterge si se scrie pe
**randuri intregi de 32 de cuvinte** (`FLASH_ERASE=20`, `FLASH_WRITE=20` in
`16lf1508.ini` — masurat, nu presupus, F-026), deci incap exact 4 randuri:

| Rand | Adresa | Continut |
|------|--------|----------|
| 0 | `0x0F80` | MAGIC(1) + DevEUI(8) |
| 1 | `0x0FA0` | `HEF_MAGIC_SESSION`(1) + DevAddr(1) |
| 2 | `0x0FC0` | inelul de frame counter, slotul 0: MAGIC(1) + counter(4) |
| 3 | `0x0FE0` | inelul de frame counter, slotul 1 |

**Prezenta marcajului de sesiune inseamna "sunt inrolat, am voie sa vorbesc".**
O inrolare este o singura stergere+scriere.

**Frame counter-ul** sta in RAM si se salveaza doar la fiecare
`FCNT_CHECKPOINT_EVERY` (50) transmisii, prin rotatie in cele 2 sloturi
(F-022) — `SLEEP` pastreaza RAM-ul, deci somnul nu schimba nimic aici. La
citire se ia **maximul** sloturilor valide. La **cold boot** se sare inainte cu
`FCNT_CHECKPOINT_EVERY`, ca sa nu se reutilizeze o valoare deja emisa; pretul
este o "gaura" in numerotare dupa fiecare reset.

**Regiunea HEF este rezervata explicit din linker** cu `--ROM=default,-f80-fff`
(proprietatea `code-model-rom`). Fara ea, linkerul plaseaza cod acolo si prima
scriere in HEF isi sterge propriul program (F-027).

### 4.2. Hub — NVS prin `Preferences`

Registrul traieste in spatiul NVS `solvix-pair` si are exact `HUB_MAX_SENSORS`
locuri. Fiecare inregistrare tine `DevEUI`, `DevAddr`, `lastFrameCounterUp`,
`hasUplink`, `downCounter`, pachete primite si **pierdute** (`lostPackets`),
starea dezinrolarii (`pendingReset`, `resetAttempts`, `resetSentMs`) si ultima
masuratoare (`lastTempX100`, `lastRssi`, `hasReading`). **Nu mai tine nicio
cheie** (F-038).

**Sase campuri sunt relative la sesiunea curenta si se pun pe 0 / `false` la
incarcarea din NVS:** `lastSeenMs`, `resetSentMs`, `hasReading`,
`lastTempX100`, `lastRssi`, `offlineReported`. Pentru `resetSentMs` nu e
curatenie: `0` inseamna "niciun RESET trimis in sesiunea asta", iar fara
zeroizare orice dezinrolare in curs ar aparea confirmata imediat dupa fiecare
repornire (F-031).

Se salveaza la fiecare inrolare, la fiecare stergere si o data la
`REGISTRY_SAVE_EVERY` (20) pachete — NVS este flash.

**`REGISTRY_BLOB_VERSION` invalideaza registrul salvat cand `DeviceRecord` se
modifica. Pretul, de stiut inainte de a-l incrementa:** hub-ul porneste cu
registrul gol in timp ce senzorii isi pastreaza starea in HEF; ei continua sa
emita, apar ca `DevAddr ... nu este inrolat`, si fiecare trebuie reinrolat
manual — numarul primit inapoi ramane insa acelasi (F-037).

---

## 5. Ce face fiecare fisier

### 5.1. `senzor/` — proiect MPLAB X

| Fisier | Rol |
|--------|-----|
| `main.c` | **Firmware-ul complet**, in 16 sectiuni numerotate: parametri, pini, registre SX1276, protocol, HEF, `Word32`, starea device-ului, NVM, driver LoRa (TX **si** RX), ADC+NTC, butoane, LED-uri, construirea pachetelor, initializare, inrolare, bucla principala cu `IDLE`/`JOINING`/`OPERATING`. **Singura linie care difera intre cele cinci placi este `SENSOR_NODE_ID`.** `LoRa_Receive()` filtreaza dupa tip, LUNGIME si `devAddr` si este singurul punct de validare a receptiei (F-035); `Rand8()` da jitter-ul din F-036. Conversia NTC este un tabel de cautare cu interpolare pe 16 biti, 25 de intrari de la −20 la +100 °C, cu 8 citiri ADC mediate (F-016, F-028) |
| `mcc_generated_files/system/src/config_bits.c` | `FOSC=INTOSC`, **`WDTE=SWDTEN`**, `MCLRE=ON`, `BOREN=ON`, `LVP=ON`, `PWRTE=OFF`. **`WRT=OFF` este obligatoriu pentru HEF.** **Fisier generat de MCC: o regenerare pune `WDTE` inapoi pe `OFF` si senzorul nu se mai trezeste** |
| `.../clock.c`, `pins.c`, `mssp.c`, `system.c` | Oscilator intern 16 MHz; `TRISA=0x3F`, `TRISB=0xB0`, `TRISC=0x37`, `ANSELA=0x17`, `ANSELB=0x20`, `ANSELC=0x06`; SPI la 125 kHz (`SSP1CON1=0x0A`, `SSP1ADD=0x1F`) |
| `.../interrupt.c` | Vector generat; **nu este folosit efectiv** |
| `nbproject/`, `Makefile*` | Doua setari **obligatorii**, deja aplicate: `optimization-level = -O2` si `code-model-rom = default,-f80-fff` (F-027) |

### 5.2. `hub/SolvixHub_Tests/` — sketch Arduino

| Fisier | Rol |
|--------|-----|
| `SolvixHub_Tests.ino` | Meniu pe Serial (115200), tabloul `TESTS[]`, comenzile `pair` / `sensors` / `list` / `provisioned` / `remove` / `stats` / `help`, butonul 1 ca declansator de pairing. `commandRemove()` doar **marcheaza** device-ul, refuza marcarea unui senzor care nu a trimis niciodata nimic (trimite la `force`) si accepta si forma scurta `remove #3` |
| `Config.h` | **Singura sursa de adevar pentru pini** si constante: SPI, ETH, LoRa, butoane, LED-uri, sectiunea de pairing (`PAIRING_MODE_TIMEOUT_MS`, `PAIRING_SEND_ACK`, `REMOVE_CONFIRM_SILENCE_MS`, `REGISTRY_*`) si sectiunea multi-senzor (`HUB_MAX_SENSORS`, `SENSOR_OFFLINE_MS`, `SENSOR_FCNT_GAP_RESTART`, `PROVISIONED_DEVICES_INIT` — **ordinea randurilor da numarul fiecarui senzor**) |
| `SpiBus.*` | Arbitrajul magistralei partajate; `SpiGuard` ridica CS-ul in destructor |
| `TestBase.*` | `Test { name, description, begin, tick, stop }` + ajutoare de afisare |
| `LoRaRadio.*` | Invelis peste libraria LoRa: `begin()`, `sendText()`, `sendRaw()`, `receive()`, `receiveRaw()`, `sleep()`. Receptia e prin polling |
| `Leds.*` | Cele doua LED-uri. `set()`, `pulse()`, `service()` fara `delay()` |
| `SensorPacket.*` | **Oglinda protocolului din `senzor/main.c`**: constantele tuturor tipurilor, `decode()`/`print()`/`printRaw()`, `messageType()`, `parseJoinRequest()`, `parseData()`, `buildJoinAccept()`, `buildCommand()`, `printEui()` |
| `DeviceRegistry.*` | Registrul pe NVS; `isProvisioned()`, **`addressForEui()`** (numarul din pozitia in tabel, F-037), `printSensorTable()` — vederea de zi cu zi, toate locurile, si cele goale |
| `EthernetLink.*` | Invelis peste EthernetENC: DHCP cu timeout, `printStatus()`, HTTP GET. Aici e definit `HUB_MAC` |
| `TestButtons.*` · `TestEncSpi.*` · `TestEthernet.*` | Tranzitii pe GPIO34/35 · diagnostic SPI pe ENC28J60 (`EREVID`) · DHCP+DNS+HTTP |
| `TestLoRaTx.*` · `TestLoRaRx.*` · `TestCoexistence.*` | Emisie la 2 s · receptie cu RSSI/SNR · ambele module alternativ (LoRa se initializeaza **primul**) |
| `TestSensorRx.*` | **Testul 7:** asculta pachetul de temperatura in clar. Util la bring-up, cu `ENABLE_PLAIN_TEMP = 1` pe senzor |
| `TestPairing.*` | **Testul 8:** fereastra de pairing, `JOIN_REQ` -> `JOIN_ACCEPT`, `DATA_UP` -> `decode()`, `CMD_DOWN` (ACK/RESET), contoarele pentru `stats`. Tot aici sta dezinrolarea confirmata: `sendRemovalReset()` + `servicePendingRemovals()` (F-031), `printSensorTag()`, golurile de frame counter si `serviceOfflineWatch()` |
| `README.md` | Instructiuni de utilizare, comenzile de pairing, tabelul SPI, note hardware |

---

## 6. Reguli de lucru

### 6.1. Reguli de cod

1. **Niciun numar de pin "in clar"** in logica de aplicatie. Pe hub totul intra
   in `Config.h`; pe senzor, in blocul de `#define` din antetul `main.c`.
2. **Parametrii radio se schimba simultan pe ambele noduri.** O singura
   diferenta (frecventa, SF, BW, CR, sync word) si legatura dispare complet,
   fara niciun mesaj de eroare.
3. **Nu se apeleaza `LoRa.end()` / `SPI.end()`** pe hub (F-003). Pentru oprirea
   radioului: `LoRa.sleep()`.
4. **Nu se acceseaza SPI din context de intrerupere** pe hub (F-004).
5. **Nu se face acces SPI pe senzor daca `loraReady == 0`** —
   `SPI1_ByteExchange` se blocheaza la nesfarsit cu MSSP-ul oprit (F-013).
6. Pe senzor, orice pin analogic nou se declara in `ANSELC`, iar orice pin
   digital de pe portul C se **scoate** din `ANSELC`.
7. **Functionalitatea noua extinde `senzor/` si `hub/`**, in structura lor
   existenta. **Nu se creeaza foldere paralele** (F-020).
8. Pe hub, un test **nu** face `digitalWrite` pe un pin de LED. Trece prin
   modulul `Leds`.
9. **Protocolul se modifica in doua fisiere simultan:** `senzor/main.c`
   (sectiunea 4) si `hub/SolvixHub_Tests/SensorPacket.h`.
10. **Constantele care trebuie sa fie identice pe cele doua capete se schimba
    tot in pereche:**
    - **lungimile celor cinci mesaje** (`JOIN_REQ_LEN`, `JOIN_ACCEPT_LEN`,
      `DATA_UP_LEN`, `CMD_DOWN_LEN`, `LORA_PACKET_LEN`), definite in ambele
      fisiere. Fara MIC, perechea tip+lungime este SINGURA verificare impotriva
      desincronizarii, deci cele cinci valori trebuie sa ramana si **distincte
      intre ele** (6 / 10 / 3 / 13 / 4);
    - `LORA_RX_BUFFER_LEN` (`senzor/main.c`) ajunge in `RegMaxPayloadLength` si
      **se recalculeaza ori de cate ori se schimba lungimile**: el este filtrul
      hardware care arunca pachetele celorlalti senzori (F-035, F-038);
    - **numarul senzorului:** randul N din `PROVISIONED_DEVICES_INIT`
      (`Config.h`) <-> `SENSOR_NODE_ID = N` (`senzor/main.c`), din care iese
      `PROVISION_DEV_EUI`. **Nu se rearanjeaza randurile tabelului intr-o retea
      deja instalata** — senzorii si-ar schimba numerele intre ei (F-037);
    - **intervalul de somn <-> fereastra de confirmare:** `SLEEP_WAKEUPS_BASE`
      + `SLEEP_SLOT_MASK` + `SLEEP_JITTER_MASK` (senzor) <->
      `REMOVE_CONFIRM_SILENCE_MS` si `SENSOR_OFFLINE_MS` (`Config.h`). Hub-ul
      confirma dezinrolarea prin **tacere**, iar un senzor care doarme tace si
      el: daca somnul creste peste fereastra, hub-ul sterge inregistrarea in
      timp ce senzorul doar doarme (F-031, F-034). Fereastra trebuie sa acopere
      cel putin patru cicluri de somn **in cazul cel mai lent** — senzorul cu
      numarul cel mai mare, jitter maxim, LFINTOSC la limita de toleranta.
      **Cresterea lui `HUB_MAX_SENSORS` intra automat in socoteala** (F-036).
11. **Nu se afiseaza pe Serial nimic ce ar trebui sa ramana secret.** Nu mai
    exista chei in proiect, dar regula ramane pentru ce vine dupa upgrade-ul de
    microcontroller: `list` arata DevEUI si DevAddr, niciodata o cheie.
12. **Un build de verificare nu scrie in `senzor/build/` sau `senzor/dist/`**
    (F-033) — sunt directoarele de lucru ale lui MPLAB X, iar obiecte straine
    acolo lasa IDE-ul sa programeze cod vechi. Daca s-a scris totusi, *Clean*
    pe proiect inainte de urmatoarea programare.
13. **Senzorul se compileaza cu `-O2`, iar regiunea HEF ramane rezervata**
    (`code-model-rom = default,-f80-fff`). Cele doua se seteaza **din fereastra
    de proprietati a proiectului**, nu editand fisierele: MPLAB X rescrie
    `Makefile-default.mk` la fiecare build (F-029).
14. **Dupa orice adaugare de cod pe senzor, citeste raportul de memorie** si
    compara-l cu cifra din [MEMORY.md](MEMORY.md). Marja curenta este
    confortabila, dar cifra este si dovada ca s-a programat codul corect
    (F-033).
15. **Pe senzor, evita `int32_t` in codul fierbinte** (F-028). O inmultire sau
    o impartire pe 32 de biti costa peste 100 de cuvinte pe PIC16. Foloseste
    uniunea `Word32` pentru conversii big-endian si aritmetica pe 16 biti.
16. **Toate cele cinci placi au acelasi `main.c`.** Se schimba o singura linie,
    `SENSOR_NODE_ID`, si se recompileaza — nu se duplica fisierul si nu se
    creeaza cate un proiect pe placa (F-020 la nivel de fisier). **Dupa fiecare
    programare, verifica pe hub ca placa apare cu numarul asteptat**
    (`sensors`).
17. **Un pachet nou care circula intre noduri trebuie sa poarte `DevAddr`**, in
    octetul `[2]`, ca toate celelalte. Altfel receptorul nu are cum sa stie al
    cui e pachetul, iar filtrul din `LoRa_Receive()` nu are dupa ce sa se
    ghideze (F-035). Partea a doua a regulii — "si `DevAddr` trebuie sa intre in
    zona acoperita de MIC" — **nu mai poate fi respectata**: adresa a devenit
    declarativa (F-038).

### 6.2. Reguli de colaborare

1. **Nu se scrie cod care nu a fost discutat si agreat in prealabil.** Nicio
   functie, constanta, fisier sau "imbunatatire" in plus fata de ce s-a cerut.
   Daca in timpul lucrului apare ceva ce pare necesar, se **propune** si se
   asteapta acordul.
2. **Nu se extinde scopul in tacere.** Un bug gasit pe langa sarcina curenta se
   **raporteaza**, nu se repara din mers.
3. **Nu se sterge cod "care oricum nu se foloseste" fara acord.** Vezi capcana
   `Word32` din F-038: stearsa din reflex odata cu cifrul, ar fi reintrodus
   ~300 de cuvinte fara ca nimeni sa observe.
4. **Fisierele generate de MCC** (`senzor/mcc_generated_files/`) nu se
   editeaza decat unde proiectul o cere deja explicit si documentat — azi doar
   `WDTE=SWDTEN` in `config_bits.c`. O regenerare le pune la loc pe implicit.
5. **Cand lipseste o informatie hardware, se scrie explicit PRESUPUNEREA** in
   cod si in acest fisier — nu se inventeaza in tacere. Cand o masuratoare pe
   placa o confirma sau o infirma, textul "PRESUPUNERE" se inlocuieste cu fapta
   constatata.

---

## 7. Conventia de mesaje de commit

Tipul si scopul in engleza (scurte, greppabile), restul in romana.

```
<tip>(<scop>): <subiect la imperativ, ~65 caractere, fara punct final>

<corp obligatoriu>
```

**Tipuri:**

| Tip | Cand |
|-----|------|
| `fix` | Repara un comportament gresit. **Cere o intrare `F-0xx` in `ISTORIC.md`** |
| `feat` | Functionalitate noua |
| `cleanup` | Se scoate cod / se simplifica, fara schimbare de comportament |
| `refactor` | Se rearanjeaza cod, comportament identic |
| `debug` | Instrumentare, urme, cod de diagnostic |
| `docs` | Numai documentatie |
| `build` | Proiect MPLAB X, Makefile, setari de compilator, `.gitignore` |
| `hw` | Maparea pinilor sau o presupunere de cablaj se schimba |

**Scop:** `senzor` · `hub` · `ambele` · `docs`

**Corpul este obligatoriu**, in ordinea asta:

1. eticheta `F-0xx`, daca commit-ul rezolva sau creeaza o intrare in
   `ISTORIC.md`;
2. **ce** s-a schimbat, concret — fisiere, functii, constante;
3. **de ce** — simptomul sau motivul, nu repovestirea diff-ului;
4. **pentru orice commit care atinge `senzor/`, cifra din raportul de
   memorie**, in forma `Flash: A -> B words (±N). RAM: A -> B B.` Regulile 13
   si 14 cer oricum citirea ei dupa fiecare build;
5. **cum s-a verificat** — compilat, programat, test manual pe placa.

Exemplu:

```
fix(senzor): fereastra de downlink se deschide imediat dupa TX

F-032. Led_PulseData() era un puls blocant de 150 ms intre
LoRa_SendBuffer() si LoRa_Receive(); hub-ul raspunde in ~55 ms, deci
niciun downlink din calea de date nu ajungea vreodata. LED1 se aprinde
acum inainte de fereastra si se stinge dupa ea, cu doua scrieri in LATC.
Flash: 3936 -> 3921 words (-15). RAM: 231 -> 231 B.
Verificat pe placa: ACK primit dupa fiecare DATA_UP, remove confirmat.
```

---

## 8. Regula de actualizare a documentatiei

**La fiecare commit**, inainte de a-l face:

| Ce s-a schimbat | Unde se actualizeaza |
|---|---|
| cod nou / fisier nou | tabelul din **§5** al acestui fisier |
| pin schimbat sau adaugat | tabelul din **§2** **si** `Config.h` / blocul de `#define` din `main.c` |
| parametru radio | tabelul din **§2.3**, **pe ambele coloane** |
| format de pachet | **§3**, plus verificarea parserului de pe hub |
| regula noua de lucru | **§6**, si numai dupa ce a fost discutata |
| **bug rezolvat** | intrare noua `F-0xx` in **[ISTORIC.md](ISTORIC.md)**, cu **simptom, cauza si fix** — nu doar cu descrierea fixului. Eticheta se citeaza in mesajul de commit |
| cifre de incadrare, versiuni de format, stare curenta | **[MEMORY.md](MEMORY.md)** |
| presupunere confirmata sau infirmata pe placa | se inlocuieste textul "PRESUPUNERE" cu fapta constatata, in cod si aici |

**Acest fisier are un buget: ~550 de linii.** Este incarcat integral in fiecare
sesiune, iar la 1687 devenise imposibil de citit si incepuse sa se
desincronizeze de cod. Ce creste peste buget se muta in `ISTORIC.md` — arhiva
nu are limita, referinta activa are. Cand bugetul se apropie, intreaba-te intai
daca textul nou este **referinta** (ramane) sau **naratiune** (pleaca): motivul,
simptomul si povestea unei decizii apartin arhivei, nu acestui fisier.
