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

Inkplate display(INKPLATE_3BIT);

// Constants
const unsigned long DEEP_SLEEP_DURATION = 1200UL; // 20 minutes in seconds
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

void setup() {
    Serial.begin(115200);
    connectWifi();
    displayImage();
    sendMqttMsg();
    goToSleep();
}

void loop() {
    // Empty loop as we're using deep sleep
}

void connectWifi() {
    Serial.print("Connecting to WiFi...");
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    
    unsigned long startAttemptTime = millis();
    
    while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 10000) {
        delay(100);
        Serial.print(".");
    }
    
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("\nFailed to connect to WiFi. Restarting...");
        ESP.restart();
    }
    
    Serial.println("\nConnected to WiFi");
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
        }
    }
    
    if (!mqttClient.connected()) {
        Serial.println("Failed to connect to MQTT after 3 attempts");
    }
}

void sendMqttMsg() {
    mqttClient.setServer(MQTT_SERVER, MQTT_PORT);
    
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
