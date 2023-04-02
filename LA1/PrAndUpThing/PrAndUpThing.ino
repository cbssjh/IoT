#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <Update.h>
#define FIRMWARE_SERVER_IP_ADDR "192.168.4.2"
#define FIRMWARE_SERVER_PORT    "8000"

WebServer webServer(80);

int firmwareVersion = 8;

//LEDs for connection of Wifi
const int red = 9;
const int green = 10;

//LEDs for OTA
const int yellow_wifi = 12;
const int green_wifi = 11;

const char *templatePage[] = {
  "<html><head><title>",                                               
  "Wifi Connect",                                                     
  "</title>\n",                                                         
  "<meta charset='utf-8'>",                                            
  "<meta name='viewport' content='width=device-width, initial-scale=1.0'>\n"
  "<style>body{background:#FFF; color: #000; font-family: sans-serif;", 
  "font-size: 150%;}</style>\n",                                        
  "</head><body>\n",                                                   
  "<h2>Welcome to Thing!</h2>\n",                                                                                     
};

void setup(){
  Serial.begin(115200);
  pinMode(red, OUTPUT);
  pinMode(green, OUTPUT);
  pinMode(yellow_wifi, OUTPUT);
  pinMode(green_wifi, OUTPUT);
  
  delay(10000);
  
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP("SEO-ESP32", "97051597");
  Serial.println("Started AP mode, IP address: " + WiFi.softAPIP().toString());

  webServer.on("/", handleRoot);
  webServer.on("/wifi", handleWifi);
  webServer.on("/status", handleStatus);
  webServer.on("/wifichz", handleWifi2); 
  webServer.onNotFound(handleNotFound);

  webServer.begin();
  Serial.println("HTTP server started");
  Serial.printf("running firmware is at version %d\n", firmwareVersion);

  // get on the network
  WiFi.begin("uos-other", "shefotherkey05"); // register MAC first! and add SSID/PSK details if needed
  uint16_t connectionTries = 0;
  Serial.print("trying to connect to Wifi...");
  while(WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    if(connectionTries++ % 75 == 0) Serial.println("");
    delay(250);
    digitalWrite(yellow_wifi, HIGH);
  }
  delay(500); // let things settle for half a second
  Serial.println("connected :)");
  digitalWrite(yellow_wifi, LOW);
  digitalWrite(green_wifi, HIGH);

  // materials for doing an HTTPS GET on github from the BinFiles/ dir
  HTTPClient http;
  int respCode;
  int highestAvailableVersion = -1;

  // read the version file from the cloud
  respCode = doCloudGet(&http, "version.txt");
  if(respCode > 0) // check response code (-ve on failure)
    highestAvailableVersion = atoi(http.getString().c_str());
  else
    Serial.printf("couldn't get version! rtn code: %d\n", respCode);
    digitalWrite(red, HIGH);
  http.end(); // free resources

  // do we know the latest version, and does the firmware need updating?
  if(respCode < 0) {
    return;
  } else if(firmwareVersion >= highestAvailableVersion) {
    Serial.printf("firmware is up to date\n");
    digitalWrite(red, HIGH);
    digitalWrite(green, HIGH);
    return;
  }

  // do a firmware update
  Serial.printf(
    "upgrading firmware from version %d to version %d\n",
    firmwareVersion, highestAvailableVersion
  );

  // do a GET for the .bin, e.g. "23.bin" when "version.txt" contains 23
  String binName = String(highestAvailableVersion);
  binName += ".bin";
  respCode = doCloudGet(&http, binName);
  int updateLength = http.getSize();

  // possible improvement: if size is improbably big or small, refuse
  if(respCode > 0 && respCode != 404) { // check response code (-ve on failure)
    Serial.printf(".bin code/size: %d; %d\n\n", respCode, updateLength);
  } else {
    Serial.printf("failed to get .bin! return code is: %d\n", respCode);
    http.end(); // free resources
    return;
  }

  // write the new version of the firmware to flash
  WiFiClient stream = http.getStream();
  Update.onProgress(handleOTAProgress); // print out progress
  if(Update.begin(updateLength)) {
    Serial.printf("starting OTA may take a minute or two...\n");
    digitalWrite(red, LOW);
    digitalWrite(green, HIGH);
    delay(1000);
    digitalWrite(green, LOW);
    delay(1000);
    digitalWrite(green, HIGH);
    delay(1000);
    digitalWrite(green, LOW);
    Update.writeStream(stream);
    if(Update.end()) {
      Serial.printf("update done, now finishing...\n");
      digitalWrite(red, LOW);
      digitalWrite(green, HIGH);
      Serial.flush();
      if(Update.isFinished()) {
        Serial.printf("update successfully finished; rebooting...\n\n");
        ESP.restart();
      } else {
        Serial.printf("update didn't finish correctly :(\n");
        Serial.flush();
      }
    } else {
      Serial.printf("an update error occurred, #: %d\n" + Update.getError());
      Serial.flush();
    }
  } else {
    Serial.printf("not enough space to start OTA update :(\n");
    Serial.flush();
  }
  stream.flush();
}

void loop(){

  webServer.handleClient();
  
}

void handleRoot() {
  
  String htmlPage = "";
  htmlPage+= "<h2>Network Configuration</h2>";
  htmlPage+= "<p>Choose a <a href=\"wifi\">wifi access point</a>.</p>";
  htmlPage+= "<p>Check <a href='/status'>wifi status</a>.</p>";
  
  webServer.send(200, "text/html", htmlPage);
}

void handleWifi(){
  const char *checked = " checked";
  int n = WiFi.scanNetworks();

  String htmlPage = "";

  if(n == 0) {
    htmlPage += "No wifi access points found :-( ";
    htmlPage += "<a href='/'>Back</a><br/><a href='/wifi'>Try again?</a></p>\n";
  } else {
    htmlPage += "<p>Wifi access points available:</p>\n"
         "<p><form method='POST' action='wifichz'> ";
    for(int i = 0; i < n; ++i) {
      htmlPage.concat("<input type='radio' name='ssid' value='");
      htmlPage.concat(WiFi.SSID(i));
      htmlPage.concat("'");
      htmlPage.concat(checked);
      htmlPage.concat(">");
      htmlPage.concat(WiFi.SSID(i));
      htmlPage.concat(" (");
      htmlPage.concat(WiFi.RSSI(i));
      htmlPage.concat(" dBm)");
      htmlPage.concat("<br/>\n");
      checked = "";
    }
    htmlPage += "<br/>Pass key: <input type='textarea' name='key'><br/><br/> ";
    htmlPage += "<input type='submit' value='Submit'></form></p>";
  }
  webServer.send(200, "text/html", htmlPage);
}

void handleNotFound() {
  Serial.println("URI Not Found: ");
  Serial.println(webServer.uri());
  webServer.send(200, "text/plain", "URI Not Found");
}

void handleStatus(){
  String s = "";
  
  s += "<ul>\n";
  s += "\n<li>SSID: ";
  s += WiFi.SSID();
  s += "</li>";
  s += "\n<li>Status: ";
  switch(WiFi.status()) {
    case WL_IDLE_STATUS:
      s += "WL_IDLE_STATUS</li>"; break;
    case WL_NO_SSID_AVAIL:
      s += "WL_NO_SSID_AVAIL</li>"; break;
    case WL_SCAN_COMPLETED:
      s += "WL_SCAN_COMPLETED</li>"; break;
    case WL_CONNECTED:
      s += "WL_CONNECTED</li>"; break;
    case WL_CONNECT_FAILED:
      s += "WL_CONNECT_FAILED</li>"; break;
    case WL_CONNECTION_LOST:
      s += "WL_CONNECTION_LOST</li>"; break;
    case WL_DISCONNECTED:
      s += "WL_DISCONNECTED</li>"; break;
    default:
      s += "unknown</li>";
   }
   s += "\n<li>Local IP: ";     s += ip2str(WiFi.localIP());
   s += "</li>\n";
   s += "\n<li>Soft AP IP: ";   s += ip2str(WiFi.softAPIP());
   s += "</li>\n";
   s += "\n<li>AP SSID name: "; s += "SEO-ESP32";
   s += "</li>\n";

   s += "</ul></p>";

  webServer.send(200, "text/html", s);
}

void handleWifi2(){
  
  String htmlPage = "";
  String ssid = "";
  String key = "";
  
  for(int i = 0; i < webServer.args(); i++ ) {
    if(webServer.argName(i) == "ssid")
      ssid = webServer.arg(i);
    else if(webServer.argName(i) == "key")
      key = webServer.arg(i);
  }

  if(ssid == "") {
    htmlPage = "<h2>Ooops, no SSID...?</h2>\n<p>Looks like a bug :-(</p>";
  } else {
    htmlPage+= "<h2>Joining wifi network...</h2>";
    htmlPage+= "<a href='/status'>wifi status</a>";
    char ssidchars[ssid.length()+1];
    char keychars[key.length()+1];
    ssid.toCharArray(ssidchars, ssid.length()+1);
    key.toCharArray(keychars, key.length()+1);
    WiFi.begin(ssidchars, keychars);
  }
  webServer.send(200, "text/html", htmlPage);
}

String ip2str(IPAddress address) { // utility for printing IP addresses
  return
    String(address[0]) + "." + String(address[1]) + "." +
    String(address[2]) + "." + String(address[3]);
}
int doCloudGet(HTTPClient *http, String fileName) {
  String url =
    String("http://") + FIRMWARE_SERVER_IP_ADDR + ":" +
    FIRMWARE_SERVER_PORT + "/" + fileName;
  Serial.printf("getting %s\n", url.c_str());

  // make GET request and return the response code
  http->begin(url);
  http->addHeader("User-Agent", "ESP32");
  return http->GET();
}

// callback handler for tracking OTA progress ///////////////////////////////
void handleOTAProgress(size_t done, size_t total) {
  float progress = (float) done / (float) total;

  int barWidth = 70;
  Serial.printf("[");
  int pos = barWidth * progress;
  for(int i = 0; i < barWidth; ++i) {
    if(i < pos)
      Serial.printf("=");
    else if(i == pos)
      Serial.printf(">");
    else
      Serial.printf(" ");
  }
  Serial.printf(
    "] %d %%%c", int(progress * 100.0), (progress == 1.0) ? '\n' : '\r'
  );
}
