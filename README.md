# SolviX — pairing criptat intre senzor si hub

Acest folder porneste din `teste-sistemcomplet/` si pastreaza aceeasi
arhitectura: un folder **`senzor/`** (proiect MPLAB X pentru PIC16LF1508)
si un folder **`hub/`** (sketch Arduino pentru ESP32). Peste
functionalitatea de temperatura, care ramane intacta, se adauga
**inrolarea de device-uri, criptarea datelor, un registru de senzori pe
hub si stergerea unui device**.

Contextul complet — pini, protocol, memorie ne-volatila, istoric de
bug-uri — este in [CLAUDE.md](CLAUDE.md). Instructiunile de utilizare ale
hub-ului sunt in [hub/SolvixHub_Tests/README.md](hub/SolvixHub_Tests/README.md).

## Ce face sistemul

1. Un senzor ne-inrolat trimite `JOIN_REQ`, semnat cu `AppKey`.
2. Hub-ul il accepta **doar cat timp este in mod pairing** (comanda
   `pair` pe Serial sau apasarea butonului 1), doar daca `DevEUI`-ul este
   in lista din `Config.h` si doar daca MIC-ul este corect.
3. Hub-ul aloca un `DevAddr`, deriva `SessKey` si raspunde cu
   `JOIN_ACCEPT`.
4. Senzorul salveaza cheia in HEF si trimite de aici incolo temperatura
   ca `DATA_ENC`: XTEA-CTR pe payload, CBC-MAC-XTEA pentru autenticitate,
   frame counter strict crescator impotriva replay-ului. Intre doua
   pachete **doarme ~30 de secunde** si se trezeste pe watchdog; butonul
   este citit la fiecare trezire, deci raspunde in cel mult ~2 secunde.
5. `remove <DevEUI>` scoate un senzor din retea. Hub-ul ii trimite
   `CMD_DOWN(RESET)` la **fiecare** pachet al lui si sterge inregistrarea
   abia dupa ce senzorul tace — tacerea este dovada ca a primit comanda
   (F-031). Senzorul isi sterge cheia si ramane **in repaus**.

Pasii 1 si 2 cer amandoi o interventie umana: senzorul trimite `JOIN_REQ`
doar dupa ce i se tine butonul 2 (RC5) apasat ~3 secunde, iar hub-ul il
asculta doar in fereastra deschisa cu `pair` (F-030).

Dupa decriptare, payload-ul este **exact acelasi pachet de 6 octeti** ca
inainte, deci trece prin acelasi cod de interpretare a temperaturii.

## Ce trebuie schimbat la fiecare placa

| Pe senzor (`senzor/main.c`) | Pe hub (`hub/SolvixHub_Tests/Config.h`) |
|-----------------------------|------------------------------------------|
| `PROVISION_DEV_EUI` | acelasi DevEUI in `PROVISIONED_DEVICES_INIT` |
| `PROVISION_APP_KEY` | aceeasi AppKey in `PROVISIONED_DEVICES_INIT` |
| `PAIRING_ENCRYPT_PAYLOAD` | `PAIRING_ENCRYPT_PAYLOAD` — **identic** |

## Incadrarea in PIC16LF1508 — masurata, nu presupusa

| configuratie | flash (words) | RAM (octeti) |
|---|---|---|
| PIC16LF1508 are | 4096 (3968 utilizabili, HEF rezervat) | 256 |
| Production, `-O2` | **3757** | **231** |
| Debug cu Snap, `-O2` | **3758** | **231** (16 sunt ai depanatorului) |

Cifrele sunt din `xc8-cc` v3.10.

> **Doua setari sunt obligatorii si se fac din fereastra de proprietati a
> proiectului MPLAB X, nu editand fisiere** — IDE-ul rescrie
> `Makefile-default.mk` la fiecare build (F-029):
> - *XC8 Compiler → Optimizations → Optimization level* = **`-O2`**
>   (cu `-O0` firmware-ul nu intra);
> - *XC8 Linker → Memory model → ROM ranges* = **`default,-f80-fff`**
>   (fara asta, linkerul pune cod peste regiunea HEF si prima inrolare
>   isi sterge propriul program — F-027).

Varianta cu AES-128 nu incapea: cerea 5250 de cuvinte si 286 de octeti,
chiar si dupa toate reducerile posibile. De aceea cifrul este XTEA-128
(F-024). Detalii in [CLAUDE.md](CLAUDE.md), sectiunea 2.

## Librarii necesare pe hub

`EthernetENC` (Juraj Andrassy) si `LoRa` (Sandeep Mistry). Criptografia
nu are nevoie de nicio biblioteca. Placa: *ESP32 Dev Module*.
