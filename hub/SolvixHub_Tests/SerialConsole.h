/*
  SerialConsole.h - consola de comenzi de pe Serial.
  ---------------------------------------------------------------------
  Codul de aici statea in sketch-ul principal, langa meniul de teste.
  Meniul a disparut odata cu testele, dar comenzile in cuvinte au ramas
  si au inceput sa se inmulteasca, asa ca si-au primit modulul lor.
  SolvixHub_Tests.ino a ramas cu setup(), loop() si butonul.

  DOUA REGULI ALE ACESTUI MODUL

  1. tick() NU BLOCHEAZA. Citeste cel mult un octet per apel si il pune
     intr-un tampon; comanda se executa la Enter. Varianta veche chema
     Serial.readStringUntil('\n'), care asteapta pana la Serial.setTimeout()
     - implicit O SECUNDA - daca terminalul este pe "No line ending".
     Adica o secunda de blocaj in loop(), declansata de o tasta, cazand
     peste fereastra de downlink de 600 ms a unui senzor care tocmai a
     emis. Fereastra aceea este singura ocazie in care hub-ul poate
     raspunde, si se inchide singura.

  2. NIMIC DISTRUCTIV NU PRIMESTE O LITERA SINGURA. De cand nu mai exista
     meniul de cifre, caracterele singulare sunt din nou libere - dar o
     apasare gresita intr-un terminal nu are voie sa stearga nimic.
     Comenzile care strica ceva se scriu in intregime si cer un cuvant de
     confirmare, ca `remove ... force` de dinainte.
*/

#ifndef SERIAL_CONSOLE_H
#define SERIAL_CONSOLE_H

#include <Arduino.h>
#include "Config.h"

namespace SerialConsole {
  void begin();
  void tick();
  void printHelp();
}

#endif // SERIAL_CONSOLE_H
