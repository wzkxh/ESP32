// V2_TimeNTP_OLED.ino
// Sync time with NTP time source, get weather (temperature) from open-meteo, show it on OLED display
// Heltec WiFi LoRa V2 - ESP32-D0WDQ6 (revision 1)

#include <WiFi.h>
#include <WiFiUdp.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <HT_SSD1306Wire.h>
#define BOARD_LED_PIN 25

typedef unsigned long U;

const U NTP_CHECK_INTERVAL = 150*60; // 2h30m
const U NTP_CHECK_REPEAT = 30*60; // 30m
const U NTP_CHECK_JITTER = 15*60; // 0..15m
const U WEATHER_POLL = 15; // 15m
const U TIME_ZONE = +3; // Kyiv

// Initialize the display object with I2C address, SDA and SCL pins
static SSD1306Wire oled(0x3c,500000,SDA_OLED,SCL_OLED,GEOMETRY_128_64,RST_OLED);

// HTTP: Weather (temperature)

static WiFiClientSecure wificlient;
char temperature[20] = "";
char temperature_time[20] = "";
static U next_weather_poll = 0;
static U last_time_minutes = 0; // minutes of the last time retrieved; then we'll poll temperature at 1,16,31,46 minutes
static char last_time_string[20] = ""; // just hh:mm:ss to show in the log

void get_temperature()
{
  U start = millis(); // for logging only
  HTTPClient http;
  if( !http.begin("https://api.open-meteo.com/v1/forecast?latitude=50.5&longitude=30.375&current=temperature_2m") )
    return;
  http.setUserAgent("ESP32-Client");
  digitalWrite(BOARD_LED_PIN,HIGH);
  int httpCode = http.GET();
  digitalWrite(BOARD_LED_PIN,LOW);
  if(httpCode!=HTTP_CODE_OK)
  {
    if( temperature_time[0]=='\0' )
      strcpy(temperature_time,"*");
    else if( temperature_time[strlen(temperature_time)-1]!='*' )
      strcat(temperature_time,"*");
  }
  else
  {
    String payload = http.getString();
    int idx0 = payload.indexOf("\"current\":");
    if(idx0>0)
    {
      float t; int r1,r2; int h, m;
      int idx1 = payload.indexOf("\"temperature_2m\":",idx0+10);
      if( idx1>0 && (r1=sscanf(payload.c_str()+idx1+16,":%f",&t))==1 )
        sprintf(temperature,"%+0.1f°",t);
      int idx2 = payload.indexOf("\"time\":",idx0+10);
      if( idx2>0 && (r2=sscanf(payload.c_str()+idx2+6,":\"%*d-%*d-%*dT%d:%d\"",&h,&m))== 2 )
        sprintf(temperature_time,"at  %02d:%02d",(h+TIME_ZONE)%24,m);
      if( (idx1>0 && r1==1) && (idx2<=0 || r2!=2) ) strcat(temperature,"!"); // mark that only this component is actual
      if( (idx1<=0 || r1!=1) && (idx2>0 && r2==2) ) strcat(temperature_time,"!");
    }
  }
  http.end();
  Serial.printf("T %s %s %s %ums\n",last_time_string,temperature,temperature_time,millis()-start);
}

void get_and_show_temperature()
{
  get_temperature();
  U minutes_to_quarter_start = WEATHER_POLL-((last_time_minutes+WEATHER_POLL-1)%WEATHER_POLL); // quarter + 1 minute
  next_weather_poll = millis() + minutes_to_quarter_start*60000;
}

// UDP: NTP time

WiFiUDP udp;

static const char NTP_SERVER_NAME[] = "pool.ntp.org"; // NTP Servers

const char SSID[] = "wifi-network";  // your wifi network SSID (name)
const char PASS[] = "clever-password";  // your network password

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
  WiFi.hostByName(NTP_SERVER_NAME,ntp_server_ip); // get a random server from the pool
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

// Main part

void days_to_ymd( U days_since_1970, int& year, int& month, int& day )
{
  // Shift epoch to 1968-03-01 (the closest March 1st leap-cycle start)
  U t = days_since_1970 + 671; // 671 days between 1968-03-01 and 1970-01-01
  U cycle4 = t / 1461; // 1461 days = 3 years of 365 days + 1 leap year of 366 days
  U doc4 = t % 1461; // Day of 4-year cycle
  U yoc = (doc4 - doc4 / 1460) / 365; // Estimate year within the 4-year cycle
  year = 1968 + cycle4 * 4 + yoc; // *Change 1968 to 2024 for since_2026
  U doy = doc4 - (365 * yoc + yoc / 4); // Day of year (March 1st is day 0)
  // Magic formula for month and day (March = 0, April = 1, etc.)
  U mp = (5 * doy + 2) / 153; // ... because average month is 30.6 days
  day = doy - (153 * mp + 2) / 5 + 1;
  month = mp < 10 ? mp + 3 : mp - 9;
  year += (month <= 2); // next year for Jan or Feb
}

void format_date( U days_since_2026, char* dt )
{
  int y,m,d;
  days_to_ymd( days_since_2026+20454, y, m, d ); // to days since 1970
  sprintf( dt, "%d.%d.%d", y, m, d ); // return
}

void format_time( U t, char* tm ) // t - seconds since midnight, 0 to <86400
{
  last_time_minutes = t/60%60; // save minutes for weather poll schedule
  sprintf( tm, "%02u:%02u:%02u", t/3600, last_time_minutes, t%60 );
  strcpy( last_time_string, tm ); // leave it there for other routines (currently - for log)
}

void split_ntp_time( uint64_t ntp_time_ms, char* dt, char* tm, char* ms, char* wd )
{
  U time_s = (U)(ntp_time_ms / 1000);
  U time_ms = (U)(ntp_time_ms % 1000);
  if(time_s==0) return;
  U seconds = time_s % (24*3600);
  U days = time_s / (24*3600);
  sprintf( ms, "%03u", time_ms );
  format_time( seconds, tm );
  format_date( days, dt );
  static const char* WEEKDAYS[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
  strcpy( wd, WEEKDAYS[(days + 4) % 7] );
}

void display_date_time( char* dt, char* wd, char* tm, char* ms )
{
  oled.clear();
  // oled.setColor(BLACK); oled.fillRect(0,19,70,17); oled.setColor(WHITE);
  oled.setFont(ArialMT_Plain_16);
  oled.drawString(0,0,dt);
  oled.drawString(97,0,wd);
  oled.setFont(ArialMT_Plain_24);
  oled.drawString(0,19,tm);
  oled.setFont(ArialMT_Plain_16);
  oled.drawString(98,24,ms); // was 72,23
  oled.drawString(0,48,temperature);
  oled.setFont(ArialMT_Plain_10);
  oled.drawString(56,52,temperature_time);
  oled.display();
}

// Timing configuration variables
static uint64_t last_ntp_time_ms = 0; // true time, ms
static uint64_t delta_ms = 0; // difference between millis() and last_ntp_time_ms
static U ntp_next_millis = 0; // to read NTP next time
static U next_millis = 0; // to show time nex time

U get_and_show_date_time()
{
  U ms_before = millis();
  U ntp_ms;
  U nt-p_s = get_ntp_time( &ntp_ms ); // seconds since 1970.1.1 0:00:00 local time
  U ms_after = millis();
  if(ntp_s==0)
    return 0;
  U one_way_trip_ms = (ms_after - ms_before + 1) / 2; // 1 - typical NTP delta_ms between receive and transmit
  last_ntp_time_ms = (uint64_t)ntp_s*1000 + ntp_ms + one_way_trip_ms; // corresponding to moment ms_after
  delta_ms = last_ntp_time_ms - ms_after;

  char dt[20], tm[20], ms[20], wd[20];
  split_ntp_time(last_ntp_time_ms,dt,tm,ms,wd);
  display_date_time(dt,wd,tm,ms);
  Serial.printf("N %u.%03u %llu %llu %s %s.%s %s\n",ntp_s,ntp_ms,last_ntp_time_ms,delta_ms,dt,tm,ms,wd);
  return ms_after;
}

void just_show_time( U millis_ms )
{
  char dt[20], tm[20], ms[20], wd[20];
  split_ntp_time(millis_ms + delta_ms,dt,tm,ms,wd);
  display_date_time(dt,wd,tm,ms);
}

void setup()
{
  Serial.begin(115200);
  delay(200);
  pinMode(BOARD_LED_PIN, OUTPUT);

  // Initialize and configure Heltec native display
  oled.init();
  oled.screenRotate(ANGLE_0_DEGREE);
  oled.setTextAlignment(TEXT_ALIGN_LEFT);

  WiFi.begin(SSID,PASS);
  while( WiFi.status() != WL_CONNECTED ) delay(500);

  wificlient.setInsecure();
  wificlient.setTimeout(5000); // timeout 5 seconds

  udp.begin(LOCAL_UDP_PORT);
  U millis_ms = get_and_show_date_time();
  while( millis_ms==0 ) { delay(30000); millis_ms = get_and_show_date_time(); } // Don't start until we get time from NTP
  U fraction_ms = (U)( last_ntp_time_ms % 1000 );
  next_millis = millis_ms + (1000-fraction_ms) + 1000; // Shift to the whole second bound
  ntp_next_millis = next_millis + NTP_CHECK_INTERVAL*1000;

  get_and_show_temperature();
}

void loop()
{
  U m = millis();
  if( m >= next_weather_poll )
    get_and_show_temperature();
  if( m >= ntp_next_millis )
  {
    U millis_ms = get_and_show_date_time();
    if(millis_ms==0)
      ntp_next_millis += NTP_CHECK_REPEAT*1000; // Couldn't retrieve time, repeat
    else
    {
      U fraction_ms = (U)( last_ntp_time_ms % 1000 );
      next_millis = millis_ms + (1000-fraction_ms); // Shift to the whole second bound
      if(fraction_ms>=500) next_millis += 1000;
      U jitter = esp_random() % NTP_CHECK_JITTER; // 0–15 min jitter
      ntp_next_millis = next_millis + (NTP_CHECK_INTERVAL + jitter)*1000; // ≈ 2h30m + 0..15m
    }
  }
  else if( m >= next_millis ) // don't show time when previous part worked - it's shown there
  {
    just_show_time(m);
    next_millis += 1000;
  }
  delay(20);
}

// EOF
