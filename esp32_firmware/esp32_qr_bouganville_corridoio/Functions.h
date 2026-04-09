/**
 ******************************************************************************
 * @file     Functions.h
 * @brief    Function declarations and structures for ESP32 Web Server
 * @version  V4.0
 * @date     2025-12-24
 * @license  MIT
 ******************************************************************************
 */

#ifndef FUNCTIONS_H
#define FUNCTIONS_H

#include <Arduino.h>
#include <Ethernet.h>
#include <Preferences.h>

/* ====== CONFIGURATION ====== */

// System Configuration
extern const char* SYSTEM_NAME;
extern const char* ADMIN_PASSWORD;

// W5500 Pin Configuration
#define W5500_CS    14
#define W5500_RST    9
#define W5500_INT   10
#define W5500_MISO  12
#define W5500_MOSI  11
#define W5500_SCK   13

// Network Configuration
extern byte mac[6];

// QR Scanner Configuration (UART2)
#define GM65_RX 17
#define GM65_TX 16

// Relay Configuration
#define RELAY_PIN   18

// Server Configuration
#define SERVER_PORT 80

// NVS Configuration
#define MAX_KEYS 100
#define MAX_VALUE_LENGTH 128

// Session Configuration
#define SESSION_TIMEOUT 60000

/* ====== STRUCTURES ====== */

struct Session {
  String sessionId;
  unsigned long lastActivity;
  bool isAuthenticated;
};

/* ====== GLOBAL VARIABLES ====== */

extern EthernetServer server;
extern Preferences prefs;
extern bool timeIsSynced;
extern Session currentSession;
extern HardwareSerial QRSerial;
extern String qrBuffer;
extern String lastScannedQR;
extern unsigned long lastScanTime;
extern unsigned long scanCounter;

/* ====== SESSION MANAGEMENT ====== */

String generateSessionId();                              // Generates a random 32-character hexadecimal session ID
bool isSessionValid(const String &sessionId);            // Validates session ID and checks if it hasn't expired
void createSession();                                    // Creates a new authenticated session with unique ID
void destroySession();                                   // Destroys current session and clears authentication

/* ====== QR SCANNER FUNCTIONS ====== */

String readQR();                                         // Reads QR code data from UART2 serial buffer
void toggleRelay();      
String readQR();
void toggleRelay();
bool validQrcode(String qrCode); // Activates relay for 500ms then deactivates it

/* ====== TIME SYNC FUNCTIONS ====== */

bool isDST(int year, int month, int day, int hour);      // Determines if given date/time is in Daylight Saving Time (Italian timezone)
bool syncTimeViaHTTP();                                  // Synchronizes system time via HTTP request to Google server
void initTime();                                         // Initializes time synchronization on startup

/* ====== HTTP HANDLERS ====== */

void serveLoginPage(EthernetClient &client);             // Serves the HTML login page with live clock
void serveDashboard(EthernetClient &client);             // Serves the authenticated dashboard with QR management interface
void handleLogin(EthernetClient &client, int contentLength);  // Processes login form submission and creates session
void handleLogout(EthernetClient &client);               // Handles logout request and destroys session
void handleOTAUpload(EthernetClient &client, int contentLength, const String &contentType, const String &sessionId);  // Handles firmware upload and OTA update process
void handleAPIGetKeys(EthernetClient &client);           // API endpoint: Returns all stored keys as JSON
void handleAPIAddKey(EthernetClient &client, int contentLength);  // API endpoint: Adds a new key-value pair to NVS storage
void handleAPIDeleteKey(EthernetClient &client, int contentLength);  // API endpoint: Deletes a key from NVS storage
void handleAPIInsert(EthernetClient &client, int contentLength);  // API endpoint: Inserts a value with auto-assigned key (JSON format)
void handleAPIRemove(EthernetClient &client, int contentLength);  // API endpoint: Removes a key by searching for its value (JSON format)
void handleAPIPrint(EthernetClient &client);             // API endpoint: Prints all key-value pairs as JSON
void handleAPIGetTime(EthernetClient &client);           // API endpoint: Returns current time with DST info as JSON
void serve404(EthernetClient &client);                   // Serves 404 Not Found error page
void serveUnauthorized(EthernetClient &client);          // Serves 401 Unauthorized error page
void sendUpdateError(EthernetClient &client, const char *msg);  // Sends OTA update error page with custom message
void sendJSON(EthernetClient &client, int statusCode, const String &json);  // Sends JSON response with specified HTTP status code
void handleAPILastScan(EthernetClient &client);

/* ====== HELPER FUNCTIONS ====== */

bool readLine(EthernetClient &client, String &out);      // Reads a single line from HTTP client until newline character
bool findMultipartField(EthernetClient &client, const String &boundary, const String &fieldName);  // Finds specific field in multipart form data (for OTA upload)
String extractCookie(const String &cookieHeader, const String &cookieName);  // Extracts cookie value from Cookie header string
String urlDecode(String str);                            // Decodes URL-encoded string (converts %20 to space, etc.)
String htmlEscape(String str);                           // Escapes HTML special characters to prevent XSS attacks
int countKeys();                                         // Counts total number of keys stored in NVS
String getKeyByIndex(int index);                         // Returns key name for given index (e.g., "k0", "k1")

#endif // FUNCTIONS_H