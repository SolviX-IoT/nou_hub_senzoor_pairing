# CLAUDE.md — SolviX HUB / Pairing criptat

> Fisier de context permanent pentru Claude Code si pentru orice om care
> intra in proiect. **Se actualizeaza la FIECARE commit**: vezi sectiunea
> [Regula de actualizare](#12-regula-de-actualizare) de la final.
>
> Acest proiect (`teste_pairing/`) **porneste din `teste-sistemcomplet/`**
> si pastreaza aceeasi arhitectura: un folder `senzor/` (proiect MPLAB X)
> si un folder `hub/` (sketch Arduino). Peste functionalitatea de
> temperatura, care ramane intacta, se adauga **inrolarea de device-uri
> (pairing), criptarea datelor, un registru de senzori pe hub si
> stergerea unui device**.

---

## 1. Despre ce este proiectul

Un sistem cu **doua noduri** care comunica radio prin **LoRa** in banda
europeana de **868 MHz**:

| Nod | Hardware | Toolchain | Rol |
|-----|----------|-----------|-----|
| **Senzor** | PIC16LF1508 + RFM96 (SX1276) + NTC 10K 3950 | MPLAB X IDE, compilator XC8, drivere MCC Melody | Se inroleaza la hub, apoi masoara temperatura si o trimite **criptata** prin LoRa. Inrolat, **doarme intre transmisii** (~30 s) |
| **Hub** | ESP32 Dev Module + RFM96 (SX1276) + ENC28J60 | Arduino IDE | Inroleaza senzorii, tine registrul lor in NVS, primeste si decripteaza datele, poate dezinrola un device |

Ambele module radio sunt SX1276, deci parametrii radio trebuie sa fie
**identici bit cu bit** pe cele doua capete, altfel pachetele nu se vad.

### Ce s-a adaugat fata de `teste-sistemcomplet/`

1. **Pairing** — un senzor ne-inrolat se alatura hub-ului si obtine o
   cheie de sesiune (`JOIN_REQ` / `JOIN_ACCEPT`).
2. **Criptare** — dupa inrolare, temperatura circula ca `DATA_ENC`:
   XTEA-CTR pe payload, CBC-MAC-XTEA pentru autenticitate. Cifrul **nu**
   este AES: nu incapea in PIC16LF1508 (F-024).
3. **Registru pe hub** — lista senzorilor inrolati, salvata in NVS, deci
   supravietuieste repornirii hub-ului.
4. **Stergere confirmata** — `remove <DevEUI>` marcheaza device-ul, iar
   hub-ul ii trimite `CMD_DOWN(RESET)` la **fiecare** pachet al lui.
   Inregistrarea, si odata cu ea cheia, se sterg abia dupa ce senzorul
   **tace** `REMOVE_CONFIRM_SILENCE_MS` — tacerea este dovada ca a primit
   comanda. Varianta care stergea din prima lasa senzorul blocat in retea
   daca acel unic downlink se pierdea (F-031).
5. **Receptie pe senzor** — driverul LoRa al senzorului era doar
   emitator; acum are si `LoRa_Receive()`.
6. **Memorie ne-volatila pe senzor** — HEF (High-Endurance Flash), pentru
   identitate, cheie de sesiune si frame counter.
7. **Somn intre transmisii** — inrolat, senzorul nu mai sta in veghe:
   ciclul este *masoara -> `DATA_ENC` -> fereastra de downlink -> radioul
   in sleep -> microcontrolerul in sleep ~30 s -> trezire*. Somnul se
   aplica **doar** in `DEV_STATE_OPERATING`; in repaus si in pairing
   senzorul ramane treaz, ca butonul 2 sa raspunda normal.
8. **Pairing manual pe senzor** — senzorul nu se mai inroleaza singur.
   Fara sesiune sta in repaus si tace; fereastra de pairing se deschide
   tinand **butonul 2 (RC5) apasat ~3 secunde**, iar LED2 clipeste cat
   este deschisa. Fereastra se inchide dupa `PAIRING_MAX_ATTEMPTS`
   incercari de join. Simetric cu hub-ul, care si el asculta `JOIN_REQ`
   doar in fereastra deschisa manual cu `pair`.

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

   Masurat cu `xc8-cc` v3.10, varianta pe **AES-128** cerea:

   | varianta | flash (words) | RAM (octeti) |
   |----------|---------------|--------------|
   | AES-128, cum a fost scrisa initial, `-O2` | 5426 | 446 |
   | + program de chei calculat din mers | 5250 | 286 |
   | + fara descifrare AES (JOIN_ACCEPT in clar) | 4433 | 286 |
   | + `PAIRING_ENCRYPT_PAYLOAD = 0` | 4325 | 286 |

   Ultimul rand inseamna **toate solutiile de rezerva aplicate
   simultan** si tot depaseste cele 4096 de cuvinte. Motivul principal:
   cele doua tabele de substitutie ale AES ocupa singure 512 cuvinte,
   adica un sfert din tot flash-ul, inainte de orice linie de cod.

   **Solutia adoptata (F-024): XTEA-128** — bloc de 64 de biti, cheie de
   128 de biti, 32 de runde, zero tabele, o singura directie (cifrarea),
   fiindca atat MIC-ul (CBC-MAC) cat si criptarea (CTR) se construiesc
   peste ea. Rezultatul, tot cu `-O2`:

   | | flash (words) | RAM (octeti) |
   |---|---|---|
   | PIC16LF1508 are | 4096 (3968 utilizabili, HEF rezervat) | 256 |
   | firmware-ul actual | **3757** (94.7%) | **231** (90.2%) |

   Cifra de 3761 de cuvinte / 250 de octeti din versiunile anterioare ale
   acestui fisier era masuratoarea de dinainte de F-029 (care a mutat
   `AppKey` din RAM in HEF) si de dinainte de pairing-ul manual. Valorile
   de mai sus sunt cele masurate acum, cu `xc8-cc` v3.10, `-O2`, cu HEF
   rezervat; vezi si sectiunea 11.

   Ce s-a pastrat: inrolarea, cheia de sesiune derivata, MIC-ul pe
   fiecare pachet, anti-replay-ul si confidentialitatea payload-ului.
   Ce s-a pierdut: blocul are 64 de biti in loc de 128, iar XTEA nu are
   statutul de standard al AES. Pentru cateva zeci de mii de pachete de
   6 octeti pe an, fiecare cu contor strict crescator, marja este
   confortabila; pentru volume mari sub aceeasi cheie, nu.

   **`-O2` este obligatoriu.** Cu `-O0` (valoarea implicita a unui
   proiect MPLAB X nou) firmware-ul nu mai incape. Proiectul este deja
   configurat cu `-O2` si cu rezervarea regiunii HEF; vezi F-027.

   Daca vreodata tot nu incape, ordinea solutiilor de rezerva ramane:
   1. `PAIRING_ENCRYPT_PAYLOAD = 0` — se pastreaza autentificarea si tot
      pairing-ul, se pierde doar confidentialitatea payload-ului;
   2. migrare pe **PIC16LF1509** (8K words, 512 B RAM), pin-compatibil —
      maparea de pini din sectiunea 3 ramane valabila bit cu bit, dar
      `HEF_BASE` devine `0x1F80`.

2. **`SLEEP` nu este acelasi lucru cu taierea alimentarii.** Distinctia
   asta decide schema frame counter-ului, si de aceea merita scrisa
   raspicat.

   **Ce se intampla acum:** inrolat, senzorul executa `SLEEP` intre
   transmisii, ~30 s, si se trezeste pe watchdog. `SLEEP` pe PIC16
   **pastreaza RAM-ul si registrele** — procesorul doar isi opreste
   ceasul. Deci schema din F-022 ramane valabila **neschimbata**:
   counter-ul traieste in RAM si se salveaza in HEF doar la fiecare
   `FCNT_CHECKPOINT_EVERY` = 50 de pachete. **Nu** se scrie la fiecare
   ciclu de somn; ar consuma HEF-ul degeaba.

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

**Pairing-ul NU adauga si NU muta niciun pin.** Tabelele de mai jos sunt
identice cu cele din `teste-sistemcomplet/`.

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
buton; acum **LED1 = transmisie de date**: se aprinde la fiecare `DATA_ENC`
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
pairing, si un sir de `DATA_ENC`.

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
de modem. 16 este putin peste cel mai lung pachet pe care il PRIMESTE
senzorul (`CMD_DOWN`, 12 octeti; `JOIN_ACCEPT` are 10).

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
| `0x10` | JOIN_REQ | senzor -> hub | 16 |
| `0x11` | JOIN_ACCEPT | hub -> senzor | 10 |
| `0x12` | DATA_ENC | senzor -> hub | 17 |
| `0x13` | CMD_DOWN | hub -> senzor | 12 |

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

Acesta este si **payload-ul care circula criptat in DATA_ENC**: dupa
decriptare, hub-ul il da neschimbat lui `SensorPacketCodec::decode()`.
Asa nu exista doua cai diferite de interpretare a temperaturii, iar
testul 7 (clar) si testul 8 (criptat) folosesc acelasi cod.

Pe senzor, TEMP_PLAIN se mai emite doar daca `ENABLE_PLAIN_TEMP` este 1,
si numai cat timp senzorul nu este inrolat. Implicit este 0.

### 5.3. Identificatori si chei

- **DevEUI** — 8 octeti, unic per senzor. PIC16LF1508 nu garanteaza un ID
  unic, deci DevEUI se **provizioneaza**: `PROVISION_DEV_EUI` in
  `senzor/main.c`, scris in HEF la prima pornire.
- **AppKey** — cheie de 128 de biti (16 B), unica per senzor. **Nu circula
  niciodata prin aer.** Sta in HEF pe senzor (`PROVISION_APP_KEY`) si in
  `PROVISIONED_DEVICES_INIT` din `hub/SolvixHub_Tests/Config.h`.
- **DevAddr** — 1 octet, alocat de hub la pairing, din `0x01`–`0xFE`.
- **SessKey** — cheie de 128 de biti derivata la fiecare inrolare.

### 5.4. JOIN_REQ (`0x10`, senzor -> hub) — 16 octeti

```
[0]      0xA5
[1]      0x10
[2..9]   DevEUI (8B)
[10..11] DevNonce (2B, diferit la fiecare incercare)
[12..15] MIC (4B) = primii 4 octeti din CBC-MAC-XTEA(AppKey, bytes[0..11])
```

### 5.5. JOIN_ACCEPT (`0x11`, hub -> senzor) — 10 octeti

```
IV_join (8B) = 0x11 | DevNonce(2) | zero(5)
[0]     0xA5
[1]     0x11
[2..5]  Enc = XTEA-CTR(AppKey, IV_join, DevAddr(1) | JoinNonce(3))
[6..9]  MIC (4B) = CBC-MAC-XTEA(AppKey,
                     0x11 | DevEUI(8) | DevNonce(2) | DevAddr(1) | JoinNonce(3))
```

Senzorul trece cei 4 octeti prin acelasi CTR — operatia este simetrica,
deci **nu exista cod de descifrare in firmware**, si tocmai asta a facut
diferenta la incadrarea in flash. Apoi recalculeaza MIC-ul cu `DevEUI`-ul
propriu si cu `DevNonce`-ul pe care tocmai l-a trimis, si accepta doar
daca se potriveste.

Un `JOIN_ACCEPT` rejucat dintr-o inrolare veche pica de doua ori: MIC-ul
nu se potriveste, iar IV-ul depinde tot de `DevNonce`, deci si
descifrarea ar da gunoi. Octetul `0x11` din fata intrarii MIC-ului
separa domeniul fata de celelalte mesaje semnate cu `AppKey`.

### 5.6. Derivarea cheii de sesiune (identic pe ambele capete)

MAC-ul da 8 octeti, iar cheia are 16, deci se cheama de doua ori peste
acelasi bloc, cu prefix diferit:

```
B = <prefix> | DevNonce(2) | JoinNonce(3) | DevAddr(1) | 0x00
SessKey[0..7]  = CBC-MAC-XTEA(AppKey, B cu prefix 0x01)
SessKey[8..15] = CBC-MAC-XTEA(AppKey, B cu prefix 0x02)
```

Blocul are exact 8 octeti, cat blocul cifrului, deci nu apare padding.

### 5.7. DATA_ENC (`0x12`, senzor -> hub) — 17 octeti

```
[0]      0xA5
[1]      0x12
[2]      DevAddr
[3..6]   FrameCounter (4B, big-endian, strict crescator)
[7..12]  EncPayload (6B) = XTEA-CTR(SessKey, IV, pachetul TEMP de 6 octeti)
         IV (8B) = DevAddr(1) | FrameCounter(4) | 0x00 (uplink) | zero(2)
[13..16] MIC (4B) = CBC-MAC-XTEA(SessKey, bytes[0..12])
```

Payload-ul are 6 octeti, deci se consuma un singur bloc de flux CTR.
Octetul de directie exista ca un eventual downlink criptat sa nu poata
refolosi acelasi flux de chei.

### 5.8. CMD_DOWN (`0x13`, hub -> senzor) — 12 octeti

```
[0]      0xA5
[1]      0x13
[2]      DevAddr
[3..6]   FrameCounter downlink (4B, BE)
[7]      CmdType: 0x01 = ACK, 0x02 = RESET (dezinrolare)
[8..11]  MIC (4B) = CBC-MAC-XTEA(SessKey, bytes[0..7])
```

La `RESET`, senzorul sterge `SessKey` + `DevAddr` din HEF si trece in
`DEV_STATE_IDLE`, adica **in repaus**: tace si nu cere inrolarea singur.
Reintrarea in retea cere `pair` pe hub **plus** trei secunde pe butonul 2
al senzorului (F-030). Dezinrolarea este decizia hub-ului, reintrarea
ramane a utilizatorului.

`CMD_DOWN` se **retrimite** la fiecare pachet al unui device marcat, cu
`FrameCounter` downlink nou de fiecare data. Un downlink are o singura
sansa — senzorul asculta doar `DOWNLINK_WINDOW_MS` = 600 ms dupa fiecare
transmisie a lui — deci hub-ul insista pana cand senzorul tace (F-031).
Din acelasi motiv, intre transmisia senzorului si deschiderea ferestrei
lui de receptie **nu are voie sa stea nimic blocant** (F-032).

### 5.9. De ce se pastreaza checksum-ul XOR sub criptare

CRC-ul LoRa prinde erorile de pe calea radio, MIC-ul prinde pachetele
falsificate — checksum-ul XOR nu mai adauga securitate. Este pastrat
pentru ca pachetul de 6 octeti sa ramana **bit cu bit** cel vechi, deci
sa poata fi dat direct decodorului existent. Daca payload-ul decriptat nu
trece de checksum desi MIC-ul a fost bun, mesajul de eroare de pe hub
indica exact cauza probabila: `PAIRING_ENCRYPT_PAYLOAD` diferit pe cele
doua capete.

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
| 0 | `0x0F80` | MAGIC(1) + DevEUI(8) + AppKey(16) |
| 1 | `0x0FA0` | MAGIC(1) + DevAddr(1) + JoinNonce(3) + DevNonce(2) + SessKey(16) |
| 2 | `0x0FC0` | inelul de frame counter, slotul 0: MAGIC(1) + counter(4) |
| 3 | `0x0FE0` | inelul de frame counter, slotul 1 |

Identitatea si sesiunea incap fiecare intr-un singur rand. O inrolare
inseamna deci **o singura** stergere+scriere, iar o cadere de tensiune nu
mai poate lasa `DevAddr` salvat fara `SessKey`.

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

Registrul senzorilor inrolati traieste in spatiul NVS `solvix-pair`.
Fiecare inregistrare tine `DevEUI`, `DevAddr`, `SessKey`,
`lastFrameCounterUp`, `hasUplink`, `downCounter`, `lastDevNonce`, numarul
de pachete, si starea dezinrolarii in curs: `pendingReset`,
`resetAttempts`, `resetSentMs`.

**Doua campuri sunt relative la `millis()`**, deci la pornirea hub-ului, si
se pun pe **0** la incarcarea din NVS: `lastSeenMs` si `resetSentMs`.
Pentru al doilea nu este doar curatenie — `0` inseamna "niciun RESET
trimis in sesiunea asta", iar confirmarea prin tacere refuza sa se
pronunte in acel caz. Fara zeroizare, un `millis()` mic minus o valoare
veche ar da o diferenta uriasa si orice dezinrolare in curs ar aparea drept
confirmata imediat dupa fiecare repornire (F-031).

Se salveaza la fiecare inrolare, la fiecare stergere si o data la
`REGISTRY_SAVE_EVERY` (implicit 20) pachete de date. NVS este flash:
scrierea la fiecare pachet l-ar uza degeaba, iar anti-replay-ul cere doar
ca frame counter-ul sa fie **strict crescator**.

Structura salvata are un numar de versiune (`REGISTRY_BLOB_VERSION`,
acum **2**): daca `DeviceRecord` se modifica, registrul vechi este ignorat
in loc sa fie interpretat gresit octet cu octet. **Pretul, de retinut
inainte de a-l incrementa:** dupa un asemenea update hub-ul porneste cu
registrul gol, in timp ce senzorii isi pastreaza sesiunile in HEF. Ei
continua sa emita si apar ca `DevAddr ... nu este inrolat`, iar fiecare
trebuie reinrolat o data, manual.

---

## 8. Ce face fiecare fisier

### 8.1. `senzor/` — proiect MPLAB X, nodul senzor

| Fisier | Rol |
|--------|-----|
| `main.c` | **Firmware-ul complet**, in 16 sectiuni numerotate: parametri, pini, registre SX1276, protocol, HEF, XTEA/CBC-MAC/CTR, starea device-ului, NVM, driver LoRa (TX **si RX**), ADC+NTC, butoane (inclusiv `ButtonPair_HeldLong()` pentru pairing-ul manual), LED-uri, construirea pachetelor, initializare, inrolare, bucla principala cu cele trei stari `IDLE` / `JOINING` / `OPERATING`. |
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
| `SolvixHub_Tests.ino` | Sketch principal: meniu pe Serial (115200), tabloul `TESTS[]`, **comenzile in cuvinte** (`pair`, `list`, `provisioned`, `remove`, `stats`, `help`), butonul 1 ca declansator de pairing, `setup()` care porneste SPI si incarca registrul. `commandRemove()` doar **marcheaza** device-ul (confirmarea se face in `TestPairing`), refuza marcarea unui senzor care nu a trimis niciodata nimic si trimite operatorul la `force`. |
| `Config.h` | **Singura sursa de adevar pentru pini** si constante: SPI, ETH, LoRa (inclusiv modulatia), butoane, LED-uri, si **sectiunea de pairing**: `PAIRING_MODE_TIMEOUT_MS`, `PAIRING_BLINK_MS`, `PAIRING_ENCRYPT_PAYLOAD`, `PAIRING_SEND_ACK`, `REMOVE_CONFIRM_SILENCE_MS`, `PAIRING_REOPEN_AFTER_REMOVE`, `PAIRING_UNKNOWN_HINT_EVERY`, `REGISTRY_*` si lista `PROVISIONED_DEVICES_INIT`. |
| `SpiBus.h`, `SpiBus.cpp` | Arbitrajul magistralei SPI partajate; `SpiGuard` ridica CS-ul in destructor. |
| `TestBase.h`, `TestBase.cpp` | Structura `Test { name, description, begin, tick, stop }` + ajutoare de afisare. |
| `LoRaRadio.h`, `LoRaRadio.cpp` | Invelis peste libraria LoRa: `begin()`, `sendText()`, **`sendRaw()` (NOU)**, `receive()`, `receiveRaw()`, `sleep()`. Receptia e prin polling. |
| `Leds.h`, `Leds.cpp` | Cele doua LED-uri (D22/D21). `set()` pentru stare, `pulse()` pentru evenimente, `service()` fara `delay()`. |
| `SensorPacket.h`, `SensorPacket.cpp` | **Oglinda protocolului din `senzor/main.c`**: constantele tuturor tipurilor, `decode()`/`print()`/`printRaw()` pentru temperatura, plus `messageType()`, `parseJoinRequest()`, `parseEncryptedData()`, `buildJoinAccept()`, `buildCommand()`, `printEui()`. |
| `HubCrypto.h`, `HubCrypto.cpp` | **NOU.** XTEA-128 (bloc de 8 octeti), CBC-MAC-XTEA, XTEA-CTR, `buildDataIv()`, `buildJoinIv()`, `deriveSessionKey()`. **Nu depinde de nicio biblioteca** (F-024). Numele **nu** este `Crypto.h`, ca sa nu ascunda antetul unei biblioteci cu acel nume (F-021). |
| `DeviceRegistry.h`, `DeviceRegistry.cpp` | **NOU.** Registrul senzorilor inrolati, salvat in NVS prin `Preferences`; cautare dupa EUI/adresa, alocare de `DevAddr`, lista de provisioning din `Config.h`. `DeviceRecord` tine si starea dezinrolarii in curs (`pendingReset`, `resetAttempts`, `resetSentMs`); `resetSentMs`, ca si `lastSeenMs`, este relativ la pornirea hub-ului si se pune pe 0 la incarcarea din NVS (F-031). |
| `EthernetLink.h`, `EthernetLink.cpp` | Invelis peste EthernetENC: DHCP cu timeout, `printStatus()`, cerere HTTP GET. Aici este definit `HUB_MAC`. |
| `TestButtons.*` | Citeste GPIO34/35 si numara tranzitiile, ca sa se vada liniile flotante. |
| `TestEncSpi.*` | Diagnostic SPI de nivel jos pe ENC28J60; verifica `EREVID`. |
| `TestEthernet.*` | DHCP + DNS + HTTP GET. |
| `TestLoRaTx.*` | Emisie LoRa: un pachet numerotat la fiecare 2 s. |
| `TestLoRaRx.*` | Receptie LoRa cu RSSI si SNR. |
| `TestCoexistence.*` | Ambele module active alternativ pe acelasi bus. LoRa se initializeaza **primul**. |
| `TestSensorRx.*` | **Testul 7:** asculta pachetul de temperatura **in clar**. Ramane util la bring-up, cu `ENABLE_PLAIN_TEMP = 1` pe senzor. |
| `TestPairing.*` | **NOU — testul 8:** fereastra de pairing cu timeout, tratarea `JOIN_REQ` (provisioning + MIC + anti-replay + alocare de adresa + `JOIN_ACCEPT`), tratarea `DATA_ENC` (adresa + counter + MIC + decriptare + `decode()`), trimiterea `CMD_DOWN` (ACK/RESET) si contoarele pentru `stats`. Tot aici sta **dezinrolarea confirmata** (F-031): `sendRemovalReset()` retrimite `RESET` la fiecare pachet al unui device marcat, iar `servicePendingRemovals()`, chemata din `tick()`, sterge inregistrarea abia dupa `REMOVE_CONFIRM_SILENCE_MS` de tacere. |
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
  cifra din sectiunea 11 (acum **3757** de cuvinte / 231 de octeti). Alta
  cifra inseamna alt cod decat cel din `main.c`, si nicio cautare in
  schema nu are rost pana nu se potriveste.
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
- **De retinut:** o schimbare care pare locala pe un nod poate invalida
  tacut o presupunere de temporizare de pe celalalt. Aici presupunerea nu
  era scrisa intr-un `#define` comun, ci intr-un **comentariu** care
  spunea "patru transmisii la 5 secunde" — si comentariile nu dau erori de
  compilare cand realitatea se schimba sub ele. Orice mecanism care
  foloseste **absenta** unui semnal drept dovada trebuie recitit ori de
  cate ori se schimba ritmul in care acel semnal apare.

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
    schimba tot in pereche: `PAIRING_ENCRYPT_PAYLOAD` (Config.h / main.c),
    perechea DevEUI + AppKey (`PROVISIONED_DEVICES_INIT` /
    `PROVISION_DEV_EUI` + `PROVISION_APP_KEY`) si lungimile zonelor
    acoperite de MIC (`*_MIC_INPUT_LEN`, definite in ambele fisiere).
    **Si perechea interval de somn <-> fereastra de confirmare:**
    `SLEEP_WAKEUPS` (senzor/main.c) <-> `REMOVE_CONFIRM_SILENCE_MS`
    (Config.h). Hub-ul confirma dezinrolarea prin tacere, iar un senzor
    care doarme tace si el: daca somnul creste peste fereastra, hub-ul
    sterge inregistrarea **si cheia** in timp ce senzorul doar doarme, si
    nu il mai poate opri niciodata (F-031). Fereastra trebuie sa acopere
    cel putin trei-patru cicluri de somn.
12. **Cheile nu se afiseaza pe Serial.** `list` arata DevEUI si DevAddr,
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
    **211 cuvinte si 25 de octeti** (dupa F-034). Marja este acum
    confortabila, fiindca `SPI1_Open` a iesit din legatura si frame
    counter-ul a trecut pe `Word32` — cele doua economii pe care F-028 le
    lasase scrise ca "inca netratat".
15. **Pe senzor, evita `int32_t` in codul fierbinte** (F-028). O
    inmultire sau o impartire pe 32 de biti costa peste 100 de cuvinte de
    program pe PIC16. Foloseste uniunea `Word32` pentru conversii
    big-endian si aritmetica pe 16 biti unde se poate.

---

## 11. Criterii de acceptanta

| Cerinta | Unde este acoperita |
|---------|---------------------|
| Un senzor provizionat se inroleaza **doar** cand hub-ul e in mod pairing | `TestPairing::handleJoinRequest()`, prima verificare |
| Un senzor neprovizionat sau cu MIC gresit e respins | idem, verificarile 1 si 2 |
| Temperatura ajunge criptata si, decriptata, trece prin `decode()` existent | `TestPairing::handleEncryptedData()` |
| Un `JOIN_REQ` rejucat e respins | verificarea `lastDevNonce` |
| Un `DATA_ENC` rejucat e respins | verificarea `frameCounter > lastFrameCounterUp` |
| Dupa reset de alimentare, senzorul reia comunicarea fara re-pairing si fara reutilizarea unui counter | HEF + saltul cu `FCNT_CHECKPOINT_EVERY` (sectiunea 7.1) |
| `remove <DevEUI>` + `RESET` dezinroleaza curat, **si dezinrolarea este confirmata, nu presupusa** | `commandRemove()` marcheaza; `handleEncryptedData()` retrimite `RESET` la fiecare pachet al device-ului marcat; `servicePendingRemovals()` sterge inregistrarea abia dupa `REMOVE_CONFIRM_SILENCE_MS` de tacere (F-031) |
| Downlink-ul ajunge efectiv la senzor | fereastra de receptie se deschide imediat dupa TX, fara nicio intarziere blocanta (F-032) |
| Un `RESET` pierdut nu lasa senzorul blocat in retea | inregistrarea si cheia se pastreaza cat timp senzorul se aude, deci hub-ul poate reincerca oricat (F-031) |
| `remove` pe un senzor oprit nu blocheaza registrul | `commandRemove()` refuza marcarea daca `hasUplink` este fals si trimite operatorul la `force` |
| Registrul hub-ului persista peste repornire | `DeviceRegistry` pe NVS |
| Senzorul se inroleaza **doar la cererea explicita a utilizatorului** | `DEV_STATE_IDLE` este starea implicita in `senzor/main.c`; fereastra se deschide numai din `ButtonPair_HeldLong()` (butonul 2 tinut ~3 s) si se inchide dupa `PAIRING_MAX_ATTEMPTS` incercari |
| Dupa `CMD_DOWN(RESET)` senzorul nu se re-inroleaza singur | ramura `CMD_TYPE_RESET` trece in `DEV_STATE_IDLE`, nu in `DEV_STATE_JOINING` |
| Inrolat, senzorul **doarme** intre transmisii si nu mai asteapta activ | `Sleep_Cycle()` in `senzor/main.c`, chemata la finalul ciclului din `DEV_STATE_OPERATING`; trezire pe WDT, fara timer si fara rutina de intrerupere |
| Somnul **nu** inghite fereastra de downlink | `Sleep_Cycle()` se cheama abia dupa ce fereastra de `DOWNLINK_WINDOW_MS` s-a inchis si eventualul `CMD_DOWN` a fost tratat (F-032) |
| Butonul raspunde si in timpul somnului | somnul e fragmentat in `SLEEP_WAKEUPS` reprize de ~2,11 s, cu butoanele citite la fiecare trezire; RC5 este pe PORTC, iar acest device NU are interrupt-on-change pe PORTC |
| Dupa `CMD_DOWN(RESET)` senzorul ramane **treaz** in repaus | bucla sare peste somn cand `deviceState != DEV_STATE_OPERATING`, deci reintra in `DEV_STATE_IDLE` cu latenta normala la buton |
| Somnul nu strica anti-replay-ul | `SLEEP` pastreaza RAM-ul, deci frame counter-ul si schema de checkpoint din F-022 raman neschimbate |
| Firmware-ul senzorului **incape** in PIC16LF1508 | **VERIFICAT** cu `xc8-cc` v3.10, `-O2`, cu HEF rezervat, in AMBELE configuratii: Production **3757** / 3968 words si **231** / 256 octeti, Debug cu Snap **3758** / 231. Ultimul cuvant de cod este la `0x0F7F` in ambele, deci regiunea HEF este curata. Marja ramasa: **211 cuvinte**. A fost nevoie de trecerea de la AES la XTEA (F-024) plus reducerile din F-025, F-028, F-029 si F-030. |

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

**Starea curenta**, masurata cu `xc8-cc` v3.10, `-O2`, cu HEF rezervat, in
ambele configuratii: **3757 / 3968 cuvinte utilizabile si 231 / 256
octeti** (Debug cu Snap: 3758 / 231). Ultimul cuvant de cod este la
`0x0F7F`, deci regiunea HEF este curata. Marja: 211 cuvinte si 25 de
octeti. Sketch-ul hub-ului compileaza pentru ESP32 Dev Module fara erori
si fara warning-uri proprii (cele 5 raman din `EthernetENC` si `LoRa`).
