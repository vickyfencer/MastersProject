int waterPin = 2;          // The pin connected to the flow sensor
volatile int pulseCount = 0; // Volatile variable to be modified in the interrupt
unsigned long lastTime = 0; // Variable to store the last time the count was reset
float waterRate;           // Variable to store the calculated water flow rate
float totalVolume = 0;    // Variable to store the total volume of water in mL

void setup() {
  pinMode(waterPin, INPUT);  // Set the waterPin as an input
  attachInterrupt(digitalPinToInterrupt(waterPin), waterFlow, RISING); // Attach interrupt to waterPin
  Serial.begin(9600);        // Initialize serial communication at 9600 bps
  Serial.println("Setup complete. Interrupt attached.");
}

void loop() {
  if (millis() - lastTime > 1000) {  // Check if 1 second has passed
    lastTime = millis();             // Update lastTime to the current time

    waterRate = pulseCount * 2.25;   // Calculate the water flow rate in mL/s
    totalVolume += waterRate;        // Accumulate the total volume in mL

    // Print the water rate and total volume in the appropriate format
    Serial.print("Current Water Consuming Rate: ");
    if (waterRate < 1000) {
      Serial.print(waterRate);
      Serial.println(" mL/s");
    } else {
      Serial.print(waterRate / 1000.0, 3); // Convert to liters
      Serial.println(" L/s");
    }

    Serial.print("Total Volume Consumed: ");
    if (totalVolume < 1000) {
      Serial.print(totalVolume);
      Serial.println(" mL");
    } else {
      Serial.print(totalVolume / 1000.0, 3); // Convert to liters
      Serial.println(" L");
    }

    pulseCount = 0; // Reset the count after processing
  }
}

void waterFlow() {
  pulseCount++; // Increment pulse count on each interrupt
  // Serial.println("Pulse detected");  // Optional: Debug message for each pulse detected
}
