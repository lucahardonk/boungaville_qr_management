/**
 ******************************************************************************
 * @file     ETH_WebServer_WebOTA.ino
 * @brief    W5500 Ethernet web server with authentication, clock, Web OTA, and NVS key storage
 * @version  V4.0
 * @date     2025-12-24
 * @author   Your Name
 * @license  MIT
 ******************************************************************************
 * 
 * Features:
 * - W5500 Ethernet connectivity with DHCP
 * - Session-based authentication system
 * - Web server with live clock (HTTP time sync)
 * - Automatic Italian DST handling (CET/CEST)
 * - Secure Web-based OTA firmware updates (session-protected)
 * - NVS key-value storage (up to 100 strings, 128 chars each)
 * - Auto-assigned keys (k0, k1, k2...)
 * - REST API for key management
 * - Clean, modular code structure
 * 
 * Hardware Requirements:
 * - ESP32-S3 board
 * - W5500 Ethernet module
 * 
 * Configuration:
 * - Partition Scheme: Must use an OTA-enabled partition (e.g., "Minimal SPIFFS with OTA")
 * - Change SYSTEM_NAME and ADMIN_PASSWORD before deployment
 * 
 * ============================================================================
 * API Usage Examples (from Linux terminal):
 * ============================================================================
 * 
 * 1. Insert a QR code (auto-assigns key like k0, k1, k2...):
 *    curl -X POST http://192.168.1.97/api/insert \
 *      -H "Content-Type: application/json" \
 *      -d '{"value":"table-5-boungaville"}'
 * 
 * 2. Print all stored QR codes:
 *    curl http://192.168.1.97/api/print
 * 
 * 3. Remove a QR code by value:
 *    curl -X POST http://192.168.1.97/api/remove \
 *      -H "Content-Type: application/json" \
 *      -d '{"value":"table-5-boungaville"}'
 * 
 * 4. Pretty print with jq:
 *    curl http://192.168.1.97/api/print | jq
 * 
 * 5. Get current Italian time with DST info:
 *    curl http://192.168.1.97/api/time
 * 
 * 6. Get current Italian time (pretty printed):
 *    curl http://192.168.1.97/api/time | jq
 * 
 * Note: Replace 192.168.1.97 with your ESP32's actual IP address
 * 
 ******************************************************************************
 */

#include <SPI.h>
#include "Functions.h"

/* ====== GLOBAL VARIABLE DEFINITIONS ====== */

// System Configuration
const char* SYSTEM_NAME = "QRCode porta ingresso blindata";  // ⚠️ CHANGE THIS FOR YOUR DEPLOYMENT
const char* ADMIN_PASSWORD = "admin123";         // ⚠️ CHANGE THIS IN PRODUCTION!

// Network Configuration
byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xAC };

// Global Objects
EthernetServer server(SERVER_PORT);
Preferences prefs;
bool timeIsSynced = false;
Session currentSession;

// QR Scanner Variables
HardwareSerial QRSerial(2);
String qrBuffer = "";
String lastScannedQR = "";
unsigned long lastScanTime = 0;

/* ====== SETUP ====== */

void setup() {
  Serial.begin(115200);
  delay(500);
  
  Serial.println("\n\n========================================");
  Serial.println("ESP32-S3 Ethernet Web Server with Auth + OTA + NVS");
  Serial.println("========================================\n");

  // Initialize random seed for session IDs
  randomSeed(analogRead(0));

  // Initialize session
  currentSession.sessionId = "";
  currentSession.isAuthenticated = false;
  currentSession.lastActivity = 0;

  // Initialize Relay Pin
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);
  Serial.println("[INIT] Relay initialized");

  // Initialize UART2 for GM65 QR Scanner
  QRSerial.begin(9600, SERIAL_8N1, GM65_RX, GM65_TX);
  Serial.println("[INIT] UART2 initialized for GM65 QR Scanner");
  Serial.print("[INIT] GM65 RX: GPIO");
  Serial.print(GM65_RX);
  Serial.print(", GM65 TX: GPIO");
  Serial.println(GM65_TX);

  // Initialize NVS
  prefs.begin("keys", false);
  Serial.print("[INIT] NVS initialized. Keys stored: ");
  Serial.println(countKeys());

  // Initialize SPI
  SPI.begin(W5500_SCK, W5500_MISO, W5500_MOSI, W5500_CS);
  Serial.println("[INIT] SPI initialized");

  // Initialize Ethernet
  Ethernet.init(W5500_CS);
  Serial.println("[INIT] Attempting DHCP...");
  
  if (Ethernet.begin(mac) == 0) {
    Serial.println("[ERROR] DHCP failed!");
    while (true) {
      delay(1000);
    }
  }

  Serial.print("[OK] IP Address: ");
  Serial.println(Ethernet.localIP());
  
  // Start web server
  server.begin();
  Serial.println("[OK] Web server started");
  
  // Initialize HTTP time synchronization
  initTime();
  
  Serial.println("\n========================================");
  Serial.print("System Name:   ");
  Serial.println(SYSTEM_NAME);
  Serial.print("Web Interface: http://");
  Serial.println(Ethernet.localIP());
  Serial.println("========================================\n");
}

/* ====== MAIN LOOP ====== */

void loop() {
  // Check for QR code scans
  String qrCode = readQR();
  if (qrCode.length() > 0) {
    // Check if QR code starts with "https:"
    if (qrCode.startsWith("https:")) {
      Serial.println("[QR] URL-type QR code detected, searching in storage...");
      
      bool found = false;
      // Search through all stored keys
      for (int i = 0; i < MAX_KEYS; i++) {
        String key = "k" + String(i);
        if (prefs.isKey(key.c_str())) {
          String storedValue = prefs.getString(key.c_str(), "");
          
          // Check if stored value also starts with "https:"
          if (storedValue.startsWith("https:")) {
            // Compare entire strings
            if (storedValue == qrCode) {
              Serial.println("[QR] ✓ Match found!");
              Serial.print("[QR] Key: ");
              Serial.print(key);
              Serial.print(" = ");
              Serial.println(storedValue);
              
              toggleRelay();
              found = true;
              break;
            }
          }
        }
      }
      
      if (!found) {
        Serial.println("[QR] ✗ No match found in storage");
        Serial.print("[QR] Scanned: ");
        Serial.println(qrCode);
      }
    } else if (qrCode.indexOf('|') != -1) {
      // Pipe-delimited QR code (name|surname|dateIn|dateOut)
      Serial.println("[QR] Pipe-delimited QR code detected, validating...");
      
      if (validQrcode(qrCode)) {
        toggleRelay();
      }
    } else {
      // Unknown format QR code, do nothing
      Serial.println("[QR] Unknown format QR code detected, ignoring");
      Serial.print("[QR] Scanned: ");
      Serial.println(qrCode);
    }
  }
  
  // Periodic time re-sync (every 1 hour)
  static unsigned long lastSync = 0;
  if (millis() - lastSync > 3600000) {
    if (!timeIsSynced) {
      Serial.println("[TIME] Attempting re-sync...");
      if (syncTimeViaHTTP()) {
        Serial.println("[TIME] Re-sync successful");
      }
    }
    lastSync = millis();
  }

  // Handle web server requests
  EthernetClient client = server.available();
  if (!client) return;

  // Parse HTTP request
  bool currentLineIsBlank = true;
  String currentLine = "";
  String requestPath = "";
  String requestMethod = "";
  int contentLength = 0;
  String contentType = "";
  String cookieHeader = "";

  while (client.connected()) {
    if (!client.available()) continue;
    
    char c = client.read();

    if (c != '\n' && c != '\r') {
      currentLine += c;
    }

    // Parse request line
    if (currentLine.startsWith("GET ") || currentLine.startsWith("POST ") || currentLine.startsWith("DELETE ")) {
      int firstSpace = currentLine.indexOf(' ');
      int secondSpace = currentLine.indexOf(' ', firstSpace + 1);
      if (secondSpace > 0) {
        requestMethod = currentLine.substring(0, firstSpace);
        requestPath = currentLine.substring(firstSpace + 1, secondSpace);
        
        // Remove query string
        int qPos = requestPath.indexOf('?');
        if (qPos >= 0) {
          requestPath = requestPath.substring(0, qPos);
        }
      }
    }

    // Parse headers
    if (currentLine.startsWith("Content-Length: ")) {
      contentLength = currentLine.substring(16).toInt();
    }
    if (currentLine.startsWith("Content-Type: ")) {
      contentType = currentLine.substring(14);
      contentType.trim();
    }
    if (currentLine.startsWith("Cookie: ")) {
      cookieHeader = currentLine.substring(8);
      cookieHeader.trim();
    }

    // End of headers
    if (c == '\n' && currentLineIsBlank) {
      // Extract session ID from cookie
      String sessionId = extractCookie(cookieHeader, "sessionId");

      // Route handling
      if (requestMethod == "GET") {
        if (requestPath == "/" || requestPath == "") {
          if (isSessionValid(sessionId)) {
            serveDashboard(client);
          } else {
            serveLoginPage(client);
          }
        } else if (requestPath == "/logout") {
          handleLogout(client);
        } else if (requestPath.startsWith("/api/")) {
          // API endpoints (no auth required for external access)
          if (requestPath == "/api/keys") {
            handleAPIGetKeys(client);
          } else if (requestPath == "/api/time") {
            handleAPIGetTime(client);
          } else if (requestPath == "/api/print") {
            handleAPIPrint(client);
          } else if (requestPath == "/api/lastscan") {  
              handleAPILastScan(client);  
          }
          else {
            serve404(client);
          }
        } else {
          serve404(client);
        }
      } else if (requestMethod == "POST") {
        if (requestPath == "/login") {
          handleLogin(client, contentLength);
        } else if (requestPath == "/doupdate") {
          if (isSessionValid(sessionId)) {
            handleOTAUpload(client, contentLength, contentType, sessionId);
          } else {
            serveUnauthorized(client);
          }
        } else if (requestPath.startsWith("/api/")) {
          // API endpoints
          if (requestPath == "/api/keys") {
            handleAPIAddKey(client, contentLength);
          } else if (requestPath == "/api/insert") {
            handleAPIInsert(client, contentLength);
          } else if (requestPath == "/api/remove") {
            handleAPIRemove(client, contentLength);
          } else {
            serve404(client);
          }
        } else {
          serve404(client);
        }
      } else if (requestMethod == "DELETE") {
        if (requestPath == "/api/keys") {
          handleAPIDeleteKey(client, contentLength);
        } else {
          serve404(client);
        }
      } else {
        serve404(client);
      }

      break;
    }

    if (c == '\n') {
      currentLineIsBlank = true;
      currentLine = "";
    } else if (c != '\r') {
      currentLineIsBlank = false;
    }
  }

  delay(1);
  client.stop();
} 