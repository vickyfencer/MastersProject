#include <WiFiS3.h>
#include <ESP_Mail_Client.h>
#include <PubSubClient.h>
#include <NTPClient.h>
#include <WiFiUdp.h>

#define TdsSensorPin A0  // TDS sensor pin definition

// Wi-Fi credentials
const char* ssid = "VM5531913";
const char* password = "3p3GrFtdfakn";

// SMTP server details
#define SMTP_HOST "smtp.gmail.com"
#define SMTP_PORT esp_mail_smtp_port_587
#define AUTHOR_EMAIL "vickyfencer007@gmail.com"
#define AUTHOR_PASSWORD "jjop kqop only xuag"
#define RECIPIENT_EMAIL "boltlancer@gmail.com"

// Pin connected to the flow sensor
int waterPin = 2;
volatile int pulseCount = 0;
unsigned long lastTime = 0;
unsigned long lastConsumptionTime = 0;
unsigned long consumptionStartTime = 0;
unsigned long totalConsumptionDuration = 0;
unsigned long emailSentTime = 0;

float waterRate;
float totalVolume = 0;
float recommendedDrinkingInterval = 7200000; // 2 hours
float userDefinedWeeklyThreshold = 1000.0; // Default weekly threshold in mL
const float NaClPercentage = 0.60; // Estimated percentage of NaCl in TDS

float warningLevels[] = {0.25, 0.50, 0.75, 0.95}; // 25%, 50%, 75%, 95%
bool warningsIssued[] = {false, false, false, false};

int IDWGState = 0;
const float IDWG_WARNING_LIMIT = 4.0;

WiFiClient client;
PubSubClient mqttClient(client);
SMTPSession smtp;
Session_Config config;
SMTP_Message message;

String fluidType = "water";
float predialysisWeight = 70.0;

bool isConsuming = false;

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org");

void waterFlow() {
    pulseCount++;
    if (!isConsuming) {
        isConsuming = true;
        consumptionStartTime = millis();
    }
}

void connectWiFi() {
    Serial.begin(115200);
    Serial.println("Starting WiFi connection...");
    WiFi.begin(ssid, password);
    Serial.print("Connecting to Wi-Fi");
    while (WiFi.status() != WL_CONNECTED) {
        delay(300);
        Serial.print(".");
    }
    Serial.println(" Connected with IP: ");
    Serial.println(WiFi.localIP());

    timeClient.begin();
    timeClient.setTimeOffset(0);
    Serial.println("Waiting for NTP time sync...");
    while (!timeClient.update()) {
        timeClient.forceUpdate();
        delay(1000);
    }
    Serial.println("Time synchronized.");
}

String getTime() {
    timeClient.update();
    return timeClient.getFormattedTime();
}

void setupEmail(String subject, String body) {
    config.server.host_name = SMTP_HOST;
    config.server.port = SMTP_PORT;
    config.login.email = AUTHOR_EMAIL;
    config.login.password = AUTHOR_PASSWORD;
    config.login.user_domain = F("127.0.0.1");

    message.sender.name = "Arduino";
    message.sender.email = AUTHOR_EMAIL;
    message.subject = subject;
    message.addRecipient("Recipient", RECIPIENT_EMAIL);
    message.text.content = body;
}

void sendEmail() {
    smtp.setTCPTimeout(10);
    if (!smtp.connect(&config)) {
        Serial.println("Connection error");
        return;
    } else {
        Serial.println("SMTP connect successful");
    }

    if (!MailClient.sendMail(&smtp, &message)) {
        Serial.println("Error sending email: " + smtp.errorReason());
    } else {
        Serial.println("Email sent successfully");
    }
}

void setup() {
    connectWiFi();
    mqttClient.setServer("192.168.0.87", 1883); // Replace with your MQTT broker's IP address

    pinMode(waterPin, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(waterPin), waterFlow, FALLING);

    pinMode(TdsSensorPin, INPUT);  // Initialize TDS sensor pin

    Serial.println("Setup complete. Interrupt attached.");
    Serial.println("Enter the weekly threshold value in mL:");
}

void loop() {
    if (!mqttClient.connected()) {
        Serial.println("Attempting MQTT connection...");
        while (!mqttClient.connected()) {
            if (mqttClient.connect("ArduinoClient")) {
                Serial.println("MQTT connected");
            } else {
                Serial.print("MQTT connection failed, rc=");
                Serial.print(mqttClient.state());
                Serial.println(" try again in 5 seconds");
                delay(5000);
            }
        }
    }
    mqttClient.loop();

    // Handle user input
    if (Serial.available() > 0) {
        String input = Serial.readStringUntil('\n');
        Serial.flush();
        char inputType = input.charAt(0);
        input.remove(0, 1);

        switch (inputType) {
        case 'T':
            userDefinedWeeklyThreshold = input.toFloat();
            Serial.print("Weekly threshold set to: ");
            Serial.println(userDefinedWeeklyThreshold);
            for (int i = 0; i < 4; i++) {
                warningsIssued[i] = false; // Reset warnings
            }
            break;
        case 'F':
            fluidType = input;
            Serial.print("Fluid type set to: ");
            Serial.println(fluidType);
            break;
        case 'W':
            predialysisWeight = input.toFloat();
            Serial.print("Predialysis weight set to: ");
            Serial.println(predialysisWeight);
            break;
        default:
            Serial.println("Invalid input type.");
            break;
        }
    }

    // TDS Measurement
    int buffer_tds[10], temp1;
    unsigned long int avgval_ADC;

    for (int i = 0; i < 10; i++) { 
        buffer_tds[i] = analogRead(TdsSensorPin);
        delay(30);
    }

    for (int i = 0; i < 9; i++) {
        for (int j = i + 1; j < 10; j++) {
            if (buffer_tds[i] > buffer_tds[j]) {
                temp1 = buffer_tds[i];
                buffer_tds[i] = buffer_tds[j];
                buffer_tds[j] = temp1;
            }
        }
    }

    avgval_ADC = 0;
    for (int i = 2; i < 8; i++) avgval_ADC += buffer_tds[i];

    float voltage_value = (float)avgval_ADC * 5.0 / 1024.0 / 6;
    float TDS = (133.42 / voltage_value * voltage_value - 255.86 * voltage_value + 857.39 * voltage_value) * 0.5;
    float FDS = TDS - 60.05;
    // Calculate NaCl Concentration in ppm
    float NaClConcentration = FDS * NaClPercentage;

    // Calculate NaCl Consumed
    float totalVolume_mL = totalVolume; // Assuming totalVolume is in mL
    float NaClConsumed_mg = totalVolume_mL * NaClConcentration;

    // Continue with existing water flow logic
    unsigned long currentMillis = millis();

    if (currentMillis - lastTime > 1000) {
        lastTime = currentMillis;
        waterRate = pulseCount * 2.25; // Example calculation, adjust based on your flow sensor specs
        totalVolume += waterRate;
 Serial.print("<------------------------------------------------------------------->");
        Serial.print("Timestamp: ");
        Serial.println(getTime());
       
       // Serial.println(pulseCount);
        Serial.print("Current Water Consuming Rate: ");
        Serial.print(waterRate);
        Serial.print(" mL/s, TDS Value = ");
        Serial.print(TDS);
        Serial.println(" PPM");
        Serial.print("Estimated NaCl Concentration = ");
    Serial.print(NaClConcentration);
        Serial.print(" Total Volume Consumed: ");
        Serial.print(totalVolume / 1000.0); // Convert to Liters
        Serial.println(" L");
          Serial.print("Total NaCl Consumed = ");
          Serial.print(NaClConsumed_mg);
    Serial.println(" mg");

        float IDWG = totalVolume / 1000.0;
        Serial.print("Predicted Interdialytic Weight Gain (IDWG): ");
        Serial.print(IDWG);
        Serial.println(" kg");

        if (IDWG > 4.0 && IDWGState == 0) {
            String idwgWarning = "Warning: Interdialytic Weight Gain (IDWG) has exceeded the safe limit of 4 kg. Current IDWG is " + String(IDWG) + " kg.";
            Serial.println(idwgWarning);

            String payload = "{";
            payload += "\"totalVolume\":";
            payload += totalVolume;
            payload += ",\"fluidType\":\"";
            payload += fluidType;
            payload += "\",\"IDWG\":";
            payload += IDWG;
            payload += ",\"warning\":\"";
            payload += idwgWarning;
            payload += "\"}";
            mqttClient.publish("sensor/waterflow", payload.c_str());

            setupEmail("Critical Alert: IDWG Limit Exceeded", idwgWarning);
            sendEmail();

            IDWGState = 1;
        } 
        else if (IDWG > 5.7 && IDWGState == 1) {
            String idwgWarning = "Critical Warning: Interdialytic Weight Gain (IDWG) has exceeded the critical limit of 5.7 kg. Immediate action required. Current IDWG is " + String(IDWG) + " kg.";
            Serial.println(idwgWarning);

            String payload = "{";
            payload += "\"totalVolume\":";
            payload += totalVolume;
            payload += ",\"fluidType\":\"";
            payload += fluidType;
            payload += "\",\"IDWG\":";
            payload += IDWG;
            payload += ",\"warning\":\"";
            payload += idwgWarning;
            payload += "\"}";
            mqttClient.publish("sensor/waterflow", payload.c_str());

            setupEmail("Critical Alert: IDWG Critical Limit Exceeded", idwgWarning);
            sendEmail();

            IDWGState = 2;
        }

        if (currentMillis - lastConsumptionTime >= recommendedDrinkingInterval) {
            lastConsumptionTime = currentMillis;

            if ((millis() - emailSentTime >= 60000) && !isConsuming) {
                String recommendation = "Recommended to drink water now.";
                Serial.print("Timestamp: ");
                Serial.println(getTime());
                Serial.println(recommendation);

                String payload = "{";
                payload += "\"pulseCount\":";
                payload += pulseCount;
                payload += ",\"waterRate\":";
                payload += waterRate;
                payload += ",\"totalVolume\":";
                payload += totalVolume;
                payload += ",\"fluidType\":\"";
                payload += fluidType;
                payload += "\",\"recommendation\":\"";
                payload += recommendation;
                payload += "\",\"timestamp\":\"";
                payload += getTime();
                payload += "\"}";
                mqttClient.publish("sensor/waterflow", payload.c_str());

                setupEmail("Water Consumption Recommendation", recommendation);
                sendEmail();

                emailSentTime = millis();
            }
        }

        for (int i = 0; i < 4; i++) {
            if (!warningsIssued[i] && totalVolume >= userDefinedWeeklyThreshold * warningLevels[i]) {
                String warning = "Warning: You have consumed " + String(warningLevels[i] * 100) + "% of your weekly fluid limit.";
                Serial.print("Timestamp: ");
                Serial.println(getTime());
                Serial.println(warning);

                String payload = "{";
                payload += "\"totalVolume\":";
                payload += totalVolume;
                payload += ",\"fluidType\":\"";
                payload += fluidType;
                payload += "\",\"warning\":\"";
                payload += warning;
                payload += "\",\"timestamp\":\"";
                payload += getTime();
                payload += "\"}";
                mqttClient.publish("sensor/waterflow", payload.c_str());

                setupEmail("Fluid Consumption Warning", warning);
                sendEmail();

                warningsIssued[i] = true;
            }
        }

        if (isConsuming) {
            unsigned long currentDuration = millis() - consumptionStartTime;
            totalConsumptionDuration += currentDuration;

            unsigned long totalDurationInMinutes = totalConsumptionDuration / 60000;
            unsigned long totalDurationInSeconds = (totalConsumptionDuration % 60000) / 1000;

            Serial.print("Timestamp: ");
            Serial.println(getTime());
            Serial.print("Total Consumption Duration: ");
            if (totalDurationInMinutes > 0) {
                Serial.print(totalDurationInMinutes);
                Serial.print(" minutes and ");
            }
            Serial.print(totalDurationInSeconds);
            Serial.println(" seconds");

            isConsuming = false;
        }

        pulseCount = 0;
    }
}
