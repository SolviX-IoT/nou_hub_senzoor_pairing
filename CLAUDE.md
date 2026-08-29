# CLAUDE.md — SolviX HUB / Pairing fara criptare

> Fisier de context permanent pentru Claude Code si pentru orice om care
> intra in proiect. **Se actualizeaza la FIECARE commit**: vezi sectiunea
> [Regula de actualizare](#12-regula-de-actualizare) de la final.
>
> Acest proiect (`teste_pairing/`) **porneste din `teste-sistemcomplet/`**
> si pastreaza aceeasi arhitectura: un folder `senzor/` (proiect MPLAB X)
> si un folder `hub/` (sketch Arduino). Peste functionalitatea de
> temperatura, care ramane intacta, se adauga **inrolarea de device-uri
> (pairing), un registru de senzori pe hub si stergerea unui device**.
>
> **Criptografia a fost SCOASA la 2026-08-29 (F-038)**: nu mai incapea
> in PIC16LF1508. Reteaua nu mai este autentificata — citeste sectiunea
> 2, punctul 1, inainte de a pune sistemul in exploatare.

---

## 1. Despre ce este proiectul

Un sistem cu **doua noduri** care comunica radio prin **LoRa** in banda
europeana de **868 MHz**:

| Nod | Hardware | Toolchain | Rol |
|-----|----------|-----------|-----|
| **Senzor** | PIC16LF1508 + RFM96 (SX1276) + NTC 10K 3950 | MPLAB X IDE, compilator XC8, drivere MCC Melody | Se inroleaza la hub, apoi masoara temperatura si o trimite **in clar** prin LoRa. Inrolat, **doarme intre transmisii**, un interval propriu numarului lui (~23–38 s) |
| **Hub** | ESP32 Dev Module + RFM96 (SX1276) + ENC28J60 | Arduino IDE | Inroleaza si tine **pana la 5 senzori** simultan, cu registrul lor in NVS; primeste datele fiecaruia, poate dezinrola un device |

**Reteaua are pana la `HUB_MAX_SENSORS` = 5 senzori**, fiecare cu un
numar fix de la 1 la 5. Numarul este chiar `DevAddr`-ul din protocol si
vine din pozitia placii in tabelul de provisioning, nu din ordinea
inrolarii — vezi sectiunea 5.10.

Ambele module radio sunt SX1276, deci parametrii radio trebuie sa fie
**identici bit cu bit** pe cele doua capete, altfel pachetele nu se vad.

### Ce s-a adaugat fata de `teste-sistemcomplet/`

1. **Pairing** — un senzor ne-inrolat se alatura hub-ului si primeste
   numarul lui (`JOIN_REQ` / `JOIN_ACCEPT`). Este o **comisionare**
   (cine e in retea, ce numar are, de unde incep contoarele), **nu un
   control de acces**: fara criptografie, apartenenta la lista de
   provisioning este declarata, nu dovedita.
2. **Date** — dupa inrolare, temperatura circula ca `DATA_UP`, in clar,
   cu adresa senzorului si un frame counter strict crescator. Pana la
   2026-08-29 circula criptata cu XTEA-CTR si semnata cu CBC-MAC-XTEA;
   vezi F-038 pentru de ce a fost scoasa si commit-ul `a710142` pentru
   ultima versiune care o contine.
3. **Registru pe hub** — lista senzorilor inrolati, salvata in NVS, deci
   supravietuieste repornirii hub-ului.
4. **Stergere confirmata** — `remove <DevEUI>` marcheaza device-ul, iar
   hub-ul ii trimite `CMD_DOWN(RESET)` la **fiecare** pachet al lui.
   Inregistrarea se sterge abia dupa ce senzorul
   **tace** `REMOVE_CONFIRM_SILENCE_MS` — tacerea este dovada ca a primit
   comanda. Varianta care stergea din prima lasa senzorul blocat in retea
   daca acel unic downlink se pierdea (F-031).
5. **Receptie pe senzor** — driverul LoRa al senzorului era doar
   emitator; acum are si `LoRa_Receive()`.
6. **Memorie ne-volatila pe senzor** — HEF (High-Endurance Flash), pentru
   identitate, starea de inrolare si frame counter.
7. **Somn intre transmisii** — inrolat, senzorul nu mai sta in veghe:
   ciclul este *masoara -> `DATA_UP` -> fereastra de downlink -> radioul
   in sleep -> microcontrolerul in sleep ~30 s -> trezire*. Somnul se
   aplica **doar** in `DEV_STATE_OPERATING`; in repaus si in pairing
   senzorul ramane treaz, ca butonul 2 sa raspunda normal.
8. **Pairing manual pe senzor** — senzorul nu se mai inroleaza singur.
   Ne-inrolat sta in repaus si tace; fereastra de pairing se deschide
   tinand **butonul 2 (RC5) apasat ~3 secunde**, iar LED2 clipeste cat
   este deschisa. Fereastra se inchide dupa `PAIRING_MAX_ATTEMPTS`
   incercari de join. Simetric cu hub-ul, care si el asculta `JOIN_REQ`
   doar in fereastra deschisa manual cu `pair`.
9. **Mai multi senzori pe acelasi hub** — pana la 5, fiecare cu un numar
   stabil 1..5 care este si `DevAddr`-ul lui. Ca sa nu vorbeasca toti
   odata, fiecare doarme un interval propriu numarului sau, plus un
   jitter aleator la fiecare ciclu; hub-ul numara pachetele pierdute si
   anunta senzorii care amutesc. Vezi sectiunea 5.10.

### Structura folderelor

```
teste_pairing/
├── CLAUDE.md                <- acest fisier
├── PINOUT_config.pdf        <- schema de conexiuni a placilor
├── README.md                <- prezentarea proiectului
├── senzor/                  <- proiect MPLAB X: firmware-ul nodului senzor
└── hub/
    └── SolvixHub_Tests/     <- sketch Arduino: suita de teste, meniu pe Serial
```

Numele folderului `SolvixHub_Tests` **nu este optional**: Arduino IDE cere
ca folderul sketch-ului si fisierul `.ino` principal sa aiba acelasi nume.
Toate fisierele hub-ului stau inauntru, plat, fara subfoldere — Arduino IDE
le arata ca tab-uri.

**Nu se creeaza foldere paralele pentru functionalitate noua.** Codul nou
intra in `senzor/` si `hub/`, in structura lor existenta: pe hub, un
modul `.h`/`.cpp` cu namespace plus un test inregistrat in meniu; pe
senzor, direct in `main.c`, peste driverele generate de MCC. Un folder
paralel ar duplica definitiile de pini si initializarea driverelor, iar
cele doua copii ar diverge — nu ar mai exista o singura sursa de adevar
pentru hardware (F-020).

---

## 2. Constrangeri care au modelat implementarea

Acestea vin din hardware-ul real si explica de ce solutia nu arata ca o
retea LoRa "de manual".

1. **Flash si RAM foarte mici pe senzor — cifre masurate, nu estimate.**
   `PIC16LF1508` are **4096 de cuvinte de flash si 256 de octeti de RAM**
   (`ROMSIZE=1000`, `RAMBANK=20-7F,A0-EF,120-16F` in fisierul de device
   support al lui XC8: 240 de octeti bancati + 16 comuni). O versiune
   anterioara a acestui fisier scria 512 B de RAM — era gresit, si din
   acea eroare a pornit toata problema de incadrare.

   **AICI S-A CONSUMAT TOATA MARJA, si de aceea nu mai exista
   criptografie in proiect.** Istoricul incadrarii, pe scurt:

   | varianta | flash (words) | RAM (octeti) |
   |----------|---------------|--------------|
   | AES-128, cum a fost scrisa initial, `-O2` | 5426 | 446 |
   | AES cu toate solutiile de rezerva aplicate simultan | 4325 | 286 |
   | XTEA-128, dupa F-024 | 3761 | 250 |
   | XTEA + pairing manual + somn + 5 senzori | **3876** | **232** |
   | **fara criptografie (acum)** | **2395** | **95** |

   AES nu incapea nici macar cu toate solutiile de rezerva: doar cele
   doua tabele de substitutie ocupa 512 cuvinte, un sfert din tot
   flash-ul, inainte de orice linie de cod (F-024). XTEA-128 a incaput,
   dar la 3876 din 3968 de cuvinte utilizabile — **92 de cuvinte marja**
   — nu mai incapea nicio functionalitate noua. La 2026-08-29
   criptografia a fost scoasa (**F-038**), ceea ce a eliberat **1481 de
   cuvinte si 137 de octeti**.

   **PRETUL, care trebuie stiut inainte de a pune sistemul in
   exploatare: reteaua NU mai este autentificata.** Nu exista MIC, cheie
   sau nonce. Oricine are un radio LoRa cu aceiasi parametri poate
   injecta o temperatura falsa pentru orice senzor, poate dezinrola orice
   placa cu patru octeti (`A5 13 <DevAddr> 02`), poate inrola o placa
   falsa cat timp fereastra de pairing este deschisa (DevEUI-ul este
   ghicibil: `"SOLVIX" | 0x00 | numar`), si poate rejuca orice pachet
   capturat. Singura limitare ramasa pe calea de date este frame
   counter-ul strict crescator. In plus, `RegSyncWord` ramane `0x12`,
   valoarea implicita a bibliotecii: inainte, MIC-ul filtra si emitatorii
   straini care nimereau aceiasi parametri.

   **Nu compensa cu nimic facut in casa.** Un pseudo-MIC de o suta de
   cuvinte care nu opreste pe nimeni este mai rau decat o absenta
   onesta, fiindca cineva se va baza pe el. Criptografia se reintroduce
   la upgrade-ul de microcontroller, din commit-ul `a710142`, care este
   ultima stare care o contine.

   **`-O2` ramane obligatoriu si regiunea HEF ramane rezervata.** Cu
   marja de acum firmware-ul ar incapea probabil si cu `-O0`, dar cele
   doua setari nu se schimba "fiindca oricum e loc": rezervarea HEF este
   singurul lucru care garanteaza ca linkerul nu pune cod peste memoria
   ne-volatila (F-027).

   Daca vreodata nu mai incape nici asa, ramane migrarea pe
   **PIC16LF1509** (8K words, 512 B RAM), pin-compatibil — maparea de
   pini din sectiunea 3 ramane valabila bit cu bit, dar `HEF_BASE`
   devine `0x1F80`.

2. **`SLEEP` nu este acelasi lucru cu taierea alimentarii.** Distinctia
   asta decide schema frame counter-ului, si de aceea merita scrisa
   raspicat.

   **Ce se intampla acum:** inrolat, senzorul executa `SLEEP` intre
   transmisii si se trezeste pe watchdog. Durata **nu este aceeasi pe
   toate placile**: `SLEEP_WAKEUPS_BASE` + `(DevAddr - 1)` + un jitter
   aleator de 0..3 treziri, adica ~23 s pentru senzorul #1 fara jitter si
   pana la ~38 s pentru senzorul #5 cu jitter maxim (F-036). `SLEEP` pe
   PIC16 **pastreaza RAM-ul si registrele** — procesorul doar isi
   opreste ceasul. Deci schema din F-022 ramane valabila
   **neschimbata**: counter-ul traieste in RAM si se salveaza in HEF doar
   la fiecare `FCNT_CHECKPOINT_EVERY` = 50 de pachete. **Nu** se scrie la
   fiecare ciclu de somn; ar consuma HEF-ul degeaba.

   **Ce ar fi altceva:** un regim in care **alimentarea se taie** intre
   transmisii (un timer extern de tip PTC/latch care scoate VDD). Acolo
   fiecare trezire ar fi un cold boot cu RAM-ul pierdut: counter-ul ar
   trebui scris la fiecare ciclu, inelul de sloturi din HEF ar trebui
   marit (sau inlocuit cu un FRAM extern), iar inrolarea ar trebui
   incadrata intr-o fereastra de alimentare. **Nu este cazul astazi.**

   Somnul se aplica doar in `DEV_STATE_OPERATING`. In `DEV_STATE_IDLE` si
   in `DEV_STATE_JOINING` senzorul ramane treaz, fiindca acolo trebuie sa
   asculte butonul 2 si sa poata face join.

3. **Senzorul era doar TX, cu polling, fara DIO0 si fara RESET cablat.**
   Pentru pairing a trebuit adaugat mod RX in driver: `LoRa_Receive()`,
   cu RX continuu si polling pe `RxDone` / `PayloadCrcError`, citire din
   `RegFifo` de la `FifoRxCurrentAddr`.

4. **Pe hub, SPI este partajat cu ENC28J60.** Tot traficul radio trece
   prin `SpiBus` (`claimLoRa()` / `SpiGuard`), receptia este prin polling
   cu `receiveRaw()`, emisia binara prin `sendRaw()`, LED-urile doar prin
   modulul `Leds`, si nu se apeleaza niciodata `LoRa.end()` / `SPI.end()`.

---

## 3. Maparea pinilor

**Pairing-ul NU adauga si NU muta niciun pin, si nici trecerea la mai
multi senzori nu o face.** Tabelele de mai jos sunt identice cu cele din
`teste-sistemcomplet/`. Cele cinci placi de senzor sunt **identice
hardware**; se deosebesc doar prin `SENSOR_NODE_ID` din `main.c`, adica
prin firmware.

### 3.1. Nod SENZOR — PIC16LF1508 (20 pini; TRIS/ANSEL din `PIN_MANAGER_Initialize`)

| Pin | Functie | Directie | Configurare | Sursa in cod |
|-----|---------|----------|-------------|--------------|
| **RB4** | **LoRa MISO** (SDI la PIC) | intrare | `TRISB=0xB0` bit4=1, digital | fix hardware pe PIC16F1508 |
| **RB5** | **LoRa NSS / CS** | iesire | `ANSELBbits.ANSB5=0`, `TRISB5=0`, inactiv HIGH | `main.c`, `LoRa_Select()` |
| **RB6** | **LoRa SCK** | iesire | fix hardware | MSSP1 |
| **RC7** | **LoRa MOSI** (SDO la PIC) | iesire | `TRISC=0x37` bit7=0 | fix hardware |
| **RC2** | **NTC 10K 3950** -> **AN6** | intrare analogica | `ANSELC=0x06` (ANSC1+ANSC2), `TRISC2=1` | `pins.c` |
| **RC4** | **Buton 1** (activ HIGH, pull-down extern) | intrare digitala | `TRISC4=1` | `main.c` |
| **RC5** | **Buton 2** — **tinut ~3 s deschide pairing-ul** (activ HIGH, pull-down extern) | intrare digitala | `TRISC5=1` | `main.c`, `ButtonPair_HeldLong()` |
| **RC3** | **LED 1** — transmisie de date; aprins cat dureaza si fereastra de downlink (F-032) | iesire | `ANSC3=0`, `TRISC3=0` | `main.c` |
| **RC6** | **LED 2** — pairing / eroare de join; **clipeste** cat fereastra de pairing e deschisa | iesire | `ANSC6=0`, `TRISC6=0` | `main.c` |
| **RC1** | **liber / neconectat** | intrare analogica (implicit MCC) | `ANSC1=1`, `TRISC1=1` din `pins.c`; **fara cod in `main.c`** | `pins.c` |
| RA0 / RA1 | ICSPDAT / ICSPCLK (programare) | — | `LVP=ON` | `config_bits.c` |
| RA3 | MCLR / VPP | intrare | `MCLRE=ON` | `config_bits.c` |

**Rolul LED-urilor s-a schimbat** fata de firmware-ul de temperatura:
inainte LED1 = transmisie periodica si LED2 = transmisie fortata de
buton; acum **LED1 = transmisie de date**: se aprinde la fiecare `DATA_UP`
emis si se stinge dupa inchiderea ferestrei de downlink - NU este un puls
blocant, fiindca ar intarzia receptia si ar face senzorul surd (F-032). Iar
**LED2 = pairing**: clipeste la 5 Hz cat timp butonul 2 este tinut apasat
si cat timp fereastra de pairing este deschisa, aprins continuu in timpul
unei incercari de join, trei clipiri scurte la esec, doua pulsuri la
reusita, un puls la primirea unui ACK.

**Butonul 2 (RC5) nu mai este liber.** `Button_RawPressed()` citeste in
continuare DOAR RC4, si blocul comentat pentru RC5 din interiorul ei
trebuie sa ramana comentat: daca RC5 ar forta si transmisii, cele trei
secunde de tinut apasat ar declansa in acelasi timp si fereastra de
pairing, si un sir de `DATA_UP`.

**NECONECTATE / NEDEFINITE IN COD (presupuneri documentate):**

- **LoRa RESET** — nu apare in codul senzorului. Se presupune legat la
  VDD sau lasat in aer (RFM96 are POR intern). Codul foloseste doar
  soft-reset prin `RegOpMode` -> SLEEP.
- **LoRa DIO0 / IRQ** — nu apare in cod. `TxDone` **si** `RxDone` se afla
  prin **polling pe `RegIrqFlags`**, nu prin intrerupere.
- **Switch** — nu exista niciun pin de switch. Cele doua butoane RC4/RC5
  sunt singurele intrari.
- **Timer hardware** — Timer0/1/2 nu sunt configurate deloc. Singura
  temporizare este `__delay_ms()` (software, `_XTAL_FREQ`).

### 3.2. Nod HUB — ESP32 Dev Module (`hub/SolvixHub_Tests/Config.h`)

| GPIO | Semnal | Modul | Observatie |
|------|--------|-------|------------|
| **18** | SCK | ENC28J60 **si** LoRa | magistrala SPI comuna |
| **19** | MISO | ENC28J60 **si** LoRa | magistrala SPI comuna |
| **23** | MOSI | ENC28J60 **si** LoRa | magistrala SPI comuna |
| **4** | CS_ETH | ENC28J60 | **NU este GPIO5** |
| **32** | RESET_ETH | ENC28J60 | activ pe LOW |
| **5** | NSS | LoRa SX1276 | |
| **14** | RST | LoRa SX1276 | activ pe LOW |
| **26** | DIO0 | LoRa SX1276 | dat librariei, dar nu se foloseste `onReceive()` |
| **34** | Buton 1 | — | **input-only**, rezistor extern obligatoriu; **deschide fereastra de pairing** |
| **35** | Buton 2 | — | **input-only**, rezistor extern obligatoriu |
| **22** | LED 1 (`PIN_LED_1`) | — | **D22**; activitate: pachet valid |
| **21** | LED 2 (`PIN_LED_2`) | — | **D21**; stare: aprins cat asculta, **clipeste** in mod pairing |

Butonul 1 este ascultat **doar** cand nu ruleaza niciun test sau cand
ruleaza chiar testul 8. Altfel o apasare in timpul testului 1 ar opri
exact testul care masoara butoanele, iar pe o placa fara rezistorul
extern de pe GPIO34 zgomotul ar comuta testele de unul singur (F-008).

Polaritatea LED-urilor este presupusa **activa HIGH**; daca pe placa sunt
cablate invers, se schimba `LED_ON_LEVEL` in `Config.h` — nicaieri
altundeva.

---

## 4. Parametrii radio LoRa (trebuie identici pe ambele capete)

**Pairing-ul nu schimba niciunul dintre ei.** Inrolarea si datele
criptate circula pe exact aceeasi modulatie ca pachetul de temperatura in
clar; se schimba doar continutul pachetelor.

| Parametru | Valoare | Registru SX1276 (senzor) | API Arduino (hub) |
|-----------|---------|--------------------------|-------------------|
| Frecventa | **868.0 MHz** | `RegFrf = 0xD9 00 00` | `LoRa.begin(868E6)` |
| Bandwidth | **125 kHz** | `RegModemConfig1 = 0x72`, biti 7:4 = 0111 | `LoRa.setSignalBandwidth(125E3)` |
| Coding rate | **4/5** | `RegModemConfig1`, biti 3:1 = 001 | `LoRa.setCodingRate4(5)` |
| Header | **explicit** | `RegModemConfig1`, bit0 = 0 | implicit in librarie |
| Spreading factor | **SF7** | `RegModemConfig2 = 0x74`, biti 7:4 = 0111 | `LoRa.setSpreadingFactor(7)` |
| CRC payload | **activ** | `RegModemConfig2`, bit2 = 1 | `LoRa.enableCrc()` |
| AGC automat | activ | `RegModemConfig3 = 0x04` | implicit |
| Sync word | **0x12** (valoarea de reset, nescrisa explicit pe senzor) | `RegSyncWord` neatins | `LoRa.setSyncWord(0x12)` |
| Preambul | 8 simboluri (reset) | neatins | `LoRa.setPreambleLength(8)` |
| Putere PA | ~14 dBm PA_BOOST | `RegPaConfig = 0x8F` | `LoRa.setTxPower(14, PA_OUTPUT_PA_BOOST_PIN)` |
| Lungime max. la RX | 16 octeti | `RegMaxPayloadLength = 0x10` **(NOU)** | implicit in librarie |

`RegMaxPayloadLength` este singurul registru adaugat: la receptie cu
header explicit, un pachet mai lung decat aceasta limita este abandonat
de modem. **6** este putin peste cel mai lung pachet pe care il PRIMESTE
senzorul (`CMD_DOWN`, 4 octeti; `JOIN_ACCEPT` are 3).

**Limita asta este si un filtru util pe gratis:** `DATA_UP` are 13 octeti
si `JOIN_REQ` are 10, deci modemul arunca singur, in hardware, pachetele
celorlalti senzori inainte ca firmware-ul sa le vada. Ce scapa de el —
`CMD_DOWN` adresat altcuiva — este filtrat in software, in `LoRa_Receive`
(F-035).

**SE RECALCULEAZA ORI DE CATE ORI SE SCHIMBA LUNGIMILE PACHETELOR.** Cat
timp `DATA_ENC` avea 17 octeti si limita era 16, filtrul exista din
intamplare; la scurtarea pachetelor (F-038) el ar fi disparut in tacere,
iar odata cu el jumatate din apararea ferestrei de downlink. Cine ridica
`RegMaxPayloadLength` peste lungimea celui mai lung pachet primit pierde
filtrul hardware si se bazeaza doar pe cel software.

**Nota critica:** senzorul nu scrie `RegSyncWord`, deci acesta ramane la
valoarea de reset **0x12**, care este exact valoarea implicita a
librariei Sandeep Mistry. Daca cineva schimba sync word-ul pe un capat,
trebuie schimbat obligatoriu si pe celalalt.

---

## 5. Protocolul de aplicatie

Toate campurile multi-octet sunt **big-endian**. Primul octet ramane
magic-ul `0xA5` din protocolul initial; ce s-a schimbat este ca octetul
`TYPE` are acum mai multe valori.

Protocolul este scris in **doua locuri care trebuie modificate
impreuna**: sectiunea 4 din `senzor/main.c` si
`hub/SolvixHub_Tests/SensorPacket.h`.

### 5.1. Tipurile de mesaj

| TYPE | Nume | Directie | Lungime |
|------|------|----------|---------|
| `0x01` | TEMP_PLAIN | senzor -> hub | 6 |
| `0x10` | JOIN_REQ | senzor -> hub | 10 |
| `0x11` | JOIN_ACCEPT | hub -> senzor | 3 |
| `0x12` | DATA_UP | senzor -> hub | 13 |
| `0x13` | CMD_DOWN | hub -> senzor | 4 |

**Cele cinci lungimi sunt DISTINCTE si trebuie sa ramana asa.** De cand
nu mai exista MIC, perechea tip+lungime este singura verificare
impotriva unei desincronizari intre capete: un pachet de format vechi
este respins de `messageType()` pe hub si de `LoRa_Receive()` pe senzor,
in loc sa fie citit la offset-uri gresite si sa scoata o temperatura
plauzibila si gresita, in tacere. Nu egala doua lungimi.

### 5.2. TEMP_PLAIN (`0x01`) — NESCHIMBAT

| Offset | Camp | Valoare |
|--------|------|---------|
| 0 | MAGIC | `0xA5` |
| 1 | TYPE | `0x01` |
| 2 | TEMP_HI | octetul inalt din `int16 = temperatura_C * 100` |
| 3 | TEMP_LO | octetul jos |
| 4 | REASON | `0x00` = interval periodic, `0x01` = buton apasat |
| 5 | CHECKSUM | `(b0 ^ b1 ^ b2 ^ b3 ^ b4) ^ 0x5A` |

`int16 = -30000` (0x8AD0) este marcajul de **eroare de citire ADC**.

Acesta este si **payload-ul transportat de DATA_UP**: hub-ul il da
neschimbat lui `SensorPacketCodec::decode()`. Asa nu exista doua cai
diferite de interpretare a temperaturii, iar testul 7 si testul 8
folosesc acelasi cod.

Pe senzor, TEMP_PLAIN se mai emite doar daca `ENABLE_PLAIN_TEMP` este 1,
si numai cat timp senzorul nu este inrolat. Implicit este 0.

### 5.3. Identificatori

- **DevEUI** — 8 octeti, `"SOLVIX" | 0x00 | SENSOR_NODE_ID`. PIC16LF1508
  nu garanteaza un ID unic, deci DevEUI se **provizioneaza** din
  `SENSOR_NODE_ID` (`senzor/main.c`) si se scrie in HEF la prima pornire.
- **DevAddr** — 1 octet, numarul senzorului, 1..`HUB_MAX_SENSORS`. Vine
  din **pozitia** DevEUI-ului in `PROVISIONED_DEVICES_INIT`
  (`DeviceRegistry::addressForEui`, F-037), deci este stabil peste
  reinrolari si peste golirea registrului, si poate fi scris pe cutie.

**Nu mai exista AppKey, SessKey, DevNonce sau JoinNonce.** Au disparut
odata cu criptografia (F-038).

### 5.4. JOIN_REQ (`0x10`, senzor -> hub) — 10 octeti

```
[0]      0xA5
[1]      0x10
[2..9]   DevEUI (8B)
```

DevEUI ramane pe fir desi hub-ul stie oricum ce numere exista: el este
cheia dupa care hub-ul verifica ca placa are voie, deriva numarul din
pozitia in tabel si o identifica in `remove <DevEUI>`. Daca JOIN_REQ ar
purta doar numarul, senzorul si-ar declara singur adresa — exact ce a
reparat F-037.

### 5.5. JOIN_ACCEPT (`0x11`, hub -> senzor) — 3 octeti

```
[0]      0xA5
[1]      0x11
[2]      DevAddr
```

`DevAddr` circula in clar, si asta **inchide ce F-035 lasase dinadins
netratat**: senzorul poate filtra fereastra de join pe adresa, fiindca
stie de la compilare ce numar asteapta (`SENSOR_NODE_ID`). Doi senzori
care se inroleaza in aceeasi secunda nu isi mai fura fereastra.

Ca efect secundar util, o placa programata cu un numar care nu
corespunde pozitiei ei din tabel **isi refuza singura JOIN_ACCEPT-ul**:
join-ul esueaza vizibil, cu trei clipiri pe LED2. Este diagnosticul care
inlocuieste sirul de "MIC gresit" de dinainte.

### 5.6. DATA_UP (`0x12`, senzor -> hub) — 13 octeti

```
[0]      0xA5
[1]      0x12
[2]      DevAddr
[3..6]   FrameCounter (4B, big-endian, strict crescator)
[7..12]  pachetul TEMP de 6 octeti, IN CLAR
```

Numele nu mai este `DATA_ENC`: nu mai exista niciun "Enc". Valoarea
tipului ramane `0x12` — un pachet de firmware vechi are 17 octeti si
cade la verificarea de lungime, ceea ce este exact simptomul util.

`DevAddr` din octetul `[2]` este **numarul senzorului**, adica raspunsul
la "de la cine vine data". **Raspunsul acela este acum DECLARATIV.** Cat
timp exista MIC, adresa intra in zona semnata cu cheia de sesiune a
acelui senzor si nu putea fi falsificata; acum orice emitator poate
pretinde orice numar.

`FrameCounter` ramane si devine **singura aparare a caii de date**:
hub-ul cere strict crescator. Tot el da pachetele pierdute si detectia de
repornire (F-036).

### 5.7. CMD_DOWN (`0x13`, hub -> senzor) — 4 octeti

```
[0]      0xA5
[1]      0x13
[2]      DevAddr
[3]      CmdType: 0x01 = ACK, 0x02 = RESET (dezinrolare)
```

Contorul downlink a fost scos de pe fir. Fara MIC nu apara nimic — un
atacator nu are nevoie sa *reia* un CMD_DOWN capturat, il fabrica din
patru octeti constanti — iar senzorul nu l-a citit niciodata.
`DeviceRecord::downCounter` ramane pe hub ca statistica locala in jurnal.

La `RESET`, senzorul sterge inrolarea din HEF si trece in
`DEV_STATE_IDLE`, adica **in repaus**: tace si nu cere inrolarea singur.
Reintrarea in retea cere `pair` pe hub **plus** trei secunde pe butonul 2
al senzorului (F-030). Dezinrolarea este decizia hub-ului, reintrarea
ramane a utilizatorului.

`CMD_DOWN` se **retrimite** la fiecare pachet al unui device marcat. Un
downlink are o singura sansa — senzorul asculta doar
`DOWNLINK_WINDOW_MS` = 600 ms dupa fiecare transmisie — deci hub-ul
insista pana cand senzorul tace (F-031). Din acelasi motiv, intre
transmisia senzorului si deschiderea ferestrei lui de receptie **nu are
voie sa stea nimic blocant** (F-032).

### 5.8. De ce se pastreaza checksum-ul XOR

Checksum-ul XOR din interiorul celor 6 octeti nu este apararea de
integritate a pachetului — aceea este **CRC-ul LoRa**, activ pe ambele
capete, care acopera *tot* payload-ul, inclusiv adresa si contorul. El
este pastrat pentru ca pachetul de temperatura sa ramana **bit cu bit**
cel vechi, deci sa poata fi dat direct decodorului existent, iar testul 7
si testul 8 sa foloseasca acelasi cod.

Daca payload-ul nu trece de checksum desi CRC-ul LoRa a fost bun,
inseamna ori un emitator strain care nimereste aceiasi parametri radio si
aceeasi adresa, ori un capat ramas pe firmware vechi.

### 5.9. Mai multi senzori pe acelasi hub

Protocolul **nu s-a schimbat cu niciun octet** ca sa suporte cei 5
senzori: `DevAddr` exista de la inceput. S-au schimbat trei lucruri in
jurul lui.

**1. Numarul senzorului este stabil.** `DevAddr` nu mai este „prima
adresa libera", ci pozitia placii in `PROVISIONED_DEVICES_INIT` din
`Config.h`, plus unu (`DeviceRegistry::addressForEui`). Randul 3 din
tabel este „Senzor #3" la prima inrolare, dupa o dezinrolare si o
reinrolare, si dupa o golire completa a registrului. Aceeasi cifra se
scrie si pe placa, ca `SENSOR_NODE_ID`. Numarul poate fi deci scris pe
cutie si ramane adevarat (F-037).

**2. Senzorii nu vorbesc odata.** Nu exista arbitraj, sloturi sau
rezervare de canal — ar fi costat pe PIC16 mai mult decat pierde astazi
in coliziuni. In schimb fiecare senzor doarme altfel:

```
treziri = SLEEP_WAKEUPS_BASE (11) + (DevAddr - 1) + jitter aleator 0..3
```

| Senzor | Treziri | Interval nominal | Cu toleranta LFINTOSC |
|--------|---------|------------------|-----------------------|
| #1 | 11..14 | 23,2 – 29,6 s | 20 – 34 s |
| #2 | 12..15 | 25,3 – 31,7 s | 22 – 36 s |
| #3 | 13..16 | 27,4 – 33,8 s | 23 – 39 s |
| #4 | 14..17 | 29,6 – 35,9 s | 25 – 41 s |
| #5 | 15..18 | 31,7 – 38,0 s | 27 – 44 s |

Cele doua efecte sunt amandoua necesare. **Intervalul propriu** face ca
doi senzori ciocniti sa nu ramana ciocniti: se despart dupa o perioada.
**Jitter-ul** (LFSR de 8 biti, semanat din DevEUI si din frame counter)
rupe si cazul in care doua placi ar nimeri acelasi numar de treziri, si
pornirea simultana dupa o pana de curent (F-036).

Un `DATA_UP` sta pe aer ~41 ms, iar un senzor emite o data la ~30 s:
0,15% ocupare per placa, 0,75% pentru toate cinci.

**3. Fereastra de downlink nu mai poate fi furata.** `LoRa_Receive` pe
senzor primeste un parametru `wantType` si arunca, fara sa inchida
fereastra, orice pachet care nu este pentru el — inclusiv un `CMD_DOWN`
adresat altui senzor. Fara asta, ACK-ul trimis lui B ar fi inchis
fereastra lui A, si un `RESET` s-ar fi pierdut la fiecare ciclu (F-035).

**Ce numara hub-ul.** Coliziunile nu lasa nicio urma directa: pachetul
pur si simplu nu ajunge. Se deduc din **golurile de frame counter** —
senzorul isi incrementeaza contorul la fiecare transmisie, deci un salt
de la 41 la 44 inseamna doua pachete pierdute. Cifra sta per senzor in
registru si apare in coloana `pierd.` a comenzii `sensors`. Un salt mai
mare decat `SENSOR_FCNT_GAP_RESTART` este raportat ca repornire a
senzorului, nu ca pierderi (F-036).

---

## 6. Conversia NTC -> temperatura (neschimbata)

- Senzor: **NTC 10K, B25/50 = 3950**, legat intre **RC2 si GND**.
- **PRESUPUNERE:** exista un rezistor fix de **10 kΩ intre RC2 si VDD**
  (divizor clasic). Daca pe placa rezistorul fix este spre GND si NTC-ul
  spre VDD, se comuta `NTC_PULLUP_TO_VDD` pe `0` in cod.
- ADC: 10 biti, referinta **VDD**, canal **AN6**, `ADCON1 = 0xD0`
  (right-justified, FOSC/16 -> TAD = 1 µs la 16 MHz).
- Conversia nu foloseste `log()` / `exp()`: **tabel de cautare cu
  interpolare liniara**, 25 de intrari de la −20 °C la +100 °C, pas de
  5 °C (F-016).
- Interpolarea se face **integral pe 16 biti** (F-028). Varianta pe
  `int32` chema rutinele de inmultire si impartire pe 32 de biti din
  biblioteca XC8, care costau 172 de cuvinte de program doar ele.
  Cuantizarea introdusa este de 500/64 ≈ 0,08 °C — cu un ordin de
  marime sub toleranta unui NTC de 1%.
- Se fac **8 citiri ADC mediate** pentru a reduce zgomotul.

---

## 7. Memoria ne-volatila

### 7.1. Senzor — HEF (High-Endurance Flash)

PIC16LF1508 **nu are EEPROM**. Se folosesc ultimele 128 de cuvinte ale
memoriei de program (`0x0F80`–`0x0FFF`), garantate la ~100.000 de cicluri.
Un cuvant are 14 biti, dar se foloseste doar octetul de jos.

Flash-ul se sterge si se scrie pe **randuri intregi de 32 de cuvinte**.
Nu este o presupunere: `FLASH_ERASE=20` si `FLASH_WRITE=20` (hexazecimal)
in `PIC12-16F1xxx_DFP/.../dat/ini/16lf1508.ini`. In 128 de cuvinte incap
deci exact **4 randuri**:

| Rand | Adresa | Continut |
|------|--------|----------|
| 0 | `0x0F80` | MAGIC(1) + DevEUI(8) |
| 1 | `0x0FA0` | MAGIC(1) + DevAddr(1) |
| 2 | `0x0FC0` | inelul de frame counter, slotul 0: MAGIC(1) + counter(4) |
| 3 | `0x0FE0` | inelul de frame counter, slotul 1 |

Randul de sesiune s-a golit odata cu criptografia (F-038): nu mai exista
nici `SessKey`, nici nonce-uri. Ce a ramas este exact bitul care conteaza
— **prezenta marcajului** inseamna "sunt inrolat, am voie sa vorbesc" —
plus `DevAddr`, care vine de la hub si din care iese slotul de somn.
O inrolare este deci o singura stergere+scriere.

**`HEF_MAGIC_SESSION` a fost schimbat de la `0xC3` la `0xC4` in acelasi
commit, si nu este o toaleta cosmetica.** Randul de sesiune vechi incepea
cu `0xC3` urmat de `DevAddr` — adica exact formatul nou, octet cu octet.
Cu marcajul neschimbat, firmware-ul nou ar fi citit o sesiune veche ca
valida si ar fi inceput sa emita catre un hub al carui registru tocmai
fusese golit de `REGISTRY_BLOB_VERSION = 4`: cinci placi blocate in
`DevAddr ... nu este inrolat`, fiecare recuperabila doar cu trei secunde
de buton, pe teren. Cu marcajul schimbat, ambele capete pornesc golite
simultan si recuperarea este cea normala.

**Regiunea HEF este rezervata explicit din linker** cu
`--ROM=default,-f80-fff` (proprietatea `code-model-rom` din proiectul
MPLAB X). Fara ea, linkerul plaseaza cod acolo si prima scriere in HEF
isi sterge propriul program — vezi F-027.

**Frame counter-ul** sta in RAM in timpul functionarii si se salveaza in
HEF doar la fiecare `FCNT_CHECKPOINT_EVERY` (implicit 50) transmisii, prin
rotatie in cele 2 sloturi (uzura se imparte la 2). La citire se ia
**maximul** sloturilor valide: counter-ul creste strict, deci maximul este
si cel mai recent, indiferent unde a ramas rotatia. Rezulta ~345 de
scrieri pe zi impartite la 2 randuri, adica peste 500 de zile pe rand.

La un **cold boot** (reset sau deconectare) se citeste valoarea din HEF si
se **sare inainte cu `FCNT_CHECKPOINT_EVERY`**: asa nu se reutilizeaza
niciodata o valoare deja emisa, iar hub-ul — care cere counter strict
crescator — nu respinge senzorul. Pretul este o "gaura" in numerotare
dupa fiecare reset, care nu deranjeaza pe nimeni.

### 7.2. Hub — NVS prin `Preferences`

Registrul senzorilor inrolati traieste in spatiul NVS `solvix-pair`. Are
exact `HUB_MAX_SENSORS` locuri. Fiecare inregistrare tine `DevEUI`,
`DevAddr` (adica **numarul senzorului**), `SessKey`,
`lastFrameCounterUp`, `hasUplink`, `downCounter`, `lastDevNonce`, numarul
de pachete primite si pe cel de pachete **pierdute** (`lostPackets`),
starea dezinrolarii in curs (`pendingReset`, `resetAttempts`,
`resetSentMs`) si ultima masuratoare, pentru tabelul `sensors`
(`lastTempX100`, `lastRssi`, `hasReading`).

**Sase campuri sunt relative la sesiunea curenta a hub-ului** si se pun
pe **0** / `false` la incarcarea din NVS: `lastSeenMs`, `resetSentMs`,
`hasReading`, `lastTempX100`, `lastRssi` si `offlineReported`.

Pentru `resetSentMs` nu este doar curatenie — `0` inseamna "niciun RESET
trimis in sesiunea asta", iar confirmarea prin tacere refuza sa se
pronunte in acel caz. Fara zeroizare, un `millis()` mic minus o valoare
veche ar da o diferenta uriasa si orice dezinrolare in curs ar aparea drept
confirmata imediat dupa fiecare repornire (F-031). Pentru ultima
masuratoare este onestitate: o temperatura salvata acum trei saptamani nu
are ce cauta in coloana "ultima citire".

Se salveaza la fiecare inrolare, la fiecare stergere si o data la
`REGISTRY_SAVE_EVERY` (implicit 20) pachete de date. NVS este flash:
scrierea la fiecare pachet l-ar uza degeaba, iar anti-replay-ul cere doar
ca frame counter-ul sa fie **strict crescator**.

Fiecare inregistrare tine acum `DevEUI`, `DevAddr`, contoarele si starea
dezinrolarii. **Nu mai tine nicio cheie** — `sessKey` si `lastDevNonce` au
disparat odata cu criptografia (F-038).

Structura salvata are un numar de versiune (`REGISTRY_BLOB_VERSION`,
acum **4**): daca `DeviceRecord` se modifica, registrul vechi este ignorat
in loc sa fie interpretat gresit octet cu octet. **Pretul, de retinut
inainte de a-l incrementa:** dupa un asemenea update hub-ul porneste cu
registrul gol, in timp ce senzorii isi pastreaza de obicei starea in HEF.
Ei continua sa emita si apar ca `DevAddr ... nu este inrolat`, iar fiecare
trebuie reinrolat o data, manual. *La trecerea pe v4 pretul acesta NU s-a
platit*, fiindca `HEF_MAGIC_SESSION` s-a schimbat in acelasi commit: si
senzorii au pornit goli, deci ambele capete erau in aceeasi stare. Numarul primit inapoi este insa acelasi
ca inainte, fiindca vine din tabelul de provisioning, nu din ordinea
inrolarii (F-037) — asa ca reinrolarea nu incurca etichetele de pe cutii.

---

## 8. Ce face fiecare fisier

### 8.1. `senzor/` — proiect MPLAB X, nodul senzor

| Fisier | Rol |
|--------|-----|
| `main.c` | **Firmware-ul complet**, in 16 sectiuni numerotate: parametri, pini, registre SX1276, protocol, HEF, `Word32`, starea device-ului, NVM, driver LoRa (TX **si RX**), ADC+NTC, butoane (inclusiv `ButtonPair_HeldLong()` pentru pairing-ul manual), LED-uri, construirea pachetelor, initializare, inrolare, bucla principala cu cele trei stari `IDLE` / `JOINING` / `OPERATING`. **Singura linie care difera intre cele cinci placi este `SENSOR_NODE_ID`** (sectiunea 1): din ea ies `DevEUI`, `DevAddr`-ul asteptat si slotul de somn. `LoRa_Receive()` filtreaza dupa tip, LUNGIME si `devAddr` si este singurul punct de validare a receptiei, ca fereastra de downlink sa nu fie consumata de pachetul altui senzor (F-035); `Rand8()` / `Rand_Seed()` dau jitter-ul care desincronizeaza placile (F-036). |
| `mcc_generated_files/system/src/config_bits.c` | Configuration bits: `FOSC=INTOSC`, **`WDTE=SWDTEN`**, `MCLRE=ON`, `BOREN=ON`, `LVP=ON`, `PWRTE=OFF`. **`WRT=OFF` este obligatoriu pentru HEF.** `WDTE=SWDTEN` tine watchdog-ul stins in veghe (transmisia si scrierea in HEF sunt lungi si blocante) si il aprinde doar in jurul lui `SLEEP`, unde expirarea **trezeste** procesorul. **Fisier generat de MCC: o regenerare pune `WDTE` inapoi pe `OFF` si senzorul nu se mai trezeste.** |
| `mcc_generated_files/system/src/clock.c`, `clock.h` | Oscilator intern la **16 MHz** (`_XTAL_FREQ = 16000000`). |
| `mcc_generated_files/system/src/pins.c` | `PIN_MANAGER_Initialize()`: `TRISA=0x3F`, `TRISB=0xB0`, `TRISC=0x37`, `ANSELA=0x17`, `ANSELB=0x20`, `ANSELC=0x06`, pull-up-uri pe PORTA/PORTB. |
| `mcc_generated_files/system/pins.h` | Macro-uri `IO_RCx_SetHigh/SetLow/GetValue/...`. |
| `mcc_generated_files/spi/src/mssp.c` | Driverul SPI. `Lora_SPI` expune `Open/Close/ByteExchange/...`. Config **index 0**: `SSP1CON1=0x0A`, `SSP1ADD=0x1F` -> **125 kHz**. |
| `mcc_generated_files/system/src/system.c` | `SYSTEM_Initialize()` = clock + pini + SPI1 + intreruperi. |
| `mcc_generated_files/system/src/interrupt.c` | Vector de intreruperi generat; **nu este folosit efectiv**. |
| `nbproject/`, `Makefile*` | Fisiere de proiect MPLAB X. Doua setari sunt **obligatorii** si sunt deja aplicate in `configurations.xml`: `optimization-level = -O2` (cu `-O0` firmware-ul nu incape) si `code-model-rom = default,-f80-fff` (rezerva regiunea HEF, F-027). |

### 8.2. `hub/SolvixHub_Tests/` — suita de teste ESP32

| Fisier | Rol |
|--------|-----|
| `SolvixHub_Tests.ino` | Sketch principal: meniu pe Serial (115200), tabloul `TESTS[]`, **comenzile in cuvinte** (`pair`, `sensors`, `list`, `provisioned`, `remove`, `stats`, `help`), butonul 1 ca declansator de pairing, `setup()` care porneste SPI si incarca registrul. `commandRemove()` doar **marcheaza** device-ul (confirmarea se face in `TestPairing`), refuza marcarea unui senzor care nu a trimis niciodata nimic si trimite operatorul la `force`; accepta si forma scurta `remove #3`, dupa numarul senzorului (`parseSensorNumber`). |
| `Config.h` | **Singura sursa de adevar pentru pini** si constante: SPI, ETH, LoRa (inclusiv modulatia), butoane, LED-uri, si **sectiunea de pairing**: `PAIRING_MODE_TIMEOUT_MS`, `PAIRING_BLINK_MS`, `PAIRING_ENCRYPT_PAYLOAD`, `PAIRING_SEND_ACK`, `REMOVE_CONFIRM_SILENCE_MS`, `PAIRING_REOPEN_AFTER_REMOVE`, `PAIRING_UNKNOWN_HINT_EVERY`, `REGISTRY_*`, si **sectiunea multi-senzor**: `HUB_MAX_SENSORS`, `SENSOR_OFFLINE_MS`, `SENSOR_FCNT_GAP_RESTART` si lista `PROVISIONED_DEVICES_INIT` cu cele 5 randuri, a caror **ordine da numarul fiecarui senzor**. |
| `SpiBus.h`, `SpiBus.cpp` | Arbitrajul magistralei SPI partajate; `SpiGuard` ridica CS-ul in destructor. |
| `TestBase.h`, `TestBase.cpp` | Structura `Test { name, description, begin, tick, stop }` + ajutoare de afisare. |
| `LoRaRadio.h`, `LoRaRadio.cpp` | Invelis peste libraria LoRa: `begin()`, `sendText()`, **`sendRaw()` (NOU)**, `receive()`, `receiveRaw()`, `sleep()`. Receptia e prin polling. |
| `Leds.h`, `Leds.cpp` | Cele doua LED-uri (D22/D21). `set()` pentru stare, `pulse()` pentru evenimente, `service()` fara `delay()`. |
| `SensorPacket.h`, `SensorPacket.cpp` | **Oglinda protocolului din `senzor/main.c`**: constantele tuturor tipurilor, `decode()`/`print()`/`printRaw()` pentru temperatura, plus `messageType()`, `parseJoinRequest()`, `parseData()`, `buildJoinAccept()`, `buildCommand()`, `printEui()`. Constantele de MIC si de cheie au disparut odata cu criptografia (F-038). |
| `DeviceRegistry.h`, `DeviceRegistry.cpp` | Registrul senzorilor inrolati, salvat in NVS prin `Preferences`; cautare dupa EUI/adresa si lista de provisioning din `Config.h` (`isProvisioned()`, care a inlocuit `findAppKey()` odata cu F-038). **`addressForEui()` da numarul senzorului** din pozitia lui in tabelul de provisioning — a inlocuit vechiul `allocateAddress()`, care dadea prima adresa libera (F-037). `DeviceRecord` tine starea dezinrolarii in curs (`pendingReset`, `resetAttempts`, `resetSentMs`), contorul de pachete pierdute si ultima masuratoare; campurile relative la `millis()` se zeroizeaza la incarcarea din NVS (F-031). `printSensorTable()` este vederea de zi cu zi: toate cele `HUB_MAX_SENSORS` locuri, si cele goale. |
| `EthernetLink.h`, `EthernetLink.cpp` | Invelis peste EthernetENC: DHCP cu timeout, `printStatus()`, cerere HTTP GET. Aici este definit `HUB_MAC`. |
| `TestButtons.*` | Citeste GPIO34/35 si numara tranzitiile, ca sa se vada liniile flotante. |
| `TestEncSpi.*` | Diagnostic SPI de nivel jos pe ENC28J60; verifica `EREVID`. |
| `TestEthernet.*` | DHCP + DNS + HTTP GET. |
| `TestLoRaTx.*` | Emisie LoRa: un pachet numerotat la fiecare 2 s. |
| `TestLoRaRx.*` | Receptie LoRa cu RSSI si SNR. |
| `TestCoexistence.*` | Ambele module active alternativ pe acelasi bus. LoRa se initializeaza **primul**. |
| `TestSensorRx.*` | **Testul 7:** asculta pachetul de temperatura **in clar**. Ramane util la bring-up, cu `ENABLE_PLAIN_TEMP = 1` pe senzor. |
| `TestPairing.*` | **Testul 8:** fereastra de pairing cu timeout, tratarea `JOIN_REQ` (provisioning + numarul din tabel + `JOIN_ACCEPT`), tratarea `DATA_UP` (adresa + counter + `decode()`), trimiterea `CMD_DOWN` (ACK/RESET) si contoarele pentru `stats`. Tot aici sta **dezinrolarea confirmata** (F-031): `sendRemovalReset()` retrimite `RESET` la fiecare pachet al unui device marcat, iar `servicePendingRemovals()`, chemata din `tick()`, sterge inregistrarea abia dupa `REMOVE_CONFIRM_SILENCE_MS` de tacere. Pentru mai multi senzori: fiecare linie de jurnal incepe cu `printSensorTag()` (`Senzor #3 (0x03)`), golurile de frame counter sunt numarate ca pachete pierdute, iar `serviceOfflineWatch()` anunta o data senzorii care amutesc si o data revenirea lor. |
| `README.md` | Instructiuni de utilizare, comenzile de pairing, tabelul SPI si notele hardware. |

---

## 9. Istoric: probleme intalnite si rezolvari

Ordinea este cronologica. Fiecare intrare spune **simptomul**, **cauza**
si **fixul**, ca sa nu se repete aceleasi greseli. F-001…F-020 sunt
mostenite din `teste-sistemcomplet/` si raman valabile.

### F-001 — Modulele SPI se calca pe MISO (hub)
- **Simptom:** date de gunoi citite de la ENC28J60 sau de la LoRa, aleatoriu.
- **Cauza:** un slave SPI elibereaza MISO doar cat timp CS-ul lui este HIGH. La boot, ambele CS-uri erau flotante, deci ambele module puteau trage simultan de aceeasi linie.
- **Fix:** `SpiBus::begin()` face ambele CS-uri OUTPUT si HIGH **inainte** de orice trafic; `claimEthernet()` / `claimLoRa()` ridica CS-ul celuilalt modul inainte ca un test sa preia bus-ul.

### F-002 — `SPI.begin()` apelat de mai multe ori (hub)
- **Simptom:** maparea de pini se pierdea dupa ce un test reinitializa bus-ul.
- **Fix:** `SPI.begin()` se apeleaza **o singura data**, din `setup()`, cu ultimul argument `-1` ca driverul ESP32 sa nu preia niciun pin drept CS hardware.

### F-003 — `LoRa.end()` oprea si Ethernet-ul (hub)
- **Simptom:** dupa oprirea testului LoRa, ENC28J60 nu mai raspundea.
- **Cauza:** `LoRa.end()` inchide magistrala SPI a intregului ESP32.
- **Fix:** nu se apeleaza niciodata `LoRa.end()` sau `SPI.end()`; pentru oprirea radioului se foloseste `LoRa.sleep()`.

### F-004 — Acces SPI din intrerupere (hub)
- **Simptom:** coruperea transferurilor Ethernet aflate in desfasurare.
- **Cauza:** `LoRa.onReceive()` ruleaza callback-ul pe intreruperea de pe DIO0, care poate cadea fix in mijlocul unui transfer Ethernet.
- **Fix:** receptia se face prin **polling** cu `LoRa.parsePacket()`.

### F-005 — CS-ul Ethernet confundat cu GPIO5 (hub)
- **Simptom:** ENC28J60 mut.
- **Fix:** CS_ETH este **GPIO4**; GPIO5 este NSS-ul LoRa. Documentat explicit in `Config.h` si in README.

### F-006 — ICMP nu functioneaza pe ENC28J60 (hub)
- **Simptom:** ping-ul esua desi reteaua era in regula.
- **Cauza:** ENC28J60 nu raspunde la ICMP fara o librarie suplimentara, adesea instabila.
- **Fix:** testul de internet foloseste o cerere **HTTP GET**, care valideaza simultan DNS, rutare si TCP.

### F-007 — DHCP blocant (hub)
- **Simptom:** meniul serial devenea inutilizabil zeci de secunde.
- **Fix:** `Ethernet.begin(MAC, timeoutMs)` — al doilea parametru limiteaza asteptarea.

### F-008 — GPIO34/35 flotante (hub)
- **Simptom:** butoanele "se apasau singure".
- **Cauza:** GPIO34 si GPIO35 sunt **input-only** si nu au pull-up/pull-down intern.
- **Fix:** rezistor extern obligatoriu pe placa. Testul de butoane numara tranzitiile tocmai ca sa faca vizibil defectul. **In plus, in acest proiect:** butonul 1 (care deschide pairing-ul) este ascultat doar cand nu ruleaza niciun test sau cand ruleaza testul 8, ca zgomotul de pe o linie flotanta sa nu comute testele singur.

### F-009 — Mod SPI gresit pentru SX1276 (senzor)
- **Simptom:** `RegVersion` citea altceva decat `0x12`.
- **Cauza:** MSSP-ul pornea intr-un mod SPI care nu corespunde cu SX1276.
- **Fix:** dupa `Lora_SPI.Open(0)` se forteaza explicit **SPI mode 0**: `SSP1CON1bits.CKP = 0`, `SSP1STATbits.CKE = 1`.

### F-010 — Verificarea prezentei radioului (senzor)
- **Fix:** se citeste `RegVersion (0x42)` si se cere `0x12`. Daca nu se potriveste, versiunea citita se **clipeste pe LED in hexazecimal** (`LoRa_ShowVersionError`), in loc sa se blocheze mut.

### F-011 — DONE ignorat de TPL5110 (senzor)
> **ISTORIC.** TPL5110 a fost scos din proiectare la 2026-08-26 si nu mai
> este pe placa. Intrarea se pastreaza pentru trasabilitatea etichetelor
> F-0xx din mesajele de commit; codul descris aici nu mai exista.
- **Simptom:** placa nu se oprea la apasarea butonului.
- **Cauza:** TPL5110, sectiunea 8.5.2 din datasheet: "DONE signals received while the DELAY/M_DRV is HIGH are ignored". M_DRV are 20 ms de debounce pe ambele fronturi.
- **Fix:** se asteapta eliberarea butonului, apoi `TPL5110_MDRV_SETTLE_MS = 60 ms`, abia apoi se semnaleaza DONE.

### F-012 — Un singur front DONE nu e suficient (senzor)
> **ISTORIC.** Vezi nota de la F-011: TPL5110 nu mai este pe placa.
- **Cauza:** DONE este declansat pe front (min. 100 ns) si doar primul front dintr-un interval este procesat.
- **Fix:** se trimit **10 pulsuri** (`TPL5110_DONE_PULSES`) in loc de un singur nivel.

### F-013 — Blocaj in SPI daca MSSP e dezactivat (senzor)
- **Simptom:** apasarea butonului bloca firmware-ul.
- **Cauza:** `SPI1_ByteExchange` asteapta `SSP1IF` la nesfarsit daca MSSP-ul este oprit.
- **Fix:** niciun acces SPI daca `loraReady == 0`. In acest proiect regula acopera si calea de pairing: bucla de JOINING nu emite si nu asculta daca radioul nu a raspuns.

### F-014 — Nu se distingea defectul de firmware de cel de cablaj (senzor)
- **Fix:** LED-ul de alimentare se aprinde imediat la start si nimic nu il mai atinge.

### F-015 — Bounce la butoane (senzor)
- **Fix:** `Button_Pressed()` citeste, asteapta 20 ms, reciteste; nepotrivirea inseamna bounce si se ignora.

### F-016 — Fara virgula mobila pe PIC16 (senzor)
- **Problema:** formula Steinhart / beta cere `log()` si `exp()`; libraria math in virgula mobila nu incape in cei 4K words alaturi de driverul LoRa — cu atat mai putin acum, langa AES.
- **Fix:** conversie prin **tabel de cautare cu interpolare liniara** in aritmetica pe intregi.

### F-017 — Fara timer hardware (senzor)
- **Constrangere impusa:** Timer0/1/2 nu sunt configurate.
- **Fix:** intervalul de transmisie se obtine cu o bucla software de pasi de 10 ms, care permite si verificarea butonului intre pasi.

### F-018 — RC1 (DONE) trebuie tinut LOW cand timerul nu e folosit (senzor)
> **ISTORIC.** TPL5110 a fost scos din proiectare la 2026-08-26. RC1 este
> acum un pin liber, fara cod in `main.c`: ramane pe intrare analogica din
> configuratia MCC, ceea ce pentru un pin neconectat este starea corecta -
> bufferul digital este dezactivat, deci un nivel flotant nu consuma
> curent.
- **Problema:** codul nu face power-cycle, dar RC1 este in continuare legat la pinul DONE al TPL5110. Lasat flotant sau HIGH, ar putea taia alimentarea in mijlocul unei transmisii — sau, acum, in mijlocul unei scrieri in HEF, ceea ce ar lasa un rand pe jumatate scris.
- **Fix:** `main()` configureaza RC1 ca iesire digitala si il tine **LOW permanent**.

### F-019 — `String` nu poate transporta un pachet binar (hub)
- **Simptom:** un pachet care contine octetul `0x00` ar fi fost trunchiat la receptie.
- **Cauza:** `LoRaRadio::receive()` acumuleaza octetii intr-un `String`, care trateaza `0x00` drept terminator de sir.
- **Fix:** `LoRaRadio::receiveRaw()`, care scrie intr-un buffer de `uint8_t`. **Extins in acest proiect pe emisie:** `JOIN_ACCEPT` si `CMD_DOWN` contin `0x00` in padding, in MIC si in contoare, deci a fost adaugat si `LoRaRadio::sendRaw()`. `sendText()` ramane pentru testele de legatura cu text.

### F-020 — Cod nou livrat in foldere paralele in loc sa extinda proiectul
- **Simptom:** functionalitatea de temperatura a fost livrata initial ca `senzor_temp/` si `hub_lora_rx/`, doua foldere noi langa `senzor/` si `hub/`.
- **Cauza:** structura existenta nu a fost respectata. Rezultatul erau doua seturi de definitii de pini si doua initializari de driver, care ar fi divergat la prima modificare.
- **Fix:** codul a fost mutat in proiectele existente. **Regula pentru viitor, respectata si de pairing:** functionalitatea noua intra in `senzor/main.c` si in module noi din `hub/SolvixHub_Tests/`; nu se creeaza foldere paralele.

### F-021 — Un fisier `Crypto.h` propriu ar fi ascuns biblioteca `Crypto` (hub)
- **Simptom (anticipat la proiectare):** eroare de compilare in interiorul bibliotecii `Crypto` (Rhys Weatherley), care nu isi mai gaseste propriile declaratii.
- **Cauza:** Arduino IDE pune folderul sketch-ului **inaintea** folderelor de biblioteci in calea de include. Un `Crypto.h` al nostru ar fi fost gasit primul si de biblioteca, atunci cand ea include `<Crypto.h>`.
- **Fix:** wrapper-ul se numeste `HubCrypto.h` / `HubCrypto.cpp`. Motivul este scris in antetul fisierului, ca sa nu fie "simplificat" inapoi la `Crypto.h`.

### F-022 — Scrierea frame counter-ului la fiecare pachet ar consuma HEF-ul (senzor)
- **Simptom (anticipat):** dupa cateva luni de functionare, scrierile in HEF ar fi inceput sa esueze in tacere, iar senzorul ar fi reluat pairing-ul la fiecare pornire.
- **Cauza:** HEF suporta ~100.000 de cicluri de stergere/scriere pe rand. Un pachet la 5 secunde inseamna 17.280 de scrieri pe zi: sub 6 zile pe un singur rand.
- **Fix:** counter-ul sta in RAM (senzorul e alimentat permanent) si se salveaza doar la fiecare `FCNT_CHECKPOINT_EVERY` = 50 de pachete, prin rotatie in sloturile inelului. La cold boot se sare inainte cu 50, ca sa nu se reutilizeze nicio valoare. Rezulta ~345 de scrieri pe zi impartite la 2 randuri (vezi F-026 pentru de ce sunt 2, nu 4), adica peste 500 de zile pe rand.

### F-023 — Payload-ul decriptat nu trecea de checksum, fara niciun indiciu (hub)
- **Simptom (anticipat la proiectare):** MIC valid, deci pachetul chiar vine de la senzorul inrolat, dar `decode()` il respinge. Simptomul arata identic cu un senzor defect.
- **Cauza posibila:** `PAIRING_ENCRYPT_PAYLOAD` are valori diferite pe hub si pe senzor, deci hub-ul "decripteaza" un text care era deja in clar (sau invers).
- **Fix:** `TestPairing` trateaza acest caz separat de un MIC gresit, spune explicit ce sa verifice si afiseaza octetii obtinuti. Constanta este documentata in tabelul de la finalul README-ului hub-ului, alaturi de perechea DevEUI/AppKey.

### F-024 — AES-128 nu incape in PIC16LF1508
- **Simptom:** proiectul nu link-edita deloc pentru `16LF1508`. Prima
  eroare era in RAM (`no space for auto/param main@tempPacket`), iar dupa
  ce se elibera RAM aparea un val de `can't find N words for psect ... in
  class CODE`.
- **Cauza:** firmware-ul cu AES-128 + AES-CMAC + AES-CTR cere **5426 de
  cuvinte de program si 446 de octeti de RAM** (masurat cu `xc8-cc`
  v3.10, `-O2`, compiland pentru `16LF1509` care are acelasi nucleu dar
  memorie mai mare — pe `16LF1508` link-editarea esueaza si nu da nicio
  cifra). Device-ul are 4096 de cuvinte si 256 de octeti. Nici macar cu
  toate cele trei solutii de rezerva prevazute in proiectare aplicate
  simultan nu se cobora sub **4325 de cuvinte / 286 de octeti**. Numai
  cele doua tabele de substitutie ale AES ocupa 512 cuvinte.
- **Fix:** cifrul a fost inlocuit cu **XTEA-128** (bloc de 64 de biti,
  cheie de 128, 32 de runde, `DELTA = 0x9E3779B9`). XTEA nu are niciun
  tabel si are nevoie doar de cifrare, fiindca MIC-ul (CBC-MAC) si
  criptarea (CTR) se construiesc amandoua peste ea. Rezultat: **3761 de
  cuvinte si 250 de octeti**, cu pairing-ul, cheia de sesiune,
  anti-replay-ul si criptarea payload-ului toate pastrate.
  `JOIN_ACCEPT` s-a scurtat de la 22 la 10 octeti, fiindca blocul cifrat
  de 16 octeti a fost inlocuit cu 4 octeti in mod CTR.
- **De retinut:** CBC-MAC-ul simplu (fara subcheile CMAC) este sigur aici
  **doar** pentru ca fiecare tip de mesaj are lungime fixa si octetul
  `TYPE` se afla in primul bloc acoperit. Daca se adauga vreodata un
  mesaj de lungime variabila, constructia trebuie schimbata.

### F-025 — RAM-ul lui PIC16LF1508 documentat gresit ca 512 B
- **Simptom:** toate estimarile de incadrare erau optimiste cu un factor
  de doi, iar buffer-ele fusesera dimensionate dupa ele.
- **Cauza:** `CLAUDE.md` si comentariile din `main.c` scriau 512 B. In
  realitate `RAMBANK=20-7F,A0-EF,120-16F` inseamna **240 de octeti
  bancati + 16 comuni = 256**. (512 B are `PIC16LF1509`.)
- **Fix:** cifra a fost corectata peste tot, iar consumul a fost taiat
  acolo unde chiar conta: cheia extinsa a disparut (XTEA nu are program
  de chei), `hefRowBuffer` tine doar cei 25 de octeti folositi in loc de
  tot randul de 32 (restul latch-urilor primesc `0xFF` direct in
  `HEF_WriteRow`), iar `txBuffer`/`rxBuffer`/`macBuffer`/`cryptoBlock` au
  fost dimensionate dupa noile lungimi de pachet.

### F-026 — Randul de HEF are 32 de cuvinte, nu 16
- **Simptom (prins inainte de a ajunge pe placa):** harta HEF folosea 8
  regiuni de cate 16 cuvinte, iar scrierea "randului" de la `0x0F90`
  (AppKey) ar fi sters de fapt tot blocul `0x0F80`–`0x0F9F` — adica
  DevEUI-ul odata cu ea. Senzorul si-ar fi pierdut identitatea la prima
  pornire, tacut.
- **Cauza:** dimensiunea randului fusese scrisa ca **presupunere** in
  proiectare si nu fusese verificata.
- **Fix:** valoarea reala este in fisierul de device support al lui XC8,
  `PIC12-16F1xxx_DFP/.../dat/ini/16lf1508.ini`: `FLASH_ERASE=20` si
  `FLASH_WRITE=20`, hexazecimal, adica **32 de cuvinte**. `HEF_ROW_WORDS`
  este acum 32, iar harta are 4 randuri: identitate (DevEUI + AppKey
  impreuna), sesiune (DevAddr + nonce-uri + SessKey impreuna) si doua
  sloturi de frame counter. Ca efect secundar bun, o inrolare inseamna
  acum o singura stergere+scriere in loc de doua.

### F-027 — Linkerul plasa cod chiar in regiunea HEF
- **Simptom (demonstrat pe fisierul de simboluri, inainte de a ajunge pe
  placa):** `Packet_BuildDataEnc` se termina la `0x0F9A` si `SPI1_Open`
  incepea acolo — adica exact peste `0x0F80`–`0x0FFF`. Prima scriere in
  HEF si-ar fi sters propriul cod si placa ar fi devenit un caramizi la
  prima inrolare.
- **Cauza:** proprietatea `code-model-rom` a proiectului MPLAB X era
  goala, deci linkerul avea voie sa foloseasca toata memoria de program,
  inclusiv ultimele 128 de cuvinte pe care firmware-ul le foloseste ca
  memorie ne-volatila.
- **Fix:** `code-model-rom = default,-f80-fff` in
  `senzor/nbproject/configurations.xml` (echivalentul lui
  `--ROM=default,-f80-fff`). Verificat dupa fix: ultima instructiune este
  la `0x0F7F`, deci regiunea HEF este curata. **Aceeasi rezervare trebuie
  refacuta daca cineva recreeaza proiectul MPLAB X de la zero.**

### F-028 — Aritmetica pe 32 de biti manca flash-ul pe PIC16
- **Simptom:** functii banale ieseau enorme: `Xtea_LoadKey` 126 de
  cuvinte, `Nvm_LoadFrameCounter` 194, `NTC_AdcToTempX100` 207 plus inca
  172 pentru rutinele `___aldiv` si `___lmul` din biblioteca.
- **Cauza:** PIC16 are un acumulator de 8 biti. Fiecare deplasare a unui
  `uint32` cu un numar de pozitii devine o bucla din biblioteca XC8, iar
  o impartire pe 32 de biti este o rutina intreaga. Impachetarea si
  despachetarea big-endian scrise "cu shift-uri" costau singure peste 300
  de cuvinte.
- **Fix, in doua locuri:**
  1. **Cifrul** foloseste uniunea `Word32 { uint32_t word; uint8_t
     byte[4]; }`. Conversiile big-endian ale cheii si ale blocului devin
     mutari de octeti, iar `sum & 3` si `(sum >> 11) & 3` din XTEA se
     citesc direct din `byte[0]` si `byte[1]`, fara nicio deplasare pe 32
     de biti.
  2. **Interpolarea NTC** se face integral pe 16 biti, in doi pasi
     (`frac = (high-adc)*64/span`, apoi `frac*500/64`), ca intermediarul
     sa ramana sub 65.535. Dispar si `___aldiv`, si `___lmul`.
- **Rezultat:** 4035 -> 3761 de cuvinte, adica exact marja care a facut
  posibila rezervarea regiunii HEF din F-027.
- **Inca netratat, daca mai e nevoie de spatiu:** frame counter-ul se
  impacheteaza si se despacheteaza tot cu deplasari pe 32 de biti
  (`Nvm_LoadFrameCounter`, `Nvm_SaveFrameCounter`, `Packet_BuildDataEnc`).
  Trecerea lor pe `Word32` este urmatoarea economie usoara. Marja curenta
  este de **207 cuvinte** din cele 3968 utilizabile.
- **De retinut:** pe acest device, orice `int32_t` nou introdus in codul
  fierbinte trebuie privit ca o cheltuiala de zeci-sute de cuvinte, nu ca
  o alegere de tip.

### F-029 — Configuratia de DEBUG nu incapea: depanatorul isi ia 16 octeti de RAM
- **Simptom:** `Build` mergea, dar `Debug Project` esua cu
  `could not find space (4 bytes) for variable _fcntSinceCheckpoint`.
- **Cauza, doua lucruri suprapuse:**
  1. MPLAB X adauga la build-ul de depanare `-mram=default,-160-16f`,
     adica **rezerva 16 octeti** (`0x160`-`0x16F`, valoarea `ICD3RAM` din
     fisierul de device support) pentru Snap/PICkit. Din cei 240 de
     octeti bancati raman 224, iar firmware-ul folosea 250.
  2. Makefile-ul generat continea `-O0`, fiindca MPLAB X il **rescrie**
     din modelul lui intern la fiecare build. Editarea lui
     `configurations.xml` cu proiectul DESCHIS in IDE nu are efect -
     setarile trebuie facute din interfata, sau IDE-ul trebuie inchis
     inainte.
- **Fix:** 19 octeti de RAM eliberati, ca sa incapa si build-ul de
  depanare:
  - `AppKey` nu se mai tine in RAM. Sta oricum permanent in HEF si se
    citeste de acolo direct in cifru, in `Key_UseApp()`. Cheia nu se
    schimba niciodata dupa provisioning, iar comutarea de cheie se
    intampla doar pe calea de inrolare, nu pe cea de date (**-16 octeti**,
    +82 de cuvinte de program);
  - `fcntSinceCheckpoint` a devenit `uint8_t`: numara doar pana la
    `FCNT_CHECKPOINT_EVERY` = 50 (**-3 octeti**).
- **Rezultat**, ambele configuratii, `-O2`, cu HEF rezervat:

  | configuratie | flash (words) | RAM (octeti) |
  |---|---|---|
  | Production | 3843 / 3968 | 231 / 256 |
  | Debug (Snap) | 3844 / 3968 | 231 / 256 (din care 16 ai depanatorului) |

- **De retinut:** daca `Debug Project` incepe iar sa dea `could not find
  space`, verifica INTAI ca optimizarea este `-O2` in fereastra de
  proprietati a proiectului, nu in fisiere - IDE-ul le suprascrie.

### F-030 — Fiecare `__delay_ms()` cu o constanta noua costa ~25 de cuvinte
- **Simptom:** adaugarea pairing-ului manual (o functie care numara
  apasarea butonului 2 si o bucla de asteptare cu LED2 clipind) a umflat
  firmware-ul cu **173 de cuvinte**, de la 3843 la 4016, iar
  link-editarea a picat cu `can't find N words for psect ... in class
  CODE` — adica exact esecul din F-024, dar din alta cauza.
- **Cauza:** `__delay_ms()` nu este o functie, ci o macro care emite o
  bucla de intarziere **inline la fiecare loc de apel**. Cele doua bucle
  noi aveau intre ele patru intarzieri distincte (100 ms, 20 ms, 10 ms si
  inca un 100 ms), fiecare cu propriul cod generat. Din cele 173 de
  cuvinte, peste 100 erau bucle de intarziere duplicate, nu logica.
- **Fix, trei masuri:**
  1. o singura functie `Pairing_BlinkStep()` detine **unicul**
     `__delay_ms(PAIR_HOLD_TICK_MS)` din firmware, iar ambele bucle de
     pairing o apeleaza;
  2. numaratoarea apasarii si asteptarea eliberarii butonului au fost
     unificate intr-o singura bucla `while (apasat)`, cu un contor care
     se opreste la prag — deci un singur loc de intarziere in loc de trei;
  3. debounce-ul separat la 20 ms pentru RC5 a fost scos: bucla reciteste
     oricum pinul de 30 de ori la 100 ms distanta si abandoneaza la prima
     citire LOW, ceea ce este un filtru de bounce mai strict decat cel din
     `Button_Pressed()` (F-015), nu mai slab.
- **Rezultat:** 4016 -> **3936** de cuvinte, adica sub cele 3968
  utilizabile, cu 32 de cuvinte de marja.
- **De retinut:** pe acest device, o intarziere inline cu o valoare noua
  se pune la socoteala ca o functie de ~25 de cuvinte. Cand ai nevoie de
  temporizare in doua locuri, imparte acelasi pas, nu copia randul.

### F-031 — `remove` era "trimite si uita": un RESET pierdut lasa senzorul blocat in retea pe veci
- **Simptom:** dupa `remove <DevEUI>`, senzorul continua sa emita si nu
  mai poate fi oprit din hub. Pe Serial curge la nesfarsit `[DATA]
  IGNORAT: DevAddr 0x.. nu este inrolat.`, iar `remove <DevEUI>` raspunde
  `Nu exista niciun device inrolat`, fiindca device-ul nu mai este in
  registru. Singura iesire ramane fizica: trei secunde pe butonul 2 al
  senzorului. Cel putin o placa a ajuns in starea asta.
- **Cauza:** `handleEncryptedData()` trimitea `CMD_DOWN(RESET)` **o
  singura data** si stergea imediat inregistrarea cu `removeByEui()`, fara
  nicio confirmare. Downlink-ul are o singura sansa: senzorul asculta doar
  `DOWNLINK_WINDOW_MS` = 600 ms dupa fiecare transmisie. Daca acel pachet
  se pierde — coliziune, fereastra ratata, orice — senzorul isi pastreaza
  `SessKey` si continua sa emita, iar hub-ul tocmai a aruncat singura
  copie a cheii cu care ar fi putut compune un alt `CMD_DOWN` valid pentru
  el. Dezinrolarea era presupusa, nu verificata.
- **Fix — stergerea se face pe dovada, nu pe speranta:**
  1. inregistrarea **nu** se mai sterge la trimiterea RESET-ului;
  2. la **fiecare** pachet primit de la un device marcat se retrimite
     `RESET`, cu `downCounter` si `resetAttempts` incrementate — un senzor
     care inca emite este dovada ca nu a primit comanda; pachetul nu se
     mai decodeaza si nu se mai numara ca date valide;
  3. inregistrarea dispare abia cand senzorul **tace**
     `REMOVE_CONFIRM_SILENCE_MS` (20 s = 4 intervale de transmisie) de la
     ultimul RESET — tacerea este exact semnalul ca a ajuns in
     `DEV_STATE_IDLE` (F-030). Verificarea ruleaza in `tick()`, prin
     `servicePendingRemovals()`, fara `delay()` si fara timer nou;
  4. `remove` pe un device care nu a trimis niciodata nimic
     (`hasUplink` fals) este refuzat, cu explicatie: RESET-ul nu are cum
     sa plece daca senzorul nu vorbeste, iar marcarea ar bloca
     inregistrarea la nesfarsit. Operatorul alege intre a porni senzorul
     si `force`;
  5. cand sosesc pachete de la un `DevAddr` neinrolat, hub-ul spune — rar,
     o data la `PAIRING_UNKNOWN_HINT_EVERY` pachete — cum se recupereaza
     senzorul, pentru placile ramase blocate de varianta veche.
- **Capcana prinsa in timpul fixului:** `resetSentMs` trebuie pus pe **0**
  la incarcarea din NVS, exact ca `lastSeenMs`. Amandoua sunt relative la
  `millis()`, deci la pornirea hub-ului o valoare veche minus un `millis()`
  mic ar da o diferenta uriasa si dezinrolarea ar aparea drept confirmata
  instantaneu, fara ca vreun RESET sa fi plecat. Zero inseamna acum
  "niciun RESET in sesiunea asta", iar confirmarea prin tacere refuza sa
  se pronunte in acel caz si asteapta un pachet.
- **De retinut:** un downlink fara confirmare nu este o comanda, este o
  speranta. Cand hub-ul arunca starea de care depinde reincercarea (aici:
  cheia), pierderea unui singur pachet devine definitiva. `DeviceRecord`
  s-a modificat, deci `REGISTRY_BLOB_VERSION` a trecut pe **2**.

### F-032 — Pulsul LED-ului de date facea senzorul surd la downlink
- **Simptom:** hub-ul retrimitea `CMD_DOWN(RESET)` la fiecare pachet, cu
  `incercarea 1`, `2`, `3`, `4`, `5`..., iar senzorul continua netulburat
  sa emita `DATA_ENC`. Nici `ACK`-urile nu ajungeau vreodata: LED2 nu
  pulsa niciodata dupa o transmisie, desi `PAIRING_SEND_ACK` era 1.
  Inrolarea, in schimb, mergea din prima de fiecare data.
- **Cauza:** in bucla de date, intre `LoRa_SendBuffer()` si
  `LoRa_Receive()` statea `Led_PulseData()`, care este un puls **blocant**
  de `LED_PULSE_MS` = **150 ms**. Fereastra de receptie a senzorului se
  deschidea deci abia la 150 ms dupa terminarea propriei transmisii, iar
  hub-ul raspunde mult mai devreme: cateva milisecunde de procesare si de
  scris pe Serial, plus ~41 ms de timp pe aer pentru cele 12 octeti ai lui
  `CMD_DOWN` la SF7/BW125/CR4/5. Downlink-ul era complet terminat pe la
  ~55 ms, cand senzorul inca tinea LED-ul aprins cu radioul in standby.
  **Niciun downlink din calea de date nu a functionat vreodata.**
  Asimetria cu inrolarea este exact dovada: `Join_Attempt()` trece direct
  de la `LoRa_SendBuffer()` la `LoRa_Receive()`, fara nicio intarziere, si
  de aceea `JOIN_ACCEPT` se prindea mereu.
- **Fix:** LED1 se aprinde inainte de fereastra si se stinge dupa ea, cu
  doua scrieri simple in `LATC`, deci receptia porneste imediat dupa TX.
  `Led_PulseData()` a ramas fara apelanti si a fost scoasa. Vizual,
  LED1 sta aprins cat dureaza fereastra de downlink in loc de 150 ms fixe
  - tot o clipire per pachet. Ca efect secundar, firmware-ul s-a micsorat
  cu 15 cuvinte: 3936 -> **3921**.
- **De retinut:** intre o transmisie si fereastra ei de receptie nu are
  voie sa stea NIMIC blocant - nici LED-uri, nici scrieri in HEF, nici
  masuratori. Fereastra este singura ocazie in care celalalt capat poate
  vorbi, si se inchide singura. Orice `__delay_ms()` pus acolo "doar
  pentru feedback vizual" costa exact functionalitatea.

### F-033 — Build de verificare in `build/`+`dist/` lasa MPLAB X cu o stare veche
- **Simptom:** doua "bug-uri" care nu existau. Intai butonul 2 parea mort:
  se tinea RC5 apasat trei secunde si nu se intampla nimic, desi F-030 era
  in `main.c`. Apoi, dupa o reprogramare, senzorul parea ca se inroleaza
  **singur**, fara nicio apasare - exact comportamentul de dinainte de
  F-030. S-a cautat in maparea pinilor, in pull-up-uri si in cablaj; nu
  era nimic acolo.
- **Cauza:** placa fusese programata cu **cod vechi**. Ca sa se verifice
  incadrarea in flash, firmware-ul fusese compilat cu `xc8-cc` chemat
  direct, iar iesirea scrisa fix in `senzor/build/` si `senzor/dist/` -
  directoarele de lucru ale lui MPLAB X. Numele obiectelor nu erau cele
  pe care le asteapta `Makefile-default.mk`, iar in `dist/` ramanea un
  `senzor.production.elf` mai nou decat sursele. Build-ul IDE-ului a
  ramas deci intr-o stare incoerenta si a produs un `.hex` care nu
  corespundea cu `main.c` de pe disc. Sursa era corecta tot timpul.
- **Fix:** *Clean* pe proiect in MPLAB X, apoi *Make and Program Device*.
  Pentru viitor: un build de verificare facut din afara IDE-ului **nu
  scrie in `senzor/build/` sau `senzor/dist/`** - se da o alta destinatie
  (un director temporar). Daca s-a scris totusi acolo, se face
  obligatoriu *Clean* inainte de urmatoarea programare.
- **Cum se verifica in trei secunde ce s-a programat:** raportul de
  memorie din fereastra de build, sau
  `senzor/dist/default/production/senzor.production.mum`, trebuie sa arate
  cifra din sectiunea 11 (acum **3876** de cuvinte / 232 de octeti; este
  aceeasi pentru toate cele cinci valori ale lui `SENSOR_NODE_ID`). Alta
  cifra inseamna alt cod decat cel din `main.c`, si nicio cautare in
  schema nu are rost pana nu se potriveste.

  **Cu cinci placi identice, verifica si CE numar are placa programata:**
  da `sensors` pe hub si uita-te ca senzorul sa apara pe randul asteptat.
  Doua placi programate din greseala cu acelasi `SENSOR_NODE_ID` au
  acelasi `DevEUI`, deci a doua o inlocuieste pe prima in registru la
  inrolare, impreuna cu cheia. Prima continua sa emita cu cheia veche si
  se vede pe Serial ca un sir de `MIC gresit` de la un numar de senzor
  care, dupa `sensors`, arata perfect sanatos — simptom care nu spune
  nicaieri "doua placi cu acelasi numar". `provisioned` arata ce numar
  ar trebui sa aiba fiecare `DevEUI`.
- **De retinut:** cand o placa se poarta ca o versiune anterioara a
  firmware-ului, prima intrebare nu este "ce am gresit in cod", ci "ce cod
  este de fapt pe cip".

### F-034 — Somnul senzorului ar fi rupt confirmarea dezinrolarii de pe hub
- **Simptom (prins la proiectare, inainte de a ajunge pe placa):** dupa
  introducerea somnului de ~30 s, un `remove <DevEUI>` ar fi raportat
  `DEZINROLARE CONFIRMATA` in mijlocul unui somn normal, fara ca senzorul
  sa fi primit vreun `RESET`. La trezire senzorul ar fi continuat sa emita
  cu cheia veche, iar hub-ul — care tocmai stersese inregistrarea si cheia
  — nu l-ar mai fi putut opri niciodata. Adica exact fundatura reparata de
  F-031, de data asta fara nicio iesire din hub.
- **Cauza:** `REMOVE_CONFIRM_SILENCE_MS` era 20 s, calibrata explicit
  pentru "patru transmisii ratate la rand" la un interval de transmisie de
  5 s. Mecanismul din F-031 foloseste **tacerea** ca dovada ca senzorul a
  primit `RESET`-ul si a intrat in repaus. Un senzor care doarme tace si
  el, si tace mai mult decat fereastra: 30 s de somn > 20 s de fereastra,
  deci prima confirmare ar fi cazut inainte de prima trezire.
- **Fix:** `REMOVE_CONFIRM_SILENCE_MS` a urcat la **120 s**, adica patru
  cicluri de somn nominale (sau ~3,5 in cazul cel mai lent, fiindca WDT-ul
  merge pe LFINTOSC cu toleranta larga). Perechea `SLEEP_WAKEUPS`
  (senzor) <-> `REMOVE_CONFIRM_SILENCE_MS` (hub) a intrat in **regula 11**
  din sectiunea 10, lista constantelor care se schimba obligatoriu pe
  ambele capete.
- **Continuare, F-036:** `SLEEP_WAKEUPS` nu mai exista ca atare. Somnul
  este acum `SLEEP_WAKEUPS_BASE` + `(DevAddr - 1)` + jitter, deci fiecare
  senzor doarme altfel si cel mai lung ciclu a crescut la ~44 s.
  `REMOVE_CONFIRM_SILENCE_MS` a urcat corespunzator la 180 s, si tot
  atunci a intrat in regula 11 si `SENSOR_OFFLINE_MS`.
- **De retinut:** o schimbare care pare locala pe un nod poate invalida
  tacut o presupunere de temporizare de pe celalalt. Aici presupunerea nu
  era scrisa intr-un `#define` comun, ci intr-un **comentariu** care
  spunea "patru transmisii la 5 secunde" — si comentariile nu dau erori de
  compilare cand realitatea se schimba sub ele. Orice mecanism care
  foloseste **absenta** unui semnal drept dovada trebuie recitit ori de
  cate ori se schimba ritmul in care acel semnal apare.

### F-035 — Fereastra de downlink putea fi consumata de pachetul altui senzor
- **Simptom (prins la proiectare, inainte de a pune a doua placa in
  retea):** cu doi sau mai multi senzori inrolati, `remove <DevEUI>` ar
  fi raportat `incercarea 1`, `2`, `3`... la nesfarsit pe un senzor
  perfect sanatos, iar ACK-urile ar fi ajuns doar din cand in cand. Adica
  simptomul lui F-032, dupa ce F-032 fusese reparat — si de aceea merita
  scris separat: are alta cauza.
- **Cauza:** `LoRa_Receive()` se intorcea la **primul** pachet cu CRC bun
  din fereastra, oricare ar fi fost el. Cat timp exista un singur senzor,
  orice pachet auzit in fereastra proprie era, prin constructie,
  raspunsul hub-ului. Cu mai multi senzori, in cele 600 ms ale lui A
  poate intra la fel de bine ACK-ul trimis lui B: functia se intorcea cu
  acel pachet, `Packet_ParseCommand()` il respingea corect (adresa nu se
  potriveste), dar **fereastra se inchisese deja**. Un downlink are o
  singura sansa per ciclu, deci pe calea de dezinrolare asta reproduce
  exact fundatura din F-031.
- **Fix:** `LoRa_Receive()` a primit parametrul `wantType`. Un pachet
  care nu are magic-ul nostru, nu are tipul cerut, sau — pentru
  `CMD_DOWN`, singurul tip care poarta adresa in clar — nu are `devAddr`
  nostru, este aruncat **si receptia continua cu timpul ramas**, exact ca
  la un CRC gresit. Filtrarea pe MIC ramane la apelant: el are cheia.
- **Al doilea filtru, gratuit:** `RegMaxPayloadLength` = 16, iar
  `DATA_ENC` are 17 octeti, deci modemul arunca singur pachetele de date
  ale celorlalti senzori inainte ca firmware-ul sa le vada (sectiunea 4).
- **Ce ramane netratat, dinadins:** in fereastra de `JOIN_ACCEPT` nu se
  poate filtra pe adresa, fiindca `DevAddr` circula acolo cifrat. Doi
  senzori care se inroleaza in aceeasi secunda isi pot fura reciproc
  fereastra; pica la MIC, incercarea esueaza si se reia dupa backoff.
  Inrolarea este oricum manuala si serializata de operator — trei
  secunde de buton pe fiecare placa in parte.
- **De retinut:** o functie care asteapta "un pachet" a fost scrisa,
  fara sa o spuna, pentru o retea cu **un singur** partener de discutie.
  Cand apare al doilea, presupunerea nu da nicio eroare de compilare.

### F-036 — Acelasi interval de somn pe toate placile inseamna coliziune blocata
- **Simptom (prins la proiectare):** doi senzori ar fi disparut **in
  perechi** din jurnalul hub-ului, sistematic si fara nicio eroare
  vizibila — nici MIC gresit, nici replay, nici pachet strain. Pur si
  simplu nu ar mai fi venit nimic de la ei, in timp ce ceilalti trei ar
  fi mers impecabil.
- **Cauza:** `SLEEP_WAKEUPS` era o constanta de compilare, aceeasi pe
  toate placile. Doua pachete care se suprapun in aer se pierd amandoua
  (SF7/BW125 nu are captura garantata la puteri apropiate). Coliziunea
  intamplatoare nu e o problema — 0,15% ocupare per senzor — dar doi
  senzori cu **exact acelasi interval** care s-au ciocnit o data raman
  ciocniti la nesfarsit: se deplaseaza cu acelasi pas, deci nu se despart
  niciodata. Probabilitatea de a intra in starea asta este mica; iesirea
  din ea, fara nicio interventie, era **zero**.
- **Fix, doua masuri care se completeaza:**
  1. **interval propriu fiecarui senzor**, `SLEEP_WAKEUPS_BASE` (11) plus
     `(DevAddr - 1)`, deci 23,2 / 25,3 / 27,4 / 29,6 / 31,7 s nominal.
     Doi senzori ciocniti se despart de la sine dupa o perioada;
  2. **jitter aleator la fiecare ciclu**, 0..3 treziri dintr-un LFSR de
     8 biti (`Rand8`), semanat din `DevEUI` si din frame counter-ul citit
     din HEF. Rupe si cazul in care doua placi ar nimeri acelasi numar de
     treziri, si pornirea simultana dupa o pana de curent, cand toate
     placile inrolate emit prima data in acelasi moment.

  Media pe cele 5 adrese ramane ~30,6 s, adica exact ritmul dinainte:
  s-a schimbat imprastierea, nu debitul. Cost: **+91 de cuvinte** de
  program si **+1 octet** de RAM, impreuna cu F-035 si cu blocul de
  provisioning per placa; verificarea de identitate din F-037 a mai luat
  28, deci in total 3757 -> **3876**.
- **Consecinta obligatorie pe hub:** `REMOVE_CONFIRM_SILENCE_MS` a urcat
  de la 120 s la **180 s**. Senzorul #5 cu jitter maxim si LFINTOSC la
  limita de toleranta doarme ~44 s; fereastra de confirmare trebuie sa
  acopere patru astfel de cicluri, altfel se repeta F-034 — hub-ul ar
  sterge inregistrarea si cheia in timpul unui somn normal.
- **Capcana prinsa in timpul fixului:** contorul nou de **pachete
  pierdute** (goluri in frame counter) numara si saltul cu
  `FCNT_CHECKPOINT_EVERY` = 50 pe care senzorul il face la fiecare
  pornire la rece (F-022). O repornire ar fi aratat ca 50 de coliziuni,
  adica exact peste cifra dupa care se judeca daca senzorii se ciocnesc
  intre ei. Peste `SENSOR_FCNT_GAP_RESTART` = 20, hub-ul spune acum "a
  repornit" in loc sa adune pierderi: douazeci de pachete pierdute la
  rand ar insemna zece minute de tacere continua, iar la atata
  `SENSOR_OFFLINE_MS` ar fi raportat deja senzorul ca disparut.
- **De retinut:** cand mai multe noduri identice impart un canal,
  **egalitatea perfecta a perioadelor este o defectiune**, nu o virtute.
  Un sistem care nu are cum sa iasa dintr-o stare proasta este mai rau
  decat unul care intra in ea mai des dar se repara singur.

### F-037 — `DevAddr` alocat "prima adresa libera" nu putea fi scris pe cutie
- **Simptom (prins la proiectare):** cu cinci placi identice in teren,
  operatorul nu ar fi avut cum sa stie care este care. Numarul afisat de
  hub ar fi depins de ordinea in care au fost pornite, s-ar fi schimbat
  dupa fiecare dezinrolare si reinrolare, si ar fi fost complet altul
  dupa o golire a registrului — de exemplu dupa un update care
  incrementeaza `REGISTRY_BLOB_VERSION` (7.2). Singurul identificator
  stabil ar fi ramas `DevEUI`, adica 16 cifre hexazecimale citite de pe
  ecran de fiecare data cand vrei sa stii a cui e temperatura.
- **Cauza:** `DeviceRegistry::allocateAddress()` intorcea prima adresa
  libera din `0x01`..`0xFE`. Cu un singur senzor asta insemna intotdeauna
  `0x01` si nu se vedea; cu cinci, numerotarea devine un accident al
  istoriei de pornire.
- **Fix:** `addressForEui()` intoarce **pozitia senzorului in tabelul
  `PROVISIONED_DEVICES_INIT` din `Config.h`, plus unu**. Tabelul este
  compilat in program, deci pozitia nu se poate pierde. Consecinte:
  randul 3 este "Senzor #3" indiferent de istorie, doua placi nu pot
  primi acelasi numar, iar numarul poate fi scris pe cutie. Aceeasi cifra
  se scrie si pe placa, ca `SENSOR_NODE_ID`, din care ies acolo `DevEUI`,
  `AppKey` **si** slotul de somn din F-036 — deci numarul chiar face doua
  treburi, nu este o eticheta.
- **Ce s-a mai simplificat odata cu asta:** cele cinci firmware-uri
  difera printr-o singura linie. Inainte se editau la fiecare placa doua
  tabele de octeti, iar o singura cifra gresita in oricare dintre ele
  dadea acelasi simptom ca o cheie complet gresita: "MIC gresit", fara
  alt indiciu.
- **Cazul de tranzitie, tratat explicit:** daca in NVS ramane o
  inregistrare dintr-o versiune in care adresele se alocau in ordinea
  inrolarii, ea poate ocupa fix numarul cerut de tabel. `JOIN_REQ`-ul
  este atunci refuzat cu explicatie si cu solutia (`remove <DevEUI>
  force`), in loc sa se suprascrie in tacere cheia altui senzor.
- **Capcana prinsa pe partea de senzor, si ea tratata in cod:** `DevEUI`
  se scria in HEF **doar la prima pornire** — daca randul de identitate
  avea marcajul, se citea si se iesea. O placa deja folosita, reprogramata
  cu alt `SENSOR_NODE_ID` (cazul obisnuit: senzorul #2 s-a ars si ii ia
  locul o placa de rezerva), si-ar fi pastrat identitatea VECHE din HEF,
  in timp ce `AppKey`-ul folosit ar fi fost cel compilat, al noului
  numar. Rezultatul: "MIC gresit" pe hub, cu o sursa perfect corecta pe
  disc — F-033 din nou, dar cu starea ne-volatila in loc de directorul de
  build. `Nvm_LoadOrCreateProvisioning()` compara acum `DevEUI`-ul din
  HEF cu cel compilat si, cand difera, rescrie randul de identitate **si
  sterge sesiunea** (o cheie de sesiune apartine identitatii cu care a
  fost negociata). Costa 28 de cuvinte si nicio scriere in plus la o
  pornire obisnuita: se compara doar.
- **De retinut:** un identificator generat "in ordinea sosirii" este
  bun numai atat timp cat nimeni nu trebuie sa il tina minte. In clipa in
  care apare pe o eticheta lipita pe o cutie, el trebuie sa vina dintr-o
  configuratie, nu dintr-o istorie.


### F-038 — Criptografia nu mai lasa loc pentru nimic altceva; scoasa temporar
- **Simptom:** nicio functionalitate noua nu mai incapea pe senzor.
  Firmware-ul ajunsese la **3876 din 3968 de cuvinte utilizabile
  (97,7%) si 232 din 256 de octeti de RAM**, adica **92 de cuvinte
  marja**. Orice adaugare — fie si o bucla de intarziere cu o constanta
  noua, care costa ~25 de cuvinte (F-030) — pica link-editarea cu
  `can't find N words for psect ... in class CODE`.
- **Cauza:** nu un bug, ci suma unei serii de decizii corecte pe un
  device prea mic. XTEA-128 cu CBC-MAC si CTR ocupa ~1300 de cuvinte si
  ~69 de octeti; peste ele se adunau `Key_UseApp()` (145 de cuvinte,
  pretul lui F-029), derivarea cheii, generarea nonce-ului, asamblarea
  intrarilor de MIC in cele patru functii de pachet, si cei 16 octeti de
  `AppKey` din memoria de program. Toate marjele fusesera deja
  consumate: AES-ul fusese inlocuit cu XTEA (F-024), aritmetica pe 32 de
  biti scoasa din codul fierbinte (F-028), `AppKey` mutat din RAM in HEF
  (F-029), buclele de intarziere unificate (F-030), `SPI1_Open` scos din
  legatura si frame counter-ul trecut pe `Word32` (F-034).
- **Fix — criptografia a fost scoasa integral, pe ambele capete, cu
  pairing-ul PASTRAT.** Rezultat masurat: **2395 de cuvinte si 95 de
  octeti**, adica **-1481 de cuvinte si -137 de octeti**, cu marja
  crescuta de la 92 la **1573 de cuvinte**. Ce s-a schimbat:
  - au disparut XTEA, CBC-MAC, CTR, `AppKey`, `SessKey`, `DevNonce`,
    `JoinNonce`, `HubCrypto.*` si comutatorul `PAIRING_ENCRYPT_PAYLOAD`;
  - pachetele s-au scurtat: `JOIN_REQ` 16 -> 10, `JOIN_ACCEPT` 10 -> 3,
    `DATA_ENC` 17 -> **`DATA_UP`** 13, `CMD_DOWN` 12 -> 4;
  - randul de sesiune din HEF s-a redus la MAGIC + `DevAddr`, iar
    `HEF_ROW_BUFFER_LEN` de la 25 la 9;
  - `REGISTRY_BLOB_VERSION` a trecut pe **4**;
  - pe hub, `findAppKey()` a devenit `isProvisioned()`, iar
    `PROVISIONED_DEVICES_INIT` a ramas o lista de DevEUI-uri.
- **Ce a RAMAS, si de ce inrolarea are in continuare rost:** ea creeaza
  intrarea in registrul hub-ului (fara care `remove` si dezinrolarea
  confirmata din F-031 nu ar exista), da senzorului bitul persistent
  "am voie sa vorbesc" (F-030), sincronizeaza originea contoarelor,
  dovedeste legatura radio in ambele sensuri o data, cu operatorul de
  fata, si confirma numarul dinspre autoritatea lui — tabelul din
  `Config.h` — spre placa. Este o **comisionare**, nu un control de
  acces, si asa trebuie descrisa peste tot.
- **Ce s-a PIERDUT, scris raspicat:** reteaua nu mai este autentificata.
  Oricine cu un radio pe aceiasi parametri poate injecta o temperatura
  falsa, poate dezinrola orice placa cu patru octeti
  (`A5 13 <DevAddr> 02`), poate inrola o placa falsa cat fereastra este
  deschisa, si poate rejuca orice pachet. Singura aparare ramasa pe
  calea de date este frame counter-ul strict crescator.
- **Trei capcane platite in timpul fixului:**
  1. **`Word32` nu se sterge odata cu cifrul.** Uniunea statea in
     sectiunea 6, dar o folosesc `Nvm_LoadFrameCounter`,
     `Nvm_SaveFrameCounter` si `Packet_BuildDataUp`. Stearsa din reflex,
     cele trei ar fi fost rescrise "cu shift-uri" si ar fi reintrodus
     ~300 de cuvinte (F-028) — o cincime din tot castigul, fara ca
     nimeni sa observe, fiindca marja e acum mare si nimic nu mai doare.
  2. **`HEF_MAGIC_SESSION` a trebuit schimbat de la `0xC3` la `0xC4`.**
     Randul de sesiune vechi incepea cu `0xC3` urmat de `DevAddr` —
     exact formatul nou, octet cu octet. Cu marcajul neschimbat,
     firmware-ul nou ar fi citit o sesiune veche ca valida si ar fi
     emis catre un hub al carui registru tocmai fusese golit de
     `REGISTRY_BLOB_VERSION = 4`: cinci placi blocate, fiecare
     recuperabila doar cu trei secunde de buton, pe teren. Cu marcajul
     schimbat, ambele capete pornesc golite simultan.
  3. **`RegMaxPayloadLength` a trebuit recalibrat.** Cat timp
     `DATA_ENC` avea 17 octeti si limita era 16, modemul arunca singur
     pachetele celorlalti senzori — jumatate din apararea ferestrei de
     downlink (F-035), obtinuta din intamplare. Cu pachete de 13 octeti
     filtrul ar fi disparut in tacere. `LORA_RX_BUFFER_LEN` a coborat la
     **6**, deci acum arunca si `DATA_UP`, si `JOIN_REQ` ale celorlalti:
     filtru mai bun decat inainte.
- **Doua lucruri s-au imbunatatit ca efect secundar:** `DevAddr` circula
  acum in clar in `JOIN_ACCEPT`, deci senzorul poate filtra fereastra de
  join pe adresa — ce F-035 lasase dinadins netratat, fiindca adresa era
  cifrata. Si, tot de acolo, o placa programata cu un numar care nu
  corespunde randului ei din tabel isi refuza singura `JOIN_ACCEPT`-ul,
  ceea ce inlocuieste diagnosticul "MIC gresit" pierdut odata cu cifrul.
- **RECUPERARE:** ultima stare cu criptografie este commit-ul
  **`a710142`** ("codul 3 senzori"). De acolo se reintroduce cifrul dupa
  upgrade-ul de microcontroller. **Nu compensa intre timp cu nimic facut
  in casa** — un pseudo-MIC de o suta de cuvinte care nu opreste pe
  nimeni este mai rau decat o absenta onesta, fiindca cineva se va baza
  pe el.
---

## 10. Reguli de lucru in acest proiect

1. **Niciun numar de pin "in clar"** in logica de aplicatie. Pe hub totul
   intra in `Config.h`; pe senzor, in blocul de `#define` din antetul
   `main.c`.
2. **Parametrii radio se schimba simultan pe ambele noduri.** O singura
   diferenta (frecventa, SF, BW, CR, sync word) si legatura dispare
   complet, fara niciun mesaj de eroare. Pairing-ul nu ii schimba.
3. **Nu se apeleaza `LoRa.end()` / `SPI.end()`** pe hub.
4. **Nu se acceseaza SPI din context de intrerupere** pe hub.
5. **Nu se face acces SPI pe senzor daca `loraReady == 0`** —
   `SPI1_ByteExchange` se blocheaza la nesfarsit cu MSSP-ul oprit.
6. Pe senzor, orice nou pin analogic trebuie declarat in `ANSELC`, iar
   orice pin digital de pe portul C trebuie **scos** din `ANSELC`.
7. Cand lipseste o informatie hardware, se scrie explicit **presupunerea**
   in cod si in acest fisier — nu se inventeaza in tacere.
8. **Functionalitatea noua extinde `senzor/` si `hub/`**, in structura lor
   existenta. Nu se creeaza foldere paralele (F-020).
9. Pe hub, un test **nu** face `digitalWrite` pe un pin de LED. Trece
   prin modulul `Leds`.
10. Protocolul de aplicatie se modifica in **doua fisiere simultan**:
    `senzor/main.c` (sectiunea 4) si `hub/SolvixHub_Tests/SensorPacket.h`.
11. **Constantele care trebuie sa fie identice pe cele doua capete** se
    schimba tot in pereche:
    - **lungimile celor cinci mesaje** (`JOIN_REQ_LEN`, `JOIN_ACCEPT_LEN`,
      `DATA_UP_LEN`, `CMD_DOWN_LEN`, `LORA_PACKET_LEN`), definite in
      ambele fisiere. De cand nu mai exista MIC, perechea tip+lungime este
      SINGURA verificare impotriva unei desincronizari, deci cele cinci
      valori trebuie sa ramana si **distincte intre ele** (6/10/3/13/4);
    - `LORA_RX_BUFFER_LEN` (senzor/main.c) ajunge in
      `RegMaxPayloadLength` si trebuie recalculat ori de cate ori se
      schimba lungimile: el este filtrul hardware care arunca pachetele
      celorlalti senzori inainte sa intre in fereastra de downlink
      (F-035);
    - **numarul senzorului:** randul N din `PROVISIONED_DEVICES_INIT`
      (Config.h) <-> `SENSOR_NODE_ID = N` (senzor/main.c), din care ies
      `PROVISION_DEV_EUI`. **Nu se rearanjeaza randurile tabelului
      intr-o retea deja instalata:** senzorii si-ar schimba numerele intre
      ei (F-037);
    - **intervalul de somn <-> fereastra de confirmare:**
      `SLEEP_WAKEUPS_BASE` + `SLEEP_SLOT_MASK` + `SLEEP_JITTER_MASK`
      (senzor/main.c) <-> `REMOVE_CONFIRM_SILENCE_MS` si
      `SENSOR_OFFLINE_MS` (Config.h). Hub-ul confirma dezinrolarea prin
      tacere, iar un senzor care doarme tace si el: daca somnul creste
      peste fereastra, hub-ul sterge inregistrarea in timp ce senzorul
      doar doarme, iar la trezire senzorul emite catre un hub care nu il
      mai recunoaste (F-031, F-034). Fereastra trebuie sa acopere cel putin patru cicluri de somn
      **in cazul cel mai lent**, adica al senzorului cu numarul cel mai
      mare, cu jitter maxim si cu LFINTOSC la limita de toleranta.
      **Cresterea lui `HUB_MAX_SENSORS` intra automat in aceasta
      socoteala** (F-036).
12. **Nu se afiseaza pe Serial nimic ce ar trebui sa ramana secret.**
    Nu mai exista chei in proiect, dar regula ramane pentru ce vine
    dupa upgrade-ul de microcontroller: `list` arata DevEUI si DevAddr,
    niciodata `SessKey` sau `AppKey`.
13. **Un build de verificare nu scrie in `senzor/build/` sau
    `senzor/dist/`** (F-033). Sunt directoarele de lucru ale lui MPLAB X;
    obiecte straine acolo lasa IDE-ul sa programeze cod vechi, iar
    simptomul arata ca un bug de firmware sau de cablaj. Daca s-a scris
    totusi acolo, *Clean* pe proiect inainte de urmatoarea programare.
    Dupa fiecare programare, verifica cifra din raportul de memorie:
    trebuie sa fie cea din sectiunea 11.
14. **Senzorul se compileaza cu `-O2`, iar regiunea HEF ramane
    rezervata** (`code-model-rom = default,-f80-fff`). Cu `-O0`
    firmware-ul nu mai incape, iar fara rezervare linkerul pune cod peste
    HEF (F-027). Cele doua se seteaza **din fereastra de proprietati a
    proiectului**, nu editand fisierele: MPLAB X rescrie
    `Makefile-default.mk` la fiecare build (F-029). Dupa orice adaugare
    de cod pe senzor, **citeste raportul de memorie** — marja este de
    **92 de cuvinte si 24 de octeti** (dupa F-035, F-036 si F-037).
    Marja s-a ingustat considerabil fata de cele 211 cuvinte de dupa
    F-034: urmatoarea adaugare pe senzor trebuie masurata inainte de a fi
    scrisa, nu dupa.
15. **Pe senzor, evita `int32_t` in codul fierbinte** (F-028). O
    inmultire sau o impartire pe 32 de biti costa peste 100 de cuvinte de
    program pe PIC16. Foloseste uniunea `Word32` pentru conversii
    big-endian si aritmetica pe 16 biti unde se poate.
16. **Toate cele cinci placi de senzor au acelasi `main.c`.** Se schimba
    o singura linie, `SENSOR_NODE_ID`, si se recompileaza. Nu se duplica
    fisierul si nu se creeaza cate un proiect MPLAB X pe placa: ar fi
    acelasi mod de a pierde sursa unica de adevar ca in F-020, doar la
    nivel de fisier in loc de folder. **Dupa fiecare programare, verifica
    pe hub ca placa apare cu numarul asteptat** (`sensors`) — este cel mai
    rapid mod de a prinde o placa programata cu numarul alteia.
17. **Un pachet nou care circula intre noduri trebuie sa poarte
    `DevAddr`**, in octetul `[2]`, ca toate celelalte. Altfel, cu cinci
    senzori pe canal, receptorul nu are cum sa stie al cui este pachetul,
    iar filtrul din `LoRa_Receive()` nu are dupa ce sa se ghideze
    (F-035). Partea a doua a regulii — "si `DevAddr` trebuie sa intre in
    zona acoperita de MIC" — **nu mai poate fi respectata**: nu mai
    exista MIC. Adresa a ramas raspunsul la "de la cine vine data", dar
    din **nefalsificabila** a devenit **declarativa** (F-038).

---

## 11. Criterii de acceptanta

| Cerinta | Unde este acoperita |
|---------|---------------------|
| Un senzor provizionat se inroleaza **doar** cand hub-ul e in mod pairing | `TestPairing::handleJoinRequest()`, prima verificare |
| Un senzor neprovizionat e respins | idem, `DeviceRegistry::isProvisioned()` |
| ~~Un senzor cu MIC gresit e respins~~ | **NU MAI ESTE ACOPERIT** (F-038): nu exista MIC, deci apartenenta la lista de provisioning este declarata, nu dovedita |
| Temperatura trece prin `decode()` existent, acelasi ca la testul 7 | `TestPairing::handleData()` |
| ~~Un `JOIN_REQ` rejucat e respins~~ | **NU MAI ESTE ACOPERIT** (F-038): `DevNonce` a disparut odata cu derivarea cheii |
| Un `DATA_UP` rejucat e respins | verificarea `frameCounter > lastFrameCounterUp` — singura aparare ramasa pe calea de date |
| Dupa reset de alimentare, senzorul reia comunicarea fara re-pairing si fara reutilizarea unui counter | HEF + saltul cu `FCNT_CHECKPOINT_EVERY` (sectiunea 7.1) |
| `remove <DevEUI>` + `RESET` dezinroleaza curat, **si dezinrolarea este confirmata, nu presupusa** | `commandRemove()` marcheaza; `handleData()` retrimite `RESET` la fiecare pachet al device-ului marcat; `servicePendingRemovals()` sterge inregistrarea abia dupa `REMOVE_CONFIRM_SILENCE_MS` de tacere (F-031) |
| Downlink-ul ajunge efectiv la senzor | fereastra de receptie se deschide imediat dupa TX, fara nicio intarziere blocanta (F-032) |
| Un `RESET` pierdut nu lasa senzorul blocat in retea | inregistrarea se pastreaza cat timp senzorul se aude, deci hub-ul poate reincerca oricat (F-031) |
| `remove` pe un senzor oprit nu blocheaza registrul | `commandRemove()` refuza marcarea daca `hasUplink` este fals si trimite operatorul la `force` |
| Registrul hub-ului persista peste repornire | `DeviceRegistry` pe NVS |
| Senzorul se inroleaza **doar la cererea explicita a utilizatorului** | `DEV_STATE_IDLE` este starea implicita in `senzor/main.c`; fereastra se deschide numai din `ButtonPair_HeldLong()` (butonul 2 tinut ~3 s) si se inchide dupa `PAIRING_MAX_ATTEMPTS` incercari |
| Dupa `CMD_DOWN(RESET)` senzorul nu se re-inroleaza singur | ramura `CMD_TYPE_RESET` trece in `DEV_STATE_IDLE`, nu in `DEV_STATE_JOINING` |
| Inrolat, senzorul **doarme** intre transmisii si nu mai asteapta activ | `Sleep_Cycle()` in `senzor/main.c`, chemata la finalul ciclului din `DEV_STATE_OPERATING`; trezire pe WDT, fara timer si fara rutina de intrerupere |
| Somnul **nu** inghite fereastra de downlink | `Sleep_Cycle()` se cheama abia dupa ce fereastra de `DOWNLINK_WINDOW_MS` s-a inchis si eventualul `CMD_DOWN` a fost tratat (F-032) |
| Butonul raspunde si in timpul somnului | somnul e fragmentat in reprize de ~2,11 s (11..18 dintre ele, dupa numarul senzorului si dupa jitter), cu butoanele citite la fiecare trezire; RC5 este pe PORTC, iar acest device NU are interrupt-on-change pe PORTC |
| Dupa `CMD_DOWN(RESET)` senzorul ramane **treaz** in repaus | bucla sare peste somn cand `deviceState != DEV_STATE_OPERATING`, deci reintra in `DEV_STATE_IDLE` cu latenta normala la buton |
| Somnul nu strica anti-replay-ul | `SLEEP` pastreaza RAM-ul, deci frame counter-ul si schema de checkpoint din F-022 raman neschimbate |
| Hub-ul tine **5 senzori** simultan | `HUB_MAX_SENSORS` = 5 in `Config.h`; `REGISTRY_MAX_DEVICES` este chiar el, iar `PROVISIONED_DEVICES_INIT` are cele 5 randuri de DevEUI |
| Fiecare senzor are un **numar stabil**, care nu depinde de istorie | `DeviceRegistry::addressForEui()` intoarce pozitia din tabelul de provisioning, plus unu (F-037). Acelasi numar este `SENSOR_NODE_ID` pe placa si `DevAddr` pe fir |
| Se stie **de la ce senzor SPUNE ca vine** fiecare masuratoare | `DevAddr` in octetul `[2]` din `DATA_UP` (5.6). Fiecare linie de jurnal incepe cu `Senzor #N`. **Atributia este declarativa, nu dovedita** (F-038) |
| Senzorii **nu emit sincronizat**, si nici nu raman ciocniti daca s-au ciocnit o data | Interval propriu din `DevAddr` plus jitter aleator la fiecare ciclu, in `Sleep_Cycle()` (F-036). Intervale nominale distincte: 23,2 / 25,3 / 27,4 / 29,6 / 31,7 s |
| Un pachet al altui senzor **nu inchide** fereastra de downlink | Filtrul `wantType` + `wantLen` + `devAddr` din `LoRa_Receive()`, plus filtrul hardware `RegMaxPayloadLength` = `LORA_RX_BUFFER_LEN` = 6, care arunca `DATA_UP` (13) si `JOIN_REQ` (10) ale celorlalti (F-035) |
| Coliziunile sunt **vizibile**, nu tacute | Golurile din frame counter se numara ca `lostPackets`, per senzor si total; apar in `sensors` si in `stats`. Un salt peste `SENSOR_FCNT_GAP_RESTART` este raportat ca repornire, nu ca pierderi |
| Un senzor care **cade** este semnalat | `serviceOfflineWatch()` anunta o data tacerea de peste `SENSOR_OFFLINE_MS` si o data revenirea |
| Dezinrolarea prin tacere **rezista** intervalelor de somn diferite | `REMOVE_CONFIRM_SILENCE_MS` = 180 s acopera patru cicluri ale senzorului cu numarul cel mai mare, cu jitter maxim si LFINTOSC la limita (F-036) |
| Reprogramarea unei placi cu alt numar chiar schimba identitatea | `Nvm_LoadOrCreateProvisioning()` compara `DevEUI`-ul din HEF cu cel compilat si, daca difera, rescrie randul de identitate si sterge inrolarea veche (F-037) |
| Firmware-ul senzorului **incape** in PIC16LF1508 | **VERIFICAT** cu `xc8-cc` v3.10, `-O2`, cu HEF rezervat, in AMBELE configuratii: Production **2395** / 3968 words si **95** / 256 octeti, Debug cu Snap **2396** / 95. Ultimul cuvant de cod este la `0x0E83`, deci regiunea HEF este curata. Marja ramasa: **1573 de cuvinte si 161 de octeti**. Cifra este aceeasi pentru toate cele cinci valori ale lui `SENSOR_NODE_ID`. |
| Reteaua **nu** este autentificata | **ASUMAT EXPLICIT** (F-038). Nu exista MIC, cheie sau nonce; oricine cu un radio pe aceiasi parametri poate injecta date, dezinrola o placa sau inrola una falsa. Avertismentul este in antetul lui `senzor/main.c`, in `SensorPacket.h` si in sectiunea 2, punctul 1 |

---

## 12. Regula de actualizare

**La fiecare commit**, inainte de a-l face, se actualizeaza `CLAUDE.md`:

- **cod nou / fisier nou** -> se adauga randul in sectiunea 8 ("Ce face
  fiecare fisier");
- **pin schimbat sau adaugat** -> se actualizeaza tabelul din sectiunea 3
  **si** `Config.h` / blocul de `#define` din `main.c`;
- **parametru radio schimbat** -> se actualizeaza tabelul din sectiunea 4
  **pe ambele coloane**;
- **format de pachet schimbat** -> se actualizeaza sectiunea 5 si se
  verifica parserul de pe hub;
- **bug rezolvat** -> se adauga o intrare noua `F-0xx` in sectiunea 9, cu
  **simptom, cauza si fix**, nu doar cu descrierea fixului;
- **presupunere confirmata sau infirmata** de masuratori pe placa -> se
  inlocuieste textul "PRESUPUNERE" cu fapta constatata (in special
  `HEF_ROW_WORDS` si topologia divizorului NTC).

- **schimbare care se vede din afara** (comportament, incadrare in flash,
  o capcana platita cu timp) -> se adauga un paragraf in sectiunea 13
  ("Jurnal"), cu data si cu eticheta `F-0xx`, si se actualizeaza blocul
  "Starea curenta" de la finalul ei.

Mesajul de commit trebuie sa mentioneze eticheta `F-0xx` atunci cand
commit-ul rezolva un bug din sectiunea 9.

---

## 13. Jurnal

**2026-08-23 — pairing criptat, incadrat in PIC16LF1508.** Auditul primei
versiuni a aratat ca schema pe AES-128 nu incapea (5426 de cuvinte / 446
de octeti fata de 4096 / 256 disponibili), deci cifrul a fost inlocuit cu
**XTEA-128** cu CBC-MAC si CTR (F-024) — pairing-ul, cheia de sesiune,
anti-replay-ul si criptarea payload-ului raman toate. Alte corectii din
audit: RAM-ul real este 256 B, nu 512 (F-025); randul de HEF are 32 de
cuvinte, nu 16, deci harta are 4 randuri (F-026); regiunea HEF este
rezervata din linker, fiindca altfel codul se scria peste ea (F-027);
aritmetica pe 32 de biti a fost scoasa din codul fierbinte (F-028).
`JOIN_ACCEPT` s-a scurtat de la 22 la 10 octeti. Hub-ul nu mai depinde de
biblioteca `Crypto`. Configuratia de DEBUG a cerut inca 19 octeti de RAM,
fiindca depanatorul isi rezerva 16 (F-029).

**2026-08-25 — pairing manual pe senzor (F-030).** Fara sesiune, senzorul
sta in `DEV_STATE_IDLE` si tace; fereastra de inrolare se deschide tinand
butonul 2 (RC5) apasat ~3 secunde si se inchide dupa
`PAIRING_MAX_ATTEMPTS` = 10 incercari, cu LED2 clipind cat este deschisa.
`CMD_DOWN(RESET)` duce senzorul in repaus, nu inapoi in pairing.
Incadrarea a cerut ca cele doua bucle de pairing sa imparta o singura
bucla de intarziere: fiecare `__delay_ms()` cu o constanta noua costa vreo
25 de cuvinte.

**2026-08-25 — dezinrolare confirmata pe hub (F-031).** `remove` era
"trimite si uita": un singur `CMD_DOWN(RESET)`, urmat imediat de stergerea
inregistrarii. Cum downlink-ul are o singura sansa, un pachet pierdut lasa
senzorul emitand cu o cheie pe care hub-ul tocmai o aruncase, deci fara
nicio cale de a-l mai opri. Acum RESET-ul se retrimite la fiecare pachet,
iar inregistrarea dispare abia dupa `REMOVE_CONFIRM_SILENCE_MS` de tacere.
`DeviceRecord` s-a modificat, deci `REGISTRY_BLOB_VERSION` a trecut pe 2 —
si, ca urmare, senzorii deja inrolati au trebuit reinrolati o data.

**2026-08-25 — downlink-ul ajunge efectiv la senzor (F-032).** Intre
transmisie si fereastra de receptie statea un puls blocant de LED de
150 ms, iar hub-ul raspunde in ~55 ms: **niciun downlink din calea de date
nu functionase vreodata**, nici ACK, nici RESET. Doar inrolarea mergea,
fiindca acolo receptia se deschide imediat dupa emisie. LED1 se aprinde
acum inainte de fereastra si se stinge dupa ea. Fixul a si eliberat 15
cuvinte.

**2026-08-25 — capcana de build (F-033).** Doua "bug-uri" inexistente
(butonul 2 mort, apoi inrolare automata) s-au dovedit a fi acelasi lucru:
placa era programata cu cod vechi, fiindca build-urile de verificare a
incadrarii scrisesera in `senzor/build/` si `senzor/dist/`, directoarele
de lucru ale lui MPLAB X. *Clean* pe proiect a rezolvat.

**2026-08-26 — TPL5110 scos din proiectare.** Componenta nu mai este pe
placa, deci a disparut si din cod: cele trei scrieri de registre din
`Board_Initialize()`, blocul de `#define` `TPL5110_DONE_*`, notele din
antetul lui `main.c` si din sectiunea 5, testul
`main_powercycle_test.c.bak` si datasheet-ul din `senzor/Datasheets/`
(folderul a ramas gol si a fost sters). **RC1 este acum un pin liber si nu
primeste cod**: ramane pe configuratia MCC din `pins.c`, adica intrare
analogica, ceea ce pentru un pin neconectat este exact starea buna -
bufferul digital de intrare este dezactivat, deci un nivel flotant nu
consuma curent. F-011, F-012 si F-018 au fost marcate ca ISTORIC in loc sa
fie sterse, ca etichetele din mesajele de commit sa ramana rezolvabile;
F-013 si F-014 raman documentatie vie, fiindca lectiile lor nu tin de
TPL5110. Castigul de memorie este cel asteptat de la trei scrieri de
registre: **-5 cuvinte**, 3921 -> 3916. Ce ramane de facut manual:
`PINOUT_config.pdf` inca arata RC1 -> TPL5110.

**2026-08-26 — somn intre transmisii (F-034).** Inrolat, senzorul nu mai
asteapta activ intre pachete: dupa fereastra de downlink adoarme radioul
si apoi procesorul, ~29,6 s, si se trezeste pe watchdog. Somnul se aplica
**doar** in `DEV_STATE_OPERATING`; in repaus si in pairing senzorul ramane
treaz, iar dupa un `CMD_DOWN(RESET)` se intoarce treaz in
`DEV_STATE_IDLE`, ca sa poata fi pus imediat in pairing de la buton.
Somnul este fragmentat in 14 reprize de ~2,11 s fiindca RC5 este pe PORTC,
iar `pic16lf1508.h` nu are niciun registru `IOCC*`: interrupt-on-change
exista doar pe PORTA si PORTB, deci un senzor adormit nu poate fi trezit
de buton. `WDTE` a trecut de la `OFF` la `SWDTEN`, ca watchdog-ul sa fie
pornit numai in jurul lui `SLEEP`.

Ca sa incapa, a fost nevoie intai de o faza de curatenie: `SPI1_Open` a
iesit din legatura (MSSP-ul se deschide scriind direct cele cinci
registre, in loc sa indexeze tabelul MCC pentru o singura configuratie),
iar frame counter-ul a trecut pe uniunea `Word32` in cele trei locuri unde
se impacheta big-endian cu deplasari pe 32 de biti — economia pe care
F-028 o lasase scrisa ca "inca netratat". Cele doua au eliberat impreuna
**186 de cuvinte**, 3916 -> 3730, iar somnul a costat 27, deci firmware-ul
a ajuns la 3757 cu 211 cuvinte marja.

Pe hub s-a schimbat **exact o constanta**, dar una critica:
`REMOVE_CONFIRM_SILENCE_MS` a urcat de la 20 s la 120 s. Hub-ul confirma
dezinrolarea prin tacere, iar un senzor care doarme tace si el: la 20 s
hub-ul ar fi sters inregistrarea **si cheia** in mijlocul unui somn
normal, si nu ar mai fi putut opri senzorul niciodata — fundatura din
F-031. Perechea a intrat in regula 11 din sectiunea 10.

**2026-08-26 — pana la 5 senzori pe acelasi hub (F-035, F-036, F-037).**
Reteaua a trecut de la un senzor la `HUB_MAX_SENSORS` = 5. Protocolul
**nu s-a schimbat cu niciun octet**: `DevAddr` exista de la inceput in
octetul `[2]` al lui `DATA_ENC` si a fost dintotdeauna acoperit de MIC,
deci intrebarea "de la cine vine data" avea deja raspunsul in pachet. S-au
schimbat trei lucruri in jurul lui.

**Numarul senzorului este acum o configuratie, nu un accident.** `DevAddr`
nu mai este prima adresa libera, ci pozitia placii in
`PROVISIONED_DEVICES_INIT`, plus unu (F-037). Randul 3 este "Senzor #3"
dupa orice dezinrolare, reinrolare sau golire a registrului, deci numarul
poate fi scris pe cutie. Pe placa, aceeasi cifra este `SENSOR_NODE_ID`:
**singura linie care difera intre cele cinci firmware-uri**, din care ies
`DevEUI`, `AppKey` si slotul de somn. Inainte se editau doua tabele de
octeti la fiecare placa, iar o cifra gresita in oricare dintre ele dadea
"MIC gresit" si nimic altceva.

**Senzorii nu mai pot ramane ciocniti.** Cu `SLEEP_WAKEUPS` constant pe
toate placile, doi senzori care s-ar fi ciocnit o data ar fi ramas
ciocniti la nesfarsit — se deplaseaza cu acelasi pas — si ar fi disparut
in perechi din jurnal, fara nicio eroare vizibila. Acum somnul este
`SLEEP_WAKEUPS_BASE` + `(DevAddr - 1)` + un jitter aleator de 0..3
treziri, dintr-un LFSR de 8 biti semanat din `DevEUI` si din frame
counter (F-036). Intervalele nominale devin 23,2 / 25,3 / 27,4 / 29,6 /
31,7 s, media pe cele cinci ramane ~30,6 s: s-a schimbat imprastierea,
nu debitul. Jitter-ul rezolva si pornirea simultana dupa o pana de
curent. Nu exista arbitraj de canal si nici sloturi — ar fi costat pe
PIC16 mai mult decat pierde astazi in coliziuni.

**Fereastra de downlink nu mai poate fi furata.** `LoRa_Receive()` se
intorcea la primul pachet cu CRC bun, oricare ar fi fost el: cu mai multi
senzori, ACK-ul trimis lui B ar fi inchis fereastra lui A, si un `RESET`
s-ar fi pierdut la fiecare ciclu — adica F-032 din nou, cu alta cauza, si
cu fundatura din F-031 la capat. Functia are acum un parametru `wantType`
si arunca, fara sa inchida fereastra, orice pachet care nu are tipul cerut
sau — pentru `CMD_DOWN` — adresa noastra (F-035). `RegMaxPayloadLength` =
16 face pe gratis jumatate din treaba: `DATA_ENC` are 17 octeti, deci
pachetele de date ale celorlalti senzori sunt aruncate de modem.

Pe hub, in afara numerotarii: comanda noua **`sensors`** arata toate cele
cinci locuri, si cele goale, cu ultima temperatura, varsta ei, RSSI si
pachetele pierdute; `remove` accepta si forma scurta `remove #3`; fiecare
linie de jurnal incepe cu `Senzor #N (0xNN)`; `serviceOfflineWatch()`
anunta o data cand un senzor amuteste si o data cand revine. Coliziunile
sunt numarate din golurile de frame counter — singura urma pe care o lasa
—, cu pragul `SENSOR_FCNT_GAP_RESTART` care distinge o repornire de
senzor de pierderi reale. `REMOVE_CONFIRM_SILENCE_MS` a urcat de la 120 s
la 180 s, fiindca senzorul #5 poate dormi ~44 s in cazul cel mai lent.
`DeviceRecord` a primit campuri noi, deci `REGISTRY_BLOB_VERSION` a trecut
pe **3**: la primul boot registrul porneste gol si fiecare senzor trebuie
reinrolat o data — dar isi primeste inapoi exact acelasi numar.

Tot pe senzor, o capcana platita din timp: `DevEUI` se scria in HEF doar
la prima pornire, deci o placa deja folosita si reprogramata cu alt
`SENSOR_NODE_ID` si-ar fi pastrat identitatea veche si ar fi dat "MIC
gresit" cu o sursa perfect corecta pe disc. `Nvm_LoadOrCreateProvisioning()`
compara acum identitatea din HEF cu cea compilata si, cand difera,
rescrie randul si sterge sesiunea (F-037).

**2026-08-29 — criptografia scoasa, pairing-ul pastrat (F-038).**
Senzorul ajunsese la 3876 din 3968 de cuvinte utilizabile: **92 de
cuvinte marja**, adica nimic nou nu mai incapea. Toate reducerile
posibile fusesera deja facute (F-024, F-028, F-029, F-030, F-034), deci
singura rezerva ramasa era cifrul insusi. XTEA-128 cu CBC-MAC si CTR a
fost scos integral, de pe ambele capete, **pastrand inrolarea**: 2395 de
cuvinte si 95 de octeti, adica **-1481 si -137**, cu marja de la 92 la
1573 de cuvinte.

Pachetele s-au scurtat (`JOIN_REQ` 16->10, `JOIN_ACCEPT` 10->3,
`DATA_ENC` 17 -> **`DATA_UP`** 13, `CMD_DOWN` 12->4), `HubCrypto.*` a
disparut, randul de sesiune din HEF s-a redus la marcaj + `DevAddr`, iar
`REGISTRY_BLOB_VERSION` a trecut pe 4. Trei capcane au fost platite pe
drum: `Word32` NU se sterge odata cu cifrul (o folosesc cele trei functii
de frame counter, F-028); `HEF_MAGIC_SESSION` a trebuit schimbat, fiindca
formatul nou al randului de sesiune este identic cu primii doi octeti ai
celui vechi; si `RegMaxPayloadLength` a trebuit recalibrat de la 16 la 6,
altfel filtrul hardware care apara fereastra de downlink (F-035) ar fi
disparut in tacere. Detaliile, in F-038.

**Ce trebuie stiut inainte de exploatare: reteaua nu mai este
autentificata.** Inrolarea a ramas o comisionare — cine e in retea, ce
numar are, de unde incep contoarele — nu un control de acces. Ultima
stare cu criptografie este commit-ul `a710142`; de acolo se reintroduce
cifrul dupa upgrade-ul de microcontroller.

**Starea curenta**, masurata cu `xc8-cc` v3.10, `-O2`, cu HEF rezervat, in
ambele configuratii: **2395 / 3968 cuvinte utilizabile si 95 / 256
octeti** (Debug cu Snap: 2396 / 95). Ultimul cuvant de cod este la
`0x0E83`, deci regiunea HEF este curata. Marja: **1573 de cuvinte si 161
de octeti** — pentru prima data de la inceputul proiectului, memoria nu
mai este constrangerea dominanta. Cifra este identica pentru toate cele
cinci valori ale lui `SENSOR_NODE_ID`. Sketch-ul hub-ului compileaza
pentru ESP32 Dev Module fara erori si fara warning-uri proprii (cele
ramase sunt din `EthernetENC` si `LoRa`), si ocupa 351 kB din 1310 kB de
flash.
