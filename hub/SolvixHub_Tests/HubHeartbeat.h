/*
  HubHeartbeat.h - semnul de viata periodic catre server, si configul
  inapoi de la el.
  ---------------------------------------------------------------------
  PASUL DE DUPA BOOTSTRAP. HubCloud duce hub-ul pana in starea Ready -
  server sanatos, identitate in flash - si de acolo tace. De acolo incepe
  modulul asta: un POST /api/device/heartbeat la fiecare
  heartbeatIntervalSeconds, cat timp hub-ul este alimentat.

  DE CE UN MODUL SEPARAT, SI NU O STARE IN PLUS IN HubCloud
  ---------------------------------------------------------------------
  Cele doua au cicluri de viata diferite, si asta se vede in cod inainte
  sa se vada in comportament. Bootstrap-ul se face O DATA si apoi tace
  pentru totdeauna; heartbeat-ul nu se termina niciodata. Bootstrap-ul are
  backoff care creste si o stare finala Blocked; heartbeat-ul nu are
  fundatura - un heartbeat esuat nu inchide nicio usa pe server, deci nu
  are de ce sa se opreasca vreodata din incercat.

  Amestecate, ar fi insemnat un HubCloud.cpp de ~900 de linii in care doua
  ritmuri diferite isi impart aceleasi contoare - exact greseala din
  F-043, unde un singur contor pentru doua lucruri diferite a facut ca
  backoff-ul sa nu creasca niciodata.

  CELE DOUA VALORI DE LA SERVER
  ---------------------------------------------------------------------
  heartbeatIntervalSeconds si heartbeatTimeoutSeconds sunt primele doua
  valori din obiectul "config" al provisioning-ului care chiar se
  folosesc. Pana acum toate unsprezece erau salvate si nefolosite.

    heartbeatIntervalSeconds  -> ritmul. Marginit de
                                 HEARTBEAT_MIN_INTERVAL_S: un parametru
                                 primit prin retea nu are voie sa poata
                                 opri receptia radio (vezi Config.h).

    heartbeatTimeoutSeconds   -> toleranta SERVERULUI, nu a noastra: dupa
                                 atata tacere el ne considera cazuti. Se
                                 foloseste in doua feluri. La pornire se
                                 verifica interval < timeout si se striga
                                 daca nu - altfel hub-ul ar fi declarat
                                 offline INTRE doua batai perfect
                                 reusite, iar cauza ar fi de negasit din
                                 teren. In rulare, cand ultima bataie
                                 reusita este mai veche decat timeout-ul,
                                 hub-ul stie ca serverul il crede mort si
                                 o spune - o data la cadere si o data la
                                 revenire, ca serviceOfflineWatch() din
                                 SensorLink.

  CINE BATE: corpul cererii NU contine niciun identificator de hub.
  Singurul lucru care spune serverului cine suntem este antetul
  X-Solvix-ApiKey, cu cheia per hub primita la provisioning. Ea nu apare
  niciodata pe Serial (regula 11 din CLAUDE.md).

  CE SE TRIMITE, SI CE ESTE INVENTAT
  ---------------------------------------------------------------------
  Schema serverului cere douazeci de campuri. Placa asta poate masura
  onest opt dintre ele; restul sunt hardware pe care hub-ul nu il are.
  Ele se trimit cu valori fixe, si FIECARE ISI POARTA PRESUPUNEREA scrisa
  alaturi in HubHeartbeat.cpp (regula 6.2.5) - o cifra plauzibila si
  inventata este mai rea decat un zero care se vede ca zero.

  A DOUA CERERE: GET /api/device/config
  ---------------------------------------------------------------------
  Cand o bataie raspunde cu configUpdateRequired=true, hub-ul cere
  configul nou de la /api/device/config - fara corp, cu acelasi antet de
  cheie - il salveaza in NVS prin HubIdentity::storeConfig() si isi
  schimba pe loc ritmul, daca acesta s-a schimbat. Nu asteapta o
  repornire.

  De ce sta in ACELASI modul cu heartbeat-ul, desi este alt endpoint:
  declansatorul vine din raspunsul heartbeat-ului si rezultatul schimba
  chiar ritmul heartbeat-ului. Sunt aceeasi conversatie cu serverul -
  „uite cum sunt" / „uite cum sa te porti" - nu doua.

  DOUA REGULI CARE NU SE INCALCA:

    1. CEL MULT O CERERE PER tick(), chiar daca si bataia, si configul
       sunt scadente. Doua cereri blocante la rand ar dubla fereastra in
       care hub-ul e surd. Configul are prioritate: e evenimentul rar.

    2. CEL MULT O PRELUARE PER configVersion ANUNTATA. Serverul stinge
       configUpdateRequired la citire, deci in mod normal se cere o
       singura data - dar daca vreodata nu il stinge, sau daca uita sa
       incrementeze configVersion, hub-ul ar cere acelasi config la
       fiecare bataie, pentru totdeauna. Garda taie exact asta si o spune
       o data pe Serial.

  Un config care nu s-a putut lua nu opreste nimic: hub-ul ramane pe cel
  vechi - care este chiar cel dupa care merge - si reincearca. O bataie
  esuata si un config nepreluat sunt lucruri diferite, cu contoare
  diferite.

  CE NU FACE
  ---------------------------------------------------------------------
  pendingCommandCount se RAPORTEAZA si atat: pentru comenzi nu exista
  inca endpoint. A implementa pe jumatate un comportament comandat de
  server produce exact purtarea pe care nimeni nu o poate testa.

  Din configul preluat se folosesc tot doar cele doua valori de heartbeat.
  Celelalte zece se salveaza si asteapta telemetria - dar se salveaza, ca
  sa fie acolo in ziua in care va exista cine sa le foloseasca.

  nextHeartbeatInSeconds se respecta: este singurul camp din raspunsul
  batailor pe care hub-ul chiar il poate onora. Trece si el prin podeaua
  si tavanul din Config.h.

  CAND NU BATE
  ---------------------------------------------------------------------
  Cererea este blocanta, ca toate celelalte, deci trece prin ACELEASI
  doua porti ca HubCloud: nu porneste cat timp un senzor tocmai a vorbit
  si isi tine fereastra de downlink deschisa (SensorLink::lastRxMs()), si
  nu porneste cat timp o dezinrolare asteapta confirmarea
  (SensorLink::hasPendingRemoval()). O bataie scadenta care prinde un
  moment prost nu se pierde: se incearca din nou la urmatoarea trecere
  prin loop().
*/

#ifndef HUB_HEARTBEAT_H
#define HUB_HEARTBEAT_H

#include <Arduino.h>
#include "Config.h"

namespace HubHeartbeat {

  // Zero I/O. Doar pune contoarele pe zero; ritmul se citeste din
  // identitate abia cand HubCloud ajunge in Ready, fiindca inainte de
  // provisioning configul nici nu exista.
  void begin();

  // Se cheama din loop(). Costa o comparatie cat timp nu are nimic de
  // facut.
  void tick();

  // Linia (sau cele doua) din comanda `status`. Singura fereastra spre
  // starea modulului - nu exista accesorii separate, fiindca nu are
  // cine sa le foloseasca.
  void printStatus();
}

#endif // HUB_HEARTBEAT_H
