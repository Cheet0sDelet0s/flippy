# 1 "/home/cheeto/Arduino/flippy/flippy.ino"
/* flippy by olly jeffery
	 - an e ink flashcard scheduler / note reader 
*/
# 5 "/home/cheeto/Arduino/flippy/flippy.ino" 2
# 6 "/home/cheeto/Arduino/flippy/flippy.ino" 2
# 7 "/home/cheeto/Arduino/flippy/flippy.ino" 2

// fonts
# 10 "/home/cheeto/Arduino/flippy/flippy.ino" 2
# 11 "/home/cheeto/Arduino/flippy/flippy.ino" 2
# 12 "/home/cheeto/Arduino/flippy/flippy.ino" 2
# 13 "/home/cheeto/Arduino/flippy/flippy.ino" 2

// esp32 specific
# 16 "/home/cheeto/Arduino/flippy/flippy.ino" 2



# 20 "/home/cheeto/Arduino/flippy/flippy.ino" 2
# 21 "/home/cheeto/Arduino/flippy/flippy.ino" 2

# 23 "/home/cheeto/Arduino/flippy/flippy.ino" 2

const bool DEBUG = false; // whether to do debug serial and stuff. will make everything run very badly if true

// SPI




// chip selects



// display specific




// I2C




// buttons






// battery voltage divider pin and values




// syncing







HardwareSerial DockSerial(1);

const int NUM_POINTS = 11;
const float lipoVoltages[NUM_POINTS] = {3.27, 3.60, 3.70, 3.76, 3.79, 3.82, 3.87, 3.92, 3.98, 4.06, 4.20};
const int lipoPercentages[NUM_POINTS] = {0, 10, 20, 30, 40, 50, 60, 70, 80, 90, 100};

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
 1,
 6,
 5,
 4,
 2
};

Menu menus[] = {
 {0, "/assets/sleeping/sleeping.bin", {0, 0, 0, 0}, false, true, false, false}, // 0 sleep menu
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
GxEPD2_BW<GxEPD2_370_GDEY037T03, GxEPD2_370_GDEY037T03::HEIGHT> display(GxEPD2_370_GDEY037T03(9, 18, 17, 35)); // GDEY037T03 240x416, UC8253

float batteryVoltage = 3.70;
int batteryPercentage = 20;

// clock
int second = 0;
int minute = 0;
int hour = 0;

// MENU
int menuIndex = 0;
int currentMenu = 1; // 0 = SLEEP   1 = MAIN MENU
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


// SYNCING

void startSync() {
 display.setPartialWindow(0, 0, 200, 25);

 display.firstPage();
 do {
  display.fillRect(0, 0, 200, 25, 0x0000);
 } while (display.nextPage());

 display.firstPage();
 do {
  display.fillRect(0, 0, 200, 25, 0xFFFF);
  display.setFont(&Lora_Italic_VariableFont_wght10pt7b);
  display.setCursor(0, 20);
  display.print("Syncing...");
 } while (display.nextPage());

 DockSerial.begin(
  115200,
  SERIAL_8N1,
  44,
  43
 );

 if (!SD.exists("/sync")) {
  if (!SD.mkdir("/sync")) {
  DockSerial.println("ERR MKDIR");
  return;
  }
 }

 DockSerial.println("READY");

 while (digitalRead(4) == 0x0) {
  delay(100);
 }

 while (digitalRead(4) == 0x1) { // sync until B3 is pressed again
  if (DockSerial.available()) {
   String line;

   if (!readLine(line)) {
    DockSerial.println("ERR LINE");
    break;
   }

   if (line == "LIST") {
    listFiles();
   }
   else if (line.startsWith("PUT ")) {
    char name[128];
    unsigned long size;
    char crcText[9];

    if (sscanf(line.c_str(), "PUT %127s %lu %8s",
      name, &size, crcText) == 3) {
    uint32_t crc = strtoul(crcText, nullptr, 16);
    handlePut(String(name), size, crc);
    } else {
    DockSerial.println("ERR FORMAT");
    }
   }
   else if (line.startsWith("GET ")) {
    String name = line.substring(4);
    handleGet(name);
   } else if (line.startsWith("DELETE ")) {
    String name = line.substring(7);

    if (!validName(name)) {
     DockSerial.println("ERR NAME");
    } else {
     String path = String("/sync") + "/" + name;

     if (SD.exists(path)) {
     if (SD.remove(path)) {
      DockSerial.println("OK");
     } else {
      DockSerial.println("ERR DELETE");
     }
     } else {
     DockSerial.println("OK");
     }
    }
   } else if (line == "FINISH") {
    break;
   } else {
    DockSerial.println("ERR COMMAND");
   }
  }
 }

 DockSerial.println("ACKFINISH");
 DockSerial.flush();
 DockSerial.end();

 display.firstPage();
 do {
  display.print(" Done!");
 } while (display.nextPage());

 delay(1000);
}

// CRC-32 (IEEE)
uint32_t crc32Update(uint32_t crc, uint8_t data) {
  crc ^= data;
  for (int i = 0; i < 8; i++) {
    crc = (crc >> 1) ^ (0xEDB88320UL & -(crc & 1));
  }
  return crc;
}

bool validName(const String &name) {
  if (name.length() == 0 || name.length() >= 128)
    return false;

  if (name == "." || name == "..")
    return false;

  // Only flat filenames; no directory traversal
  for (char c : name) {
    if (c == '/' || c == '\\' || c == ':' ||
        c == '\r' || c == '\n')
      return false;
  }
  return true;
}

bool readLine(String &line) {
  line = "";
  while (true) {
    if (!DockSerial.available()) {
      delay(1);
      continue;
    }

    char c = DockSerial.read();
    if (c == '\n')
      return true;

    if (c != '\r') {
      if (line.length() >= 200)
        return false;
      line += c;
    }
  }
}

uint32_t fileCRC(File &file) {
  uint32_t crc = 0xFFFFFFFF;
  while (file.available()) {
    crc = crc32Update(crc, file.read());
  }
  return crc ^ 0xFFFFFFFF;
}

String hex32(uint32_t value) {
  char buf[9];
  snprintf(buf, sizeof(buf), "%08lX", (unsigned long)value);
  return String(buf);
}

void listFiles() {
  File root = SD.open("/sync");
  if (!root) {
    DockSerial.println("ERR SD");
    return;
  }

  File entry = root.openNextFile();
  while (entry) {
    if (!entry.isDirectory()) {
      String name = String(entry.name());
      if (validName(name)) {
        uint32_t size = entry.size();
        uint32_t crc = fileCRC(entry);

        DockSerial.printf("FILE %s %lu %s\n",
          name.c_str(),
          (unsigned long)size,
          hex32(crc).c_str());
      }
    }
    entry.close();
    entry = root.openNextFile();
  }

  root.close();
  DockSerial.println("END");
}

void handlePut(String name, uint32_t size, uint32_t expectedCRC) {
  if (!validName(name)) {
    DockSerial.println("ERR NAME");
    return;
  }

  String tempName = String("/sync") + "/" + name + ".part";
  String finalName = String("/sync") + "/" + name;

  SD.remove(tempName);

  File file = SD.open(tempName, "w");
  if (!file) {
    DockSerial.println("ERR OPEN");
    return;
  }

  DockSerial.println("GO");

  uint32_t crc = 0xFFFFFFFF;
  uint32_t received = 0;
  uint8_t buffer[512];

  while (received < size) {
    size_t wanted = min((uint32_t)sizeof(buffer), size - received);

    size_t count = 0;
    while (count < wanted) {
      if (DockSerial.available()) {
        int c = DockSerial.read();
        if (c >= 0)
          buffer[count++] = (uint8_t)c;
      } else {
        delay(1);
      }
    }

    file.write(buffer, count);

    for (size_t i = 0; i < count; i++)
      crc = crc32Update(crc, buffer[i]);

    received += count;
  }

  file.flush();
  file.close();

  crc ^= 0xFFFFFFFF;

  if (crc != expectedCRC) {
    SD.remove(tempName);
    DockSerial.println("ERR CRC");
    return;
  }

  SD.remove(finalName);
  if (!SD.rename(tempName, finalName)) {
    SD.remove(tempName);
    DockSerial.println("ERR RENAME");
    return;
  }

  DockSerial.println("OK");
}

void handleGet(String name) {
  if (!validName(name)) {
    DockSerial.println("ERR NAME");
    return;
  }

  File file = SD.open(String("/sync") + "/" + name, "r");
  if (!file) {
    DockSerial.println("ERR NOTFOUND");
    return;
  }

  uint32_t size = file.size();
  uint32_t crc = fileCRC(file);
  file.seek(0);

  DockSerial.printf("DATA %lu %s\n",
    (unsigned long)size,
    hex32(crc).c_str());

  uint8_t buffer[512];
  while (file.available()) {
    size_t count = file.read(buffer, sizeof(buffer));
    DockSerial.write(buffer, count);
  }

  file.close();
}

// END OF SYNCING


bool loadFlashcards(const String& folderPath) {
    flashcardCount = 0;

    scanDirectory(folderPath);

 if (DEBUG) {
  HWCDCSerial.printf(
   "Loaded %d flashcards from %s\n",
   flashcardCount,
   folderPath.c_str()
  );

  for (int i = 0; i < flashcardCount; i++) {
   HWCDCSerial.print("Q: ");
   HWCDCSerial.print(flashcards[i].question);
   HWCDCSerial.print(" A: ");
   HWCDCSerial.println(flashcards[i].answer);
  }
 }

    return flashcardCount > 0;
}

void scanDirectory(const String& path) {
    File dir = SD.open(path.c_str());

    if (!dir || !dir.isDirectory()) {
        HWCDCSerial.printf("Cannot open directory: %s\n", path.c_str());

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
            File markdownFile = SD.open(fullPath.c_str(), "r");

            if (markdownFile) {
                HWCDCSerial.printf("Reading: %s\n", fullPath.c_str());
                parseMarkdownFile(markdownFile);
                markdownFile.close();
    HWCDCSerial.print("Flashcard count is now ");
    HWCDCSerial.println(flashcardCount);
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
            HWCDCSerial.println("Flashcard array is full!");
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
        HWCDCSerial.println("Failed to open directory");
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
   HWCDCSerial.println(entry.name());
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

 display.setTextColor(0x0000);
 display.fillScreen(1);
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

 display.display(true);
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

  // Draw menu text
  for (int i = 0; i < 4; i++) {
   const int y = 50 + 60 * i;
   const int windowY = 9 + 60 * i;

   display.setCursor(44, y);
   display.print(listMenu[i]);
  }
    } while (display.nextPage());
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
 HWCDCSerial.print("current menu was ");
 HWCDCSerial.println(currentMenu);

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
  refreshLevel = 2;
  switch(m.id) { // this is dumb as fuck but i cant be bothered to do anything else <3
   case 6: // whether this is the question side
    switch(button) { // im crine theres another switch statement
     case 1: // flip card button
      currentMenu = 7;
      break;
     case 2: // hint button (not implemented yet)
      break;
     case 4: // home button
      refreshLevel = 3;
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

  if (currentMenu == 4) {
   startSync();
   currentMenu = 1; // return to main menu
  }
 }

 HWCDCSerial.print("it is now ");
 HWCDCSerial.println(currentMenu);
}

void readFile(fs::FS &fs, const char *path) {
 HWCDCSerial.printf("reading file: %s\n", path);
 File file = fs.open(path);
 if(!file){
  HWCDCSerial.println("failed to open file for reading");
  return;
 }

 HWCDCSerial.print("read from file: ");
 while(file.available()){
  HWCDCSerial.write(file.read());
 }
 file.close();
}

void drawBinaryImage(const char* filename, int16_t x, int16_t y,
                     uint16_t w, uint16_t h) {
    File file = SD.open(filename, "r");
    if (!file) return;

    size_t size = ((size_t)w * h + 7) / 8;
    uint8_t* bitmap = new uint8_t[size];

    if (file.read(bitmap, size) == size) {
        display.drawBitmap(x, y, bitmap, w, h,
                           0x0000, 0xFFFF);
    }

    delete[] bitmap;
    file.close();
}

int readBattery() { // scuffed battery voltage read + convert to percentage. no idea if it works properly (i think voltage is correct)
  const int SAMPLES = 2;
  float totalVoltage = 0;

  for (int i = 0; i < SAMPLES; i++) {

 int v = analogRead(7);
 HWCDCSerial.println(v);
 float adcVoltage = (float(map(v, 0, 4095, 15, 245)) / 100.00f) * 1.4f;
 HWCDCSerial.println(adcVoltage);
    totalVoltage += adcVoltage;
  }

  float measuredVoltage = totalVoltage / SAMPLES;
  batteryVoltage = (measuredVoltage * (100000 /* ohms*/ + 100000 /* ohms*/)) / 100000 /* ohms*/;

  HWCDCSerial.println(batteryVoltage);

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
   HWCDCSerial.flush();
   return 1;
    }
  }
  batteryPercentage = 100;
  HWCDCSerial.flush();
  return 0;
}

void leveledRefresh(int refreshLevel) { // version 2 no paging
 if (refreshLevel == 0) {
  return;
 }
 display.fillScreen(0xFFFF);
 display.display(true);

 if (refreshLevel > 1) {
  display.fillScreen(0x0000);
  display.display(true);
  if (refreshLevel > 2) {
   display.fillScreen(0xFFFF);
   display.display(true);
  }
 }
}

void drawCornerStats() {
 display.setFont(&Lora_Italic_VariableFont_wght7pt7b);
 display.setCursor(381, 15);
 display.print(String(hour) + ":" + String(minute));

 // battery
 display.setCursor(381, 30);
 display.print(String(batteryPercentage) + "%");
}

void setup()
{
 if (DEBUG) {
  HWCDCSerial.begin(115200);
  delay(500);
 }

 HWCDCSerial.println("began serial wheyyy");
 // init buttons
 rtc_gpio_deinit(deepWakePin);
 pinMode(1, 0x05);
 pinMode(6, 0x05);
 pinMode(5, 0x05);
 pinMode(4, 0x05);
 pinMode(2, 0x05);

 pinMode(7, 0x01);

 SPI.begin(12, 11, 13, 9);
 HWCDCSerial.println("began spi");
 Wire.begin(38, 37);
 HWCDCSerial.println("began i2c");
 setRTCToCompileTime();

 display.init(0, false, 50, false);
 HWCDCSerial.println("inited display");

 analogSetAttenuation(ADC_11db);

 display.setRotation(3);
 display.setFont(&FreeMonoBold9pt7b);
 display.setTextColor(0x0000);

 if(!SD.begin(10)){
  HWCDCSerial.println("card mount failed lololol");
 }

 uint8_t cardType = SD.cardType();
 if(cardType == CARD_NONE){
  HWCDCSerial.println("did you forget to plug the card in (or spi isnt working ig)");
 }

 HWCDCSerial.println("initing sd card...");
 if (!SD.begin(10)) {
  HWCDCSerial.println("sd card init failed lololololollol");
 }

 esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();

 readBattery();
 updateTime();

 display.setPartialWindow(0, 0, display.width(), display.height());

 display.fillScreen(0xFFFF);
 display.display(true);

 display.fillScreen(0x0000);
 display.display(true);

 display.fillScreen(0xFFFF);
 display.display(true);

 drawBinaryImage("/assets/mainmenu/mainmenu.bin", 0, 0, display.width(), display.height());
 drawCornerStats();
 display.display(true);

 setListMenuToDir("/sync/");

 HWCDCSerial.println("setup finished");
 HWCDCSerial.flush();
}

void setRTCToCompileTime() {
 char monthStr[4];
 int day, year;
 int hour, minute, second;

 // Parse compile date, e.g. "Oct 21 2026"
 sscanf("Nov  1 2026", "%3s %d %d", monthStr, &day, &year);

 // Parse compile time, e.g. "17:57:00"
 sscanf("14:24:57", "%d:%d:%d", &hour, &minute, &second);

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
  HWCDCSerial.println("Failed to parse compile month");
  return;
 }

 // Start writing at the seconds register (0x02).
 Wire.beginTransmission(0x51);
 Wire.write(0x02);
 Wire.write(decToBcd(second)); // Seconds
 Wire.write(decToBcd(minute)); // Minutes
 Wire.write(decToBcd(hour)); // Hours
 Wire.write(decToBcd(day)); // Day of month
 Wire.write(1); // Weekday (0–6; placeholder)
 Wire.write(decToBcd(month)); // Month
 Wire.write(decToBcd(year % 100)); // Year (00–99)

 if (Wire.endTransmission() == 0) {
  HWCDCSerial.println("RTC set to sketch compile time.");
 } else {
  HWCDCSerial.println("Failed to set RTC.");
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
 Wire.beginTransmission(0x51);
 Wire.write(0x02); // Seconds register

 if (Wire.endTransmission(false) != 0 ||
  Wire.requestFrom(0x51, 3) != 3) {
  HWCDCSerial.println("Read failed");
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

 if (digitalRead(1) == 0x0) buttonsPressed += "PWR ";
 if (digitalRead(6) == 0x0) buttonsPressed += " B1 ";
 if (digitalRead(5) == 0x0) buttonsPressed += " B2 ";
 if (digitalRead(4) == 0x0) buttonsPressed += " B3 ";
 if (digitalRead(2) == 0x0) buttonsPressed += " B4 ";

 do {
  display.setCursor(0, 230);
  display.fillRect(0, 208, display.width(), 32, 0xFFFF);
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
  pinMode((uint8_t)pin, 0x05);
 }

 // Enable wake-up when any pin goes LOW
 for (gpio_num_t pin : wakePins) {
  do { esp_err_t err_rc_ = (gpio_wakeup_enable(pin, GPIO_INTR_LOW_LEVEL)); if ((err_rc_ != 0 /*!< esp_err_t value indicating success (no error) */)) { _esp_error_check_failed(err_rc_, "/home/cheeto/Arduino/flippy/flippy.ino", 1139, 
# 1139 "/home/cheeto/Arduino/flippy/flippy.ino" 3
 __PRETTY_FUNCTION__
# 1139 "/home/cheeto/Arduino/flippy/flippy.ino"
 , "gpio_wakeup_enable(pin, GPIO_INTR_LOW_LEVEL)"); } } while(0)

   ;
 }

 do { esp_err_t err_rc_ = (esp_sleep_enable_gpio_wakeup()); if ((err_rc_ != 0 /*!< esp_err_t value indicating success (no error) */)) { _esp_error_check_failed(err_rc_, "/home/cheeto/Arduino/flippy/flippy.ino", 1144, 
# 1144 "/home/cheeto/Arduino/flippy/flippy.ino" 3
__PRETTY_FUNCTION__
# 1144 "/home/cheeto/Arduino/flippy/flippy.ino"
, "esp_sleep_enable_gpio_wakeup()"); } } while(0);

 // Also wake after five minutes
 do { esp_err_t err_rc_ = (esp_sleep_enable_timer_wakeup(SLEEP_TIME_US)); if ((err_rc_ != 0 /*!< esp_err_t value indicating success (no error) */)) { _esp_error_check_failed(err_rc_, "/home/cheeto/Arduino/flippy/flippy.ino", 1147, 
# 1147 "/home/cheeto/Arduino/flippy/flippy.ino" 3
__PRETTY_FUNCTION__
# 1147 "/home/cheeto/Arduino/flippy/flippy.ino"
, "esp_sleep_enable_timer_wakeup(SLEEP_TIME_US)"); } } while(0)

  ;

 if (DEBUG) {
  HWCDCSerial.flush(); // make sure all serial has been transmitted
  HWCDCSerial.end();
 }

 esp_light_sleep_start();

 if (DEBUG) {
  HWCDCSerial.begin(115200); // reinit serial
  delay(500);
  HWCDCSerial.println("wakey wakey");
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
 do { esp_err_t err_rc_ = (esp_sleep_enable_ext0_wakeup(deepWakePin, 0)); if ((err_rc_ != 0 /*!< esp_err_t value indicating success (no error) */)) { _esp_error_check_failed(err_rc_, "/home/cheeto/Arduino/flippy/flippy.ino", 1180, 
# 1180 "/home/cheeto/Arduino/flippy/flippy.ino" 3
__PRETTY_FUNCTION__
# 1180 "/home/cheeto/Arduino/flippy/flippy.ino"
, "esp_sleep_enable_ext0_wakeup(deepWakePin, 0)"); } } while(0);
 esp_deep_sleep_start();
 // program never continues from here as deep sleep does not preserve RAM
}

void loop() {
 while (digitalRead(1) == 0x0) {
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
            HWCDCSerial.println("Wake-up reason: GPIO");
   buttonPressed = true;
            break;

        case ESP_SLEEP_WAKEUP_TIMER:
            HWCDCSerial.println("Wake-up reason: 5-minute timer");
            break;

        default:
            HWCDCSerial.printf("Wake-up reason: %d (other/undefined)\n",
                          (int)cause);
            break;
    }

 delay(100); // to allow time for multi button presses

 if (buttonPressed) {
  int pressedPin = 0;
  int secondPin = 0;

  // Print which pins are LOW after waking
  for (gpio_num_t pin : wakePins) {
   if (digitalRead((uint8_t)pin) == 0x0) {
    if (pressedPin == 0) { // if we have not found the first pressed button yet
     pressedPin = (int)pin;
     HWCDCSerial.print("pressed pin is ");
     HWCDCSerial.println(pin);
     int timeout = 0;
     while (digitalRead((uint8_t)pin) == 0x0 && timeout < 20) {
      delay(50);
      timeout += 1;
     }
    } else {
     secondPin = (int)pin; // if we have found a first pressed button
     HWCDCSerial.print("second pin is ");
     HWCDCSerial.println(pin);
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
   HWCDCSerial.println("second button pressed");

   pressedButton = concat(pressedButton, secondButton);
  }

  if (pressedButton == 0) { // power button
   HWCDCSerial.println("power button pressed");
   while (digitalRead(1) == 0x0) {
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

   HWCDCSerial.print("pressed button is ");
   HWCDCSerial.println(pressedButton);
  }
 } else { // if wakeup caused by sleep timeout
  HWCDCSerial.println("going to sleep due to timer");
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

 HWCDCSerial.println(menuToPath());

 Menu m = getMenu(currentMenu);

 if (!m.listMenu && !m.flashcard) {
  drawBinaryImage(menuToPath(), 0, 0, display.width(), display.height());

  if (m.drawTopLeftBattery) {
   display.setCursor(0, 74);
   display.setFont(&Lora_Italic_VariableFont_wght15pt7b);
   display.print(String(batteryPercentage) + "%");
  }

  if (m.drawCornerStats) {
   drawCornerStats();
  }
  display.display(true);
 } else if (!m.flashcard) { // menu is a listMenu so requires special handling
  setListMenuToDir(currentDirectory);
  drawListMenu();
 } else { // menu is flashcard so requires special handling
  drawFlashcard();
 }
}
// hmm
