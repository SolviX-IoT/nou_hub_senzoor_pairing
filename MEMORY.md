# MEMORY.md — starea curenta a proiectului

> Digest scurt: **ce este adevarat acum**. Cifre, versiuni de format, ce se
> schimba la fiecare placa. Se citeste cand intrebi "unde am ramas" sau cand
> verifici ce s-a programat pe cip.
>
> Referinta completa (pini, protocol, reguli, conventia de commit) este in
> [CLAUDE.md](CLAUDE.md). Motivul din spatele fiecarei decizii este in
> [ISTORIC.md](ISTORIC.md).
>
> **Se actualizeaza ori de cate ori se schimba o cifra de aici** — vezi
> [CLAUDE.md §8](CLAUDE.md).

---

## Ce este proiectul

Doua noduri care comunica prin **LoRa 868 MHz**. Un **senzor** pe PIC16LF1508
masoara temperatura cu un NTC si o trimite in clar; un **hub** pe ESP32 tine
pana la 5 senzori inrolati, cu registrul lor in NVS. Peste temperatura s-a
adaugat **inrolarea de device-uri (pairing)**, un registru pe hub si stergerea
confirmata a unui device.

Inrolarea este o **comisionare** — cine e in retea, ce numar are, de unde incep
contoarele — **nu un control de acces**.

| Nod | Hardware | Toolchain |
|-----|----------|-----------|
| Senzor | PIC16LF1508 + RFM96 (SX1276) + NTC 10K 3950 | MPLAB X, XC8 v3.10, drivere MCC Melody |
| Hub | ESP32 Dev Module + RFM96 (SX1276) + ENC28J60 | Arduino IDE, placa *ESP32 Dev Module* |

Librarii necesare pe hub: **EthernetENC** (Juraj Andrassy), **LoRa**
(Sandeep Mistry) si **ArduinoJson v7** (Benoit Blanchon).

---

## Avertisment: reteaua NU este autentificata

Criptografia a fost scoasa la **2026-08-29** (F-038) fiindca nu mai incapea in
PIC16LF1508. Nu exista MIC, cheie sau nonce: oricine cu un radio pe aceiasi
parametri poate injecta o temperatura falsa, poate dezinrola orice placa cu
patru octeti, poate inrola o placa falsa cat fereastra e deschisa si poate
rejuca orice pachet. Singura aparare pe calea de date este **frame counter-ul
strict crescator**.

Este o masura temporara, pana la un microcontroller cu mai multa memorie.
**Ultima versiune cu cifru este commit-ul `a710142`** — de acolo se
reintroduce. **Nu compensa intre timp cu nimic facut in casa.**

---

## Incadrarea in PIC16LF1508 — masurata, nu presupusa

| Configuratie | Flash (words) | RAM (octeti) |
|---|---|---|
| PIC16LF1508 are | 4096 (**3968 utilizabili**, HEF rezervat) | 256 |
| **Production, `-O2`** | **2395** | **95** |
| **Debug cu Snap, `-O2`** | **2396** | **95** (din care 16 ai depanatorului) |

**Marja: 1573 de cuvinte si 161 de octeti.** Pentru prima data de la inceputul
proiectului, memoria nu mai este constrangerea dominanta. Ultimul cuvant de cod
este la `0x0E83`, deci regiunea HEF (`0x0F80`–`0x0FFF`) este curata.

Cifrele sunt din `xc8-cc` v3.10 si sunt **identice pentru toate cele cinci
valori** ale lui `SENSOR_NODE_ID`.

> **Doua setari obligatorii, din fereastra de proprietati a proiectului MPLAB X
> — nu editand fisiere** (IDE-ul rescrie `Makefile-default.mk` la fiecare
> build, F-029):
> - *XC8 Compiler → Optimizations → Optimization level* = **`-O2`**
> - *XC8 Linker → Memory model → ROM ranges* = **`default,-f80-fff`**
>   (fara asta, linkerul pune cod peste HEF si prima inrolare isi sterge
>   propriul program — F-027)

**Hub:** sketch-ul compileaza pentru ESP32 Dev Module fara erori si fara
warning-uri proprii (cele ramase sunt din `EthernetENC` si `LoRa`) si ocupa
**356 kB din 1310 kB** de flash, cu **25,6 kB** de RAM global.

Cifra era 351 kB inainte de 2026-09-01. Stergerea celor sapte teste (F-039) a
dat inapoi ~26 kB, iar `ArduinoJson` plus modulele noi de retea au adaugat
~31 kB: net, aproape neutru.

> **Atentie la WiFi, cand va veni.** Stiva ESP32 de WiFi adauga 350–500 kB si
> ar duce sketch-ul pe la 800–900 kB. Incape in 1310 kB, dar **inchide usa
> OTA**: o schema cu doua partitii de aplicatie da fiecareia ~640 kB, si 900
> nu intra. Iar `autoFirmwareUpdate` este un camp pe care serverul chiar il
> trimite in `config`. **Schema de partitii se decide inainte de WiFi, nu
> dupa.**

---

## Hub-ul in cloud — ce este adevarat acum

Hub-ul porneste singur, asculta senzorii, isi ia adresa prin DHCP si se
provizioneaza la `http://84.117.97.136:7039`. Doi pasi, in ordine:

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

**Cele 11 valori de `config` se salveaza, dar NU se folosesc inca.** Heartbeat
si telemetrie sunt etapa urmatoare; carligul este `SensorLink::onReading()`,
inca neinregistrat. La fel `maxSensors` de la server: se salveaza, se compara
cu `HUB_MAX_SENSORS` si se anunta nepotrivirea, dar valoarea locala ramane cea
care dimensioneaza registrul.

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
`IDENTITY_BLOB_VERSION` are voie sa creasca vreodata si daca `forget yes` este
o operatie sigura. Pana atunci, codul nu re-provizioneaza niciodata singur.

---

## Versiuni de format — ambele capete pornesc golite impreuna

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

## Ce se schimba la fiecare placa

**O singura linie**, in `senzor/main.c`:

```c
#define SENSOR_NODE_ID          N        /* N = 1..5 */
```

Din ea ies `PROVISION_DEV_EUI` (`"SOLVIX" | 0x00 | N`) si slotul de somn. Pe
hub nu se schimba nimic: `PROVISIONED_DEVICES_INIT` din `Config.h` este deja
completat pentru toate cele 5 placi, iar **ordinea randurilor da numarele
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

## Dupa fiecare programare

1. **Verifica cifra din raportul de memorie** (fereastra de build sau
   `senzor/dist/default/production/senzor.production.mum`): trebuie sa fie
   **2395 / 95**. Alta cifra inseamna alt cod decat cel din `main.c` — si nicio
   cautare in schema nu are rost pana nu se potriveste (F-033).
2. **Verifica pe hub ca placa apare cu numarul asteptat:** comanda `sensors`.
   Doua placi programate din greseala cu acelasi `SENSOR_NODE_ID` au acelasi
   `DevEUI`, iar a doua o inlocuieste pe prima in registru. `provisioned` arata
   ce numar ar trebui sa aiba fiecare `DevEUI`.

---

## Ramas de facut

- `PINOUT_config.pdf` inca arata **RC1 -> TPL5110**. Componenta a fost scoasa
  din proiectare la 2026-08-26; RC1 este acum un pin liber, fara cod.
