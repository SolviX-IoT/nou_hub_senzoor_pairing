# ISTORIC — SolviX HUB + SENZOR

> Arhiva proiectului: **de ce** arata codul asa cum arata. Nu se incarca
> automat in context — se deschide cand ai nevoie de motivul din spatele unei
> decizii, cand un simptom seamana cu ceva ce s-a mai intamplat, sau cand un
> mesaj de commit citeaza o eticheta `F-0xx`.
>
> Referinta activa (pini, protocol, reguli) este in [CLAUDE.md](CLAUDE.md).
> Starea de acum (cifre, versiuni de format) este in [MEMORY.md](MEMORY.md).
>
> **Nicio intrare nu se sterge.** Ce nu mai este valabil se marcheaza
> *ISTORIC*, ca etichetele din mesajele de commit sa ramana rezolvabile.

---

## 1. Constrangerile care au modelat implementarea

Acestea vin din hardware-ul real si explica de ce solutia nu arata ca o
retea LoRa "de manual".

### 1.1. Flash si RAM foarte mici pe senzor — cifre masurate, nu estimate

`PIC16LF1508` are **4096 de cuvinte de flash si 256 de octeti de RAM**
(`ROMSIZE=1000`, `RAMBANK=20-7F,A0-EF,120-16F` in fisierul de device support
al lui XC8: 240 de octeti bancati + 16 comuni). O versiune anterioara a
documentatiei scria 512 B de RAM — era gresit, si din acea eroare a pornit
toata problema de incadrare (F-025).

**Aici s-a consumat toata marja, si de aceea nu mai exista criptografie in
proiect.** Istoricul incadrarii:

| varianta | flash (words) | RAM (octeti) |
|----------|---------------|--------------|
| AES-128, cum a fost scrisa initial, `-O2` | 5426 | 446 |
| AES cu toate solutiile de rezerva aplicate simultan | 4325 | 286 |
| XTEA-128, dupa F-024 | 3761 | 250 |
| XTEA + pairing manual + somn + 5 senzori | 3876 | 232 |
| **fara criptografie (acum)** | **2395** | **95** |

AES nu incapea nici macar cu toate solutiile de rezerva: doar cele doua tabele
de substitutie ocupa 512 cuvinte, un sfert din tot flash-ul, inainte de orice
linie de cod (F-024). XTEA-128 a incaput, dar la 3876 din 3968 de cuvinte
utilizabile — **92 de cuvinte marja** — nu mai incapea nicio functionalitate
noua. La 2026-08-29 criptografia a fost scoasa (F-038), ceea ce a eliberat
**1481 de cuvinte si 137 de octeti**.

**Pretul: reteaua NU mai este autentificata.** Nu exista MIC, cheie sau nonce.
Oricine are un radio LoRa cu aceiasi parametri poate injecta o temperatura
falsa pentru orice senzor, poate dezinrola orice placa cu patru octeti
(`A5 13 <DevAddr> 02`), poate inrola o placa falsa cat timp fereastra de
pairing este deschisa (DevEUI-ul este ghicibil: `"SOLVIX" | 0x00 | numar`), si
poate rejuca orice pachet capturat. Singura limitare ramasa pe calea de date
este frame counter-ul strict crescator. In plus, `RegSyncWord` ramane `0x12`,
valoarea implicita a bibliotecii: inainte, MIC-ul filtra si emitatorii straini
care nimereau aceiasi parametri.

**Nu compensa cu nimic facut in casa.** Un pseudo-MIC de o suta de cuvinte
care nu opreste pe nimeni este mai rau decat o absenta onesta, fiindca cineva
se va baza pe el. Criptografia se reintroduce la upgrade-ul de microcontroller,
din commit-ul `a710142`, care este ultima stare care o contine.

**`-O2` ramane obligatoriu si regiunea HEF ramane rezervata.** Cu marja de
acum firmware-ul ar incapea probabil si cu `-O0`, dar cele doua setari nu se
schimba "fiindca oricum e loc": rezervarea HEF este singurul lucru care
garanteaza ca linkerul nu pune cod peste memoria ne-volatila (F-027).

Daca vreodata nu mai incape nici asa, ramane migrarea pe **PIC16LF1509**
(8K words, 512 B RAM), pin-compatibil — maparea de pini ramane valabila bit cu
bit, dar `HEF_BASE` devine `0x1F80`.

### 1.2. `SLEEP` nu este acelasi lucru cu taierea alimentarii

Distinctia asta decide schema frame counter-ului.

**Ce se intampla acum:** inrolat, senzorul executa `SLEEP` intre transmisii si
se trezeste pe watchdog. Durata **nu este aceeasi pe toate placile**:
`SLEEP_WAKEUPS_BASE` + `(DevAddr - 1)` + un jitter aleator de 0..3 treziri,
adica ~23 s pentru senzorul #1 fara jitter si pana la ~38 s pentru senzorul #5
cu jitter maxim (F-036). `SLEEP` pe PIC16 **pastreaza RAM-ul si registrele** —
procesorul doar isi opreste ceasul. Deci schema din F-022 ramane valabila
neschimbata: counter-ul traieste in RAM si se salveaza in HEF doar la fiecare
`FCNT_CHECKPOINT_EVERY` = 50 de pachete. **Nu** se scrie la fiecare ciclu de
somn; ar consuma HEF-ul degeaba.

**Ce ar fi altceva:** un regim in care **alimentarea se taie** intre transmisii
(un timer extern de tip PTC/latch care scoate VDD). Acolo fiecare trezire ar fi
un cold boot cu RAM-ul pierdut: counter-ul ar trebui scris la fiecare ciclu,
inelul de sloturi din HEF ar trebui marit (sau inlocuit cu un FRAM extern), iar
inrolarea ar trebui incadrata intr-o fereastra de alimentare. **Nu este cazul
astazi.**

Somnul se aplica doar in `DEV_STATE_OPERATING`. In `DEV_STATE_IDLE` si in
`DEV_STATE_JOINING` senzorul ramane treaz, fiindca acolo trebuie sa asculte
butonul 2 si sa poata face join.

### 1.3. Senzorul era doar TX, cu polling, fara DIO0 si fara RESET cablat

Pentru pairing a trebuit adaugat mod RX in driver: `LoRa_Receive()`, cu RX
continuu si polling pe `RxDone` / `PayloadCrcError`, citire din `RegFifo` de la
`FifoRxCurrentAddr`.

### 1.4. Pe hub, SPI este partajat cu ENC28J60

Tot traficul radio trece prin `SpiBus` (`claimLoRa()` / `SpiGuard`), receptia
este prin polling cu `receiveRaw()`, emisia binara prin `sendRaw()`, LED-urile
doar prin modulul `Leds`, si nu se apeleaza niciodata `LoRa.end()` /
`SPI.end()`.

---

## 2. Cronologie

| Data | Etichete | Ce s-a intamplat |
|------|----------|------------------|
| — | F-001…F-020 | Mostenite din `teste-sistemcomplet/`: bug-urile de SPI partajat, de retea si de firmware de baza |
| — | F-021…F-023 | Faza de proiectare a pairing-ului: capcane prinse inainte de a ajunge pe placa |
| 2026-08-23 | F-024…F-029 | Pairing criptat, incadrat in PIC16LF1508: AES inlocuit cu XTEA-128, harta HEF corectata, regiunea HEF rezervata din linker |
| 2026-08-25 | F-030 | Pairing manual pe senzor, declansat din butonul 2 |
| 2026-08-25 | F-031 | Dezinrolare confirmata pe hub: RESET retrimis pana la tacere |
| 2026-08-25 | F-032 | Fereastra de downlink se deschide imediat dupa TX |
| 2026-08-25 | F-033 | Capcana de build: MPLAB X programa cod vechi |
| 2026-08-26 | *(fara F)* | TPL5110 scos din proiectare — vezi 2.1 |
| 2026-08-26 | F-034 | Somn intre transmisii pe senzor |
| 2026-08-26 | F-035…F-037 | Pana la 5 senzori pe acelasi hub |
| 2026-08-29 | F-038 | Criptografia scoasa, pairing-ul pastrat |

### 2.1. 2026-08-26 — TPL5110 scos din proiectare *(fara eticheta F)*

Componenta nu mai este pe placa, deci a disparut si din cod: cele trei scrieri
de registre din `Board_Initialize()`, blocul de `#define` `TPL5110_DONE_*`,
notele din antetul lui `main.c`, testul `main_powercycle_test.c.bak` si
datasheet-ul din `senzor/Datasheets/` (folderul a ramas gol si a fost sters).

**RC1 este acum un pin liber si nu primeste cod:** ramane pe configuratia MCC
din `pins.c`, adica intrare analogica, ceea ce pentru un pin neconectat este
exact starea buna — bufferul digital de intrare este dezactivat, deci un nivel
flotant nu consuma curent.

F-011, F-012 si F-018 au fost marcate ISTORIC in loc sa fie sterse, ca
etichetele din mesajele de commit sa ramana rezolvabile; F-013 si F-014 raman
documentatie vie, fiindca lectiile lor nu tin de TPL5110.

Castigul de memorie este cel asteptat de la trei scrieri de registre:
**-5 cuvinte**, 3921 -> 3916.

**Ramas de facut manual:** `PINOUT_config.pdf` inca arata RC1 -> TPL5110.

---

## 3. Catalogul F-001 … F-038

Fiecare intrare spune **simptomul**, **cauza** si **fixul**, ca sa nu se repete
aceleasi greseli.

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
- **Problema:** formula Steinhart / beta cere `log()` si `exp()`; libraria math in virgula mobila nu incape in cei 4K words alaturi de driverul LoRa.
- **Fix:** conversie prin **tabel de cautare cu interpolare liniara** in aritmetica pe intregi: 25 de intrari de la −20 °C la +100 °C, pas de 5 °C.

### F-017 — Fara timer hardware (senzor)
- **Constrangere impusa:** Timer0/1/2 nu sunt configurate.
- **Fix:** intervalul de transmisie se obtine cu o bucla software de pasi de 10 ms, care permite si verificarea butonului intre pasi.

### F-018 — RC1 (DONE) trebuie tinut LOW cand timerul nu e folosit (senzor)
> **ISTORIC.** TPL5110 a fost scos din proiectare la 2026-08-26. RC1 este
> acum un pin liber, fara cod in `main.c`: ramane pe intrare analogica din
> configuratia MCC, ceea ce pentru un pin neconectat este starea corecta —
> bufferul digital este dezactivat, deci un nivel flotant nu consuma curent.
- **Problema:** codul nu face power-cycle, dar RC1 este in continuare legat la pinul DONE al TPL5110. Lasat flotant sau HIGH, ar putea taia alimentarea in mijlocul unei transmisii — sau in mijlocul unei scrieri in HEF, ceea ce ar lasa un rand pe jumatate scris.
- **Fix:** `main()` configureaza RC1 ca iesire digitala si il tine **LOW permanent**.

### F-019 — `String` nu poate transporta un pachet binar (hub)
- **Simptom:** un pachet care contine octetul `0x00` ar fi fost trunchiat la receptie.
- **Cauza:** `LoRaRadio::receive()` acumuleaza octetii intr-un `String`, care trateaza `0x00` drept terminator de sir.
- **Fix:** `LoRaRadio::receiveRaw()`, care scrie intr-un buffer de `uint8_t`. **Extins in acest proiect pe emisie:** `JOIN_ACCEPT` si `CMD_DOWN` contin `0x00` in padding si in contoare, deci a fost adaugat si `LoRaRadio::sendRaw()`. `sendText()` ramane pentru testele de legatura cu text.

### F-020 — Cod nou livrat in foldere paralele in loc sa extinda proiectul
- **Simptom:** functionalitatea de temperatura a fost livrata initial ca `senzor_temp/` si `hub_lora_rx/`, doua foldere noi langa `senzor/` si `hub/`.
- **Cauza:** structura existenta nu a fost respectata. Rezultatul erau doua seturi de definitii de pini si doua initializari de driver, care ar fi divergat la prima modificare.
- **Fix:** codul a fost mutat in proiectele existente. **Regula pentru viitor, respectata si de pairing:** functionalitatea noua intra in `senzor/main.c` si in module noi din `hub/SolvixHub_Tests/`; nu se creeaza foldere paralele.

### F-021 — Un fisier `Crypto.h` propriu ar fi ascuns biblioteca `Crypto` (hub)
> **ISTORIC.** Wrapper-ul `HubCrypto.*` a disparut odata cu criptografia
> (F-038). Lectia despre calea de include ramane valabila pentru orice
> fisier nou din folderul sketch-ului.
- **Simptom (anticipat la proiectare):** eroare de compilare in interiorul bibliotecii `Crypto` (Rhys Weatherley), care nu isi mai gaseste propriile declaratii.
- **Cauza:** Arduino IDE pune folderul sketch-ului **inaintea** folderelor de biblioteci in calea de include. Un `Crypto.h` al nostru ar fi fost gasit primul si de biblioteca, atunci cand ea include `<Crypto.h>`.
- **Fix:** wrapper-ul s-a numit `HubCrypto.h` / `HubCrypto.cpp`, cu motivul scris in antetul fisierului.

### F-022 — Scrierea frame counter-ului la fiecare pachet ar consuma HEF-ul (senzor)
- **Simptom (anticipat):** dupa cateva luni de functionare, scrierile in HEF ar fi inceput sa esueze in tacere, iar senzorul ar fi reluat pairing-ul la fiecare pornire.
- **Cauza:** HEF suporta ~100.000 de cicluri de stergere/scriere pe rand. Un pachet la 5 secunde inseamna 17.280 de scrieri pe zi: sub 6 zile pe un singur rand.
- **Fix:** counter-ul sta in RAM (senzorul e alimentat permanent) si se salveaza doar la fiecare `FCNT_CHECKPOINT_EVERY` = 50 de pachete, prin rotatie in sloturile inelului. La cold boot se sare inainte cu 50, ca sa nu se reutilizeze nicio valoare. Rezulta ~345 de scrieri pe zi impartite la 2 randuri (vezi F-026 pentru de ce sunt 2, nu 4), adica peste 500 de zile pe rand.

### F-023 — Payload-ul decriptat nu trecea de checksum, fara niciun indiciu (hub)
> **ISTORIC.** `PAIRING_ENCRYPT_PAYLOAD` nu mai exista (F-038), deci cauza
> descrisa aici nu mai poate aparea.
- **Simptom (anticipat la proiectare):** MIC valid, deci pachetul chiar vine de la senzorul inrolat, dar `decode()` il respinge. Simptomul arata identic cu un senzor defect.
- **Cauza posibila:** `PAIRING_ENCRYPT_PAYLOAD` are valori diferite pe hub si pe senzor, deci hub-ul "decripteaza" un text care era deja in clar (sau invers).
- **Fix:** `TestPairing` trata acest caz separat de un MIC gresit, spunea explicit ce sa verifice si afisa octetii obtinuti.

### F-024 — AES-128 nu incape in PIC16LF1508
*(2026-08-23)*
- **Simptom:** proiectul nu link-edita deloc pentru `16LF1508`. Prima eroare era in RAM (`no space for auto/param main@tempPacket`), iar dupa ce se elibera RAM aparea un val de `can't find N words for psect ... in class CODE`.
- **Cauza:** firmware-ul cu AES-128 + AES-CMAC + AES-CTR cere **5426 de cuvinte de program si 446 de octeti de RAM** (masurat cu `xc8-cc` v3.10, `-O2`, compiland pentru `16LF1509` care are acelasi nucleu dar memorie mai mare — pe `16LF1508` link-editarea esueaza si nu da nicio cifra). Device-ul are 4096 de cuvinte si 256 de octeti. Nici macar cu toate cele trei solutii de rezerva prevazute in proiectare aplicate simultan nu se cobora sub **4325 de cuvinte / 286 de octeti**. Numai cele doua tabele de substitutie ale AES ocupa 512 cuvinte.
- **Fix:** cifrul a fost inlocuit cu **XTEA-128** (bloc de 64 de biti, cheie de 128, 32 de runde, `DELTA = 0x9E3779B9`). XTEA nu are niciun tabel si are nevoie doar de cifrare, fiindca MIC-ul (CBC-MAC) si criptarea (CTR) se construiesc amandoua peste ea. Rezultat: **3761 de cuvinte si 250 de octeti**, cu pairing-ul, cheia de sesiune, anti-replay-ul si criptarea payload-ului toate pastrate. `JOIN_ACCEPT` s-a scurtat de la 22 la 10 octeti.
- **De retinut:** CBC-MAC-ul simplu (fara subcheile CMAC) era sigur acolo **doar** pentru ca fiecare tip de mesaj avea lungime fixa si octetul `TYPE` se afla in primul bloc acoperit. Daca se reintroduce cifrul si se adauga un mesaj de lungime variabila, constructia trebuie schimbata.

### F-025 — RAM-ul lui PIC16LF1508 documentat gresit ca 512 B
*(2026-08-23)*
- **Simptom:** toate estimarile de incadrare erau optimiste cu un factor de doi, iar buffer-ele fusesera dimensionate dupa ele.
- **Cauza:** documentatia si comentariile din `main.c` scriau 512 B. In realitate `RAMBANK=20-7F,A0-EF,120-16F` inseamna **240 de octeti bancati + 16 comuni = 256**. (512 B are `PIC16LF1509`.)
- **Fix:** cifra a fost corectata peste tot, iar consumul a fost taiat acolo unde chiar conta: cheia extinsa a disparut (XTEA nu are program de chei), `hefRowBuffer` tine doar octetii folositi in loc de tot randul de 32 (restul latch-urilor primesc `0xFF` direct in `HEF_WriteRow`), iar buffer-ele de pachete au fost dimensionate dupa noile lungimi.

### F-026 — Randul de HEF are 32 de cuvinte, nu 16
*(2026-08-23)*
- **Simptom (prins inainte de a ajunge pe placa):** harta HEF folosea 8 regiuni de cate 16 cuvinte, iar scrierea "randului" de la `0x0F90` ar fi sters de fapt tot blocul `0x0F80`–`0x0F9F` — adica DevEUI-ul odata cu el. Senzorul si-ar fi pierdut identitatea la prima pornire, tacut.
- **Cauza:** dimensiunea randului fusese scrisa ca **presupunere** in proiectare si nu fusese verificata.
- **Fix:** valoarea reala este in fisierul de device support al lui XC8, `PIC12-16F1xxx_DFP/.../dat/ini/16lf1508.ini`: `FLASH_ERASE=20` si `FLASH_WRITE=20`, hexazecimal, adica **32 de cuvinte**. `HEF_ROW_WORDS` este acum 32, iar harta are 4 randuri. Ca efect secundar bun, o inrolare inseamna o singura stergere+scriere in loc de doua.

### F-027 — Linkerul plasa cod chiar in regiunea HEF
*(2026-08-23)*
- **Simptom (demonstrat pe fisierul de simboluri, inainte de a ajunge pe placa):** `Packet_BuildDataEnc` se termina la `0x0F9A` si `SPI1_Open` incepea acolo — adica exact peste `0x0F80`–`0x0FFF`. Prima scriere in HEF si-ar fi sters propriul cod si placa ar fi devenit un caramizi la prima inrolare.
- **Cauza:** proprietatea `code-model-rom` a proiectului MPLAB X era goala, deci linkerul avea voie sa foloseasca toata memoria de program, inclusiv ultimele 128 de cuvinte pe care firmware-ul le foloseste ca memorie ne-volatila.
- **Fix:** `code-model-rom = default,-f80-fff` in `senzor/nbproject/configurations.xml` (echivalentul lui `--ROM=default,-f80-fff`). Verificat dupa fix: ultima instructiune este la `0x0F7F`, deci regiunea HEF este curata. **Aceeasi rezervare trebuie refacuta daca cineva recreeaza proiectul MPLAB X de la zero.**

### F-028 — Aritmetica pe 32 de biti manca flash-ul pe PIC16
*(2026-08-23)*
- **Simptom:** functii banale ieseau enorme: `Xtea_LoadKey` 126 de cuvinte, `Nvm_LoadFrameCounter` 194, `NTC_AdcToTempX100` 207 plus inca 172 pentru rutinele `___aldiv` si `___lmul` din biblioteca.
- **Cauza:** PIC16 are un acumulator de 8 biti. Fiecare deplasare a unui `uint32` cu un numar de pozitii devine o bucla din biblioteca XC8, iar o impartire pe 32 de biti este o rutina intreaga. Impachetarea si despachetarea big-endian scrise "cu shift-uri" costau singure peste 300 de cuvinte.
- **Fix, in doua locuri:**
  1. **Cifrul** folosea uniunea `Word32 { uint32_t word; uint8_t byte[4]; }`. Conversiile big-endian devin mutari de octeti, fara nicio deplasare pe 32 de biti.
  2. **Interpolarea NTC** se face integral pe 16 biti, in doi pasi (`frac = (high-adc)*64/span`, apoi `frac*500/64`), ca intermediarul sa ramana sub 65.535. Dispar si `___aldiv`, si `___lmul`. Cuantizarea introdusa este de 500/64 ≈ 0,08 °C — cu un ordin de marime sub toleranta unui NTC de 1%.
- **Rezultat:** 4035 -> 3761 de cuvinte, adica exact marja care a facut posibila rezervarea regiunii HEF din F-027.
- **De retinut:** pe acest device, orice `int32_t` nou introdus in codul fierbinte trebuie privit ca o cheltuiala de zeci-sute de cuvinte, nu ca o alegere de tip. `Word32` a supravietuit scoaterii cifrului — o folosesc cele trei functii de frame counter (vezi capcana 1 din F-038).

### F-029 — Configuratia de DEBUG nu incapea: depanatorul isi ia 16 octeti de RAM
*(2026-08-23)*
- **Simptom:** `Build` mergea, dar `Debug Project` esua cu `could not find space (4 bytes) for variable _fcntSinceCheckpoint`.
- **Cauza, doua lucruri suprapuse:**
  1. MPLAB X adauga la build-ul de depanare `-mram=default,-160-16f`, adica **rezerva 16 octeti** (`0x160`-`0x16F`, valoarea `ICD3RAM` din fisierul de device support) pentru Snap/PICkit. Din cei 240 de octeti bancati raman 224, iar firmware-ul folosea 250.
  2. Makefile-ul generat continea `-O0`, fiindca MPLAB X il **rescrie** din modelul lui intern la fiecare build. Editarea lui `configurations.xml` cu proiectul DESCHIS in IDE nu are efect — setarile trebuie facute din interfata, sau IDE-ul trebuie inchis inainte.
- **Fix:** 19 octeti de RAM eliberati: `AppKey` nu s-a mai tinut in RAM, ci s-a citit direct din HEF in cifru (`-16 octeti`, `+82` cuvinte de program); `fcntSinceCheckpoint` a devenit `uint8_t` (`-3 octeti`).
- **De retinut:** daca `Debug Project` incepe iar sa dea `could not find space`, verifica INTAI ca optimizarea este `-O2` in fereastra de proprietati a proiectului, nu in fisiere — IDE-ul le suprascrie.

### F-030 — Fiecare `__delay_ms()` cu o constanta noua costa ~25 de cuvinte
*(2026-08-25)*
- **Simptom:** adaugarea pairing-ului manual (o functie care numara apasarea butonului 2 si o bucla de asteptare cu LED2 clipind) a umflat firmware-ul cu **173 de cuvinte**, de la 3843 la 4016, iar link-editarea a picat cu `can't find N words for psect ... in class CODE` — adica exact esecul din F-024, dar din alta cauza.
- **Cauza:** `__delay_ms()` nu este o functie, ci o macro care emite o bucla de intarziere **inline la fiecare loc de apel**. Cele doua bucle noi aveau intre ele patru intarzieri distincte (100 ms, 20 ms, 10 ms si inca un 100 ms), fiecare cu propriul cod generat. Din cele 173 de cuvinte, peste 100 erau bucle de intarziere duplicate, nu logica.
- **Fix, trei masuri:**
  1. o singura functie `Pairing_BlinkStep()` detine **unicul** `__delay_ms(PAIR_HOLD_TICK_MS)` din firmware, iar ambele bucle de pairing o apeleaza;
  2. numaratoarea apasarii si asteptarea eliberarii butonului au fost unificate intr-o singura bucla `while (apasat)`, cu un contor care se opreste la prag — deci un singur loc de intarziere in loc de trei;
  3. debounce-ul separat la 20 ms pentru RC5 a fost scos: bucla reciteste oricum pinul de 30 de ori la 100 ms distanta si abandoneaza la prima citire LOW, ceea ce este un filtru de bounce mai strict decat cel din `Button_Pressed()` (F-015), nu mai slab.
- **Rezultat:** 4016 -> **3936** de cuvinte, adica sub cele 3968 utilizabile.
- **Comportamentul rezultat:** fara sesiune, senzorul sta in `DEV_STATE_IDLE` si tace; fereastra de inrolare se deschide tinand butonul 2 (RC5) apasat ~3 secunde si se inchide dupa `PAIRING_MAX_ATTEMPTS` = 10 incercari, cu LED2 clipind cat este deschisa. `CMD_DOWN(RESET)` duce senzorul in repaus, nu inapoi in pairing.
- **De retinut:** pe acest device, o intarziere inline cu o valoare noua se pune la socoteala ca o functie de ~25 de cuvinte. Cand ai nevoie de temporizare in doua locuri, imparte acelasi pas, nu copia randul.

### F-031 — `remove` era "trimite si uita": un RESET pierdut lasa senzorul blocat in retea pe veci
*(2026-08-25)*
- **Simptom:** dupa `remove <DevEUI>`, senzorul continua sa emita si nu mai poate fi oprit din hub. Pe Serial curge la nesfarsit `[DATA] IGNORAT: DevAddr 0x.. nu este inrolat.`, iar `remove <DevEUI>` raspunde `Nu exista niciun device inrolat`, fiindca device-ul nu mai este in registru. Singura iesire ramane fizica: trei secunde pe butonul 2 al senzorului. Cel putin o placa a ajuns in starea asta.
- **Cauza:** `handleEncryptedData()` trimitea `CMD_DOWN(RESET)` **o singura data** si stergea imediat inregistrarea cu `removeByEui()`, fara nicio confirmare. Downlink-ul are o singura sansa: senzorul asculta doar `DOWNLINK_WINDOW_MS` = 600 ms dupa fiecare transmisie. Daca acel pachet se pierde — coliziune, fereastra ratata, orice — senzorul isi pastreaza starea si continua sa emita, iar hub-ul tocmai a aruncat singura copie a cheii cu care ar fi putut compune un alt `CMD_DOWN` valid pentru el. Dezinrolarea era presupusa, nu verificata.
- **Fix — stergerea se face pe dovada, nu pe speranta:**
  1. inregistrarea **nu** se mai sterge la trimiterea RESET-ului;
  2. la **fiecare** pachet primit de la un device marcat se retrimite `RESET`, cu `downCounter` si `resetAttempts` incrementate — un senzor care inca emite este dovada ca nu a primit comanda; pachetul nu se mai decodeaza si nu se mai numara ca date valide;
  3. inregistrarea dispare abia cand senzorul **tace** `REMOVE_CONFIRM_SILENCE_MS` de la ultimul RESET — tacerea este exact semnalul ca a ajuns in `DEV_STATE_IDLE` (F-030). Verificarea ruleaza in `tick()`, prin `servicePendingRemovals()`, fara `delay()` si fara timer nou;
  4. `remove` pe un device care nu a trimis niciodata nimic (`hasUplink` fals) este refuzat, cu explicatie: RESET-ul nu are cum sa plece daca senzorul nu vorbeste, iar marcarea ar bloca inregistrarea la nesfarsit. Operatorul alege intre a porni senzorul si `force`;
  5. cand sosesc pachete de la un `DevAddr` neinrolat, hub-ul spune — rar, o data la `PAIRING_UNKNOWN_HINT_EVERY` pachete — cum se recupereaza senzorul, pentru placile ramase blocate de varianta veche.
- **Capcana prinsa in timpul fixului:** `resetSentMs` trebuie pus pe **0** la incarcarea din NVS, exact ca `lastSeenMs`. Amandoua sunt relative la `millis()`, deci la pornirea hub-ului o valoare veche minus un `millis()` mic ar da o diferenta uriasa si dezinrolarea ar aparea drept confirmata instantaneu, fara ca vreun RESET sa fi plecat. Zero inseamna acum "niciun RESET in sesiunea asta", iar confirmarea prin tacere refuza sa se pronunte in acel caz si asteapta un pachet.
- **Consecinta:** `DeviceRecord` s-a modificat, deci `REGISTRY_BLOB_VERSION` a trecut pe **2** — si, ca urmare, senzorii deja inrolati au trebuit reinrolati o data.
- **De retinut:** un downlink fara confirmare nu este o comanda, este o speranta. Cand hub-ul arunca starea de care depinde reincercarea, pierderea unui singur pachet devine definitiva.

### F-032 — Pulsul LED-ului de date facea senzorul surd la downlink
*(2026-08-25)*
- **Simptom:** hub-ul retrimitea `CMD_DOWN(RESET)` la fiecare pachet, cu `incercarea 1`, `2`, `3`, `4`, `5`..., iar senzorul continua netulburat sa emita date. Nici `ACK`-urile nu ajungeau vreodata: LED2 nu pulsa niciodata dupa o transmisie, desi `PAIRING_SEND_ACK` era 1. Inrolarea, in schimb, mergea din prima de fiecare data.
- **Cauza:** in bucla de date, intre `LoRa_SendBuffer()` si `LoRa_Receive()` statea `Led_PulseData()`, care este un puls **blocant** de `LED_PULSE_MS` = **150 ms**. Fereastra de receptie a senzorului se deschidea deci abia la 150 ms dupa terminarea propriei transmisii, iar hub-ul raspunde mult mai devreme: cateva milisecunde de procesare si de scris pe Serial, plus ~41 ms de timp pe aer la SF7/BW125/CR4-5. Downlink-ul era complet terminat pe la ~55 ms, cand senzorul inca tinea LED-ul aprins cu radioul in standby. **Niciun downlink din calea de date nu a functionat vreodata.** Asimetria cu inrolarea este exact dovada: `Join_Attempt()` trece direct de la `LoRa_SendBuffer()` la `LoRa_Receive()`, fara nicio intarziere, si de aceea `JOIN_ACCEPT` se prindea mereu.
- **Fix:** LED1 se aprinde inainte de fereastra si se stinge dupa ea, cu doua scrieri simple in `LATC`, deci receptia porneste imediat dupa TX. `Led_PulseData()` a ramas fara apelanti si a fost scoasa. Vizual, LED1 sta aprins cat dureaza fereastra de downlink in loc de 150 ms fixe — tot o clipire per pachet. Ca efect secundar, firmware-ul s-a micsorat cu 15 cuvinte: 3936 -> **3921**.
- **De retinut:** intre o transmisie si fereastra ei de receptie nu are voie sa stea NIMIC blocant — nici LED-uri, nici scrieri in HEF, nici masuratori. Fereastra este singura ocazie in care celalalt capat poate vorbi, si se inchide singura. Orice `__delay_ms()` pus acolo "doar pentru feedback vizual" costa exact functionalitatea.

### F-033 — Build de verificare in `build/`+`dist/` lasa MPLAB X cu o stare veche
*(2026-08-25)*
- **Simptom:** doua "bug-uri" care nu existau. Intai butonul 2 parea mort: se tinea RC5 apasat trei secunde si nu se intampla nimic, desi F-030 era in `main.c`. Apoi, dupa o reprogramare, senzorul parea ca se inroleaza **singur**, fara nicio apasare — exact comportamentul de dinainte de F-030. S-a cautat in maparea pinilor, in pull-up-uri si in cablaj; nu era nimic acolo.
- **Cauza:** placa fusese programata cu **cod vechi**. Ca sa se verifice incadrarea in flash, firmware-ul fusese compilat cu `xc8-cc` chemat direct, iar iesirea scrisa fix in `senzor/build/` si `senzor/dist/` — directoarele de lucru ale lui MPLAB X. Numele obiectelor nu erau cele pe care le asteapta `Makefile-default.mk`, iar in `dist/` ramanea un `senzor.production.elf` mai nou decat sursele. Build-ul IDE-ului a ramas deci intr-o stare incoerenta si a produs un `.hex` care nu corespundea cu `main.c` de pe disc. Sursa era corecta tot timpul.
- **Fix:** *Clean* pe proiect in MPLAB X, apoi *Make and Program Device*. Pentru viitor: un build de verificare facut din afara IDE-ului **nu scrie in `senzor/build/` sau `senzor/dist/`** — se da o alta destinatie (un director temporar). Daca s-a scris totusi acolo, se face obligatoriu *Clean* inainte de urmatoarea programare.
- **Cum se verifica in trei secunde ce s-a programat:** raportul de memorie din fereastra de build, sau `senzor/dist/default/production/senzor.production.mum`, trebuie sa arate cifra curenta din [MEMORY.md](MEMORY.md). Alta cifra inseamna alt cod decat cel din `main.c`, si nicio cautare in schema nu are rost pana nu se potriveste.
- **Cu cinci placi identice, verifica si CE numar are placa programata:** da `sensors` pe hub si uita-te ca senzorul sa apara pe randul asteptat. Doua placi programate din greseala cu acelasi `SENSOR_NODE_ID` au acelasi `DevEUI`, deci a doua o inlocuieste pe prima in registru la inrolare. `provisioned` arata ce numar ar trebui sa aiba fiecare `DevEUI`.
- **De retinut:** cand o placa se poarta ca o versiune anterioara a firmware-ului, prima intrebare nu este "ce am gresit in cod", ci "ce cod este de fapt pe cip".

### F-034 — Somnul senzorului ar fi rupt confirmarea dezinrolarii de pe hub
*(2026-08-26)*
- **Simptom (prins la proiectare, inainte de a ajunge pe placa):** dupa introducerea somnului de ~30 s, un `remove <DevEUI>` ar fi raportat `DEZINROLARE CONFIRMATA` in mijlocul unui somn normal, fara ca senzorul sa fi primit vreun `RESET`. La trezire senzorul ar fi continuat sa emita cu cheia veche, iar hub-ul — care tocmai stersese inregistrarea si cheia — nu l-ar mai fi putut opri niciodata. Adica exact fundatura reparata de F-031, de data asta fara nicio iesire din hub.
- **Cauza:** `REMOVE_CONFIRM_SILENCE_MS` era 20 s, calibrata explicit pentru "patru transmisii ratate la rand" la un interval de transmisie de 5 s. Mecanismul din F-031 foloseste **tacerea** ca dovada ca senzorul a primit `RESET`-ul si a intrat in repaus. Un senzor care doarme tace si el, si tace mai mult decat fereastra: 30 s de somn > 20 s de fereastra, deci prima confirmare ar fi cazut inainte de prima trezire.
- **Fix:** `REMOVE_CONFIRM_SILENCE_MS` a urcat la **120 s**, adica patru cicluri de somn nominale. Perechea `SLEEP_WAKEUPS` (senzor) <-> `REMOVE_CONFIRM_SILENCE_MS` (hub) a intrat in lista constantelor care se schimba obligatoriu pe ambele capete.
- **Ce a costat incadrarea:** somnul in sine a costat 27 de cuvinte, dar nu incapea. A fost nevoie intai de o faza de curatenie: `SPI1_Open` a iesit din legatura (MSSP-ul se deschide scriind direct cele cinci registre, in loc sa indexeze tabelul MCC pentru o singura configuratie), iar frame counter-ul a trecut pe uniunea `Word32` in cele trei locuri unde se impacheta big-endian cu deplasari pe 32 de biti — economia pe care F-028 o lasase scrisa ca "inca netratat". Cele doua au eliberat impreuna **186 de cuvinte**, 3916 -> 3730, iar cu somnul firmware-ul a ajuns la **3757**.
- **De ce e fragmentat somnul:** RC5 este pe PORTC, iar `pic16lf1508.h` nu are niciun registru `IOCC*` — interrupt-on-change exista doar pe PORTA si PORTB, deci un senzor adormit nu poate fi trezit de buton. Somnul se imparte in reprize de ~2,11 s, cu butoanele citite la fiecare trezire. `WDTE` a trecut de la `OFF` la `SWDTEN`, ca watchdog-ul sa fie pornit numai in jurul lui `SLEEP`.
- **Continuare, F-036:** `SLEEP_WAKEUPS` nu mai exista ca atare. Somnul este acum `SLEEP_WAKEUPS_BASE` + `(DevAddr - 1)` + jitter, deci fiecare senzor doarme altfel si cel mai lung ciclu a crescut la ~44 s. `REMOVE_CONFIRM_SILENCE_MS` a urcat corespunzator la 180 s, si tot atunci a intrat in regula si `SENSOR_OFFLINE_MS`.
- **De retinut:** o schimbare care pare locala pe un nod poate invalida tacut o presupunere de temporizare de pe celalalt. Aici presupunerea nu era scrisa intr-un `#define` comun, ci intr-un **comentariu** care spunea "patru transmisii la 5 secunde" — si comentariile nu dau erori de compilare cand realitatea se schimba sub ele. Orice mecanism care foloseste **absenta** unui semnal drept dovada trebuie recitit ori de cate ori se schimba ritmul in care acel semnal apare.

### F-035 — Fereastra de downlink putea fi consumata de pachetul altui senzor
*(2026-08-26)*
- **Simptom (prins la proiectare, inainte de a pune a doua placa in retea):** cu doi sau mai multi senzori inrolati, `remove <DevEUI>` ar fi raportat `incercarea 1`, `2`, `3`... la nesfarsit pe un senzor perfect sanatos, iar ACK-urile ar fi ajuns doar din cand in cand. Adica simptomul lui F-032, dupa ce F-032 fusese reparat — si de aceea merita scris separat: are alta cauza.
- **Cauza:** `LoRa_Receive()` se intorcea la **primul** pachet cu CRC bun din fereastra, oricare ar fi fost el. Cat timp exista un singur senzor, orice pachet auzit in fereastra proprie era, prin constructie, raspunsul hub-ului. Cu mai multi senzori, in cele 600 ms ale lui A poate intra la fel de bine ACK-ul trimis lui B: functia se intorcea cu acel pachet, parserul il respingea corect (adresa nu se potriveste), dar **fereastra se inchisese deja**. Un downlink are o singura sansa per ciclu, deci pe calea de dezinrolare asta reproduce exact fundatura din F-031.
- **Fix:** `LoRa_Receive()` a primit parametrul `wantType`. Un pachet care nu are magic-ul nostru, nu are tipul cerut, sau — pentru `CMD_DOWN` — nu are `devAddr` nostru, este aruncat **si receptia continua cu timpul ramas**, exact ca la un CRC gresit.
- **Al doilea filtru, gratuit:** `RegMaxPayloadLength` = `LORA_RX_BUFFER_LEN`, deci modemul arunca singur, in hardware, pachetele mai lungi decat cel mai lung pe care senzorul il PRIMESTE. Vezi capcana 3 din F-038 pentru de ce valoarea trebuie recalculata la fiecare schimbare de lungimi.
- **Ce a ramas netratat atunci, dinadins:** in fereastra de `JOIN_ACCEPT` nu se putea filtra pe adresa, fiindca `DevAddr` circula acolo cifrat. Doi senzori care se inrolau in aceeasi secunda isi puteau fura reciproc fereastra. **Rezolvat de F-038:** fara cifru, `DevAddr` circula in clar in `JOIN_ACCEPT`, iar senzorul stie de la compilare ce numar asteapta.
- **De retinut:** o functie care asteapta "un pachet" a fost scrisa, fara sa o spuna, pentru o retea cu **un singur** partener de discutie. Cand apare al doilea, presupunerea nu da nicio eroare de compilare.

### F-036 — Acelasi interval de somn pe toate placile inseamna coliziune blocata
*(2026-08-26)*
- **Simptom (prins la proiectare):** doi senzori ar fi disparut **in perechi** din jurnalul hub-ului, sistematic si fara nicio eroare vizibila — nici MIC gresit, nici replay, nici pachet strain. Pur si simplu nu ar mai fi venit nimic de la ei, in timp ce ceilalti trei ar fi mers impecabil.
- **Cauza:** `SLEEP_WAKEUPS` era o constanta de compilare, aceeasi pe toate placile. Doua pachete care se suprapun in aer se pierd amandoua (SF7/BW125 nu are captura garantata la puteri apropiate). Coliziunea intamplatoare nu e o problema — 0,15% ocupare per senzor — dar doi senzori cu **exact acelasi interval** care s-au ciocnit o data raman ciocniti la nesfarsit: se deplaseaza cu acelasi pas, deci nu se despart niciodata. Probabilitatea de a intra in starea asta este mica; iesirea din ea, fara nicio interventie, era **zero**.
- **Fix, doua masuri care se completeaza:**
  1. **interval propriu fiecarui senzor**, `SLEEP_WAKEUPS_BASE` (11) plus `(DevAddr - 1)`, deci 23,2 / 25,3 / 27,4 / 29,6 / 31,7 s nominal. Doi senzori ciocniti se despart de la sine dupa o perioada;
  2. **jitter aleator la fiecare ciclu**, 0..3 treziri dintr-un LFSR de 8 biti (`Rand8`), semanat din `DevEUI` si din frame counter-ul citit din HEF. Rupe si cazul in care doua placi ar nimeri acelasi numar de treziri, si pornirea simultana dupa o pana de curent, cand toate placile inrolate emit prima data in acelasi moment.

  Media pe cele 5 adrese ramane ~30,6 s, adica exact ritmul dinainte: s-a schimbat imprastierea, nu debitul. Cost: **+91 de cuvinte** si **+1 octet**, impreuna cu F-035 si cu blocul de provisioning per placa; verificarea de identitate din F-037 a mai luat 28, deci in total 3757 -> **3876**.
- **Consecinta obligatorie pe hub:** `REMOVE_CONFIRM_SILENCE_MS` a urcat de la 120 s la **180 s**. Senzorul #5 cu jitter maxim si LFINTOSC la limita de toleranta doarme ~44 s; fereastra de confirmare trebuie sa acopere patru astfel de cicluri, altfel se repeta F-034.
- **Capcana prinsa in timpul fixului:** contorul nou de **pachete pierdute** (goluri in frame counter) numara si saltul cu `FCNT_CHECKPOINT_EVERY` = 50 pe care senzorul il face la fiecare pornire la rece (F-022). O repornire ar fi aratat ca 50 de coliziuni, adica exact peste cifra dupa care se judeca daca senzorii se ciocnesc intre ei. Peste `SENSOR_FCNT_GAP_RESTART` = 20, hub-ul spune acum "a repornit" in loc sa adune pierderi: douazeci de pachete pierdute la rand ar insemna zece minute de tacere continua, iar la atata `SENSOR_OFFLINE_MS` ar fi raportat deja senzorul ca disparut.
- **De retinut:** cand mai multe noduri identice impart un canal, **egalitatea perfecta a perioadelor este o defectiune**, nu o virtute. Un sistem care nu are cum sa iasa dintr-o stare proasta este mai rau decat unul care intra in ea mai des dar se repara singur.

### F-037 — `DevAddr` alocat "prima adresa libera" nu putea fi scris pe cutie
*(2026-08-26)*
- **Simptom (prins la proiectare):** cu cinci placi identice in teren, operatorul nu ar fi avut cum sa stie care este care. Numarul afisat de hub ar fi depins de ordinea in care au fost pornite, s-ar fi schimbat dupa fiecare dezinrolare si reinrolare, si ar fi fost complet altul dupa o golire a registrului — de exemplu dupa un update care incrementeaza `REGISTRY_BLOB_VERSION`. Singurul identificator stabil ar fi ramas `DevEUI`, adica 16 cifre hexazecimale citite de pe ecran de fiecare data cand vrei sa stii a cui e temperatura.
- **Cauza:** `DeviceRegistry::allocateAddress()` intorcea prima adresa libera din `0x01`..`0xFE`. Cu un singur senzor asta insemna intotdeauna `0x01` si nu se vedea; cu cinci, numerotarea devine un accident al istoriei de pornire.
- **Fix:** `addressForEui()` intoarce **pozitia senzorului in tabelul `PROVISIONED_DEVICES_INIT` din `Config.h`, plus unu**. Tabelul este compilat in program, deci pozitia nu se poate pierde. Consecinte: randul 3 este "Senzor #3" indiferent de istorie, doua placi nu pot primi acelasi numar, iar numarul poate fi scris pe cutie. Aceeasi cifra se scrie si pe placa, ca `SENSOR_NODE_ID`, din care ies acolo `DevEUI` **si** slotul de somn din F-036 — deci numarul chiar face doua treburi, nu este o eticheta.
- **Ce s-a mai simplificat odata cu asta:** cele cinci firmware-uri difera printr-o singura linie. Inainte se editau la fiecare placa doua tabele de octeti, iar o singura cifra gresita in oricare dintre ele dadea acelasi simptom ca o cheie complet gresita: "MIC gresit", fara alt indiciu.
- **Cazul de tranzitie, tratat explicit:** daca in NVS ramane o inregistrare dintr-o versiune in care adresele se alocau in ordinea inrolarii, ea poate ocupa fix numarul cerut de tabel. `JOIN_REQ`-ul este atunci refuzat cu explicatie si cu solutia (`remove <DevEUI> force`), in loc sa se suprascrie in tacere inregistrarea altui senzor.
- **Capcana prinsa pe partea de senzor, si ea tratata in cod:** `DevEUI` se scria in HEF **doar la prima pornire** — daca randul de identitate avea marcajul, se citea si se iesea. O placa deja folosita, reprogramata cu alt `SENSOR_NODE_ID` (cazul obisnuit: senzorul #2 s-a ars si ii ia locul o placa de rezerva), si-ar fi pastrat identitatea VECHE din HEF, in timp ce cheia folosita ar fi fost cea compilata, a noului numar. Rezultatul: "MIC gresit" pe hub, cu o sursa perfect corecta pe disc — F-033 din nou, dar cu starea ne-volatila in loc de directorul de build. `Nvm_LoadOrCreateProvisioning()` compara acum `DevEUI`-ul din HEF cu cel compilat si, cand difera, rescrie randul de identitate **si sterge sesiunea**. Costa 28 de cuvinte si nicio scriere in plus la o pornire obisnuita: se compara doar.
- **Ce s-a schimbat pe hub odata cu trecerea la 5 senzori:** comanda noua **`sensors`** arata toate cele cinci locuri, si cele goale, cu ultima temperatura, varsta ei, RSSI si pachetele pierdute; `remove` accepta si forma scurta `remove #3`; fiecare linie de jurnal incepe cu `Senzor #N (0xNN)`; `serviceOfflineWatch()` anunta o data cand un senzor amuteste si o data cand revine. `DeviceRecord` a primit campuri noi, deci `REGISTRY_BLOB_VERSION` a trecut pe **3**: la primul boot registrul porneste gol si fiecare senzor trebuie reinrolat o data — dar isi primeste inapoi exact acelasi numar.
- **De retinut:** un identificator generat "in ordinea sosirii" este bun numai atat timp cat nimeni nu trebuie sa il tina minte. In clipa in care apare pe o eticheta lipita pe o cutie, el trebuie sa vina dintr-o configuratie, nu dintr-o istorie.

### F-038 — Criptografia nu mai lasa loc pentru nimic altceva; scoasa temporar
*(2026-08-29)*
- **Simptom:** nicio functionalitate noua nu mai incapea pe senzor. Firmware-ul ajunsese la **3876 din 3968 de cuvinte utilizabile (97,7%) si 232 din 256 de octeti de RAM**, adica **92 de cuvinte marja**. Orice adaugare — fie si o bucla de intarziere cu o constanta noua, care costa ~25 de cuvinte (F-030) — pica link-editarea cu `can't find N words for psect ... in class CODE`.
- **Cauza:** nu un bug, ci suma unei serii de decizii corecte pe un device prea mic. XTEA-128 cu CBC-MAC si CTR ocupa ~1300 de cuvinte si ~69 de octeti; peste ele se adunau `Key_UseApp()` (145 de cuvinte, pretul lui F-029), derivarea cheii, generarea nonce-ului, asamblarea intrarilor de MIC in cele patru functii de pachet, si cei 16 octeti de `AppKey` din memoria de program. Toate marjele fusesera deja consumate: AES-ul fusese inlocuit cu XTEA (F-024), aritmetica pe 32 de biti scoasa din codul fierbinte (F-028), `AppKey` mutat din RAM in HEF (F-029), buclele de intarziere unificate (F-030), `SPI1_Open` scos din legatura si frame counter-ul trecut pe `Word32` (F-034).
- **Fix — criptografia a fost scoasa integral, pe ambele capete, cu pairing-ul PASTRAT.** Rezultat masurat: **2395 de cuvinte si 95 de octeti**, adica **-1481 de cuvinte si -137 de octeti**, cu marja crescuta de la 92 la **1573 de cuvinte**. Ce s-a schimbat:
  - au disparut XTEA, CBC-MAC, CTR, `AppKey`, `SessKey`, `DevNonce`, `JoinNonce`, `HubCrypto.*` si comutatorul `PAIRING_ENCRYPT_PAYLOAD`;
  - pachetele s-au scurtat: `JOIN_REQ` 16 -> 10, `JOIN_ACCEPT` 10 -> 3, `DATA_ENC` 17 -> **`DATA_UP`** 13, `CMD_DOWN` 12 -> 4;
  - randul de sesiune din HEF s-a redus la MAGIC + `DevAddr`, iar `HEF_ROW_BUFFER_LEN` de la 25 la 9;
  - `REGISTRY_BLOB_VERSION` a trecut pe **4**;
  - pe hub, `findAppKey()` a devenit `isProvisioned()`, iar `PROVISIONED_DEVICES_INIT` a ramas o lista de DevEUI-uri.
- **Ce a RAMAS, si de ce inrolarea are in continuare rost:** ea creeaza intrarea in registrul hub-ului (fara care `remove` si dezinrolarea confirmata din F-031 nu ar exista), da senzorului bitul persistent "am voie sa vorbesc" (F-030), sincronizeaza originea contoarelor, dovedeste legatura radio in ambele sensuri o data, cu operatorul de fata, si confirma numarul dinspre autoritatea lui — tabelul din `Config.h` — spre placa. Este o **comisionare**, nu un control de acces, si asa trebuie descrisa peste tot.
- **Ce s-a PIERDUT, scris raspicat:** reteaua nu mai este autentificata. Oricine cu un radio pe aceiasi parametri poate injecta o temperatura falsa, poate dezinrola orice placa cu patru octeti (`A5 13 <DevAddr> 02`), poate inrola o placa falsa cat fereastra este deschisa, si poate rejuca orice pachet. Singura aparare ramasa pe calea de date este frame counter-ul strict crescator.
- **Trei capcane platite in timpul fixului:**
  1. **`Word32` nu se sterge odata cu cifrul.** Uniunea statea in sectiunea cifrului, dar o folosesc `Nvm_LoadFrameCounter`, `Nvm_SaveFrameCounter` si `Packet_BuildDataUp`. Stearsa din reflex, cele trei ar fi fost rescrise "cu shift-uri" si ar fi reintrodus ~300 de cuvinte (F-028) — o cincime din tot castigul, fara ca nimeni sa observe, fiindca marja e acum mare si nimic nu mai doare.
  2. **`HEF_MAGIC_SESSION` a trebuit schimbat de la `0xC3` la `0xC4`.** Randul de sesiune vechi incepea cu `0xC3` urmat de `DevAddr` — exact formatul nou, octet cu octet. Cu marcajul neschimbat, firmware-ul nou ar fi citit o sesiune veche ca valida si ar fi emis catre un hub al carui registru tocmai fusese golit de `REGISTRY_BLOB_VERSION = 4`: cinci placi blocate in `DevAddr ... nu este inrolat`, fiecare recuperabila doar cu trei secunde de buton, pe teren. Cu marcajul schimbat, ambele capete pornesc golite simultan si recuperarea este cea normala.
  3. **`RegMaxPayloadLength` a trebuit recalibrat.** Cat timp `DATA_ENC` avea 17 octeti si limita era 16, modemul arunca singur pachetele celorlalti senzori — jumatate din apararea ferestrei de downlink (F-035), obtinuta din intamplare. Cu pachete de 13 octeti filtrul ar fi disparut in tacere. `LORA_RX_BUFFER_LEN` a coborat la **6**, deci acum arunca si `DATA_UP` (13), si `JOIN_REQ` (10) ale celorlalti: filtru mai bun decat inainte.
- **Doua lucruri s-au imbunatatit ca efect secundar:** `DevAddr` circula acum in clar in `JOIN_ACCEPT`, deci senzorul poate filtra fereastra de join pe adresa — ce F-035 lasase dinadins netratat, fiindca adresa era cifrata. Si, tot de acolo, o placa programata cu un numar care nu corespunde randului ei din tabel isi refuza singura `JOIN_ACCEPT`-ul, ceea ce inlocuieste diagnosticul "MIC gresit" pierdut odata cu cifrul.
- **RECUPERARE:** ultima stare cu criptografie este commit-ul **`a710142`** ("codul 3 senzori"). De acolo se reintroduce cifrul dupa upgrade-ul de microcontroller. **Nu compensa intre timp cu nimic facut in casa** — un pseudo-MIC de o suta de cuvinte care nu opreste pe nimeni este mai rau decat o absenta onesta, fiindca cineva se va baza pe el.

---

## 4. Criterii de acceptanta

Fotografie a ce s-a verificat, la 2026-08-29. Randurile taiate sunt cerinte
care **nu mai sunt acoperite** dupa F-038 — se pastreaza ca sa se vada exact
ce s-a pierdut odata cu cifrul.

| Cerinta | Unde este acoperita |
|---------|---------------------|
| Un senzor provizionat se inroleaza **doar** cand hub-ul e in mod pairing | `TestPairing::handleJoinRequest()`, prima verificare |
| Un senzor neprovizionat e respins | idem, `DeviceRegistry::isProvisioned()` |
| ~~Un senzor cu MIC gresit e respins~~ | **NU MAI ESTE ACOPERIT** (F-038): apartenenta la lista de provisioning este declarata, nu dovedita |
| Temperatura trece prin `decode()` existent, acelasi ca la testul 7 | `TestPairing::handleData()` |
| ~~Un `JOIN_REQ` rejucat e respins~~ | **NU MAI ESTE ACOPERIT** (F-038): `DevNonce` a disparut odata cu derivarea cheii |
| Un `DATA_UP` rejucat e respins | verificarea `frameCounter > lastFrameCounterUp` — singura aparare ramasa pe calea de date |
| Dupa reset de alimentare, senzorul reia comunicarea fara re-pairing si fara reutilizarea unui counter | HEF + saltul cu `FCNT_CHECKPOINT_EVERY` |
| `remove <DevEUI>` + `RESET` dezinroleaza curat, **si dezinrolarea este confirmata, nu presupusa** | `commandRemove()` marcheaza; `handleData()` retrimite `RESET` la fiecare pachet al device-ului marcat; `servicePendingRemovals()` sterge abia dupa `REMOVE_CONFIRM_SILENCE_MS` de tacere (F-031) |
| Downlink-ul ajunge efectiv la senzor | fereastra de receptie se deschide imediat dupa TX, fara nicio intarziere blocanta (F-032) |
| Un `RESET` pierdut nu lasa senzorul blocat in retea | inregistrarea se pastreaza cat timp senzorul se aude, deci hub-ul poate reincerca oricat (F-031) |
| `remove` pe un senzor oprit nu blocheaza registrul | `commandRemove()` refuza marcarea daca `hasUplink` este fals si trimite operatorul la `force` |
| Registrul hub-ului persista peste repornire | `DeviceRegistry` pe NVS |
| Senzorul se inroleaza **doar la cererea explicita a utilizatorului** | `DEV_STATE_IDLE` este starea implicita; fereastra se deschide numai din `ButtonPair_HeldLong()` si se inchide dupa `PAIRING_MAX_ATTEMPTS` incercari |
| Dupa `CMD_DOWN(RESET)` senzorul nu se re-inroleaza singur | ramura `CMD_TYPE_RESET` trece in `DEV_STATE_IDLE`, nu in `DEV_STATE_JOINING` |
| Inrolat, senzorul **doarme** intre transmisii | `Sleep_Cycle()`, chemata la finalul ciclului din `DEV_STATE_OPERATING`; trezire pe WDT, fara timer si fara rutina de intrerupere |
| Somnul **nu** inghite fereastra de downlink | `Sleep_Cycle()` se cheama abia dupa ce fereastra s-a inchis si eventualul `CMD_DOWN` a fost tratat (F-032) |
| Butonul raspunde si in timpul somnului | somnul e fragmentat in reprize de ~2,11 s, cu butoanele citite la fiecare trezire; RC5 este pe PORTC, iar acest device NU are interrupt-on-change pe PORTC |
| Dupa `CMD_DOWN(RESET)` senzorul ramane **treaz** in repaus | bucla sare peste somn cand `deviceState != DEV_STATE_OPERATING` |
| Somnul nu strica anti-replay-ul | `SLEEP` pastreaza RAM-ul, deci schema de checkpoint din F-022 ramane neschimbata |
| Hub-ul tine **5 senzori** simultan | `HUB_MAX_SENSORS` = 5; `PROVISIONED_DEVICES_INIT` are cele 5 randuri |
| Fiecare senzor are un **numar stabil**, care nu depinde de istorie | `DeviceRegistry::addressForEui()` (F-037) |
| Se stie **de la ce senzor SPUNE ca vine** fiecare masuratoare | `DevAddr` in octetul `[2]` din `DATA_UP`. **Atributia este declarativa, nu dovedita** (F-038) |
| Senzorii **nu emit sincronizat**, si nici nu raman ciocniti daca s-au ciocnit o data | Interval propriu din `DevAddr` plus jitter aleator, in `Sleep_Cycle()` (F-036) |
| Un pachet al altui senzor **nu inchide** fereastra de downlink | Filtrul `wantType` + `wantLen` + `devAddr` din `LoRa_Receive()`, plus filtrul hardware `RegMaxPayloadLength` (F-035) |
| Coliziunile sunt **vizibile**, nu tacute | Golurile din frame counter se numara ca `lostPackets`; apar in `sensors` si in `stats` |
| Un senzor care **cade** este semnalat | `serviceOfflineWatch()` anunta o data tacerea si o data revenirea |
| Dezinrolarea prin tacere **rezista** intervalelor de somn diferite | `REMOVE_CONFIRM_SILENCE_MS` = 180 s (F-036) |
| Reprogramarea unei placi cu alt numar chiar schimba identitatea | `Nvm_LoadOrCreateProvisioning()` (F-037) |
| Firmware-ul senzorului **incape** in PIC16LF1508 | **VERIFICAT**, cifrele in [MEMORY.md](MEMORY.md) |
| Reteaua **nu** este autentificata | **ASUMAT EXPLICIT** (F-038) |
