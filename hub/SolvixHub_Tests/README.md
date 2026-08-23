# SolvixHub_Tests — varianta cu pairing criptat

Suita de teste hardware pentru hub-ul pe ESP32, ca un singur sketch
Arduino modular. Fata de varianta din `teste-sistemcomplet`, aici se
adauga **inrolarea senzorilor (pairing), criptarea datelor, registrul de
device-uri si stergerea unui device** — testul **8**.

## Cum se deschide

Arduino IDE cere ca folderul sketch-ului si fisierul `.ino` principal sa
aiba acelasi nume, ceea ce este deja respectat. Se deschide
`SolvixHub_Tests.ino`; celelalte fisiere apar automat ca tab-uri.

## Librarii necesare

Din Library Manager:

- **EthernetENC** (Juraj Andrassy) — driver pentru ENC28J60
- **LoRa** (Sandeep Mistry) — driver pentru SX1276/78

Placa: *ESP32 Dev Module*, din pachetul `esp32` by Espressif Systems.

> **Pentru criptografie nu este nevoie de nicio biblioteca.** Cifrul este
> XTEA-128, scris explicit in `HubCrypto.cpp`, fiindca trebuie sa fie
> identic bit cu bit cu implementarea de pe PIC (F-024). Fisierul se
> numeste `HubCrypto.h`, **nu** `Crypto.h`, ca sa nu ascunda antetul unei
> biblioteci cu acel nume daca cineva o instaleaza: Arduino IDE cauta
> intai in folderul sketch-ului (F-021).

## Cum se ruleaza

1. Serial Monitor la **115200** baud.
2. Terminator de linie: **Newline** (sau *Both NL & CR*).
3. Se scrie cifra testului dorit, sau o comanda, si se apasa Enter.

| Tasta | Test |
|-------|------|
| 1 | Butoane (GPIO34 / GPIO35) |
| 2 | Diagnostic SPI ENC28J60 |
| 3 | Ethernet: DHCP + internet |
| 4 | LoRa: emisie |
| 5 | LoRa: receptie |
| 6 | Coexistenta LoRa + Ethernet |
| 7 | Senzor: receptie temperatura **in clar** |
| 8 | **Pairing criptat: inrolare + date** |
| 0 | Opreste testul curent |
| m | Reafiseaza meniul |

Ordinea recomandata la o placa noua: **2 → 3 → 4/5 → 6 → 8**.

## Comenzi de pairing

Se scriu ca text, nu ca cifre, si merg si in timpul unui test:

| Comanda | Ce face |
|---------|---------|
| `pair` | Deschide fereastra de inrolare (porneste testul 8 daca nu ruleaza). LED 2 clipeste cat timp fereastra e deschisa. |
| `list` | Senzorii inrolati, din registrul salvat in NVS |
| `provisioned` | Senzorii care **au voie** sa se inroleze (lista din `Config.h`) |
| `remove <DevEUI>` | Il scoate din retea: la primul lui pachet primeste `CMD_DOWN(RESET)`, apoi dispare din registru |
| `remove <DevEUI> force` | Il sterge imediat din registru, fara sa il anunte |
| `stats` | Contoarele testului de pairing |
| `help` | Lista aceasta |

`DevEUI` se scrie ca 16 cifre hexazecimale, de exemplu
`534F4C5649580001`. Separatorii `-`, `:`, `.` si spatiul sunt ignorati.

**Butonul 1 (GPIO34)** deschide si el fereastra de pairing, ca sa nu fie
nevoie de un calculator langa hub. Este ascultat doar cand nu ruleaza
niciun test sau cand ruleaza chiar testul 8.

## Cum decurge o inrolare

1. `pair` pe hub — fereastra ramane deschisa `PAIRING_MODE_TIMEOUT_MS`
   (implicit 2 minute).
2. Senzorul ne-inrolat trimite `JOIN_REQ` (DevEUI + DevNonce + MIC).
3. Hub-ul cauta DevEUI in lista din `Config.h`, verifica MIC-ul cu
   `AppKey`, verifica sa nu fie un `DevNonce` deja folosit, aloca un
   `DevAddr`, deriva `SessKey` si raspunde cu `JOIN_ACCEPT`.
4. Senzorul despacheteaza `JOIN_ACCEPT` (CTR, deci fara cod de
   descifrare), deriva aceeasi `SessKey`, o scrie in HEF si trece in
   regim normal.
5. De aici incolo, temperatura vine ca `DATA_ENC` — criptata cu XTEA-CTR
   si semnata cu CBC-MAC. Dupa decriptare, payload-ul este **exact acelasi
   pachet de 6 octeti** ca la testul 7 si trece prin acelasi
   `SensorPacketCodec::decode()`.

Un senzor **neprovizionat** (DevEUI absent din `Config.h`) sau cu
`AppKey` gresita este refuzat cu mesaj explicit. Un `JOIN_REQ` sau un
`DATA_ENC` rejucat este respins si numarat separat in `stats`.

## Ce se pastreaza peste repornire

Registrul (`DevEUI`, `DevAddr`, `SessKey`, ultimul frame counter) traieste
in **NVS**, prin `Preferences`, in spatiul `solvix-pair`. Se salveaza la
fiecare inrolare, la fiecare stergere si o data la `REGISTRY_SAVE_EVERY`
pachete de date. NVS este flash: scrierea la fiecare pachet l-ar uza
degeaba, iar anti-replay-ul cere doar ca frame counter-ul sa fie **strict
crescator**, nu ca hub-ul sa tina minte ultima valoare la milisecunda.

## Magistrala SPI partajata

Ethernet-ul si LoRa stau pe aceleasi trei fire:

| Semnal | GPIO | Folosit de |
|--------|------|------------|
| SCK | 18 | ambele |
| MISO | 19 | ambele |
| MOSI | 23 | ambele |
| CS_ETH | 4 | doar ENC28J60 |
| RESET_ETH | 32 | doar ENC28J60 |
| NSS_LoRa | 5 | doar SX1276 |
| RST_LoRa | 14 | doar SX1276 |
| DIO0_LoRa | 26 | doar SX1276 |

## Restul pinilor

| Semnal | GPIO | Nota |
|--------|------|------|
| Buton 1 | 34 | input-only, rezistor extern obligatoriu; deschide pairing-ul |
| Buton 2 | 35 | input-only, rezistor extern obligatoriu |
| LED 1 (D22) | 22 | activitate: pulseaza la un pachet valid |
| LED 2 (D21) | 21 | stare: aprins cat asculta, **clipeste** in mod pairing |

LED-urile sunt presupuse **active HIGH**. Daca pe placa sunt cablate
invers, se schimba `LED_ON_LEVEL` in [Config.h](Config.h) — nicaieri
altundeva.

Un slave SPI isi elibereaza linia MISO doar cat timp CS-ul lui este
HIGH. Daca ambele CS-uri ajung LOW simultan, cele doua module trag de
acelasi fir: datele sunt gunoi si, pe termen lung, iesirile se pot
deteriora.

Masurile luate, toate in [SpiBus.h](SpiBus.h) si [SpiBus.cpp](SpiBus.cpp):

1. `SPI.begin()` se apeleaza **o singura data**, din `setup()`. Niciun
   test nu reinitializeaza magistrala.
2. Ambele CS-uri devin OUTPUT si urca pe HIGH **inainte** de orice
   trafic, ca sa nu ramana flotante la boot.
3. `SPI.begin(SCK, MISO, MOSI, -1)` — ultimul argument este `-1`
   intentionat, ca driverul ESP32 sa nu preia niciun pin drept CS
   hardware cu comutare automata.
4. `claimEthernet()` / `claimLoRa()` ridica CS-ul celuilalt modul inainte
   ca un test sa preia bus-ul.
5. Fiecare acces se face intr-o tranzactie SPI, deci fiecare modul isi
   impune propria viteza si propriul mod fara sa il afecteze pe celalalt.
   Clasa `SpiGuard` face acest lucru pentru codul propriu si ridica CS-ul
   automat in destructor, inclusiv pe caile de `return` timpuriu.
6. **Nu se apeleaza niciodata `LoRa.end()` sau `SPI.end()`.** `LoRa.end()`
   inchide magistrala SPI a intregului ESP32, iar ENC28J60 ar ramane fara
   ceas. Pentru oprirea radioului se foloseste `LoRa.sleep()`.
7. Receptia LoRa se face prin polling cu `LoRa.parsePacket()`, nu prin
   `LoRa.onReceive()`. Un callback pe DIO0 ar accesa SPI din context de
   intrerupere, posibil exact in mijlocul unui transfer Ethernet.
8. In testul de coexistenta, LoRa se initializeaza primul, apoi Ethernet.

## Note hardware

- **GPIO34 si GPIO35** sunt pini exclusiv de intrare si nu au pull-up sau
  pull-down intern. Fara rezistor extern, starea citita este zgomot.
- **CS-ul Ethernet-ului este GPIO4**, nu GPIO5. GPIO5 este NSS-ul LoRa.
- ENC28J60 nu raspunde la ICMP fara o librarie suplimentara, adesea
  instabila. Testul de internet foloseste in schimb o cerere HTTP GET.
- Adresa MAC este definita in [EthernetLink.cpp](EthernetLink.cpp) si
  trebuie sa fie unica in reteaua locala.

## Legatura cu nodul senzor

Parametrii de modulatie din [Config.h](Config.h) trebuie sa fie
**identici** cu valorile scrise in registrele SX1276 de firmware-ul
senzorului. **Pairing-ul nu schimba niciunul dintre ei**: inrolarea si
datele criptate circula pe exact aceeasi modulatie ca pachetul de
temperatura in clar.

Doua constante trebuie insa sa fie identice pe cele doua capete, altfel
totul pare "corect" dar nimic nu se valideaza:

| Pe hub (`Config.h`) | Pe senzor (`main.c`) |
|---------------------|----------------------|
| `PAIRING_ENCRYPT_PAYLOAD` | `PAIRING_ENCRYPT_PAYLOAD` |
| perechea din `PROVISIONED_DEVICES_INIT` | `PROVISION_DEV_EUI` + `PROVISION_APP_KEY` |

Formatul tuturor pachetelor este descris in
[SensorPacket.h](SensorPacket.h) si oglindit in sectiunea 4 din
`senzor/main.c`. Cele doua se modifica **impreuna**.
