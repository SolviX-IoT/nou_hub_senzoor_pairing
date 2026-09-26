# MEMORY.md — starea, referinta si jurnalul proiectului

> Tot ce **nu** este regula sta aici: ce s-a schimbat la fiecare commit,
> starea de acum (cifre, versiuni de format, ce se schimba la fiecare placa)
> si referinta tehnica (pini, radio, protocol, memorie, rolul fisierelor).
>
> Regulile sunt in [CLAUDE.md](CLAUDE.md). Motivul din spatele fiecarei
> decizii (simptom, cauza, fix) este in [ISTORIC.md](ISTORIC.md), pe etichete
> `F-0xx`.
>
> **Se actualizeaza la fiecare commit**, dupa [CLAUDE.md §5](CLAUDE.md).

**Cuprins:** Jurnal pe commit-uri · 1. Proiectul · 2. Hardware ·
3. Protocolul · 4. Memoria ne-volatila · 5. Fisierele · 6. Incadrarea in
memorie · 7. Hub-ul in cloud · 8. Versiuni de format · 9. Ce se schimba la
fiecare placa · 10. Dupa fiecare programare · 11. Ramas de facut

---

## Jurnal pe commit-uri

Cel mai nou sus. O intrare per commit: titlul, **ce** s-a schimbat (fisiere,
functii, constante), **de ce**, **cum s-a verificat**, iar pentru `senzor/`
cifra `Flash: A -> B words (±N). RAM: A -> B B.`

### 2026-09-26 — `docs(docs): CLAUDE.md redus la reguli, restul mutat in MEMORY.md`
- **Ce:** CLAUDE.md pastreaza doar regulile de cod, regulile de colaborare,
  conventia de commit si regula de actualizare. Referinta (hardware, radio,
  protocol, memorie, rolul fisierelor) s-a mutat aici, in §1–§5. Sectiune
  noua: acest jurnal. Conventia de commit: fara `Co-Authored-By`, corpul
  mesajului optional (detaliile stau in jurnal).
- **De ce:** CLAUDE.md se incarca in fiecare sesiune si ajunsese la ~550 de
  linii.
- **Corectat din mers:** referintele din documentatie la comenzile `sensors` si
  `provisioned`, sterse in F-046, arata acum spre `status`. Mesajul de boot din
  `SolvixHub_Tests.ino` inca spune `sensors` — ramas in §11.
- **Verificat:** fiecare sectiune din vechiul CLAUDE.md are un loc aici sau in
  CLAUDE.md.

### 2026-09-06 — `heartbeat activ` (4d3bcff)
Modulul `HubHeartbeat` (`POST /api/device/heartbeat`, preluarea de config);
consola redusa la `pair` / `status` / `remove` si cod fara apelanti scos
(F-046). Mesaj de commit in afara conventiei.

### 2026-09-01 — `pasul4_initializare_hub_in_database` (ec060a7)
Suita de teste scoasa, hub-ul porneste singur (F-039). Module noi: `NetLink`,
`Http`, `HubIdentity`, `HubCloud` — bootstrap-ul in cloud (F-042, F-043).

### 2026-08-30 — `docs(docs): CLAUDE.md impartit in CLAUDE.md + MEMORY.md + ISTORIC.md` (d3e6733)

### 2026-08-29 — `Criptografia scoasa, pairing-ul pastrat (F-038)` (af0719b)
Ultima versiune cu cifru este `a710142`.

---

## 1. Proiectul

Doua noduri care comunica prin **LoRa 868 MHz**. Un **senzor** pe PIC16LF1508
masoara temperatura cu un NTC si o trimite in clar; un **hub** pe ESP32 tine
pana la 5 senzori inrolati, cu registrul lor in NVS, si se inregistreaza in
cloud. Inrolarea este o **comisionare** — cine e in retea, ce numar are, de
unde incep contoarele — **nu un control de acces**.

| Nod | Hardware | Toolchain | Rol |
|-----|----------|-----------|-----|
| **Senzor** | PIC16LF1508 + RFM96 (SX1276) + NTC 10K 3950 | MPLAB X, XC8 v3.10, drivere MCC Melody | Se inroleaza la hub, apoi masoara temperatura si o trimite **in clar**. Inrolat, **doarme intre transmisii**, un interval propriu numarului lui (~23–38 s) |
| **Hub** | ESP32 Dev Module + RFM96 (SX1276) + ENC28J60 | Arduino IDE, placa *ESP32 Dev Module* | Inroleaza si tine **pana la 5 senzori**; primeste datele fiecaruia, poate dezinrola un device; bootstrap in cloud si heartbeat |

Librarii necesare pe hub: **EthernetENC** (Juraj Andrassy), **LoRa**
(Sandeep Mistry) si **ArduinoJson v7** (Benoit Blanchon).

### Structura folderelor

```
nou_hub_senzoor_pairing/
├── CLAUDE.md · MEMORY.md · ISTORIC.md · README.md
├── PINOUT_config.pdf        <- schema de conexiuni
├── senzor/                  <- proiect MPLAB X: firmware-ul nodului senzor
└── hub/SolvixHub_Tests/     <- sketch Arduino: firmware-ul hub-ului
```

Hub-ul **nu mai este o suita de teste** (F-039), dar folderul a ramas
`SolvixHub_Tests`: Arduino IDE cere ca folderul si `.ino`-ul principal sa se
numeasca la fel, iar redenumirea este un pas separat (un `git mv` in acelasi
commit cu alte schimbari ar face diff-ul necitibil). Fisierele hub-ului stau
plat, fara subfoldere.

### Fluxul complet

**Inrolare**, cu interventie umana la ambele capete: senzorul trimite
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
deci trece prin acelasi `SensorPacketCodec::decode()` ca pachetul unui senzor
neinrolat: nu exista doua cai de interpretare a temperaturii.

### Avertisment: reteaua NU este autentificata

Criptografia a fost scoasa la **2026-08-29** (F-038) fiindca nu mai incapea in
PIC16LF1508. Nu exista MIC, cheie sau nonce: oricine cu un radio pe aceiasi
parametri poate injecta o temperatura falsa, poate dezinrola orice placa cu
patru octeti (`A5 13 <DevAddr> 02`), poate inrola o placa falsa cat fereastra
e deschisa si poate rejuca orice pachet. Singura aparare pe calea de date este
**frame counter-ul strict crescator**.

Este o masura temporara, pana la un microcontroller cu mai multa memorie.
**Ultima versiune cu cifru este commit-ul `a710142`** — de acolo se
reintroduce.

---

## 2. Hardware

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
ferestrei de downlink — **nu este un puls blocant**, ar face senzorul surd
(F-032). **LED2** clipeste la 5 Hz cat butonul 2 e apasat si cat fereastra de
pairing e deschisa, continuu in timpul unui join, trei clipiri la esec, doua
pulsuri la reusita, un puls la un ACK.

**Butonul 2 (RC5) nu este liber.** `Button_RawPressed()` citeste doar RC4;
blocul comentat pentru RC5 din interiorul ei ramane comentat, altfel cele trei
secunde de tinut apasat ar declansa in acelasi timp si fereastra de pairing, si
un sir de `DATA_UP`.

**Neconectate / nedefinite in cod (PRESUPUNERI):** LoRa **RESET** nu apare in
cod (se presupune la VDD sau in aer — RFM96 are POR intern; se foloseste doar
soft-reset prin `RegOpMode`); **DIO0/IRQ** nu apare in cod — `TxDone` **si**
`RxDone` se afla prin **polling pe `RegIrqFlags`**; RC4/RC5 sunt singurele
intrari; **Timer0/1/2 nu sunt configurate** (F-017), singura temporizare este
`__delay_ms()` la `_XTAL_FREQ` = 16 MHz.

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

Zgomotul pe linia flotanta a butonului 1 poate deschide cel mult o fereastra
de inrolare degeaba, care se inchide singura (F-008). Polaritatea LED-urilor
este **PRESUPUSA activa HIGH**; daca sunt cablate invers, se schimba
`LED_ON_LEVEL` in `Config.h`, nicaieri altundeva.

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

**`RegMaxPayloadLength` este si un filtru pe gratis.** Cel mai lung pachet pe
care senzorul il PRIMESTE are 4 octeti (`CMD_DOWN`); `DATA_UP` are 13 si
`JOIN_REQ` 10, deci modemul arunca in hardware pachetele celorlalti senzori
inainte ca firmware-ul sa le vada (F-035). Ridicat peste cel mai lung pachet
primit, filtrul dispare in tacere (capcana 3 din F-038).

**Sync word:** senzorul nu scrie `RegSyncWord`, deci ramane la valoarea de
reset `0x12`, exact valoarea implicita a librariei.

---

## 3. Protocolul de aplicatie

Toate campurile multi-octet sunt **big-endian**. Primul octet este magic-ul
`0xA5`. Protocolul este scris in doua locuri care se modifica impreuna:
sectiunea 4 din `senzor/main.c` si `hub/SolvixHub_Tests/SensorPacket.h`.

| TYPE | Nume | Directie | Lungime |
|------|------|----------|---------|
| `0x01` | TEMP_PLAIN | senzor -> hub | 6 |
| `0x10` | JOIN_REQ | senzor -> hub | 10 |
| `0x11` | JOIN_ACCEPT | hub -> senzor | 3 |
| `0x12` | DATA_UP | senzor -> hub | 13 |
| `0x13` | CMD_DOWN | hub -> senzor | 4 |

Cele cinci lungimi sunt distincte fiindca, fara MIC, perechea tip+lungime este
singura verificare impotriva unei desincronizari intre capete: un pachet de
format vechi este respins de `messageType()` pe hub si de `LoRa_Receive()` pe
senzor, in loc sa fie citit la offset-uri gresite si sa scoata o temperatura
plauzibila si gresita, in tacere.

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
Nu mai exista AppKey, SessKey, DevNonce sau JoinNonce (F-038).

**Note care nu se pot deduce din layout:**
- `JOIN_REQ` poarta `DevEUI`, nu numarul: el este cheia dupa care hub-ul
  verifica provisioning-ul si deriva numarul. Altfel senzorul si-ar declara
  singur adresa (F-037).
- `DevAddr` in clar in `JOIN_ACCEPT` lasa senzorul sa filtreze fereastra de join
  pe adresa. O placa programata cu un numar care nu corespunde pozitiei ei din
  tabel **isi refuza singura JOIN_ACCEPT-ul**, cu trei clipiri pe LED2.
- `DevAddr` din `DATA_UP[2]` raspunde la "de la cine vine data", dar raspunsul
  este **declarativ**, nu dovedit (F-038).
- **Checksum-ul XOR nu este apararea de integritate** — aceea este CRC-ul LoRa.
  Exista ca pachetul de temperatura sa ramana bit cu bit cel vechi. Daca nu
  trece desi CRC-ul LoRa a fost bun: ori un emitator strain pe aceiasi
  parametri, ori un capat ramas pe firmware vechi.
- `CMD_DOWN` se **retrimite** la fiecare pachet al unui device marcat: un
  downlink are o singura sansa, fiindca senzorul asculta doar
  `DOWNLINK_WINDOW_MS` = 600 ms dupa fiecare transmisie (F-031). La `RESET`
  senzorul trece in `DEV_STATE_IDLE`; reintrarea cere `pair` pe hub **plus**
  trei secunde pe butonul 2 (F-030).
- **Cei 5 senzori nu vorbesc odata.** Fara arbitraj si fara sloturi: fiecare
  doarme `SLEEP_WAKEUPS_BASE (11) + (DevAddr - 1) + jitter 0..3` treziri
  (tabelul din §9). Intervalul propriu desparte doi senzori ciocniti, jitter-ul
  rupe pornirea simultana dupa o pana de curent (F-036). Coliziunile se deduc
  din **golurile de frame counter**, in coloana `pierd.` din tabelul afisat de
  `status`; un salt peste `SENSOR_FCNT_GAP_RESTART` este raportat ca repornire,
  nu ca pierderi.

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

Regiunea HEF este rezervata din linker cu `--ROM=default,-f80-fff`
(proprietatea `code-model-rom`); fara ea, linkerul plaseaza cod acolo si prima
scriere in HEF isi sterge propriul program (F-027).

### 4.2. Hub — NVS prin `Preferences`

Registrul traieste in spatiul NVS `solvix-pair` si are exact `HUB_MAX_SENSORS`
locuri. Fiecare inregistrare tine `DevEUI`, `DevAddr`, `lastFrameCounterUp`,
`hasUplink`, `downCounter`, pachete primite si **pierdute** (`lostPackets`),
starea dezinrolarii (`pendingReset`, `resetAttempts`, `resetSentMs`) si ultima
masuratoare (`lastTempX100`, `lastRssi`, `hasReading`). Nu tine nicio cheie
(F-038).

**Sase campuri sunt relative la sesiunea curenta si se pun pe 0 / `false` la
incarcarea din NVS:** `lastSeenMs`, `resetSentMs`, `hasReading`,
`lastTempX100`, `lastRssi`, `offlineReported`. Pentru `resetSentMs` nu e
curatenie: `0` inseamna "niciun RESET trimis in sesiunea asta", iar fara
zeroizare orice dezinrolare in curs ar aparea confirmata imediat dupa fiecare
repornire (F-031).

Se salveaza la fiecare inrolare, la fiecare stergere si o data la
`REGISTRY_SAVE_EVERY` (20) pachete — NVS este flash.

**Pretul cresterii lui `REGISTRY_BLOB_VERSION`:** hub-ul porneste cu registrul
gol in timp ce senzorii isi pastreaza starea in HEF; ei continua sa emita,
apar ca `DevAddr ... nu este inrolat`, si fiecare trebuie reinrolat manual —
numarul primit inapoi ramane insa acelasi (F-037).

Identitatea hub-ului sta separat, in `solvix-hub` (vezi `HubIdentity` in §5.2
si §7).

---

## 5. Ce face fiecare fisier

### 5.1. `senzor/` — proiect MPLAB X

| Fisier | Rol |
|--------|-----|
| `main.c` | **Firmware-ul complet**, in 16 sectiuni numerotate: parametri, pini, registre SX1276, protocol, HEF, `Word32`, starea device-ului, NVM, driver LoRa (TX **si** RX), ADC+NTC, butoane, LED-uri, construirea pachetelor, initializare, inrolare, bucla principala cu `IDLE`/`JOINING`/`OPERATING`. **Singura linie care difera intre cele cinci placi este `SENSOR_NODE_ID`.** `LoRa_Receive()` filtreaza dupa tip, LUNGIME si `devAddr` si este singurul punct de validare a receptiei (F-035); `Rand8()` da jitter-ul din F-036. Conversia NTC este un tabel de cautare cu interpolare pe 16 biti, 25 de intrari de la −20 la +100 °C, cu 8 citiri ADC mediate (F-016, F-028) |
| `mcc_generated_files/system/src/config_bits.c` | `FOSC=INTOSC`, **`WDTE=SWDTEN`**, `MCLRE=ON`, `BOREN=ON`, `LVP=ON`, `PWRTE=OFF`. **`WRT=OFF` este obligatoriu pentru HEF.** Fisier generat de MCC: o regenerare pune `WDTE` inapoi pe `OFF` si senzorul nu se mai trezeste |
| `.../clock.c`, `pins.c`, `mssp.c`, `system.c` | Oscilator intern 16 MHz; `TRISA=0x3F`, `TRISB=0xB0`, `TRISC=0x37`, `ANSELA=0x17`, `ANSELB=0x20`, `ANSELC=0x06`; SPI la 125 kHz (`SSP1CON1=0x0A`, `SSP1ADD=0x1F`) |
| `.../interrupt.c` | Vector generat; nu este folosit efectiv |
| `nbproject/`, `Makefile*` | Doua setari obligatorii, deja aplicate: `optimization-level = -O2` si `code-model-rom = default,-f80-fff` (F-027) |

### 5.2. `hub/SolvixHub_Tests/` — sketch Arduino

Hub-ul face doua lucruri: se inregistreaza in retea (bootstrap-ul in cloud,
apoi un heartbeat periodic) si vorbeste cu senzorii. Suita de teste a disparut
la 2026-09-01 (F-039), iar comenzile de diagnostic si codul ramas fara apelanti
la aceeasi data (F-046).

Consola are **trei comenzi**: `pair`, `status`, `remove`. `status` arata tot:
tabelul senzorilor, o linie de retea, una de cloud, una de **puls** (ultimul
heartbeat reusit) si identitatea hub-ului, cu `pairingCode` intreg si **fara
`apiKey`**.

Sketch-ul are **13 module**, fiecare cu perechea `.h` (ce ofera) / `.cpp` (cum
face), plus `Config.h` si `.ino` — 28 de fisiere. `SensorPacket` ramane modul
separat fiindca este oglinda sectiunii 4 din `senzor/main.c`; `SpiBus` fiindca
este o constrangere fizica a placii.

| Fisier | Rol |
|--------|-----|
| `SolvixHub_Tests.ino` | Doar `setup()`, `loop()` si butonul 1. `setup()` merge in ordinea Leds -> SpiBus -> registru -> identitate -> **SensorLink (radioul asculta)** -> NetLink -> HubCloud -> HubHeartbeat -> consola: radioul porneste inaintea retelei, iar **esecul retelei nu opreste boot-ul**. `loop()` nu contine nimic blocant |
| `Config.h` | **Singura sursa de adevar pentru pini** si constante: SPI, ETH, LoRa, butoane, LED-uri, pairing (`PAIRING_MODE_TIMEOUT_MS`, `PAIRING_SEND_ACK`, `REMOVE_CONFIRM_SILENCE_MS`, `REGISTRY_*`), multi-senzor (`HUB_MAX_SENSORS`, `SENSOR_OFFLINE_MS`, `SENSOR_FCNT_GAP_RESTART`, `PROVISIONED_DEVICES_INIT` — **ordinea randurilor da numarul fiecarui senzor**), RETEAUA si CLOUD (`HUB_NET_TRANSPORT`, `ETH_DHCP_*`, parametrii de fabrica ai hub-ului, `CLOUD_*`, `HTTP_*`, `IDENTITY_NVS_NAMESPACE`), HEARTBEAT (`CLOUD_PATH_HEARTBEAT`, `CLOUD_API_KEY_HEADER`, `HEARTBEAT_MIN_INTERVAL_S` / `HEARTBEAT_MAX_INTERVAL_S` — marginile in care se accepta ritmul cerut de server) |
| `SpiBus.*` | Arbitrajul magistralei partajate: `begin()` o singura data, `claimEthernet()`/`claimLoRa()`, resetul celor doua module. Sunt **preconditii, nu lacate** — bibliotecile isi coboara singure CS-ul |
| `Console.*` | `printSeparator()` si `printTitle()` |
| `LoRaRadio.*` | Invelis peste libraria LoRa: `begin()`, `sendRaw()`, `receiveRaw()`, `sleep()`. Numai variante binare, fara `String` (F-019). Receptia e prin polling |
| `Leds.*` | Cele doua LED-uri. `set()`, `pulse()`, `service()` fara `delay()` |
| `SensorPacket.*` | **Oglinda protocolului din `senzor/main.c`**: constantele tuturor tipurilor, `decode()`/`print()`/`printRaw()`, `messageType()`, `parseJoinRequest()`, `parseData()`, `buildJoinAccept()`, `buildCommand()`, `printEui()` |
| `DeviceRegistry.*` | Registrul pe NVS (`solvix-pair`); `isProvisioned()`, **`addressForEui()`** (numarul din pozitia in tabel, F-037), `printSensorTable()` — toate locurile, si cele goale |
| `SensorLink.*` | **Runtime-ul permanent** (fostul „test 8"): fereastra de pairing, `JOIN_REQ` -> `JOIN_ACCEPT`, `DATA_UP` -> `decode()`, `CMD_DOWN` (ACK/RESET), dezinrolarea confirmata prin tacere (F-031), golurile de frame counter, `serviceOfflineWatch()`. `lastRxMs()` si `hasPendingRemoval()` sunt **cele doua porti** prin care trece orice altceva lung din `loop()`; ACK-ul pleaca **inaintea** blocului de log (F-040) |
| `NetLink.*` | Reteaua, agnostica de transport. `acquireClient()` / `releaseClient()` intorc un `Client*` si sunt **si granita magistralei SPI**, deci `Http` nu afla niciodata pe ce transport merge. Ethernet azi, WiFi printr-un `#elif` mai tarziu, fara schimbari la apelanti. Aici e definit `HUB_MAC` |
| `Http.*` | GET si POST peste un `Client&`, fara niciun `String`: linie de status, antete plafonate, corp marginit, **de-chunker** (F-042), `Retry-After`. Numele nu este `HttpClient.h`, ca sa nu ascunda antetul bibliotecii cu acel nume (aceeasi capcana ca F-021) |
| `HubIdentity.*` | Identitatea primita de la cloud, in NVS (`solvix-hub`, separat de registru): `hubGuid`, `apiKey`, `pairingCode`, `lifecycleStatus`, `provisionedAt`, `maxSensors` si valorile de `config` (11 de la provisioning, 12 de la `/api/device/config`) — se folosesc **doua**, `heartbeatIntervalSeconds` si `heartbeatTimeoutSeconds`. `storeConfig()` inlocuieste **numai** blocul de configurare, fara sa atinga versiunea. **`maxOfflineMessages` sta ULTIMUL in `HubConfig`** (vezi §7). Versiunea se scrie **ultima** si se sterge **prima**, ca o identitate pe jumatate scrisa sa arate ca una lipsa. Dimensiunile campurilor isi poarta marja in comentariu (F-044) |
| `HubCloud.*` | Masina de stari a bootstrap-ului: `NetWait` -> `Health` -> `Provision` -> `Ready`, cu backoff 5/10/30/60 s si contoare separate pe cele doua cai (F-043). Sanatatea se judeca dupa `"database": "Reachable"`, nu dupa `"status"`. Un 429 primeste pauza lui lunga; `Blocked` este o asteptare de 30 de minute, nu o fundatura (F-046). Cererile sunt blocante dar trec prin cele doua porti din `SensorLink` |
| `HubHeartbeat.*` | `POST /api/device/heartbeat` la fiecare `heartbeatIntervalSeconds`, autentificat cu `X-Solvix-ApiKey`. Porneste abia din `HubCloud::Ready`. Detaliile de comportament: §7 |
| `SerialConsole.*` | Cele trei comenzi. Citeste **cel mult 32 de octeti per apel** si incheie linia si dupa liniste, nu doar la Enter — altfel pe „No line ending" nu s-ar executa nimic (F-045) |
| `README.md` | Instructiuni de utilizare, comenzile, secventa de pornire, tabelul SPI, note hardware |

---

## 6. Incadrarea in memorie — masurata, nu presupusa

### Senzor — PIC16LF1508

| Configuratie | Flash (words) | RAM (octeti) |
|---|---|---|
| PIC16LF1508 are | 4096 (**3968 utilizabili**, HEF rezervat) | 256 |
| **Production, `-O2`** | **2395** | **95** |
| **Debug cu Snap, `-O2`** | **2396** | **95** (din care 16 ai depanatorului) |

**Marja: 1573 de cuvinte si 161 de octeti.** Memoria nu mai este constrangerea
dominanta. Ultimul cuvant de cod este la `0x0E83`, deci regiunea HEF
(`0x0F80`–`0x0FFF`) este curata.

Cifrele sunt din `xc8-cc` v3.10 si sunt **identice pentru toate cele cinci
valori** ale lui `SENSOR_NODE_ID`.

> **Doua setari obligatorii, din fereastra de proprietati a proiectului MPLAB X
> — nu editand fisiere** (IDE-ul rescrie `Makefile-default.mk` la fiecare
> build, F-029):
> - *XC8 Compiler → Optimizations → Optimization level* = **`-O2`**
> - *XC8 Linker → Memory model → ROM ranges* = **`default,-f80-fff`**
>   (fara asta, linkerul pune cod peste HEF si prima inrolare isi sterge
>   propriul program — F-027)

### Hub — ESP32

Sketch-ul compileaza pentru ESP32 Dev Module fara erori si fara warning-uri
proprii (cele ramase sunt din `EthernetENC` si `LoRa`) si ocupa **349,8 kB din
1310 kB** de flash, cu **25,6 kB** de RAM global.

Cifra era 351 kB inainte de 2026-09-01. Stergerea celor sapte teste (F-039) a
dat inapoi ~26 kB, `ArduinoJson` plus modulele noi de retea au adaugat ~31 kB,
iar curatenia din F-046 a mai scos ~9 kB. Sketch-ul are acum **~5000 de linii**.

> **Atentie la WiFi, cand va veni.** Stiva ESP32 de WiFi adauga 350–500 kB si
> ar duce sketch-ul pe la 800–900 kB. Incape in 1310 kB, dar **inchide usa
> OTA**: o schema cu doua partitii de aplicatie da fiecareia ~640 kB, si 900
> nu intra. Iar `autoFirmwareUpdate` este un camp pe care serverul chiar il
> trimite in `config`. **Schema de partitii se decide inainte de WiFi, nu
> dupa.**

---

## 7. Hub-ul in cloud — ce este adevarat acum

Hub-ul porneste singur, asculta senzorii, isi ia adresa prin DHCP si se
provizioneaza la `http://84.117.97.136:7039`. Doi pasi de pornire, in ordine,
si apoi bate:

1. `GET /api/health` cu antetul `X-Solvix-AdminKey`. **Sanatatea se judeca
   dupa campul `database` = `Reachable`**, nu dupa `status`: API-ul poate
   raspunde perfect cu baza de date cazuta. La esec: backoff 5 / 10 / 30 / 60 s,
   apoi 60 s la nesfarsit.
2. `POST /api/device/provision` cu `deviceUid`, `serialNumber`,
   `provisioningSecret` si `firmwareVersion` din `Config.h`. Raspunsul —
   `hubGuid`, `apiKey`, `pairingCode`, `lifecycleStatus`, `provisionedAt`,
   `maxSensors` si cele 11 valori de `config` — se salveaza in NVS
   (`solvix-hub`, separat de registrul senzorilor). **A doua pornire nu mai
   cere nimic.**

**Masurat cu `curl` la 2026-09-01, si ambele lucruri conteaza (F-042):**
- serverul raspunde `Transfer-Encoding: chunked`, **fara** `Content-Length`,
  deci de-chunker-ul din `Http.cpp` este obligatoriu, nu o precautie;
- `X-Solvix-AdminKey` **nu** este ceruta la `/api/device/provision`: aceeasi
  cerere cu si fara ea primeste acelasi 401 de provisioning. Se trimite
  totusi, `CLOUD_PROVISION_SENDS_ADMIN_KEY` = 1; trecerea pe 0 este sigura si
  ar scoate cheia globala din fiecare hub din teren.

### Pasul 3: heartbeat-ul

Dupa provisioning hub-ul bate singur: `POST /api/device/heartbeat` la fiecare
`heartbeatIntervalSeconds`, autentificat cu **`X-Solvix-ApiKey`** si cheia per
hub din NVS. Corpul nu poarta niciun identificator, deci antetul acela este
singurul lucru care spune serverului cine bate.

- **8 din cele 20 de campuri se masoara**: `uptimeSeconds`,
  `internetAvailable`, `cloudReachable`, `connectionType`, cele trei numere de
  senzori, `freeMemoryBytes`. Restul (baterie, alimentare, `cpuTemperature`,
  `freeStorageBytes`, `publicIp`, `wifiSignalStrength`) sunt hardware
  inexistent si se trimit zero / sir gol, cu presupunerea scrisa langa fiecare.
- **Ritmul serverului nu este crezut pe cuvant:** si
  `heartbeatIntervalSeconds`, si `nextHeartbeatInSeconds` trec prin
  `HEARTBEAT_MIN_INTERVAL_S` = 15 s si `HEARTBEAT_MAX_INTERVAL_S` = 3600 s. Cat
  dureaza o cerere hub-ul este surd, iar tavanul opreste rasturnarea inmultirii
  cu 1000. (Review-ul din 2026-09-26 a gasit ca tavanul nu se aplica pe
  toate caile — vezi §11.)
- **`heartbeatTimeoutSeconds`** este toleranta serverului: se verifica la
  pornire ca ritmul incape in ea, si in rulare se anunta o data caderea si o
  data revenirea. Fara linia asta, „hub-ul apare mort in aplicatie" ar fi
  singura defectiune din tot lantul complet invizibila de pe placa.
- La `configUpdateRequired` se cere **`GET /api/device/config`** (fara corp,
  aceeasi cheie), se salveaza prin `HubIdentity::storeConfig()` si ritmul se
  schimba pe loc. `pendingCommandCount` doar se raporteaza: pentru comenzi nu
  exista inca endpoint.
- **Cel mult o cerere per `tick()`** si **cel mult o preluare per
  `configVersion` anuntata** — a doua este garda impotriva unui server care
  nu stinge steagul si ne-ar tine intr-un ciclu fara capat.
- **`maxOfflineMessages` sta ULTIMUL in `HubConfig`.** Vine doar din
  `/api/device/config`, deci a aparut dupa ce existau hub-uri cu identitatea
  salvata. Fiindca e la coada, blobul vechi de **20** de octeti se citeste
  corect peste structura de **22** (`getBytes` accepta un blob mai scurt,
  `loadFromNvs` face `memset` inainte), campul nou ramane 0, si
  **`IDENTITY_BLOB_VERSION` a ramas 1 — nimic nu se re-provizioneaza**.
  Verificat cu `static_assert` pe compilatorul xtensa: toate offset-urile
  vechi neschimbate. Un camp inserat la mijloc ar sparge exact asta.

**Din valorile de `config` se folosesc doua** — cele doua de heartbeat.
Restul de zece se salveaza si asteapta telemetria in loturi. La fel
`maxSensors` de la server: se salveaza, se compara cu `HUB_MAX_SENSORS` si se
anunta nepotrivirea, dar valoarea locala ramane cea care dimensioneaza
registrul.

**Stare la 2026-09-01, prima rulare cu serverul real:** reteaua, DHCP-ul,
HTTP-ul si parsarea merg cap-coada — `GET /api/health` intoarce 200 in ~300 ms
si se citeste corect. Provisioning-ul insa primeste **429, "prea multe
incercari esuate pentru acest device"**: serverul a limitat acest `deviceUid`
dupa esecuri anterioare. Hub-ul asteapta acum 15 minute intre incercari
(sau cat cere `Retry-After`) si se opreste de tot dupa 5 esecuri consecutive,
in loc sa reincerce la fiecare 11 secunde si sa-si intretina singur blocajul
(F-043). **Cauza esecurilor de dinainte de limitare nu este inca stabilita** —
se va vedea la prima incercare de dupa expirarea ferestrei.

**Ce nu s-a confirmat inca la backend:** este `/api/device/provision`
idempotent pentru un `deviceUid` deja cunoscut? De raspunsul asta depinde daca
`IDENTITY_BLOB_VERSION` are voie sa creasca vreodata. Pana atunci, codul nu
re-provizioneaza niciodata singur.

---

## 8. Versiuni de format

Ambele capete pornesc golite impreuna.

| Constanta | Valoare | Unde |
|---|---|---|
| `REGISTRY_BLOB_VERSION` | **4** | `hub/SolvixHub_Tests/DeviceRegistry.h` |
| `HEF_MAGIC_SESSION` | **`0xC4`** | `senzor/main.c` |
| `LORA_RX_BUFFER_LEN` (= `RegMaxPayloadLength`) | **6** | `senzor/main.c` |
| `HUB_MAX_SENSORS` | **5** | `hub/SolvixHub_Tests/Config.h` |
| `REMOVE_CONFIRM_SILENCE_MS` | **180000** (180 s) | `hub/SolvixHub_Tests/Config.h` |
| `SENSOR_OFFLINE_MS` | **150000** (150 s) | `hub/SolvixHub_Tests/Config.h` |
| `IDENTITY_BLOB_VERSION` | **1** | `hub/SolvixHub_Tests/Config.h` |

Lungimile pachetelor, **distincte si obligatoriu asa**: TEMP_PLAIN 6 ·
JOIN_REQ 10 · JOIN_ACCEPT 3 · DATA_UP 13 · CMD_DOWN 4.

La trecerea pe `REGISTRY_BLOB_VERSION = 4`, `HEF_MAGIC_SESSION` s-a schimbat in
acelasi commit — deci ambele capete au pornit golite simultan si nu a fost
nevoie de nicio recuperare pe teren (capcana 2 din F-038).

---

## 9. Ce se schimba la fiecare placa

**O singura linie**, in `senzor/main.c`:

```c
#define SENSOR_NODE_ID          N        /* N = 1..5 */
```

Din ea ies `PROVISION_DEV_EUI` (`"SOLVIX" | 0x00 | N`) si slotul de somn. Pe
hub nu se schimba nimic: `PROVISIONED_DEVICES_INIT` din `Config.h` este deja
completat pentru toate cele 5 placi, iar **ordinea randurilor da numerele
senzorilor** — nu se rearanjeaza intr-o retea deja instalata (F-037).

**In copia de lucru de acum, `SENSOR_NODE_ID` este 3.**

Numarul este stabil peste dezinrolari, reinrolari si goliri de registru, deci
**poate fi scris pe cutie**. O placa deja folosita poate fi reprogramata cu alt
numar fara nicio procedura speciala: firmware-ul observa la pornire ca
`DevEUI`-ul din HEF nu mai este cel compilat, rescrie identitatea si sterge
inrolarea veche; placa porneste in repaus si asteapta o inrolare noua.

**Intervalele de somn** ies tot din numar:

| Senzor | Treziri | Interval nominal | Cu toleranta LFINTOSC |
|--------|---------|------------------|-----------------------|
| #1 | 11..14 | 23,2 – 29,6 s | 20 – 34 s |
| #2 | 12..15 | 25,3 – 31,7 s | 22 – 36 s |
| #3 | 13..16 | 27,4 – 33,8 s | 23 – 39 s |
| #4 | 14..17 | 29,6 – 35,9 s | 25 – 41 s |
| #5 | 15..18 | 31,7 – 38,0 s | 27 – 44 s |

---

## 10. Dupa fiecare programare

1. **Verifica cifra din raportul de memorie** (fereastra de build sau
   `senzor/dist/default/production/senzor.production.mum`): trebuie sa fie
   **2395 / 95**. Alta cifra inseamna alt cod decat cel din `main.c` — si nicio
   cautare in schema nu are rost pana nu se potriveste (F-033).
2. **Verifica pe hub ca placa apare cu numarul asteptat:** comanda `status`.
   Doua placi programate din greseala cu acelasi `SENSOR_NODE_ID` au acelasi
   `DevEUI`, iar a doua o inlocuieste pe prima in registru.

---

## 11. Ramas de facut

- **Heartbeat-ul si actualizarea de config nu au fost incercate inca pe
  serverul real.** Compileaza si sunt cablate in `loop()`, dar nu pot porni
  pana cand provisioning-ul nu trece: pana atunci nu exista `apiKey`, iar
  `apiKey` este singura autentificare a ambelor endpoint-uri. Prima rulare de
  dupa expirarea ferestrei de rate limiting le valideaza pe toate deodata. De
  verificat atunci, in ordine:
  1. antetul `X-Solvix-ApiKey` este acceptat si la `/api/device/heartbeat`, si
     la `/api/device/config`;
  2. schema corpului de heartbeat este cea asteptata de server;
  3. ce valori vin efectiv in `heartbeatIntervalSeconds` /
     `heartbeatTimeoutSeconds` — daca a doua nu o depaseste confortabil pe
     prima, hub-ul striga la pornire si configul trebuie reparat pe server;
  4. **stinge serverul `configUpdateRequired` dupa un GET pe
     `/api/device/config`?** Asa este presupus. Daca nu il stinge, garda pe
     `configVersion` opreste ciclul si scrie pe Serial exact asta.
- **Gasite la review-ul din 2026-09-26 (commit `4d3bcff`), nereparate inca**
  — fiecare cere o intrare `F-0xx` cand se repara:
  - `NetLink::retry()` a ramas fara apelant: un DHCP esuat la boot nu se mai
    reincearca niciodata (`maintain()` iese devreme cat `!s_up`);
  - `Retry-After` dintr-un 429 la heartbeat nu trece prin
    `HEARTBEAT_MAX_INTERVAL_S` (depasire la `* 1000UL`);
  - `applyRhythm()` aplica doar podeaua, nu si tavanul, pe
    `heartbeatIntervalSeconds`;
  - primul heartbeat pleaca in aceeasi trecere prin `loop()` cu cererea
    `HubCloud` care tocmai s-a terminat (doua cereri blocante la rand);
  - 401/403 la heartbeat si un GET de config esuat se reincearca la nesfarsit,
    la 60 s;
  - fara `reboot`, nu mai exista oprire ordonata care sa salveze registrul
    (pana la 19 pachete de contoare pierdute la repornire);
  - mesajul de boot din `SolvixHub_Tests.ino` inca spune `Scrie 'sensors'`;
  - `printProblemDetail` este duplicat in `HubCloud.cpp` si `HubHeartbeat.cpp`;
    comentarii ramase pentru functii sterse (`LoRaRadio.h`, `SensorLink.cpp`,
    `DeviceRegistry.h`).
- **Restructurarea hub-ului in mai putine fisiere** — propusa la 2026-09-26
  (grupare pe domenii, 28 -> ~14 fisiere), asteapta acordul.
- `PINOUT_config.pdf` inca arata **RC1 -> TPL5110**. Componenta a fost scoasa
  din proiectare la 2026-08-26; RC1 este acum un pin liber, fara cod.
