/* Convert RF signal into bits (temperature sensor version) 
 * Written by : Ray Wang (Rayshobby LLC)
 * http://rayshobby.net/?p=8827
 *
 * Mangled by sweh for ESP8266 and transmitting to MQTT
 */

#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include "network_conn.h"

#define _mqttBase    "test-therm"

// For MQTT
WiFiClient espClient;
PubSubClient client(espClient);

char mqttChannel[30];  // Enough for Base + "/" + 6 hex digits + "/" + channel
char macstr[7];        // To hold part of the MAC address

// ring buffer size has to be large enough to fit
// data between two successive sync signals
#define RING_BUFFER_SIZE  256

#define SYNC_LENGTH  9000
#define SEP_LENGTH   500
#define BIT1_LENGTH  4000
#define BIT0_LENGTH  2000

#define DATAPIN  D2 // GPIO4 is D2

unsigned long timings[RING_BUFFER_SIZE];
unsigned int syncIndex1 = 0;  // index of the first sync signal
unsigned int syncIndex2 = 0;  // index of the second sync signal

bool received = false;

// detect if a sync signal is present
bool isSync(unsigned int idx)
{
  unsigned long t0 = timings[(idx+RING_BUFFER_SIZE-1) % RING_BUFFER_SIZE];
  unsigned long t1 = timings[idx];

  // on the temperature sensor, the sync signal
  // is roughtly 9.0ms. Accounting for error
  // it should be within 8.0ms and 10.0ms
  if (t0>(SEP_LENGTH-100)   && t0<(SEP_LENGTH+100)   &&
      t1>(SYNC_LENGTH-1000) && t1<(SYNC_LENGTH+1000) &&
      digitalRead(DATAPIN) == HIGH)
  {
    return true;
  }
  return false;
}

/* Interrupt handler */
ICACHE_RAM_ATTR void handler()
{
  static unsigned long duration = 0;
  static unsigned long lastTime = 0;
  static unsigned int ringIndex = 0;
  static unsigned int syncCount = 0;

  // ignore if we haven't processed the previous received signal
  if (received == true)
    return;

  // calculating timing since last change
  unsigned long time = micros();
  duration = time - lastTime;
  lastTime = time;

  // store data in ring buffer
  ringIndex = (ringIndex + 1) % RING_BUFFER_SIZE;
  timings[ringIndex] = duration;

  // detect sync signal
  if (isSync(ringIndex))
  {
    syncCount ++;

    // first time sync is seen, record buffer index
    if (syncCount == 1)
    {
      syncIndex1 = (ringIndex+1) % RING_BUFFER_SIZE;
    }
    else if (syncCount == 2)
    {
      // second time sync is seen, start bit conversion
      syncCount = 0;
      syncIndex2 = (ringIndex+1) % RING_BUFFER_SIZE;
      unsigned int changeCount = (syncIndex2 < syncIndex1) ? (syncIndex2+RING_BUFFER_SIZE - syncIndex1) : (syncIndex2 - syncIndex1);

      // changeCount must be 66 -- 32 bits x 2 + 2 for sync
      if (changeCount != 66)
      {
        received = false;
        syncIndex1 = 0;
        syncIndex2 = 0;
      } 
      else
      {
        received = true;
      }
    }
  }
}


void setup()
{
  // Let's create the channel names based on the MAC address
  unsigned char mac[6];
  WiFi.macAddress(mac);
  sprintf(macstr, "%02X%02X%02X", mac[3], mac[4], mac[5]);

  Serial.begin(115200);
  delay(500);

  Serial.println();
  Serial.print("MAC: ");
  Serial.println(WiFi.macAddress());

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);     // Connect to the network
  Serial.print("Connecting to ");
  Serial.print(ssid);
  Serial.println(" ...");

  int i = 0;
  while (WiFi.status() != WL_CONNECTED) // Wait for the Wi-Fi to connect
  {
    delay(1000);
    Serial.print(++i); Serial.print(' ');
  }

  Serial.println('\n');
  Serial.println("Connection established!");  
  Serial.print("IP address:\t");
  Serial.println(WiFi.localIP());
  Serial.print("Hostname:\t");
  Serial.println(WiFi.hostname());

  // Get the current time.  Initial log lines may be wrong 'cos
  // it's not instant.  Timezone handling... hmm.  GMT is good
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  Serial.print("Waiting for NTP time sync: ");
  time_t now = time(nullptr);
  while (now < 8 * 3600 * 2) {
    delay(500);
    Serial.print(".");
    now = time(nullptr);
  }
  Serial.println("");
  struct tm timeinfo;
  gmtime_r(&now, &timeinfo);
  Serial.print("Current time: ");
  Serial.println(asctime(&timeinfo));

  // Now we're on the network, setup the MQTT client
  client.setServer(mqttServer, mqttPort);

#ifdef NETWORK_UPDATE
   __setup_updater();
#endif

  // Attach the interrupt so when the 433 receiver gets data we can process it
  pinMode(DATAPIN, INPUT);
  attachInterrupt(digitalPinToInterrupt(DATAPIN), handler, CHANGE);
}

void loop()
{
 // Try to reconnect to MQTT each time around the loop, in case we disconnect
  while (!client.connected())
  {
    Serial.println("Connecting to MQTT Server " + String(mqttServer));

    // Generate a random ID each time
    String clientId = "ESP8266Client-therm-";
    clientId += String(random(0xffff), HEX);

    if (client.connect(clientId.c_str())) {
      Serial.println("MQTT connected.");
    } else {
      Serial.println("failed with state " + client.state());
      delay(2000);
    }
  }

  // Keep MQTT alive
  client.loop();

#ifdef NETWORK_UPDATE
  __netupdateServer.handleClient();
#endif

  if (received == true)
  {
    // disable interrupt to avoid new data corrupting the buffer
    detachInterrupt(digitalPinToInterrupt(DATAPIN));

    // loop over the lowest 12 bits of the middle 2 bytes
    unsigned long temp = 0;
    bool negative = false;
    bool fail = false;
    for(unsigned int i=(syncIndex1+24)%RING_BUFFER_SIZE; i!=(syncIndex1+48)%RING_BUFFER_SIZE; i=(i+2)%RING_BUFFER_SIZE)
    {
      unsigned long t0 = timings[i], t1 = timings[(i+1)%RING_BUFFER_SIZE];
      if (t0>(SEP_LENGTH-100) && t0<(SEP_LENGTH+100))
      {
        if (t1>(BIT1_LENGTH-1000) && t1<(BIT1_LENGTH+1000))
        {
          if(i == (syncIndex1+24)%RING_BUFFER_SIZE) negative = true;
          temp = (temp << 1) + 1;
        } 
        else if (t1>(BIT0_LENGTH-1000) && t1<(BIT0_LENGTH+1000))
        {
          temp = (temp << 1) + 0;
        } 
        else
        {
          fail = true;
        }
      } 
      else
      {
        fail = true;
      }
    }

    long celc = temp;
    if (!fail)
    {
      if (negative) {
        celc = temp -4096;
      }

      // Some form of sanity check
      if (celc > -200 && celc < 1000)
      {
        char cstr[10],fstr[10];
        sprintf(cstr,"%.1f",celc/10.0);
        sprintf(fstr,"%.1f",celc*9/50.0+32);

        Serial.print("OK ");
        Serial.print(cstr);
        Serial.print("C ");
        Serial.print(fstr);
        Serial.println("F");

        // Let's build the JSON string.  This is a kludge
        time_t now = time(nullptr);
        String tm = ctime(&now);
        tm.trim();
        String json="{\"date\":\"" + tm + " GMT\", \"degF\": \"" + fstr + "\", \"degC\": \"" + cstr + "\"}";
        client.publish(_mqttBase,json.c_str(),true);
      }
      else
      {
        Serial.print("ERR out of bounds: ");
        Serial.println(celc);
      }
    }
    else
    {
      Serial.println("ERR Decoding error.");
    } 

    // delay for 1 second to avoid repetitions
    delay(1000);
    received = false;
    syncIndex1 = 0;
    syncIndex2 = 0;

    // re-enable interrupt
    attachInterrupt(digitalPinToInterrupt(DATAPIN), handler, CHANGE);
  }
}
