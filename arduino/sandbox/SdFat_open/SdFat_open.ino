/*
 * Print size, modify date/time, and name for all files in root.
 */
#include "SdFat.h"


// SD Card configuration
#define SD_FAT_TYPE 0
#define error(s) sd.errorHalt(&Serial, F(s))
// Try max SPI clock for an SD. Reduce SPI_CLOCK if errors occur.
#define SPI_CLOCK SD_SCK_MHZ(50)

// // Try to select the best SD card configuration.
#if HAS_SDIO_CLASS
#define SD_CONFIG SdioConfig(FIFO_SDIO)
#elif ENABLE_DEDICATED_SPI
#define SD_CONFIG SdSpiConfig(SD_CS_PIN, DEDICATED_SPI, SPI_CLOCK)
#else  // HAS_SDIO_CLASS
#define SD_CONFIG SdSpiConfig(SD_CS_PIN, SHARED_SPI, SPI_CLOCK)
#endif  // HAS_SDIO_CLASS

const uint8_t SD_CS_PIN = 4;

SdFat sd;
File dir;
File file;
File songFiles[30];
size_t size = 100;
char filename[100];
String filestr;
uint8_t numSongs = 0;
uint8_t currentSong = 0;

void setup() {
  Serial.begin(9600);

  // Wait for USB Serial
  while (!Serial) {
    yield();
  }
  Serial.print("Default SD_CS_PIN: ");
  Serial.println(SD_CS_PIN);


  Serial.println("Type any character to start");
  while (!Serial.available()) {
    yield();
  }

  // Initialize the SD.
  if (!sd.begin(SD_CONFIG)) {
    sd.initErrorHalt(&Serial);
  }
  // Open root directory
  if (!dir.open("/")) {
    error("dir.open failed");
  }
  // Open next file in root.
  // Warning, openNext starts at the current position of dir so a
  // rewind may be necessary in your application.
  while (file.openNext(&dir, O_RDONLY)) {
    file.getName(filename, size);
    // String filestring = String(filename);
    Serial.print(filename);
    // Serial.print(filestring);
    Serial.print("  ");
    Serial.println((filename[0] == '.'));

    if (!(filename[0] == '.'))
    {
      file.printFileSize(&Serial);
      Serial.write(' ');
      file.printModifyDateTime(&Serial);
      Serial.write(' ');
      file.printName(&Serial);
      if (file.isDir()) {
        File fileSub;

        songFiles[numSongs] = file;
        numSongs++;

        // Indicate a directory.
        Serial.write('/');
        Serial.println();
        while (fileSub.openNext(&file, O_RDONLY))
        {
          fileSub.getName(filename, size);
          filestr = String(filename);
          if (~filestr.startsWith("."))
          {
            Serial.write('        ');
            fileSub.printFileSize(&Serial);
            Serial.write(' ');
            fileSub.printModifyDateTime(&Serial);
            Serial.write(' ');
            fileSub.printName(&Serial);
            if (fileSub.isDir()) 
            {
              // Indicate a directory.
              Serial.write('/');
            }
          }
          Serial.println();
          fileSub.close();
        }
    }
    Serial.println();
    file.close();
    }

  }
  if (dir.getError()) {
    Serial.println("openNext failed");
  } else {
    Serial.print("Songs loaded! Found ");
    Serial.print(numSongs);
    Serial.println(" songs.");

    // Display songs on LCD
    
  }
}
//------------------------------------------------------------------------------
void loop() {}