#include "alati.h"
#include "evirad.h"

#include <WebServer_WT32_ETH01.h>
#include <AsyncUDP_WT32_ETH01.h>

#define DEBUG_ETHERNET_WEBSERVER_PORT       Serial
#define _ETHERNET_WEBSERVER_LOGLEVEL_       3

#include <HTTPUpdate.h>

void primiUdp(AsyncUDPPacket packet);
char udpDbuf[MAX_UDPD_BUF+1];
AsyncUDP udp;
AsyncUDP udpD;// udpD je samo za debug

extern struct konfiguracija cnf;
extern uint32_t sekunde;

//***********************


void primiUdp(AsyncUDPPacket packet) {
  Serial.print("Received UDP Packet Type: ");
  Serial.println(packet.isBroadcast() ? "Broadcast" : packet.isMulticast() ? "Multicast" : "Unicast");
  Serial.print("From: "); Serial.print(packet.remoteIP()); Serial.print(":"); Serial.print(packet.remotePort());
  Serial.print(", To: "); Serial.print(packet.localIP());  Serial.print(":"); Serial.print(packet.localPort());
  Serial.print(", Length: "); Serial.println(packet.length());
  Serial.write(packet.data(), packet.length()); Serial.println();
}
 
void udpDebug() { // održava debug konekciju na udpServer
  static int doUdpD=5;
 
  if(doUdpD) { --doUdpD; return; }
  doUdpD = cnf.udpInterval;

  if( ! udpD.connected() ) {
    IPAddress ip;
    if ( 1 == WiFi.hostByName(cnf.udpServer, ip)) udpD.connect(ip, cnf.udpPort);
  }
  else {
    char buf[30]= "jaSam=";
    strcat(buf, cnf.jaSam);
    udpD.write((uint8_t *) buf, strlen(buf)); // samo za održavanje konekcije
  }
}

void alati_setup(void) {
  Serial.begin(115200);
  // Serial.setDebugOutput(true);

  WT32_ETH01_onEvent();
  ETH.begin(ETH_PHY_ADDR, ETH_PHY_POWER);
  //  ETH.config(myIP, myGW, mySN, myDNS); // za namještanje statičke IP adrese

  // varijanta WiFi veze
//  WiFi.mode(WIFI_STA); WiFi.begin(cnf.wifiAp, cnf.wifiPass); // WifiMulti.addAP("TP-LINK_B36B52", "");
//  Serial.print("moja IP adresa: ");  Serial.println(WiFi.localIP());

//  bootloader_random_enable(); //!!! Potrebno ako WiFi nije uključen. https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/random.html
  
  // Initialize the libsodium library
  if (sodium_init() == -1) {
    prikaz("Error initializing libsodium.");
    while (1) {}
  }

  udp.onPacket([](AsyncUDPPacket packet) { primiUdp(packet); });
  udpD.onPacket([](AsyncUDPPacket packet) { 
    uint16_t duz = packet.length();
    if(duz> MAX_UDPD_BUF) duz=MAX_UDPD_BUF;
    memcpy((char*) udpDbuf, packet.data(), duz);
    udpDbuf[duz]=0;
  });
}

void fajl2mem(char * imeFajla, uint8_t * mem, size_t uku) {
  File file = SPIFFS.open(imeFajla, "rb"); // Open the file in read binary mode
  if (!file) {
    prikaz("Failed to open file\n");
    return;
  }
  size_t fileSize = file.size();
  if( uku != fileSize) {
    char txt[100];
    sprintf(txt, "struct size = %zu, file size = %zu\n", uku, fileSize);
    prikaz(txt);
  }
  
  // Read the struct from the file
  if (file.read(mem, fileSize) != fileSize) {
    prikaz("Error reading struct from file");
  }
  else prikaz("\ngotovo\n");
  file.close();
}

bool mem2fajl(uint8_t * mem, char * imeFajla, size_t uku) {
  File file = SPIFFS.open(imeFajla, "wb"); // Open the file in read binary mode
  if (!file) {
    prikaz("Failed to open file\n");
    return false;
  }
  // Write the memory contents to the file
  size_t upisano = file.write(mem, uku);
  file.close();

  if (upisano != uku) {
    prikaz("Error writing to file\n");
    return false;
  }
  prikaz("\ngotovo\n");
  return true;
}

String lokIP(void) {
  return ETH.localIP().toString();
}

bool nemaInternet(void){ // !!! dodati da provjeru WiFi i pristupa internetu
  if ( WT32_ETH01_isConnected()) return false;
  //  if (WiFi.status() == WL_CONNECTED) return false;
  return true;
}

void notifikacija(char* naslov, char* opis){
  if (nemaInternet()) return;

  HTTPClient http;
  String url = String(cnf.pushServer) + "/?k=" + cnf.slusalac + "&t=" + naslov + "&c=" + opis + "&u=" + "dk.hexco.biz";
  http.begin(url);
  int httpCode = http.GET();
  char buf[100]; sprintf( buf, "notifikacija ... code%d\n", httpCode); prikaz(buf);
  http.end();
}    

void prikaziMACiIP(void){
  char buf[300];
  char * pb = buf;
  pb += sprintf(pb, "moja ETH MAC adresa: %s\n", ETH.macAddress().c_str());
  pb += sprintf(pb, "moja ETH IP adresa: %s\n", ETH.localIP().toString().c_str());
  pb += sprintf(pb, "moja WiFi MAC adresa: %s\n", WiFi.macAddress().c_str());
  pb += sprintf(pb, "moja WiFi IP adresa: %s\n", WiFi.localIP().toString().c_str());
  prikaz(buf);
}


 
void update_started() { Serial.println("CALLBACK:  HTTP update process started"); }
void update_finished() { Serial.println("CALLBACK:  HTTP update process finished"); }
void update_progress(int cur, int total) { Serial.printf("CALLBACK:  HTTP update process at %d of %d bytes...\n", cur, total);}
void update_error(int err) { Serial.printf("CALLBACK:  HTTP update fatal error code %d\n", err);}

void otaProg(const char* url) {
  Serial.printf("\npunim fleš sa %s\n", url);
  if(nemaInternet()) return;
  WiFiClient client;

  // The line below is optional. It can be used to blink the LED on the board during flashing
  // The LED will be on during download of one buffer of data from the network. The LED will
  // be off during writing that buffer to flash
  // On a good connection the LED should flash regularly. On a bad connection the LED will be
  // on much longer than it will be off. Other pins than LED_BUILTIN may be used. The second
  // value is used to put the LED on. If the LED is on with HIGH, that value should be passed
  // httpUpdate.setLedPin(LED_BUILTIN, LOW);

  httpUpdate.onStart(update_started);
  httpUpdate.onEnd(update_finished);
  httpUpdate.onProgress(update_progress);
  httpUpdate.onError(update_error);

  t_httpUpdate_return ret = httpUpdate.update(client, url );
  //t_httpUpdate_return ret = httpUpdate.update(client, "hub.hexco.biz", 80, "/p-esp32.bin"); // drugi način
  Serial.println( "HTTP CODE = " + String(ret));
  switch (ret) {
    case HTTP_UPDATE_FAILED:
      Serial.printf("HTTP_UPDATE_FAILED Error (%d): %s\n", httpUpdate.getLastError(), httpUpdate.getLastErrorString().c_str());
      break;

    case HTTP_UPDATE_NO_UPDATES:
      Serial.println("HTTP_UPDATE_NO_UPDATES");
      break;

    case HTTP_UPDATE_OK:
      Serial.println("HTTP_UPDATE_OK");
      break;
  }
}

void uploadFile(const char* filePath, const char* serverUrl) {
  if(nemaInternet()) return;
      
  HTTPClient http;

  File file = SPIFFS.open(filePath, "rb");
  if(! file) { prikaz("Failed to open local file\n"); return; }
  
  // Start the HTTP PUT request
  http.begin(serverUrl);
  http.addHeader("Content-Type", "application/octet-stream");

  // Send the file content using the HTTP PUT request
  int httpCode = http.sendRequest("PUT", &file, file.size());

  char buf[100]; sprintf(buf, "HPPT PUT ... code:%d\n", httpCode); prikaz(buf);
  
  http.end();
  file.close();
}


//*************************
char downloadFile(const char* urlS, const char* filename) {

  WiFiClient client;
  HTTPClient http;
  char url[200];
//  strcpy(url, urlEncode(urlS).c_str());
  strcpy(url, urlS);

  prikaz("\nDownloading file from: "); prikaz(url);

  if (http.begin(client, url)) {
    int httpCode = http.GET();

    if (httpCode > 0) {
      if (httpCode == HTTP_CODE_OK) {
        File file = SPIFFS.open(filename, "w");
        if (!file) {
          prikaz("\nFailed to create file\n");
        } else {
          Stream* response = http.getStreamPtr();
          uint8_t buffer[MAX_RX_DUZ];
          while (response->available()) {
            size_t bytesRead = response->readBytes(buffer, MAX_RX_DUZ);
            file.write(buffer, bytesRead);
          }
          file.close();
          prikaz("\nFile downloaded successfully\n");
          http.end(); return 1;
        }
      } else {
        char buf[100];
        sprintf(buf, "\nHTTP error code: %d", httpCode); prikaz(buf);
      }
    } else {
      prikaz("\nConnection failed\n");
    }
    http.end();
  } else {
    prikaz("\nUnable to connect to the server\n");
  }
  return 0;
}

//*********************
uint32_t koder(char* jaSam, char* poruka, uint32_t porukaDuz, unsigned char* sifrat) {
  unsigned int jaSamDuz = strlen(jaSam);
  if( ! porukaDuz) porukaDuz = strlen(poruka);
  unsigned long long sifratDuz = jaSamDuz + 1 + crypto_stream_chacha20_ietf_NONCEBYTES + porukaDuz + crypto_aead_chacha20poly1305_ietf_ABYTES;

  memcpy(sifrat, jaSam, jaSamDuz); // na početku stavljamo ime pošiljaoca
  sifrat[jaSamDuz] = '|'; // dodajemo delimiter '|' iza jaSam

  unsigned char * nonce = sifrat + jaSamDuz + 1; // ovdje počinje nonce
  unsigned char * kript = nonce + crypto_stream_chacha20_ietf_NONCEBYTES; // početak šifrata
  
  esp_fill_random(nonce, crypto_stream_chacha20_ietf_NONCEBYTES); // generišemo nonce

  crypto_aead_chacha20poly1305_ietf_encrypt(
    kript, NULL, // dužina kripta nam ne treba, znamo da je = porukaDuz + 28
    (const unsigned char*)poruka , porukaDuz,
    (const unsigned char*) jaSam, jaSamDuz, // aditional data = jaSam
    NULL, // nonce secret ne koristimo
    nonce, // nonce public
    (const unsigned char*)cnf.kljuc);

  return sifratDuz; // ukupna dužina kodirane poruke sa jaSam| + nonce + sifrat
}  
  
uint32_t dekoder(char* jaSam, unsigned char* sifrat, unsigned long long sifratDuz, char* poruka) {
  unsigned long long porukaDuz = sifratDuz - crypto_stream_chacha20_ietf_NONCEBYTES - crypto_aead_chacha20poly1305_ietf_ABYTES;
//  unsigned char* poruka = (unsigned char*)malloc(porukaDuz + 1); // reservišemo memoriju za dekodiranu poruku, +1 za terminirajuću nulu

  crypto_aead_chacha20poly1305_ietf_decrypt(
    (unsigned char*) poruka, NULL, // NULL umjesto &porukaDuzina. Ne treba nam porukaDuzina jer je već znamo.
    NULL, // nsec
    sifrat + crypto_stream_chacha20_ietf_NONCEBYTES, // kriptovani dio
    sifratDuz - crypto_stream_chacha20_ietf_NONCEBYTES, // njegova dužina 
    (const unsigned char*)jaSam, strlen(jaSam),
    sifrat, // nonce
    (const unsigned char*)cnf.kljuc);

  poruka[porukaDuz]=0; // terminiramo string
  return porukaDuz;
}  

struct ipadr_port {
  IPAddress ip;
  uint16_t port;
} adrPort;

void nadjiAdrPort(char * url) {
  char * ipPort = strchr(url, ':');
  if(ipPort == NULL) adrPort.port = 80;
  else {
    *ipPort = 0;
    adrPort.port = atoi(++ipPort);
  }
  WiFi.hostByName(url, adrPort.ip);
}

 
void posaljiPost(char* url, char* poruka, char* odgovor,  size_t maxDuz){
  WiFiClient client;
  HTTPClient http;

  uint8_t *sifrat = (uint8_t *) malloc(4096); // ne očekujemo duže
  uint32_t sifratDuz = koder( cnf.jaSam, poruka, 0, sifrat); 
  
//  Serial.printf("porukaDuz=%d, sifratDuz=%d maxDuz=%d\n", strlen(poruka), sifratDuz, maxDuz); //!!! maći
  http.begin(client, url);
//  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS); // Enable automatic redirection following
  http.addHeader("Content-Type", "application/octet-stream");
  http.addHeader("Content-Length", String(sifratDuz));
  int httpCode = http.POST(sifrat, sifratDuz); // start connection and send HTTP header and body
  char buf[100]; sprintf(buf, "\n[HTTP] POST... code: %d\n", httpCode); prikaz(buf);

  // httpCode will be negative on error
  if (httpCode > 0) { // HTTP header has been send and Server response header has been handled
    if (httpCode == HTTP_CODE_OK) {
      Stream* response = http.getStreamPtr();
      response->setTimeout(1000);
      uint8_t buffer[MAX_RX_DUZ];

      while (response->available()) {
        size_t bytesRead = response->readBytes(buffer, MAX_RX_DUZ);

        size_t otvorenoDuz = dekoder(cnf.jaSam, buffer, bytesRead, odgovor);
        prikaz(odgovor);
        odgovor += otvorenoDuz;
        maxDuz -= otvorenoDuz;
//        Serial.printf("otvorenoDuz=%d odgovor=%s maxDuz=%d\n", otvorenoDuz, odgovor, maxDuz); //!!maci
      }
      *odgovor = 0; // terminiranje odgovora
    }
  }
  http.end();
  free(sifrat);
}



void test(char* prvi, char * drugi) {

// saljiServeru
  char odgovor[302];
  posaljiPost(cnf.serverUrl, prvi, odgovor,  300);
  prikaz(odgovor);
  return;

// nadzornik

/*  prikaz("nakon 13s WD treba da resetuje WT32\n");
  uint32_t  sada= millis();
  while( millis() - sada < 20000);
*/

// kodiranje i dekoderanje
  
  char txPoruka[100] = {"Zdravo kako se osjećate danas?"};
  char rxPoruka[105];
  uint8_t sifrat[150];
  
  Serial.println("\nOriginal: ");
  for (int i = 0; i < strlen(txPoruka); i++) {
    Serial.print(txPoruka[i], DEC); Serial.print(" ");
  }
  
  unsigned long long sifratDuz = koder(cnf.jaSam, txPoruka, 0, sifrat);

  Serial.println("\nŠifrat: ");
  for (int i = 0; i < sifratDuz; i++) {
    Serial.print(sifrat[i], HEX); Serial.print(" ");
  }
  
  uint32_t porukaDuz = dekoder(cnf.jaSam, sifrat + strlen(cnf.jaSam) + 1, sifratDuz - strlen(cnf.jaSam) - 1, rxPoruka);

  Serial.println("\nDekodirano: ");
  Serial.println((char*)rxPoruka);
}

void prikaz( char* str) {
//  if(cnf.nacin & UDP_DEBUG) 
//  else Serial.print(str);
  Serial.print(str);
  if(udpD.connected()) udpD.write( (uint8_t *) str, strlen(str));
}

void prikazHex( uint8_t* mem, size_t ukupno, size_t pocAdr) { // za prikaz binarnih podataka
//  if(cnf.nacin & UDP_DEBUG) 
//  else Serial.print(str);

  char *buf = (char*) malloc(1200); // bafer za 16 linija: adresa(4)+1+hex(48)+1+ ascii(16)+2
  char * pbuf;
  int x=0, y=0;
  
  for(size_t i=0; i<ukupno; ++i) {
    if( x == 0) {
      if(y == 0) { 
        memset(buf, ' ', 16*(7+49+18)); // brišemo bafer
        buf[0] = '\n';
        pbuf = buf+1;
      }
      sprintf(pbuf, "%05X ", pocAdr+i); pbuf += 6; 
    }
    sprintf(pbuf + 3*x, "%02X", mem[i]); // hex
    *(pbuf + 3*x+2) =' '; // uklanjamo terminirajuću nulu
    *(pbuf + 49 + x) = isprint(mem[i]) ? mem[i] : '.' ; // 0xb7, nije se prikazivala tačka u sredini
    if( ++x == 16) {
      x= 0;
      *(pbuf + 49 + 16) = '\n'; pbuf += 49+17;
      if(++y == 16) {
        *pbuf = 0; // terminiramo
        prikaz(buf); // šaljemo na Serial port i UDP
        y=0;
      }
    }
  }
  if(x || y) { *(pbuf + 49+x) = 0; prikaz(buf); } // terminiramo i šaljemo zadnji slog
  free(buf);
}

void prikaziFajl(char * ime, char kako) {
  char buf[260];
  File file = SPIFFS.open(ime, "r"); if(!file) prikaz("taj fajl ne postoji\n");
  size_t fileSize = file.size();
  size_t poc = 0;
  while ( poc < fileSize) {
    size_t procitano = file.readBytes(buf, 256);
    buf[procitano]=0;
    if(kako == 't') prikaz(buf);
    else prikazHex((uint8_t*)buf, procitano, poc);
    poc += procitano;
  }
}




void helpAlati(void) {
  char* help = R"(
  -- OS -- (imena fajlova počinju sa '/') 
ip     # prikazuje mac i ip adrese
ls     # lista fajlove u flešu 
cat fajl         # prikazuje sadržaj fajla
brisi fajl       # briše fajl
mv fajl novoIme  # mijenja ime fajla
mkdir folder     # pravi novi folder
rmdir folder     # uklanja folder
dump struc/fajl  # hex prikaz strukture ili fajla
spusti url fajl  # spusta url u fajl
digni fajl url   # PUT fajl na url
uzmi fajl struc  # prebacuje podatke iz fajla u strukturu
cuvaj struc fajl # čuva strukturu u fajl
otaprog url      # ubacuje program sa url u fleš
noti naslov opis # šalje notifikaciju

  -- UDP komande --
udpk url   # napravi konekciju - udpk mijanovic.ddns.net:3003
udp poruka # šalje poruku na konekciju
udpz       # zatvori konekciju

udps url poruka  # šalje poruku 
udpb port poruka # broadcast - udpe 3009 Svima na znanje!
udpl port        # sluša (listen)

test str1 str2   # poziva funkciju test(char*, char*)
)";
  prikaz(help);
}


bool uradiAlati(char * komanda) {
  char * prvi = strchr(komanda, ' ');  // prvi parametar
  char * drugi; // drugi parametar
  if(prvi != NULL) {
    * prvi = 0; // terminiramo komandu
    ++prvi; // ostatak teksta
    drugi = strchr(prvi, ' ');
    if(drugi != NULL) {
      *drugi = 0;
      ++drugi;
    }
  }
  if( ! strcmp(komanda, "ip")) prikaziMACiIP();
 
  else if( ! strcmp(komanda, "ls")) {
    char str[200];
    File root = SPIFFS.open("/");
    File file = root.openNextFile();
    prikaz("\nListing files and sizes:\n");
    while (file) {
      sprintf(str, "/%s - %zu bytes\n", file.name(), file.size());
      prikaz(str);
      file = root.openNextFile();
    }
    size_t uzeto=SPIFFS.usedBytes();
    size_t ukupno=SPIFFS.totalBytes();
    sprintf(str, "Used %u of %u total bytes. Free %u bytes.\n", uzeto, ukupno, ukupno-uzeto);
    prikaz(str);
  }
 
  else if( ! strcmp(komanda, "cat")) prikaziFajl(prvi, 't');

  else if( ! strcmp(komanda, "mv")) {
    if(SPIFFS.rename(prvi, drugi)) prikaz("zamjenjeno\n");
    else prikaz("ne može\n");
  }
  
  else if( ! strcmp(komanda, "brisi")) {
    if (SPIFFS.remove(prvi)) prikaz("pobrisan\n");
    else prikaz("ne može\n");
  }

  else if( ! strcmp(komanda, "mkdir")) {
    if (SPIFFS.mkdir(prvi)) prikaz("napravljen\n");
    else prikaz("ne može\n");
  }

  else if( ! strcmp(komanda, "rmdir")) {
    if (SPIFFS.rmdir(prvi)) prikaz("uklonjen\n");
    else prikaz("ne može\n");
  }

  else if( ! strcmp(komanda, "dump")) {
    size_t uku;
    uint8_t * mem = structAdresaUkupno(prvi, &uku);
    if(mem) prikazHex(mem, uku, 0);
    else prikaziFajl(prvi, 'h');
  }

  else if( ! strcmp(komanda, "spusti")) downloadFile(prvi, drugi); // url, imeFajla
  else if( ! strcmp(komanda, "digni")) uploadFile(prvi, drugi); // imeFajla, url
  else if( ! strcmp(komanda, "otaprog")) otaProg(prvi); // url
  else if( ! strcmp(komanda, "noti")) notifikacija(prvi, drugi); // naslov, opis
  
  else if( ! strcmp(komanda, "uzmi")) { // prvi = ime fajla, drugi = ime struct
    size_t uku;
    uint8_t * mem = structAdresaUkupno(drugi, &uku);
    if(mem) fajl2mem(prvi, mem, uku);
  }
  
  else if( ! strcmp(komanda, "cuvaj")) { // prvi = ime struct, drugi = ime fajla
    size_t uku;
    uint8_t * mem = structAdresaUkupno(prvi, &uku);
    if(mem) mem2fajl( mem, drugi, uku);
  }
  else if( ! strcmp(komanda, "udpk")) { nadjiAdrPort(prvi); udp.connect(adrPort.ip, adrPort.port); }
//  else if( ! strcmp(komanda, "udp")) { if(drugi) prvi[strlen(prvi)] = ' '; udp.write((uint8_t*) prvi, strlen(prvi) ); } // spajamo prvi i drugi dio i šaljemo zajedno
  else if( ! strcmp(komanda, "udp")) { if(drugi) *--drugi = ' '; udp.write((uint8_t*) prvi, strlen(prvi) ); } // spajamo prvi i drugi dio i šaljemo zajedno
  else if( ! strcmp(komanda, "udpz")) udp.close();
  
  else if( ! strcmp(komanda, "udps")) { nadjiAdrPort(prvi); udp.writeTo((uint8_t *) drugi, strlen(drugi), adrPort.ip, adrPort.port); } // prvi = url, drugi = poruka
  else if( ! strcmp(komanda, "udpb")) udp.broadcastTo(drugi, atoi(prvi)); // prvi = port, drugi = poruka
  else if( ! strcmp(komanda, "udpl")) udp.listen(atoi(prvi));

  else if( ! strcmp(komanda, "test")) test(prvi, drugi);
  else return false;
  return true;
}

void uradi(const char * tekst, int duzina) {
  // Preko udp može doći neterminiran tekst, pa moramo dodati 0. Zato moramo uzeti memoriju sa malloc
  if( duzina > 200) return; // ne interesuju nas komande duže od 200 znakova
  char komanda[201];

  memcpy(komanda, tekst, duzina);
  komanda[duzina] = '\0'; // terminiranje

  if( ! strcmp(komanda, "help") ) {
    helpAlati();
    helpEvirad();
  }
  else if(uradiAlati(komanda));
  else if(uradiEvirad(komanda));
  else if(citajPisi(komanda));
  else prikaz("Nepoznata komanda. Probajte 'help'.\n");
}

bool binarySearchFile(const char *filename, const uint8_t * searchKey, uint8_t *foundRecord) {
  File file = SPIFFS.open(filename, "rb");
  if (!file) {
    prikaz("File open failed");
    return false;
  }

  size_t recordSize = 16; // Size of each data record
  size_t numRecords = file.size() / recordSize; // Calculate the number of records

  size_t low = 0;
  size_t high = numRecords - 1;

  while (low <= high) {
    size_t mid = low + (high - low) / 2;
    size_t offset = mid * recordSize;

    file.seek(offset);
    file.readBytes(reinterpret_cast<char*>(foundRecord), recordSize);

    int cmp = memcmp(searchKey, foundRecord, 5);

    if (cmp == 0) {
      file.close();
      return true;
    } else if (cmp < 0) {
      if(mid==0) break;
      high = mid - 1;
    } else {
      low = mid + 1;
    }
  }

  file.close();
  return false;
}
