#ifndef EVIRAD_H
#define EVIRAD_H

#include <Arduino.h>
#include <WiFi.h>
//#include "/home/zoran/snap/arduino/85/.arduino15/packages/esp32/hardware/esp32/2.0.10/libraries/WiFi/src/WiFi.h"
#include <HTTPClient.h>
#include <SPIFFS.h>

#define FORMAT_SPIFFS_IF_FAILED true

#define LED1 5 //  RX2 LED na WT32 modulu
#define LED2 17 // TX2 LED na WT32 modulu
#define WG1_D1 35  // J25
#define WG1_D0 14  // J24
#define WG1_LED 4 // J26
#define WG2_D1 36  // J19
#define WG2_D0 39  // J18
#define WG2_LED 15 // J20
#define OUT1 2     // J31
#define OUT2 12     // J30

#define MAX_NEPOZNATIH 256
#define MAX_KARTICA 10000
#define MAX_DOGADJAJA 1024
#define MAX_RX_DUZ 128 // mora biti usklađeno sa maxTxDuz u php-u
// wt32 šalje šifrat bilo koje dužine
// php šifrate duže od MAX_RX_DUZ šalje parče po parče, te ne može da zaguši WT32

// način rada
#define KODIRAJ 1
#define DEKODIRAJ 2
#define UPISUJ_NEPOZNATE 4
#define UDP_DEBUG 8 // 

const char verzijaProg='2';

struct konfiguracija {
  char jaSam[20]; char kljuc[32]; uint8_t opcija; char verzijaKartica; char verzijaCnf; // 'lični podaci'
  char wifiSsid[16]; char wifiPass[16]; // Ako nema eterneta, ide na wifi mrežu - klijent (Station)
  char mojSsid[16]; char mojPass[16]; // Ako treba da napravi wifi mrežu - hub (Access poin)
  char serverUrl[60]; uint16_t serverPort; uint16_t serverInterval;// server za POST zahtjeve
  char udpServer[60]; uint16_t udpPort; uint16_t udpInterval; // udp server (umjesto wssHosta) i post server mogu biti različiti
  uint8_t nacin; // da_ne: kodiranje, dekodiranje, upisuj nepoznate kartice
  char pushServer[60]; char slusalac[16]; uint8_t pushMaks; // za notifikaciju, maksimalan broj notifikacije
  uint16_t frek[2][2]; uint8_t ispuna[2][2]; uint8_t x250ms[2][2]; // 2 brave sa po 2 intervala različitog rada
};


struct asocijacija {
  char ime[30];
  int tip; // */- broj okteta
  void * pok;
};


void eviradSetup(void);
void otvori(char vrata);
char prebaciDogadjajeUFles(void);
void logNepoznate(uint8_t* kartica, char citac);
uint8_t pravo_i_idKartice(uint8_t* kartica, uint16_t& idKartice);
char pravoProlaza(char pravo, char citac);
void obradiKarticu(char citac, uint8_t* kartica);
void eviradLoop(void);
bool uradiEvirad(char * komanda);
bool citajPisi(char * komanda);
void postaviPwm(int pin, uint16_t frekv, uint8_t ispuna);
uint8_t * structAdresaUkupno(char * imeStruct, size_t * uku);
void javiSeServeru(void);
void helpEvirad(void);

#endif // EVIRAD_H
