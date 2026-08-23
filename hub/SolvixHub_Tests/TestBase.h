/*
  TestBase.h - interfata comuna a tuturor testelor.
  ---------------------------------------------------------------------
  Fiecare test este un mic modul cu trei functii:

    begin() - pregateste hardware-ul si spune daca a reusit
    tick()  - un pas de lucru, apelat repetat din loop(); NU are voie sa
              blocheze la nesfarsit, ca sa ramana posibila schimbarea
              testului din meniul serial
    stop()  - lasa hardware-ul intr-o stare neutra pentru testul urmator
              (in special: ridica CS-ul modulului folosit)

  Testele nu se apeleaza intre ele si nu isi impart variabile globale.
*/

#ifndef TEST_BASE_H
#define TEST_BASE_H

#include <Arduino.h>
#include "Config.h"
#include "SpiBus.h"

struct Test {
  const char* name;
  const char* description;
  bool (*begin)();
  void (*tick)();
  void (*stop)();
};

// Ajutoare de afisare folosite de mai multe teste.
void printSeparator();
void printTitle(const char* title);
void printHexByte(const char* label, uint8_t value);

#endif // TEST_BASE_H
