#include <OneWire.h>
#include <DS18B20.h>
#include <DallasTemperature.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// LCD setup (20x4, I2C address 0x27)
LiquidCrystal_I2C lcd(0x27, 20, 4);

#define ONE_WIRE_BUS 10
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

// pin number, lcd, tsaka dagdag led

// Plant selection buttons
const int buttonLettuce = 12;
const int buttonKale = 9;
const int buttonBokChoy = 11;

// New control buttons
const int ackButton = 3;

// Sensor pins
const int pHPin = A0;
const int TDSPin = A1;

// Output pins
const int temp = 5;
const int pH = 6;
const int TDS = 7;
const int buzzer = 4;
const int blueLED= 2;

unsigned long timerStart = 0;
const unsigned long TIMER_DURATION = 21600000UL; // 6 hours

struct PlantParameters {
  float minTemp;
  float maxTemp;
  float minPH;
  float maxPH;
  int minTDS;
  int maxTDS;
  String name;
};

PlantParameters currentPlant;
String lastSelectedPlant = "";
bool plantSelected = false;
bool timerActive = false;

void setup() {
  pinMode(buttonLettuce, INPUT);
  pinMode(buttonKale, INPUT);
  pinMode(buttonBokChoy, INPUT);
  pinMode(ackButton, INPUT);

  pinMode(temp, OUTPUT);
  pinMode(pH, OUTPUT);
  pinMode(TDS, OUTPUT);
  pinMode(blueLED, OUTPUT);
  pinMode(buzzer, OUTPUT);

  Serial.begin(9600);
  sensors.begin();

  // LCD init
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Select Plant...");

  Serial.println("Please select a plant by pressing a button...");
}

void loop() {

  // Allow plant selection only if reset has been triggered
  if (!plantSelected) {
    if (digitalRead(buttonLettuce) == HIGH) {
      clearSerial();
      currentPlant = {20.0, 22.0, 6.0, 6.5, 560, 840, "Lettuce"};
      displayParameters();
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Plant: Lettuce");
      lastSelectedPlant = "Lettuce";
      plantSelected = true;
      delay(500);
    }

    if (digitalRead(buttonKale) == HIGH) {
      clearSerial();
      currentPlant = {20.0, 23.0, 6.0, 6.5, 1120, 1750, "Kale"};
      displayParameters();
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Plant: Kale");
      lastSelectedPlant = "Kale";
      plantSelected = true;
      delay(500);
    }

    if (digitalRead(buttonBokChoy) == HIGH) {
      clearSerial();
      currentPlant = {18.0, 20.0, 5.5, 6.5, 1050, 1400, "Bok Choy"};
      displayParameters();
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Plant: Bok Choy");
      lastSelectedPlant = "Bok Choy";
      plantSelected = true;
      delay(500);
    }
    return; // Skip rest of loop until plant selected
  }

  // Acknowledge maintenance
  if (digitalRead(ackButton) == HIGH) {
    timerStart = millis();
    timerActive = true;  // ✅ start tracking time
    digitalWrite(blueLED, LOW);
    digitalWrite(buzzer, LOW);

    lcd.clear();
    lcd.setCursor(0, 0);   // Line 1 (left-aligned)
    lcd.print("Maintenance");
    lcd.setCursor(8, 1);  // Line 2 (right side)
    lcd.print("Acknowledged");
    lcd.setCursor(2, 3);   // Line 3 (slightly centered)
    lcd.print("Timer Restarted");

    Serial.println("Maintenance acknowledged. Timer restarted.");
    delay(500); // debounce
  }

  // 6-hour timer check
  if (timerActive) {
    unsigned long elapsed = millis() - timerStart;
    if (elapsed >= TIMER_DURATION) {
      timerActive = false;
      digitalWrite(blueLED, HIGH);
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Maintenance Due!");
    }
  }

  // Sensor readings
  sensors.requestTemperatures();
  float tempC = sensors.getTempCByIndex(0);

  const int numSamples = 10;
  int total = 0;
  for (int i = 0; i < numSamples; i++) {
    total += analogRead(pHPin);
    delay(10);
  }
  int sensorValue = total / numSamples;
  float phValue = 7.0 + (sensorValue - 445.0) * (-4.2) / 230.0;

  int sensorValueTDS = analogRead(TDSPin);
  float voltageTDS = sensorValueTDS * (5.0 / 1024.0);
  float compensationCoefficient = 1.0 + 0.02 * (tempC - 25.0);
  float compensationVoltage = voltageTDS / compensationCoefficient;
  float tdsValue = (133.42 * pow(compensationVoltage, 3)
                    - 255.86 * pow(compensationVoltage, 2)
                    + 857.39 * compensationVoltage) * 0.5;

  unsigned long timeLeft = max(0UL, TIMER_DURATION - (millis() - timerStart));
  int hours = timeLeft / 3600000UL;
  int minutes = (timeLeft % 3600000UL) / 60000UL;
  int seconds = (timeLeft % 60000UL) / 1000UL;

  // Condition check
  int notOptimalCount = 0;
  String issues[3];

  bool tempIssue = !isInRange(tempC, currentPlant.minTemp, currentPlant.maxTemp);
  bool phIssue = !isInRange(phValue, currentPlant.minPH - 0.5, currentPlant.maxPH + 0.5);
  bool tdsIssue = !isInRange(tdsValue, currentPlant.minTDS, currentPlant.maxTDS);

  if (tempIssue) issues[notOptimalCount++] = "Temperature";
  if (phIssue)   issues[notOptimalCount++] = "pH";
  if (tdsIssue)  issues[notOptimalCount++] = "TDS";

  String message;
  if (notOptimalCount == 0) message = "All conditions optimal";
  else if (notOptimalCount == 1) message = issues[0] + " not optimal";
  else if (notOptimalCount == 2) message = issues[0] + " & " + issues[1] + " not optimal";
  else message = "All conditions BAD!";

  updateStatus(notOptimalCount, tempIssue, phIssue, tdsIssue);

  // Output to Serial
  Serial.println("------");
  Serial.println("Plant: " + currentPlant.name);
  Serial.print("Temp: ");
  Serial.print(tempC, 1);
  Serial.print("°C  ;   pH: ");
  Serial.println(phValue, 2);
  Serial.print("TDS: ");
  Serial.print(tdsValue, 0);
  Serial.print("   ;   Timer: ");
  Serial.print(hours);
  Serial.print(":");
  if (minutes < 10) Serial.print("0");
  Serial.print(minutes);
  Serial.print(":");
  if (seconds < 10) Serial.print("0");
  Serial.println(seconds);
  Serial.println(message);
  Serial.println("------");

  // Output to LCD
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("P:");
  lcd.print(currentPlant.name);

  lcd.setCursor(0, 1);
  lcd.print("T:");
  lcd.print(tempC, 1);
  lcd.print((char)223);
  lcd.print("C");

  lcd.setCursor(12, 1);
  lcd.print("pH:");
  lcd.print(phValue, 2);

  lcd.setCursor(0, 2);
  lcd.print("TDS:");
  lcd.print(tdsValue, 0);
  lcd.print(" ppm");

  lcd.setCursor(0, 3);
  lcd.print(message);

  // ✅ Show timer if active
  if (timerActive) {
    unsigned long timeLeft = max(0UL, TIMER_DURATION - (millis() - timerStart));
    int hours = timeLeft / 3600000UL;
    int minutes = (timeLeft % 3600000UL) / 60000UL;
    int seconds = (timeLeft % 60000UL) / 1000UL;

    lcd.setCursor(12, 0);
    if (hours < 10) lcd.print("0");
    lcd.print(hours);
    lcd.print(":");
    if (minutes < 10) lcd.print("0");
    lcd.print(minutes);
    lcd.print(":");
    if (seconds < 10) lcd.print("0");
    lcd.print(seconds);
  }

  delay(1000);
}

bool isInRange(float val, float minVal, float maxVal) {
  return val >= minVal && val <= maxVal;
}

void updateStatus(int notOptimalCount, bool tempIssue, bool phIssue, bool tdsIssue) {
  digitalWrite(temp, tempIssue);
  digitalWrite(pH, phIssue);
  digitalWrite(TDS, tdsIssue);

  if (tempIssue && phIssue && tdsIssue) {
    digitalWrite(buzzer, HIGH);
  } else {
    digitalWrite(buzzer, LOW);
  }
}

void clearSerial() {
  for (int i = 0; i < 20; i++) Serial.println();
}

void displayParameters() {
  Serial.println(currentPlant.name + " selected.");
  Serial.print("Temperature Range: ");
  Serial.print(currentPlant.minTemp);
  Serial.print(" - ");
  Serial.println(currentPlant.maxTemp);

  Serial.print("pH Range: ");
  Serial.print(currentPlant.minPH);
  Serial.print(" - ");
  Serial.println(currentPlant.maxPH);

  Serial.print("TDS Range: ");
  Serial.print(currentPlant.minTDS);
  Serial.print(" - ");
  Serial.println(currentPlant.maxTDS);
  Serial.println("---------------------------");
}
