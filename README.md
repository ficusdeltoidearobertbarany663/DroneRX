# DroneRX — Passive Drone Detection & Identification

**M5Stack CoreS3 (ESP32-S3) — Autonomous passive RF scanner**

DroneRX détecte et identifie les drones en temps réel par écoute passive des balises WiFi réglementaires, sans émission radio, sans cloud, sans dépendance externe.

DroneRX detects and identifies drones in real time by passively listening to regulatory WiFi beacons — no radio emission, no cloud, no external dependency.

---

## Fonctionnalités / Features

### Scanner RF passif / Passive RF Scanner
- **4 protocoles** : FR (SGDSN 6A:5C:35), ASTM F3411 ODID (FA:0B:BC), DJI DroneID (26:37:12), Parrot (90:3A:E6)
- **3 modes de scan** : Detection (canaux 1–14), Classic (1, 6, 11), Tracking (canaux actifs uniquement)
- **Waterfall SDR** temps réel sur l'écran — visualisation RSSI par canal avec code couleur par protocole
- **Beep mélodique** à chaque nouveau drone détecté

### Connectivité / Connectivity
- **WiFi Access Point** : SSID `drone.local`, captive portal avec DHCP maison (hostname client), DNS, mDNS
- **BLE (Web Bluetooth)** : lien données bidirectionnel via Nordic UART Service — scan RF 100% entre les bursts BLE
- **Zéro pairing** : connexion BLE directe, sans PIN, sans appairage

### Page HTML autonome / Standalone HTML Page
- Servie par l'ESP32 via le captive portal, sauvegardable localement pour usage hors-ligne
- **Carte Leaflet** (OpenStreetMap) avec position et trajectoire des drones
- **Onglets Info / Météo / Drones** : détail par drone, météo temps réel (Open-Meteo), données ADS-B (avions)
- **Tag "myself"** : marqueur position opérateur (GPS auto + placement manuel)
- Fonctionne hors-ligne avec données live via BLE depuis la page sauvegardée

### Émission balise / Beacon TX
- Émission de balises **FR + ODID** simultanées sur canal 6
- Saisie de l'ID exploitant via **clavier tactile** sur l'écran
- Stockage NVS persistant de l'ID

### Écran & interface / Display & UI
- Écran tactile avec navigation par états : Splash → Scan WiFi → Portal → Help → Mode Select → Sniff
- Affichage batterie, transport actif (BLE/WiFi), compteurs par protocole
- Veille automatique après 5 min d'inactivité BLE, réveil au toucher
- Boutons tactiles : Power Off, Reboot, Mode

---

## Architecture

```
M5Stack CoreS3
├── WiFi AP (drone.local)        → sert la page HTML + captive portal
│   ├── DHCP maison              → attribution IP + hostname client
│   ├── DNS catchall             → redirection captive portal
│   └── HTTP/WebSocket           → page Leaflet + données temps réel
│
├── BLE (Nordic UART)            → données drone vers navigateur (Web Bluetooth)
│   └── Scan RF entre bursts     → pas de perte de détection
│
├── Mode promiscuous             → écoute passive canaux WiFi
│   ├── Parsing OUI 4 protocoles → identification drone
│   └── Waterfall RSSI           → visualisation spectre en temps réel
│
└── Beacon TX (FR+ODID)          → émission balise identification
```

## Fichiers / Files

| Fichier | Rôle |
|---|---|
| `drone_rx_v3.ino` | Firmware complet — scanner, AP, BLE, captive portal, waterfall, TX |
| `drone_page.h` | Page HTML/JS/CSS embarquée (PROGMEM) — carte Leaflet, météo, ADS-B |

## Matériel / Hardware

- **M5Stack CoreS3** (ESP32-S3, écran tactile 320×240, batterie, speaker)
- Aucun matériel additionnel requis

## Compilation

- **Board** : ESP32S3 Dev Module
- **USB CDC** : Enabled
- **PSRAM** : OPI
- **Flash** : 16MB QIO
- **Librairies** : M5Unified, ESPmDNS, BLE (built-in Arduino ESP32)

## Usage

1. Flash le firmware sur M5Stack CoreS3
2. L'appareil démarre en écran splash — toucher pour continuer
3. Connexion WiFi au réseau `drone.local` (mot de passe : `drone.local`)
4. Le captive portal redirige vers la page de détection
5. Sauvegarder la page HTML localement (Ctrl+S / menu navigateur)
6. Passer en mode BLE (toucher l'écran) → la page sauvegardée reçoit les données live via Web Bluetooth
7. Choisir le mode de scan : Detection, Classic ou Tracking

---

**AH & EPERRET** — [eperret@azs.fr](mailto:eperret@azs.fr)
