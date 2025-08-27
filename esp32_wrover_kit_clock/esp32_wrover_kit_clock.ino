#define NTP 1

/*  Rui Santos & Sara Santos - Random Nerd Tutorials - https://RandomNerdTutorials.com/esp32-cyd-lvgl-digital-clock/  |  https://RandomNerdTutorials.com/esp32-tft-lvgl-digital-clock/
    THIS EXAMPLE WAS TESTED WITH THE FOLLOWING HARDWARE:
    1) ESP32-2432S028R 2.8 inch 240×320 also known as the Cheap Yellow Display (CYD): https://makeradvisor.com/tools/cyd-cheap-yellow-display-esp32-2432s028r/
      SET UP INSTRUCTIONS: https://RandomNerdTutorials.com/cyd-lvgl/
    2) REGULAR ESP32 Dev Board + 2.8 inch 240x320 TFT Display: https://makeradvisor.com/tools/2-8-inch-ili9341-tft-240x320/ and https://makeradvisor.com/tools/esp32-dev-board-wi-fi-bluetooth/
      SET UP INSTRUCTIONS: https://RandomNerdTutorials.com/esp32-tft-lvgl/
    Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files.
    The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
*/

/*  Install the "lvgl" library version 9.2 by kisvegabor to interface with the TFT Display - https://lvgl.io/
    *** IMPORTANT: lv_conf.h available on the internet will probably NOT work with the examples available at Random Nerd Tutorials ***
    *** YOU MUST USE THE lv_conf.h FILE PROVIDED IN THE LINK BELOW IN ORDER TO USE THE EXAMPLES FROM RANDOM NERD TUTORIALS ***
    FULL INSTRUCTIONS AVAILABLE ON HOW CONFIGURE THE LIBRARY: https://RandomNerdTutorials.com/cyd-lvgl/ or https://RandomNerdTutorials.com/esp32-tft-lvgl/   */
#include <lvgl.h>

/*  Install the "TFT_eSPI" library by Bodmer to interface with the TFT Display - https://github.com/Bodmer/TFT_eSPI
    *** IMPORTANT: User_Setup.h available on the internet will probably NOT work with the examples available at Random Nerd Tutorials ***
    *** YOU MUST USE THE User_Setup.h FILE PROVIDED IN THE LINK BELOW IN ORDER TO USE THE EXAMPLES FROM RANDOM NERD TUTORIALS ***
    FULL INSTRUCTIONS AVAILABLE ON HOW CONFIGURE THE LIBRARY: https://RandomNerdTutorials.com/cyd-lvgl/ or https://RandomNerdTutorials.com/esp32-tft-lvgl/   */
#include <TFT_eSPI.h>

#include <WiFi.h>
#ifdef NTP
#include "time.h"
#include "esp_sntp.h"
#else
#include <HTTPClient.h>
#include <ArduinoJson.h>
#endif

// Replace with your network credentials
const char* ssid = "xxx";
const char* password = "yyy";


#ifdef NTP
const char* ntpServer1 = "pool.ntp.org";
const char* ntpServer2 = "time.nist.gov";
const char* ntpServer3 = "time.google.com";
const long gmtOffset_sec = 28800;  // tw need +8hr,28800=8*60*60
const int daylightOffset_sec = 0;  // tw no need daylight save

struct tm timeinfo;
#else
// Specify the timezone you want to get the time for: http://worldtimeapi.org/api/timezone
// Timezone example for Portugal: "Europe/Lisbon"
const char* timezone = "Asia/Taipei";
#endif

static unsigned long refresh_tick = 0;

uint8_t LCD_BL = 5;

// Store date and time
String current_date;
String current_time;

// Store hour, minute, second
static int32_t hour;
static int32_t minute;
static int32_t second;
static int day_of_week;
bool sync_time_date = false;
bool do_lvgl_update = true;

// Set up the rgb led names
uint8_t ledR = 0;
uint8_t ledG = 2;
uint8_t ledB = 4;

typedef enum {
  LED_OFF = 0,
  LED_R,
  LED_G,
  LED_B,
} status_led;

#define SCREEN_WIDTH 240
#define SCREEN_HEIGHT 320

#define DRAW_BUF_SIZE (SCREEN_WIDTH * SCREEN_HEIGHT / 10 * (LV_COLOR_DEPTH / 8))
uint32_t draw_buf[DRAW_BUF_SIZE / 4];

// If logging is enabled, it will inform the user about what is happening in the library
void log_print(lv_log_level_t level, const char* buf) {
  LV_UNUSED(level);
  Serial.println(buf);
  Serial.flush();
}

String format_time(int time) {
  return (time < 10) ? "0" + String(time) : String(time);
}

static lv_obj_t* text_label_time;
static lv_obj_t* text_label_date;
static lv_obj_t* text_label_day_of_week;

hw_timer_t* timer = NULL;
volatile SemaphoreHandle_t timerSemaphore;
portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED;

volatile uint32_t isrCounter = 0;
volatile uint32_t lastIsrAt = 0;

void ARDUINO_ISR_ATTR onTimer() {
  // Increment the counter and set the time of ISR
  portENTER_CRITICAL_ISR(&timerMux);
  isrCounter = isrCounter + 1;
  lastIsrAt = millis();
  portEXIT_CRITICAL_ISR(&timerMux);
  // Give a semaphore that we can check in the loop
  xSemaphoreGiveFromISR(timerSemaphore, NULL);
  // It is safe to use digitalRead/Write here if you want to toggle an output
}


static void update_led(status_led led) {
  ledcWrite(ledR, 0);
  ledcWrite(ledG, 0);
  ledcWrite(ledB, 0);

  switch (led) {
    case LED_R:
      ledcWrite(ledR, 16);
      break;
    case LED_G:
      ledcWrite(ledG, 16);
      break;
    case LED_B:
      ledcWrite(ledB, 16);
      break;
    default:
      break;
  }
}

static void loop_second_refresh(void) {
  second++;
  if (second > 59) {
    second = 0;
    minute++;
    do_lvgl_update = true;
    if (minute > 59) {
      minute = 0;
      hour++;
      sync_time_date = true;
      Serial.println("do sync_time_date");
      Serial.println("\n");
      if (hour > 23) {
        hour = 0;
      }
    }
  }
}

static void lvgl_timer_cb(lv_timer_t* timer) {
  LV_UNUSED(timer);


  if (do_lvgl_update) {
    String hour_time_f = format_time(hour);
    String minute_time_f = format_time(minute);
    String second_time_f = format_time(second);

    //peter String final_time_str = String(hour_time_f) + ":" + String(minute_time_f) + ":" + String(second_time_f);
    String final_time_str = String(hour_time_f) + ":" + String(minute_time_f);

    //Serial.println(final_time_str);
    lv_label_set_text(text_label_time, final_time_str.c_str());
    lv_label_set_text(text_label_date, current_date.c_str());


    String final_day_of_week_str;
    const char* day_of_week_string[7] = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" };
    final_day_of_week_str = day_of_week_string[day_of_week];
    lv_label_set_text(text_label_day_of_week, final_day_of_week_str.c_str());

    do_lvgl_update = false;
  }
}

void lv_create_main_gui(void) {

  //Serial.println("Current Time: " + current_time);
  //Serial.println("Current Date: " + current_date);

  lv_timer_t* timer = lv_timer_create(lvgl_timer_cb, 1000, NULL);
  lv_timer_ready(timer);

  // Create a text label for the time aligned center
  text_label_time = lv_label_create(lv_screen_active());
  lv_label_set_text(text_label_time, "");
  lv_obj_align(text_label_time, LV_ALIGN_CENTER, 0, -60);  //-30);
  // Set font type and size
  static lv_style_t style_text_label;
  lv_style_init(&style_text_label);
  lv_style_set_text_font(&style_text_label, &lv_font_montserrat_96);
  lv_obj_add_style(text_label_time, &style_text_label, 0);

  // Create a text label for the date aligned center
  text_label_date = lv_label_create(lv_screen_active());
  lv_label_set_text(text_label_date, current_date.c_str());
  lv_obj_align(text_label_date, LV_ALIGN_CENTER, 0, 20);  //30);
  // Set font type and size
  static lv_style_t style_text_label2;
  lv_style_init(&style_text_label2);
  lv_style_set_text_font(&style_text_label2, &lv_font_montserrat_48);
  lv_obj_add_style(text_label_date, &style_text_label2, 0);
  //lv_obj_set_style_text_color((lv_obj_t*)text_label_date, lv_palette_main(LV_PALETTE_GREY), 0);

  // Create a text label for the day_of_week aligned center
  text_label_day_of_week = lv_label_create(lv_screen_active());
  lv_label_set_text(text_label_day_of_week, "");
  lv_obj_align(text_label_day_of_week, LV_ALIGN_CENTER, 0, 85);
  // Set font type and size
  static lv_style_t style_text_label3;
  lv_style_init(&style_text_label3);
  lv_style_set_text_font(&style_text_label3, &lv_font_montserrat_48);
  lv_obj_add_style(text_label_day_of_week, &style_text_label3, 0);
}

#ifdef NTP
void requestLocalTime() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    sync_time_date = true;
    Serial.println("No time available (yet)");
    update_led(LED_R);
    return;
  }
  Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S");

  hour = timeinfo.tm_hour;
  minute = timeinfo.tm_min;
  second = timeinfo.tm_sec;
  day_of_week = timeinfo.tm_wday;

  //Serial.printf("timeinfo.tm_year: %d\n", timeinfo.tm_year);
  current_date = String(timeinfo.tm_year + 1900) + "-" + String(timeinfo.tm_mon + 1) + "-" + String(timeinfo.tm_mday);
  update_led(LED_G);
}

// Callback function (gets called when time adjusts via NTP)
void timeavailable(struct timeval* t) {
  Serial.println("Got time adjustment from NTP!");
  requestLocalTime();
}
#endif

void get_date_and_time() {

  if (WiFi.status() == WL_CONNECTED) {
#ifdef NTP
    requestLocalTime();
#else
    HTTPClient http;

    // Construct the API endpoint
    String url = String("http://worldtimeapi.org/api/timezone/") + timezone;
    http.begin(url);
    int httpCode = http.GET();  // Make the GET request

    if (httpCode > 0) {
      // Check for the response
      if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
        //Serial.println("Time information:");
        //Serial.println(payload);
        // Parse the JSON to extract the time
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, payload);
        if (!error) {
          const char* datetime = doc["datetime"];
          // Split the datetime into date and time
          String datetime_str = String(datetime);
          int splitIndex = datetime_str.indexOf('T');
          current_date = datetime_str.substring(0, splitIndex);
          current_time = datetime_str.substring(splitIndex + 1, splitIndex + 9);  // Extract time portion
          hour = current_time.substring(0, 2).toInt();
          minute = current_time.substring(3, 5).toInt();
          second = current_time.substring(6, 8).toInt();

          Serial.println("recevice Current Time: " + current_time);
          Serial.println("recevice Current Date: " + current_date);

          day_of_week = doc["day_of_week"].as<int>();
          Serial.printf("recevice day_of_week: %d\n", day_of_week);

          update_led(LED_G);
        } else {
          Serial.print("deserializeJson() failed: ");
          Serial.println(error.c_str());
        }
      }
    } else {
      Serial.printf("GET request failed, error: %s\n", http.errorToString(httpCode).c_str());
      sync_time_date = true;
    }
    http.end();  // Close connection
#endif
  } else {
    Serial.println("Not connected to Wi-Fi");
    update_led(LED_R);
  }
}

void setup() {
  String LVGL_Arduino = String("LVGL Library Version: ") + lv_version_major() + "." + lv_version_minor() + "." + lv_version_patch();
  Serial.begin(115200);
  Serial.println(LVGL_Arduino);

  ledcAttach(ledR, 120, 8);  // 120Hz PWM, 8-bit resolution
  ledcAttach(ledG, 120, 8);
  ledcAttach(ledB, 120, 8);
  update_led(LED_OFF);

  ledcAttach(LCD_BL, 120, 8);  // 120Hz PWM, 8-bit resolution
  ledcWrite(LCD_BL, 255);      // off


  // Connect to Wi-Fi
  WiFi.begin(ssid, password);
  Serial.print("Connecting");
  while (WiFi.status() != WL_CONNECTED) {
    update_led(LED_B);
    delay(500);
    update_led(LED_OFF);
    Serial.print(".");
  }
  Serial.print("\n");

  update_led(LED_B);
  Serial.print("\nConnected to Wi-Fi network with IP Address: ");
  Serial.println(WiFi.localIP());

#ifdef NTP
  /**
   * NTP server address could be acquired via DHCP,
   *
   * NOTE: This call should be made BEFORE esp32 acquires IP address via DHCP,
   * otherwise SNTP option 42 would be rejected by default.
   * NOTE: configTime() function call if made AFTER DHCP-client run
   * will OVERRIDE acquired NTP server address
   */
  esp_sntp_servermode_dhcp(1);  // (optional)

  // set notification call-back function
  sntp_set_time_sync_notification_cb(timeavailable);

  /**
   * This will set configured ntp servers and constant TimeZone/daylightOffset
   * should be OK if your time zone does not need to adjust daylightOffset twice a year,
   * in such a case time adjustment won't be handled automagically.
   */
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer1, ntpServer2, ntpServer3);
#endif


  // Get the time and date from WorldTimeAPI
  while (hour == 0 && minute == 0 && second == 0) {
    get_date_and_time();
    update_led(LED_R);
    delay(1000);
    update_led(LED_OFF);
    Serial.print("#");
  }
  Serial.print("\n");
  update_led(LED_G);


  // Start LVGL
  lv_init();
  // Register print function for debugging
  lv_log_register_print_cb(log_print);

  // Create a display object
  lv_display_t* disp;
  // Initialize the TFT display using the TFT_eSPI library
  disp = lv_tft_espi_create(SCREEN_WIDTH, SCREEN_HEIGHT, draw_buf, sizeof(draw_buf));
  lv_display_set_rotation(disp, LV_DISPLAY_ROTATION_90);

  // Function to draw the GUI
  lv_create_main_gui();

  ledcWrite(LCD_BL, 255 - 64);



  // Create semaphore to inform us when the timer has fired
  timerSemaphore = xSemaphoreCreateBinary();

  // Set timer frequency to 1Mhz
  timer = timerBegin(1000000);

  // Attach onTimer function to our timer.
  timerAttachInterrupt(timer, &onTimer);

  // Set alarm to call onTimer function every second (value in microseconds).
  // Repeat the alarm (third parameter) with unlimited count = 0 (fourth parameter).
  timerAlarm(timer, 1000000, true, 0);
}

void loop() {
  // If Timer has fired, every 1s
  if (xSemaphoreTake(timerSemaphore, 0) == pdTRUE) {
    uint32_t isrCount = 0, isrTime = 0;
    // Read the interrupt count and time
    portENTER_CRITICAL(&timerMux);
    isrCount = isrCounter;
    isrTime = lastIsrAt;
    portEXIT_CRITICAL(&timerMux);
    // Print it
    // Serial.print("onTimer no. ");
    // Serial.print(isrCount);
    // Serial.print(" at ");
    // Serial.print(isrTime);
    // Serial.println(" ms");

    loop_second_refresh();

    if (sync_time_date) {
      sync_time_date = false;
      get_date_and_time();
      if (hour == 0 && minute == 0 && second == 0) {
        sync_time_date = true;
      }
    }
  }

  if (millis() - refresh_tick >= 1) {
    refresh_tick = millis();
    lv_task_handler();  // let the GUI do its work
    lv_tick_inc(1);     // tell LVGL how much time has passed in milliseconds
  }
}