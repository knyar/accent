#include "Network.h"

#include <Preferences.h>
#include <WiFi.h>
#include <base64.h>
#include "WifiForm.h"

// The name of the Wifi preferences.
const char* kWifiPreferences = "wifi";

// The preferences key for the Wifi SSID.
const char* kWifiSsidKey = "ssid";

// The preferences key for the Wifi password.
const char* kWifiPasswordKey = "password";

// The SSID of the Wifi setup access point.
const char* kSetupSsid = "AccentSetup";

// The IP address of the Wifi setup access point.
const IPAddress kSetupIp(1, 2, 3, 4);

// // The subnet mask of the Wifi setup access point.
const IPAddress kSetupSubnet(255, 255, 255, 0);

// The relative URL showing the Wifi setup form.
const String kShowWifiFormUrl = "/go";

// The relative URL saving the Wifi setup form.
const String kSaveWifiFormUrl = "/save";

// The time in milliseconds when a connection attempt times out.
const uint32_t kConnectTimeoutMs = 30 * 1000;

// The time in milliseconds between each connection check.
const uint32_t kConnectTimeoutStepMs = 500;

// The time in milliseconds before timing out when reading HTTP data.
const uint16_t kReadTimeoutMs = 30 * 1000;

bool Network::ConnectWifi() {
  if (WiFi.isConnected()) {
    Serial.println("Already connected");
    return true;
  }

  // Load SSID and password from preferences.
  Preferences preferences;
  preferences.begin(kWifiPreferences, true);
  String ssid = preferences.getString(kWifiSsidKey, "");
  if (ssid.length() == 0) {
    Serial.println("No Wifi credentials");
    return false;
  }
  String password = preferences.getString(kWifiPasswordKey, "");
  preferences.end();

  // Start connecting with SSID and password.
  Serial.printf("Connecting to \"%s\" .", ssid.c_str());
  WiFi.mode(WIFI_STA);
  if (password.length() > 0) {
    WiFi.begin(ssid.c_str(), password.c_str());
  } else {
    WiFi.begin(ssid.c_str());
  }

  // Wait until connected or time out.
  for (int i = 0; !WiFi.isConnected(); ++i) {
    if (i > kConnectTimeoutMs / kConnectTimeoutStepMs) {
      Serial.println("\nTimed out connecting");
      display_.ShowError();
      // Restart rather than return false to reset the Wifi connection.
      power_.Restart();
    }
    delay(kConnectTimeoutStepMs);
    Serial.print(".");
  }

  Serial.printf("\nConnected to %s as %s\n", WiFi.SSID().c_str(),
                WiFi.localIP().toString().c_str());
  return true;
}

bool Network::HttpGet(HTTPClient* http, const String& url) {
  return HttpGet(http, url, {});
}

bool Network::HttpGet(HTTPClient* http, const String& base_url,
                      const std::vector<String>& parameters) {
  if (parameters.size() % 2 != 0) {
    Serial.printf("Incomplete pairs of keys and values for URL: %s\n",
                  base_url.c_str());
    return false;
  }

  // Add any parameters to the URL.
  String url = base_url;
  for (int i = 0; i < parameters.size(); i += 2) {
    String delimiter = (i == 0 ? "?" : "&");
    String key = parameters[i];
    String value = parameters[i + 1];
    url += delimiter + key + "=" + value;
  }

  Serial.printf("Requesting URL: %s\n", url.c_str());
  if (!http->begin(url)) {
    Serial.printf("Failed to connect to server: %s\n", url.c_str());
    return false;
  }

  // Apply the read timeout after connecting.
  http->setTimeout(kReadTimeoutMs);

  // Authenticate the request.
  AddAuthHeader(http);

  int status = http->GET();
  if (status <= 0) {
    Serial.printf("Request failed: %s\n", http->errorToString(status).c_str());
    http->end();
    return false;
  }

  Serial.printf("Status code: %d\n", status);
  if (status != HTTP_CODE_OK) {
    http->end();
    return false;
  }

  return true;
}

void Network::ResetWifi() {
  Serial.println("Resetting Wifi credentials");

  Preferences preferences;
  preferences.begin(kWifiPreferences, false);
  preferences.putString(kWifiSsidKey, "");
  preferences.putString(kWifiPasswordKey, "");
  preferences.end();
}

bool Network::StartWifiSetupServer() {
  Serial.println("Starting Wifi setup");

  WiFi.mode(WIFI_AP);
  if (!WiFi.softAP(kSetupSsid)) {
    Serial.println("Failed to start access point");
    wifi_setup_server_ = nullptr;
    return false;
  }

  // Wait for the access point to start.
  delay(100);

  if (!WiFi.softAPConfig(kSetupIp, kSetupIp, kSetupSubnet)) {
    Serial.println("Failed to apply access point config");
    wifi_setup_server_ = nullptr;
    return false;
  }

  IPAddress ip = WiFi.softAPIP();
  Serial.printf("Access point \"%s\" started at %s\n", kSetupSsid,
                ip.toString().c_str());

  // Start a web server handling the Wifi setup.
  wifi_setup_server_ = new WebServer(80);
  wifi_setup_server_->on(kShowWifiFormUrl,
                         std::bind(&Network::ShowWifiForm, this));
  wifi_setup_server_->on(kSaveWifiFormUrl,
                         std::bind(&Network::SaveWifiForm, this));
  wifi_setup_server_->onNotFound(std::bind(&Network::SendNotFound, this));
  wifi_setup_server_->begin();
  return true;
}

bool Network::HandleWifiSetupServer() {
  if (!wifi_setup_server_) {
    return false;
  }

  wifi_setup_server_->handleClient();
  return true;
}

void Network::ShowWifiForm() {
  if (wifi_setup_server_->method() != HTTP_GET) {
    wifi_setup_server_->send(HTTP_CODE_BAD_REQUEST);
    return;
  }

  wifi_setup_server_->send(HTTP_CODE_OK, "text/html", kWifiForm);
}

void Network::SaveWifiForm() {
  if (wifi_setup_server_->method() != HTTP_POST) {
    wifi_setup_server_->send(HTTP_CODE_BAD_REQUEST);
    return;
  }

  // Parse the arguments from the form.
  String ssid;
  String password;
  for (int i = 0; i < wifi_setup_server_->args(); ++i) {
    String arg_name = wifi_setup_server_->argName(i);
    String arg = wifi_setup_server_->arg(i);
    if (arg_name == "ssid") {
      ssid = arg;
    } else if (arg_name == "password") {
      password = arg;
    }
  }

  // Save the SSID and password.
  Serial.println("Saving Wifi credentials");
  Preferences preferences;
  preferences.begin(kWifiPreferences, false);
  preferences.putString(kWifiSsidKey, ssid);
  preferences.putString(kWifiPasswordKey, password);
  preferences.end();

  // Restart to have the settings take effect.
  Serial.println("Restarting");
  power_.Restart();
}

void Network::SendNotFound() { wifi_setup_server_->send(HTTP_CODE_NOT_FOUND); }

void Network::AddAuthHeader(HTTPClient* http) {
  // Use the Wifi MAC address as the unique user key.
  String user_key = WiFi.macAddress();
  user_key.replace(":", "");  // Disallowed character

  // Add the header with the Base64-encoded authorization (no username).
  String authorization = base64::encode(":" + user_key);
  http->addHeader("Authorization", "Basic " + authorization);
}
