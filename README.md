# SolviX — pairing intre senzor si hub

Acest folder porneste din `teste-sistemcomplet/` si pastreaza aceeasi
arhitectura: un folder **`senzor/`** (proiect MPLAB X pentru PIC16LF1508)
si un folder **`hub/`** (sketch Arduino pentru ESP32). Peste
functionalitatea de temperatura, care ramane intacta, se adauga
**inrolarea de device-uri, un registru de senzori pe hub si stergerea
unui device**. Hub-ul tine **pana la 5 senzori** simultan, fiecare cu un
numar fix de la 1 la 5.

> **Criptografia a fost scoasa la 2026-08-29 (F-038)**, fiindca nu mai
> incapea in PIC16LF1508: firmware-ul ajunsese la 97,7% ocupare, cu 92 de
> cuvinte marja. **Reteaua nu mai este autentificata** — oricine cu un
> radio LoRa pe aceiasi parametri poate injecta date sau dezinrola o
> placa. Este o masura temporara, pana la un microcontroller cu mai multa
> memorie; ultima versiune cu cifru este commit-ul `a710142`.

Contextul complet — pini, protocol, memorie ne-volatila, istoric de
bug-uri — este in [CLAUDE.md](CLAUDE.md). Instructiunile de utilizare ale
hub-ului sunt in [hub/SolvixHub_Tests/README.md](hub/SolvixHub_Tests/README.md).

## Ce face sistemul

1. Un senzor ne-inrolat trimite `JOIN_REQ`, cu `DevEUI`-ul lui.
2. Hub-ul il accepta **doar cat timp este in mod pairing** (comanda
   `pair` pe Serial sau apasarea butonului 1) si doar daca `DevEUI`-ul
   este in lista din `Config.h`.
3. Hub-ul ii da **numarul lui** ca `DevAddr` — pozitia placii in tabelul
   de provisioning, deci acelasi numar de fiecare data — si raspunde cu
   `JOIN_ACCEPT`.
4. Senzorul salveaza inrolarea in HEF si trimite de aici incolo
   temperatura ca `DATA_UP`, in clar, cu frame counter strict crescator
   impotriva replay-ului. Intre doua
   pachete **doarme un interval propriu numarului lui** (23–38 s
   nominal) si se trezeste pe watchdog; butonul este citit la fiecare
   trezire, deci raspunde in cel mult ~2 secunde.
5. `remove <DevEUI>` scoate un senzor din retea. Hub-ul ii trimite
   `CMD_DOWN(RESET)` la **fiecare** pachet al lui si sterge inregistrarea
   abia dupa ce senzorul tace — tacerea este dovada ca a primit comanda
   (F-031). Senzorul isi sterge inrolarea si ramane **in repaus**.

Pasii 1 si 2 cer amandoi o interventie umana: senzorul trimite `JOIN_REQ`
doar dupa ce i se tine butonul 2 (RC5) apasat ~3 secunde, iar hub-ul il
asculta doar in fereastra deschisa cu `pair` (F-030).

Payload-ul este **exact acelasi pachet de 6 octeti** ca inainte, deci
trece prin acelasi cod de interpretare a temperaturii — acelasi pe care
il foloseste si testul 7.

## Cei 5 senzori

Fiecare senzor are un **numar fix, 1..5**, care este in acelasi timp
`DevAddr`-ul lui din protocol si pozitia lui in tabelul de provisioning
din `Config.h`. Numarul nu depinde de ordinea inrolarii si nu se schimba
dupa o dezinrolare, deci poate fi scris pe cutie.

- **De la cine SPUNE ca vine data:** `DevAddr` circula in clar in fiecare
  `DATA_UP`. Fara MIC, atributia este **declarativa**: orice emitator
  poate pretinde orice numar. Ce ramane adevarat este ca doua pachete
  suprapuse in aer se pierd amandoua, deci nu exista date amestecate —
  oricare ajunge, ajunge intreg.
- **Ca sa nu vorbeasca toti odata:** fiecare senzor doarme un interval
  propriu numarului lui (23,2 / 25,3 / 27,4 / 29,6 / 31,7 s nominal) plus
  un jitter aleator la fiecare ciclu. Doi senzori care s-au ciocnit o
  data se despart de la sine dupa o perioada, in loc sa ramana ciocniti.
- **Ce se vede pe hub:** comanda `sensors` da tabelul celor cinci locuri
  — ultima temperatura, de cat timp nu s-a mai auzit fiecare, RSSI,
  pachete primite si pachete pierdute. Un senzor care amuteste este
  anuntat o data, si tot o data la revenire.

## Ce trebuie schimbat la fiecare placa

**O singura linie.**

| Pe senzor (`senzor/main.c`) | Pe hub (`hub/SolvixHub_Tests/Config.h`) |
|-----------------------------|------------------------------------------|
| `SENSOR_NODE_ID` = 1..5 — din el ies `PROVISION_DEV_EUI`, `PROVISION_APP_KEY` si slotul de somn | randul N din `PROVISIONED_DEVICES_INIT`, deja completat pentru toate cele 5 placi |
| `PAIRING_ENCRYPT_PAYLOAD` | `PAIRING_ENCRYPT_PAYLOAD` — **identic** |

Ordinea randurilor din `PROVISIONED_DEVICES_INIT` **da numerele
senzorilor**; nu se rearanjeaza intr-o retea deja instalata.

O placa deja folosita poate fi reprogramata cu alt numar fara nicio
procedura speciala: firmware-ul observa la pornire ca `DevEUI`-ul din HEF
nu mai este cel compilat, rescrie identitatea si sterge inrolarea veche.
Placa porneste in repaus si asteapta o inrolare noua.

## Incadrarea in PIC16LF1508 — masurata, nu presupusa

| configuratie | flash (words) | RAM (octeti) |
|---|---|---|
| PIC16LF1508 are | 4096 (3968 utilizabili, HEF rezervat) | 256 |
| Production, `-O2` | **2395** | **95** |
| Debug cu Snap, `-O2` | **2396** | **95** |

Cifrele sunt din `xc8-cc` v3.10 si sunt aceleasi pentru toate cele cinci
valori ale lui `SENSOR_NODE_ID`: doar ramura placii curente se
compileaza.

> **Doua setari sunt obligatorii si se fac din fereastra de proprietati a
> proiectului MPLAB X, nu editand fisiere** — IDE-ul rescrie
> `Makefile-default.mk` la fiecare build (F-029):
> - *XC8 Compiler → Optimizations → Optimization level* = **`-O2`**
>   (cu `-O0` firmware-ul nu intra);
> - *XC8 Linker → Memory model → ROM ranges* = **`default,-f80-fff`**
>   (fara asta, linkerul pune cod peste regiunea HEF si prima inrolare
>   isi sterge propriul program — F-027).

Marja este acum de **1573 de cuvinte si 161 de octeti**. Pana la
2026-08-29 firmware-ul avea criptografie si ocupa 3876 de cuvinte, adica
97,7% — 92 de cuvinte marja, in care nu mai incapea nimic. Scoaterea
cifrului (F-038) a eliberat 1481 de cuvinte si 137 de octeti. Detalii in
[CLAUDE.md](CLAUDE.md), sectiunea 2 si F-038.

## Librarii necesare pe hub

`EthernetENC` (Juraj Andrassy) si `LoRa` (Sandeep Mistry). Placa:
*ESP32 Dev Module*.
