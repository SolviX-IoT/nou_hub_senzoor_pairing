# SolvixHub — hub-ul SolviX pe ESP32

Firmware-ul hub-ului, ca un singur sketch Arduino modular. Hub-ul
**porneste singur si ruleaza permanent**: asculta senzorii pe LoRa, ii
inroleaza la cerere, isi ia adresa in retea si se provizioneaza in cloud.

> **Pana la 2026-09-01 acesta a fost o SUITA DE TESTE** — un meniu pe
> Serial din care se pornea cate un test o data, iar produsul propriu-zis
> (inrolare + receptia temperaturilor) traia in „testul 8". Testele au
> fost sterse (F-039). Comenzile in cuvinte au ramas si s-au inmultit.

## Cum se deschide

Arduino IDE cere ca folderul sketch-ului si fisierul `.ino` principal sa
aiba acelasi nume. Se deschide `SolvixHub_Tests.ino`; celelalte fisiere
apar automat ca tab-uri.

## Librarii necesare

Din Library Manager:

- **EthernetENC** (Juraj Andrassy) — driver pentru ENC28J60
- **LoRa** (Sandeep Mistry) — driver pentru SX1276/78
- **ArduinoJson v7** (Benoit Blanchon) — raspunsurile API-ului

Placa: *ESP32 Dev Module*, din pachetul `esp32` by Espressif Systems.

> **Criptografia a fost scoasa la 2026-08-29 (F-038).** Nu mai incapea in
> PIC16LF1508: firmware-ul senzorului ajunsese la 97,7% ocupare, cu 92 de
> cuvinte marja. `HubCrypto.*` a disparut din sketch. **Reteaua nu mai
> este autentificata** — oricine cu un radio LoRa pe aceiasi parametri
> poate injecta date, dezinrola o placa sau inrola una falsa. Este o
> masura temporara; ultima versiune cu cifru este commit-ul `a710142`.

## Ce face la pornire

```
Leds -> SpiBus -> DeviceRegistry (NVS) -> HubIdentity (NVS)
     -> SensorLink (LoRa ASCULTA) -> NetLink (DHCP) -> HubCloud
```

Ordinea nu este intamplatoare. **Radioul porneste inaintea retelei si
esecul retelei nu opreste nimic:** legatura cu senzorii este produsul,
cloud-ul este un canal de raportare peste ea. Un hub fara cablu de retea
trebuie sa poata fi pus in functiune pe teren.

Dupa aceea, `HubCloud` parcurge singur:

1. asteapta o adresa IP;
2. `GET /api/health` — serverul este bun doar daca `"database"` este
   `"Reachable"`; `"status": "Healthy"` singur **nu** este suficient,
   fiindca API-ul poate raspunde perfect cu baza de date cazuta.
   La esec reincearca, cu backoff 5 / 10 / 30 / 60 s;
3. daca identitatea din flash este goala, `POST /api/device/provision` cu
   parametrii de fabrica din `Config.h`; raspunsul se salveaza in NVS,
   deci a doua pornire nu mai cere nimic.

Heartbeat-ul si telemetria sunt etapa urmatoare. Cele 11 valori din
`config` primite de la server **se salveaza, dar inca nu se folosesc**.

## Comenzi

Se scriu ca text si se termina cu Enter (Serial Monitor la **115200**,
terminator **Newline**).

| Comanda | Ce face |
|---------|---------|
| `pair` | Deschide fereastra de inrolare. LED 2 clipeste cat timp e deschisa |
| `sensors` | **Tabelul retelei:** toate locurile, cu ultima temperatura, vechimea ei, RSSI, pachete primite si pierdute |
| `list` | Senzorii inrolati, din registrul salvat in NVS |
| `provisioned` | Senzorii care **au voie** sa se inroleze (lista din `Config.h`) |
| `remove <DevEUI>` | Il scoate din retea. Primeste `CMD_DOWN(RESET)` la **fiecare** pachet al lui, iar inregistrarea dispare abia dupa ce senzorul **tace** `REMOVE_CONFIRM_SILENCE_MS` (F-031). Refuzat daca senzorul nu a trimis niciodata nimic |
| `remove #3` | Acelasi lucru, dupa **numarul** senzorului |
| `remove <...> force` | Il sterge imediat din registru, fara sa il anunte. Recuperarea se face de la butonul 2 al senzorului |
| `stats` | Contoarele legaturii radio, apoi tabelul `sensors` |
| `net` | Transport, IP, reinnoiri DHCP, conexiuni TCP deschise/inchise. Reincearca DHCP daca legatura e jos |
| `hub` | Identitatea hub-ului. `apiKey` **mascat**, `provisioningSecret` deloc |
| `cloud` | Starea bootstrap-ului: ultimul status HTTP, ultima sanatate, esecuri, urmatoarea reincercare |
| `health` | Verifica acum serverul si baza de date |
| `provision` | Cere acum provisioning-ul. **Refuza daca e deja provizionat** |
| `forget yes` | Sterge identitatea din flash. `forget` gol doar avertizeaza. Senzorii **nu** sunt afectati |
| `mem` | Heap liber si minimul atins de la pornire |
| `reboot` | Salveaza registrul si reporneste |
| `help` | Lista aceasta |

`DevEUI` se scrie ca 16 cifre hexazecimale, de exemplu
`534F4C5649580001`. Separatorii `-`, `:`, `.` si spatiul sunt ignorati.
Numarul senzorului se scrie ca `#3` sau ca `3`.

**Butonul 1 (GPIO34)** deschide si el fereastra de pairing, ca sa nu fie
nevoie de un calculator langa hub.

## Regula care guverneaza tot ce se adauga in `loop()`

Senzorul isi deschide fereastra de receptie **imediat** dupa ce a emis si
o tine deschisa doar `DOWNLINK_WINDOW_MS` = 600 ms. In plus,
`LoRa.parsePacket()` pune modemul in **RX_SINGLE**, care expira dupa
~102 ms.

Prin urmare orice lucru lung pus in `loop()` **nu intarzie receptia, ci o
distruge**: pachetele nu se acumuleaza nicaieri. De aceea:

- consola citeste **un octet per apel**, nu cu `readStringUntil()`, care
  ar bloca o secunda intreaga daca terminalul e pe „No line ending";
- `HubCloud` si `NetLink::maintain()` trec prin doua porti: nu pornesc
  nimic cat timp un senzor tocmai a vorbit (`HTTP_QUIET_AFTER_RX_MS`),
  si nici cat timp o dezinrolare asteapta confirmare;
- `Ethernet.begin()` primeste timeouts mici (8 s / 2 s), fiindca
  `Ethernet.maintain()` le refoloseste la reinnoirea lease-ului, unde
  schimbul DHCP este **blocant**.

## Reteaua: pana la 5 senzori, fiecare cu numarul lui

Hub-ul tine `HUB_MAX_SENSORS` senzori — implicit **5**. Fiecare are un
**numar** fix, de la 1 la 5.

Numarul nu este o eticheta pusa pe deasupra: este chiar **`DevAddr`**,
octetul `[2]` din fiecare `DATA_UP` si din fiecare `CMD_DOWN`. Si nu
vine din ordinea inrolarii, ci din **pozitia senzorului in tabelul
`PROVISIONED_DEVICES_INIT` din `Config.h`**. Consecinta este tot rostul
schemei:

- randul 3 din tabel inseamna „Senzor #3" la prima inrolare, dupa o
  dezinrolare si o reinrolare, si dupa o golire completa a registrului;
- doua placi nu pot primi niciodata acelasi numar;
- numarul poate fi scris pe cutie si ramane adevarat.

Aceeasi cifra se scrie si pe placa, ca **`SENSOR_NODE_ID`** in
`senzor/main.c`. Este singura linie care se schimba intre cele cinci
firmware-uri: din ea ies acolo `DevEUI` si slotul de somn.
Randul N din tabel corespunde placii cu `SENSOR_NODE_ID = N`.

### De la cine vine data

`DevAddr` circula **in clar** in octetul `[2]`. De cand nu mai exista
MIC, atributia este **declarativa**: orice emitator cu aceiasi parametri
radio poate pretinde orice numar. Pana la F-038, adresa intra in zona
semnata cu cheia de sesiune a acelui senzor si nu putea fi falsificata.

Nu exista cazul „date amestecate intre senzori": doua pachete care se
suprapun in aer se pierd **amandoua**, deci ori un pachet ajunge intreg
si atribuit corect, ori nu ajunge deloc.

### Ca sa nu vorbeasca toti odata

Hub-ul **nu** programeaza sloturi si nu cere nimanui sa astepte; un
protocol de rezervare a canalului ar fi costat pe PIC16 mai mult decat
pierde astazi in coliziuni. In schimb, fiecare senzor doarme altfel
(`senzor/main.c`, sectiunea 1):

| Senzor | Treziri WDT | Interval nominal |
|--------|-------------|------------------|
| #1 | 11 + jitter 0..3 | 23,2 – 29,6 s |
| #2 | 12 + jitter 0..3 | 25,3 – 31,7 s |
| #3 | 13 + jitter 0..3 | 27,4 – 33,8 s |
| #4 | 14 + jitter 0..3 | 29,6 – 35,9 s |
| #5 | 15 + jitter 0..3 | 31,7 – 38,0 s |

Doua efecte, amandoua necesare. **Intervalul propriu** (din `DevAddr`)
face ca doi senzori care s-au ciocnit o data sa nu ramana ciocniti: se
despart de la sine dupa o perioada. **Jitter-ul aleator** de la fiecare
ciclu rupe si cazul in care doua placi ar nimeri acelasi numar de
treziri, si pornirea simultana dupa o pana de curent.

Un `DATA_UP` de 13 octeti sta pe aer ~41 ms la SF7/BW125/CR4-5, iar un
senzor emite o data la ~30 s: ocuparea canalului este de 0,15% per placa.
Cate o coliziune izolata tot se intampla, si de aceea hub-ul o **numara**
— vezi coloana `pierd.` din `sensors`.

### Cand un senzor amuteste

Un senzor care nu s-a mai auzit de `SENSOR_OFFLINE_MS` (150 s, adica
trei-patru masuratori ratate la rand) este anuntat o data pe Serial, si
tot o data la revenire. Cu o singura placa disparitia ei era evidenta —
nu mai curgea nimic; cu cinci, jurnalul curge la fel de repede si lipsa
exact a uneia trece neobservata.

## Cum decurge o inrolare

1. `pair` pe hub — fereastra ramane deschisa `PAIRING_MODE_TIMEOUT_MS`
   (implicit 2 minute).
2. Senzorul ne-inrolat trimite `JOIN_REQ` (10 octeti: antet + DevEUI).
3. Hub-ul cauta DevEUI in lista din `Config.h` si ii da numarul din
   POZITIA lui in acea lista, prin `JOIN_ACCEPT` (3 octeti).
4. Senzorul verifica faptul ca numarul primit este chiar cel pe care il
   asteapta (`SENSOR_NODE_ID`), il scrie in HEF si trece in regim normal.
   Daca nu se potriveste, join-ul esueaza vizibil, cu trei clipiri pe
   LED2 — diagnosticul care a inlocuit sirul de "MIC gresit".
5. De aici incolo, temperatura vine ca `DATA_UP`, in clar. Payload-ul
   este **exact acelasi pachet de 6 octeti** pe care il emite un senzor
   neinrolat, si trece prin acelasi `SensorPacketCodec::decode()`: nu
   exista doua cai de interpretare a temperaturii.

Un senzor **neprovizionat** (DevEUI absent din `Config.h`) este refuzat
cu mesaj explicit. Un `DATA_UP` rejucat este respins si numarat separat
in `stats` — este singura aparare ramasa pe calea de date.

**Senzorul nu se inroleaza singur.** Ne-inrolat sta in repaus si tace;
fereastra lui se deschide tinand **butonul 2 (RC5) apasat trei secunde**.
Deci pasul 1 si pasul 2 de mai sus cer amandoua o interventie umana: `pair`
pe hub *si* apasarea de pe senzor.

## Cum decurge o dezinrolare

Un downlink are o singura sansa: senzorul asculta doar 600 ms dupa fiecare
transmisie a lui. De aceea `remove` **nu** crede ca a reusit din prima
(F-031):

1. `remove <DevEUI>` doar **marcheaza** device-ul. Inregistrarea ramane
   pe loc.
2. La **fiecare** pachet primit de la el, hub-ul retrimite
   `CMD_DOWN(RESET)` si numara incercarea. Un senzor care inca emite este
   dovada ca nu a primit comanda. Pachetele lui nu se mai afiseaza ca
   masuratori.
3. Cand senzorul **tace** `REMOVE_CONFIRM_SILENCE_MS`, dezinrolarea este
   confirmata: el a primit RESET-ul, si-a sters inrolarea din HEF si a
   intrat in repaus. Abia acum inregistrarea dispare din registru.
4. Cu `PAIRING_REOPEN_AFTER_REMOVE = 1`, fereastra de pairing se
   redeschide singura. Pentru reinrolare mai este nevoie si de cele trei
   secunde pe butonul 2 al senzorului.

`list` arata device-urile aflate in acest proces, cu numarul de RESET-uri
trimise si cat a trecut de la ultimul.

**`remove` pe un senzor oprit** este refuzat: fara pachete de la el,
RESET-ul nu are cum sa plece, iar marcarea ar bloca inregistrarea la
nesfarsit. Ori porneste senzorul, ori foloseste `force`.

**Un senzor ramas inrolat pe cont propriu** (dupa un `force`, dupa o
golire a registrului, sau dupa o dezinrolare esuata) emite pachete pe care
hub-ul le vede ca `DevAddr` neinrolat. Se recupereaza de pe senzor: butonul 2
apasat trei secunde. Hub-ul aminteste asta la fiecare
`PAIRING_UNKNOWN_HINT_EVERY` pachete de acest fel.

## Ce se pastreaza peste repornire

> **La actualizarea la versiunea fara criptare (F-038):**
> `REGISTRY_BLOB_VERSION` a trecut pe **4**, fiindca `DeviceRecord` a
> pierdut `sessKey` si `lastDevNonce`. Registrul salvat de versiunea
> precedenta este ignorat, deci hub-ul porneste gol — dar **si senzorii
> pornesc goli**, fiindca `HEF_MAGIC_SESSION` s-a schimbat in acelasi
> commit. Ambele capete sunt deci in aceeasi stare, iar recuperarea este
> cea normala: `pair` pe hub plus butonul 2 tinut trei secunde pe fiecare
> placa. Numarul primit inapoi este acelasi ca inainte, fiindca vine din
> tabelul de provisioning, nu din ordinea inrolarii.

Registrul (`DevEUI`, `DevAddr`, ultimul frame counter, contoare) traieste
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
datele circula pe exact aceeasi modulatie ca pachetul de
temperatura in clar.

Cateva lucruri trebuie insa sa fie identice pe cele doua capete, altfel
totul pare "corect" dar nimic nu se valideaza:

| Pe hub (`Config.h`) | Pe senzor (`main.c`) |
|---------------------|----------------------|
| lungimile din `SensorPacket.h` (6/10/3/13/4) | aceleasi, in `main.c` sectiunea 4 — si trebuie sa ramana **distincte** |
| randul N din `PROVISIONED_DEVICES_INIT` | `SENSOR_NODE_ID = N`, din care iese `PROVISION_DEV_EUI` |
| `REMOVE_CONFIRM_SILENCE_MS` si `SENSOR_OFFLINE_MS` | `SLEEP_WAKEUPS_BASE`, `SLEEP_SLOT_MASK`, `SLEEP_JITTER_MASK` |

Ultima linie nu este o simetrie de forma, ci o dependenta reala: hub-ul
confirma dezinrolarea prin **tacere**, iar un senzor care doarme tace si
el. Daca somnul creste peste fereastra de confirmare, hub-ul sterge
inregistrarea in timp ce senzorul doar doarme, iar la trezire senzorul
emite catre un hub care nu il mai recunoaste (F-031, F-034, F-036). Atentie in special la
cresterea lui `HUB_MAX_SENSORS`: ultimul senzor primeste automat cel mai
lung interval de somn.

### Constantele de pairing din `Config.h`

| Constanta | Implicit | Ce face |
|-----------|----------|---------|
| `PAIRING_MODE_TIMEOUT_MS` | 120000 | Cat sta deschisa fereastra de inrolare |
| `PAIRING_BLINK_MS` | 250 | Ritmul de clipire al LED 2 in mod pairing |
| `PAIRING_SEND_ACK` | 1 | Trimite `CMD_DOWN(ACK)` dupa fiecare pachet valid |
| `REMOVE_CONFIRM_SILENCE_MS` | 180000 | Cat trebuie sa taca un senzor marcat ca dezinrolarea sa fie confirmata. Patru cicluri de somn in cazul cel mai lent (senzorul #5, cu jitter maxim si LFINTOSC la limita de toleranta) |
| `PAIRING_REOPEN_AFTER_REMOVE` | 1 | Redeschide fereastra de pairing dupa o dezinrolare confirmata. Doar in urma unei comenzi date de om, deci nimeni nu se inroleaza pe furis |
| `PAIRING_UNKNOWN_HINT_EVERY` | 10 | La cate pachete de la un `DevAddr` neinrolat se repeta sfatul de recuperare |
| `HUB_MAX_SENSORS` | 5 | Cati senzori are reteaua. Este si limita registrului, si domeniul numerelor (1..5), si domeniul adreselor alocate |
| `SENSOR_OFFLINE_MS` | 150000 | Dupa cat timp fara niciun pachet este anuntat un senzor ca „nu se mai aude" |
| `SENSOR_FCNT_GAP_RESTART` | 20 | De la ce salt in frame counter hub-ul spune „senzorul a repornit" in loc sa numere pierderi. La pornire la rece senzorul sare inainte cu 50 (F-022), si acelea nu sunt pachete pierdute |
| `REGISTRY_MAX_DEVICES` | `HUB_MAX_SENSORS` | Cati senzori incap in registru |
| `REGISTRY_SAVE_EVERY` | 20 | La cate pachete se rescrie registrul in NVS |

Formatul tuturor pachetelor este descris in
[SensorPacket.h](SensorPacket.h) si oglindit in sectiunea 4 din
`senzor/main.c`. Cele doua se modifica **impreuna**.
