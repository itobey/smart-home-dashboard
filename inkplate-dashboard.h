// Next 3 lines are a precaution, you can ignore those, and the example would also work without them
#ifndef ARDUINO_INKPLATE10
#error "Wrong board selection for this example, please select Inkplate 10 in the boards menu."
#endif

#include <Arduino.h>
#include <HTTPClient.h>
#include <Inkplate.h>
#include <WiFi.h>
#include <driver/rtc_io.h>
#include <PubSubClient.h>
#include <esp_arduino_version.h>
#include <esp_err.h>
#include <esp_attr.h>
#include <esp_system.h>
#include <esp_task_wdt.h>

Inkplate display(INKPLATE_3BIT);

// Constants
const unsigned long DEEP_SLEEP_DURATION = 1200UL; // 20 minutes in seconds
const unsigned int WDT_TIMEOUT_SECONDS = 60;
const unsigned int WIFI_CONNECT_ATTEMPTS = 2;
const unsigned long WIFI_CONNECT_TIMEOUT_MS = 10000UL;
const char* WIFI_SSID = ""; // Your WiFi SSID
const char* WIFI_PASSWORD = ""; // Your WiFi password
const char* MQTT_SERVER = "192.168.0.34";
const int MQTT_PORT = 1883;
const char* MQTT_CLIENT_ID = "Inkplate";
const char* MQTT_USER = ""; // Add your MQTT username here
const char* MQTT_PASSWORD = ""; // Add your MQTT password here
const char* MQTT_TOPIC = "home/inkplate";
const char* IMAGE_URL = "https://hass-screenshot-nginx.nuc.one/output.jpeg";

WiFiClient espClient;
PubSubClient mqttClient(espClient);

// Unlike ordinary globals, this survives a watchdog reboot without flash writes.
// Initialize explicitly on a fresh boot or a wake from deep sleep.
RTC_NOINIT_ATTR uint32_t watchdogRecoveryUsed;

bool connectWifi();
void displayImage();
void sendMqttMsg();
void goToSleep();

bool shouldSleepAfterWatchdogReset() {
    esp_reset_reason_t reason = esp_reset_reason();
    bool watchdogReset = reason == ESP_RST_TASK_WDT ||
                         reason == ESP_RST_INT_WDT || reason == ESP_RST_WDT;
    if (!watchdogReset) {
        watchdogRecoveryUsed = 0;
        return false;
    }

    if (watchdogRecoveryUsed != 0) {
        return true;
    }

    watchdogRecoveryUsed = 1;
    Serial.println("Watchdog reset. Trying one immediate recovery.");
    return false;
}

bool initWatchdog() {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
    esp_task_wdt_config_t wdtConfig = {};
    wdtConfig.timeout_ms = WDT_TIMEOUT_SECONDS * 1000;
    wdtConfig.idle_core_mask = 0;
    wdtConfig.trigger_panic = true;

    esp_err_t result = esp_task_wdt_init(&wdtConfig);
    if (result == ESP_ERR_INVALID_STATE) {
        // Startup may have initialized the watchdog with different settings.
        result = esp_task_wdt_reconfigure(&wdtConfig);
    }
#else
    // The older API also updates an already-initialized watchdog.
    esp_err_t result = esp_task_wdt_init(WDT_TIMEOUT_SECONDS, true);
#endif
    if (result != ESP_OK) {
        Serial.printf("Failed to configure watchdog: %s\n", esp_err_to_name(result));
        return false;
    }

    // The current task may already be subscribed by the runtime.
    if (esp_task_wdt_status(NULL) != ESP_OK) {
        result = esp_task_wdt_add(NULL);
        if (result != ESP_OK) {
            Serial.printf("Failed to register watchdog task: %s\n", esp_err_to_name(result));
            return false;
        }
    }
    return true;
}

void setup() {
    Serial.begin(115200);
    if (shouldSleepAfterWatchdogReset()) {
        Serial.println("Repeated watchdog reset. Sleeping before retrying.");
        goToSleep();
        return;
    }
    if (!initWatchdog()) {
        Serial.println("Watchdog unavailable. Sleeping before retrying.");
        goToSleep();
        return;
    }
    if (!connectWifi()) {
        goToSleep();
        return;
    }
    esp_task_wdt_reset();
    displayImage();
    esp_task_wdt_reset();
    sendMqttMsg();
    esp_task_wdt_reset();
    goToSleep();
}

void loop() {
    // Empty loop as we're using deep sleep
}

bool connectWifi() {
    WiFi.mode(WIFI_STA);
    for (unsigned int attempt = 0; attempt < WIFI_CONNECT_ATTEMPTS; attempt++) {
        Serial.printf("Connecting to WiFi (attempt %u/%u)...\n",
                      attempt + 1, WIFI_CONNECT_ATTEMPTS);
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

        unsigned long startAttemptTime = millis();
        while (WiFi.status() != WL_CONNECTED &&
               millis() - startAttemptTime < WIFI_CONNECT_TIMEOUT_MS) {
            delay(100);
            Serial.print(".");
        }

        if (WiFi.status() == WL_CONNECTED) {
            Serial.println("\nConnected to WiFi");
            return true;
        }

        WiFi.disconnect();
        esp_task_wdt_reset();
    }

    Serial.println("\nFailed to connect to WiFi after two attempts. Sleeping before retrying.");
    return false;
}

void displayImage() {
    display.begin();
    Serial.println("Downloading image...");
    
    if (!display.drawImage(IMAGE_URL, 0, 0, false, false)) {
        Serial.println("Error opening image");
        return;
    }
    
    display.display();
    Serial.println("Image displayed successfully");
}

void reconnectMqtt() {
    int attempts = 0;
    while (!mqttClient.connected() && attempts < 3) {
        Serial.print("Attempting MQTT connection...");
        if (mqttClient.connect(MQTT_CLIENT_ID, MQTT_USER, MQTT_PASSWORD)) {
            Serial.println("connected");
            return;
        } else {
            Serial.print("failed, rc=");
            Serial.print(mqttClient.state());
            Serial.println(" retrying in 5 seconds");
            delay(5000);
            attempts++;
            esp_task_wdt_reset();
        }
    }
    
    if (!mqttClient.connected()) {
        Serial.println("Failed to connect to MQTT after 3 attempts");
    }
}

void sendMqttMsg() {
    mqttClient.setServer(MQTT_SERVER, MQTT_PORT);
    mqttClient.setSocketTimeout(10); // Seconds for MQTT response/read waits.
    
    if (!mqttClient.connected()) {
        reconnectMqtt();
    }
    
    if (mqttClient.connected()) {
        int temperature = display.readTemperature();
        float voltage = display.readBattery();
        
        char message[60];
        snprintf(message, sizeof(message), "inkplate temperature=%d,voltage=%.2f", temperature, voltage);
        
        if (mqttClient.publish(MQTT_TOPIC, message)) {
            Serial.println("MQTT message sent successfully");
        } else {
            Serial.println("Failed to send MQTT message");
        }
        
        mqttClient.disconnect();
    }
}

void goToSleep() {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    rtc_gpio_isolate(GPIO_NUM_12);
    
    esp_sleep_enable_timer_wakeup(DEEP_SLEEP_DURATION * 1000000UL);
    Serial.println("Going to sleep...");
    Serial.flush();
    esp_deep_sleep_start();
}
