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
#include <Ethernet.h>
#include <Update.h>
#include <Preferences.h>
#include <time.h>
#include <sys/time.h>

/* ====== CONFIGURATION ====== */

// System Configuration
const char* SYSTEM_NAME = "Restaurant Manager";  // ⚠️ CHANGE THIS FOR YOUR DEPLOYMENT
const char* ADMIN_PASSWORD = "admin123";         // ⚠️ CHANGE THIS IN PRODUCTION!

// W5500 Pin Configuration
#define W5500_CS    14
#define W5500_RST    9
#define W5500_INT   10
#define W5500_MISO  12
#define W5500_MOSI  11
#define W5500_SCK   13

// Network Configuration
byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };

// Server Configuration
#define SERVER_PORT 80

// NVS Configuration
#define MAX_KEYS 100
#define MAX_VALUE_LENGTH 128

// Session Configuration
#define SESSION_TIMEOUT 3600000  // 1 hour in milliseconds

/* ====== SESSION MANAGEMENT ====== */

struct Session {
  String sessionId;
  unsigned long lastActivity;
  bool isAuthenticated;
};

Session currentSession;

String generateSessionId() {
  String sessionId = "";
  for (int i = 0; i < 32; i++) {
    sessionId += String(random(0, 16), HEX);
  }
  return sessionId;
}

bool isSessionValid(const String &sessionId) {
  if (sessionId.length() == 0) return false;
  if (sessionId != currentSession.sessionId) return false;
  if (!currentSession.isAuthenticated) return false;
  
  unsigned long now = millis();
  if (now - currentSession.lastActivity > SESSION_TIMEOUT) {
    currentSession.isAuthenticated = false;
    return false;
  }
  
  currentSession.lastActivity = now;
  return true;
}

void createSession() {
  currentSession.sessionId = generateSessionId();
  currentSession.lastActivity = millis();
  currentSession.isAuthenticated = true;
  
  Serial.print("[AUTH] New session created: ");
  Serial.println(currentSession.sessionId);
}

void destroySession() {
  currentSession.sessionId = "";
  currentSession.isAuthenticated = false;
  Serial.println("[AUTH] Session destroyed");
}

/* ====== GLOBAL VARIABLES ====== */

EthernetServer server(SERVER_PORT);
Preferences prefs;
bool timeIsSynced = false;

/* ====== FUNCTION PROTOTYPES ====== */

// HTTP Handlers
void serveLoginPage(EthernetClient &client);
void serveDashboard(EthernetClient &client);
void handleLogin(EthernetClient &client, int contentLength);
void handleLogout(EthernetClient &client);
void handleOTAUpload(EthernetClient &client, int contentLength, const String &contentType, const String &sessionId);
void handleAPIGetKeys(EthernetClient &client);
void handleAPIAddKey(EthernetClient &client, int contentLength);
void handleAPIDeleteKey(EthernetClient &client, int contentLength);
void handleAPIInsert(EthernetClient &client, int contentLength);
void handleAPIRemove(EthernetClient &client, int contentLength);
void handleAPIPrint(EthernetClient &client);
void handleAPIGetTime(EthernetClient &client);
void serve404(EthernetClient &client);
void serveUnauthorized(EthernetClient &client);
void sendUpdateError(EthernetClient &client, const char *msg);
void sendJSON(EthernetClient &client, int statusCode, const String &json);
bool syncTimeViaHTTP();
bool isDST(int year, int month, int day, int hour);

// Helper Functions
bool readLine(EthernetClient &client, String &out);
bool findMultipartField(EthernetClient &client, const String &boundary, const String &fieldName);
String extractCookie(const String &cookieHeader, const String &cookieName);
String urlDecode(String str);
String htmlEscape(String str);
int countKeys();
String getKeyByIndex(int index);

/* ====== TIME SYNC FUNCTIONS ====== */

bool isDST(int year, int month, int day, int hour) {
  if (month < 3 || month > 10) return false;
  if (month > 3 && month < 10) return true;
  
  int a = (14 - month) / 12;
  int y = year - a;
  int m = month + 12 * a - 2;
  int dayOfWeek1st = (1 + y + y/4 - y/100 + y/400 + (31*m)/12) % 7;
  
  int lastSunday = 31 - ((dayOfWeek1st + 30) % 7);
  if (month == 3) lastSunday = 31 - ((dayOfWeek1st + 30) % 7);
  if (month == 10) lastSunday = 31 - ((dayOfWeek1st + 30) % 7);
  
  if (month == 3) {
    if (day < lastSunday) return false;
    if (day > lastSunday) return true;
    if (hour < 2) return false;
    return true;
  }
  
  if (month == 10) {
    if (day < lastSunday) return true;
    if (day > lastSunday) return false;
    if (hour < 3) return true;
    return false;
  }
  
  return false;
}

bool syncTimeViaHTTP() {
  Serial.println("[TIME] Attempting HTTP time sync...");
  
  EthernetClient httpClient;
  const char* timeServer = "www.google.com";
  
  if (httpClient.connect(timeServer, 80)) {
    Serial.println("[TIME] Connected to time server");
    
    httpClient.println("HEAD / HTTP/1.1");
    httpClient.print("Host: ");
    httpClient.println(timeServer);
    httpClient.println("Connection: close");
    httpClient.println();
    
    unsigned long timeout = millis();
    while (httpClient.connected() && !httpClient.available()) {
      if (millis() - timeout > 5000) {
        Serial.println("[TIME] HTTP timeout");
        httpClient.stop();
        return false;
      }
      delay(10);
    }
    
    while (httpClient.available()) {
      String line = httpClient.readStringUntil('\n');
      line.trim();
      
      if (line.startsWith("Date: ")) {
        Serial.print("[TIME] Received: ");
        Serial.println(line);
        
        int firstComma = line.indexOf(',');
        if (firstComma < 0) continue;
        
        String dateTime = line.substring(firstComma + 2);
        dateTime.trim();
        
        struct tm timeinfo = {0};
        char monthStr[4];
        
        int parsed = sscanf(dateTime.c_str(), "%d %3s %d %d:%d:%d",
                           &timeinfo.tm_mday,
                           monthStr,
                           &timeinfo.tm_year,
                           &timeinfo.tm_hour,
                           &timeinfo.tm_min,
                           &timeinfo.tm_sec);
        
        if (parsed == 6) {
          const char* months[] = {"Jan","Feb","Mar","Apr","May","Jun",
                                  "Jul","Aug","Sep","Oct","Nov","Dec"};
          for (int i = 0; i < 12; i++) {
            if (strcmp(monthStr, months[i]) == 0) {
              timeinfo.tm_mon = i;
              break;
            }
          }
          
          timeinfo.tm_year -= 1900;
          timeinfo.tm_isdst = 0;
          
          int year = timeinfo.tm_year + 1900;
          int month = timeinfo.tm_mon + 1;
          int day = timeinfo.tm_mday;
          
          long days = 0;
          for (int y = 1970; y < year; y++) {
            days += (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0)) ? 366 : 365;
          }
          
          int daysInMonth[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
          bool isLeap = (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
          if (isLeap) daysInMonth[1] = 29;
          
          for (int m = 1; m < month; m++) {
            days += daysInMonth[m - 1];
          }
          
          days += day - 1;
          
          time_t gmtEpoch = days * 86400L + 
                           timeinfo.tm_hour * 3600L + 
                           timeinfo.tm_min * 60L + 
                           timeinfo.tm_sec;
          
          bool inDST = isDST(year, month, day, timeinfo.tm_hour);
          
          int offsetHours = inDST ? 2 : 1;
          time_t italianEpoch = gmtEpoch + (offsetHours * 3600);
          
          struct timeval tv = { .tv_sec = italianEpoch, .tv_usec = 0 };
          settimeofday(&tv, NULL);
          
          struct tm italianTime;
          time_t displayTime = italianEpoch;
          gmtime_r(&displayTime, &italianTime);
          
          char timeStr[64];
          snprintf(timeStr, sizeof(timeStr), "%04d-%02d-%02d %02d:%02d:%02d %s",
                   italianTime.tm_year + 1900,
                   italianTime.tm_mon + 1,
                   italianTime.tm_mday,
                   italianTime.tm_hour,
                   italianTime.tm_min,
                   italianTime.tm_sec,
                   inDST ? "CEST (UTC+2)" : "CET (UTC+1)");
          
          Serial.print("[TIME] ✓ Time set via HTTP: ");
          Serial.println(timeStr);
          
          httpClient.stop();
          timeIsSynced = true;
          return true;
        }
      }
    }
    
    httpClient.stop();
  } else {
    Serial.println("[TIME] Failed to connect to time server");
  }
  
  return false;
}

void initTime() {
  Serial.println("[TIME] Initializing time synchronization...");
  
  if (syncTimeViaHTTP()) {
    Serial.println("[TIME] ✓ Time synchronized successfully");
  } else {
    Serial.println("[TIME] ✗ Time sync failed");
    Serial.println("[TIME] Clock will show incorrect time until sync succeeds");
  }
}

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
  
  EthernetClient client = server.available();
  if (!client) return;

  Serial.println("[CLIENT] New connection");
  
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
      
      Serial.print("[REQUEST] ");
      Serial.print(requestMethod);
      Serial.print(" ");
      Serial.println(requestPath);

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
          } else {
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
  Serial.println("[CLIENT] Disconnected\n");
}

/* ====== HTTP HANDLERS ====== */

void serveLoginPage(EthernetClient &client) {
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: text/html");
  client.println("Connection: close");
  client.println();
  
  client.print("<!DOCTYPE HTML><html><head><meta charset='UTF-8'>");
  client.print("<meta name='viewport' content='width=device-width, initial-scale=1.0'>");
  client.print("<title>");
  client.print(SYSTEM_NAME);
  client.print("</title><style>");
  client.flush();
  
  client.print("*{margin:0;padding:0;box-sizing:border-box}");
  client.print("body{font-family:'Segoe UI',Tahoma,Geneva,Verdana,sans-serif;background:linear-gradient(135deg,#667eea 0%,#764ba2 100%);min-height:100vh;display:flex;align-items:center;justify-content:center;padding:20px}");
  client.print(".container{background:white;padding:60px;border-radius:20px;box-shadow:0 20px 60px rgba(0,0,0,0.3);max-width:550px;width:100%}");  client.flush();
  
  client.print("h1{color:#333;font-size:38px;margin-bottom:10px;text-align:center}");
  client.print("p{text-align:center;color:#666;margin:10px 0;font-size:16px}");
  client.print(".status-bar{display:flex;justify-content:space-between;align-items:center;margin:30px 0;padding:15px;background:#f8f9fa;border-radius:10px}");
  client.flush();
  
  client.print(".status-item{display:flex;align-items:center;gap:8px;font-size:14px;color:#495057}");
  client.print(".badge{display:inline-block;padding:5px 12px;background:#28a745;color:white;border-radius:20px;font-size:12px;font-weight:600}");
  client.print("#clock{font-size:24px;font-weight:bold;color:#667eea;font-family:'Courier New',monospace}");
  client.flush();
  
  client.print(".dst-badge{display:inline-block;padding:5px 12px;background:#ffc107;color:#333;border-radius:20px;font-size:11px;font-weight:600;margin-top:10px}");
  client.print(".login-form{margin-top:30px}");
  client.print(".form-group{margin-bottom:20px}");
  client.flush();
  
  client.print("label{display:block;margin-bottom:8px;color:#333;font-weight:600;font-size:14px}");
  client.print("input[type=password]{width:100%;padding:15px;border:2px solid #e0e0e0;border-radius:10px;font-size:14px;transition:border-color 0.3s}");
  client.print("input[type=password]:focus{outline:none;border-color:#667eea}");
  client.flush();
  
  client.print(".btn-login{width:100%;background:linear-gradient(135deg,#667eea 0%,#764ba2 100%);color:white;padding:18px;border:none;border-radius:50px;cursor:pointer;font-size:16px;font-weight:600;transition:transform 0.2s,box-shadow 0.2s;box-shadow:0 4px 15px rgba(102,126,234,0.4)}");
  client.print(".btn-login:hover{transform:translateY(-2px);box-shadow:0 6px 20px rgba(102,126,234,0.6)}");
  client.print(".error{background:#f8d7da;border:2px solid #f5c6cb;color:#721c24;padding:12px;border-radius:10px;margin-bottom:20px;font-size:14px;text-align:center;display:none}");
  client.print("</style></head><body><div class='container'>");
  client.flush();
  
  client.print("<h1>🌐 Welcome to<br>");
  client.print(SYSTEM_NAME);
  client.print("</h1>");
  client.print("<p>Powered by ESP32-S3 & W5500</p>");
  client.flush();
  
  client.print("<div class='status-bar'>");
  client.print("<div class='status-item'><span class='badge'>✓ Online</span></div>");
  client.print("<div class='status-item'><div id='clock'>--:--:--</div></div>");
  client.print("</div>");
  client.print("<div id='dstBadge' style='text-align:center;display:none'></div>");
  client.flush();
  
  client.print("<form class='login-form' method='POST' action='/login' onsubmit='return validateForm()'>");
  client.print("<div id='errorMsg' class='error'></div>");
  client.print("<div class='form-group'>");
  client.print("<label for='password'>🔒 Password</label>");
  client.print("<input type='password' id='password' name='password' placeholder='Enter password' required autofocus>");
  client.print("</div>");
  client.print("<button type='submit' class='btn-login'>Login</button>");
  client.print("</form>");
  client.flush();
  
  client.print("<script>");
  client.print("async function updateClock(){");
  client.print("try{");
  client.print("const res=await fetch('/api/time');");
  client.print("const data=await res.json();");
  client.print("if(data.success && data.synced){");
  client.print("document.getElementById('clock').textContent=data.time;");
  client.print("const badge=document.getElementById('dstBadge');");
  client.print("if(data.dst){");
  client.print("badge.innerHTML='<span class=\"dst-badge\">☀️ CEST (UTC+2)</span>';");
  client.print("badge.style.display='block';");
  client.print("}else{");
  client.print("badge.innerHTML='<span class=\"dst-badge\" style=\"background:#6c757d;color:white\">❄️ CET (UTC+1)</span>';");
  client.print("badge.style.display='block';");
  client.print("}");
  client.print("}else{");
  client.print("document.getElementById('clock').textContent='Syncing...';");
  client.print("}");
  client.print("}catch(e){");
  client.print("document.getElementById('clock').textContent='--:--:--';");
  client.print("}");
  client.print("}");
  client.flush();
  
  client.print("function validateForm(){");
  client.print("const pwd=document.getElementById('password').value;");
  client.print("if(!pwd){");
  client.print("showError('Please enter password');");
  client.print("return false;");
  client.print("}");
  client.print("return true;");
  client.print("}");
  client.flush();
  
  client.print("function showError(msg){");
  client.print("const err=document.getElementById('errorMsg');");
  client.print("err.textContent=msg;");
  client.print("err.style.display='block';");
  client.print("}");
  client.flush();
  
  client.print("setInterval(updateClock,1000);");
  client.print("updateClock();");
  
  // Check for error parameter in URL
  client.print("const urlParams=new URLSearchParams(window.location.search);");
  client.print("if(urlParams.get('error')==='1'){");
  client.print("showError('Invalid password. Please try again.');");
  client.print("}");
  
  client.print("</script>");
  client.print("</div></body></html>");
  client.flush();
}

void serveDashboard(EthernetClient &client) {
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: text/html");
  client.println("Connection: close");
  client.println();
  
  client.print("<!DOCTYPE HTML><html><head><meta charset='UTF-8'>");
  client.print("<meta name='viewport' content='width=device-width,initial-scale=1.0'>");
  client.print("<title>");
  client.print(SYSTEM_NAME);
  client.print(" - Dashboard</title><style>");
  client.flush();
  
  client.print("*{margin:0;padding:0;box-sizing:border-box}");
  client.print("body{font-family:'Segoe UI',Tahoma,Geneva,Verdana,sans-serif;background:linear-gradient(135deg,#667eea 0%,#764ba2 100%);min-height:100vh;padding:20px}");
  client.print(".header{background:white;padding:20px 30px;border-radius:15px;box-shadow:0 4px 15px rgba(0,0,0,0.1);margin-bottom:20px;display:flex;justify-content:space-between;align-items:center;flex-wrap:wrap;gap:15px}");
  client.flush();
  
  client.print(".header-left h1{color:#333;font-size:24px;margin-bottom:5px}");
  client.print(".header-left p{color:#666;font-size:14px}");
  client.print(".header-right{display:flex;gap:15px;align-items:center}");
  client.print(".btn{padding:10px 20px;border:none;border-radius:8px;cursor:pointer;font-size:14px;font-weight:600;transition:all 0.2s;text-decoration:none;display:inline-block}");
  client.flush();
  
  client.print(".btn-logout{background:#dc3545;color:white}");
  client.print(".btn-logout:hover{background:#c82333;transform:translateY(-2px)}");
  client.print(".main-container{display:grid;grid-template-columns:1fr 300px;gap:20px;max-width:1400px;margin:0 auto}");
  client.flush();
  
  client.print("@media(max-width:1024px){.main-container{grid-template-columns:1fr}}");
  client.print(".panel{background:white;padding:30px;border-radius:15px;box-shadow:0 4px 15px rgba(0,0,0,0.1)}");
  client.print(".panel-title{font-size:20px;color:#333;margin-bottom:20px;display:flex;align-items:center;gap:10px}");
  client.flush();
  
  client.print(".keys-table{width:100%;border-collapse:collapse;margin-top:15px}");
  client.print(".keys-table th{background:#667eea;color:white;padding:12px;text-align:left;font-weight:600;font-size:14px}");
  client.print(".keys-table td{padding:12px;border-bottom:1px solid #e0e0e0;font-size:13px}");
  client.print(".keys-table tr:hover{background:#f8f9fa}");
  client.flush();
  
  client.print(".key-value{font-family:'Courier New',monospace;color:#495057;word-break:break-all}");
  client.print(".btn-danger{background:#dc3545;color:white;padding:6px 12px;font-size:12px}");
  client.print(".btn-danger:hover{background:#c82333}");
  client.flush();
  
  client.print(".add-form{background:#f8f9fa;padding:20px;border-radius:10px;margin-bottom:20px}");
  client.print(".form-row{display:flex;gap:10px;margin-bottom:10px}");
  client.print("input[type=text]{flex:1;padding:10px;border:2px solid #e0e0e0;border-radius:8px;font-size:14px}");
  client.print("input[type=text]:focus{outline:none;border-color:#667eea}");
  client.flush();
  
  client.print(".btn-primary{background:linear-gradient(135deg,#667eea 0%,#764ba2 100%);color:white}");
  client.print(".btn-primary:hover{transform:translateY(-2px);box-shadow:0 4px 15px rgba(102,126,234,0.4)}");
  client.print(".char-counter{font-size:12px;color:#999;margin-top:5px}");
  client.print(".char-counter.warning{color:#ffc107}");
  client.print(".char-counter.error{color:#dc3545}");
  client.flush();
  
  client.print(".sidebar{display:flex;flex-direction:column;gap:20px}");
  client.print(".info-card{background:white;padding:20px;border-radius:15px;box-shadow:0 4px 15px rgba(0,0,0,0.1)}");
  client.print(".info-card h3{font-size:16px;color:#333;margin-bottom:15px}");
  client.print(".info-item{display:flex;justify-content:space-between;padding:10px 0;border-bottom:1px solid #e0e0e0;font-size:14px}");
  client.print(".info-item:last-child{border-bottom:none}");
  client.flush();
  
  client.print(".info-label{color:#666}");
  client.print(".info-value{color:#333;font-weight:600}");
  client.print("#clock{font-size:20px;font-weight:bold;color:#667eea;font-family:'Courier New',monospace}");
  client.print(".ota-section{background:linear-gradient(135deg,#667eea15 0%,#764ba215 100%);padding:20px;border-radius:10px;border:2px solid #667eea30}");
  client.flush();
  
  client.print(".ota-section h3{font-size:16px;color:#333;margin-bottom:15px;text-align:center}");
  client.print("input[type=file]{width:100%;padding:10px;border:2px solid #e0e0e0;border-radius:8px;font-size:13px;margin-bottom:10px}");
  client.print(".btn-upload{width:100%;background:linear-gradient(135deg,#667eea 0%,#764ba2 100%);color:white;padding:12px;border:none;border-radius:8px;cursor:pointer;font-size:14px;font-weight:600}");
  client.print(".btn-upload:hover{transform:translateY(-2px);box-shadow:0 4px 15px rgba(102,126,234,0.4)}");
  client.flush();
  
  client.print(".empty-state{text-align:center;padding:40px 20px;color:#999}");
  client.print(".empty-state-icon{font-size:48px;margin-bottom:15px}");
  client.print(".badge{display:inline-block;padding:5px 10px;background:#28a745;color:white;border-radius:15px;font-size:11px;font-weight:600}");
  client.print(".dst-badge{background:#ffc107;color:#333;padding:4px 10px;border-radius:15px;font-size:11px;font-weight:600;margin-left:10px}");
  client.print("</style></head><body>");
  client.flush();
  
  client.print("<div class='header'>");
  client.print("<div class='header-left'>");
  client.print("<h1>🔑 ");
  client.print(SYSTEM_NAME);
  client.print("</h1>");
  client.print("<p>QR Code Management Dashboard</p>");
  client.print("</div>");
  client.print("<div class='header-right'>");
  client.print("<span class='badge'>✓ Authenticated</span>");
  client.print("<a href='/logout' class='btn btn-logout'>Logout</a>");
  client.print("</div>");
  client.print("</div>");
  client.flush();
  
  client.print("<div class='main-container'>");
  client.print("<div class='panel'>");
  client.print("<div class='panel-title'>📋 QR Codes Storage</div>");
  client.flush();
  
  client.print("<div class='add-form'>");
  client.print("<h3 style='margin-bottom:12px;color:#333;font-size:15px'>➕ Add New QR Code</h3>");
  client.print("<div class='form-row'>");
  client.print("<input type='text' id='newValue' placeholder='Enter value (max 128 chars)' maxlength='128' oninput='updateCounter()'>");
  client.print("<button class='btn btn-primary' onclick='addKey()'>Add</button>");
  client.print("</div>");
  client.print("<div class='char-counter' id='charCounter'>0 / 128 characters</div>");
  client.print("</div>");
  client.flush();
  
  client.print("<div id='keysContainer'>");
  int keyCount = countKeys();
  if (keyCount == 0) {
    client.print("<div class='empty-state'>");
    client.print("<div class='empty-state-icon'>📭</div>");
    client.print("<p>No QR codes stored yet. Add your first one above!</p>");
    client.print("</div>");
  } else {
    client.print("<table class='keys-table'>");
    client.print("<thead><tr><th style='width:15%'>Key</th><th style='width:70%'>Value</th><th style='width:15%;text-align:center'>Action</th></tr></thead>");
    client.print("<tbody id='keysTableBody'></tbody>");
    client.print("</table>");
  }
  client.print("</div>");
  client.print("</div>");
  client.flush();
  
  client.print("<div class='sidebar'>");
  client.print("<div class='info-card'>");
  client.print("<h3>ℹ️ System Information</h3>");
  client.print("<div class='info-item'>");
  client.print("<span class='info-label'>IP Address</span>");
  client.print("<span class='info-value'>");
  client.print(Ethernet.localIP());
  client.print("</span>");
  client.print("</div>");
  client.flush();
  
  client.print("<div class='info-item'>");
  client.print("<span class='info-label'>Time</span>");
  client.print("<span class='info-value'><span id='clock'>--:--:--</span><span id='dstBadge'></span></span>");
  client.print("</div>");
  client.flush();
  
  client.print("<div class='info-item'>");
  client.print("<span class='info-label'>Stored Keys</span>");
  client.print("<span class='info-value'>");
  client.print(countKeys());
  client.print(" / ");
  client.print(MAX_KEYS);
  client.print("</span>");
  client.print("</div>");
  client.print("</div>");
  client.flush();
  
  client.print("<div class='info-card ota-section'>");
  client.print("<h3>🔄 Firmware Update</h3>");
  client.print("<form method='POST' action='/doupdate' enctype='multipart/form-data'>");
  client.print("<input type='file' name='update' accept='.bin' required>");
  client.print("<button type='submit' class='btn-upload'>Upload Firmware</button>");
  client.print("</form>");
  client.print("<p style='font-size:11px;color:#666;margin-top:10px;text-align:center'>⚠️ Do not disconnect during update</p>");
  client.print("</div>");
  client.print("</div>");
  client.print("</div>");
  client.flush();
  
  client.print("<script>");
  client.print("async function updateClock(){");
  client.print("try{");
  client.print("const res=await fetch('/api/time');");
  client.print("const data=await res.json();");
  client.print("if(data.success && data.synced){");
  client.print("document.getElementById('clock').textContent=data.time;");
  client.print("const badge=document.getElementById('dstBadge');");
  client.print("if(data.dst){");
  client.print("badge.innerHTML='<span class=\"dst-badge\">☀️ CEST</span>';");
  client.print("}else{");
  client.print("badge.innerHTML='<span class=\"dst-badge\" style=\"background:#6c757d;color:white\">❄️ CET</span>';");
  client.print("}");
  client.print("}");
  client.print("}catch(e){}");
  client.print("}");
  client.flush();
  
  client.print("function updateCounter(){");
  client.print("const val=document.getElementById('newValue').value;");
  client.print("const counter=document.getElementById('charCounter');");
  client.print("counter.textContent=val.length+' / 128 characters';");
  client.print("counter.className='char-counter';");
  client.print("if(val.length>100)counter.className+=' warning';");
  client.print("if(val.length>=128)counter.className+=' error';");
  client.print("}");
  client.flush();
  
  client.print("async function loadKeys(){");
  client.print("try{");
  client.print("const res=await fetch('/api/keys');");
  client.print("const data=await res.json();");
  client.print("const tbody=document.getElementById('keysTableBody');");
  client.print("if(!tbody)return;");
  client.print("tbody.innerHTML='';");
  client.print("data.keys.forEach(item=>{");
  client.print("const row=tbody.insertRow();");
  client.print("row.innerHTML=`<td><strong>${escapeHtml(item.key)}</strong></td>");
  client.print("<td class='key-value'>${escapeHtml(item.value)}</td>");
  client.print("<td style='text-align:center'><button class='btn btn-danger' onclick='deleteKey(\"${escapeHtml(item.key)}\")'>Delete</button></td>`;");
  client.print("});");
  client.print("}catch(e){console.error(e);}");
  client.print("}");
  client.flush();
  
  client.print("async function addKey(){");
  client.print("const value=document.getElementById('newValue').value;");
  client.print("if(!value){alert('Please enter a value');return;}");
  client.print("if(value.length>128){alert('Value too long (max 128 chars)');return;}");
  client.print("try{");
  client.print("const res=await fetch('/api/keys',{");
  client.print("method:'POST',");
  client.print("headers:{'Content-Type':'application/x-www-form-urlencoded'},");
  client.print("body:'value='+encodeURIComponent(value)");
  client.print("});");
  client.print("const data=await res.json();");
  client.print("if(data.success){");
  client.print("document.getElementById('newValue').value='';");
  client.print("updateCounter();");
  client.print("location.reload();");
  client.print("}else{");
  client.print("alert('Error: '+data.message);");
  client.print("}");
  client.print("}catch(e){alert('Error: '+e);}");
  client.print("}");
  client.flush();
  
  client.print("async function deleteKey(key){");
  client.print("if(!confirm('Delete key \"'+key+'\"?'))return;");
  client.print("try{");
  client.print("const res=await fetch('/api/keys',{");
  client.print("method:'DELETE',");
  client.print("headers:{'Content-Type':'application/x-www-form-urlencoded'},");
  client.print("body:'key='+encodeURIComponent(key)");
  client.print("});");
  client.print("const data=await res.json();");
  client.print("if(data.success){");
  client.print("location.reload();");
  client.print("}else{");
  client.print("alert('Error: '+data.message);");
  client.print("}");
  client.print("}catch(e){alert('Error: '+e);}");
  client.print("}");
  client.flush();
  
  client.print("function escapeHtml(text){");
  client.print("const div=document.createElement('div');");
  client.print("div.textContent=text;");
  client.print("return div.innerHTML;");
  client.print("}");
  client.flush();
  
  client.print("setInterval(updateClock,1000);");
  client.print("updateClock();");
  client.print("loadKeys();");
  client.print("</script>");
  client.print("</body></html>");
  client.flush();
}

void handleLogin(EthernetClient &client, int contentLength) {
  if (contentLength <= 0 || contentLength > 256) {
    client.println("HTTP/1.1 302 Found");
    client.println("Location: /?error=1");
    client.println("Connection: close");
    client.println();
    return;
  }

  String body = "";
  while (client.available() && body.length() < (unsigned)contentLength) {
    body += (char)client.read();
  }

  int pwdPos = body.indexOf("password=");
  if (pwdPos < 0) {
    client.println("HTTP/1.1 302 Found");
    client.println("Location: /?error=1");
    client.println("Connection: close");
    client.println();
    return;
  }

  String password = urlDecode(body.substring(pwdPos + 9));
  
  if (password == ADMIN_PASSWORD) {
    createSession();
    
    Serial.println("[AUTH] ✓ Login successful");
    
    client.println("HTTP/1.1 302 Found");
    client.println("Location: /");
    client.print("Set-Cookie: sessionId=");
    client.print(currentSession.sessionId);
    client.println("; Path=/; HttpOnly; Max-Age=3600");
    client.println("Connection: close");
    client.println();
  } else {
    Serial.println("[AUTH] ✗ Login failed - invalid password");
    
    client.println("HTTP/1.1 302 Found");
    client.println("Location: /?error=1");
    client.println("Connection: close");
    client.println();
  }
}

void handleLogout(EthernetClient &client) {
  destroySession();
  
  client.println("HTTP/1.1 302 Found");
  client.println("Location: /");
  client.println("Set-Cookie: sessionId=; Path=/; HttpOnly; Max-Age=0");
  client.println("Connection: close");
  client.println();
}

void handleOTAUpload(EthernetClient &client, int contentLength, const String &contentType, const String &sessionId) {
  Serial.println("[OTA] Update requested (authenticated session)");

  if (!contentType.startsWith("multipart/form-data")) {
    Serial.println("[OTA] ERROR: Not multipart/form-data");
    sendUpdateError(client, "Invalid content type");
    return;
  }

  int bPos = contentType.indexOf("boundary=");
  if (bPos < 0) {
    Serial.println("[OTA] ERROR: Boundary not found");
    sendUpdateError(client, "Boundary not found");
    return;
  }
  
  String boundary = "--" + contentType.substring(bPos + 9);
  boundary.trim();
  Serial.print("[OTA] Boundary: ");
  Serial.println(boundary);

  if (!findMultipartField(client, boundary, "update")) {
    Serial.println("[OTA] ERROR: Firmware file not found");
    sendUpdateError(client, "Firmware file not found");
    return;
  }

  Serial.println("[OTA] ✓ Firmware file found, starting update...");

  if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
    Serial.println("[OTA] ERROR: Update.begin() failed");
    Update.printError(Serial);
    sendUpdateError(client, "Failed to initialize update");
    return;
  }

  const size_t BUF_SIZE = 1024;
  uint8_t buf[BUF_SIZE];
  size_t written = 0;
  
  const int WIN_SIZE = 128;
  char window[WIN_SIZE];
  int winLen = 0;
  String boundaryCRLF = "\r\n" + boundary;

  while (client.connected()) {
    while (!client.available()) {
      delay(1);
    }

    int len = client.read(buf, BUF_SIZE);
    if (len <= 0) break;

    int fileBytesToWrite = 0;
    
    for (int i = 0; i < len; i++) {
      if (winLen == WIN_SIZE) {
        memmove(window, window + 1, WIN_SIZE - 1);
        winLen--;
      }
      window[winLen++] = (char)buf[i];

      if (winLen >= (int)boundaryCRLF.length()) {
        bool match = true;
        for (int j = 0; j < (int)boundaryCRLF.length(); j++) {
          if (window[winLen - boundaryCRLF.length() + j] != boundaryCRLF[j]) {
            match = false;
            break;
          }
        }

        if (match) {
          int boundaryStartInWin = winLen - boundaryCRLF.length();
          size_t totalReceived = written + i + 1;
          size_t fileTotal = totalReceived - (winLen - boundaryStartInWin);
          fileBytesToWrite = fileTotal - written;
          goto WRITE_AND_FINISH;
        }
      }
    }

    {
      size_t w = Update.write(buf, len);
      written += w;
      if (written % 51200 == 0) {
        Serial.printf("[OTA] Progress: %u bytes\n", (unsigned)written);
      }
    }
    continue;

WRITE_AND_FINISH:
    if (fileBytesToWrite < 0) fileBytesToWrite = 0;
    if (fileBytesToWrite > len) fileBytesToWrite = len;

    if (fileBytesToWrite > 0) {
      size_t w = Update.write(buf, fileBytesToWrite);
      written += w;
    }
    Serial.printf("[OTA] Total written: %u bytes\n", (unsigned)written);
    break;
  }

  Serial.printf("[OTA] Finalizing update (%u bytes)...\n", (unsigned)written);
  
  if (Update.end(true)) {
    Serial.println("[OTA] ✅ UPDATE SUCCESS!");
    
    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: text/html");
    client.println("Connection: close");
    client.println();
    client.println("<html><head><meta http-equiv='refresh' content='15;url=/'></head>");
    client.println("<body style='font-family: Arial; text-align: center; padding: 50px; background: linear-gradient(135deg, #667eea 0%, #764ba2 100%); color: white;'>");
    client.println("<div style='background: white; color: #333; padding: 50px; border-radius: 20px; display: inline-block;'>");
    client.println("<h2 style='color: #28a745;'>✅ Update Successful!</h2>");
    client.println("<p>Device is rebooting...</p>");
    client.println("<p style='color: #666; font-size: 14px; margin-top: 20px;'>You will be redirected in 15 seconds.</p>");
    client.println("</div></body></html>");
    
    delay(500);
    client.stop();
    delay(500);
    
    Serial.println("[OTA] Rebooting...\n");
    ESP.restart();
  } else {
    Serial.println("[OTA] ❌ UPDATE FAILED!");
    Update.printError(Serial);
    sendUpdateError(client, "Update failed during finalization");
  }
}

void handleAPIGetTime(EthernetClient &client) {
  time_t now;
  struct tm timeinfo;
  
  time(&now);
  gmtime_r(&now, &timeinfo);
  
  bool inDST = isDST(timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, 
                     timeinfo.tm_mday, timeinfo.tm_hour);
  
  char timeStr[9];
  snprintf(timeStr, sizeof(timeStr), "%02d:%02d:%02d", 
           timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
  
  char dateStr[11];
  snprintf(dateStr, sizeof(dateStr), "%04d-%02d-%02d", 
           timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday);
  
  const char* days[] = {"Sunday", "Monday", "Tuesday", "Wednesday", 
                        "Thursday", "Friday", "Saturday"};
  const char* dayStr = days[timeinfo.tm_wday];
  
  const char* tzName = inDST ? "CEST" : "CET";
  const char* tzOffset = inDST ? "+02:00" : "+01:00";
  
  String json = "{\"success\":true,\"time\":\"";
  json += timeStr;
  json += "\",\"date\":\"";
  json += dateStr;
  json += "\",\"day\":\"";
  json += dayStr;
  json += "\",\"timezone\":\"";
  json += tzName;
  json += "\",\"offset\":\"";
  json += tzOffset;
  json += "\",\"timestamp\":";
  json += String((unsigned long)now);
  json += ",\"synced\":";
  json += timeIsSynced ? "true" : "false";
  json += ",\"dst\":";
  json += inDST ? "true" : "false";
  json += "}";
  
  sendJSON(client, 200, json);
}

void handleAPIGetKeys(EthernetClient &client) {
  String json = "{\"success\":true,\"count\":" + String(countKeys()) + ",\"keys\":[";
  
  bool first = true;
  for (int i = 0; i < MAX_KEYS; i++) {
    String key = "k" + String(i);
    if (prefs.isKey(key.c_str())) {
      String value = prefs.getString(key.c_str(), "");
      if (!first) json += ",";
      json += "{\"key\":\"" + key + "\",\"value\":\"";
      for (unsigned int j = 0; j < value.length(); j++) {
        char c = value.charAt(j);
        if (c == '"') json += "\\\"";
        else if (c == '\\') json += "\\\\";
        else if (c == '\n') json += "\\n";
        else if (c == '\r') json += "\\r";
        else if (c == '\t') json += "\\t";
        else json += c;
      }
      json += "\"}";
      first = false;
    }
  }
  
  json += "]}";
  sendJSON(client, 200, json);
}

void handleAPIAddKey(EthernetClient &client, int contentLength) {
  if (contentLength <= 0 || contentLength > 512) {
    sendJSON(client, 400, "{\"success\":false,\"message\":\"Invalid content length\"}");
    return;
  }

  String body = "";
  while (client.available() && body.length() < (unsigned)contentLength) {
    body += (char)client.read();
  }

  int valuePos = body.indexOf("value=");
  
  if (valuePos < 0) {
    sendJSON(client, 400, "{\"success\":false,\"message\":\"Missing value\"}");
    return;
  }

  String value = urlDecode(body.substring(valuePos + 6));

  if (value.length() == 0) {
    sendJSON(client, 400, "{\"success\":false,\"message\":\"Value cannot be empty\"}");
    return;
  }

  if (value.length() > MAX_VALUE_LENGTH) {
    sendJSON(client, 400, "{\"success\":false,\"message\":\"Value too long (max 128 chars)\"}");
    return;
  }

  int keyIndex = -1;
  for (int i = 0; i < MAX_KEYS; i++) {
    String key = "k" + String(i);
    if (!prefs.isKey(key.c_str())) {
      keyIndex = i;
      break;
    }
  }

  if (keyIndex < 0) {
    sendJSON(client, 400, "{\"success\":false,\"message\":\"Maximum keys reached (100/100)\"}");
    return;
  }

  String key = "k" + String(keyIndex);

  prefs.putString(key.c_str(), value);
  Serial.print("[NVS] Saved ");
  Serial.print(key);
  Serial.print(" = ");
  Serial.println(value);

  String response = "{\"success\":true,\"message\":\"Key saved\",\"key\":\"" + key + "\"}";
  sendJSON(client, 200, response);
}

void handleAPIDeleteKey(EthernetClient &client, int contentLength) {
  if (contentLength <= 0 || contentLength > 256) {
    sendJSON(client, 400, "{\"success\":false,\"message\":\"Invalid content length\"}");
    return;
  }

  String body = "";
  while (client.available() && body.length() < (unsigned)contentLength) {
    body += (char)client.read();
  }

  int keyPos = body.indexOf("key=");
  if (keyPos < 0) {
    sendJSON(client, 400, "{\"success\":false,\"message\":\"Missing key\"}");
    return;
  }

  String key = urlDecode(body.substring(keyPos + 4));

  if (!prefs.isKey(key.c_str())) {
    sendJSON(client, 404, "{\"success\":false,\"message\":\"Key not found\"}");
    return;
  }

  prefs.remove(key.c_str());
  Serial.print("[NVS] Deleted key: ");
  Serial.println(key);

  sendJSON(client, 200, "{\"success\":true,\"message\":\"Key deleted\"}");
}

void handleAPIInsert(EthernetClient &client, int contentLength) {
  if (contentLength <= 0 || contentLength > 512) {
    sendJSON(client, 400, "{\"success\":false,\"message\":\"Invalid content length\"}");
    return;
  }

  String body = "";
  while (client.available() && body.length() < (unsigned)contentLength) {
    body += (char)client.read();
  }

  int valueStart = body.indexOf("\"value\"");
  if (valueStart < 0) {
    sendJSON(client, 400, "{\"success\":false,\"message\":\"Missing 'value' field\"}");
    return;
  }
  
  int colonPos = body.indexOf(":", valueStart);
  int quoteStart = body.indexOf("\"", colonPos);
  int quoteEnd = body.indexOf("\"", quoteStart + 1);
  
  if (quoteStart < 0 || quoteEnd < 0) {
    sendJSON(client, 400, "{\"success\":false,\"message\":\"Invalid JSON format\"}");
    return;
  }
  
  String value = body.substring(quoteStart + 1, quoteEnd);
  value.trim();

  if (value.length() == 0) {
    sendJSON(client, 400, "{\"success\":false,\"message\":\"Value cannot be empty\"}");
    return;
  }

  if (value.length() > MAX_VALUE_LENGTH) {
    sendJSON(client, 400, "{\"success\":false,\"message\":\"Value too long (max 128 chars)\"}");
    return;
  }

  int keyIndex = -1;
  for (int i = 0; i < MAX_KEYS; i++) {
    String key = "k" + String(i);
    if (!prefs.isKey(key.c_str())) {
      keyIndex = i;
      break;
    }
  }

  if (keyIndex < 0) {
    sendJSON(client, 400, "{\"success\":false,\"message\":\"Maximum keys reached (100/100)\"}");
    return;
  }

  String key = "k" + String(keyIndex);
  prefs.putString(key.c_str(), value);
  
  Serial.print("[API] INSERT: ");
  Serial.print(key);
  Serial.print(" = ");
  Serial.println(value);

  String response = "{\"success\":true,\"message\":\"Value inserted\",\"key\":\"" + key + "\",\"value\":\"";
  for (unsigned int i = 0; i < value.length(); i++) {
    char c = value.charAt(i);
    if (c == '"') response += "\\\"";
    else if (c == '\\') response += "\\\\";
    else if (c == '\n') response += "\\n";
    else if (c == '\r') response += "\\r";
    else if (c == '\t') response += "\\t";
    else response += c;
  }
  response += "\"}";
  
  sendJSON(client, 200, response);
}

void handleAPIRemove(EthernetClient &client, int contentLength) {
  if (contentLength <= 0 || contentLength > 512) {
    sendJSON(client, 400, "{\"success\":false,\"message\":\"Invalid content length\"}");
    return;
  }

  String body = "";
  while (client.available() && body.length() < (unsigned)contentLength) {
    body += (char)client.read();
  }

  int valueStart = body.indexOf("\"value\"");
  if (valueStart < 0) {
    sendJSON(client, 400, "{\"success\":false,\"message\":\"Missing 'value' field\"}");
    return;
  }
  
  int colonPos = body.indexOf(":", valueStart);
  int quoteStart = body.indexOf("\"", colonPos);
  int quoteEnd = body.indexOf("\"", quoteStart + 1);
  
  if (quoteStart < 0 || quoteEnd < 0) {
    sendJSON(client, 400, "{\"success\":false,\"message\":\"Invalid JSON format\"}");
    return;
  }
  
  String searchValue = body.substring(quoteStart + 1, quoteEnd);
  searchValue.trim();

  if (searchValue.length() == 0) {
    sendJSON(client, 400, "{\"success\":false,\"message\":\"Value cannot be empty\"}");
    return;
  }

  String foundKey = "";
  for (int i = 0; i < MAX_KEYS; i++) {
    String key = "k" + String(i);
    if (prefs.isKey(key.c_str())) {
      String storedValue = prefs.getString(key.c_str(), "");
      if (storedValue == searchValue) {
        foundKey = key;
        break;
      }
    }
  }

  if (foundKey.length() == 0) {
    sendJSON(client, 404, "{\"success\":false,\"message\":\"Value not found in storage\"}");
    return;
  }

  prefs.remove(foundKey.c_str());
  
  Serial.print("[API] REMOVE: ");
  Serial.print(foundKey);
  Serial.print(" (value: ");
  Serial.print(searchValue);
  Serial.println(")");

  String response = "{\"success\":true,\"message\":\"Value removed\",\"key\":\"" + foundKey + "\"}";
  sendJSON(client, 200, response);
}

void handleAPIPrint(EthernetClient &client) {
  Serial.println("[API] PRINT: Listing all key-value pairs");
  
  String json = "{\"success\":true,\"count\":" + String(countKeys()) + ",\"data\":[";
  
  bool first = true;
  for (int i = 0; i < MAX_KEYS; i++) {
    String key = "k" + String(i);
    if (prefs.isKey(key.c_str())) {
      String value = prefs.getString(key.c_str(), "");
      
      if (!first) json += ",";
      json += "{\"key\":\"" + key + "\",\"value\":\"";
      
      for (unsigned int j = 0; j < value.length(); j++) {
        char c = value.charAt(j);
        if (c == '"') json += "\\\"";
        else if (c == '\\') json += "\\\\";
        else if (c == '\n') json += "\\n";
        else if (c == '\r') json += "\\r";
        else if (c == '\t') json += "\\t";
        else json += c;
      }
      json += "\"}";
      first = false;
    }
  }
  
  json += "]}";
  sendJSON(client, 200, json);
}

void serve404(EthernetClient &client) {
  client.println("HTTP/1.1 404 Not Found");
  client.println("Content-Type: text/html");
  client.println("Connection: close");
  client.println();
  client.println("<html><body style='font-family: Arial; text-align: center; padding: 50px;'>");
  client.println("<h2>404 - Page Not Found</h2>");
  client.println("<a href='/' style='color: #667eea;'>← Back to Home</a>");
  client.println("</body></html>");
}

void serveUnauthorized(EthernetClient &client) {
  client.println("HTTP/1.1 401 Unauthorized");
  client.println("Content-Type: text/html");
  client.println("Connection: close");
  client.println();
  client.println("<html><body style='font-family: Arial; text-align: center; padding: 50px;'>");
  client.println("<h2 style='color: #dc3545;'>🔒 Unauthorized</h2>");
  client.println("<p>Your session has expired or is invalid.</p>");
  client.println("<a href='/' style='color: #667eea; text-decoration: none; font-weight: 600;'>← Login Again</a>");
  client.println("</body></html>");
}

void sendUpdateError(EthernetClient &client, const char *msg) {
  client.println("HTTP/1.1 500 Internal Server Error");
  client.println("Content-Type: text/html");
  client.println("Connection: close");
  client.println();
  client.println("<html><body style='font-family: Arial; text-align: center; padding: 50px;'>");
  client.println("<h2 style='color: #dc3545;'>❌ Update Failed</h2>");
  client.print("<p>");
  client.print(msg);
  client.println("</p>");
  client.println("<p style='color: #666; font-size: 14px;'>Check Serial Monitor for details.</p>");
  client.println("<a href='/' style='color: #667eea; text-decoration: none; font-weight: 600;'>← Back to Dashboard</a>");
  client.println("</body></html>");
}

void sendJSON(EthernetClient &client, int statusCode, const String &json) {
  client.print("HTTP/1.1 ");
  client.print(statusCode);
  client.println(" OK");
  client.println("Content-Type: application/json");
  client.println("Connection: close");
  client.println();
  client.println(json);
}

/* ====== HELPER FUNCTIONS ====== */

bool readLine(EthernetClient &client, String &out) {
  out = "";
  while (client.connected()) {
    while (!client.available()) {
      delay(1);
    }
    char c = client.read();
    if (c == '\r') continue;
    if (c == '\n') return true;
    out += c;
  }
  return false;
}

bool findMultipartField(EthernetClient &client, const String &boundary, const String &fieldName) {
  String line;
  String searchStr = "name=\"" + fieldName + "\"";
  
  while (true) {
    if (!readLine(client, line)) return false;
    
    if (line.startsWith(boundary)) {
      if (!readLine(client, line)) return false;
      
      if (line.indexOf(searchStr) >= 0) {
        while (true) {
          if (!readLine(client, line)) return false;
          if (line.length() == 0) break;
        }
        return true;
      } else {
        while (true) {
          if (!readLine(client, line)) return false;
          if (line.length() == 0) break;
        }
      }
    }
  }
  
  return false;
}

String extractCookie(const String &cookieHeader, const String &cookieName) {
  int startPos = cookieHeader.indexOf(cookieName + "=");
  if (startPos < 0) return "";
  
  startPos += cookieName.length() + 1;
  int endPos = cookieHeader.indexOf(";", startPos);
  
  if (endPos < 0) {
    return cookieHeader.substring(startPos);
  } else {
    return cookieHeader.substring(startPos, endPos);
  }
}

String urlDecode(String str) {
  String decoded = "";
  char c;
  for (unsigned int i = 0; i < str.length(); i++) {
    c = str.charAt(i);
    if (c == '+') {
      decoded += ' ';
    } else if (c == '%') {
      if (i + 2 < str.length()) {
        char hex[3] = { str.charAt(i + 1), str.charAt(i + 2), '\0' };
        decoded += (char)strtol(hex, NULL, 16);
        i += 2;
      }
    } else {
      decoded += c;
    }
  }
  return decoded;
}

String htmlEscape(String str) {
  String escaped = "";
  for (unsigned int i = 0; i < str.length(); i++) {
    char c = str.charAt(i);
    if (c == '<') escaped += "&lt;";
    else if (c == '>') escaped += "&gt;";
    else if (c == '&') escaped += "&amp;";
    else if (c == '"') escaped += "&quot;";
    else if (c == '\'') escaped += "&#39;";
    else escaped += c;
  }
  return escaped;
}

int countKeys() {
  int count = 0;
  for (int i = 0; i < MAX_KEYS; i++) {
    String key = "k" + String(i);
    if (prefs.isKey(key.c_str())) {
      count++;
    }
  }
  return count;
}

String getKeyByIndex(int index) {
  return "k" + String(index);
}