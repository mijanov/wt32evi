#ifndef ALATI_H
#define ALATI_H

#include <WiFi.h>
#include <SPIFFS.h>
#include <sodium.h>
#include <HTTPClient.h>
#include <base64.h>
#include <UrlEncode.h>

#define MAX_UDPD_BUF 200
#define CONFIG_FILE "config.bin"

void alati_setup(void);
String lokIP(void);
bool nemaInternet(void);
void prikaziMACiIP(void);
void update_started(void);
void update_finished(void);
void update_progress(int cur, int total);
void update_error(int err);
void otaProg(const char* url); // puni fleš sa url
char downloadFile(const char* url, const char* filename); // preuzima fajl sa url i upisuje u fleš fajl filename
void posaljiPost(char* url, char* poruka, char* odgovor, size_t maxDuz); // šalje POST zahtjev na url
void posaljiUdp(char* urlPort, char * poruka);
void test(char* prvi, char * drugi);
void fajl2mem(char * imeFajla, uint8_t * cnf, size_t uku);
bool mem2fajl(uint8_t * cnf, char * imeFajla, size_t uku);
void prikaz(char* str);
void prikazHex(uint8_t* buf, size_t ukupno, size_t pocAdr);
void udpDebug(void);
void uradi(const char * tekst, int duzina);
void notifikacija(char* naslov, char* poruka);
extern uint8_t * structAdresaUkupno(char * imeStruct, size_t * uku); // vraća pokazivač na struct sa datim imenom i postavlja uku
bool binarySearchFile(const char *filename, const uint8_t * searchKey, uint8_t *foundRecord);

#endif // ALATI_H
