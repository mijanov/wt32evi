#include "alati.h"
#include "evirad.h"
#include <esp_task_wdt.h>

uint32_t ms;
uint32_t sekunde;
extern struct konfiguracija cnf;
extern char udpDbuf[MAX_UDPD_BUF+1];



void setup() {
  alati_setup();
  eviradSetup();
  fajl2mem( "/config",  (uint8_t*) &cnf, sizeof(cnf)); // /config prenosi u cnf
  esp_task_wdt_init(60, true);
  esp_task_wdt_add(NULL); //add current thread to WDT watch
}


void loop() {
  uint16_t dms = millis() - ms;

  if(Serial.available()) {
    String komanda = Serial.readStringUntil('\r');
    uradi(komanda.c_str(), komanda.length());
  }

  // 8 puta u sekundi
  if( ! (dms & 127) ) {
    eviradLoop(); 
    if(udpDbuf[0]) uradi(udpDbuf, strlen(udpDbuf)); // ako primi UDP komandu
    udpDbuf[0]=0;
  }

  // svake sekunde
  if(dms > 1000) {
    esp_task_wdt_reset();
    ms += 1000;
    digitalWrite(LED1, !digitalRead(5));
    ++sekunde;
    javiSeServeru(); // periodično obraćanje serveru
    udpDebug(); // održava udp konekciju za debug
  }

}  
