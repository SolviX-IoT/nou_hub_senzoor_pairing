# CLAUDE.md — SolviX HUB + SENZOR

> **Doar regulile.** Se incarca in fiecare sesiune, deci ramane scurt.
> - **[MEMORY.md](MEMORY.md)** — tot restul: jurnalul pe commit-uri, starea
>   curenta (cifre, versiuni), referinta (pini, radio, protocol, memorie,
>   rolul fiecarui fisier). Citeste-l inainte de a atinge codul.
> - **[ISTORIC.md](ISTORIC.md)** — arhiva `F-001…F-0xx`: simptom, cauza, fix.

---

## 1. Proiectul, pe scurt

- **Senzor** (`senzor/`, MPLAB X + XC8): PIC16LF1508 + SX1276, masoara
  temperatura si o trimite prin LoRa 868 MHz.
- **Hub** (`hub/SolvixHub_Tests/`, Arduino IDE): ESP32 + SX1276 + ENC28J60,
  inroleaza pana la 5 senzori si vorbeste cu cloud-ul. Folderul si `.ino`-ul
  trebuie sa aiba acelasi nume; fisierele stau plat, fara subfoldere.

**Reteaua radio NU este autentificata** (F-038): fara MIC, cheie sau nonce.
Singura aparare pe calea de date este frame counter-ul strict crescator.
**Nu compensa cu nimic facut in casa** — cifrul se reintroduce din `a710142`
dupa upgrade-ul de microcontroller.

---

## 2. Reguli de cod

### Ambele capete
1. **Parametrii radio** (frecventa, SF, BW, CR, sync word) se schimba
   **simultan pe ambele noduri** — o singura diferenta si legatura dispare,
   fara nicio eroare.
2. **Protocolul se modifica in doua fisiere deodata:** `senzor/main.c`
   (sectiunea 4) si `hub/SolvixHub_Tests/SensorPacket.h`.
3. **Cele cinci lungimi de pachet (6 / 10 / 3 / 13 / 4) raman distincte** —
   perechea tip+lungime e singura verificare contra desincronizarii. La orice
   schimbare de lungime se recalculeaza `LORA_RX_BUFFER_LEN` (filtrul hardware
   `RegMaxPayloadLength`, F-035).
4. **Orice pachet nou poarta `DevAddr` in octetul `[2]`** (F-035).
5. **Numarul senzorului:** randul N din `PROVISIONED_DEVICES_INIT` <->
   `SENSOR_NODE_ID = N`. **Nu se rearanjeaza randurile** intr-o retea instalata
   (F-037).
6. **Somn <-> fereastra de confirmare:** `REMOVE_CONFIRM_SILENCE_MS` si
   `SENSOR_OFFLINE_MS` acopera cel putin **patru cicluri de somn in cazul cel
   mai lent** (numarul maxim, jitter maxim, LFINTOSC la limita). Altfel hub-ul
   sterge un senzor care doar doarme (F-031, F-034, F-036).
7. **Niciun numar de pin in clar:** pe hub in `Config.h`, pe senzor in blocul
   de `#define` din `main.c`.
8. **Nimic secret pe Serial** — `apiKey` si orice cheie viitoare, niciodata.
9. **Functionalitatea noua extinde `senzor/` si `hub/`**, fara foldere
   paralele (F-020).

### Senzor
10. **Fara acces SPI cand `loraReady == 0`** — MSSP-ul oprit blocheaza
    `SPI1_ByteExchange` la nesfarsit (F-013).
11. Pin analogic nou -> declarat in `ANSELC`; pin digital pe portul C -> scos
    din `ANSELC`.
12. **`-O2` si `code-model-rom = default,-f80-fff`**, setate din fereastra de
    proprietati MPLAB X, nu editand fisiere (F-027, F-029).
13. **Dupa orice cod nou, citeste raportul de memorie** si compara-l cu
    MEMORY.md (F-033).
14. **Evita `int32_t` in codul fierbinte**; foloseste uniunea `Word32` (F-028).
15. **Toate placile au acelasi `main.c`**; difera doar `SENSOR_NODE_ID`. Dupa
    programare, verifica pe hub numarul placii (`status`).
16. **Un build de verificare nu scrie in `senzor/build/` sau `senzor/dist/`**
    (F-033); daca s-a scris, *Clean* inainte de programare.
17. **Intre transmisie si fereastra de downlink nu sta nimic blocant** (F-032).
18. **`mcc_generated_files/` nu se editeaza.** Singura exceptie: `WDTE=SWDTEN`
    in `config_bits.c` — o regenerare MCC il pune pe `OFF` si senzorul nu se
    mai trezeste.
19. `Button_RawPressed()` citeste doar RC4; blocul pentru RC5 ramane comentat.

### Hub
20. **Fara `LoRa.end()` / `SPI.end()`** (F-003); oprirea radioului e
    `LoRa.sleep()`.
21. **Fara SPI din context de intrerupere** (F-004).
22. **LED-urile doar prin modulul `Leds`**, niciodata `digitalWrite` direct.
23. **`loop()` nu contine nimic blocant**; orice cerere lunga trece prin
    portile `SensorLink::lastRxMs()` / `hasPendingRemoval()`.
24. **Campuri noi in `HubConfig` doar la coada.** Cresterea
    `REGISTRY_BLOB_VERSION` goleste registrul si cere reinrolarea manuala a
    tuturor senzorilor.

---

## 3. Reguli de colaborare

1. **Nu se scrie cod nediscutat.** Nicio functie, constanta, fisier sau
   "imbunatatire" in plus fata de ce s-a cerut — se propune si se asteapta
   acordul.
2. **Nu se extinde scopul in tacere.** Un bug gasit pe langa sarcina se
   raporteaza, nu se repara din mers.
3. **Nu se sterge cod "nefolosit" fara acord** (capcana `Word32`, F-038).
4. **Informatie hardware lipsa -> PRESUPUNERE scrisa explicit**, in cod si in
   MEMORY.md; inlocuita cu fapta cand o masuratoare o confirma sau o infirma.

---

## 4. Mesajele de commit

```
<tip>(<scop>): <titlu descriptiv>
```

| Tip | Cand |
|-----|------|
| `fix` | Repara un comportament gresit — **cere o intrare `F-0xx` in ISTORIC.md**, citata in mesaj |
| `feat` | Functionalitate noua |
| `cleanup` | Scoate / simplifica cod, fara schimbare de comportament |
| `refactor` | Rearanjeaza cod, comportament identic |
| `debug` | Instrumentare, cod de diagnostic |
| `docs` | Numai documentatie |
| `build` | Proiect MPLAB X, Makefile, setari de compilator, `.gitignore` |
| `hw` | Maparea pinilor sau o presupunere de cablaj |

**Scop:** `senzor` · `hub` · `ambele` · `docs`.

- **Titlul spune ce face commit-ul**, la imperativ, in romana, ~65 de
  caractere, fara punct final. Tipul si scopul raman in engleza.
- **Fara `Co-Authored-By`** si fara alte linii de atribuire.
- Corpul este optional: detaliile stau in jurnalul din MEMORY.md.

Exemple:
```
fix(hub): reincearca DHCP-ul dupa un boot fara retea (F-047)
feat(hub): trimite heartbeat periodic catre cloud
docs(docs): muta referinta hardware in MEMORY.md
```

---

## 5. La fiecare commit, inainte de a-l face

1. **MEMORY.md → Jurnal pe commit-uri**: o intrare noua, sus — data, titlul
   commit-ului, **ce** s-a schimbat (fisiere, functii, constante), **de ce**,
   **cum s-a verificat**; pentru `senzor/`, si
   `Flash: A -> B words (±N). RAM: A -> B B.`
2. **MEMORY.md → restul**: orice cifra, versiune de format, pin, parametru
   radio, format de pachet sau rol de fisier care s-a schimbat.
3. **Bug rezolvat** -> intrare noua `F-0xx` in ISTORIC.md, cu **simptom, cauza
   si fix**.
4. **Regula noua** -> in acest fisier, doar dupa ce a fost discutata.

**Buget: ~150 de linii.** Aici stau doar reguli; referinta si povestea
deciziilor pleaca in MEMORY.md si ISTORIC.md.
