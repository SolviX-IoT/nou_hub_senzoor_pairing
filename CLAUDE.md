# CLAUDE.md — SolviX HUB / Pairing criptat

> Fisier de context permanent pentru Claude Code si pentru orice om care
> intra in proiect. **Se actualizeaza la FIECARE commit**: vezi sectiunea
> [Regula de actualizare](#10-regula-de-actualizare) de la final.
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
| **Senzor** | PIC16LF1508 + RFM96 (SX1276) + NTC 10K 3950 + TPL5110 | MPLAB X IDE, compilator XC8, drivere MCC Melody | Se inroleaza la hub, apoi masoara temperatura si o trimite **criptata** prin LoRa |
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
4. **Stergere** — `remove <DevEUI>` scoate un device din retea si ii
   trimite un `RESET` la primul contact.
5. **Receptie pe senzor** — driverul LoRa al senzorului era doar
   emitator; acum are si `LoRa_Receive()`.
6. **Memorie ne-volatila pe senzor** — HEF (High-Endurance Flash), pentru
   identitate, cheie de sesiune si frame counter.

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
   | PIC16LF1508 are | 4096 | 256 |
   | firmware-ul actual | **3761** (91.8%) | **250** (97.7%) |

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

2. **Sleep-ul este DEZACTIVAT** (problema hardware la TPL5110, de rezolvat
   ulterior). Senzorul ramane alimentat permanent, transmite pe intervalul
   software existent (`TX_INTERVAL_MS`) si tine RC1 (DONE) **LOW
   permanent** (F-018). RAM-ul se pastreaza intre transmisii, deci frame
   counter-ul poate sta in RAM si se salveaza in HEF doar la fiecare
   `FCNT_CHECKPOINT_EVERY` pachete.
   **Cand TPL5110 se repara si sleep-ul se reactiveaza**, fiecare trezire
   redevine cold boot cu RAM pierdut: counter-ul va trebui scris la
   fiecare ciclu, inelul de sloturi din HEF va trebui marit (sau inlocuit
   cu un FRAM extern), iar join-ul va trebui incadrat intr-o fereastra de
   alimentare.

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
| **RC5** | **Buton 2** (activ HIGH, pull-down extern) | intrare digitala | `TRISC5=1` | `main.c` |
| **RC3** | **LED 1** — transmisie de date | iesire | `ANSC3=0`, `TRISC3=0` | `main.c` |
| **RC6** | **LED 2** — pairing / eroare de join | iesire | `ANSC6=0`, `TRISC6=0` | `main.c` |
| **RC1** | **TPL5110 DONE** | iesire, **LOW permanent** | `ANSC1=0`, `TRISC1=0` | `main.c` (F-018) |
| RA0 / RA1 | ICSPDAT / ICSPCLK (programare) | — | `LVP=ON` | `config_bits.c` |
| RA3 | MCLR / VPP | intrare | `MCLRE=ON` | `config_bits.c` |

**Rolul LED-urilor s-a schimbat** fata de firmware-ul de temperatura:
inainte LED1 = transmisie periodica si LED2 = transmisie fortata de
buton; acum **LED1 = transmisie de date** (orice `DATA_ENC` reusit) si
**LED2 = pairing** (aprins in timpul unei incercari de join, trei clipiri
scurte la esec, doua pulsuri la reusita, un puls la primirea unui ACK).

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

La `RESET`, senzorul sterge `SessKey` + `DevAddr` din HEF si revine in
starea "ne-inrolat".

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
`lastFrameCounterUp`, `downCounter`, `lastDevNonce`, numarul de pachete si
marcajul `pendingReset`.

Se salveaza la fiecare inrolare, la fiecare stergere si o data la
`REGISTRY_SAVE_EVERY` (implicit 20) pachete de date. NVS este flash:
scrierea la fiecare pachet l-ar uza degeaba, iar anti-replay-ul cere doar
ca frame counter-ul sa fie **strict crescator**.

Structura salvata are un numar de versiune (`REGISTRY_BLOB_VERSION`): daca
`DeviceRecord` se modifica, registrul vechi este ignorat in loc sa fie
interpretat gresit octet cu octet.

---

## 8. Ce face fiecare fisier

### 8.1. `senzor/` — proiect MPLAB X, nodul senzor

| Fisier | Rol |
|--------|-----|
| `main.c` | **Firmware-ul complet**, in 16 sectiuni numerotate: parametri, pini, registre SX1276, protocol, HEF, XTEA/CBC-MAC/CTR, starea device-ului, NVM, driver LoRa (TX **si RX**), ADC+NTC, butoane, LED-uri, construirea pachetelor, initializare, inrolare, bucla principala. |
| `main_powercycle_test.c.bak` | Testul provizoriu de power-cycle prin TPL5110. Pastrat ca referinta pentru F-011…F-014. Extensia `.bak` il tine in afara compilarii, iar `nbproject/configurations.xml` listeaza oricum doar `main.c`. |
| `mcc_generated_files/system/src/config_bits.c` | Configuration bits: `FOSC=INTOSC`, `WDTE=OFF`, `MCLRE=ON`, `BOREN=ON`, `LVP=ON`, `PWRTE=OFF`. **`WRT=OFF` este obligatoriu pentru HEF.** |
| `mcc_generated_files/system/src/clock.c`, `clock.h` | Oscilator intern la **16 MHz** (`_XTAL_FREQ = 16000000`). |
| `mcc_generated_files/system/src/pins.c` | `PIN_MANAGER_Initialize()`: `TRISA=0x3F`, `TRISB=0xB0`, `TRISC=0x37`, `ANSELA=0x17`, `ANSELB=0x20`, `ANSELC=0x06`, pull-up-uri pe PORTA/PORTB. |
| `mcc_generated_files/system/pins.h` | Macro-uri `IO_RCx_SetHigh/SetLow/GetValue/...`. |
| `mcc_generated_files/spi/src/mssp.c` | Driverul SPI. `Lora_SPI` expune `Open/Close/ByteExchange/...`. Config **index 0**: `SSP1CON1=0x0A`, `SSP1ADD=0x1F` -> **125 kHz**. |
| `mcc_generated_files/system/src/system.c` | `SYSTEM_Initialize()` = clock + pini + SPI1 + intreruperi. |
| `mcc_generated_files/system/src/interrupt.c` | Vector de intreruperi generat; **nu este folosit efectiv**. |
| `nbproject/`, `Makefile*` | Fisiere de proiect MPLAB X. Doua setari sunt **obligatorii** si sunt deja aplicate in `configurations.xml`: `optimization-level = -O2` (cu `-O0` firmware-ul nu incape) si `code-model-rom = default,-f80-fff` (rezerva regiunea HEF, F-027). |
| `Datasheets/TPL5110.PDF` | Datasheet-ul timerului de alimentare. |

### 8.2. `hub/SolvixHub_Tests/` — suita de teste ESP32

| Fisier | Rol |
|--------|-----|
| `SolvixHub_Tests.ino` | Sketch principal: meniu pe Serial (115200), tabloul `TESTS[]`, **comenzile in cuvinte** (`pair`, `list`, `provisioned`, `remove`, `stats`, `help`), butonul 1 ca declansator de pairing, `setup()` care porneste SPI si incarca registrul. |
| `Config.h` | **Singura sursa de adevar pentru pini** si constante: SPI, ETH, LoRa (inclusiv modulatia), butoane, LED-uri, si **sectiunea de pairing**: `PAIRING_MODE_TIMEOUT_MS`, `PAIRING_BLINK_MS`, `PAIRING_ENCRYPT_PAYLOAD`, `PAIRING_SEND_ACK`, `REGISTRY_*` si lista `PROVISIONED_DEVICES_INIT`. |
| `SpiBus.h`, `SpiBus.cpp` | Arbitrajul magistralei SPI partajate; `SpiGuard` ridica CS-ul in destructor. |
| `TestBase.h`, `TestBase.cpp` | Structura `Test { name, description, begin, tick, stop }` + ajutoare de afisare. |
| `LoRaRadio.h`, `LoRaRadio.cpp` | Invelis peste libraria LoRa: `begin()`, `sendText()`, **`sendRaw()` (NOU)**, `receive()`, `receiveRaw()`, `sleep()`. Receptia e prin polling. |
| `Leds.h`, `Leds.cpp` | Cele doua LED-uri (D22/D21). `set()` pentru stare, `pulse()` pentru evenimente, `service()` fara `delay()`. |
| `SensorPacket.h`, `SensorPacket.cpp` | **Oglinda protocolului din `senzor/main.c`**: constantele tuturor tipurilor, `decode()`/`print()`/`printRaw()` pentru temperatura, plus `messageType()`, `parseJoinRequest()`, `parseEncryptedData()`, `buildJoinAccept()`, `buildCommand()`, `printEui()`. |
| `HubCrypto.h`, `HubCrypto.cpp` | **NOU.** XTEA-128 (bloc de 8 octeti), CBC-MAC-XTEA, XTEA-CTR, `buildDataIv()`, `buildJoinIv()`, `deriveSessionKey()`. **Nu depinde de nicio biblioteca** (F-024). Numele **nu** este `Crypto.h`, ca sa nu ascunda antetul unei biblioteci cu acel nume (F-021). |
| `DeviceRegistry.h`, `DeviceRegistry.cpp` | **NOU.** Registrul senzorilor inrolati, salvat in NVS prin `Preferences`; cautare dupa EUI/adresa, alocare de `DevAddr`, lista de provisioning din `Config.h`. |
| `EthernetLink.h`, `EthernetLink.cpp` | Invelis peste EthernetENC: DHCP cu timeout, `printStatus()`, cerere HTTP GET. Aici este definit `HUB_MAC`. |
| `TestButtons.*` | Citeste GPIO34/35 si numara tranzitiile, ca sa se vada liniile flotante. |
| `TestEncSpi.*` | Diagnostic SPI de nivel jos pe ENC28J60; verifica `EREVID`. |
| `TestEthernet.*` | DHCP + DNS + HTTP GET. |
| `TestLoRaTx.*` | Emisie LoRa: un pachet numerotat la fiecare 2 s. |
| `TestLoRaRx.*` | Receptie LoRa cu RSSI si SNR. |
| `TestCoexistence.*` | Ambele module active alternativ pe acelasi bus. LoRa se initializeaza **primul**. |
| `TestSensorRx.*` | **Testul 7:** asculta pachetul de temperatura **in clar**. Ramane util la bring-up, cu `ENABLE_PLAIN_TEMP = 1` pe senzor. |
| `TestPairing.*` | **NOU — testul 8:** fereastra de pairing cu timeout, tratarea `JOIN_REQ` (provisioning + MIC + anti-replay + alocare de adresa + `JOIN_ACCEPT`), tratarea `DATA_ENC` (adresa + counter + MIC + decriptare + `decode()`), trimiterea `CMD_DOWN` (ACK/RESET) si contoarele pentru `stats`. |
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
- **Simptom:** placa nu se oprea la apasarea butonului.
- **Cauza:** TPL5110, sectiunea 8.5.2 din datasheet: "DONE signals received while the DELAY/M_DRV is HIGH are ignored". M_DRV are 20 ms de debounce pe ambele fronturi.
- **Fix:** se asteapta eliberarea butonului, apoi `TPL5110_MDRV_SETTLE_MS = 60 ms`, abia apoi se semnaleaza DONE.

### F-012 — Un singur front DONE nu e suficient (senzor)
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
12. **Cheile nu se afiseaza pe Serial.** `list` arata DevEUI si DevAddr,
    niciodata `SessKey` sau `AppKey`.
13. **Senzorul se compileaza cu `-O2`, iar regiunea HEF ramane
    rezervata** (`code-model-rom = default,-f80-fff`). Cu `-O0`
    firmware-ul nu mai incape, iar fara rezervare linkerul pune cod peste
    HEF (F-027). Cele doua se seteaza **din fereastra de proprietati a
    proiectului**, nu editand fisierele: MPLAB X rescrie
    `Makefile-default.mk` la fiecare build (F-029). Dupa orice adaugare
    de cod pe senzor, **citeste raportul de memorie** — marja este de
    ~125 de cuvinte si 25 de octeti.
14. **Pe senzor, evita `int32_t` in codul fierbinte** (F-028). O
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
| `remove <DevEUI>` + `RESET` dezinroleaza curat | `commandRemove()` + `pendingReset` in `handleEncryptedData()` |
| Registrul hub-ului persista peste repornire | `DeviceRegistry` pe NVS |
| Firmware-ul senzorului **incape** in PIC16LF1508 | **VERIFICAT** cu `xc8-cc` v3.10, `-O2`, cu HEF rezervat, in AMBELE configuratii: Production 3843 words / 231 octeti, Debug cu Snap 3844 / 231. A fost nevoie de trecerea de la AES la XTEA (F-024) plus reducerile din F-025, F-028 si F-029. |

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

Mesajul de commit trebuie sa mentioneze eticheta `F-0xx` atunci cand
commit-ul rezolva un bug din sectiunea 9.

---

*Ultima actualizare: 2026-08-23 — pairing criptat, incadrat in
PIC16LF1508. Auditul primei versiuni a aratat ca schema pe AES-128 nu
incapea (5426 de cuvinte / 446 de octeti fata de 4096 / 256 disponibili),
deci cifrul a fost inlocuit cu **XTEA-128** cu CBC-MAC si CTR (F-024) —
pairing-ul, cheia de sesiune, anti-replay-ul si criptarea payload-ului
raman toate. Alte corectii din audit: RAM-ul real este 256 B, nu 512
(F-025); randul de HEF are 32 de cuvinte, nu 16, deci harta are 4 randuri
(F-026); regiunea HEF este acum rezervata din linker, fiindca altfel
codul se scria peste ea (F-027); aritmetica pe 32 de biti a fost scoasa
din codul fierbinte (F-028). `JOIN_ACCEPT` s-a scurtat de la 22 la 10
octeti. Hub-ul nu mai depinde de biblioteca `Crypto`. Rezultat masurat cu
`xc8-cc` v3.10, `-O2`, cu HEF rezervat: **3761/4096 cuvinte (91.8%) si
250/256 octeti (97.7%)**.*
