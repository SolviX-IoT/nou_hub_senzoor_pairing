/*
  Console.h - ajutoarele de afisare pe Serial, folosite de toate modulele.
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

#ifndef CONSOLE_H
#define CONSOLE_H

#include <Arduino.h>
#include "Config.h"

void printSeparator();
void printTitle(const char* title);
void printHexByte(const char* label, uint8_t value);

#endif // CONSOLE_H
