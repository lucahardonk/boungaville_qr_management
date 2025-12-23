/**
 ******************************************************************************
 * @file     ETH_WebServer_WebOTA.ino
 * @brief    W5500 Ethernet web server with visit counter, clock, Web OTA, and NVS key storage
 * @version  V3.0
 * @date     2025-12-23
 * @author   Your Name
 * @license  MIT
 ******************************************************************************
 * 
 * Features:
 * - W5500 Ethernet connectivity with DHCP
 * - Web server with visit counter and live clock
 * - Secure Web-based OTA firmware updates with password protection
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
 * - Change OTA_PASSWORD before deployment
 * 
 ******************************************************************************
 */

#include <SPI.h>
#include <Ethernet.h>
#include <Update.h>
#include <Preferences.h>

/* ====== CONFIGURATION ====== */

// W5500 Pin Configuration
#define W5500_CS    14
#define W5500_RST    9
#define W5500_INT   10
#define W5500_MISO  12
#define W5500_MOSI  11
#define W5500_SCK   13

// Network Configuration
byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };

// OTA Configuration
const char* OTA_PASSWORD = "admin123";  // ⚠️ CHANGE THIS IN PRODUCTION!

// Server Configuration
#define SERVER_PORT 80

// NVS Configuration
#define MAX_KEYS 100
#define MAX_VALUE_LENGTH 128

/* ====== GLOBAL VARIABLES ====== */

EthernetServer server(SERVER_PORT);
unsigned long visitCount = 200;
Preferences prefs;

/* ====== FUNCTION PROTOTYPES ====== */

// HTTP Handlers
void serveHomePage(EthernetClient &client);
void serveUpdatePage(EthernetClient &client);
void serveKeysPage(EthernetClient &client);
void handleOTAUpload(EthernetClient &client, int contentLength, const String &contentType);
void handleAPIGetKeys(EthernetClient &client);
void handleAPIAddKey(EthernetClient &client, int contentLength);
void handleAPIDeleteKey(EthernetClient &client, int contentLength);
void serve404(EthernetClient &client);
void sendUpdateError(EthernetClient &client, const char *msg);
void sendJSON(EthernetClient &client, int statusCode, const String &json);

// Helper Functions
bool readLine(EthernetClient &client, String &out);
bool findMultipartField(EthernetClient &client, const String &boundary, const String &fieldName);
bool extractPassword(EthernetClient &client, const String &boundary, String &password);
String urlDecode(String str);
String htmlEscape(String str);
int countKeys();
String getKeyByIndex(int index);

/* ====== SETUP ====== */

void setup() {
  Serial.begin(115200);
  delay(500);
  
  Serial.println("\n\n========================================");
  Serial.println("ESP32-S3 Ethernet Web Server with OTA + NVS");
  Serial.println("========================================\n");

  // Initialize NVS
  prefs.begin("keys", false); // namespace "keys", RW mode
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
  Serial.println("\n========================================");
  Serial.print("Web Interface: http://");
  Serial.println(Ethernet.localIP());
  Serial.print("OTA Update:    http://");
  Serial.print(Ethernet.localIP());
  Serial.println("/update");
  Serial.print("Keys Manager:  http://");
  Serial.print(Ethernet.localIP());
  Serial.println("/keys");
  Serial.println("========================================\n");
}

/* ====== MAIN LOOP ====== */

void loop() {
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

  while (client.connected()) {
    if (!client.available()) continue;
    
    char c = client.read();

    if (c != '\n' && c != '\r') {
      currentLine += c;
    }

    // Parse request line (GET/POST/DELETE and path)
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

    // End of headers (blank line)
    if (c == '\n' && currentLineIsBlank) {
      
      Serial.print("[REQUEST] ");
      Serial.print(requestMethod);
      Serial.print(" ");
      Serial.println(requestPath);

      // Route handling
      if (requestMethod == "GET") {
        if (requestPath == "/" || requestPath == "") {
          serveHomePage(client);
        } else if (requestPath == "/update") {
          serveUpdatePage(client);
        } else if (requestPath == "/keys") {
          serveKeysPage(client);
        } else if (requestPath == "/api/keys") {
          handleAPIGetKeys(client);
        } else {
          serve404(client);
        }
      } else if (requestMethod == "POST") {
        if (requestPath == "/doupdate") {
          handleOTAUpload(client, contentLength, contentType);
        } else if (requestPath == "/api/keys") {
          handleAPIAddKey(client, contentLength);
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

void serveHomePage(EthernetClient &client) {
  visitCount++;
  
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: text/html");
  client.println("Connection: close");
  client.println();
  
  // Invia HTML in blocchi più piccoli con flush periodici
  client.print("<!DOCTYPE HTML><html><head><meta charset='UTF-8'>");
  client.print("<meta name='viewport' content='width=device-width, initial-scale=1.0'>");
  client.print("<title>ESP32-S3-ETH</title><style>");
  client.flush();
  
  client.print("*{margin:0;padding:0;box-sizing:border-box}");
  client.print("body{font-family:'Segoe UI',Tahoma,Geneva,Verdana,sans-serif;background:linear-gradient(135deg,#667eea 0%,#764ba2 100%);min-height:100vh;display:flex;align-items:center;justify-content:center;padding:20px}");
  client.print(".container{background:white;padding:50px;border-radius:20px;box-shadow:0 20px 60px rgba(0,0,0,0.3);max-width:600px;width:100%}");
  client.flush();
  
  client.print("h1{color:#333;font-size:48px;margin-bottom:10px;text-align:center}");
  client.print("p{text-align:center;color:#666;margin:10px 0;font-size:16px}");
  client.print(".counter{font-size:20px;margin-top:30px;padding:15px;background:#f8f9fa;border-radius:10px;text-align:center;color:#495057;font-weight:600}");
  client.flush();
  
  client.print("#clock{font-size:36px;margin:30px 0 10px 0;font-weight:bold;color:#667eea;text-align:center;font-family:'Courier New',monospace}");
  client.print(".clock-label{text-align:center;color:#999;font-size:14px;margin-bottom:30px}");
  client.print(".ota-section{margin-top:40px;padding:25px;background:linear-gradient(135deg,#667eea15 0%,#764ba215 100%);border-radius:15px;border:2px solid #667eea30}");
  client.flush();
  
  client.print(".ota-title{font-weight:600;color:#333;margin-bottom:15px;text-align:center;font-size:18px}");
  client.print(".ota-info{text-align:center;color:#666;font-size:14px;margin:10px 0}");
  client.print(".ota-button{display:block;margin:20px auto 0;padding:15px 40px;background:linear-gradient(135deg,#667eea 0%,#764ba2 100%);color:white;text-decoration:none;border-radius:50px;font-weight:600;text-align:center;transition:transform 0.2s,box-shadow 0.2s;box-shadow:0 4px 15px rgba(102,126,234,0.4)}");
  client.flush();
  
  client.print(".ota-button:hover{transform:translateY(-2px);box-shadow:0 6px 20px rgba(102,126,234,0.6)}");
  client.print(".badge{display:inline-block;padding:5px 12px;background:#28a745;color:white;border-radius:20px;font-size:12px;font-weight:600;margin-top:10px}");
  client.print("</style></head><body><div class='container'>");
  client.flush();
  
  client.print("<h1>🌐 Hello World!</h1>");
  client.print("<p>Welcome to ESP32-S3-ETH Web Server</p>");
  client.print("<p>Powered by W5500 Ethernet Module</p>");
  client.print("<div class='badge'>✓ Online</div>");
  client.flush();
  
  client.print("<div class='counter'>📊 Page Visits: ");
  client.print(visitCount);
  client.print("</div>");
  client.flush();

  client.print("<div id='clock'>--:--:--</div>");
  client.print("<p class='clock-label'>🕐 Your Local Time</p>");
  client.flush();

  client.print("<div class='ota-section'>");
  client.print("<div class='ota-title'>🔄 Firmware Update</div>");
  client.print("<div class='ota-info'>Device IP: <strong>");
  client.print(Ethernet.localIP());
  client.print("</strong></div>");
  client.flush();
  
  client.print("<div class='ota-info'>🔒 Password Protected</div>");
  client.print("<a href='/update' class='ota-button'>Upload Firmware</a>");
  client.print("</div>");
  client.flush();

  client.print("<div class='ota-section' style='margin-top:20px'>");
  client.print("<div class='ota-title'>🔑 Key-Value Storage</div>");
  client.print("<div class='ota-info'>Stored Keys: <strong>");
  client.print(countKeys());
  client.print(" / ");
  client.print(MAX_KEYS);
  client.print("</strong></div>");
  client.flush();
  
  client.print("<a href='/keys' class='ota-button'>Manage Keys</a>");
  client.print("</div>");
  client.flush();

  client.print("<script>");
  client.print("function updateClock(){");
  client.print("const now=new Date();");
  client.print("const h=String(now.getHours()).padStart(2,'0');");
  client.print("const m=String(now.getMinutes()).padStart(2,'0');");
  client.print("const s=String(now.getSeconds()).padStart(2,'0');");
  client.print("document.getElementById('clock').textContent=h+':'+m+':'+s;");
  client.print("}");
  client.flush();
  
  client.print("setInterval(updateClock,1000);");
  client.print("updateClock();");
  client.print("</script>");
  client.flush();

  client.print("</div></body></html>");
  client.flush();
}

void serveKeysPage(EthernetClient &client) {
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: text/html");
  client.println("Connection: close");
  client.println();
  
  client.print("<!DOCTYPE HTML><html><head><meta charset='UTF-8'>");
  client.print("<meta name='viewport' content='width=device-width,initial-scale=1.0'>");
  client.print("<title>Key Manager</title><style>");
  client.flush();
  
  client.print("*{margin:0;padding:0;box-sizing:border-box}");
  client.print("body{font-family:'Segoe UI',Tahoma,Geneva,Verdana,sans-serif;background:linear-gradient(135deg,#667eea 0%,#764ba2 100%);min-height:100vh;padding:20px}");
  client.print(".container{background:white;padding:40px;border-radius:20px;box-shadow:0 20px 60px rgba(0,0,0,0.3);max-width:900px;margin:0 auto}");
  client.flush();
  
  client.print("h2{color:#333;text-align:center;margin-bottom:10px;font-size:32px}");
  client.print(".subtitle{text-align:center;color:#666;margin-bottom:30px;font-size:14px}");
  client.print(".add-form{background:#f8f9fa;padding:25px;border-radius:15px;margin-bottom:30px}");
  client.flush();
  
  client.print(".form-row{display:flex;gap:10px;margin-bottom:15px}");
  client.print("input[type=text]{flex:1;padding:12px;border:2px solid #e0e0e0;border-radius:10px;font-size:14px}");
  client.print("input[type=text]:focus{outline:none;border-color:#667eea}");
  client.flush();
  
  client.print(".btn{padding:12px 30px;border:none;border-radius:10px;cursor:pointer;font-size:14px;font-weight:600;transition:all 0.2s}");
  client.print(".btn-primary{background:linear-gradient(135deg,#667eea 0%,#764ba2 100%);color:white}");
  client.print(".btn-primary:hover{transform:translateY(-2px);box-shadow:0 4px 15px rgba(102,126,234,0.4)}");
  client.flush();
  
  client.print(".btn-danger{background:#dc3545;color:white;padding:8px 15px;font-size:12px}");
  client.print(".btn-danger:hover{background:#c82333}");
  client.print(".keys-table{width:100%;border-collapse:collapse;margin-top:20px}");
  client.flush();
  
  client.print(".keys-table th{background:#667eea;color:white;padding:15px;text-align:left;font-weight:600}");
  client.print(".keys-table td{padding:12px 15px;border-bottom:1px solid #e0e0e0}");
  client.print(".keys-table tr:hover{background:#f8f9fa}");
  client.flush();
  
  client.print(".key-value{font-family:'Courier New',monospace;font-size:13px;color:#495057;word-break:break-all}");
  client.print(".back-link{display:inline-block;margin-top:25px;color:#667eea;text-decoration:none;font-weight:600}");
  client.print(".back-link:hover{color:#764ba2}");
  client.flush();
  
  client.print(".info-box{background:#d1ecf1;border:2px solid #bee5eb;border-radius:10px;padding:15px;margin-bottom:20px;color:#0c5460;font-size:13px;text-align:center}");
  client.print(".empty-state{text-align:center;padding:60px 20px;color:#999}");
  client.print(".empty-state-icon{font-size:64px;margin-bottom:20px}");
  client.flush();
  
  client.print(".char-counter{font-size:12px;color:#999;margin-top:5px}");
  client.print(".char-counter.warning{color:#ffc107}");
  client.print(".char-counter.error{color:#dc3545}");
  client.print("</style></head><body><div class='container'>");
  client.flush();
  
  client.print("<h2>🔑 Key Manager</h2>");
  client.print("<p class='subtitle'>Manage persistent key-value storage (NVS)</p>");
  client.flush();
  
  client.print("<div class='info-box'>📊 <strong>");
  client.print(countKeys());
  client.print(" / ");
  client.print(MAX_KEYS);
  client.print("</strong> keys stored | Max value length: <strong>128 characters</strong>");
  client.print("</div>");
  client.flush();

  client.print("<div class='add-form'>");
  client.print("<h3 style='margin-bottom:15px;color:#333'>➕ Add New Key</h3>");
  client.print("<div class='form-row'>");
  client.print("<input type='text' id='newValue' placeholder='Enter value (max 128 chars)' maxlength='128' oninput='updateCounter()'>");
  client.print("<button class='btn btn-primary' onclick='addKey()'>Add Key</button>");
  client.print("</div>");
  client.flush();
  
  client.print("<div class='char-counter' id='charCounter'>0 / 128 characters</div>");
  client.print("<p style='font-size:12px;color:#999;margin-top:10px'>ℹ️ Key name will be auto-assigned (k0, k1, k2...)</p>");
  client.print("</div>");
  client.flush();

  client.print("<div id='keysContainer'>");
  
  int keyCount = countKeys();
  if (keyCount == 0) {
    client.print("<div class='empty-state'>");
    client.print("<div class='empty-state-icon'>📭</div>");
    client.print("<p>No keys stored yet. Add your first key above!</p>");
    client.print("</div>");
  } else {
    client.print("<table class='keys-table'>");
    client.print("<thead><tr><th style='width:20%'>Key</th><th style='width:65%'>Value</th><th style='width:15%;text-align:center'>Action</th></tr></thead>");
    client.print("<tbody id='keysTableBody'>");
    client.print("</tbody>");
    client.print("</table>");
  }
  client.flush();
  
  client.print("</div>");
  client.print("<a href='/' class='back-link'>← Back to Home</a>");
  client.flush();

  client.print("<script>");
  
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
  client.flush();
  
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
  client.flush();
  
  client.print("const res=await fetch('/api/keys',{");
  client.print("method:'POST',");
  client.print("headers:{'Content-Type':'application/x-www-form-urlencoded'},");
  client.print("body:'value='+encodeURIComponent(value)");
  client.print("});");
  client.flush();
  
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
  client.flush();
  
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

  client.print("loadKeys();");
  client.print("</script>");
  client.print("</div></body></html>");
  client.flush();
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
      // Escape JSON special chars
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

  // Read body
  String body = "";
  while (client.available() && body.length() < (unsigned)contentLength) {
    body += (char)client.read();
  }

  // Parse value=...
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

  // Trova la chiave kN più bassa disponibile
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

  // Save to NVS
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

  // Read body
  String body = "";
  while (client.available() && body.length() < (unsigned)contentLength) {
    body += (char)client.read();
  }

  // Parse key=...
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

void serveUpdatePage(EthernetClient &client) {
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: text/html");
  client.println("Connection: close");
  client.println();
  
  client.println("<!DOCTYPE HTML>");
  client.println("<html>");
  client.println("<head>");
  client.println("<meta charset='UTF-8'>");
  client.println("<meta name='viewport' content='width=device-width, initial-scale=1.0'>");
  client.println("<title>OTA Firmware Update</title>");
  client.println("<style>");
  client.println("* { margin: 0; padding: 0; box-sizing: border-box; }");
  client.println("body { font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; background: linear-gradient(135deg, #667eea 0%, #764ba2 100%); min-height: 100vh; display: flex; align-items: center; justify-content: center; padding: 20px; }");
  client.println(".container { background: white; padding: 50px; border-radius: 20px; box-shadow: 0 20px 60px rgba(0,0,0,0.3); max-width: 500px; width: 100%; }");
  client.println("h2 { color: #333; text-align: center; margin-bottom: 10px; font-size: 32px; }");
  client.println(".subtitle { text-align: center; color: #666; margin-bottom: 30px; font-size: 14px; }");
  client.println(".form-group { margin-bottom: 20px; }");
  client.println("label { display: block; margin-bottom: 8px; color: #333; font-weight: 600; font-size: 14px; }");
  client.println("input[type=password], input[type=file] { width: 100%; padding: 15px; border: 2px solid #e0e0e0; border-radius: 10px; font-size: 14px; transition: border-color 0.3s; }");
  client.println("input[type=password]:focus, input[type=file]:focus { outline: none; border-color: #667eea; }");
  client.println("input[type=submit] { width: 100%; background: linear-gradient(135deg, #667eea 0%, #764ba2 100%); color: white; padding: 18px; border: none; border-radius: 50px; cursor: pointer; font-size: 16px; font-weight: 600; transition: transform 0.2s, box-shadow 0.2s; box-shadow: 0 4px 15px rgba(102, 126, 234, 0.4); margin-top: 10px; }");
  client.println("input[type=submit]:hover { transform: translateY(-2px); box-shadow: 0 6px 20px rgba(102, 126, 234, 0.6); }");
  client.println(".back-link { display: block; text-align: center; margin-top: 25px; color: #667eea; text-decoration: none; font-weight: 600; transition: color 0.3s; }");
  client.println(".back-link:hover { color: #764ba2; }");
  client.println(".warning { background: #fff3cd; border: 2px solid #ffc107; border-radius: 10px; padding: 15px; margin-bottom: 25px; color: #856404; font-size: 13px; text-align: center; }");
  client.println(".icon { font-size: 48px; text-align: center; margin-bottom: 20px; }");
  client.println("</style>");
  client.println("</head>");
  client.println("<body>");
  client.println("<div class='container'>");
  client.println("<div class='icon'>🔄</div>");
  client.println("<h2>Firmware Update</h2>");
  client.println("<p class='subtitle'>Upload new firmware to your device</p>");
  
  client.println("<div class='warning'>");
  client.println("⚠️ <strong>Warning:</strong> Do not disconnect power during update!");
  client.println("</div>");
  
  client.println("<form method='POST' action='/doupdate' enctype='multipart/form-data'>");
  
  client.println("<div class='form-group'>");
  client.println("<label for='password'>🔒 Password</label>");
  client.println("<input type='password' id='password' name='password' placeholder='Enter OTA password' required>");
  client.println("</div>");
  
  client.println("<div class='form-group'>");
  client.println("<label for='update'>📁 Firmware File (.bin)</label>");
  client.println("<input type='file' id='update' name='update' accept='.bin' required>");
  client.println("</div>");
  
  client.println("<input type='submit' value='Upload Firmware'>");
  client.println("</form>");
  
  client.println("<a href='/' class='back-link'>← Back to Home</a>");
  client.println("</div>");
  client.println("</body>");
  client.println("</html>");
}

void handleOTAUpload(EthernetClient &client, int contentLength, const String &contentType) {
  Serial.println("[OTA] Update requested");

  // Validate content type
  if (!contentType.startsWith("multipart/form-data")) {
    Serial.println("[OTA] ERROR: Not multipart/form-data");
    sendUpdateError(client, "Invalid content type");
    return;
  }

  // Extract boundary
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

  // Extract and verify password
  String receivedPassword;
  if (!extractPassword(client, boundary, receivedPassword)) {
    Serial.println("[OTA] ERROR: Failed to extract password");
    sendUpdateError(client, "Failed to parse password");
    return;
  }

  Serial.print("[OTA] Password received: ");
  Serial.println(receivedPassword);

  if (receivedPassword != OTA_PASSWORD) {
    Serial.println("[OTA] ERROR: Wrong password!");
    client.println("HTTP/1.1 401 Unauthorized");
    client.println("Content-Type: text/html");
    client.println("Connection: close");
    client.println();
    client.println("<html><body style='font-family: Arial; text-align: center; padding: 50px;'>");
    client.println("<h2 style='color: #dc3545;'>❌ Authentication Failed</h2>");
    client.println("<p>Incorrect password. Please try again.</p>");
    client.println("<a href='/update' style='color: #667eea; text-decoration: none; font-weight: 600;'>← Try Again</a>");
    client.println("</body></html>");
    return;
  }

  Serial.println("[OTA] ✓ Password correct");

  // Find firmware file part
  if (!findMultipartField(client, boundary, "update")) {
    Serial.println("[OTA] ERROR: Firmware file not found");
    sendUpdateError(client, "Firmware file not found");
    return;
  }

  Serial.println("[OTA] ✓ Firmware file found, starting update...");

  // Initialize OTA
  if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
    Serial.println("[OTA] ERROR: Update.begin() failed");
    Update.printError(Serial);
    sendUpdateError(client, "Failed to initialize update");
    return;
  }

  // Stream firmware data
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

  // Finalize update
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
  client.println("<a href='/update' style='color: #667eea; text-decoration: none; font-weight: 600;'>← Try Again</a>");
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

bool extractPassword(EthernetClient &client, const String &boundary, String &password) {
  String line;
  
  while (true) {
    if (!readLine(client, line)) return false;
    
    if (line.startsWith(boundary)) {
      if (!readLine(client, line)) return false;
      
      if (line.indexOf("name=\"password\"") >= 0) {
        // Skip remaining headers
        while (true) {
          if (!readLine(client, line)) return false;
          if (line.length() == 0) break;
        }
        
        // Read password value
        if (!readLine(client, password)) return false;
        return true;
      } else {
        // Skip this part
        while (true) {
          if (!readLine(client, line)) return false;
          if (line.length() == 0) break;
        }
      }
    }
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
        // Skip remaining headers
        while (true) {
          if (!readLine(client, line)) return false;
          if (line.length() == 0) break;
        }
        return true;
      } else {
        // Skip this part
        while (true) {
          if (!readLine(client, line)) return false;
          if (line.length() == 0) break;
        }
      }
    }
  }
  
  return false;
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