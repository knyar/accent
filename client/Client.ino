#include <Arduino.h>
#include "Display.h"
#include "Network.h"
#include "Power.h"

// The baud rate for the serial connection.
const uint32_t kSerialSpeed = 115200;

// The GPIO pin used to measure battery voltage.
const uint8_t kBatteryLevelPin = 34;

// The base URL for server requests.
const String kBaseUrl = "https://knyar-accent.ew.r.appspot.com";

// The URL for the next wake time endpoint.
const String kNextEndpoint = kBaseUrl + "/next";

// The URL for the e-paper display image endpoint.
const String kEpdEndpoint = kBaseUrl + "/epd";

// The size in bytes of the streaming HTTP response and image buffers.
const uint32_t kStreamBufferSize = 1024;

// The time in milliseconds to wait before restarting after an error.
uint64_t kRestartDelayMs = 60 * 60 * 1000;  // 1 hour

// Helper library instances.
Display display(kSerialSpeed);
Network network(kSerialSpeed);
Power power;

void setup() {
  // Connect to Wifi or start the setup flow.
  if (!network.ConnectWifi()) {
    display.ShowWifiSetup();
    network.StartWifiSetupServer();
    return;
  }

  // Show the latest image.
  display.Initialize();
  if (!downloadImage()) {
    return;
  }
  display.Finalize();

  // Go to sleep until the next refresh.
  scheduleSleep();
}

void loop() {
  if (network.HandleWifiSetupServer()) {
    // Continue to loop.
    return;
  }

  // Falling through means there was an error.
  Serial.println("Restarting after error");
  display.ShowError();
  power.DeepSleep(kRestartDelayMs);
}

// Streams the image from the server and sends it to the display in chunks.
bool downloadImage() {
  int batteryLevel = analogRead(kBatteryLevelPin);
  Serial.printf("Battery level: %d\n", batteryLevel);
  
  Serial.println("Downloading image");
  HTTPClient http;

  // Request the current image from the server.
  if (!network.HttpGet(&http, kEpdEndpoint,
                       {"battery", String(batteryLevel),
                        "width", String(display.Width()),
                        "height", String(display.Height())})) {
    return false;
  }

  // Start reading from the stream.
  uint8_t buffer[kStreamBufferSize];
  WiFiClient* stream = http.getStreamPtr();
  uint32_t total_count = 0;
  do {
    if (!http.connected()) {
      Serial.println("Connection lost");
      http.end();
      return false;
    }

    Serial.printf("%d bytes available\n", stream->available());

    // Fill the buffer.
    uint32_t count = stream->readBytes(buffer, sizeof(buffer));

    // Send the buffer to the display.
    display.Load(buffer, count, total_count);

    total_count += count;
    Serial.printf("Read %lu bytes (%lu total)\n", count, total_count);
  } while (stream->available() > 0);

  Serial.println("Download complete");
  http.end();
  return true;
}

// Sleeps for a time received from the server.
void scheduleSleep() {
  Serial.println("Scheduling sleep");
  HTTPClient http;

  // Request the next wake time from the server.
  if (!network.HttpGet(&http, kNextEndpoint)) {
    return;
  }

  // Read the sleep time from the server.
  String delay_ms_str = http.getString();
  http.end();
  Serial.printf("Sleep server response: %s\n", delay_ms_str.c_str());
  uint64_t delay_ms = strtoull(delay_ms_str.c_str(), nullptr, 10);
  power.DeepSleep(delay_ms);
}
