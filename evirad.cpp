#include "evirad.h"
#include "alati.h"

volatile uint32_t pulseTime1, pulseTime2;
volatile uint64_t card1, card2;
int16_t vrBrave[2][2]; // tekuća vremena rada 1. i 2. brave x 2 koraka
extern uint32_t sekunde; // vrijeme od početka rada
char novoPKC[3];
uint16_t doSlanja = 10; // mora biti globalna zbog 'odmah prenesi'
  

struct konfiguracija cnf;
struct asocijacija asoc[] = {
  { "jaSam", 's', (void*) cnf.jaSam},
  { "kljuc", 's', (void*) cnf.kljuc},
  { "opcija", 1, (void*) &cnf.opcija},
  { "verzijaKartica", 'c', (void*) &cnf.verzijaKartica},
  { "verzijaCnf", 'c', (void*) &cnf.verzijaCnf},
  { "wifiSsid", 's', (void*) cnf.wifiSsid},
  { "wifiPass", 's', (void*) cnf.wifiPass},
  { "mojSsid", 's', (void*) cnf.mojSsid},
  { "mojPass", 's', (void*) cnf.mojPass},
  { "serverUrl", 's', (void*) cnf.serverUrl},
  { "serverPort", 2, (void*) &cnf.serverPort},
  { "serverInterval", 2, (void*) &cnf.serverInterval},
  { "udpServer", 's', (void*) cnf.udpServer},
  { "udpPort", 2, (void*) &cnf.udpPort},
  { "udpInterval", 2, (void*) &cnf.udpInterval},
  { "nacin", 1, (void*) &cnf.nacin},
  { "pushServer", 's', (void*) cnf.pushServer},
  { "slusalac", 's', (void*) cnf.slusalac},
  { "pushMaks", 1, (void*) &cnf.pushMaks},
  { "frek[2][2]", 2, (void*) cnf.frek},
  { "ispuna[2][2]", 1, (void*) cnf.ispuna},
  { "x250ms[2][2]", 1, (void*) cnf.x250ms}
};


struct nepoznateKartice { // iako je 10 okteta, kompajler uzima 16 za jedan zapis
  uint32_t sekunde;
  uint8_t kartica[5];
  char citac;
} nk[MAX_NEPOZNATIH];
uint16_t nkXrd, nkXwr; // indeksi za upis u nk


struct dogadjaji { // 14 okteta, kompajler uzima 16
  uint32_t sekunde;
  uint8_t kartica[5];
  uint16_t idK;
  char citac;
  uint8_t pravo;
  char vrata;
} dg[MAX_DOGADJAJA];
uint16_t dgXrd, dgXwr; // indeks za upis u dg

//++++++++++++++ Define interrupt service routines ++++++++++++

void IRAM_ATTR isr_WG1_D0() {
  if (digitalRead(WG1_D0) || digitalRead(WG1_D0)) return; // filter koji eliminiše kratke smetnje
  card1 <<= 1;
  pulseTime1 = millis();
}
void IRAM_ATTR isr_WG1_D1() {
  if (digitalRead(WG1_D1) || digitalRead(WG1_D1)) return;
  card1 <<= 1;
  ++card1;
  pulseTime1 = millis();
}
void IRAM_ATTR isr_WG2_D0() {
  if (digitalRead(WG2_D0) || digitalRead(WG2_D0)) return;
  card2 <<= 1;
  pulseTime2 = millis();
}
void IRAM_ATTR isr_WG2_D1() {
  if (digitalRead(WG2_D1) || digitalRead(WG2_D1)) return;
  card2 <<= 1;
  ++card2;
  pulseTime2 = millis();
}

// +++++++++++++++ PWM za pogon brave ++++++++++++++

void postaviPwm(int pin, uint16_t frekv, uint8_t ispuna){ // frekv Hz, ispuna 0-255 (0-100%)
  const int LED_Channel = 0;
  const int resolution = 8; /*PWM resolution*/
  ledcSetup(LED_Channel, frekv, resolution);  /*PWM signal defined*/
  if(pin < 0) ledcDetachPin( -pin); else ledcAttachPin(pin, LED_Channel);
  ledcWrite(LED_Channel, ispuna);
}


// +++++++++ vraća pointer na struct podatke i uku (veličina u oktetima)

uint8_t * structAdresaUkupno(char * imeStruct, size_t * uku){
  if( ! strcmp(imeStruct, "cnf")) { *uku = sizeof(cnf); return (uint8_t*) &cnf; }
  if( ! strcmp(imeStruct, "nk")) { *uku = sizeof(nk); return (uint8_t*) &nk; }
  if( ! strcmp(imeStruct, "dg")) { *uku = sizeof(dg); return (uint8_t*) &dg; }
  return NULL;    
}



void eviradSetup(void) {
  pinMode(LED1, OUTPUT);
  pinMode(LED2, OUTPUT);
  pinMode(5, OUTPUT); digitalWrite(5, HIGH);

  pinMode(WG1_D0, INPUT);
  pinMode(WG1_D1, INPUT);
  pinMode(WG1_LED, OUTPUT);
  digitalWrite(WG1_LED, HIGH); // nula je aktivna
  pinMode(WG2_D0, INPUT);
  pinMode(WG2_D1, INPUT);
  pinMode(WG2_LED, OUTPUT);
  digitalWrite(WG2_LED, HIGH);
  pinMode(OUT1, OUTPUT);
  digitalWrite(OUT1, LOW); // jedinica je aktivna
  pinMode(OUT2, OUTPUT);
  digitalWrite(OUT2, LOW);

  // Attach interrupt for all 4 inputs on the board.
  attachInterrupt(WG1_D0, isr_WG1_D0, FALLING);
  attachInterrupt(WG1_D1, isr_WG1_D1, FALLING);
  attachInterrupt(WG2_D0, isr_WG2_D0, FALLING);
  attachInterrupt(WG2_D1, isr_WG2_D1, FALLING);

  if (!SPIFFS.begin(FORMAT_SPIFFS_IF_FAILED)) prikaz("\nFailed to mount SPIFFS\n");
}


void otvori(char vrata) {
  if( vrata <= ' ') { prikaz("nema pravo prolaska\n"); return; }

  char buf[200];
  sprintf(buf, "  otvaram vrata %c\n", vrata); prikaz(buf);

  char br = vrata - 'A'; // prelazimo na 0 i 1
  int pin = br ? OUT2 : OUT1;
  int16_t x250 = (uint16_t)(millis()/250); // četvrtinke
  
  vrBrave[br][0] = x250 + cnf.x250ms[br][0];
  vrBrave[br][1] = vrBrave[br][0] + cnf.x250ms[br][1];
  
  postaviPwm(pin, cnf.frek[br][0], cnf.ispuna[br][0]);
}



void logNepoznate(uint8_t* kartica, char citac) {
  
  nk[nkXwr].sekunde = sekunde;
  memcpy (nk[nkXwr].kartica, kartica, 5);
  nk[nkXwr].citac = citac;

  if(++nkXwr >= MAX_NEPOZNATIH) nkXwr=0;
}


char pravoProlaza(uint8_t pravo, char citac){
  //!!! napraviti prava po vremenu i mjestu čitanja
  //!!! uzeti u obzir mjesto koje pripada ovom WT32
  //!!! za sada, ako je pravo>0 vraćamo citac
  if(pravo ) return citac;
  return ' ';
}


void obradiKarticu(char citac, volatile uint64_t* kartica64) {
  uint8_t kartica[5];
  for(int i=0; i<5; ++i) kartica[i] = ((uint8_t *)kartica64)[4-i]; // preslažemo bajtove od višeg ka nižem
  *kartica64=0; // brišemo sve bitove da bi sledeća kartica bila ispravno pročitana sa W26 čitačem

  uint8_t osoba[17]; // 0-4 kartica, 5-6 id, 7 pravo, 8-15 opis
  bool nasao = binarySearchFile("/kartice", kartica, osoba);
  uint16_t idOsobe = osoba[5] + 256*osoba[6];
  osoba[16]=0; // terminiranje

  char vrata=' ';
  if(! nasao) {
    logNepoznate(kartica, citac);
    if( ! (cnf.nacin & UPISUJ_NEPOZNATE)) return; // ako ne treba da se upisuje u događaje, to je sve
    idOsobe = 0; osoba[7]=0; // id i pravo
    memcpy(osoba+8, "nepoznat", 8);
  } 
  else {
    vrata = pravoProlaza(osoba[7], citac);
    otvori(vrata);
  }

  uint8_t * k = kartica; // samo radi kraćeg imena
  char buf[200];
  sprintf(buf, "%us %02X%02X%02X%02X%02X id=%hu c=%c p=%hhu v=%c %.8s\n", sekunde, k[0],k[1],k[2],k[3],k[4], idOsobe, citac, osoba[7], vrata, osoba+8);
  prikaz(buf);

  dg[dgXwr].sekunde = sekunde;
  memcpy( dg[dgXwr].kartica, kartica, 5);
  dg[dgXwr].idK = idOsobe;
  dg[dgXwr].citac = citac;
  dg[dgXwr].pravo = osoba[7];
  dg[dgXwr].vrata = vrata;

  if(++dgXwr >= MAX_DOGADJAJA) dgXwr=0;

  if(dgXrd == dgXwr) {
    char imeFajla[20];
    sprintf(imeFajla, "/dg-%u", sekunde);
    prikaz("prebacujem događaje u fleš\n");
    mem2fajl( (uint8_t *)&dg, imeFajla, sizeof(dg));
  }
}


char * do100dogadjaja(char * p){
  p += sprintf(p, ",\"dogadjaji\":\"");
  int16_t maxD = dgXwr - dgXrd;
  if (maxD < 0) maxD += MAX_DOGADJAJA;
  if(maxD > 100) maxD=100;

  uint16_t s = dgXrd;
  for(int i=0; i< maxD; ++i) {
    uint8_t * k = dg[s].kartica;
    p += sprintf(p, "%u;%02X%02X%02X%02X%02X;%u;%c;%d;%c|", dg[s].sekunde, k[0], k[1], k[2], k[3], k[4],
      dg[s].idK, dg[s].citac, dg[s].pravo, dg[s].vrata);
    if(++s >= MAX_DOGADJAJA) s=0;
  }
  *(p-1) = '"'; // poslednje '|' mijenjamo u '"'
  return p;
}

char ucitajKartice(void) {
  char url[200];
  sprintf(url, "%s/?jaSam=%s&dajMi=kartice&bin", cnf.serverUrl, cnf.jaSam);
  return downloadFile(url, "/kartice");
}


char * izvuci( char * json, char * komanda) {
  char * poc, * p = json;

  p = strstr( p, komanda); if(p == NULL) return NULL; // nema komande
  p = strchr( p, ':'); if(p == NULL) return NULL; //nema : iza komande
  p = strchr( p, '"'); if(p == NULL) return NULL; //nema "
  poc = ++p; // ovdje počinje tražena vrijednost
  while( *p) {
    if(*p == '"') break; // kraj
    else if(*p == '\\') { if(* ++p == 0) return NULL;} // nezavršen escape
    ++p;
  }
  *p = 0; // terminiramo tekstualnu vrijednost
  return poc;
}
  

void javiSeServeru(void) {
  if(doSlanja) { --doSlanja; return; }
  doSlanja = cnf.serverInterval;
  if( nemaInternet()) return;

#define MAX_ODGOVOR 1200  
  char *odgovor = (char *)malloc(MAX_ODGOVOR +2);
  char *poruka = (char *)malloc(4000); // do 100 dogadjaja
  char * p = poruka;
  p += sprintf(p, "{\"sekunde\":%u, \"status\":\"%s vPKC:%c%c%c free:%u\"", sekunde, lokIP().c_str(),
    verzijaProg, cnf.verzijaKartica, cnf.verzijaCnf, ESP.getFreeHeap() ); 

  if(dgXwr != dgXrd) p = do100dogadjaja(p);
  sprintf(p, "}");
  Serial.println(poruka);
  posaljiPost(cnf.serverUrl, poruka, odgovor, MAX_ODGOVOR);
  Serial.println(odgovor);
  
  // Ako dobijemo komandu, treba da je izvršimo. Inače, reagujemo na razliku u verziji programa, kartica ili configa.
  // izvuci() izvlači vrijednost datog ključa tako što mijenja odgovor na tu vrijednost
  char * vredn;
  if( vredn = izvuci(odgovor, "komanda") ) uradi(vredn, strlen(vredn));
  else if( vredn = izvuci(odgovor, "vPKC") ) strcpy(novoPKC, vredn);
//  else if( ...
  free(poruka);
  free(odgovor);
}

void statistika(void){
  char buf[1000];
  sprintf(buf, "sada: %us, doSlanja=%d, swVerzija: %c, freeRam: %u bytes\n dgXwr=%u, dgXrd=%u, nkXwr=%u, nkXrd=%u\n",
    sekunde, doSlanja, verzijaProg, ESP.getFreeHeap(), dgXwr, dgXrd, nkXwr, nkXrd);
  prikaz(buf);
}


void obradiVerzije(void) {
  static uint16_t cekaj = 0;
  char url[200];
  
  if( cekaj ) { --cekaj; return; } // pravi pauzu između 2 pokušaja
  if(nemaInternet()) return;
  for(char i=0; i<4; ++i) if(*(vrBrave+i)) return; // nećemo dok brava radi

  if(novoPKC[0] != verzijaProg) {
    sprintf(url, "%s/?jaSam=%s&dajMi=otaProg", cnf.serverUrl, cnf.jaSam);
    otaProg( url);
    cekaj = 60; // ponovni pokušaj za minut
  }
  else if( novoPKC[1] != cnf.verzijaKartica) {
    if( ucitajKartice()) {
      cnf.verzijaKartica = novoPKC[1];
      mem2fajl((uint8_t*)&cnf, "/config", sizeof(cnf)); // čuvamo u flešu
    }
    else cekaj = 20; // ponovni pokušaj za 20 sekundi
  }
//  else if( novoPKC[2] != cnf.verzijaCnf) { // za sada nemamo
    
}
 

void eviradLoop(void) { // 8 puta u sekundi
  uint32_t vrPrag = millis()-10; // vremenski prag 10ms za završeno čitanje kartice
  if(card1 && pulseTime1 && vrPrag > pulseTime1) { obradiKarticu('A', &card1); pulseTime1=0; }
  if(card2 && pulseTime2 && vrPrag > pulseTime2) { obradiKarticu('B', &card2); pulseTime2=0; }
  digitalWrite(LED2, !digitalRead(LED2));

  // upravljanje bravom
  // otvori(vrata) postavlja vrBrave[][]
  
  bool braveRade = false;
  int16_t x250 = (int16_t)(millis()/256);
  for(int brava=0; brava<2; ++brava){
    int pin = brava ? OUT2 : OUT1;

    if(vrBrave[brava][1] == 0) continue; // brava ne radi
    if(vrBrave[brava][0]) { // u toku je 1. korak - trešenje brave
      braveRade = true;
      if( x250 - vrBrave[brava][0] > 0) { // prelazimo na 2. korak
        vrBrave[brava][0];
        postaviPwm(pin, cnf.frek[brava][1], cnf.ispuna[brava][1]);
      }
    }
    else { // 2. korak * mala struja držanja
      if( x250 - vrBrave[brava][1] > 0) { // isteklo vrijeme rada brave
        vrBrave[brava][1] = 0; 
        if(cnf.ispuna[brava][1] < 128) postaviPwm(pin, cnf.frek[brava][1], 0);
        else postaviPwm(pin, cnf.frek[brava][1], 255);
      }
      else braveRade = true;
    }
  }
  if( ! braveRade) obradiVerzije();
}

//******************* za interaktivni rad - citanje promjenljivih i podešavanje cnf ***************

bool citajPisi(char * ime) {
  int16_t x1=-1, x2=-1;
  char * dim=NULL;
  char * novo=NULL;
  char rezultat[50];
  
  char * jedn = strchr(ime, '='); if(jedn) { *jedn=0; novo = jedn+1; }
  dim=strstr(ime, "[");
  if(dim) {
    *dim=0;
    ++dim;
    sscanf( dim, " %hd%*[^0-9]%hd", &x1, &x2);
  }
  // ime:"ispuna[1][2]=56" => ime:"ispuna", x1:1, x2:2, novo:"56"

  int duz=strlen(ime);

  int i, uku = sizeof(asoc)/sizeof(asoc[0]);
  for( i=0; i<uku; ++i) {
    if( strncmp(ime, asoc[i].ime, duz)) continue;
    int t = asoc[i].tip;

    if(t=='s') { // string
      char * p = (char*) asoc[i].pok;
      if(novo) strcpy( p, novo);
      sprintf(rezultat, "%s\n", p);
    }
    else if(t=='c') {
      char * p = (char*) asoc[i].pok;
      if(novo) *p = novo[0];
      sprintf(rezultat, "%c\n", *p);
    }
    if(t==1) {
      uint8_t * p = (uint8_t *) asoc[i].pok + ( x2>=0 ? 2*x1+x2 : ( x1>=0 ? x1 : 0));
      if(novo) sscanf(novo, "%hhu", p);
      sprintf(rezultat, "%hhu\n", *p);
    }
    if(t==2) {
      uint16_t * p = (uint16_t *) asoc[i].pok + ( x2>=0 ? 2*x1+x2 : ( x1>=0 ? x1 : 0)) ;
      if(novo) sscanf(novo, "%hu", p);
      sprintf(rezultat, "%hu\n", *p);
    }
    // if(t==-1) ... t<0 - predviđeni za signed +/- brojeve
    break;
  }
  if( i == uku) return false;
  prikaz(rezultat); return true;
}


void citajSveCnf(void) {
  int uku = sizeof(asoc)/sizeof(asoc[0]);
  char zajedno[1200];
  char * pzaj = zajedno;

  for(int i=0; i<uku; ++i) {
    int16_t x1=-1, x2=-1;
    char ime[50];
    char * dim=NULL;

    strcpy (ime, asoc[i].ime);
    dim=strchr(ime, '[');
    if(dim) sscanf( ++dim, " %hd%*[^0-9]%hd", &x1, &x2);

    int t = asoc[i].tip;
    pzaj += sprintf(pzaj, "\n%s: ", ime);
    if( t == 's') { pzaj += sprintf(pzaj, "%s", (char*)asoc[i].pok); continue; }
    if( t == 'c') { pzaj += sprintf(pzaj, "%c", *(char*)asoc[i].pok); continue; }
    int maks = x1<0 ? 1 : ( x2<0 ? x1 : x1*x2);
    for(int j=0; j<maks; ++j) {
      if( t == 1) pzaj += sprintf(pzaj, "%hhu ", *((uint8_t*)asoc[i].pok + j));
      if( t == 2) pzaj += sprintf(pzaj, "%hu ", *((uint16_t*)asoc[i].pok + j));
    }
  }
  prikaz(zajedno);
}


void helpEvirad(void) {
  char* help = R"(
  -- EVIRAD --
cnf  # prikazuje configuracione podatke
ka  # prikazuje kartice
uk  # komanda za preuzimanje kartica iz server baze
nk  # prikazuje nepoznate kartice
dg  # prikazuje dogadjaje
op  # odmah prenesi događaje u bazu podataka
ov  # otvori vrata 1
ow  # otvori vrata 2
frekv[1][0]  # prikazuje samo jednu vrijednost iz cnf
ispuna[0][0]=100  # modifikuje cnf
stat  # statusni i ostali podaci

)";
  prikaz(help);
}



bool uradiEvirad(char* komanda) {
  if( ! strcmp(komanda, "cnf")) {
    citajSveCnf();
  }
  else if( ! strcmp(komanda, "ka")) {
    char buf[16*16];
    uint16_t procitano;
    
    File file = SPIFFS.open("/kartice", "rb"); // Open the file in read binary mode
    while( procitano = file.readBytes( (char*) buf, sizeof(buf))){ // ispisivaćemo po 16 kartica
      char zaSlanje[1200];
      char * p = zaSlanje;
      uint8_t * pb = (uint8_t*) buf; // vraćamo pb na početak
    
      for(int i=0; i< procitano/16; ++i) {
        p += sprintf( p, "%02X%02X%02X%02X%02X %5hu %hhu %.8s\n", pb[0], pb[1], pb[2], pb[3], pb[4], pb[5]+256*pb[6], pb[7], (char*)(pb+8));
        // %.8s stampa samo 8 slova bez obzira koliko je string dugačak. Tačka je za "precision"
        pb += 16;
      }
      prikaz(zaSlanje);
    }      
  }
  else if( ! strcmp(komanda, "dg")) {
    char buf[200]; // !!!??? napraviti kao za kartice, da se odrađuje po 16 ili više zapisa
    
    for( uint16_t i = dgXrd; i != dgXwr; ) {
      uint8_t * k = dg[i].kartica;
      sprintf(buf,"%us k=%02X%02X%02X%02X%02X id=%-5hu c=%c p=%d v=%c\n", dg[i].sekunde, k[0],k[1],k[2],k[3],k[4], dg[i].idK, dg[i].citac, dg[i].pravo, dg[i].vrata);
      if( ++i >= MAX_DOGADJAJA) i=0;
      prikaz(buf);
    }
  }
  else if( ! strcmp(komanda, "nk")) {
    char buf[200];
    for(uint16_t i=nkXrd; i != nkXwr; ) {
      uint8_t * k = nk[i].kartica;
      sprintf(buf,"%us k=%02X%02X%02X%02X%02X c=%c\n", nk[i].sekunde, k[0], k[1], k[2], k[3], k[4], nk[i].citac);
      if( ++i >= MAX_DOGADJAJA) i=0;
      prikaz(buf);
   }
  }
  else if( ! strcmp(komanda, "uk")) ucitajKartice();
  else if( ! strcmp(komanda, "op")) doSlanja=1;
  else if( ! strcmp(komanda, "ov")) otvori('A');
  else if( ! strcmp(komanda, "ow")) otvori('B');
  else if( ! strcmp(komanda, "stat")) statistika();
  else return false;
  return true;
}
