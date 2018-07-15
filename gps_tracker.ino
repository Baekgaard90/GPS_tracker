#include <Adafruit_FONA.h>
#include <Adafruit_GPS.h>
#include <SoftwareSerial.h>

#define FONA_RX  9
#define FONA_TX  8
#define FONA_RST 4
#define FONA_RI  7

SoftwareSerial fonaSS = SoftwareSerial(FONA_TX, FONA_RX);
SoftwareSerial *fonaSerial = &fonaSS;

Adafruit_FONA fona = Adafruit_FONA(FONA_RST);

// what's the name of the hardware serial port?
#define GPSSerial Serial1

// Connect to the GPS on the hardware port
Adafruit_GPS GPS(&GPSSerial);

uint32_t timer1 = millis();
uint32_t timer2 = millis();

bool track_status = false;

void setup() {
  Serial.begin(115200);

  // Starting FONA module
  fonaSerial->begin(4800);
  if (! fona.begin(*fonaSerial)) {
    Serial.println(F("Couldn't find FONA"));
    while (1);
  }

  fona.setGPRSNetworkSettings(F("Internet"), F(""), F(""));

  // Starting GPS module
  GPS.begin(9600);
  GPS.sendCommand(PMTK_SET_NMEA_OUTPUT_RMCGGA);
  GPS.sendCommand(PMTK_SET_NMEA_UPDATE_1HZ); // 1 Hz update rate
  GPS.standby(); // Returns false if already on standby.

  delay(5000);
}

void loop() {

  uint16_t i, n;
  n = 10;

  switch (checkSMS(&timer1, 10000)) {
    case 1:
      Serial.println("Initiating GPS tracking...");
      track_status = true;
      GPS.wakeup(); // Returns false if already woken up.
      i = 1;
      while (!fona.enableGPRS(true) & i <= n) {
        Serial.print("Failed to turn GPRS on. Retrying "); Serial.print(i); Serial.print("/"); Serial.println(n);
        i += 1;
        delay(1000);
      }
      break;
    case -1:
      Serial.println("Deactivating GPS tracking...");
      track_status = false;
      GPS.standby(); // Returns false if already on standby.
      i = 1;
      while (!fona.enableGPRS(false) & i <= n) {
        Serial.print("Failed to turn GPRS off. Retrying "); Serial.print(i); Serial.print("/"); Serial.println(n);
        i += 1;
        delay(1000);
      }
      break;
  }
  if (track_status) {
    track(&timer2, 10000);
  }
}

float geoDMS2DD(float number, char letter) {
  // Convert GPS coordinates from format DMS to DD.

  float x;
  x = floor(number / 100);
  x += (number - x * 100) / 60;
  if (letter == 'S' | letter == 'W') {
    x = -x;
  }
  return x;
}

bool intervalTimer(uint32_t *timer, uint32_t len) {
  // Function returns true once for each time interval of length 'len' (in milliseconds).

  // millis() overflows (goes back to zero) after approximately 50 days. Reset timer if that happens.
  if (*timer > millis()) *timer = millis();

  if (millis() - *timer > len) {
    *timer = millis(); // reset the timer
    return true;
  }
  else {
    return false;
  }
}

int checkSMS(uint32_t *timer, uint32_t interval_len) {
  // timer and len determines how often to check for a new SMS.
  // If a new SMS is available, determine if GPS tracking should be turned on or off (or if nothing should be done)

  uint16_t smslen;
  char command[14];
  int16_t output;

  if (intervalTimer(&*timer, interval_len)) {
    if (fona.readSMS(1, command, 14, &smslen)) { // pass in buffer and max len!
      if (strcmp(command, "GPS tracking 1") == 0) {
        output = 1;
      }
      else if (strcmp(command, "GPS tracking 0") == 0) {
        output = -1;
      }
      else {
        output = 0;
      }
      if (!fona.deleteSMS(1)) {
        Serial.println("Failed to delete SMS...");
      }
    }
    else {
      output = 0;
    }
  }
  else {
    output = 0;
  }
  return output;
}

void track(uint32_t *timer, uint32_t interval_len) {
  // Constantly check the GPS for new geo data and call function to pass it to web server
  // once every time interval of length 'interval_len' (in milliseconds).

  char c = GPS.read();
  if (GPS.newNMEAreceived()) {
    if (!GPS.parse(GPS.lastNMEA()))
      return;
  }

  if (intervalTimer(&*timer, interval_len)) {
    if (GPS.fix) {
      passGeoToServer();
    }
  }
}

void passGeoToServer() {
  // Function to pass geo data to web server.

  uint16_t statuscode;
  int16_t length;
  char url[150];
  char temp_buffer[12];
  int16_t bat_soc;

  if (!fona.getBattPercent(&bat_soc)) {
    bat_soc = -1;
  }

  strncpy(url, "http://lbae.dk/main/pass_geo?device=FeatherTracker", 150);

  strcat(url, "&battery=");
  dtostrf(bat_soc, 2, 0, temp_buffer);
  strcat(url, temp_buffer);

  strcat(url, "&lat=");
  dtostrf(geoDMS2DD(GPS.latitude, GPS.lat), 12, 8, temp_buffer);
  strcat(url, temp_buffer);

  strcat(url, "&lon=");
  dtostrf(geoDMS2DD(GPS.longitude, GPS.lon), 12, 8, temp_buffer);
  strcat(url, temp_buffer);

  // Speed is in knots and is converted to km/h
  strcat(url, "&speed=");
  dtostrf(GPS.speed * 1.852, 6, 2, temp_buffer);
  strcat(url, temp_buffer);

  Serial.println(url);

  if (fona.HTTP_GET_start(url, &statuscode, (uint16_t *)&length)) {
    Serial.println("Succesfully passed data");
    fona.HTTP_GET_end();
  }
}

void printGPSinfo() {
  Serial.print("\nTime: ");
  Serial.print(GPS.hour, DEC); Serial.print(':');
  Serial.print(GPS.minute, DEC); Serial.print(':');
  Serial.print(GPS.seconds, DEC); Serial.print('.');
  Serial.println(GPS.milliseconds);
  Serial.print("Date: ");
  Serial.print(GPS.day, DEC); Serial.print('/');
  Serial.print(GPS.month, DEC); Serial.print("/20");
  Serial.println(GPS.year, DEC);
  Serial.print("Fix: "); Serial.print((int)GPS.fix);
  Serial.print(" quality: "); Serial.println((int)GPS.fixquality);
  if (GPS.fix) {
    Serial.print("Location: ");
    Serial.print(GPS.latitude, 4); Serial.print(GPS.lat);
    Serial.print(", ");
    Serial.print(GPS.longitude, 4); Serial.println(GPS.lon);
    Serial.print("Speed (knots): "); Serial.println(GPS.speed);
    Serial.print("Angle: "); Serial.println(GPS.angle);
    Serial.print("Altitude: "); Serial.println(GPS.altitude);
    Serial.print("Satellites: "); Serial.println((int)GPS.satellites);
  }
}
