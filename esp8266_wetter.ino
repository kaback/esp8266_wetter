//Environment: ESP8266 core (esp8266.github.io/Arduino/) 
//Hardware: ESP201


#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <WiFiUdp.h>
#include <Ticker.h>
#include "complexx.h"
#include <math.h>


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
volatile Signal pushDataSignal = unknown;
volatile Signal calcWindspeedSignal = unknown;

volatile unsigned int adc = 0;

WiFiUDP udp;
const byte led = 13;
const byte wspin = 12;
String ip("");

ESP8266WebServer server(80);
Ticker theTicker;
Ticker adcTicker;

volatile unsigned long wind_time = 0;
volatile unsigned long wind_timetemp = 0;
volatile unsigned long wind_period = 0;
float wind_speed_max = 0;
float wind_speed_gmw = 0;
float wind_aktuell = 0;
float wind_direction = 0.0;

const double tau = 2*M_PI;
Complex j(0.0, 1.0);
Complex j_tau = j * tau / 360;
Complex base = j_tau.c_exp();
Complex total(0,0);

void wind_average(const double wind_direction)
{
  // Winkel in komplexe zahl (exponentialform) umrechen,
  // gleitend Mittelwert bilden
  Complex new_value = one * base.c_pow(wind_direction);
  total = total - total/40 + new_value/40;
}

//--------------------
// handleRoot()
//
// wird aufgerufen, wenn ein HTTP Get auf / gemacht wird
//--------------------
void handleRoot() {

  server.send(200, "text/plain", "hello from esp8266 (Waidberg Wind: <a href=\"wind/\">click</a>)!");

}

//--------------------
// handleNotFound()
//
// Wird aufgerufen, wenn ein Get auf eine URL gemacht wird, fuer die sonst kein handle passt
//--------------------
void handleNotFound(){

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

}

//------------------
// handleWetter()
//
// Wird aufgerufen, wenn ein HTTP GET auf /wetter gemacht wird
//------------------
void handleWetter(){
  
  String message = " {\"version\": \"0.3\",";
  message += "\"id\": \"16fe34d8a2b0\",";
  message += "\"nickname\": \"Waidberg\",";
  message += "\"sensors\": {";
  
  message += "\"windspeed\": [{";
  message += "\"name\": \"Windgeschwindigkeit\",";
  message += "\"value\": ";
  message += (float)wind_speed_max;
  message += ",";
  message += "\"unit\": \"m/s\"";
  message += "},";//],";

  message += "{";
  message += "\"name\": \"Windgeschwindigkeit(gmw20)\",";
  message += "\"value\": ";
  // Der gleitende Mittelwert fuer die Windgeschwindigkeit
  // funktioniert nur wenn auch wind ist. Mit Windgeschwindigkeiten
  // von 0 kann er noch nicht umgehen.
  if (wind_speed_max == 0)
  {
    message += 0;
  } else {
    message += (float)wind_speed_gmw;
  }
  message += ",";
  message += "\"unit\": \"m/s\"";
  message += "}],";

  message += "\"windvane\": [{";
  message += "\"name\": \"Windrichtung\",";
  message += "\"value\": ";
  message += (float)wind_direction;
  message += ",";
  message += "\"unit\": \"deg\"";
  message += "},"; //],";

  message += "{";
  message += "\"name\": \"Windrichtung(gmw40)\",";
  message += "\"value\": ";
  
  double tmp = (total.c_logn(base)).real();
  if(tmp < 0)
  {
      tmp = 360 + tmp;
  }
  message += tmp;
  message += ",";
  message += "\"unit\": \"deg\"";
  message += "}],";
  
  message += "},";
  message += "\"system\": {";
  message += "\"voltage\": ";
  message += adc;
  message += ",";
  message += "\"IP\": \"";
  message += ip;
  message += "\",";
  message += "\"timestamp\": 0,";
  message += "\"uptime\": 0,";
  message += "\"heap\": 0";
  message += "}}\n";
  
  server.send(200, "text/plain", message);
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
    server.on("/wind", handleWetter);
    server.onNotFound(handleNotFound);
  
    server.begin();
    Serial.println("HTTP server started");
}

//-------------------
// startWIFI()
//
// Wifi starten, verbinden, ...
//-------------------
void startWIFI(void) {
    Serial.print("Connecting to ");
    Serial.println(ssid);

    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, pass);

    while (WiFi.status() != WL_CONNECTED) {
        Serial.print(".");
        delay(500);
    }
    Serial.println("");
    Serial.println("WiFi connected");

    ip = ipToString(WiFi.localIP());
    Serial.println("IP address: ");
    Serial.println(ip);
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

//-----------------------
// enablePushDataSignal()
//
// Wird vom Ticker getriggert.
// Interrupt-like. So kurz wie moeglich..
//-----------------------
void enablePushDataSignal(void) {
  pushDataSignal = DO;
}

//-----------------------------
// pushData()
//
// Daten zur Sammelstelle puschen
//-----------------------------
void pushData(void) {
  
  String message = rcon_command;
  message += " {\"version\": \"0.3\",";
  message += "\"id\": \"16fe34d8a2b0\",";
  message += "\"nickname\": \"Waidberg\",";
  message += "\"sensors\": {";
  
  message += "\"windspeed\": [{";
  message += "\"name\": \"Windgeschwindigkeit\",";
  message += "\"value\": ";
  message += (float)wind_speed_max;
  message += ",";
  message += "\"unit\": \"m/s\"";
  message += "},";//],";

  message += "{";
  message += "\"name\": \"Windgeschwindigkeit(gmw20)\",";
  message += "\"value\": ";
  // Der gleitende Mittelwert fuer die Windgeschwindigkeit
  // funktioniert nur wenn auch wind ist. Mit Windgeschwindigkeiten
  // von 0 kann er noch nicht umgehen.
  if (wind_speed_max == 0)
  {
    message += 0;
  } else {
    message += (float)wind_speed_gmw;
  }
  message += ",";
  message += "\"unit\": \"m/s\"";
  message += "}],";

  message += "\"windvane\": [{";
  message += "\"name\": \"Windrichtung\",";
  message += "\"value\": ";
  message += (float)wind_direction;
  message += ",";
  message += "\"unit\": \"deg\"";
  message += "},"; //],";

  message += "{";
  message += "\"name\": \"Windrichtung(gmw40)\",";
  message += "\"value\": ";
  
  double tmp = (total.c_logn(base)).real();
  if(tmp < 0)
  {
      tmp = 360 + tmp;
  }
  message += tmp;
  message += ",";
  message += "\"unit\": \"deg\"";
  message += "}],";
  
  message += "},";
  message += "\"system\": {";
  message += "\"voltage\": ";
  message += adc;
  message += ",";
  message += "\"IP\": \"";
  message += ip;
  message += "\",";
  message += "\"timestamp\": 0,";
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
  //Zeit merken
  wind_timetemp = millis();

  if((wind_timetemp - wind_time) <= 3)
  {
      //this has been a bounce, since even at huricane
      //diff between two rising edges is longer than 1ms (~6ms)
      return;
  }
  
  wind_period = wind_timetemp - wind_time;
  wind_time = wind_timetemp;
      
  //set Flag that we have new data
  calcWindspeedSignal = DO;
}

void adcInterrupt(void) {
  adc = (unsigned int) analogRead(A0);
  adc2deg();
  wind_average(wind_direction);
}


void adc2deg(void) {
  if(adc >= 968)
    wind_direction = 22.5; //NNO
  else if(adc >= 880) 
    wind_direction = 337.5; //NNW
  else if(adc >= 767) 
    wind_direction = 0; //N
    else if(adc >= 606) 
    wind_direction = 67.5; //ONO
    else if(adc >= 479) 
    wind_direction = 45; //NO
    else if(adc >= 409) 
    wind_direction = 112.5; //OSO
    else if(adc >= 346) 
    wind_direction = 90; //O
    else if(adc >= 295) 
    wind_direction = 292.5; //WNW
    else if(adc >= 262) 
    wind_direction = 315; //NW
    else if(adc >= 238) 
    wind_direction = 157.5; //SSO
    else if(adc >= 227) 
    wind_direction = 135; //SO
    else if(adc >= 214) 
    wind_direction = 247.5; //WSW
    else if(adc >= 205) 
    wind_direction = 270; //W
    else if(adc >= 199) 
    wind_direction = 202.5; //SSW
    else if(adc >= 192) 
    wind_direction = 225; //SW
    else 
    wind_direction = 180; //S

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

  pinMode(A0, INPUT);


  Serial.begin(115200);
  Serial.println();
  Serial.println();

  startWIFI();
  startUDP();
  startHTTP();
  
  pinMode(wspin, INPUT_PULLUP);
  attachInterrupt(wspin, windsensorInterrupt, FALLING);

  theTicker.attach(10, enablePushDataSignal);
  adcTicker.attach(1, adcInterrupt);
  

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
    wind_speed_max = 0.0;
    //wind_aktuell = 0.0;
    pushDataSignal = DONE;
  }

  if (calcWindspeedSignal == DO)
  {
    wind_aktuell = ((1000.0/wind_period)+2.0)/3.0;
    
    if (wind_aktuell > wind_speed_max)
      wind_speed_max = wind_aktuell;

    wind_speed_gmw = wind_speed_gmw - wind_speed_gmw/20 + wind_aktuell/20;
    calcWindspeedSignal = DONE;
    
    //Serial.println(wind_speed);
  }
  server.handleClient();
  yield();
}

String ipToString(IPAddress ip){
  String s="";
  for (int i=0; i<4; i++)
    s += i  ? "." + String(ip[i]) : String(ip[i]);
  return s;
}

//
// END OF FILE
//

