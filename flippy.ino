/* flippy by olly jeffery
	 - an e ink flashcard scheduler / note reader 
*/ 
#include <Arduino.h>
#include <GxEPD2_BW.h>
#include <Fonts/FreeMonoBold9pt7b.h>

// fonts
#include <Lora_Italic_VariableFont_wght28pt7b.h>
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
	int id; // what number refers to this menu
	char *imgPath; // path on sd card to the menu image
	int linkedMenus[4]; // what each button should transition that menu to (-1 means special logic)
	bool drawCornerStats; // draw time, battery, time since last sync and cards due in top right
	bool drawTopLeftBattery; // draw big battery percentage in top left (used for sleep menu)
	bool listMenu; // whether this menu is a generic list menu or not
	bool flashcard; // whether menu is flashcard menu or not
};

struct Flashcard {
    String question;
    String answer;
};

const int MAX_FLASHCARDS = 200;

Flashcard flashcards[MAX_FLASHCARDS];
int flashcardCount = 0;

constexpr gpio_num_t wakePins[] = {
	GPIO_NUM_1,
    GPIO_NUM_2,
    GPIO_NUM_4,
    GPIO_NUM_5,
    GPIO_NUM_6
};

constexpr gpio_num_t deepWakePin = GPIO_NUM_1;

constexpr uint64_t SLEEP_TIME_US = 3ULL * 60 * 1000000;

int buttons[] = {
	PWR_PIN,
	B1_PIN,
	B2_PIN,
	B3_PIN,
	B4_PIN
};

Menu menus[] = {
	{0, "/assets/sleeping/sleeping.bin", {0, 0, 0, 0}, false, true, false, false},  // 0 sleep menu
	{1, "/assets/mainmenu/mainmenu.bin", {2, 3, 4, 5}, true, false, false, false}, // 1 main menu

	{2, "/assets/revise/revise.bin", {1, 22, 1, 1}, true, false, false, false}, // 2 revise menu
	{22, "/assets/general/listmenu.bin", {0,0,0,0}, true, false, true, false}, // 2.2 revise->browse

	{3, "/assets/read/read.bin", {1, 1, 1, 1}, true, false, false, false}, // 3 read menu
	{4, "/assets/sync/sync.bin", {1, 1, 1, 1}, true, false, false, false}, // 4 sync menu
	{5, "/assets/settings/settings.bin", {1, 1, 1, 1}, true, false, false, false}, // 5 settings menu
	{6, "/assets/general/flashcardQuestion.bin", {0,0,0,0}, false, false, false, true}, // 6 flashcard question side
	{7, "/assets/general/flashcardAnswer.bin", {0,0,0,0}, false, false, false, true}, // 7 flashcard answer side
};

int menuCount = sizeof(menus) / sizeof(menus[0]);

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

int refreshLevel = 3;

String listMenu[4] = {
	"back",
	"2",
	"3",
	"down"
};

String currentDirectory = "/sync/";
bool finishedDirectory = false;

bool loadFlashcards(const String& folderPath) {
    flashcardCount = 0;

    scanDirectory(folderPath);

	if (DEBUG) {
		Serial.printf(
			"Loaded %d flashcards from %s\n",
			flashcardCount,
			folderPath.c_str()
		);

		for (int i = 0; i < flashcardCount; i++) {
			Serial.print("Q: ");
			Serial.print(flashcards[i].question);
			Serial.print(" A: ");
			Serial.println(flashcards[i].answer);
		}
	}

    return flashcardCount > 0;
}

void scanDirectory(const String& path) {
    File dir = SD.open(path.c_str());

    if (!dir || !dir.isDirectory()) {
        Serial.printf("Cannot open directory: %s\n", path.c_str());

        if (dir) {
            dir.close();
        }

        return;
    }

    File entry = dir.openNextFile();

    while (entry) {
        // Build the full path before closing the entry
        String fullPath = path;

        if (!fullPath.endsWith("/")) {
            fullPath += "/";
        }

        fullPath += entry.name();

        bool isDir = entry.isDirectory();
        String filename = String(entry.name());
        filename.toLowerCase();

        entry.close();

        if (isDir) {
            scanDirectory(fullPath);
        } else if (filename.endsWith(".md")) {
            File markdownFile = SD.open(fullPath.c_str(), FILE_READ);

            if (markdownFile) {
                Serial.printf("Reading: %s\n", fullPath.c_str());
                parseMarkdownFile(markdownFile);
                markdownFile.close();
				Serial.print("Flashcard count is now ");
				Serial.println(flashcardCount);
            }
        }

        entry = dir.openNextFile();
    }

    dir.close();
}

void parseMarkdownFile(File file) {
    while (file.available()) {
        String line = file.readStringUntil('\n');
        line.trim();

        // Skip empty lines
        if (line.length() == 0) {
            continue;
        }

        // Find the question/answer separator
        int separator = line.indexOf("::");

        if (separator == -1) {
            continue;
        }

        // Stop if the array is full
        if (flashcardCount >= MAX_FLASHCARDS) {
            Serial.println("Flashcard array is full!");
            return;
        }

        String question = line.substring(0, separator);
        String answer = line.substring(separator + 2);

        question.trim();
        answer.trim();

        // Don't add empty cards
        if (question.length() == 0 || answer.length() == 0) {
            continue;
        }

        flashcards[flashcardCount].question = question;
        flashcards[flashcardCount].answer = answer;

        flashcardCount++;
    }
}

String trimString(String text) {
    text.trim();
    return text;
}

void goUpDirectory(String* path) {
    if (path->length() <= 1) return;

    path->remove(path->lastIndexOf('/', path->length() - 2) + 1);
}

void setListMenuToDir(String dirname) {
    File dir = SD.open(dirname);

    if (!dir || !dir.isDirectory()) {
        Serial.println("Failed to open directory");
        return;
    }

	if (menuIndex == 0) {
		listMenu[0] = "back";
	} else {
		listMenu[0] = "up";
	}
	
	listMenu[1] = "";
	listMenu[2] = "";

	File entry = dir.openNextFile();
	int filesFound = 0;
	
	for (int i = 0; i < menuIndex; i++) {
		entry = dir.openNextFile();
	}

    while (entry && filesFound < 2) {
		String name = entry.name();
		if (name.charAt(0) != '.') {
			listMenu[1 + filesFound] = entry.name();

			filesFound++;
			Serial.println(entry.name());
		}

        entry.close();

		if (filesFound < 2) {
			entry = dir.openNextFile();
		}
    }

	if (dir.openNextFile()) { // whether there are more files to open or if we have hit the end of the directory
		finishedDirectory = false;
		listMenu[3] = "down";
	} else {
		finishedDirectory = true; // dir.openNextFile() == false so no more files
		listMenu[3] = "";
	}

    dir.close();
}

void drawFlashcard() {
	display.setPartialWindow(0, 0, display.width(), display.height());

	display.firstPage();
	do {
		
		display.setTextColor(GxEPD_BLACK);
		drawBinaryImage(menuToPath(), 0, 0, display.width(), display.height());

		display.setFont(&Lora_Italic_VariableFont_wght7pt7b);
		printInBox(currentDirectory + " | Q" + String(menuIndex), 10, 20, 365, 30);

		display.setFont(&Lora_Italic_VariableFont_wght15pt7b);
		if (currentMenu == 6) {
            //display.print(flashcards[menuIndex].question);
			printInBox(flashcards[menuIndex].question, 10, 50, 365, 140);
        } else {
            printInBox(flashcards[menuIndex].answer, 10, 50, 365, 140);
        }
	} while (display.nextPage());

	Serial.println(flashcards[menuIndex].question);
}

void drawListMenu() {
	display.setPartialWindow(0, 0, display.width(), display.height());
    display.setFont(&Lora_Italic_VariableFont_wght28pt7b);

    // Draw background
    display.firstPage();
    do {
        drawBinaryImage(
            menuToPath(),
            0, 0,
            display.width(),
            display.height()
        );
    } while (display.nextPage());

    // Draw menu text
    for (int i = 0; i < 4; i++) {
        const int y = 50 + 60 * i;
        const int windowY = 9 + 60 * i;

        display.setPartialWindow(43, windowY, 281, 42);
        display.firstPage();
        do {
            display.fillRect(43, windowY, 281, 42, GxEPD_WHITE);
            display.setCursor(44, y);
            display.print(listMenu[i]);
        } while (display.nextPage());
    }
}

Menu getMenu(int id) {
	for (int i = 0; i < menuCount; i++) {
		if (menus[i].id == id) {
			return menus[i];
		}
	}
}

void printInBox(String text, int x, int y, int w, int h) {
  int cursorX = x;
  int cursorY = y;

  int16_t x1, y1;
  uint16_t textW, textH;

  String word = "";
  String line = "";

  // Get the current font's line height
  display.getTextBounds("Ag", 0, 0, &x1, &y1, &textW, &textH);
  int lineHeight = textH + 2;

  // Process each character, building words
  for (int i = 0; i <= text.length(); i++) {
    char c = (i < text.length()) ? text[i] : ' ';

    if (c != ' ' && c != '\n' && c != '\t') {
      word += c;
      continue;
    }

    if (word.length() > 0) {
      String testLine = line;
      if (testLine.length() > 0) testLine += " ";
      testLine += word;

      display.getTextBounds(testLine, 0, 0,
                             &x1, &y1, &textW, &textH);

      if (textW > w && line.length() > 0) {
        // Print the existing line and start a new one
        display.setCursor(cursorX, cursorY);
        display.print(line);

        cursorY += lineHeight;
        line = word;
      } else {
        line = testLine;
      }

      word = "";
    }

    if (c == '\n') {
      if (line.length() > 0) {
        display.setCursor(cursorX, cursorY);
        display.print(line);
        line = "";
      }

      cursorY += lineHeight;
    }

    // Stop if the next line would exceed the box
    if (cursorY + lineHeight > y + h) {
      return;
    }
  }

  // Print the final line
  if (line.length() > 0 && cursorY + lineHeight <= y + h) {
    display.setCursor(cursorX, cursorY);
    display.print(line);
  }
}

char* menuToPath() {
	return getMenu(currentMenu).imgPath;
}

void handleMenuButton(int button) { // big boy function (for big boys only)
	Serial.print("current menu was ");
	Serial.println(currentMenu);

	Menu m = getMenu(currentMenu);

	if (m.listMenu) { // is a list menu
		switch(button) {
			case 1:
				if (listMenu[0] == "back") {
					if (currentDirectory == "/sync/") {
						currentMenu = 1;
					} else {
						goUpDirectory(&currentDirectory);
					}
				} else if (menuIndex != 0) {
					menuIndex -= 2;
				}
				break;
			case 2:
				currentDirectory = currentDirectory + listMenu[1] + "/";
				menuIndex = 0;
				break;
			case 3:
				currentDirectory = currentDirectory + listMenu[2] + "/";
				menuIndex = 0;
				break;
			case 4:
				if (listMenu[3] == "down") {
					menuIndex += 2;
				}
				break;
			case 43: // LMAO this is so fucked. 43 means both button 4 and 3 is pressed
				loadFlashcards(currentDirectory);
				currentMenu = 6;
				menuIndex = 0;
		}
	} else if (m.flashcard) { // is a flashcard menu

		switch(m.id) { // this is dumb as fuck but i cant be bothered to do anything else <3
			case 6: // whether this is the question side
				switch(button) { // im crine theres another switch statement
					case 1: // flip card button
						currentMenu = 7;
						break;
					case 2: // hint button (not implemented yet)
						break;
					case 4: // home button
						currentMenu = 1;
						menuIndex = 0;
						break;
				}
				break;
			case 7:
				switch(button) { // more switch statements, just for you <3
					case 1: // flip card button
						currentMenu = 6;
						break;
					case 2: // correct button
						menuIndex += 1; // advance to next flashcard
						currentMenu = 6; // go back to question side
						break;
					case 3: // incorrect button HAH idiot
						menuIndex += 1; // same as correct button
						currentMenu = 6;
						break;
					case 4: // home button
						currentMenu = 1;
						menuIndex = 0;
						break;
				}
				break;
		}

	} else {
		currentMenu = getMenu(currentMenu).linkedMenus[button - 1];
	}

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
	Serial.println(v);
	float adcVoltage = (float(map(v, 0, 4095, 15, 245)) / 100.00f) * 1.4f;
	Serial.println(adcVoltage);
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
	display.firstPage();				 // refresh built into display controller
	do {
		display.fillScreen(GxEPD_WHITE);
	} while (display.nextPage());

	if (refreshLevel > 1) {
		display.firstPage();
		do {
			display.fillScreen(GxEPD_BLACK);
		} while (display.nextPage());

		if (refreshLevel > 2) {
			display.firstPage();
			do {
				display.fillScreen(GxEPD_WHITE);
			} while (display.nextPage());
		}
	}
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

	analogSetAttenuation(ADC_11db);

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

	setListMenuToDir("/sync/");

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

long long concat(long long a, long long b) {
    long long multiplier = 1;
    long long temp = b;

    // Handle the case where the second number is 0
    if (b == 0) {
        multiplier = 10;
    } else {
        while (temp > 0) {
            multiplier *= 10;
            temp /= 10;
        }
    }

    return a * multiplier + b;
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
	while (digitalRead(PWR_PIN) == LOW) {
		delay(50);
	}

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

	delay(100); // to allow time for multi button presses

	if (buttonPressed) {
		int pressedPin = 0;
		int secondPin = 0;

		// Print which pins are LOW after waking
		for (gpio_num_t pin : wakePins) {
			if (digitalRead((uint8_t)pin) == LOW) {
				if (pressedPin == 0) { // if we have not found the first pressed button yet
					pressedPin = (int)pin;
					Serial.print("pressed pin is ");
					Serial.println(pin);
				} else {
					secondPin = (int)pin; // if we have found a first pressed button
					Serial.print("second pin is ");
					Serial.println(pin);
				}
				
			}
		}

		int pressedButton = -1; // when multiple buttons are pressed, the button read will be the bigger number button before the smaller one. eg 32 for 3 and 2, 43 for 4 and 3.
		int secondButton = -1;

		for (int i = 0; i < 6; i++) {
			if (pressedPin == buttons[i]) {
				pressedButton = i;
			} else if (secondPin == buttons [i]) {
				secondButton = i;
			}
		}

		if (secondButton != -1) {
			Serial.println("second button pressed");
			
			pressedButton = concat(pressedButton, secondButton);
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

	Menu m = getMenu(currentMenu);

	if (!m.listMenu && !m.flashcard) {
		display.firstPage();
		do {
			drawBinaryImage(menuToPath(), 0, 0, display.width(), display.height());
			
			if (m.drawTopLeftBattery) {
				display.setCursor(0, 74);
				display.setFont(&Lora_Italic_VariableFont_wght15pt7b);
				display.print(String(batteryPercentage) + "%");
			}

		} while (display.nextPage());

		if (m.drawCornerStats) {
			drawCornerStats();
		}
	} else if (!m.flashcard) { // menu is a listMenu so requires special handling
		setListMenuToDir(currentDirectory);
		drawListMenu();
	} else { // menu is flashcard so requires special handling
		drawFlashcard();
	}
}
// hmm