/*
 * ══════════════════════════════════════════════════════════════
 *  AH & EPERRET — DRONE RX V3.5
 *  M5Stack CoreS3 — M5Unified
 *
 *  V3.5 :
 *    - USB data → BLE (Web Bluetooth), Serial debug conservé
 *    - SSID/pwd drone.local, mDNS, DHCP maison (hostname client)
 *    - Scan: DETECTION 1-14, CLASSIC 1,6,11, TRACKING canaux actifs
 *    - Waterfall RSSI réel (toutes trames radio)
 *    - Dwell 1s/canal
 *
 *  Architecture :
 *    WiFi AP  = page HTML (drone.local), pas de scan
 *    BLE      = données drone bidirectionnel, scan 100% entre bursts
 *
 *  Protocoles (4) : FR / ODID / DJI / PAR
 *  Beep mélodique uniquement sur nouveau drone.
 *
 *  Board  : ESP32S3 Dev Module
 *  Lib    : M5Unified, ESPmDNS, BLE (built-in)
 *  USB CDC: Enabled / PSRAM: OPI / Flash: 16MB QIO
 *
 *  Fichiers : drone_rx_v3.ino + drone_page.h (même dossier)
 * ══════════════════════════════════════════════════════════════
 */

#include <M5Unified.h>
#include <WiFi.h>
#include <WiFiServer.h>
#include <WiFiClient.h>
#include "esp_wifi.h"
#include "esp_wifi_types.h"
#include "esp_netif.h"
#include "mbedtls/sha1.h"
#include "mbedtls/base64.h"
#include "drone_page.h"
#include <Preferences.h>
#include <WiFiUdp.h>
#include <ESPmDNS.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>


// ══════════════════════════════════════════════════════════════
//  CONFIGURATION
// ══════════════════════════════════════════════════════════════

// ── WiFi Access Point ──
#define AP_SSID         "drone.local"
#define AP_PASS         "drone.local"
#define AP_CHANNEL      13
#define WS_PORT         81
#define HTTP_PORT       80
#define MAX_WS_CLIENTS  4

// ── Batterie ──
#define VBAT_SHUTDOWN   3300
#define VBAT_SAMPLES    5
#define VBAT_MIN_VALID  1000
#define VBAT_CHECK_MS   3000

// ── Scan canaux ──
#define DWELL_BASE_MS   1000
#define DWELL_JITTER_MS 0

// ── BLE ──
#define BLE_DEVICE_NAME   "drone.local"
#define BLE_SERVICE_UUID  "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"   // Nordic UART
#define BLE_TX_CHAR_UUID  "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"   // notify → phone
#define BLE_RX_CHAR_UUID  "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"   // write ← phone
#define BLE_QUEUE_SIZE    8
#define BLE_MSG_SIZE      300
#define BLE_IDLE_SLEEP_MS 300000  // 5 min sans BLE → écran veille


// ══════════════════════════════════════════════════════════════
//  PROTOCOLES
// ══════════════════════════════════════════════════════════════

#define PROTO_FR    0
#define PROTO_ODID  1
#define PROTO_DJI   2
#define PROTO_PAR   3
#define NUM_PROTOS  4

// ── V3.3 WATERFALL : proto fictif pour Unknown (waterfall only) ──
#define PROTO_UNK   4

static const char* PROTO_NAMES[] = { "FR", "ODID", "DJI", "PAR" };

static const uint8_t OUI_FR[]   = { 0x6A, 0x5C, 0x35 };
static const uint8_t OUI_ODID[] = { 0xFA, 0x0B, 0xBC };
static const uint8_t OUI_DJI[]  = { 0x26, 0x37, 0x12 };
static const uint8_t OUI_PAR[]  = { 0x90, 0x3A, 0xE6 };


// ══════════════════════════════════════════════════════════════
//  COULEURS
// ══════════════════════════════════════════════════════════════

#define C_BG   TFT_BLACK
#define C_HDR  0x1926     // bleu sombre
#define C_GRID 0x1082     // gris très sombre

static const uint16_t PROTO_COLORS[] = {
  TFT_CYAN,      // FR
  TFT_GREEN,     // ODID
  TFT_ORANGE,    // DJI
  TFT_MAGENTA,   // PAR
  0x0617         // ── V3.3 : UNK = cyan sombre ──
};

// ── V3.3 WATERFALL : couleurs RSSI fond, mapping direct ──
static uint16_t rssiToColor(int8_t rssi) {
  if (rssi >= -50) return 0x4000;  // rouge sombre (fort = bruyant)
  if (rssi >= -65) return 0x4200;  // orange sombre
  if (rssi >= -80) return 0x4220;  // jaune sombre
  return 0x0200;                   // vert sombre (faible = calme)
}


// ══════════════════════════════════════════════════════════════
//  STRUCTURES WiFi
// ══════════════════════════════════════════════════════════════

typedef struct {
  uint16_t frame_ctrl;
  uint16_t duration;
  uint8_t  addr1[6];
  uint8_t  addr2[6];
  uint8_t  addr3[6];
  uint16_t seq_ctrl;
} __attribute__((packed)) wifi_mgmt_hdr_t;


// ══════════════════════════════════════════════════════════════
//  FILE DE RÉCEPTION
// ══════════════════════════════════════════════════════════════

#define RX_BUF_SIZE   400

typedef struct {
  uint8_t  proto;
  uint8_t  mac[6];
  int8_t   rssi;
  uint8_t  channel;
  uint8_t  payload[RX_BUF_SIZE];
  uint16_t payloadLen;
  bool     ready;
} rx_packet_t;

#define RX_QUEUE_SIZE 8
volatile rx_packet_t rxQueue[RX_QUEUE_SIZE];
volatile uint8_t rxHead = 0;


// ══════════════════════════════════════════════════════════════
//  V3.3 WATERFALL : file UNK légère (canal + rssi seulement)
// ══════════════════════════════════════════════════════════════

#define UNK_QUEUE_SIZE 16

typedef struct {
  uint8_t channel;
  int8_t  rssi;
  bool    ready;
} unk_event_t;

volatile unk_event_t unkQueue[UNK_QUEUE_SIZE];
volatile uint8_t unkHead = 0;

// ── V3.3 : dernier RSSI vu par canal (maj par chaque beacon) ──
volatile int8_t channelRSSI[15];  // index 1–14, 0 inutilisé


// ══════════════════════════════════════════════════════════════
//  LISTE DRONES
// ══════════════════════════════════════════════════════════════

#define MAX_DRONES 16

typedef struct {
  uint8_t  mac[6];
  uint8_t  proto;
  uint8_t  channel;
  int8_t   rssi;
  uint32_t lastSeen;
  bool     active;
} drone_entry_t;

drone_entry_t droneList[MAX_DRONES];
int droneCount    = 0;
int selectedDrone = -1;


// ══════════════════════════════════════════════════════════════
//  MODES DE SCAN
// ══════════════════════════════════════════════════════════════

enum ScanMode {
  MODE_DETECTION,
  MODE_CLASSIC,
  MODE_TRACKING
};

ScanMode scanMode = MODE_DETECTION;
const char* SCAN_MODE_NAMES[] = { "DETECTION", "CLASSIC", "TRACKING" };

// Canaux actifs détectés (partagé Détection/Classic, vidé au démarrage)
bool activeChannels[15];  // index 1–14
uint8_t activeChCount = 0;

uint8_t countActiveChannels() {
  uint8_t n = 0;
  for (uint8_t i = 1; i <= 14; i++) if (activeChannels[i]) n++;
  return n;
}

// Séquence Classic : canaux attendus
static const uint8_t SEQ_CLASSIC[] = { 1, 6, 11 };
#define SEQ_CLASSIC_LEN 3


// ══════════════════════════════════════════════════════════════
//  TRANSPORT
// ══════════════════════════════════════════════════════════════

enum Transport {
  XPORT_NONE,
  XPORT_BLE,
  XPORT_WIFI
};

Transport activeTransport = XPORT_NONE;
bool wifiAPActive = false;


// ══════════════════════════════════════════════════════════════
//  ÉTAT APPLICATION
// ══════════════════════════════════════════════════════════════

enum AppState {
  STATE_SPLASH,
  STATE_SCAN,
  STATE_PORTAL,     // ── V3.4 : monitoring connexion client ──
  STATE_HELP,
  STATE_MODE_SELECT,
  STATE_SNIFF,
  STATE_KEYBOARD,    // ── V3.3 TX : saisie ID exploitant ──
  STATE_TX_EMIT      // ── V3.3 TX : emission beacon FR ──
};

AppState appState = STATE_SPLASH;

// ── V3.4 PORTAL : étapes de connexion client ──
#define PORTAL_WAIT     0   // en attente de client WiFi
#define PORTAL_CLIENT   1   // client connecté (IP obtenue)
#define PORTAL_DNS      2   // requête DNS reçue
#define PORTAL_REDIRECT 3   // 302 redirect envoyé
#define PORTAL_SERVED   4   // page HTML servie

uint8_t  portalStep = PORTAL_WAIT;
char     portalClientIP[20] = "";
char     portalDNSHost[40] = "";
uint8_t  portalLastStaCount = 0;
uint32_t portalPageSize = 0;


// ══════════════════════════════════════════════════════════════
//  WATERFALL SDR — Dimensions écran
// ══════════════════════════════════════════════════════════════
//
//  Layout écran sniff (320×240) :
//    Y 0–6    : Titre mode
//    Y 26–34  : Compteurs protocoles
//    Y 42–54  : Info transport
//    Y 56–68  : Dernier événement
//    Y 69     : Labels canaux (1–14)
//    Y 80–199 : Zone waterfall (120 lignes × 14 colonnes)
//    Y 200–213: Boutons touch [PWR OFF] [MODE]
//    Y 214–225: (espace)
//    Y 226–240: Barre info (CH, Total, Transport, Batterie)
//
// ══════════════════════════════════════════════════════════════

#define WF_X       4           // Début X du waterfall
#define WF_Y       80          // Début Y
#define WF_H       118         // Hauteur (lignes de temps)
#define WF_COLS    14          // 14 canaux
#define WF_COL_W   22         // Largeur par canal (22×14 = 308)

uint8_t  wfLine = 0;          // Ligne courante (curseur temps)
uint32_t lastWfTick = 0;

// ── Boutons touch sur écran sniff ──
#define BTN_Y       200         // Y des boutons
#define BTN_H       16          // Hauteur boutons
#define BTN_PWR_X   10          // PWR OFF : x 10–102
#define BTN_PWR_W   92
#define BTN_RBT_X   106         // REBOOT : x 106–198
#define BTN_RBT_W   92
#define BTN_MODE_X  202         // MODE : x 202–310
#define BTN_MODE_W  108


// ══════════════════════════════════════════════════════════════
//  VARIABLES GLOBALES
// ══════════════════════════════════════════════════════════════

WiFiServer wsServer(WS_PORT);
WiFiServer httpServer(HTTP_PORT);
WiFiClient wsClients[MAX_WS_CLIENTS];
bool       wsConnected[MAX_WS_CLIENTS];

// ── CAPTIVE PORTAL DNS ──
WiFiUDP dnsServer;
#define DNS_PORT 53

// ══════════════════════════════════════════════════════════════
//  SERVEUR DHCP MAISON — option 12 = hostname client
// ══════════════════════════════════════════════════════════════

// Forward declaration (définie dans la section ÉCRANS)
void drawPortalStep(uint8_t step);

WiFiUDP dhcpUDP;
#define DHCP_PORT 67
#define DHCP_CLIENT_PORT 68
#define DHCP_POOL_START 2          // 192.168.4.2
#define DHCP_LEASE_TIME 3600       // 1h en secondes
#define MAX_DHCP_CLIENTS MAX_WS_CLIENTS

typedef struct {
  uint8_t  mac[6];
  uint8_t  ipLast;               // dernier octet IP (192.168.4.X)
  char     hostname[32];
  bool     active;
  uint32_t leaseStart;
} dhcp_client_t;

dhcp_client_t dhcpClients[MAX_DHCP_CLIENTS];

int findDHCPClient(const uint8_t* mac) {
  for (int i = 0; i < MAX_DHCP_CLIENTS; i++)
    if (dhcpClients[i].active && memcmp(dhcpClients[i].mac, mac, 6) == 0) return i;
  return -1;
}

int allocDHCPClient(const uint8_t* mac, const char* name) {
  // Chercher un slot libre
  for (int i = 0; i < MAX_DHCP_CLIENTS; i++) {
    if (!dhcpClients[i].active) {
      memcpy(dhcpClients[i].mac, mac, 6);
      dhcpClients[i].ipLast = DHCP_POOL_START + i;
      strncpy(dhcpClients[i].hostname, name, 31);
      dhcpClients[i].hostname[31] = '\0';
      dhcpClients[i].active = true;
      dhcpClients[i].leaseStart = millis();
      return i;
    }
  }
  return -1;
}

const char* getDHCPClientName(int idx) {
  if (idx < 0 || idx >= MAX_DHCP_CLIENTS || !dhcpClients[idx].active) return "?";
  return dhcpClients[idx].hostname[0] ? dhcpClients[idx].hostname : "?";
}

void clearDHCPClients() {
  for (int i = 0; i < MAX_DHCP_CLIENTS; i++) dhcpClients[i].active = false;
}

// Envoyer une réponse DHCP (OFFER type=2 ou ACK type=5)
void sendDHCPReply(const uint8_t* req, int reqLen,
                   const uint8_t* clientMAC, uint8_t ipLast,
                   uint32_t xid, uint8_t msgType) {
  uint8_t resp[300];
  memset(resp, 0, sizeof(resp));

  resp[0]  = 2;    // op = BOOTREPLY
  resp[1]  = 1;    // htype = Ethernet
  resp[2]  = 6;    // hlen = 6
  // XID (bytes 4-7)
  resp[4]  = (xid >> 24) & 0xFF;
  resp[5]  = (xid >> 16) & 0xFF;
  resp[6]  = (xid >> 8)  & 0xFF;
  resp[7]  = xid & 0xFF;
  // yiaddr = IP assignée (bytes 16-19)
  resp[16] = 192; resp[17] = 168; resp[18] = 4; resp[19] = ipLast;
  // siaddr = IP serveur (bytes 20-23)
  resp[20] = 192; resp[21] = 168; resp[22] = 4; resp[23] = 1;
  // chaddr = MAC client (bytes 28-33)
  memcpy(resp + 28, clientMAC, 6);
  // Magic cookie (bytes 236-239)
  resp[236] = 0x63; resp[237] = 0x82; resp[238] = 0x53; resp[239] = 0x63;

  // Options à partir de 240
  int p = 240;
  // 53 = DHCP Message Type
  resp[p++] = 53; resp[p++] = 1; resp[p++] = msgType;
  // 54 = Server Identifier
  resp[p++] = 54; resp[p++] = 4;
  resp[p++] = 192; resp[p++] = 168; resp[p++] = 4; resp[p++] = 1;
  // 51 = Lease Time
  resp[p++] = 51; resp[p++] = 4;
  resp[p++] = (DHCP_LEASE_TIME >> 24); resp[p++] = (DHCP_LEASE_TIME >> 16) & 0xFF;
  resp[p++] = (DHCP_LEASE_TIME >> 8) & 0xFF;  resp[p++] = DHCP_LEASE_TIME & 0xFF;
  // 1 = Subnet Mask
  resp[p++] = 1; resp[p++] = 4;
  resp[p++] = 255; resp[p++] = 255; resp[p++] = 255; resp[p++] = 0;
  // 3 = Router
  resp[p++] = 3; resp[p++] = 4;
  resp[p++] = 192; resp[p++] = 168; resp[p++] = 4; resp[p++] = 1;
  // 6 = DNS Server
  resp[p++] = 6; resp[p++] = 4;
  resp[p++] = 192; resp[p++] = 168; resp[p++] = 4; resp[p++] = 1;
  // 255 = End
  resp[p++] = 255;

  // Envoi forcé sur le sous-réseau AP pour éviter la perte de paquet en AP_STA
  dhcpUDP.beginPacket(IPAddress(192, 168, 4, 255), DHCP_CLIENT_PORT);
  dhcpUDP.write(resp, p);
  dhcpUDP.endPacket();
}

void handleDHCP() {
  int pktLen = dhcpUDP.parsePacket();
  if (pktLen < 240) return;

  uint8_t buf[576];
  int n = dhcpUDP.read(buf, sizeof(buf));
  if (n < 240 || buf[0] != 1) return;  // BOOTREQUEST only

  // XID
  uint32_t xid = ((uint32_t)buf[4] << 24) | ((uint32_t)buf[5] << 16) |
                 ((uint32_t)buf[6] << 8)  | buf[7];
  // Client MAC
  uint8_t cMAC[6];
  memcpy(cMAC, buf + 28, 6);

  // Magic cookie check
  if (buf[236] != 0x63 || buf[237] != 0x82 ||
      buf[238] != 0x53 || buf[239] != 0x63) return;

  // Parse options
  uint8_t msgType = 0;
  char hostname[32] = "";
  int pos = 240;
  while (pos < n) {
    uint8_t opt = buf[pos++];
    if (opt == 255) break;
    if (opt == 0) continue;
    if (pos >= n) break;
    uint8_t ol = buf[pos++];
    if (pos + ol > n) break;
    if (opt == 53 && ol >= 1) msgType = buf[pos];
    if (opt == 12) {
      int hl = (ol < 31) ? ol : 31;
      memcpy(hostname, buf + pos, hl);
      hostname[hl] = '\0';
    }
    pos += ol;
  }

  if (msgType == 1) {
    // DISCOVER → trouver ou allouer un client
    int idx = findDHCPClient(cMAC);
    if (idx < 0) idx = allocDHCPClient(cMAC, hostname);
    if (idx < 0) return;  // pool plein
    if (hostname[0]) strncpy(dhcpClients[idx].hostname, hostname, 31);
    sendDHCPReply(buf, n, cMAC, dhcpClients[idx].ipLast, xid, 2);
    Serial.printf("[DHCP] OFFER → 192.168.4.%d (%s)\n",
      dhcpClients[idx].ipLast, getDHCPClientName(idx));
  }
  else if (msgType == 3) {
    // REQUEST → ACK
    int idx = findDHCPClient(cMAC);
    if (idx < 0) idx = allocDHCPClient(cMAC, hostname);
    if (idx < 0) return;
    if (hostname[0]) strncpy(dhcpClients[idx].hostname, hostname, 31);
    dhcpClients[idx].leaseStart = millis();
    sendDHCPReply(buf, n, cMAC, dhcpClients[idx].ipLast, xid, 5);
    Serial.printf("[DHCP] ACK → 192.168.4.%d  \"%s\"\n",
      dhcpClients[idx].ipLast, getDHCPClientName(idx));

    // CORRECTION Gemini : On affiche l'IP ET on récupère le hostname mémorisé
    snprintf(portalClientIP, sizeof(portalClientIP), "192.168.4.%d (%s)",
      dhcpClients[idx].ipLast, getDHCPClientName(idx));
    if (portalStep < PORTAL_CLIENT) {
      portalStep = PORTAL_CLIENT;
      if (appState == STATE_PORTAL) drawPortalStep(PORTAL_CLIENT);
    }
  }
}

uint8_t    currentChannel     = 1;
uint32_t   lastChannelSwitch  = 0;
uint8_t    scanChIndex        = 0;

uint32_t   totalCount         = 0;
uint32_t   protoCounts[NUM_PROTOS] = { 0, 0, 0, 0 };
bool       screenAsleep = false;

char       hexBuffer[RX_BUF_SIZE * 2 + 1];
char       jsonBuffer[RX_BUF_SIZE * 2 + 128];

volatile bool    newBeacon   = false;
volatile uint8_t lastProto   = 0;

uint16_t  vbatHistory[VBAT_SAMPLES];
uint8_t   vbatIndex   = 0;
bool      vbatFilled  = false;
uint32_t  lastVbatCheck = 0;

char      serialInBuf[256];
uint8_t   serialInLen      = 0;

// ── V3.3 TX : structure télémétrie complète ──
Preferences nvs;

typedef struct {
  char     id[24];             // ID exploitant Alpha Tango (ex: FRAokesq0o9db0q6-cf8)
  bool     idValid;
  double   lat, lon, alt;      // position courante (centre carte)
  double   homeLat, homeLon;   // position décollage (fixée au startTx)
  double   homeAlt;
  uint8_t  speed;              // m/s (0-254)
  uint16_t heading;            // 0-360 deg
  int8_t   vertSpeed;          // val*0.5 m/s (ODID encoding)
  uint8_t  status;             // 0=Undeclared 1=Ground 2=Airborne 3=Emergency
  char     selfDesc[24];       // description opération
  uint8_t  uaType;             // 2=Helicopter/Multirotor
  uint8_t  idType;             // 1=SerialANSI
  uint8_t  euCategory;         // 1=Open
  uint8_t  euClass;            // 2=C1
} telemetry_t;

telemetry_t telem = {
  "FRAokesq0o9db0q6-cf8", true,  // id, idValid
  4.9371, -52.3258, 30,          // lat, lon, alt (Cayenne centre, 30m AGL)
  4.9371, -52.3258, 0,           // homeLat, homeLon, homeAlt (sol)
  2, 90, 0,                      // speed 2m/s, heading 90deg Est, vertSpeed 0
  2,                              // status = Airborne
  "Recreational flight",          // selfDesc
  2,                              // uaType = Helicopter/Multi
  1,                              // idType = SerialANSI
  1,                              // euCategory = Open
  2                               // euClass = C1
};

uint8_t   txMAC[6];
uint8_t   beaconBuf[256];
uint16_t  beaconLen         = 0;
uint32_t  txCount           = 0;
uint16_t  txSeqNum          = 0;
uint32_t  lastTxBeacon      = 0;

#define KB_MAX_LEN 20
char      kbInput[KB_MAX_LEN + 1] = "";
uint8_t   kbLen = 0;
bool      kbShift = true;  // ── V3.3 : demarre en MAJ (FRA...) ──
bool      soundEnabled = true;  // ── V3.4 : controle son depuis HTML ──


// ══════════════════════════════════════════════════════════════
//  OUI HELPERS
// ══════════════════════════════════════════════════════════════

static inline bool ouiMatch(const uint8_t* a, const uint8_t* b) {
  return (a[0] == b[0]) && (a[1] == b[1]) && (a[2] == b[2]);
}


// ══════════════════════════════════════════════════════════════
//  BATTERIE
// ══════════════════════════════════════════════════════════════

void vbatAdd(uint16_t mv) {
  if (mv < VBAT_MIN_VALID) return;
  vbatHistory[vbatIndex] = mv;
  vbatIndex = (vbatIndex + 1) % VBAT_SAMPLES;
  if (vbatIndex == 0) vbatFilled = true;
}

uint16_t vbatAvg() {
  uint8_t count = vbatFilled ? VBAT_SAMPLES : vbatIndex;
  if (count == 0) return 4200;
  uint32_t sum = 0;
  for (uint8_t i = 0; i < count; i++) sum += vbatHistory[i];
  return sum / count;
}

void checkBattery() {
  if (millis() - lastVbatCheck < VBAT_CHECK_MS) return;
  lastVbatCheck = millis();

  vbatAdd(M5.Power.getBatteryVoltage());
  uint16_t avg = vbatAvg();

  if (avg < VBAT_SHUTDOWN && vbatFilled) {
    Serial.printf("[PWR] %dmV SHUTDOWN\n", avg);
    M5.Display.fillScreen(TFT_RED);
    M5.Display.setTextSize(2);
    M5.Display.setTextColor(TFT_WHITE, TFT_RED);
    M5.Display.setCursor(10, 60);
    M5.Display.printf("BAT %d mV\nEXTINCTION", avg);
    M5.Speaker.tone(400, 500); delay(600);
    M5.Speaker.tone(300, 500); delay(600);
    M5.Speaker.stop();
    esp_wifi_set_promiscuous(false);
    WiFi.mode(WIFI_OFF);
    delay(500);
    M5.Power.powerOff();
    esp_deep_sleep_start();
    while (1) delay(1000);
  }
}


// ══════════════════════════════════════════════════════════════
//  TRANSPORT WiFi / BLE
// ══════════════════════════════════════════════════════════════
void updateTransportDisplay();
void savePilotID();

void checkSerialInput() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (serialInLen > 0) {
        serialInBuf[serialInLen] = '\0';
        // ── Reception JSON depuis BLE/Serial debug ──
        if (serialInBuf[0] == '{') {
          char* gp = strstr(serialInBuf, "\"g\":[");
          if (gp) {
            gp += 5;
            char* end;
            double la = strtod(gp, &end);
            if (end != gp && *end == ',') {
              gp = end + 1;
              double lo = strtod(gp, &end);
              if (end != gp) {
                telem.lat = la;
                telem.lon = lo;
                gp = (*end == ',') ? end + 1 : end;
                telem.alt = strtod(gp, NULL);
                Serial.printf("[GPS] %.5f,%.5f,%.0f\n", telem.lat, telem.lon, telem.alt);
              }
            }
          }
          // ── V3.4 : son on/off depuis HTML ──
          char* sp = strstr(serialInBuf, "\"snd\":");
          if (sp) {
            sp += 6;
            soundEnabled = (*sp == '1');
            M5.Speaker.setVolume(soundEnabled ? 128 : 0);
            Serial.printf("[SND] %s\n", soundEnabled ? "ON" : "OFF");
          }
          // ── V3.4 : reception config TX depuis HTML ──
          char* tp = strstr(serialInBuf, "\"tx\":{");
          if (tp) {
            char* f;
            f = strstr(tp, "\"id\":\"");
            if (f) {
              f += 6; char* e = strchr(f, '"');
              if (e && (e-f) >= 3 && (e-f) <= KB_MAX_LEN) {
                memset(telem.id, 0, sizeof(telem.id));
                memcpy(telem.id, f, e-f);
                telem.idValid = true;
                savePilotID();
              }
            }
            f = strstr(tp, "\"uaType\":");
            if (f) { f += 9; telem.uaType = (uint8_t)atoi(f); }
            f = strstr(tp, "\"idType\":");
            if (f) { f += 9; telem.idType = (uint8_t)atoi(f); }
            f = strstr(tp, "\"euCat\":");
            if (f) { f += 8; telem.euCategory = (uint8_t)atoi(f); }
            f = strstr(tp, "\"euClass\":");
            if (f) { f += 10; telem.euClass = (uint8_t)atoi(f); }
            f = strstr(tp, "\"selfDesc\":\"");
            if (f) {
              f += 12; char* e = strchr(f, '"');
              if (e && (e-f) <= 23) {
                memset(telem.selfDesc, 0, sizeof(telem.selfDesc));
                memcpy(telem.selfDesc, f, e-f);
              }
            }
            Serial.printf("[TX-CFG] id=%s ua=%d cls=%d\n",
              telem.id, telem.uaType, telem.euClass);
          }
        }
        serialInLen = 0;
      }
    } else if (serialInLen < sizeof(serialInBuf) - 1) {
      serialInBuf[serialInLen++] = c;
    }
  }
}

// Déclaration anticipée du callback sniffer
void wifi_sniffer_cb(void* buf, wifi_promiscuous_pkt_type_t type);

void enablePromiscuous() {
  // ── V3.3 : country JP pour canal 14 ──
  wifi_country_t country = {
    .cc = "JP", .schan = 1, .nchan = 14,
    .policy = WIFI_COUNTRY_POLICY_AUTO
  };
  esp_wifi_set_country(&country);

  esp_wifi_set_promiscuous(true);
  esp_wifi_set_promiscuous_rx_cb(wifi_sniffer_cb);
  wifi_promiscuous_filter_t filter = {
    .filter_mask = WIFI_PROMIS_FILTER_MASK_ALL
  };
  esp_wifi_set_promiscuous_filter(&filter);
  esp_wifi_set_channel(currentChannel, WIFI_SECOND_CHAN_NONE);
}

// Forward declarations BLE (défini après WS)
void startBLE();
void stopBLE();

void startWiFiAP() {
  if (wifiAPActive) return;

  Serial.println("[XPORT] Demarrage WiFi AP");

  // ── FIX V3.2 : force disconnect avant recréation ──
  WiFi.softAPdisconnect(true);
  delay(100);
  WiFi.mode(WIFI_OFF);
  delay(100);

  WiFi.mode(WIFI_AP_STA);

  // ── FIX V3.3 : country JP pour autoriser canal 14 ──
  wifi_country_t country = {
    .cc = "JP",
    .schan = 1,
    .nchan = 14,
    .policy = WIFI_COUNTRY_POLICY_AUTO
  };
  esp_wifi_set_country(&country);

  WiFi.softAP(AP_SSID, AP_PASS, AP_CHANNEL);

  // ── DHCP MAISON : désactiver le serveur lwIP intégré ──
  esp_netif_t* apNetif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
  if (apNetif) {
    esp_netif_dhcps_stop(apNetif);
    Serial.println("[DHCP] lwIP DHCP server arrete");
  }

  // ── Démarrer notre DHCP sur port 67 ──
  clearDHCPClients();
  dhcpUDP.begin(DHCP_PORT);
  Serial.println("[DHCP] Serveur maison actif (port 67)");

  // ── mDNS : drone.local → 192.168.4.1 ──
  MDNS.begin("drone");
  Serial.println("[mDNS] drone.local actif");

  wsServer.begin();
  httpServer.begin();
  dnsServer.begin(DNS_PORT);
  for (int i = 0; i < MAX_WS_CLIENTS; i++) wsConnected[i] = false;

  wifiAPActive    = true;
  activeTransport = XPORT_WIFI;

  Serial.printf("[AP] %s / %s CH%d\n", AP_SSID, AP_PASS, AP_CHANNEL);
  Serial.printf("[AP] IP: %s → http://drone.local\n", WiFi.softAPIP().toString().c_str());
  Serial.println("[DNS] Captive portal fallback actif");

  updateTransportDisplay();
}

void stopWiFiAP() {
  if (!wifiAPActive) return;

  Serial.println("[XPORT] Arret WiFi AP");
  dnsServer.stop();
  dhcpUDP.stop();
  MDNS.end();
  for (int i = 0; i < MAX_WS_CLIENTS; i++) {
    if (wsConnected[i]) { wsClients[i].stop(); wsConnected[i] = false; }
  }

  WiFi.softAPdisconnect(true);
  wifiAPActive = false;
  updateTransportDisplay();
}

void checkTransport() {
  // WiFi AP au boot si pas encore active
  if (activeTransport == XPORT_NONE && !wifiAPActive) {
    startWiFiAP();
  }
}


// ══════════════════════════════════════════════════════════════
//  CAPTIVE PORTAL DNS — répond à TOUT avec 192.168.4.1
// ══════════════════════════════════════════════════════════════

void handleDNS() {
  int pktLen = dnsServer.parsePacket();
  if (pktLen < 12) return;

  uint8_t buf[512];
  int len = dnsServer.read(buf, sizeof(buf));
  if (len < 12) return;

  // Flags : QR=1, AA=1, RCODE=0
  buf[2] = 0x85;
  buf[3] = 0x80;
  // ANCOUNT = 1
  buf[6] = 0x00;
  buf[7] = 0x01;

  // ── V3.4 : extraire le hostname du QNAME pour l'écran portal ──
  int qpos = 12;
  char hostname[40] = "";
  int hlen = 0;
  while (qpos < len && buf[qpos] != 0 && hlen < 38) {
    uint8_t lbl = buf[qpos++];
    if (hlen > 0) hostname[hlen++] = '.';
    for (uint8_t j = 0; j < lbl && qpos < len && hlen < 38; j++)
      hostname[hlen++] = (char)buf[qpos++];
  }
  hostname[hlen] = '\0';

  // Skipper le QNAME (re-parcourir depuis 12)
  int pos = 12;
  while (pos < len && buf[pos] != 0) pos += buf[pos] + 1;
  pos += 5;  // null + QTYPE(2) + QCLASS(2)

  // Réponse : pointer QNAME (0xC00C), A record, TTL 0, IP 192.168.4.1
  if (pos + 16 <= (int)sizeof(buf)) {
    buf[pos++] = 0xC0; buf[pos++] = 0x0C;  // name pointer
    buf[pos++] = 0x00; buf[pos++] = 0x01;  // TYPE A
    buf[pos++] = 0x00; buf[pos++] = 0x01;  // CLASS IN
    buf[pos++] = 0x00; buf[pos++] = 0x00;
    buf[pos++] = 0x00; buf[pos++] = 0x00;  // TTL 0
    buf[pos++] = 0x00; buf[pos++] = 0x04;  // RDLENGTH 4
    buf[pos++] = 192; buf[pos++] = 168;
    buf[pos++] = 4;   buf[pos++] = 1;

    dnsServer.beginPacket(dnsServer.remoteIP(), dnsServer.remotePort());
    dnsServer.write(buf, pos);
    dnsServer.endPacket();

    // ── V3.4 PORTAL : tracker l'étape DNS ──
    if (portalStep < PORTAL_DNS) {
      portalStep = PORTAL_DNS;
      strncpy(portalDNSHost, hostname, sizeof(portalDNSHost) - 1);
      portalDNSHost[sizeof(portalDNSHost) - 1] = '\0';
      if (appState == STATE_PORTAL) drawPortalStep(PORTAL_DNS);
    }
  }
}


// ══════════════════════════════════════════════════════════════
//  SERVEUR HTTP
// ══════════════════════════════════════════════════════════════

// ── Helper : servir une page PROGMEM ──
void servePage(WiFiClient& client, const char* page) {
  uint32_t pageLen = strlen_P(page);
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: text/html; charset=utf-8");
  client.println("Connection: close");
  client.printf("Content-Length: %lu\r\n", pageLen);
  client.println();

  const char* ptr = page;
  uint32_t remaining = pageLen;
  char chunk[1024];
  while (remaining > 0) {
    uint16_t sz = (remaining > sizeof(chunk)) ? sizeof(chunk) : remaining;
    memcpy_P(chunk, ptr, sz);
    client.write((const uint8_t*)chunk, sz);
    ptr += sz;
    remaining -= sz;
  }
}

void handleHTTP() {
  if (!wifiAPActive) return;

  WiFiClient client = httpServer.available();
  if (!client) return;

  uint32_t t0 = millis();
  while (!client.available() && millis() - t0 < 2000) delay(1);
  if (!client.available()) { client.stop(); return; }

  String reqLine = "";
  String hostHeader = "";

  while (client.available()) {
    String line = client.readStringUntil('\n');
    line.trim();
    if (reqLine.length() == 0) reqLine = line;
    if (line.startsWith("Host:")) {
      hostHeader = line.substring(5);
      hostHeader.trim();
    }
    if (line.length() == 0) break;
  }

  // ═══════════════════════════════════════════════════════
  //  FAKE INTERNET OK — l'OS croit qu'il y a internet
  //  Pas de sandbox, Chrome normal, sauvegarde possible
  // ═══════════════════════════════════════════════════════

  // ── Android : /generate_204 → HTTP 204 ──
  if (reqLine.indexOf("/generate_204") >= 0 ||
      reqLine.indexOf("/gen_204") >= 0) {
    client.print("HTTP/1.1 204 No Content\r\n"
                 "Content-Length: 0\r\n"
                 "Connection: close\r\n\r\n");
    client.stop();
    Serial.println("[HTTP] Android → 204 OK");
    if (portalStep < PORTAL_REDIRECT) {
      portalStep = PORTAL_REDIRECT;
      if (appState == STATE_PORTAL) drawPortalStep(PORTAL_REDIRECT);
    }
    return;
  }

  // ── iOS : /hotspot-detect → Success ──
  if (reqLine.indexOf("/hotspot-detect") >= 0 ||
      reqLine.indexOf("/library/test/success") >= 0) {
    const char* ok = "<HTML><HEAD><TITLE>Success</TITLE></HEAD><BODY>Success</BODY></HTML>";
    client.printf("HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n"
                  "Content-Length: %d\r\nConnection: close\r\n\r\n%s",
                  strlen(ok), ok);
    client.stop();
    Serial.println("[HTTP] iOS → Success");
    if (portalStep < PORTAL_REDIRECT) {
      portalStep = PORTAL_REDIRECT;
      if (appState == STATE_PORTAL) drawPortalStep(PORTAL_REDIRECT);
    }
    return;
  }

  // ── Windows : /connecttest /ncsi ──
  if (reqLine.indexOf("/connecttest") >= 0 ||
      reqLine.indexOf("/ncsi.txt") >= 0) {
    const char* ok = "Microsoft Connect Test";
    client.printf("HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n"
                  "Content-Length: %d\r\nConnection: close\r\n\r\n%s",
                  strlen(ok), ok);
    client.stop();
    return;
  }

  // ── Autres probes ──
  if (reqLine.indexOf("/success.txt") >= 0 ||
      reqLine.indexOf("/kindle-wifi") >= 0 ||
      reqLine.indexOf("/check_network") >= 0 ||
      reqLine.indexOf("/redirect") >= 0) {
    client.print("HTTP/1.1 204 No Content\r\n"
                 "Content-Length: 0\r\n"
                 "Connection: close\r\n\r\n");
    client.stop();
    return;
  }

  // ═══════════════════════════════════════════════════════
  //  DNS HIJACK — n'importe quelle URL → 302 → drone.local
  // ═══════════════════════════════════════════════════════

  if (hostHeader.length() > 0 &&
      !hostHeader.startsWith("192.168.4.1") &&
      !hostHeader.startsWith("drone.local")) {
    client.print("HTTP/1.1 302 Found\r\n"
                 "Location: http://drone.local/\r\n"
                 "Connection: close\r\n"
                 "Content-Length: 0\r\n\r\n");
    client.stop();
    Serial.printf("[HTTP] %s → drone.local\n", hostHeader.c_str());
    return;
  }

  // ═══════════════════════════════════════════════════════
  //  drone.local/ ou 192.168.4.1/ → page DroneRX
  // ═══════════════════════════════════════════════════════

  servePage(client, PAGE_HTML);
  client.stop();
  Serial.printf("[HTTP] Page servie (%lu o)\n", (unsigned long)strlen_P(PAGE_HTML));

  if (portalStep < PORTAL_SERVED) {
    portalPageSize = strlen_P(PAGE_HTML);
    portalStep = PORTAL_SERVED;
    if (appState == STATE_PORTAL) drawPortalStep(PORTAL_SERVED);
  }
}


// ══════════════════════════════════════════════════════════════
//  WEBSOCKET
// ══════════════════════════════════════════════════════════════

void wsSendFrame(WiFiClient& c, const char* msg, uint16_t len) {
  if (!c.connected()) return;
  c.write(0x81);
  if (len < 126) c.write((uint8_t)len);
  else { c.write(126); c.write((uint8_t)(len >> 8)); c.write((uint8_t)(len & 0xFF)); }
  c.write((const uint8_t*)msg, len);
}

void wsSendAll(const char* msg, uint16_t len) {
  for (int i = 0; i < MAX_WS_CLIENTS; i++)
    if (wsConnected[i] && wsClients[i].connected())
      wsSendFrame(wsClients[i], msg, len);
}

bool wsHandshake(WiFiClient& client) {
  String wsKey = "";
  while (client.available()) {
    String line = client.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) break;
    if (line.startsWith("Sec-WebSocket-Key:")) {
      wsKey = line.substring(19);
      wsKey.trim();
    }
  }
  if (wsKey.length() == 0) return false;

  String acc = wsKey + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
  uint8_t sha[20];
  mbedtls_sha1((const uint8_t*)acc.c_str(), acc.length(), sha);
  char b64[64];
  size_t b64Len = 0;
  mbedtls_base64_encode((uint8_t*)b64, sizeof(b64), &b64Len, sha, 20);
  b64[b64Len] = '\0';

  client.print("HTTP/1.1 101 Switching Protocols\r\n"
               "Upgrade: websocket\r\nConnection: Upgrade\r\n"
               "Sec-WebSocket-Accept: ");
  client.print(b64);
  client.print("\r\n\r\n");
  return true;
}

void wsAcceptClients() {
  if (!wifiAPActive) return;
  WiFiClient c = wsServer.available();
  if (!c) return;

  int slot = -1;
  for (int i = 0; i < MAX_WS_CLIENTS; i++)
    if (!wsConnected[i]) { slot = i; break; }
  if (slot < 0) { c.stop(); return; }

  uint32_t t = millis() + 2000;
  while (!c.available() && millis() < t) delay(1);

  if (wsHandshake(c)) {
    wsClients[slot] = c;
    wsConnected[slot] = true;
    Serial.printf("[WS] Client %d\n", slot);
  } else {
    c.stop();
  }
}


// ══════════════════════════════════════════════════════════════
//  SCAN VENDOR IEs — CŒUR DE LA DÉTECTION
// ══════════════════════════════════════════════════════════════

// ── V3.3 : retourne true si au moins un OUI drone trouvé ──
bool scanAllVendorIEs(const uint8_t* body, uint16_t bodyLen,
                      const uint8_t* mac, int8_t rssi, uint8_t ch) {
  bool foundDrone = false;
  uint16_t i = 0;
  while (i + 1 < bodyLen) {
    uint8_t ieType = body[i];
    uint8_t ieLen  = body[i + 1];
    if (i + 2 + ieLen > bodyLen) break;

    if (ieType == 0xDD && ieLen >= 4) {
      const uint8_t* oui     = body + i + 2;
      uint8_t        proto   = 255;
      const uint8_t* payload = oui;
      uint16_t       payloadLen = ieLen;

      // ── FIX V3.2 : vérification longueur minimale par protocole ──
      if (ouiMatch(oui, OUI_FR) && ieLen >= 5) {
        proto = PROTO_FR; payload = oui + 4; payloadLen = ieLen - 4;
      }
      else if (ouiMatch(oui, OUI_ODID) && ieLen >= 5 && oui[3] == 0x0D) {
        proto = PROTO_ODID; payload = oui + 4; payloadLen = ieLen - 4;
      }
      else if (ouiMatch(oui, OUI_DJI) && ieLen >= 4) {
        proto = PROTO_DJI; payload = oui + 3; payloadLen = ieLen - 3;
      }
      else if (ouiMatch(oui, OUI_PAR) && ieLen >= 4) {
        proto = PROTO_PAR; payload = oui + 3; payloadLen = ieLen - 3;
      }
      // else → ignoré

      if (proto != 255 && payloadLen > 0 && payloadLen <= RX_BUF_SIZE) {
        foundDrone = true;  // ── V3.3 ──
        uint8_t slot = rxHead;
        volatile rx_packet_t* pkt = &rxQueue[slot];
        if (!pkt->ready) {
          pkt->proto   = proto;
          pkt->rssi    = rssi;
          pkt->channel = ch;
          pkt->payloadLen = payloadLen;
          memcpy((void*)pkt->mac, mac, 6);
          memcpy((void*)pkt->payload, payload, payloadLen);
          pkt->ready = true;
          rxHead = (rxHead + 1) % RX_QUEUE_SIZE;
        }
      }
    }
    i += 2 + ieLen;
  }
  return foundDrone;  // ── V3.3 ──
}


// ══════════════════════════════════════════════════════════════
//  CALLBACK SNIFFER
// ══════════════════════════════════════════════════════════════

void wifi_sniffer_cb(void* buf, wifi_promiscuous_pkt_type_t type) {
  const wifi_promiscuous_pkt_t* pkt = (wifi_promiscuous_pkt_t*)buf;

  // ── WATERFALL : RSSI par canal, maj par TOUTE trame radio ──
  uint8_t ch   = pkt->rx_ctrl.channel;
  int8_t  rssi = pkt->rx_ctrl.rssi;
  if (ch >= 1 && ch <= 14) channelRSSI[ch] = rssi;

  // Seules les trames Management nous intéressent pour les drones
  if (type != WIFI_PKT_MGMT) return;

  const uint8_t* frame    = pkt->payload;
  uint16_t       frameLen = pkt->rx_ctrl.sig_len;

  if (frameLen < sizeof(wifi_mgmt_hdr_t) + 12) return;

  uint16_t fc = frame[0] | (frame[1] << 8);
  if (((fc >> 2) & 0x03) != 0 || ((fc >> 4) & 0x0F) != 8) return;

  const wifi_mgmt_hdr_t* hdr = (wifi_mgmt_hdr_t*)frame;
  uint16_t bodyOffset = sizeof(wifi_mgmt_hdr_t) + 12;
  if (bodyOffset >= frameLen) return;

  bool foundDrone = scanAllVendorIEs(frame + bodyOffset, frameLen - bodyOffset,
                                     hdr->addr2, rssi, ch);

  // ── V3.3 WATERFALL : si aucun OUI drone, queue UNK ──
  if (!foundDrone && ch >= 1 && ch <= 14) {
    uint8_t slot = unkHead;
    volatile unk_event_t* evt = &unkQueue[slot];
    if (!evt->ready) {
      evt->channel = ch;
      evt->rssi    = rssi;
      evt->ready   = true;
      unkHead = (unkHead + 1) % UNK_QUEUE_SIZE;
    }
  }
}


// ══════════════════════════════════════════════════════════════
//  HEX + JSON + SEND
// ══════════════════════════════════════════════════════════════

void toHex(const uint8_t* data, uint16_t len, char* out) {
  const char h[] = "0123456789ABCDEF";
  for (uint16_t i = 0; i < len; i++) {
    out[i * 2]     = h[(data[i] >> 4) & 0x0F];
    out[i * 2 + 1] = h[data[i] & 0x0F];
  }
  out[len * 2] = '\0';
}

void buildJSON(uint8_t proto, const char* hex,
               const uint8_t* mac, int8_t rssi, uint8_t ch) {
  snprintf(jsonBuffer, sizeof(jsonBuffer),
    "{\"p\":\"%s\",\"h\":\"%s\",\"r\":%d,"
    "\"m\":\"%02X:%02X:%02X:%02X:%02X:%02X\",\"c\":%d}",
    PROTO_NAMES[proto], hex, rssi,
    mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], ch);
}


// ══════════════════════════════════════════════════════════════
//  BLE GATT SERVER — Nordic UART Service (NUS)
// ══════════════════════════════════════════════════════════════

BLEServer*         pBLEServer = nullptr;
BLECharacteristic* pTxChar    = nullptr;
BLECharacteristic* pRxChar    = nullptr;
bool     bleConnected   = false;
uint32_t bleLastActivity = 0;

// ── File d'attente BLE : JSON drones accumulés pendant le scan ──
char    bleQueue[BLE_QUEUE_SIZE][BLE_MSG_SIZE];
uint8_t bleQHead = 0;
uint8_t bleQTail = 0;
uint8_t bleQCount = 0;

void bleEnqueue(const char* msg) {
  if (bleQCount >= BLE_QUEUE_SIZE) return;  // plein → drop
  snprintf(bleQueue[bleQHead], BLE_MSG_SIZE, "%s\n", msg);
  bleQHead = (bleQHead + 1) % BLE_QUEUE_SIZE;
  bleQCount++;
}

void bleBurst() {
  if (!bleConnected || bleQCount == 0) return;
  while (bleQCount > 0) {
    uint16_t len = strlen(bleQueue[bleQTail]);
    pTxChar->setValue((uint8_t*)bleQueue[bleQTail], len);
    pTxChar->notify();
    bleQTail = (bleQTail + 1) % BLE_QUEUE_SIZE;
    bleQCount--;
    delay(3);  // laisser le stack BLE envoyer
  }
  bleLastActivity = millis();
}

// ── Callback connexion/déconnexion ──
class BLEServerCB : public BLEServerCallbacks {
  void onConnect(BLEServer* s) override {
    bleConnected = true;
    bleLastActivity = millis();
    activeTransport = XPORT_BLE;
    Serial.println("[BLE] Client connecte");
    updateTransportDisplay();
  }
  void onDisconnect(BLEServer* s) override {
    bleConnected = false;
    Serial.println("[BLE] Client deconnecte — advertising relance");
    s->startAdvertising();
  }
};

// ── Callback réception commande depuis le phone ──
void processBLECommand(const char* cmd);

class BLERxCB : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* pChar) override {
    String val = pChar->getValue();
    if (val.length() > 0) {
      bleLastActivity = millis();
      processBLECommand(val.c_str());
    }
  }
};

void startBLE() {
  if (pBLEServer) return;  // déjà actif

  BLEDevice::init(BLE_DEVICE_NAME);
  BLEDevice::setMTU(247);

  pBLEServer = BLEDevice::createServer();
  pBLEServer->setCallbacks(new BLEServerCB());

  BLEService* pService = pBLEServer->createService(BLE_SERVICE_UUID);

  pTxChar = pService->createCharacteristic(
    BLE_TX_CHAR_UUID,
    BLECharacteristic::PROPERTY_NOTIFY
  );
  pTxChar->addDescriptor(new BLE2902());

  pRxChar = pService->createCharacteristic(
    BLE_RX_CHAR_UUID,
    BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR
  );
  pRxChar->setCallbacks(new BLERxCB());

  pService->start();

  BLEAdvertising* pAdv = BLEDevice::getAdvertising();
  pAdv->addServiceUUID(BLE_SERVICE_UUID);
  pAdv->setScanResponse(true);
  pAdv->setMinPreferred(0x06);
  pAdv->start();

  bleLastActivity = millis();
  Serial.println("[BLE] GATT server + advertising actif");
}

void stopBLE() {
  if (pBLEServer) {
    BLEDevice::getAdvertising()->stop();
    BLEDevice::deinit(false);
    pBLEServer = nullptr;
    pTxChar = nullptr;
    pRxChar = nullptr;
    bleConnected = false;
  }
  Serial.println("[BLE] Arrete");
}

// ── Traitement commandes JSON reçues via BLE ──
void processBLECommand(const char* cmd) {
  Serial.printf("[BLE RX] %s\n", cmd);

  const char* gp = strstr(cmd, "\"g\":[");
  if (gp) {
    gp += 5;
    char* end;
    double la = strtod(gp, &end);
    if (end != gp && *end == ',') {
      gp = end + 1;
      double lo = strtod(gp, &end);
      if (end != gp) {
        telem.lat = la;
        telem.lon = lo;
        gp = (*end == ',') ? end + 1 : end;
        telem.alt = strtod(gp, NULL);
        Serial.printf("[BLE GPS] %.5f,%.5f,%.0f\n", telem.lat, telem.lon, telem.alt);
      }
    }
  }

  const char* sp = strstr(cmd, "\"snd\":");
  if (sp) {
    sp += 6;
    soundEnabled = (*sp == '1');
    M5.Speaker.setVolume(soundEnabled ? 128 : 0);
    Serial.printf("[BLE SND] %s\n", soundEnabled ? "ON" : "OFF");
  }

  const char* tp = strstr(cmd, "\"tx\":{");
  if (tp) {
    const char* f;
    f = strstr(tp, "\"id\":\"");
    if (f) {
      f += 6;
      const char* e = strchr(f, '"');
      if (e && (e - f) >= 3 && (e - f) <= 20) {
        memset(telem.id, 0, sizeof(telem.id));
        memcpy(telem.id, f, e - f);
        telem.idValid = true;
        savePilotID();
      }
    }
    f = strstr(tp, "\"uaType\":");
    if (f) { f += 9; telem.uaType = (uint8_t)atoi(f); }
    f = strstr(tp, "\"idType\":");
    if (f) { f += 9; telem.idType = (uint8_t)atoi(f); }
    f = strstr(tp, "\"euCat\":");
    if (f) { f += 8; telem.euCategory = (uint8_t)atoi(f); }
    f = strstr(tp, "\"euClass\":");
    if (f) { f += 10; telem.euClass = (uint8_t)atoi(f); }
    f = strstr(tp, "\"selfDesc\":\"");
    if (f) {
      f += 12;
      const char* e = strchr(f, '"');
      if (e && (e - f) <= 23) {
        memset(telem.selfDesc, 0, sizeof(telem.selfDesc));
        memcpy(telem.selfDesc, f, e - f);
      }
    }
    Serial.printf("[BLE TX-CFG] id=%s ua=%d cls=%d\n",
      telem.id, telem.uaType, telem.euClass);
  }
}


void sendJSON() {
  uint16_t len = strlen(jsonBuffer);
  Serial.println(jsonBuffer);
  if (wifiAPActive) wsSendAll(jsonBuffer, len);
  if (bleConnected) bleEnqueue(jsonBuffer);
}


// ══════════════════════════════════════════════════════════════
//  DRONE LIST + BEEP NOUVEAU DRONE
// ══════════════════════════════════════════════════════════════

int findDrone(const uint8_t* mac) {
  for (int i = 0; i < droneCount; i++)
    if (droneList[i].active && memcmp(droneList[i].mac, mac, 6) == 0)
      return i;
  return -1;
}

bool registerDrone(const uint8_t* mac, uint8_t proto, uint8_t ch, int8_t rssi) {
  // Marquer le canal comme actif (pour tracking)
  if (ch >= 1 && ch <= 14) activeChannels[ch] = true;

  int idx = findDrone(mac);
  if (idx >= 0) {
    droneList[idx].channel  = ch;
    droneList[idx].rssi     = rssi;
    droneList[idx].lastSeen = millis();
    return false;
  }
  if (droneCount < MAX_DRONES) {
    memcpy(droneList[droneCount].mac, mac, 6);
    droneList[droneCount].proto    = proto;
    droneList[droneCount].channel  = ch;
    droneList[droneCount].rssi     = rssi;
    droneList[droneCount].lastSeen = millis();
    droneList[droneCount].active   = true;
    droneCount++;
    return true;
  }
  return false;
}

void clearDrones() {
  droneCount    = 0;
  selectedDrone = -1;
  for (int i = 0; i < MAX_DRONES; i++) droneList[i].active = false;
}

void beepNewDrone() {
  if (!soundEnabled) return;
  M5.Speaker.tone(1200, 80);  delay(90);
  M5.Speaker.tone(1600, 80);  delay(90);
  M5.Speaker.tone(2400, 120); delay(130);
  M5.Speaker.stop();
}


// ══════════════════════════════════════════════════════════════
//  SCAN CANAL ADAPTATIF
// ══════════════════════════════════════════════════════════════

uint8_t nextChannel() {
  // TRACKING : cycle sur canaux actifs uniquement
  if (scanMode == MODE_TRACKING) {
    uint8_t n = countActiveChannels();
    if (n == 0) return currentChannel; // sécurité
    uint8_t count = 0;
    for (uint8_t ch = 1; ch <= 14; ch++) {
      if (!activeChannels[ch]) continue;
      if (count == scanChIndex % n) {
        scanChIndex++;
        return ch;
      }
      count++;
    }
    scanChIndex = 0;
    return currentChannel;
  }

  // CLASSIC : 1, 6, 11
  if (scanMode == MODE_CLASSIC) {
    uint8_t ch = SEQ_CLASSIC[scanChIndex % SEQ_CLASSIC_LEN];
    scanChIndex = (scanChIndex + 1) % SEQ_CLASSIC_LEN;
    return ch;
  }

  // DETECTION : 1 → 14 séquentiel
  uint8_t ch = (scanChIndex % 14) + 1;
  scanChIndex = (scanChIndex + 1) % 14;
  return ch;
}

void switchChannel() {
  uint32_t dwell = DWELL_BASE_MS + random(-DWELL_JITTER_MS, DWELL_JITTER_MS + 1);
  if (millis() - lastChannelSwitch < dwell) return;
  // ── BLE burst : envoyer les JSON accumulés entre 2 canaux ──
  bleBurst();
  lastChannelSwitch = millis();
  currentChannel = nextChannel();
  esp_wifi_set_channel(currentChannel, WIFI_SECOND_CHAN_NONE);
}


// ══════════════════════════════════════════════════════════════
//  TRAITEMENT FILE RX
// ══════════════════════════════════════════════════════════════

void processRxQueue() {
  for (uint8_t i = 0; i < RX_QUEUE_SIZE; i++) {
    volatile rx_packet_t* pkt = &rxQueue[i];
    if (!pkt->ready) continue;

    uint8_t  proto = pkt->proto;
    uint16_t plen  = pkt->payloadLen;
    uint8_t  mac[6];
    memcpy(mac, (void*)pkt->mac, 6);
    int8_t   rssi = pkt->rssi;
    uint8_t  ch   = pkt->channel;
    uint8_t  localPayload[RX_BUF_SIZE];
    memcpy(localPayload, (void*)pkt->payload, plen);
    pkt->ready = false;

    totalCount++;
    if (proto < NUM_PROTOS) protoCounts[proto]++;
    newBeacon = true;
    lastProto = proto;

    bool isNew = registerDrone(mac, proto, ch, rssi);

    toHex(localPayload, plen, hexBuffer);
    buildJSON(proto, hexBuffer, mac, rssi, ch);
    sendJSON();

    // ── Waterfall : plot la trame ──
    waterfallPlot(ch, proto, rssi);

    showLastEvent(proto, mac, rssi);
    drawCounters();
    drawBottomBar();

    if (isNew) {
      Serial.printf("[NEW] %s MAC=%02X:%02X:%02X:%02X:%02X:%02X CH%d\n",
        PROTO_NAMES[proto], mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], ch);
      beepNewDrone();
    }
  }
}

// ── V3.3 WATERFALL : traitement file UNK (waterfall seulement) ──
void processUnkQueue() {
  for (uint8_t i = 0; i < UNK_QUEUE_SIZE; i++) {
    volatile unk_event_t* evt = &unkQueue[i];
    if (!evt->ready) continue;
    waterfallPlot(evt->channel, PROTO_UNK, evt->rssi);
    evt->ready = false;
  }
}


// ══════════════════════════════════════════════════════════════
//  WATERFALL SDR
// ══════════════════════════════════════════════════════════════

void drawWaterfallBase() {
  // Zone noire
  M5.Display.fillRect(WF_X, WF_Y, WF_COLS * WF_COL_W, WF_H, C_BG);

  // Labels canaux
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(TFT_DARKGREY, C_BG);
  for (int ch = 1; ch <= 14; ch++) {
    int x = WF_X + (ch - 1) * WF_COL_W;
    M5.Display.setCursor(x + 4, WF_Y - 10);
    if (ch < 10) M5.Display.printf(" %d", ch);
    else M5.Display.printf("%d", ch);
  }

  // Séparateurs verticaux (grille)
  for (int ch = 0; ch <= 14; ch++) {
    int x = WF_X + ch * WF_COL_W;
    M5.Display.drawFastVLine(x, WF_Y, WF_H, C_GRID);
  }

  wfLine = 0;
  lastWfTick = millis();
}

// Avance le curseur temps (appelé périodiquement)
void waterfallTick() {
  if (millis() - lastWfTick < 200) return;  // ~5 lignes/seconde
  lastWfTick = millis();

  // Avancer le curseur de 2
  wfLine = (wfLine + 2) % WF_H;

  // Effacer 2 lignes DEVANT en NOIR (front visible du sweep)
  int e1 = (wfLine + 2) % WF_H;
  int e2 = (wfLine + 3) % WF_H;

  for (int ch = 1; ch <= 14; ch++) {
    int x = WF_X + (ch - 1) * WF_COL_W + 1;
    M5.Display.drawFastHLine(x, WF_Y + e1, WF_COL_W - 2, C_BG);
    M5.Display.drawFastHLine(x, WF_Y + e2, WF_COL_W - 2, C_BG);
  }

  // Séparateurs grille sur les lignes effacées
  for (int ch = 0; ch <= 14; ch++) {
    int x = WF_X + ch * WF_COL_W;
    M5.Display.drawPixel(x, WF_Y + e1, C_GRID);
    M5.Display.drawPixel(x, WF_Y + e2, C_GRID);
  }

  // ── V3.3 : fond RSSI sur les lignes COURANTES (derrière les plots) ──
  for (int ch = 1; ch <= 14; ch++) {
    int x = WF_X + (ch - 1) * WF_COL_W + 1;
    int8_t r = channelRSSI[ch];
    uint16_t bg = (r > -100) ? rssiToColor(r) : C_BG;
    M5.Display.drawFastHLine(x, WF_Y + wfLine, WF_COL_W - 2, bg);
    M5.Display.drawFastHLine(x, WF_Y + ((wfLine + 1) % WF_H), WF_COL_W - 2, bg);
  }
}

// Plot une trame détectée sur le waterfall
void waterfallPlot(uint8_t channel, uint8_t proto, int8_t rssi) {
  if (channel < 1 || channel > 14) return;
  // ── V3.3 : accepte PROTO_UNK en plus des 4 protos ──
  if (proto > PROTO_UNK) return;

  int x = WF_X + (channel - 1) * WF_COL_W + 1;
  int y = WF_Y + wfLine;
  uint16_t color = PROTO_COLORS[proto];

  // ── V3.3 : UNK = trait fin fixe, drones = largeur selon RSSI ──
  int w;
  if (proto == PROTO_UNK) {
    w = 4;
  } else {
    w = WF_COL_W - 2;
    if (rssi < -80) w = w / 3;
    else if (rssi < -65) w = w * 2 / 3;
  }

  // Centrer le point dans la colonne
  int xOff = (WF_COL_W - 2 - w) / 2;

  M5.Display.fillRect(x + xOff, y, w, 2, color);
}


// ══════════════════════════════════════════════════════════════
//  ÉCRANS
// ══════════════════════════════════════════════════════════════

void drawSplash() {
  M5.Display.fillScreen(C_BG);

  M5.Display.setTextSize(3);
  M5.Display.setTextColor(TFT_CYAN, C_BG);
  M5.Display.setCursor(20, 50);
  M5.Display.println("DRONE_RX V3.4");

  M5.Display.setTextSize(1);
  M5.Display.setTextColor(TFT_DARKGREY, C_BG);
  M5.Display.setCursor(90, 90);
  M5.Display.println("by eperret@azs.fr");

  M5.Display.setTextColor(TFT_YELLOW, C_BG);
  M5.Display.setCursor(80, 145);
  M5.Display.println("FR | ODID | DJI | PARROT");

  M5.Display.setTextColor(TFT_DARKGREY, C_BG);
  M5.Display.setCursor(30, 170);
  M5.Display.printf("WiFi: %s / %s", AP_SSID, AP_PASS);

  M5.Display.setCursor(115, 195);
  M5.Display.printf("BAT: %d mV", M5.Power.getBatteryVoltage());

  M5.Display.fillRect(0, 218, 320, 22, C_HDR);
  M5.Display.setTextColor(TFT_WHITE, C_HDR);
  M5.Display.setCursor(70, 224);
  M5.Display.println(">>> TOUCHER <<<");
}

void drawScanScreen() {
  M5.Display.fillScreen(C_BG);

  M5.Display.setTextSize(2);
  M5.Display.setTextColor(TFT_CYAN, C_BG);
  M5.Display.setCursor(10, 6);
  M5.Display.println("DRONE RX V3.4");

  M5.Display.setTextSize(1);
  M5.Display.setTextColor(TFT_DARKGREY, C_BG);
  M5.Display.setCursor(10, 30);
  M5.Display.println("FR | ODID | DJI | PARROT");

  M5.Display.setTextColor(TFT_YELLOW, C_BG);
  M5.Display.setCursor(10, 44);
  M5.Display.println("Scan WiFi 2.4GHz...");

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);
  int n = WiFi.scanNetworks();

  if (n == 0) {
    M5.Display.setTextColor(TFT_RED, C_BG);
    M5.Display.setCursor(10, 62);
    M5.Display.println("Aucun reseau");
  } else {
    int mx = (n > 12) ? 12 : n;
    char line[54];
    for (int i = 0; i < mx; i++) {
      String ssid = WiFi.SSID(i);
      if (ssid.length() > 22) ssid = ssid.substring(0, 22);
      if (ssid.length() == 0) ssid = "(masque)";
      snprintf(line, sizeof(line), "CH%2d %4ddBm %s",
               WiFi.channel(i), WiFi.RSSI(i), ssid.c_str());
      uint16_t col = TFT_GREEN;
      if (WiFi.RSSI(i) < -70) col = TFT_YELLOW;
      if (WiFi.RSSI(i) < -85) col = TFT_RED;
      M5.Display.setTextColor(col, C_BG);
      M5.Display.setCursor(6, 62 + i * 12);
      M5.Display.println(line);
    }
  }
  WiFi.scanDelete();

  M5.Display.fillRect(0, 218, 320, 22, C_HDR);
  M5.Display.setTextColor(TFT_WHITE, C_HDR);
  M5.Display.setCursor(30, 224);
  M5.Display.println(">>> TOUCHER POUR CONTINUER <<<");
}

// ── V3.4 : ecran PORTAL — monitoring connexion client ──

void drawPortalScreen() {
  M5.Display.fillScreen(C_BG);

  M5.Display.setTextSize(2);
  M5.Display.setTextColor(TFT_CYAN, C_BG);
  M5.Display.setCursor(50, 4);
  M5.Display.println("DRONE.LOCAL");

  M5.Display.setTextSize(1);
  M5.Display.setTextColor(TFT_GREEN, C_BG);
  M5.Display.setCursor(10, 30);
  M5.Display.printf("WiFi : %s / %s", AP_SSID, AP_PASS);

  M5.Display.setTextColor(TFT_YELLOW, C_BG);
  M5.Display.setCursor(10, 44);
  M5.Display.printf("URL  : http://drone.local  CH%d", AP_CHANNEL);

  M5.Display.drawFastHLine(10, 60, 300, 0x1082);

  // Dessiner toutes les étapes en gris (pending)
  const char* labels[] = {
    "AP WiFi + mDNS + DHCP",
    "Client connecte",
    "Requete DNS recue",
    "OS probe → internet OK",
    "Page servie (Chrome)"
  };

  for (int i = 0; i < 5; i++) {
    uint16_t y = 68 + i * 22;
    M5.Display.setTextColor(0x4208, C_BG);  // gris sombre
    M5.Display.setCursor(10, y);
    M5.Display.printf("  [ ] %s", labels[i]);
  }

  // Step 0 est toujours OK (AP déjà active quand on arrive ici)
  drawPortalStep(PORTAL_WAIT);

  M5.Display.fillRect(0, 226, 320, 14, C_HDR);
  M5.Display.setTextColor(TFT_DARKGREY, C_HDR);
  M5.Display.setCursor(50, 228);
  M5.Display.println("En attente de client...");
}

void drawPortalStep(uint8_t step) {
  uint16_t y = 68 + step * 22;

  // Effacer la ligne
  M5.Display.fillRect(10, y, 300, 16, C_BG);
  M5.Display.setTextSize(1);

  switch (step) {
    case PORTAL_WAIT:
      M5.Display.setTextColor(TFT_GREEN, C_BG);
      M5.Display.setCursor(10, y);
      M5.Display.print("  [+] AP WiFi active");
      // Passer la ligne suivante en jaune "en cours"
      M5.Display.fillRect(10, y + 22, 300, 16, C_BG);
      M5.Display.setTextColor(TFT_YELLOW, C_BG);
      M5.Display.setCursor(10, y + 22);
      M5.Display.print("  [>] Client connecte ...");
      break;

    case PORTAL_CLIENT:
      M5.Display.setTextColor(TFT_GREEN, C_BG);
      M5.Display.setCursor(10, y);
      M5.Display.printf("  [+] Client : %s", portalClientIP);
      // Ligne suivante
      M5.Display.fillRect(10, y + 22, 300, 16, C_BG);
      M5.Display.setTextColor(TFT_YELLOW, C_BG);
      M5.Display.setCursor(10, y + 22);
      M5.Display.print("  [>] Requete DNS ...");
      M5.Speaker.tone(1200, 40); delay(50); M5.Speaker.stop();
      break;

    case PORTAL_DNS:
      M5.Display.setTextColor(TFT_GREEN, C_BG);
      M5.Display.setCursor(10, y);
      // Tronquer le hostname si trop long
      if (strlen(portalDNSHost) > 28) portalDNSHost[28] = '\0';
      M5.Display.printf("  [+] DNS: %s", portalDNSHost);
      // Ligne suivante
      M5.Display.fillRect(10, y + 22, 300, 16, C_BG);
      M5.Display.setTextColor(TFT_YELLOW, C_BG);
      M5.Display.setCursor(10, y + 22);
      M5.Display.print("  [>] Redirect 302 ...");
      M5.Speaker.tone(1400, 40); delay(50); M5.Speaker.stop();
      break;

    case PORTAL_REDIRECT:
      M5.Display.setTextColor(TFT_GREEN, C_BG);
      M5.Display.setCursor(10, y);
      M5.Display.print("  [+] WiFi stable (fake 204)");
      // Ligne suivante
      M5.Display.fillRect(10, y + 22, 300, 16, C_BG);
      M5.Display.setTextColor(TFT_YELLOW, C_BG);
      M5.Display.setCursor(10, y + 22);
      M5.Display.print("  [>] Chrome > drone.local ...");
      M5.Speaker.tone(1600, 40); delay(50); M5.Speaker.stop();
      break;

    case PORTAL_SERVED:
      M5.Display.setTextColor(TFT_GREEN, C_BG);
      M5.Display.setCursor(10, y);
      M5.Display.printf("  [+] Page OK (%lu o)", (unsigned long)portalPageSize);

      // ── Tout vert : footer ──
      M5.Display.fillRect(0, 190, 320, 24, C_BG);
      M5.Display.setTextSize(1);
      M5.Display.setTextColor(TFT_CYAN, C_BG);
      M5.Display.setCursor(20, 192);
      M5.Display.print("[Sauver] > WiFi/4G > rouvrir");

      M5.Display.fillRect(0, 226, 320, 14, C_HDR);
      M5.Display.setTextColor(TFT_WHITE, C_HDR);
      M5.Display.setCursor(30, 228);
      M5.Display.println(">>> TOUCHER POUR CONTINUER <<<");

      M5.Speaker.tone(1000, 60); delay(70);
      M5.Speaker.tone(1500, 60); delay(70);
      M5.Speaker.tone(2000, 80); delay(100);
      M5.Speaker.stop();
      break;
  }
}

// ── V3.3 : ecran HELP connexion ──
void drawHelpScreen() {
  M5.Display.fillScreen(C_BG);
  M5.Display.setTextSize(2);
  M5.Display.setTextColor(TFT_CYAN, C_BG);
  M5.Display.setCursor(60, 4);
  M5.Display.println("QUICK START");

  M5.Display.setTextSize(1);
  M5.Display.setTextColor(TFT_GREEN, C_BG);
  M5.Display.setCursor(6, 32);
  M5.Display.println("1. WiFi > drone.local / drone.local");

  M5.Display.setTextColor(TFT_YELLOW, C_BG);
  M5.Display.setCursor(6, 52);
  M5.Display.println("2. Chrome > http://drone.local");
  M5.Display.setTextColor(TFT_DARKGREY, C_BG);
  M5.Display.setCursor(6, 64);
  M5.Display.println("   ou 192.168.4.1 si mDNS absent");

  M5.Display.setTextColor(TFT_GREEN, C_BG);
  M5.Display.setCursor(6, 84);
  M5.Display.println("3. Cliquer [Sauvegarder]");

  M5.Display.setTextColor(TFT_YELLOW, C_BG);
  M5.Display.setCursor(6, 104);
  M5.Display.println("4. Quitter WiFi > votre WiFi/4G");

  M5.Display.setTextColor(TFT_GREEN, C_BG);
  M5.Display.setCursor(6, 124);
  M5.Display.println("5. Ouvrir DroneRX_V3.html sauvee");
  M5.Display.setTextColor(TFT_DARKGREY, C_BG);
  M5.Display.setCursor(6, 136);
  M5.Display.println("   (carte + BLE dispo)");

  M5.Display.setTextColor(TFT_YELLOW, C_BG);
  M5.Display.setCursor(6, 156);
  M5.Display.println("6. BLE Connect = scan 100% RX");

  M5.Display.setTextColor(TFT_RED, C_BG);
  M5.Display.setCursor(6, 180);
  M5.Display.println("Sans BLE: WS auto via WiFi drone.local");
  M5.Display.setTextColor(TFT_DARKGREY, C_BG);
  M5.Display.setCursor(6, 194);
  M5.Display.println("(pas de scan, pas de carte)");

  M5.Display.fillRect(0, 218, 320, 22, C_HDR);
  M5.Display.setTextColor(TFT_WHITE, C_HDR);
  M5.Display.setCursor(30, 224);
  M5.Display.println(">>> TOUCHER POUR CONTINUER <<<");
}

void drawModeSelect() {
  M5.Display.fillScreen(C_BG);

  M5.Display.setTextSize(2);
  M5.Display.setTextColor(TFT_CYAN, C_BG);
  M5.Display.setCursor(30, 4);
  M5.Display.println("CHOIX DU MODE");

  // DETECTION (y: 30–69)
  M5.Display.fillRoundRect(10, 30, 300, 40, 5, 0x0410);
  M5.Display.setTextColor(TFT_GREEN, 0x0410);
  M5.Display.setTextSize(2);
  M5.Display.setCursor(30, 34);
  M5.Display.println("DETECTION");
  M5.Display.setTextSize(1);
  M5.Display.setCursor(30, 54);
  M5.Display.println("CH 1-14  1s/canal");

  // CLASSIC (y: 76–115)
  M5.Display.fillRoundRect(10, 76, 300, 40, 5, 0x4208);
  M5.Display.setTextColor(TFT_YELLOW, 0x4208);
  M5.Display.setTextSize(2);
  M5.Display.setCursor(30, 80);
  M5.Display.println("CLASSIC");
  M5.Display.setTextSize(1);
  M5.Display.setCursor(30, 100);
  M5.Display.println("CH 1,6,11  1s/canal");

  // TRACKING (y: 122–161) — grisé si pas de canaux actifs
  uint8_t ach = countActiveChannels();
  if (ach > 0) {
    M5.Display.fillRoundRect(10, 122, 300, 40, 5, 0x4000);
    M5.Display.setTextColor(TFT_RED, 0x4000);
    M5.Display.setTextSize(2);
    M5.Display.setCursor(30, 126);
    M5.Display.println("TRACKING");
    M5.Display.setTextSize(1);
    M5.Display.setCursor(30, 146);
    char tbuf[32];
    snprintf(tbuf, sizeof(tbuf), "%d canaux actifs", ach);
    M5.Display.println(tbuf);
  } else {
    M5.Display.fillRoundRect(10, 122, 300, 40, 5, 0x2104);
    M5.Display.setTextColor(TFT_DARKGREY, 0x2104);
    M5.Display.setTextSize(2);
    M5.Display.setCursor(30, 126);
    M5.Display.println("TRACKING");
    M5.Display.setTextSize(1);
    M5.Display.setCursor(30, 146);
    M5.Display.println("Detection/Classic d'abord");
  }

  // TX DRONE ID (y: 168–207)
  M5.Display.fillRoundRect(10, 168, 300, 40, 5, 0x4210);
  M5.Display.setTextColor(TFT_YELLOW, 0x4210);
  M5.Display.setTextSize(2);
  M5.Display.setCursor(30, 172);
  M5.Display.println("TX DRONE ID");
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(TFT_DARKGREY, 0x4210);
  M5.Display.setCursor(30, 192);
  M5.Display.printf("Beacon FR CH6 %s", telem.idValid ? "[ID OK]" : "[saisir ID]");
}

void drawDroneSelect() {
  M5.Display.fillScreen(C_BG);

  M5.Display.setTextSize(2);
  M5.Display.setTextColor(TFT_RED, C_BG);
  M5.Display.setCursor(10, 8);
  M5.Display.println("SELECT DRONE");

  if (droneCount == 0) {
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(TFT_DARKGREY, C_BG);
    M5.Display.setCursor(10, 50);
    M5.Display.println("Aucun drone detecte.");
    M5.Display.println("Lance DETECTION d'abord.");
    M5.Display.fillRect(0, 218, 320, 22, C_HDR);
    M5.Display.setTextColor(TFT_WHITE, C_HDR);
    M5.Display.setCursor(50, 224);
    M5.Display.println("TOUCHER = retour");
    return;
  }

  int mx = (droneCount > 8) ? 8 : droneCount;
  for (int i = 0; i < mx; i++) {
    uint16_t y = 40 + i * 24;
    M5.Display.fillRoundRect(10, y, 300, 22, 3, 0x1082);
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(PROTO_COLORS[droneList[i].proto], 0x1082);
    char buf[54];
    snprintf(buf, sizeof(buf), "[%s] %02X:%02X:%02X:%02X:%02X:%02X CH%d %ddBm",
      PROTO_NAMES[droneList[i].proto],
      droneList[i].mac[0], droneList[i].mac[1], droneList[i].mac[2],
      droneList[i].mac[3], droneList[i].mac[4], droneList[i].mac[5],
      droneList[i].channel, droneList[i].rssi);
    M5.Display.setCursor(14, y + 4);
    M5.Display.print(buf);
  }
}


// ══════════════════════════════════════════════════════════════
//  ÉCRAN SNIFF — AVEC WATERFALL + BOUTONS TOUCH
// ══════════════════════════════════════════════════════════════

void drawSniffScreen() {
  M5.Display.fillScreen(C_BG);

  // Titre mode
  M5.Display.setTextSize(2);
  M5.Display.setTextColor(TFT_CYAN, C_BG);
  M5.Display.setCursor(10, 4);
  M5.Display.print(SCAN_MODE_NAMES[scanMode]);
  if (scanMode == MODE_TRACKING) {
    M5.Display.setTextSize(1);
    M5.Display.print(" CH");
    bool first = true;
    for (uint8_t i = 1; i <= 14; i++) {
      if (activeChannels[i]) {
        if (!first) M5.Display.print(",");
        M5.Display.printf("%d", i);
        first = false;
      }
    }
  }

  // Compteurs + transport + dernier event
  drawCounters();
  drawTransportLine();
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(TFT_DARKGREY, C_BG);
  M5.Display.setCursor(10, 58);
  M5.Display.println("En attente...");

  // Waterfall SDR
  drawWaterfallBase();

  // Boutons touch
  drawTouchButtons();

  // Barre info
  drawBottomBar();
}

void drawCounters() {
  M5.Display.setTextSize(1);
  char buf[16];
  struct { uint16_t x; uint16_t c; uint8_t p; const char* pfx; } ct[] = {
    {10, TFT_CYAN, 0, "FR:"},
    {74, TFT_GREEN, 1, "OD:"},
    {150, TFT_ORANGE, 2, "DJI:"},
    {225, TFT_MAGENTA, 3, "PAR:"}
  };
  for (int i = 0; i < 4; i++) {
    M5.Display.setTextColor(ct[i].c, C_BG);
    snprintf(buf, sizeof(buf), "%s%lu ", ct[i].pfx, (unsigned long)protoCounts[ct[i].p]);
    M5.Display.setCursor(ct[i].x, 28);
    M5.Display.print(buf);
  }
}

void drawTransportLine() {
  M5.Display.fillRect(0, 42, 320, 14, C_BG);
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(TFT_DARKGREY, C_BG);
  M5.Display.setCursor(10, 44);
  if (activeTransport == XPORT_BLE)
    M5.Display.println("BLE actif - radio 100% RX");
  else
    M5.Display.printf("WiFi:%s http://drone.local", AP_SSID);
}

void updateTransportDisplay() {
  if (appState != STATE_SNIFF) return;
  drawTransportLine();
  drawBottomBar();
}

void drawTouchButtons() {
  // [PWR OFF] — rouge sombre
  M5.Display.fillRoundRect(BTN_PWR_X, BTN_Y, BTN_PWR_W, BTN_H, 3, 0x4000);
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(TFT_RED, 0x4000);
  M5.Display.setCursor(BTN_PWR_X + 25, BTN_Y + 4);
  M5.Display.print("PWR OFF");

  // [REBOOT] — orange sombre
  M5.Display.fillRoundRect(BTN_RBT_X, BTN_Y, BTN_RBT_W, BTN_H, 3, 0x4200);
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(TFT_ORANGE, 0x4200);
  M5.Display.setCursor(BTN_RBT_X + 28, BTN_Y + 4);
  M5.Display.print("REBOOT");

  // [MODE] — bleu sombre
  M5.Display.fillRoundRect(BTN_MODE_X, BTN_Y, BTN_MODE_W, BTN_H, 3, C_HDR);
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(TFT_CYAN, C_HDR);
  M5.Display.setCursor(BTN_MODE_X + 30, BTN_Y + 4);
  M5.Display.print("CHG MODE");
}

void drawBottomBar() {
  M5.Display.fillRect(0, 226, 320, 14, C_HDR);
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(TFT_WHITE, C_HDR);

  char buf[54];
  const char* xp = (activeTransport == XPORT_BLE) ? "BLE" : "WiFi";
  snprintf(buf, sizeof(buf), "CH:%d Tot:%lu %s [%s] D:%d",
    currentChannel, (unsigned long)totalCount,
    SCAN_MODE_NAMES[scanMode], xp, droneCount);
  M5.Display.setCursor(6, 228);
  M5.Display.print(buf);

  uint16_t avg = vbatAvg();
  M5.Display.setTextColor(avg < 3500 ? TFT_RED : TFT_DARKGREY, C_HDR);
  M5.Display.setCursor(278, 228);
  M5.Display.printf("%dmV", avg);
}

void showLastEvent(uint8_t proto, const uint8_t* mac, int8_t rssi) {
  M5.Display.fillRect(0, 56, 320, 12, C_BG);
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(PROTO_COLORS[proto], C_BG);
  char buf[54];
  snprintf(buf, sizeof(buf), "[%s] %02X:%02X:%02X:%02X:%02X:%02X %ddBm CH%d",
    PROTO_NAMES[proto],
    mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
    rssi, currentChannel);
  M5.Display.setCursor(6, 58);
  M5.Display.print(buf);
}


// ══════════════════════════════════════════════════════════════
//  GESTION TOUCH PENDANT SNIFF
// ══════════════════════════════════════════════════════════════

// Retourne true si on doit quitter le mode sniff
bool handleSniffTouch() {
  auto t = M5.Touch.getDetail();
  if (!t.isPressed()) return false;

  int tx = t.x;
  int ty = t.y;

  // Zone boutons touch (y: 200–216)
  if (ty >= BTN_Y && ty <= BTN_Y + BTN_H) {

    // PWR OFF
    if (tx >= BTN_PWR_X && tx < BTN_PWR_X + BTN_PWR_W) {
      Serial.println("[TOUCH] PWR OFF");
      M5.Display.fillScreen(TFT_RED);
      M5.Display.setTextSize(3);
      M5.Display.setTextColor(TFT_WHITE, TFT_RED);
      M5.Display.setCursor(30, 80);
      M5.Display.println("EXTINCTION");
      M5.Speaker.tone(400, 500); delay(600);
      M5.Speaker.tone(300, 500); delay(600);
      M5.Speaker.stop();
      esp_wifi_set_promiscuous(false);
      WiFi.mode(WIFI_OFF);
      delay(500);
      M5.Power.powerOff();
      esp_deep_sleep_start();
      while (1) delay(1000);
    }

    // REBOOT → restart board (retour WiFi AP pour nouveau device)
    if (tx >= BTN_RBT_X && tx < BTN_RBT_X + BTN_RBT_W) {
      Serial.println("[TOUCH] REBOOT");
      M5.Display.fillScreen(TFT_ORANGE);
      M5.Display.setTextSize(3);
      M5.Display.setTextColor(TFT_WHITE, TFT_ORANGE);
      M5.Display.setCursor(60, 80);
      M5.Display.println("REBOOT...");
      M5.Speaker.tone(1000, 200); delay(300);
      M5.Speaker.stop();
      esp_wifi_set_promiscuous(false);
      stopBLE();
      WiFi.mode(WIFI_OFF);
      delay(500);
      ESP.restart();
    }

    // CHANGE MODE
    if (tx >= BTN_MODE_X && tx < BTN_MODE_X + BTN_MODE_W) {
      Serial.println("[TOUCH] CHANGE MODE");
      M5.Speaker.tone(1500, 50); delay(80); M5.Speaker.stop();

      // ── FIX V3.2 : debounce — attendre relâchement ──
      while (true) { M5.update(); auto r = M5.Touch.getDetail(); if (!r.isPressed()) break; delay(20); }

      // Arrêter le sniff proprement
      esp_wifi_set_promiscuous(false);

      // ── FIX V3.2 : clear drones au changement de mode ──
      clearDrones();
      totalCount = 0;
      for (int i = 0; i < NUM_PROTOS; i++) protoCounts[i] = 0;

      appState = STATE_MODE_SELECT;
      drawModeSelect();
      delay(300);
      return true;
    }
  }

  return false;
}


// ══════════════════════════════════════════════════════════════
//  START SNIFF
// ══════════════════════════════════════════════════════════════

void startSniffMode() {
  appState = STATE_SNIFF;

  // ── Séquentiel : WiFi AP OFF → BLE ON ──
  stopWiFiAP();
  WiFi.mode(WIFI_OFF);
  delay(100);
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);
  startBLE();
  activeTransport = XPORT_BLE;

  totalCount = 0;
  for (int i = 0; i < NUM_PROTOS; i++) protoCounts[i] = 0;
  scanChIndex = 0;

  for (int i = 0; i < RX_QUEUE_SIZE; i++) rxQueue[i].ready = false;

  // ── V3.3 : reset file UNK et RSSI canaux ──
  for (int i = 0; i < UNK_QUEUE_SIZE; i++) unkQueue[i].ready = false;
  for (int i = 0; i < 15; i++) channelRSSI[i] = -100;

  enablePromiscuous();

  Serial.printf("\n[RX] Mode %s actif [BLE]\n\n",
    SCAN_MODE_NAMES[scanMode]);

  M5.Speaker.tone(1000, 60); delay(80);
  M5.Speaker.tone(1500, 60); delay(80);
  M5.Speaker.tone(2000, 60); delay(80);
  M5.Speaker.stop();

  drawSniffScreen();
}


// ══════════════════════════════════════════════════════════════
//  V3.3 TX — NVS STOCKAGE ID EXPLOITANT
// ══════════════════════════════════════════════════════════════

void loadPilotID() {
  nvs.begin("droneRX", true);
  String s = nvs.getString("exploitantId", "");
  nvs.end();
  if (s.length() >= 3) {
    strncpy(telem.id, s.c_str(), KB_MAX_LEN);
    telem.id[KB_MAX_LEN] = '\0';
    telem.idValid = true;
  } else if (strlen(telem.id) >= 3) {
    // Garder le défaut codé en dur, et le sauvegarder en NVS
    telem.idValid = true;
    savePilotID();
  }
}

void savePilotID() {
  nvs.begin("droneRX", false);
  nvs.putString("exploitantId", telem.id);
  nvs.end();
}


// ══════════════════════════════════════════════════════════════
//  V3.3 TX — CLAVIER TACTILE
// ══════════════════════════════════════════════════════════════

#define KB_X0  5
#define KB_Y0  55
#define KB_KW  29
#define KB_KH  26
#define KB_PX  31
#define KB_PY  30

static const char* KB_ROW[] = { "0123456789", "abcdefghij", "klmnopqrst", "uvwxyz-" };

void drawKBInput() {
  M5.Display.fillRect(10, 28, 300, 20, 0x1082);
  M5.Display.setTextSize(2);
  M5.Display.setTextColor(TFT_CYAN, 0x1082);
  M5.Display.setCursor(14, 30);
  if (kbLen > 0) M5.Display.print(kbInput);
  else M5.Display.print("_");
  if (kbLen < KB_MAX_LEN) {
    int cx = 14 + kbLen * 12;
    M5.Display.fillRect(cx, 44, 12, 2, TFT_CYAN);
  }
}

void drawKeyboard() {
  M5.Display.fillScreen(C_BG);

  M5.Display.setTextSize(2);
  M5.Display.setTextColor(TFT_YELLOW, C_BG);
  M5.Display.setCursor(10, 4);
  M5.Display.println("ID EXPLOITANT");

  drawKBInput();

  for (int row = 0; row < 4; row++) {
    int nk = strlen(KB_ROW[row]);
    for (int col = 0; col < nk; col++) {
      int kx = KB_X0 + col * KB_PX;
      int ky = KB_Y0 + row * KB_PY;
      M5.Display.fillRoundRect(kx, ky, KB_KW, KB_KH, 3, 0x1082);
      M5.Display.setTextSize(2);
      M5.Display.setTextColor(TFT_WHITE, 0x1082);
      M5.Display.setCursor(kx + 8, ky + 5);
      char c = KB_ROW[row][col];
      if (kbShift && c >= 'a' && c <= 'z') c -= 32;
      char s[2] = { c, 0 };
      M5.Display.print(s);
    }
  }

  // Boutons : MAJ / DEL / OK sur une 5ème rangée
  int btnY = KB_Y0 + 4 * KB_PY;

  // MAJ
  uint16_t majCol = kbShift ? 0x0410 : 0x1082;
  M5.Display.fillRoundRect(KB_X0, btnY, 80, KB_KH, 3, majCol);
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(kbShift ? TFT_GREEN : TFT_DARKGREY, majCol);
  M5.Display.setCursor(KB_X0 + 24, btnY + 9);
  M5.Display.print("MAJ");

  // DEL
  M5.Display.fillRoundRect(KB_X0 + 90, btnY, 80, KB_KH, 3, 0x4000);
  M5.Display.setTextColor(TFT_RED, 0x4000);
  M5.Display.setCursor(KB_X0 + 118, btnY + 9);
  M5.Display.print("DEL");

  // OK
  M5.Display.fillRoundRect(KB_X0 + 180, btnY, 80, KB_KH, 3, 0x0410);
  M5.Display.setTextColor(TFT_GREEN, 0x0410);
  M5.Display.setCursor(KB_X0 + 212, btnY + 9);
  M5.Display.print("OK");

  M5.Display.setTextSize(1);
  M5.Display.setTextColor(TFT_DARKGREY, C_BG);
  M5.Display.setCursor(10, 225);
  M5.Display.printf("20 chars | %s", telem.idValid ? "Modif" : "Nouveau");
}

void startTxMode();  // forward

bool handleKeyboardTouch() {
  auto t = M5.Touch.getDetail();
  if (!t.isPressed()) return false;
  int tx = t.x, ty = t.y;

  while (true) { M5.update(); auto r = M5.Touch.getDetail(); if (!r.isPressed()) break; delay(20); }

  // Touches caractères (4 rangées)
  for (int row = 0; row < 4; row++) {
    int ky = KB_Y0 + row * KB_PY;
    if (ty < ky || ty > ky + KB_KH) continue;
    int nk = strlen(KB_ROW[row]);
    for (int col = 0; col < nk; col++) {
      int kx = KB_X0 + col * KB_PX;
      if (tx >= kx && tx < kx + KB_KW) {
        if (kbLen < KB_MAX_LEN) {
          char c = KB_ROW[row][col];
          if (kbShift && c >= 'a' && c <= 'z') c -= 32;
          kbInput[kbLen++] = c;
          kbInput[kbLen] = '\0';
        }
        M5.Speaker.tone(1800, 30); delay(40); M5.Speaker.stop();
        drawKBInput();
        return false;
      }
    }
  }

  // Boutons MAJ / DEL / OK (5ème rangée)
  int btnY = KB_Y0 + 4 * KB_PY;
  if (ty >= btnY && ty <= btnY + KB_KH) {
    // MAJ
    if (tx >= KB_X0 && tx < KB_X0 + 80) {
      kbShift = !kbShift;
      M5.Speaker.tone(1200, 30); delay(40); M5.Speaker.stop();
      drawKeyboard();
      return false;
    }
    // DEL
    if (tx >= KB_X0 + 90 && tx < KB_X0 + 170) {
      if (kbLen > 0) kbInput[--kbLen] = '\0';
      M5.Speaker.tone(800, 30); delay(40); M5.Speaker.stop();
      drawKBInput();
      return false;
    }
    // OK
    if (tx >= KB_X0 + 180 && tx < KB_X0 + 260 && kbLen >= 3) {
      strncpy(telem.id, kbInput, KB_MAX_LEN);
      telem.id[KB_MAX_LEN] = '\0';
      telem.idValid = true;
      savePilotID();
      M5.Speaker.tone(2000, 50); delay(60);
      M5.Speaker.tone(2400, 80); delay(100);
      M5.Speaker.stop();
      startTxMode();
      return true;
    }
  }
  return false;
}


// ══════════════════════════════════════════════════════════════
//  V3.3 TX — CONSTRUCTION BEACON FR
// ══════════════════════════════════════════════════════════════

static void beaconPutI32(uint8_t* buf, uint16_t& p, int32_t v) {
  buf[p++] = (v >> 24) & 0xFF;
  buf[p++] = (v >> 16) & 0xFF;
  buf[p++] = (v >> 8) & 0xFF;
  buf[p++] = v & 0xFF;
}

uint16_t buildFRBeacon(uint8_t* buf) {
  uint16_t p = 0;

  // ── 802.11 Header ──
  buf[p++] = 0x80; buf[p++] = 0x00;
  buf[p++] = 0x00; buf[p++] = 0x00;
  memset(buf + p, 0xFF, 6); p += 6;
  memcpy(buf + p, txMAC, 6); p += 6;
  memcpy(buf + p, txMAC, 6); p += 6;
  uint16_t seqCtrl = (txSeqNum++) << 4;
  buf[p++] = seqCtrl & 0xFF;
  buf[p++] = (seqCtrl >> 8) & 0xFF;
  memset(buf + p, 0, 8); p += 8;
  buf[p++] = 0x64; buf[p++] = 0x00;
  buf[p++] = 0x01; buf[p++] = 0x04;
  buf[p++] = 0x00; buf[p++] = 0x00;
  buf[p++] = 0x01; buf[p++] = 0x04;
  buf[p++] = 0x82; buf[p++] = 0x84; buf[p++] = 0x8B; buf[p++] = 0x96;
  buf[p++] = 0x03; buf[p++] = 0x01; buf[p++] = 0x06;

  // ── Vendor Specific IE : FR Drone ID ──
  buf[p++] = 0xDD;
  uint16_t lenPos = p; p++;
  buf[p++] = 0x6A; buf[p++] = 0x5C; buf[p++] = 0x35;
  buf[p++] = 0x01;

  // TLV Version (type 1)
  buf[p++] = 0x01; buf[p++] = 0x01; buf[p++] = 0x01;

  // TLV ID exploitant (type 2, len 30)
  buf[p++] = 0x02; buf[p++] = 0x1E;
  memset(buf + p, 0, 30);
  uint8_t idLen = strlen(telem.id);
  if (idLen > 30) idLen = 30;
  memcpy(buf + p, telem.id, idLen);
  p += 30;

  // TLV Lat (type 4, val = deg * 1e5)
  int32_t iLat = (int32_t)(telem.lat * 1e5);
  buf[p++] = 0x04; buf[p++] = 0x04;
  beaconPutI32(buf, p, iLat);

  // TLV Lon (type 5)
  int32_t iLon = (int32_t)(telem.lon * 1e5);
  buf[p++] = 0x05; buf[p++] = 0x04;
  beaconPutI32(buf, p, iLon);

  // TLV Alt MSL (type 6)
  int16_t iAlt = (int16_t)telem.alt;
  buf[p++] = 0x06; buf[p++] = 0x02;
  buf[p++] = (iAlt >> 8) & 0xFF;
  buf[p++] = iAlt & 0xFF;

  // TLV Home Lat (type 8)
  int32_t hLat = (int32_t)(telem.homeLat * 1e5);
  buf[p++] = 0x08; buf[p++] = 0x04;
  beaconPutI32(buf, p, hLat);

  // TLV Home Lon (type 9)
  int32_t hLon = (int32_t)(telem.homeLon * 1e5);
  buf[p++] = 0x09; buf[p++] = 0x04;
  beaconPutI32(buf, p, hLon);

  // TLV Speed (type 10, val = m/s)
  buf[p++] = 0x0A; buf[p++] = 0x01; buf[p++] = telem.speed;

  // TLV Heading (type 11, val = heading * 256 / 360)
  buf[p++] = 0x0B; buf[p++] = 0x01;
  buf[p++] = (uint8_t)(telem.heading * 256 / 360);

  buf[lenPos] = (uint8_t)(p - lenPos - 1);
  return p;
}

// ── V3.3 : helpers little-endian pour ODID (standard ASTM) ──
// ── ODID PACKED STRUCTS (format fil, spec ASTM F3411-22a) ──
// Convention GCC packed LE : premier champ déclaré = bits bas

struct __attribute__((__packed__)) ODID_BasicID_enc {
  uint8_t ProtoVersion:4; uint8_t MessageType:4;
  uint8_t UAType:4;       uint8_t IDType:4;
  char UASID[20]; char Rsvd[3];
};
struct __attribute__((__packed__)) ODID_Location_enc {
  uint8_t ProtoVersion:4; uint8_t MessageType:4;
  uint8_t SpeedMult:1; uint8_t EWDirection:1;
  uint8_t HeightType:1; uint8_t LocReserved:1; uint8_t Status:4;
  uint8_t Direction; uint8_t SpeedHorizontal; int8_t SpeedVertical;
  int32_t Latitude; int32_t Longitude;
  uint16_t AltitudeBaro; uint16_t AltitudeGeo; uint16_t Height;
  uint8_t HorizAccuracy:4; uint8_t VertAccuracy:4;
  uint8_t SpeedAccuracy:4; uint8_t BaroAccuracy:4;
  uint16_t TimeStamp;
  uint8_t TSAccuracy:4; uint8_t LocReserved2:4;
  char LocReserved3;
};
struct __attribute__((__packed__)) ODID_SelfID_enc {
  uint8_t ProtoVersion:4; uint8_t MessageType:4;
  uint8_t DescType; char Desc[23];
};
struct __attribute__((__packed__)) ODID_System_enc {
  uint8_t ProtoVersion:4; uint8_t MessageType:4;
  uint8_t OperatorLocationType:2; uint8_t ClassificationType:3; uint8_t SysRsvd:3;
  int32_t OperatorLatitude; int32_t OperatorLongitude;
  uint16_t AreaCount; uint8_t AreaRadius;
  uint16_t AreaCeiling; uint16_t AreaFloor;
  uint8_t ClassEU:4; uint8_t CategoryEU:4;
  uint16_t OperatorAltitudeGeo; uint32_t Timestamp; char SysRsvd2;
};
struct __attribute__((__packed__)) ODID_OperatorID_enc {
  uint8_t ProtoVersion:4; uint8_t MessageType:4;
  uint8_t OperatorIdType; char OperatorId[20]; char OpRsvd[3];
};
struct __attribute__((__packed__)) ODID_Pack_enc {
  uint8_t ProtoVersion:4; uint8_t MessageType:4;
  uint8_t SingleMessageSize; uint8_t MsgPackSize;
  uint8_t Messages[5][25];
};

// ── ODID encode helpers ──
static int32_t odidEncLatLon(double d){return(int32_t)round(d*1e7);}
static uint16_t odidEncAlt(float m){int v=(int)round((m+1000.0f)/0.5f);return(uint16_t)(v<0?0:(v>65535?65535:v));}
static void odidEncDir(float deg,uint8_t*dir,uint8_t*ew){int d=(int)roundf(deg);if(d==360)d=0;if(d>=180){*ew=1;*dir=(uint8_t)(d-180);}else{*ew=0;*dir=(uint8_t)d;}}
static void odidEncSpeedH(float mps,uint8_t*val,uint8_t*mult){if(mps<=255*0.25f){*mult=0;*val=(uint8_t)roundf(mps/0.25f);}else{*mult=1;*val=(uint8_t)roundf((mps-63.75f)/0.75f);}}
static int8_t odidEncSpeedV(float mps){int v=(int)roundf(mps/0.5f);return(int8_t)(v<-128?-128:(v>127?127:v));}
static uint8_t odidSendCounter=0;

uint16_t buildODIDBeacon(uint8_t* buf) {
  uint16_t p = 0;
  // ── 802.11 Header (24) + Beacon (12) + SSID hidden (2) + Rates (3) ──
  buf[p++]=0x80;buf[p++]=0x00;buf[p++]=0x00;buf[p++]=0x00;
  memset(buf+p,0xFF,6);p+=6; memcpy(buf+p,txMAC,6);p+=6; memcpy(buf+p,txMAC,6);p+=6;
  buf[p++]=0x00;buf[p++]=0x00;
  uint64_t bts=(uint64_t)micros();memcpy(buf+p,&bts,8);p+=8;
  buf[p++]=0x64;buf[p++]=0x00;buf[p++]=0x20;buf[p++]=0x04;
  buf[p++]=0x00;buf[p++]=0x00;
  buf[p++]=0x01;buf[p++]=0x01;buf[p++]=0x8C;
  // ── Vendor IE ODID ──
  uint16_t packLen=3+5*25;
  buf[p++]=0xDD;buf[p++]=(uint8_t)(4+1+packLen);
  buf[p++]=0xFA;buf[p++]=0x0B;buf[p++]=0xBC;buf[p++]=0x0D;
  buf[p++]=odidSendCounter++;
  // ── MessagePack via structs packed ──
  uint8_t*ps=buf+p;memset(ps,0,packLen);
  ODID_Pack_enc*pk=(ODID_Pack_enc*)ps;
  pk->ProtoVersion=2;pk->MessageType=0xF;pk->SingleMessageSize=25;pk->MsgPackSize=5;
  uint8_t idLen=strlen(telem.id);if(idLen>20)idLen=20;
  // Basic ID
  ODID_BasicID_enc*bid=(ODID_BasicID_enc*)pk->Messages[0];
  bid->ProtoVersion=2;bid->MessageType=0;bid->IDType=telem.idType;bid->UAType=telem.uaType;
  memcpy(bid->UASID,telem.id,idLen);
  // Location
  ODID_Location_enc*loc=(ODID_Location_enc*)pk->Messages[1];
  loc->ProtoVersion=2;loc->MessageType=1;loc->Status=telem.status;loc->HeightType=0;
  uint8_t dir,ew;odidEncDir((float)telem.heading,&dir,&ew);
  loc->Direction=dir;loc->EWDirection=ew;
  uint8_t sv,sm;odidEncSpeedH((float)telem.speed,&sv,&sm);
  loc->SpeedHorizontal=sv;loc->SpeedMult=sm;
  loc->SpeedVertical=odidEncSpeedV((float)telem.vertSpeed*0.5f);
  loc->Latitude=odidEncLatLon(telem.lat);loc->Longitude=odidEncLatLon(telem.lon);
  loc->AltitudeBaro=odidEncAlt(telem.alt);loc->AltitudeGeo=odidEncAlt(telem.alt);
  loc->Height=odidEncAlt(telem.alt-telem.homeAlt);
  loc->HorizAccuracy=10;loc->VertAccuracy=3;loc->BaroAccuracy=2;loc->SpeedAccuracy=3;
  loc->TSAccuracy=2;loc->TimeStamp=(uint16_t)(((millis()/100)%36000));
  // Self-ID
  ODID_SelfID_enc*sid=(ODID_SelfID_enc*)pk->Messages[2];
  sid->ProtoVersion=2;sid->MessageType=3;sid->DescType=0;
  uint8_t dLen=strlen(telem.selfDesc);if(dLen>23)dLen=23;
  memcpy(sid->Desc,telem.selfDesc,dLen);
  // System
  ODID_System_enc*sys=(ODID_System_enc*)pk->Messages[3];
  sys->ProtoVersion=2;sys->MessageType=4;sys->OperatorLocationType=0;sys->ClassificationType=1;
  sys->OperatorLatitude=odidEncLatLon(telem.homeLat);sys->OperatorLongitude=odidEncLatLon(telem.homeLon);
  sys->AreaCount=1;sys->AreaRadius=10;sys->AreaCeiling=odidEncAlt(150.0f);sys->AreaFloor=odidEncAlt(0.0f);
  sys->CategoryEU=telem.euCategory;sys->ClassEU=telem.euClass;
  sys->OperatorAltitudeGeo=odidEncAlt(telem.homeAlt);sys->Timestamp=millis()/1000;
  // Operator ID
  ODID_OperatorID_enc*oid=(ODID_OperatorID_enc*)pk->Messages[4];
  oid->ProtoVersion=2;oid->MessageType=5;oid->OperatorIdType=0;
  memcpy(oid->OperatorId,telem.id,idLen);
  p+=packLen;
  return p;
}

void emitBeacon() {
  if (!telem.idValid) return;

  // ── V3.3 : alterner FR et ODID par secondes (10 beacons = 1s) ──
  bool useODID = ((txCount / 10) % 2) == 1;

  if (useODID) {
    beaconLen = buildODIDBeacon(beaconBuf);
  } else {
    beaconLen = buildFRBeacon(beaconBuf);
  }

  esp_err_t err = esp_wifi_80211_tx(WIFI_IF_STA, beaconBuf, beaconLen, true);
  if (err == ESP_OK) txCount++;
  else Serial.printf("[TX] err=%d\n", err);
}


// ══════════════════════════════════════════════════════════════
//  V3.3 TX — ECRAN EMISSION + START/STOP
// ══════════════════════════════════════════════════════════════

void updateTxGPS() {
  if (appState != STATE_TX_EMIT) return;
  M5.Display.fillRect(10, 95, 300, 50, C_BG);
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(TFT_GREEN, C_BG);
  M5.Display.setCursor(10, 98);
  M5.Display.printf("Lat: %.5f", telem.lat);
  M5.Display.setCursor(10, 113);
  M5.Display.printf("Lon: %.5f", telem.lon);
  M5.Display.setCursor(10, 128);
  bool useODID = ((txCount / 10) % 2) == 1;
  M5.Display.printf("Alt:%.0fm TX:%lu [%s]", telem.alt, (unsigned long)txCount, useODID ? "ODID" : "FR");
}

void drawTxScreen() {
  M5.Display.fillScreen(C_BG);

  M5.Display.setTextSize(2);
  M5.Display.setTextColor(TFT_YELLOW, C_BG);
  M5.Display.setCursor(20, 4);
  M5.Display.println("TX DRONE ID");

  M5.Display.setTextSize(1);
  M5.Display.setTextColor(TFT_CYAN, C_BG);
  M5.Display.setCursor(10, 35);
  M5.Display.printf("ID: %s", telem.id);

  M5.Display.setTextColor(TFT_GREEN, C_BG);
  M5.Display.setCursor(10, 55);
  M5.Display.println("Beacon FR + ODID alterne CH6");

  M5.Display.setTextColor(TFT_DARKGREY, C_BG);
  M5.Display.setCursor(10, 75);
  M5.Display.println("Position = centre carte BLE");

  updateTxGPS();

  // Boutons
  M5.Display.fillRoundRect(10, 180, 145, 30, 3, 0x4000);
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(TFT_RED, 0x4000);
  M5.Display.setCursor(45, 190);
  M5.Display.print("STOP TX");

  M5.Display.fillRoundRect(165, 180, 145, 30, 3, C_HDR);
  M5.Display.setTextColor(TFT_CYAN, C_HDR);
  M5.Display.setCursor(200, 190);
  M5.Display.print("EDIT ID");

  M5.Display.fillRect(0, 226, 320, 14, C_HDR);
  M5.Display.setTextColor(TFT_WHITE, C_HDR);
  M5.Display.setCursor(40, 228);
  M5.Display.printf("TX CH6 FR+ODID | BAT:%dmV", vbatAvg());
}

void startTxMode() {
  appState = STATE_TX_EMIT;
  txCount = 0;
  txSeqNum = 0;

  // Fixer la position de décollage
  telem.homeLat = telem.lat;
  telem.homeLon = telem.lon;
  telem.homeAlt = telem.alt;
  Serial.printf("[TX] Home = %.5f, %.5f, %.0f\n", telem.homeLat, telem.homeLon, telem.homeAlt);

  esp_fill_random(txMAC, 6);
  txMAC[0] = (txMAC[0] & 0xFE) | 0x02;

  esp_wifi_set_promiscuous(false);
  if (wifiAPActive) stopWiFiAP();

  // ── V3.3 TX : STA déconnecté + promiscuous pour raw TX ──
  WiFi.mode(WIFI_OFF);
  delay(100);
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  wifi_country_t country = {
    .cc = "JP", .schan = 1, .nchan = 14,
    .policy = WIFI_COUNTRY_POLICY_AUTO
  };
  esp_wifi_set_country(&country);
  esp_wifi_set_channel(6, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(true);

  lastTxBeacon = 0;

  Serial.println("[TX] Emission FR+ODID CH6 STA+promisc");
  M5.Speaker.tone(1000, 60); delay(80);
  M5.Speaker.tone(1500, 60); delay(80);
  M5.Speaker.stop();

  drawTxScreen();
}

void stopTxMode() {
  esp_wifi_set_promiscuous(false);
  WiFi.mode(WIFI_OFF);
  delay(100);

  M5.Speaker.tone(500, 100); delay(120); M5.Speaker.stop();
  Serial.println("[TX] Stop");

  appState = STATE_MODE_SELECT;
  checkTransport();
  drawModeSelect();
}


// ══════════════════════════════════════════════════════════════
//  SETUP
// ══════════════════════════════════════════════════════════════

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  Serial.begin(115200);
  delay(2000);

  M5.Display.setRotation(1);
  M5.Display.setBrightness(200);
  M5.Speaker.setVolume(128);
  M5.Power.setChargeCurrent(500);
  M5.Power.setChargeVoltage(4200);

  Serial.println();
  Serial.println("===========================================");
  Serial.println(" AH & EPERRET — Drone RX V3.5");
  Serial.println(" WiFi AP + BLE / drone.local");
  Serial.println(" Captive Portal + Waterfall SDR");
  Serial.println("===========================================");
  Serial.println();

  for (int i = 0; i < VBAT_SAMPLES; i++) vbatHistory[i] = 4200;
  for (int i = 0; i < 15; i++) channelRSSI[i] = -100;  // ── V3.3 ──
  memset(activeChannels, 0, sizeof(activeChannels));    // vidé au démarrage
  clearDrones();
  loadPilotID();  // ── V3.3 TX ──

  appState = STATE_SPLASH;
  drawSplash();

  M5.Speaker.tone(523, 80);  delay(100);
  M5.Speaker.tone(659, 80);  delay(100);
  M5.Speaker.tone(784, 80);  delay(100);
  M5.Speaker.tone(1047, 150); delay(200);
  M5.Speaker.stop();
}


// ══════════════════════════════════════════════════════════════
//  LOOP PRINCIPALE
// ══════════════════════════════════════════════════════════════

void loop() {
  M5.update();
  checkSerialInput();

  // ── Screen timeout : veille après 5 min sans BLE (mode SNIFF uniquement) ──
  if (appState == STATE_SNIFF && !screenAsleep && millis() - bleLastActivity > BLE_IDLE_SLEEP_MS) {
    M5.Display.setBrightness(0);
    screenAsleep = true;
    Serial.println("[SCREEN] Veille");
  }
  // ── Touch wake ──
  if (screenAsleep) {
    auto tw = M5.Touch.getDetail();
    if (tw.isPressed()) {
      M5.Display.setBrightness(200);
      screenAsleep = false;
      bleLastActivity = millis();
      Serial.println("[SCREEN] Reveil");
      while (true) { M5.update(); auto r = M5.Touch.getDetail(); if (!r.isPressed()) break; delay(20); }
    }
  }

  // ── V3.4 CAPTIVE PORTAL : servir DHCP+DNS+HTTP+WS dans TOUS les états ──
  if (wifiAPActive) {
    handleDHCP();
    handleDNS();
    handleHTTP();
    wsAcceptClients();
    for (int i = 0; i < MAX_WS_CLIENTS; i++)
      if (wsConnected[i] && !wsClients[i].connected())
        wsConnected[i] = false;
  }

  // ── SPLASH ──
  if (appState == STATE_SPLASH) {
    auto t = M5.Touch.getDetail();
    if (t.isPressed()) {
      M5.Speaker.tone(2000, 30); delay(50); M5.Speaker.stop();
      while (true) { M5.update(); auto r = M5.Touch.getDetail(); if (!r.isPressed()) break; delay(20); }
      appState = STATE_SCAN;
      drawScanScreen();
    }
    delay(50);
    return;
  }

  // ── SCAN WiFi ──
  if (appState == STATE_SCAN) {
    auto t = M5.Touch.getDetail();
    if (t.isPressed()) {
      M5.Speaker.tone(2000, 30); delay(50); M5.Speaker.stop();
      while (true) { M5.update(); auto r = M5.Touch.getDetail(); if (!r.isPressed()) break; delay(20); }
      checkTransport();

      // ── V3.4 : si WiFi AP active → écran portal ──
      if (wifiAPActive) {
        portalStep = PORTAL_WAIT;
        portalLastStaCount = 0;
        portalClientIP[0] = '\0';
        portalDNSHost[0] = '\0';
        appState = STATE_PORTAL;
        drawPortalScreen();
      } else {
        // Pas de WiFi AP, skip portal
        appState = STATE_HELP;
        drawHelpScreen();
      }
    }
    delay(50);
    return;
  }

  // ── V3.4 : PORTAL — monitoring client WiFi ──
  if (appState == STATE_PORTAL) {
    // Fallback : si STA connecté mais DHCP pas encore reçu
    uint8_t staCount = WiFi.softAPgetStationNum();
    if (staCount > portalLastStaCount && portalStep < PORTAL_CLIENT) {
      portalLastStaCount = staCount;
      // Attendre 500ms pour laisser le DHCP arriver avec le hostname
      // Si handleDHCP() l'a déjà mis, on ne touche pas
    }

    // Touch : continuer seulement si page servie
    if (portalStep >= PORTAL_SERVED) {
      auto t = M5.Touch.getDetail();
      if (t.isPressed()) {
        M5.Speaker.tone(2000, 30); delay(50); M5.Speaker.stop();
        while (true) { M5.update(); auto r = M5.Touch.getDetail(); if (!r.isPressed()) break; delay(20); }
        appState = STATE_HELP;
        drawHelpScreen();
      }
    }
    delay(50);
    return;
  }

  // ── V3.3 : HELP ──
  if (appState == STATE_HELP) {
    auto t = M5.Touch.getDetail();
    if (t.isPressed()) {
      M5.Speaker.tone(2000, 30); delay(50); M5.Speaker.stop();
      while (true) { M5.update(); auto r = M5.Touch.getDetail(); if (!r.isPressed()) break; delay(20); }
      appState = STATE_MODE_SELECT;
      drawModeSelect();
    }
    delay(50);
    return;
  }

  // ── MODE SELECT ──
  if (appState == STATE_MODE_SELECT) {
    auto t = M5.Touch.getDetail();
    if (t.isPressed()) {
      int y = t.y;
      M5.Speaker.tone(1500, 50); delay(80); M5.Speaker.stop();
      while (true) { M5.update(); auto r = M5.Touch.getDetail(); if (!r.isPressed()) break; delay(20); }

      if (y >= 30 && y < 70) {
        scanMode = MODE_DETECTION;
        clearDrones();
        startSniffMode();
      }
      else if (y >= 76 && y < 116) {
        scanMode = MODE_CLASSIC;
        clearDrones();
        startSniffMode();
      }
      else if (y >= 122 && y < 162) {
        // Tracking : ignoré si pas de canaux actifs (bouton grisé)
        if (countActiveChannels() > 0) {
          scanMode = MODE_TRACKING;
          clearDrones();
          startSniffMode();
        }
      }
      // ── V3.3 TX : 4ème bouton ──
      else if (y >= 168 && y < 208) {
        if (telem.idValid) {
          startTxMode();
        } else {
          kbLen = 0; kbInput[0] = '\0'; kbShift = true;
          appState = STATE_KEYBOARD;
          drawKeyboard();
        }
      }
      delay(300);
    }
    delay(50);
    return;
  }

  // ── V3.3 : KEYBOARD ──
  if (appState == STATE_KEYBOARD) {
    if (handleKeyboardTouch()) return;
    delay(50);
    return;
  }

  // ── V3.3 : TX EMIT ──
  if (appState == STATE_TX_EMIT) {
    checkSerialInput();
    checkBattery();

    if (millis() - lastTxBeacon >= 100) {
      lastTxBeacon = millis();
      emitBeacon();
      if (txCount % 10 == 0) updateTxGPS();
      if (txCount <= 3) {
        Serial.printf("[TX] len=%d lat=%.5f lon=%.5f alt=%.0f cnt=%lu\n",
          beaconLen, telem.lat, telem.lon, telem.alt, (unsigned long)txCount);
      }
    }

    auto t = M5.Touch.getDetail();
    if (t.isPressed()) {
      int tx = t.x, ty = t.y;
      while (true) { M5.update(); auto r = M5.Touch.getDetail(); if (!r.isPressed()) break; delay(20); }

      if (ty >= 180 && ty <= 210) {
        if (tx < 160) { stopTxMode(); return; }
        else {
          strncpy(kbInput, telem.id, KB_MAX_LEN);
          kbInput[KB_MAX_LEN] = '\0';
          kbLen = strlen(kbInput);
          kbShift = false;
          esp_wifi_set_promiscuous(false);
          appState = STATE_KEYBOARD;
          drawKeyboard();
          return;
        }
      }
    }
    delay(20);
    return;
  }

  // ══════════════════════════════════════════════
  //  STATE_SNIFF — boucle active
  // ══════════════════════════════════════════════

  checkBattery();

  // Touch : PWR OFF, REBOOT ou CHANGE MODE
  if (handleSniffTouch()) return;

  // Scan canaux
  switchChannel();

  // File RX
  processRxQueue();

  // ── V3.3 : file UNK waterfall ──
  processUnkQueue();

  // Waterfall tick (avance le curseur temps)
  waterfallTick();

  delay(5);
}
