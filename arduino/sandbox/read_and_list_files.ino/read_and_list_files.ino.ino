/*
  Listfiles

  This example shows how print out the files in a
  directory on a SD card

  The circuit:
   SD card attached to SPI bus as follows:
 ** MOSI - pin 11
 ** MISO - pin 12
 ** CLK - pin 13
 ** CS - pin 4 (for MKRZero SD: SDCARD_SS_PIN)

  created   Nov 2010
  by David A. Mellis
  modified 9 Apr 2012
  by Tom Igoe
  modified 2 Feb 2014
  by Scott Fitzgerald

  This example code is in the public domain.

*/
#include <SPI.h>
#include <SD.h>

// Try SdFat to read LONGNAMES

int SD_PORT = 4;

File root;
File list_files[20];
int num_files = 0;

void setup() {
  // Open serial communications and wait for port to open:
  Serial.begin(9600);
  while (!Serial) {
    ; // wait for serial port to connect. Needed for native USB port only
  }

  Serial.print("Initializing SD card...");

  if (!SD.begin(SD_PORT)) {
    Serial.println("initialization failed!");
    while (1);
  }
  Serial.println("initialization done.");

  root = SD.open("/");

  num_files = gather_files(root, 0, list_files);

  Serial.println(num_files);

  Serial.println("done!");
}

void loop() {
  // nothing happens after setup finishes.
}

int gather_files(File dir, int numTabs, File list_files[]) {
  int num = 0;
  while (true) {

    File entry =  dir.openNextFile();
    if (! entry) {
      // no more files
      break;
    }
    for (uint8_t i = 0; i < numTabs; i++) {
      Serial.print('\t');
    }
    char* filename = entry.name();
    Serial.print(filename);
    Serial.print("\t\t");
    Serial.print(sizeof(filename)*8);

    if (entry.isDirectory()) {
      Serial.println("This function only lists files in the rood directory");
    } else {
      // files have sizes, directories do not
      Serial.print("\t\t");
      Serial.println(entry.size(), DEC);
      list_files[num] = entry;
      num++;
      char c;
      while (1){
        c = entry.read();
        if (c != -1)
        {
          Serial.print(c);
        }
        else
        {
          Serial.println();
          break;          
        }
      }
    }
    entry.close();
  }
  return num;
}



