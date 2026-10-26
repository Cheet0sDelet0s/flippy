#line 1 "/home/cheeto/Arduino/flippy/flippy.ino"
/* flippy by olly jeffery
	 - an e ink flashcard scheduler / note reader 
*/ 
#include <Arduino.h>
#include <GxEPD2_BW.h>
#include <Fonts/FreeMonoBold9pt7b.h>

// fonts
#include <Lora_Italic_VariableFont_wght15pt7b.h>
#include <Lora_Italic_VariableFont_wght10pt7b.h>
#include <Lora_Italic_VariableFont_wght7pt7b.h>

// esp32 specific
#include "driver/rtc_io.h"
#include "esp_sleep.h"
#include "driver/gpio.h"

#include "FS.h"
#include "SD.h" // DO NOT PUT A "." IN FRONT OF FILEPATHS

#include <Wire.h>

const bool DEBUG = false; // whether to do debug serial and stuff. will make everything run very badly if true

// SPI
#define MISO_PIN 11
#define MOSI_PIN 13
#define SCK_PIN 12

// chip selects
#define DISPLAY_CS 9
#define SD_CS 10

// display specific
#define BUSY_PIN 35
#define RES_PIN 17
#define DC_PIN 18

// I2C
#define SCL_PIN 37
#define SDA_PIN 38
#define RTC_ADDR 0x51

// buttons
#define PWR_PIN 1
#define B1_PIN 6
#define B2_PIN 5
#define B3_PIN 4
#define B4_PIN 2

// battery voltage divider pin and values
#define BAT_PIN 7
#define R1 100000 // ohms
#define R2 100000 // ohms

const int NUM_POINTS = 11;
const float lipoVoltages[NUM_POINTS]   = {3.27, 3.60, 3.70, 3.76, 3.79, 3.82, 3.87, 3.92, 3.98, 4.06, 4.20};
const int lipoPercentages[NUM_POINTS]  = {0,    10,   20,   30,   40,   50,   60,   70,   80,   90,   100};

struct Menu {
	char *imgPath; // path on sd card to the menu image
	int linkedMenus[4]; // what each button should transition that menu to (-1 means special logic)
	bool drawCornerStats; // draw time, battery, time since last sync and cards due in top right
	bool drawTopLeftBattery; // draw big battery percentage in top left (used for sleep menu)
};

constexpr gpio_num_t wakePins[] = {
	GPIO_NUM_1,
    GPIO_NUM_2,
    GPIO_NUM_4,
    GPIO_NUM_5,
    GPIO_NUM_6
};

constexpr gpio_num_t deepWakePin = GPIO_NUM_1;

constexpr uint64_t SLEEP_TIME_US = 1ULL * 60 * 1000000;

int buttons[] = {
	PWR_PIN,
	B1_PIN,
	B2_PIN,
	B3_PIN,
	B4_PIN
};

Menu menus[6] = {
	{"/assets/sleeping/sleeping.bin", {0, 0, 0, 0}, false, true},  // 0 sleep menu
	{"/assets/mainmenu/mainmenu.bin", {2, 3, 4, 5}, true, false}, // 1 main menu
	{"/assets/revise/revise.bin", {1, 1, 1, 1}, true, false}, // 2 revise menu
	{"/assets/read/read.bin", {1, 1, 1, 1}, true, false}, // 3 read menu
	{"/assets/sync/sync.bin", {1, 1, 1, 1}, true, false}, // 4 sync menu
	{"/assets/settings/settings.bin", {1, 1, 1, 1}, true, false} // 5 settings menu
};

//display object
GxEPD2_BW<GxEPD2_370_GDEY037T03, GxEPD2_370_GDEY037T03::HEIGHT> display(GxEPD2_370_GDEY037T03(DISPLAY_CS, DC_PIN, RES_PIN, BUSY_PIN)); // GDEY037T03 240x416, UC8253

float batteryVoltage = 3.70;
int batteryPercentage = 20;

// clock
int second = 0;
int minute = 0;
int hour = 0;

// MENU
int menuIndex = 0;
int currentMenu = 1;  // 0 = SLEEP   1 = MAIN MENU
int menuLength = 4; // not used currently

#line 112 "/home/cheeto/Arduino/flippy/flippy.ino"
char * menuToPath();
#line 116 "/home/cheeto/Arduino/flippy/flippy.ino"
void handleMenuButton(int button);
#line 124 "/home/cheeto/Arduino/flippy/flippy.ino"
void readFile(fs::FS &fs, const char *path);
#line 139 "/home/cheeto/Arduino/flippy/flippy.ino"
void drawBinaryImage(const char* filename, int16_t x, int16_t y, uint16_t w, uint16_t h);
#line 156 "/home/cheeto/Arduino/flippy/flippy.ino"
int readBattery();
#line 196 "/home/cheeto/Arduino/flippy/flippy.ino"
void leveledRefresh(int refreshLevel);
#line 221 "/home/cheeto/Arduino/flippy/flippy.ino"
void drawCornerStats();
#line 246 "/home/cheeto/Arduino/flippy/flippy.ino"
void setup();
#line 325 "/home/cheeto/Arduino/flippy/flippy.ino"
void setRTCToCompileTime();
#line 372 "/home/cheeto/Arduino/flippy/flippy.ino"
uint8_t decToBcd(uint8_t val);
#line 376 "/home/cheeto/Arduino/flippy/flippy.ino"
uint8_t bcdToDec(uint8_t bcd);
#line 380 "/home/cheeto/Arduino/flippy/flippy.ino"
void updateTime();
#line 400 "/home/cheeto/Arduino/flippy/flippy.ino"
void drawTest();
#line 429 "/home/cheeto/Arduino/flippy/flippy.ino"
void lightSleep();
#line 463 "/home/cheeto/Arduino/flippy/flippy.ino"
void deepSleep();
#line 483 "/home/cheeto/Arduino/flippy/flippy.ino"
void loop();
#line 112 "/home/cheeto/Arduino/flippy/flippy.ino"
char* menuToPath() {
	return menus[currentMenu].imgPath;
}

void handleMenuButton(int button) {
	Serial.print("current menu was ");
	Serial.println(currentMenu);
	currentMenu = menus[currentMenu].linkedMenus[button - 1];
	Serial.print("it is now ");
	Serial.println(currentMenu);
}

void readFile(fs::FS &fs, const char *path){
	Serial.printf("reading file: %s\n", path);
	File file = fs.open(path);
	if(!file){
		Serial.println("failed to open file for reading");
		return;
	}
	
	Serial.print("read from file: ");
	while(file.available()){
		Serial.write(file.read());
	}
	file.close();
}

void drawBinaryImage(const char* filename, int16_t x, int16_t y,
                     uint16_t w, uint16_t h) {
    File file = SD.open(filename, FILE_READ);
    if (!file) return;

    size_t size = ((size_t)w * h + 7) / 8;
    uint8_t* bitmap = new uint8_t[size];

    if (file.read(bitmap, size) == size) {
        display.drawBitmap(x, y, bitmap, w, h,
                           GxEPD_BLACK, GxEPD_WHITE);
    }

    delete[] bitmap;
    file.close();
}

int readBattery() { // scuffed battery voltage read + convert to percentage. no idea if it works properly (i think voltage is correct)
  const int SAMPLES = 2;
  float totalVoltage = 0;

  for (int i = 0; i < SAMPLES; i++) {

	int v = analogRead(BAT_PIN);
	float adcVoltage = (v / 4095.0f) * 1.95f;

    totalVoltage += adcVoltage;
  }

  float measuredVoltage = totalVoltage / SAMPLES;
  batteryVoltage = (measuredVoltage * (R1 + R2)) / R2;

  Serial.println(batteryVoltage);

  // beautifully written by google ai overview <<<333
  if (batteryVoltage >= lipoVoltages[NUM_POINTS - 1]) return 100;
  if (batteryVoltage <= lipoVoltages[0]) return 0;

  // 2. Find the segment the voltage falls into
  for (int i = 0; i < NUM_POINTS - 1; i++) {
    if (batteryVoltage >= lipoVoltages[i] && batteryVoltage <= lipoVoltages[i + 1]) {
      
      // 3. Linearly interpolate within this specific segment
      float voltageDiff = lipoVoltages[i + 1] - lipoVoltages[i];
      float progress = (batteryVoltage - lipoVoltages[i]) / voltageDiff;
      
      int percentDiff = lipoPercentages[i + 1] - lipoPercentages[i];
      batteryPercentage = lipoPercentages[i] + (progress * percentDiff);
	  Serial.flush();
	  return 1;
    }
  }
  batteryPercentage = 100;
  Serial.flush();
  return 0;
}

void leveledRefresh(int refreshLevel) {  // custom screen refresh to bypass seizure-inducing
	// display.firstPage();				 // refresh built into display controller
	// do {
	// 	display.fillScreen(GxEPD_WHITE);
	// } while (display.nextPage());

	// if (refreshLevel > 1) {
	// 	display.firstPage();
	// 	do {
	// 		display.fillScreen(GxEPD_BLACK);
	// 	} while (display.nextPage());

	// 	if (refreshLevel > 2) {
	// 		display.firstPage();
	// 		do {
	// 			display.fillScreen(GxEPD_WHITE);
	// 		} while (display.nextPage());
	// 	}
	// }

	display.fillScreen(GxEPD_WHITE);
	display.fillScreen(GxEPD_BLACK);
	display.fillScreen(GxEPD_WHITE);
}

void drawCornerStats() {
	display.setPartialWindow(381, 0, 35, 64);

	display.firstPage();

	do {
		// clear all previous info drawn
		display.fillScreen(GxEPD_WHITE);
	} while (display.nextPage());	
	display.firstPage();
	do {
		// time
		display.setFont(&Lora_Italic_VariableFont_wght7pt7b);
		display.setCursor(381, 15);
		display.print(String(hour) + ":" + String(minute));

		// battery
		display.setCursor(381, 30);
		display.print(String(batteryPercentage) + "%");
	} while (display.nextPage());

	// return partial window to entire display
	display.setPartialWindow(0, 0, display.width(), display.height());
}

void setup()
{
	if (DEBUG) {
		Serial.begin(115200);
		delay(500);
	}
	Serial.println("began serial wheyyy");
	// init buttons
	rtc_gpio_deinit(deepWakePin);
	pinMode(PWR_PIN, INPUT_PULLUP);
	pinMode(B1_PIN, INPUT_PULLUP);
	pinMode(B2_PIN, INPUT_PULLUP);
	pinMode(B3_PIN, INPUT_PULLUP);
	pinMode(B4_PIN, INPUT_PULLUP);

	pinMode(BAT_PIN, INPUT);

	SPI.begin(SCK_PIN, MISO_PIN, MOSI_PIN, DISPLAY_CS);
	Serial.println("began spi");
	Wire.begin(SDA_PIN, SCL_PIN);
	Serial.println("began i2c");
	setRTCToCompileTime();

	display.init(0, false, 50, false);
	Serial.println("inited display");

	analogSetAttenuation(ADC_6db);

	display.setRotation(3);
	display.setFont(&FreeMonoBold9pt7b);
	display.setTextColor(GxEPD_BLACK);

	if(!SD.begin(SD_CS)){
		Serial.println("card mount failed lololol");
	}

	uint8_t cardType = SD.cardType();
	if(cardType == CARD_NONE){
		Serial.println("omg did you forget to plug the card in (or spi isnt working ig)");
	}

	Serial.println("initing sd card...");
	if (!SD.begin(SD_CS)) {
		Serial.println("oopsie sd card init failed lololololollol");
	}

	esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();

	readBattery();
	updateTime();

	display.setPartialWindow(0, 0, display.width(), display.height());

	display.firstPage();
	do {
		display.fillScreen(GxEPD_WHITE);
	} while (display.nextPage());

	display.firstPage();
	do {
		display.fillScreen(GxEPD_BLACK);
	} while (display.nextPage());

	display.firstPage();
	do {
		display.fillScreen(GxEPD_WHITE);
	} while (display.nextPage());

	display.firstPage();
	do {
		drawBinaryImage("/assets/mainmenu/mainmenu.bin", 0, 0, display.width(), display.height());
	} while (display.nextPage());

	drawCornerStats();

	Serial.println("setup finished");
	Serial.flush();
}

void setRTCToCompileTime() {
	char monthStr[4];
	int day, year;
	int hour, minute, second;

	// Parse compile date, e.g. "Oct 21 2026"
	sscanf(__DATE__, "%3s %d %d", monthStr, &day, &year);

	// Parse compile time, e.g. "17:57:00"
	sscanf(__TIME__, "%d:%d:%d", &hour, &minute, &second);

	const char* months[] = {
		"Jan", "Feb", "Mar", "Apr", "May", "Jun",
		"Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
	};

	int month = 0;
	for (int i = 0; i < 12; i++) {
		if (strncmp(monthStr, months[i], 3) == 0) {
		month = i + 1;
		break;
		}
	}

	if (month == 0) {
		Serial.println("Failed to parse compile month");
		return;
	}

	// Start writing at the seconds register (0x02).
	Wire.beginTransmission(RTC_ADDR);
	Wire.write(0x02);
	Wire.write(decToBcd(second));       // Seconds
	Wire.write(decToBcd(minute));       // Minutes
	Wire.write(decToBcd(hour));         // Hours
	Wire.write(decToBcd(day));          // Day of month
	Wire.write(1);                      // Weekday (0–6; placeholder)
	Wire.write(decToBcd(month));        // Month
	Wire.write(decToBcd(year % 100));   // Year (00–99)

	if (Wire.endTransmission() == 0) {
		Serial.println("RTC set to sketch compile time.");
	} else {
		Serial.println("Failed to set RTC.");
	}
}

uint8_t decToBcd(uint8_t val) {
  return ((val / 10) << 4) | (val % 10);
}

uint8_t bcdToDec(uint8_t bcd) {
  return ((bcd >> 4) * 10) + (bcd & 0x0F);
}

void updateTime() {
	Wire.beginTransmission(RTC_ADDR);
	Wire.write(0x02);  // Seconds register

	if (Wire.endTransmission(false) != 0 ||
		Wire.requestFrom(RTC_ADDR, 3) != 3) {
		Serial.println("Read failed");
		delay(1000);
		return;
	}

	uint8_t s = Wire.read();
	uint8_t m = Wire.read();
	uint8_t h = Wire.read();

	second = bcdToDec(s & 0x7F);
	minute = bcdToDec(m & 0x7F);
	hour = bcdToDec(h & 0x3F);
}

void drawTest() {
	updateTime();

	display.setPartialWindow(0, 208, display.width(), 32);
	display.firstPage();

	String buttonsPressed = "";

	if (digitalRead(PWR_PIN) == LOW) buttonsPressed += "PWR ";
	if (digitalRead(B1_PIN) == LOW) buttonsPressed += " B1 ";
	if (digitalRead(B2_PIN) == LOW) buttonsPressed += " B2 ";
	if (digitalRead(B3_PIN) == LOW) buttonsPressed += " B3 ";
	if (digitalRead(B4_PIN) == LOW) buttonsPressed += " B4 ";

	do {
		display.setCursor(0, 230);
		display.fillRect(0, 208, display.width(), 32, GxEPD_WHITE);
		display.print(hour);
		display.print(":");
		display.print(minute);
		display.print(":");
		display.print(second);
		display.print(" buttons: ");
		display.print(buttonsPressed);
	} while (display.nextPage());

	delay(100);
}

void lightSleep() {
	// Configure the inputs with internal pull-ups
	for (gpio_num_t pin : wakePins) {
		pinMode((uint8_t)pin, INPUT_PULLUP);
	}

	// Enable wake-up when any pin goes LOW
	for (gpio_num_t pin : wakePins) {
		ESP_ERROR_CHECK(
			gpio_wakeup_enable(pin, GPIO_INTR_LOW_LEVEL)
		);
	}

	ESP_ERROR_CHECK(esp_sleep_enable_gpio_wakeup());

	// Also wake after five minutes
	ESP_ERROR_CHECK(
		esp_sleep_enable_timer_wakeup(SLEEP_TIME_US)
	);

	if (DEBUG) {
		Serial.flush(); // make sure all serial has been transmitted
		Serial.end();
	}

	esp_light_sleep_start();

	if (DEBUG) {
		Serial.begin(115200); // reinit serial
		delay(500);
		Serial.println("wakey wakey");
	}
}

void deepSleep() {
	// disable light sleep wakeups
	for (gpio_num_t pin : wakePins) {
		gpio_wakeup_disable(pin);
	}

	esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);

	// Configure the RTC GPIO and internal pull-up
	rtc_gpio_init(deepWakePin);
	rtc_gpio_set_direction(deepWakePin, RTC_GPIO_MODE_INPUT_ONLY);
	rtc_gpio_pullup_en(deepWakePin);
	rtc_gpio_pulldown_dis(deepWakePin);

	// Wake when the pin is LOW
	ESP_ERROR_CHECK(esp_sleep_enable_ext0_wakeup(deepWakePin, 0));
	esp_deep_sleep_start();
	// program never continues from here as deep sleep does not preserve RAM
}

void loop() {
	static bool shouldLightSleep = true;
	
	display.hibernate();

	if (!shouldLightSleep) {
		deepSleep();
	}

	lightSleep();

	esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();

	bool buttonPressed = false;
	switch (cause) {
        case ESP_SLEEP_WAKEUP_GPIO:
            Serial.println("Wake-up reason: GPIO");
			buttonPressed = true;
            break;

        case ESP_SLEEP_WAKEUP_TIMER:
            Serial.println("Wake-up reason: 5-minute timer");
            break;

        default:
            Serial.printf("Wake-up reason: %d (other/undefined)\n",
                          (int)cause);
            break;
    }

	int refreshLevel = 3;

	if (buttonPressed) {
		int pressedPin = 0;

		// Print which pins are LOW after waking
		for (gpio_num_t pin : wakePins) {
			if (digitalRead((uint8_t)pin) == LOW) {
				pressedPin = (int)pin;
				Serial.print("pressed pin is ");
				Serial.println(pin);
				break;
			}
		}

		int pressedButton = -1;

		for (int i = 0; i < 6; i++) {
			if (pressedPin == buttons[i]) {
				pressedButton = i;
			}
		}

		if (pressedButton == 0) { // power button
			Serial.println("power button pressed");
			while (digitalRead(PWR_PIN) == LOW) {
				delay(50);
			}
			menuIndex = 0;
			currentMenu = 0;
			refreshLevel = 3;
			shouldLightSleep = false;
		} else {
			handleMenuButton(pressedButton);
		}

		if (DEBUG) {
			delay(800);

			Serial.print("pressed button is ");
			Serial.println(pressedButton);
		}
	} else { // if wakeup caused by sleep timeout
		Serial.println("going to sleep due to timer");
		menuIndex = 0;
		currentMenu = 0;
		refreshLevel = 3;
		shouldLightSleep = false;
	}

	display.init(0, false, 50, false);
	delay(200);
	display.setPartialWindow(0, 0, display.width(), display.height());
	
	leveledRefresh(refreshLevel);
	readBattery();
	updateTime();

	Serial.println(menuToPath());

	display.firstPage();
	do {
		drawBinaryImage(menuToPath(), 0, 0, display.width(), display.height());
		
		if (menus[currentMenu].drawTopLeftBattery) {
			display.setCursor(0, 74);
			display.setFont(&Lora_Italic_VariableFont_wght15pt7b);
			display.print(String(batteryPercentage) + "%");
		}

	} while (display.nextPage());

	if (menus[currentMenu].drawCornerStats) {
		drawCornerStats();
	}
	
}
