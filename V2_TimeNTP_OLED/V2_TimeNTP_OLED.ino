/*
 * V2_TimeNTP_OLED.ino
 * Example showing time sync to NTP time source
 * Heltec WiFi LoRa V2
 */

#include <WiFi.h>
#include <WiFiUdp.h>
#include <HTTPClient.h>
#include <HT_SSD1306Wire.h>
#define BOARD_LED_PIN 25

typedef unsigned long U;

const U NTP_CHECK_INTERVAL = 10000; // 2h46m
const U NTP_CHECK_AGAIN =     2000; // 33m20s
const U NTP_CHECK_JITTER =    1000; // 16m40s

// Initialize the display object with I2C address, SDA and SCL pins
static SSD1306Wire display(0x3c,500000,SDA_OLED,SCL_OLED,GEOMETRY_128_64,RST_OLED);

/*-------- HTTP stuff -------------*/

HTTPClient http;
char temperature[20] = "";

void get_temperature()
{
  U start = millis();
  Serial.println("> get_temperature");
  http.begin("https://api.open-meteo.com/v1/forecast?latitude=50.5&longitude=30.375&current=temperature_2m");
  int httpCode = http.GET();
  if(httpCode!=200)
  {
    Serial.printf("HTTP error: %d\n",httpCode);
    sprintf(temperature,"[error:%d]",httpCode);
  }
  else
  {
    temperature[0]='\0';
    String payload = http.getString();
    Serial.println(payload);
    int idx1 = payload.indexOf("\"current\":");
    if(idx1>0)
    {
      float t;
      int idx2 = payload.indexOf("\"temperature_2m\":",idx1+10);
      if( idx2>0 && sscanf(payload.c_str()+idx2+16,":%f",&t)==1 )
        sprintf(temperature,"%+0.1f°",t);
    }
  }
  http.end();
  Serial.print("< get_temperature "); Serial.print(temperature); Serial.print(" <<"); Serial.println(millis()-start);
}

/*-------- NTP/UDP stuff ----------*/

WiFiUDP udp;

static const char NTP_SERVER_NAME[] = "pool.ntp.org"; // NTP Servers

const char SSID[] = "wifinetwork";  // your wifi network SSID (name)
const char PASS[] = "cleverpasword";  // your network password

const U TIME_ZONE = +3; // Kyiv

U LOCAL_UDP_PORT = 8888; // local port to listen for UDP packets; another: 2390

const int NTP_PACKET_SIZE = 48; // NTP time is in the first 48 bytes of message
byte packet_buffer[NTP_PACKET_SIZE]; //buffer to hold incoming & outgoing packets

U get_4_byte_number( byte* buf, U pos )
{
  return (U)buf[pos] << 24 | (U)buf[pos+1] << 16 | (U)buf[pos+2] << 8 | (U)buf[pos+3];
}

void send_ntp_packet( IPAddress &address, byte* packet_buffer )
{
  memset(packet_buffer,0,NTP_PACKET_SIZE);
  // Initialize values needed to form NTP request
  packet_buffer[0] = 0b11100011;   // LI, Version, Mode
  packet_buffer[1] = 0;     // Stratum, or type of clock
  packet_buffer[2] = 6;     // Polling Interval
  packet_buffer[3] = 0xEC;  // Peer Clock Precision
  // 8 bytes of zero for Root Delay & Root Dispersion
  packet_buffer[12] = 49;
  packet_buffer[13] = 0x4E;
  packet_buffer[14] = 49;
  packet_buffer[15] = 52;
  // all NTP fields have been given values, now
  // you can send a packet requesting a timestamp:
  udp.beginPacket(address,123); //NTP requests are to port 123
  udp.write(packet_buffer,NTP_PACKET_SIZE);
  udp.endPacket();
}

U get_ntp_time( U* ms=0 ) // return local seconds since 2026.1.1 0:00:00 and ms
{
  digitalWrite(BOARD_LED_PIN,HIGH);
  IPAddress ntp_server_ip; // NTP server's ip address
  while( udp.parsePacket() > 0 ) ; // discard any previously received packets
  //_Serial.println("Transmit NTP Request");
  // get a random server from the pool
  WiFi.hostByName(NTP_SERVER_NAME,ntp_server_ip);
  //_Serial.print(NTP_SERVER_NAME); Serial.print(": "); Serial.println(ntp_server_ip);
  send_ntp_packet(ntp_server_ip,packet_buffer);
  delay(100); // 500 in ms sample
  U begin_wait = millis();
  U local_seconds_since_2026 = 0; // return 0 if unable to get NTP time
  do
  {
    U size = udp.parsePacket();
    if( size >= NTP_PACKET_SIZE )
    {
      udp.read(packet_buffer,NTP_PACKET_SIZE);  // read packet into the buffer
      // Convert four bytes starting at location 40 to a long integer
      U seconds_since_1900 = get_4_byte_number(packet_buffer,40); // Server Transmit
      if( ms )
      {
        // Extract the 4 bytes for the fraction (1/2^32 s) starting at location 44
        U transmit_fraction = get_4_byte_number(packet_buffer,44); // Server Transmit fraction
        *ms = (U)(((uint64_t)transmit_fraction * 1000) >> 32); // Convert to milliseconds
        U receive_s = get_4_byte_number(packet_buffer,32); // Server Receive, s
        U receive_fraction = get_4_byte_number(packet_buffer,36); // and fraction
        Serial.printf("Rx %u ~%u --> Tx %u ~%u (%u)  Diff %u - %u µs\n", receive_s, receive_fraction,
          seconds_since_1900, transmit_fraction, *ms, transmit_fraction-receive_fraction,
          (U)(((uint64_t)(transmit_fraction-receive_fraction)*1000000)>>32));
      }
      digitalWrite(BOARD_LED_PIN,LOW);
      local_seconds_since_2026 = seconds_since_1900 - 3976214400UL + TIME_ZONE * 3600;
      // 1900->1970 25550 days 2,208,988,800 seconds
      // 1970->2026 20454 days 1,767,225,600 seconds - exactly 2*(4*7) years!
      break;
    }
  }
  while( millis()-begin_wait < 1500 );
  digitalWrite(BOARD_LED_PIN,LOW);
  return local_seconds_since_2026;
}

// -----------------------

void format_time( U t, char* tm ) // t - seconds since midnight, 0 to <86400
{
  sprintf( tm, "%02d:%02d:%02d", t/3600, t/60%60, t%60 );
}

void days_to_ymd( U days_since_1970, int& year, int& month, int& day );

void format_date( U days_since_2026, char* dt )
{
  int y,m,d;
  days_to_ymd( days_since_2026+20454, y, m, d ); // to days since 1970
  sprintf( dt, "%d.%d.%d", y, m, d );
}

void Serial_show_date_time( U time_s, U time_ms, char* dt, char* tm, char* wd, bool prt )
{
  if(time_s==0) return;
  U seconds = time_s % (24*3600);
  U days = time_s / (24*3600);
  format_time( seconds, tm );
  format_date( days, dt );
  static const char* WEEKDAYS[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
  strcpy( wd, WEEKDAYS[(days + 4) % 7] );
  if(!prt) return;
  Serial.print(dt); Serial.print(" "); Serial.print(tm); Serial.print(".");
  if(time_ms<100) Serial.print("0"); if(time_ms<10) Serial.print("0"); Serial.print(time_ms);
  Serial.print(" "); Serial.println(wd);
}

void display_show_date_time( char* dt, char* wd, char* tm, char* ms )
{
  display.clear();
  // display.setColor(BLACK); display.fillRect(0,19,70,17); display.setColor(WHITE);
  display.setFont(ArialMT_Plain_16);
  display.drawString(0,0,dt);
  display.drawString(97,0,wd);
  display.setFont(ArialMT_Plain_24);
  display.drawString(0,19,tm);
  display.setFont(ArialMT_Plain_16); // was 10
  display.drawString(98,24,ms); // was 72,23
  display.drawString(0,48,temperature);
  display.display();
}

// Timing configuration variables
// _sms - s+ms, _s, _ms - s and ms parts, _d - days, _ds - seconds inside day
static uint64_t last_ntp_time_sms = 0; // true time
static uint64_t delta_sms = 0;
static U ntp_next_millis = 0; // to read NTP next time
U next_millis = 0; // to show time nex time

U get_and_show_date_time()
{
  U ms_before = millis();
  U ntp_ms;
  U ntp_s = get_ntp_time( &ntp_ms ); // seconds since 1970.1.1 0:00:00 local time
  U ms_after = millis();
  Serial.print("get_ntp_time took (ms) "); Serial.println(ms_after-ms_before);
  if(ntp_s==0)
  {
    Serial.println("Couldn't get NTP time");
    return 0;
  }
  U one_way_trip_ms = (ms_after - ms_before + 1) / 2; // 1 - typical NTP delta between receive and transmit
  last_ntp_time_sms = (uint64_t)ntp_s*1000 + ntp_ms + one_way_trip_ms; // corresponding to moment ms_after
  delta_sms = last_ntp_time_sms - ms_after;
  U real_s = (U)(last_ntp_time_sms / 1000);
  U real_ms = (U)(last_ntp_time_sms % 1000);

  Serial.print("NTP time returned: "); Serial.print(ntp_s); Serial.print(".");
  if(ntp_ms<100) Serial.print("0"); if(ntp_ms<10) Serial.print("0"); Serial.println(ntp_ms);
  Serial.print("One way trip: "); Serial.println(one_way_trip_ms);
  Serial.print("NTP true time ms: "); Serial.print(last_ntp_time_sms);
  Serial.print("  DELTA "); Serial.println(delta_sms);

  char dt[20], tm[20], wd[20], ms[20];
  Serial_show_date_time(real_s,real_ms,dt,tm,wd,true);
  sprintf(ms,"%03d",real_ms);
  display_show_date_time(dt,wd,tm,ms);
  return ms_after;
}

void just_show_time( U millis_sms )
{
  uint64_t ntp_time_sms = millis_sms + delta_sms;
  U real_s = (U)(ntp_time_sms / 1000);
  U real_ms = (U)(ntp_time_sms % 1000);
  char dt[20], tm[20], wd[20], ms[20];
  Serial_show_date_time(real_s,real_ms,dt,tm,wd,false);
  sprintf(ms,"%03d",real_ms);
  display_show_date_time(dt,wd,tm,ms);
}

void setup()
{
  Serial.begin(115200);
  delay(200);
  pinMode(BOARD_LED_PIN, OUTPUT);

  // Initialize and configure Heltec native display
  display.init();
  display.screenRotate(ANGLE_0_DEGREE);
  display.setTextAlignment(TEXT_ALIGN_LEFT);

  Serial.printf("WiFi: Connecting to '%s'\n",SSID); // not shown for some reason...
  WiFi.begin(SSID,PASS);
  while( WiFi.status() != WL_CONNECTED ) { delay(500); Serial.print("."); } Serial.println("");
  Serial.print("IP number assigned by DHCP is "); Serial.println(WiFi.localIP());
  get_temperature();
  Serial.println("Starting UDP");
  udp.begin(LOCAL_UDP_PORT);
  Serial.println("Waiting for sync");
  U millis_ms;
  for( millis_ms = get_and_show_date_time(); millis_ms==0; millis_ms = get_and_show_date_time() )
  {
    Serial.println("Waiting 30s");
    delay(30000);
  }
  delay(10000);
  Serial.println("ONCE MORE"); // do it again - now travel time should be much shorter
  for( millis_ms = get_and_show_date_time(); millis_ms==0; millis_ms = get_and_show_date_time() )
  {
    Serial.println("Waiting 30s");
    delay(30000);
  }
  U fraction_ms = (U)( last_ntp_time_sms % 1000 );
  Serial.print("[1] millis_ms "); Serial.print(millis_ms); Serial.print(" fr_ms "); Serial.println(fraction_ms);
  next_millis = millis_ms + (1000-fraction_ms) + 1000; // Shift to the whole second bound
  ntp_next_millis = next_millis + NTP_CHECK_INTERVAL*1000;
  Serial.print("[2] next_millis "); Serial.println(next_millis);
  Serial.print("[3] ntp_next_millis "); Serial.println(ntp_next_millis);
}

void loop()
{
  U m = millis();
  if( m >= ntp_next_millis )
  {
    U millis_ms = get_and_show_date_time();
    if(millis_ms==0)
    {
      ntp_next_millis += NTP_CHECK_AGAIN*1000;
      Serial.print("[9] ntp_next_millis "); Serial.println(ntp_next_millis);
    }
    else
    {
      U fraction_ms = (U)( last_ntp_time_sms % 1000 );
      Serial.print("[5] millis_ms "); Serial.print(millis_ms); Serial.print(" fr_ms "); Serial.println(fraction_ms);
      U jitter = esp_random() % NTP_CHECK_JITTER; // 0–16 min jitter in ms
      Serial.print("[6] jitter "); Serial.println(jitter);
      next_millis = millis_ms + (1000-fraction_ms); // Shift to the whole second bound
      if(fraction_ms>=500) next_millis += 1000;
      ntp_next_millis = next_millis + (NTP_CHECK_INTERVAL + jitter)*1000; // ≈ 10,000,000 ms (2h46m) ± 1,000,000 (16m40s)
      Serial.print("[7] next_millis "); Serial.println(next_millis);
      Serial.print("[8] ntp_next_millis "); Serial.println(ntp_next_millis);
    }
    get_temperature();
  }
  else if( m >= next_millis )
  {
    just_show_time(m);
    next_millis += 1000;
  }
  delay(20);
}

void days_to_ymd( U days_since_1970, int& year, int& month, int& day )
{
  // Shift epoch to 1968-03-01 (the closest March 1st leap-cycle start)
  // 671 days between 1968-03-01 and 1970-01-01
  U t = days_since_1970 + 671;
  // 1461 days = 3 years of 365 days + 1 leap year of 366 days
  U cycle4 = t / 1461;
  U doc4 = t % 1461; // Day of 4-year cycle
  // Estimate year within the 4-year cycle
  U yoc = (doc4 - doc4 / 1460) / 365;
  year = 1968 + cycle4 * 4 + yoc; // Change 1968 to 2024 for since_2026
  // Day of year (March 1st is day 0)
  U doy = doc4 - (365 * yoc + yoc / 4);
  // Magic formula for month and day (March = 0, April = 1, etc.)
  U mp = (5 * doy + 2) / 153;
  day = doy - (153 * mp + 2) / 5 + 1;
  month = mp < 10 ? mp + 3 : mp - 9;
  year += (month <= 2); // next year for Jan or Feb
}

// https://api.open-meteo.com/v1/forecast?latitude=50.5&longitude=30.375&current=temperature_2m,precipitation&hourly=temperature_2m,precipitation_probability&timezone=Europe/Kyiv
// https://api.open-meteo.com/v1/forecast?latitude=50.5&longitude=30.375&current=temperature_2m&timezone=Europe/Kyiv
// {"latitude":50.5,"longitude":30.375,"generationtime_ms":0.05555152893066406,"utc_offset_seconds":10800,"timezone":"Europe/Kiev",
// "timezone_abbreviation":"GMT+3","elevation":164.0,"current_units":{"time":"iso8601","interval":"seconds","temperature_2m":"°C"},
// "current":{"time":"2026-05-21T20:30","interval":900,"temperature_2m":24.8}}

