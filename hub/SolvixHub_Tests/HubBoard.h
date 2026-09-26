/*
  HubBoard.h - placa: magistrala SPI, LED-urile si afisarea pe Serial.
  ---------------------------------------------------------------------
  Module, in ordinea dependentelor, fiecare in namespace-ul lui:
    SpiBus   arbitrajul magistralei SPI partajate de ENC28J60 si LoRa
    Leds     cele doua LED-uri, fara delay()
    Console  printSeparator() / printTitle() - functii libere, fara namespace

  Fiecare modul isi pastreaza mai jos descrierea completa.
*/

#ifndef HUB_BOARD_H
#define HUB_BOARD_H

#include <Arduino.h>
#include <SPI.h>
#include "Config.h"

// =====================================================================
//  SpiBus
// =====================================================================

/*
  SpiBus - arbitrajul magistralei SPI partajate.
  ---------------------------------------------------------------------
  PROBLEMA:
  Modulul Ethernet (ENC28J60) si modulul LoRa (SX1276) sunt legate pe
  aceiasi trei pini: SCK (18), MISO (19), MOSI (23). Fiecare are propriul
  chip select: CS_ETH = GPIO4, NSS_LoRa = GPIO5.

  Un slave SPI isi pune iesirea MISO in "high impedance" (adica se
  deconecteaza electric de pe fir) doar cat timp CS-ul lui este HIGH.
  Daca ambele CS-uri ajung LOW simultan, ambele module incearca sa
  scrie pe MISO in acelasi timp: datele sunt gunoi, iar pe termen lung
  se pot deteriora iesirile.

  REGULILE respectate de tot proiectul:
  1. SPI.begin(...) se apeleaza O SINGURA DATA, aici, la pornire.
     Niciun test nu reinitializeaza magistrala.
  2. Amandoua CS-urile sunt configurate ca OUTPUT si duse pe HIGH
     INAINTE de a exista trafic pe bus. Astfel niciun modul nu este
     selectat la boot, cand pinii ar fi altfel flotanti.
  3. Inainte ca un test sa preia un modul, se apeleaza claimEthernet()
     sau claimLoRa(). Functia ridica CS-ul celuilalt modul, garantat.
  4. Fiecare acces la bus se face in interiorul unei tranzactii SPI
     (SPI.beginTransaction / endTransaction). Asa fiecare modul isi
     impune propria viteza si propriul mod SPI fara sa il incurce pe
     celalalt. Librariile EthernetENC si LoRa fac deja acest lucru
     intern, si tot codul care atinge magistrala trece azi prin ele.
  5. Nu se apeleaza NICIODATA LoRa.end() sau SPI.end(). LoRa.end()
     inchide magistrala SPI a intregului ESP32, iar modulul Ethernet ar
     ramane fara ceas. Pentru a "opri" LoRa se foloseste LoRa.sleep().
  6. Intreruperea DIO0 a modulului LoRa nu este folosita cu callback
     (LoRa.onReceive). Un callback ar declansa acces SPI din context de
     intrerupere, posibil chiar in mijlocul unui transfer Ethernet.
     Peste tot se citeste prin polling, cu LoRa.parsePacket().
*/

namespace SpiBus {

  // Configureaza pinii CS, ii duce pe HIGH si porneste magistrala.
  // Apelata o singura data, din setup(). Apelurile ulterioare nu fac nimic.
  void begin();

  // Ridica ambele chip select-uri: niciun modul nu este selectat.
  void deselectAll();

  // Da magistrala unui singur modul, deselectandu-l explicit pe celalalt.
  void claimEthernet();
  void claimLoRa();

  // Reset hardware al ENC28J60 (puls LOW pe PIN_ETH_RESET).
  // Deselecteaza LoRa inainte, ca modulul sa nu prinda zgomot pe bus.
  void resetEthernetModule();

  // Reset hardware al SX1276 (puls LOW pe PIN_LORA_RST).
  void resetLoRaModule();
}


// =====================================================================
//  Leds
// =====================================================================

/*
  Leds - cele doua LED-uri ale hub-ului (D22 si D21).
  ---------------------------------------------------------------------
  Modulul exista ca sa nu apara digitalWrite pe un numar de pin prin
  teste. Pinii si polaritatea stau doar in Config.h.

  Doua feluri de a aprinde un LED:

  - set(...)   - nivel continuu. Se foloseste pentru stare: "radioul
                 asculta", "am adresa IP".
  - pulse(...) - aprinde acum si stinge singur dupa LED_PULSE_MS.
                 Se foloseste pentru evenimente: "a venit un pachet".

  pulse() NU foloseste delay(): stingerea se face din service(), pe care
  fiecare tick() de test il apeleaza. Asa receptia urmatorului pachet nu
  este intarziata de LED. Un test care foloseste pulse() TREBUIE sa
  cheme service() in tick()-ul lui, altfel LED-ul ramane aprins.
*/

namespace Leds {

  // Configureaza ambii pini ca iesiri si stinge LED-urile. Apelata o
  // singura data, din setup(). Apelurile ulterioare nu fac nimic.
  void begin();

  // Nivel continuu pe un LED.
  void set(uint8_t pin, bool on);

  // Aprinde LED-ul si programeaza stingerea peste LED_PULSE_MS.
  void pulse(uint8_t pin);

  // Stinge pulsurile ajunse la scadenta. Se cheama din tick().
  void service();

  // Stinge ambele LED-uri si anuleaza pulsurile in curs. Se cheama la
  // oprirea unui test, ca urmatorul sa porneasca de la zero.
  void allOff();
}


// =====================================================================
//  Console
// =====================================================================

/*
  Console - ajutoarele de afisare pe Serial, folosite de toate modulele.
  ---------------------------------------------------------------------
  Fisierul se numea TestBase.h si tinea structura `Test`, adica interfata
  begin/tick/stop a suitei de teste. Suita a disparut: hub-ul nu mai este
  un meniu de teste independente, ci un aparat care porneste singur si
  ruleaza permanent. Structura `Test` a fost stearsa odata cu ea.

  Ce a ramas sunt cele trei functii de formatare, care nu au avut
  niciodata legatura cu testarea - sunt pur si simplu felul in care acest
  proiect scrie un titlu si un octet pe Serial.

  printHexByte() nu mai are apelanti de cand a fost sters TestEncSpi. Este
  pastrata dinadins: este forma in care proiectul afiseaza un registru
  citit de pe SPI (valoare hexazecimala plus reprezentarea binara), si
  prima comanda de diagnostic care se va adauga va avea nevoie de ea.
*/

void printSeparator();
void printTitle(const char* title);

#endif // HUB_BOARD_H
