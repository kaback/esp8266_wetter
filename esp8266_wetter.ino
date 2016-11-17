//Environment: ESP8266 core (esp8266.github.io/Arduino/) 
//Hardware: ESP201

#include <dht11.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <WiFiUdp.h>
#include <Ticker.h>

// Diese Werte kommen aus config.h, einfach das include auskommentieren und die Werte hier definieren
#include "config.h"
//char ssid[] = ""; //wifi SSID to connect to
//char pass[] = ""; // wifi Pass
//IPAddress thechat(x, x, x, x); //collector server
//unsigned int localPort = xxxx; //local UDP Port
//String rcon_command = ""; //rcon kommando (user:pass command)

// Darueber signalisieren wir, ob etwas getan werden soll.
enum Signal {
  unknown,
  DO,
  DONE
};

// Darueber signalisieren wir, ob json gepusht werden soll
Signal pushDataSignal = unknown;
Signal calcWindspeedSignal = unknown;

WiFiUDP udp;
const int led = 13;
dht11 DHT11;
#define DHT11PIN 4
ESP8266WebServer server(80);
Ticker theTicker;

long wind_timeold = 0;
long wind_timenew = 0;
long wind_timetemp = 0;
long wind_period = 0;
long wind_speed = 0;
long wind_tmp = 0;

//--------------------
// handleRoot()
//
// wird aufgerufen, wenn ein HTTP Get auf / gemacht wird
//--------------------
void handleRoot() {
  digitalWrite(led, 1);
  server.send(200, "text/plain", "hello from esp8266!");
  digitalWrite(led, 0);
}

//--------------------
// handleNotFound()
//
// Wird aufgerufen, wenn ein Get auf eine URL gemacht wird, fuer die sonst kein handle passt
//--------------------
void handleNotFound(){
  digitalWrite(led, 1);
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET)?"GET":"POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint8_t i=0; i<server.args(); i++){
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  server.send(404, "text/plain", message);
  digitalWrite(led, 0);
}

//------------------
// handleWetter()
//
// Wird aufgerufen, wenn ein HTTP GET auf /wetter gemacht wird
//------------------
void handleWetter(){
  digitalWrite(led, 1);
  String message = "Werkstatt\n";
  message += "Luftfeuchte (%): ";
  message += (float)DHT11.humidity;
  message += "\nTemperatur (degC): ";
  message += (float)DHT11.temperature;
  message += "\nTaupunkt (degC): ";
  message += dewPointFast(DHT11.temperature, DHT11.humidity);
  message += "\n";
  server.send(200, "text/plain", message);
  digitalWrite(led, 0);

}

//-------------------
// startWIFI()
//
// Wifi starten, verbinden, ...
//-------------------
void startWIFI(void) {
    Serial.print("Connecting to ");
    Serial.println(ssid);
    WiFi.begin(ssid, pass);

    while (WiFi.status() != WL_CONNECTED) {
        Serial.print(".");
        delay(500);
    }
    Serial.println("");
    Serial.println("WiFi connected");
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());
}

//----------------------
// startUDP()
//
// UDP starten ...
//----------------------
void startUDP(void) {
    udp.begin(localPort);
    Serial.println("UDP started");
}

//----------------------
// startHTTP()
//
// HTTP starten....
//----------------------
void startHTTP(void) {
    server.on("/", handleRoot);
    //server.on("/inline", [](){
    //  server.send(200, "text/plain", "this works as well");
    //});
    server.on("/wetter", handleWetter);
    server.onNotFound(handleNotFound);
  
    server.begin();
    Serial.println("HTTP server started");
}


//-----------------------
// enablePushDataSignal()
//
// Wird vom Ticker getriggert.
// Interrupt-like. So kurz wie moeglich..
//-----------------------
void enablePushDataSignal(void) {
  pushDataSignal = DO;
}

// dewPoint function NOAA
// reference (1) : http://wahiduddin.net/calc/density_algorithms.htm
// reference (2) : http://www.colorado.edu/geography/weather_station/Geog_site/about.htm
//
double dewPoint(double celsius, double humidity)
{
  // (1) Saturation Vapor Pressure = ESGG(T)
  double RATIO = 373.15 / (273.15 + celsius);
  double RHS = -7.90298 * (RATIO - 1);
  RHS += 5.02808 * log10(RATIO);
  RHS += -1.3816e-7 * (pow(10, (11.344 * (1 - 1/RATIO ))) - 1) ;
  RHS += 8.1328e-3 * (pow(10, (-3.49149 * (RATIO - 1))) - 1) ;
  RHS += log10(1013.246);

        // factor -3 is to adjust units - Vapor Pressure SVP * humidity
  double VP = pow(10, RHS - 3) * humidity;

        // (2) DEWPOINT = F(Vapor Pressure)
  double T = log(VP/0.61078);   // temp var
  return (241.88 * T) / (17.558 - T);
}

// delta max = 0.6544 wrt dewPoint()
// 6.9 x faster than dewPoint()
// reference: http://en.wikipedia.org/wiki/Dew_point
double dewPointFast(double celsius, double humidity)
{
  double a = 17.271;
  double b = 237.7;
  double temp = (a * celsius) / (b + celsius) + log(humidity*0.01);
  double Td = (b * temp) / (a - temp);
  return Td;
}

//-----------------------------
// pushData()
//
// Daten zur Sammelstelle puschen
//-----------------------------
void pushData(void) {

  readDHT11();
  
  String message = rcon_command;
  message += " {\"version\": \"0.3\",";
  message += "\"id\": \"16fe34d8a2b0\",";
  message += "\"nickname\": \"Rohnstedt\",";
  message += "\"sensors\": {";
  
  message += "\"humidity\": [{";
  message += "\"name\": \"Luftfeuchte_Werkstatt\",";
  message += "\"value\": ";
  message += (float)DHT11.humidity;
  message += ",";
  message += "\"unit\": \"%\"";
  message += "}],";
  
  message += "\"windspeed\": [{";
  message += "\"name\": \"Windgeschwindigkeit_Hof\",";
  message += "\"value\": ";
  message += (float)wind_speed;
  message += ",";
  message += "\"unit\": \"m/s\"";
  message += "}],";

  message += "\"temperature\": [{";
  message += "\"name\": \"Temperatur_Werkstatt\",";
  message += "\"value\": ";
  message += (float)DHT11.temperature;
  message += ",";
  message += "\"unit\": \"deg\"";
  message += "}],";

  message += "\"dewpoint\": [{";
  message += "\"name\": \"Taupunkt_Werkstatt\",";
  message += "\"value\": ";
  message += dewPointFast(DHT11.temperature, DHT11.humidity);
  message += ",";
  message += "\"unit\": \"deg\"";
  message += "}]";
  
  message += "},";
  message += "\"system\": {";
  message += "\"voltage\": 0.0,";
  message += "\"timestamp\": 1463229197,";
  message += "\"uptime\": 0,";
  message += "\"heap\": 0";
  message += "}}\n";
  
  int message_len = message.length() + 1;
  
  char msgbuffer[message_len];
 
  message.toCharArray(msgbuffer, message_len);
    
  udp.beginPacket(thechat, 9910);
  udp.write(msgbuffer);
  udp.endPacket();
  Serial.println(msgbuffer);

  readUDP();
}

//----------------------
// readDHT11()
//
// DHT11 Tempsensor auslesen
//----------------------
void readDHT11(void) {
   int chk = DHT11.read(DHT11PIN);

  //Serial.print("Read sensor: ");
  switch (chk)
  {
    case DHTLIB_OK: 
    //Serial.println("OK"); 
    break;
    case DHTLIB_ERROR_CHECKSUM: 
    Serial.println(" DHT11 Checksum error"); 
    break;
    case DHTLIB_ERROR_TIMEOUT: 
    Serial.println("DHT11 Time out error"); 
    break;
    default: 
    Serial.println("DHT11 Unknown error"); 
    break;
  }
}

//-----------------
//readUDP()
//
// Nachsehen, ob wir etwas aus dem UDP Empfangspuffer lesen können
//-----------------
void readUDP() {
   char rcvbuffer[16];
  delay(2000);
  int cb = udp.parsePacket();
  if (!cb) {
    //Serial.println("no packet yet");
  }
  else {
    Serial.print("UDP packet received, length=");
    Serial.println(cb);
    udp.read(rcvbuffer, 16);
    Serial.println(rcvbuffer);
  }
}

void windsensorInterrupt(void) {
   wind_timeold = wind_timenew;
   wind_timetemp = millis();

  if((wind_timetemp - wind_timeold) <= 1)
  {
      //this has been a bounce, since even at huricane
      //diff between two rising edges is longer than 1ms (~6ms)
  } else {
      wind_timenew = wind_timetemp;
      wind_period = wind_timenew - wind_timeold;
      //set Flag that we have new data
      calcWindspeedSignal = DO;
  }   
   
}

//################################################################
//################################################################

//--------------------------
// setup()
//
// ARDUINO setup() Routine
//--------------------------
void setup()
{

  pinMode(led, OUTPUT);
  digitalWrite(led, 0);

  Serial.begin(115200);
  Serial.println();
  Serial.println();

  startWIFI();
  startUDP();
  startHTTP();

  attachInterrupt(0, windsensorInterrupt, FALLING);

  theTicker.attach(4*60, enablePushDataSignal);

}

//----------------------------
// loop()
//
// ARDUINO loop Routine
//----------------------------
void loop()
{
  //connect wifi if not connected
  if (WiFi.status() != WL_CONNECTED) {
    startWIFI();
  }
    
  if (pushDataSignal == DO)
  {
    pushData();
    wind_speed = 0;
    pushDataSignal = DONE;
  }

  if (calcWindspeedSignal == DO)
  {
    wind_tmp = ((1000/wind_period)+2)/3;
    
    if (wind_tmp > wind_speed)
      wind_speed = wind_tmp;
      
    calcWindspeedSignal = DONE;
    
    //Serial.println(wind_speed);
  }
  server.handleClient();
  yield();
}



//
// END OF FILE
//

