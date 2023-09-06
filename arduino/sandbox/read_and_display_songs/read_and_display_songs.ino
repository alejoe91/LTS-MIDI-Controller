/*
 * Print size, modify date/time, and name for all files in root.
 */
#include <SdFat.h>
#include <EncoderButton.h>
#include <LiquidCrystal_I2C.h>
#include <MIDI.h>
#include <MD_MIDIFile.h>

//------------------------------------------------------------------------------
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
File rootDir;
File songDir;
size_t size = 20;
char filename[20];

//------------------------------------------------------------------------------
// LCD configuration
const int pinInput1 = A0;  
const int pinInput2 = A1;
const int pinButton = 5;

EncoderButton myEnc(pinInput1, pinInput2, pinButton);
LiquidCrystal_I2C lcd(0x27,20,4);

bool lcdIsOn = true;

//------------------------------------------------------------------------------
// MIDI interfaces

//------------------------------------------------------------------------------

char songList[10][30];
uint8_t numSongs = 0;
int8_t currentSong = 0;
uint8_t firstSongDisplayed = 0;
uint8_t lastSongDisplayed = 3;
const uint8_t numSongsDisplayed = 4;
static enum { S_LOAD_SONG, S_WAIT_TO_START, S_PLAY } state = S_LOAD_SONG;

// Looper commands can be: R1-R2-S1-S2-C1-C2-SA-CA
// E.g. 33-R1 --> Rec/Play/Dub Channel 1 at beat 33
uint16_t looperBeats[20] = {0};
char looperCmds[20][3] = {""};
uint8_t numLooperCmds = 0;

// Synth commands can be: P (Play) or S (Stop)
// E.g. 33-129-P --> Play Preset 129 at beat 33 OR 127-0-S --> Stop at beat 127 (the second entry is ignored)
uint16_t synthBeats[20] = {0};
uint16_t synthPresets[20] = {0};
char synthCmds[20][2] = {""};
uint8_t numSynthCmds = 0;

//------------------------------------------------------------------------------
// Callbacks

// Encoder
void onClicked(EncoderButton& eb) {
  if (lcdIsOn)
    {
      lcd.noBacklight();
      lcdIsOn = false;
    }
  else
  {
    lcd.backlight();
    lcdIsOn = true;
  }
}

void onEncoder(EncoderButton& eb) {
  int increment = eb.increment();
  currentSong = currentSong + increment;
  displaySongs();
}

// MIDI
// void handleClock(){}

// void handleStart(){}

// void handleStop(){}

// void midiCallback(midi_event *pev){}


// Loaders and String helpers
int endsWith(const char *str, const char *suffix)
{
    if (!str || !suffix)
        return 0;
    size_t lenstr = strlen(str);
    size_t lensuffix = strlen(suffix);
    if (lensuffix >  lenstr)
        return 0;
    return strncmp(str + lenstr - lensuffix, suffix, lensuffix) == 0;
}

bool startsWith(const char *pre, const char *str)
{
    size_t lenpre = strlen(pre),
           lenstr = strlen(str);
    return lenstr < lenpre ? false : memcmp(pre, str, lenpre) == 0;
}


void loadSongList(){
  // Open root directory
  if (!rootDir.open("/")) 
  {
    Serial.println("Root dir open failed");
  }
  else
  {
    // Open files and store names them
    while (songDir.openNext(&rootDir, O_RDONLY)) 
    {
      songDir.getName(filename, size);
      if (!(filename[0] == '.'))
      {
        if (songDir.isDir()) {
          File fileSub;
          songList[numSongs][0] = '\0';
          strncpy(songList[numSongs], filename, strlen(filename)); // fix this
          Serial.print("Song name: ");
          Serial.print(songList[numSongs]);
          numSongs++;

          Serial.write('/');
          Serial.println();
          while (fileSub.openNext(&songDir, O_RDONLY))
          {
            fileSub.getName(filename, size);
            Serial.print("  ");
            Serial.print(filename);
            Serial.println();
            fileSub.close();
          }
          
        }
      }
      songDir.close();
    }
    rootDir.close();
  } 
}

void displaySongs()
{
  // delete cursors
  lcd.clear();
  // for (int row=0; row<numSongsDisplayed; row++)
  // {
  //   lcd.setCursor(0, row);
  //   lcd.print(" ");
  // }
  Serial.print("Current pre: ");
  Serial.print(currentSong);
  Serial.print(" - First pre: ");
  Serial.print(firstSongDisplayed);
  Serial.print(" - Last pre: ");
  Serial.println(lastSongDisplayed);
  if (currentSong < 0)
  {
    Serial.println("Current song negative");
    currentSong = numSongs - 1;
    firstSongDisplayed = currentSong - numSongsDisplayed + 1;
    lastSongDisplayed = currentSong;
  }
  else if (currentSong > numSongs - 1)
  {
    Serial.println("Current song greater than num songs");
    currentSong = 0;
    firstSongDisplayed = 0;
    lastSongDisplayed = numSongsDisplayed - 1;
  }
  else if (currentSong > lastSongDisplayed)
  {
    Serial.println("Current song greater than last displayed");
    lastSongDisplayed = currentSong;
    firstSongDisplayed = lastSongDisplayed - numSongsDisplayed + 1;
  }
  else if (currentSong < firstSongDisplayed)
  {
    Serial.println("Current song less than first displayed");
    firstSongDisplayed = currentSong;
    lastSongDisplayed = firstSongDisplayed + numSongsDisplayed - 1;
  }

  Serial.print("Current post: ");
  Serial.print(currentSong);
  Serial.print(" - First post: ");
  Serial.print(firstSongDisplayed);
  Serial.print(" - Last post: ");
  Serial.println(lastSongDisplayed);

  // Display titles
  for (int row=firstSongDisplayed; row<=lastSongDisplayed; row++)
  {
      uint8_t real_row = row - firstSongDisplayed;
      lcd.setCursor(2, real_row);
      lcd.print(songList[row]);
  }
    
  // Display cursor
  lcd.setCursor(0, currentSong - firstSongDisplayed);
  lcd.print(">");
}

void loadSong(const char* songName){
  if (!rootDir.open("/")) {
    Serial.println("Root open failed");
  }
  else
  {
    File file;
    if (!songDir.open(songName)) {
      Serial.println("Song folder open failed");
    } 

    if (file.open(&songDir, "looper.txt", O_RDONLY))
    {
      // parse looper
      Serial.println("Parsing looper");
      char currentCmd[6]; // max size is 5 + NULL character
      currentCmd[0] = '\0';
      char currentChar[2];
      uint16_t currentBeat = 0;
      int data;
      if (file.fileSize() > 0)
      {
        bool isEmpty = false;
        while (!isEmpty) 
        {
          data = file.read();
          if ((char(data) == ',') || (data == -1))
          {
            /* 
            In this case, the command is complete and needs to be parsed.
            Commands for the looper are always in this format:
            {beat}-{cmd}
            */
            const char delimiter[2] = "-";
            char *looperCmd, beat_str;
            int beat;
            
            /* get the first token */
            beat = atoi(strtok(currentCmd, delimiter));
            looperCmd = strtok(NULL, delimiter);

            looperBeats[currentBeat] = beat;
            strncpy(looperCmds[currentBeat], looperCmd, 3);

            currentBeat++;
            // reset
            currentCmd[0] = '\0';
            if (data < 0)
            {
              // exit the loop
              isEmpty = true;
              Serial.println("End of file looper.txt");
            }
              
          }
          else
          {
            currentChar[0] = char(data);
            currentChar[1] = '\0';
            strcat(currentCmd, currentChar);
          }
        }
      }
      numLooperCmds = currentBeat;
      file.close();
    }
    if (file.open(&songDir, "synth.txt", O_RDONLY))
    {
      // parse synth
      Serial.println("Parsing synth");

      char currentCmd[20]; // max size is 5 + NULL character
      currentCmd[0] = '\0';
      char currentChar[2];
      uint16_t currentBeat = 0;

      // TODO fix this!
      int data;
      if (file.fileSize() > 0)
      {
        bool isEmpty = false;
        while (!isEmpty) 
        {
          data = file.read();
          // Serial.println(char(data));

          if ((char(data) == ',') || (data == -1))
          {
            /* 
            In this case, the command is complete and needs to be parsed.
            Commands for the looper are always in this format:
            {beat}-{cmd}
            */
            const char delimiter[2] = "-";
            char * synthCmd;
            uint8_t synthProgram;
            int beat, preset;
            
            // First token is always the beat, second is the Preset, third is rthe Cmd
            beat = atoi(strtok(currentCmd, delimiter));
            synthProgram = atoi(strtok(NULL, delimiter));
            synthCmd = strtok(NULL, delimiter);

            synthBeats[currentBeat] = beat;
            synthPresets[currentBeat] = synthProgram;
            strncpy(synthCmds[currentBeat], synthCmd, 3);
            currentBeat++;
            // reset
            currentCmd[0] = '\0';
            if (data < 0)
            {
              // exit the loop
              isEmpty = true;
              Serial.println("End of file synth.txt");
            }
          }
          else
          {
            currentChar[0] = char(data);
            currentChar[1] = '\0';
            strcat(currentCmd, currentChar);
          }
        }
      }
      numSynthCmds = currentBeat;
      file.close();
    }
    // file.close();
    songDir.close();
    rootDir.close();
  }
}

//------------------------------------------------------------------------------
// Setup
void setup() {
  pinMode(pinInput1, INPUT);
  pinMode(pinInput2, INPUT);
  pinMode(pinButton, INPUT);

  Serial.begin(9600);

  // Wait for USB Serial
  while (!Serial) {
    yield();
  }
  Serial.print("Default SD_CS_PIN: ");
  Serial.println(SD_CS_PIN);
  delay(2000);

  // Initialize LCD and Encoder.
  lcd.init(); // LCD driver initialization
  lcd.backlight(); // Open the backlight
  lcd.clear();

  myEnc.setClickHandler(onClicked);
  myEnc.setEncoderHandler(onEncoder);

  // Begin MIDI interfaces
  // MIDI IN
  // MIDI Looper
  // MIDI Synth

  // Initialize the SD and load files
  if (!sd.begin(SD_CONFIG)) {
    sd.initErrorHalt(&Serial);
  }
  loadSongList();

  if (numSongs > 0)
  {
    Serial.println("");
    Serial.print("Songs loaded! Found ");
    Serial.print(numSongs);
    Serial.println(" songs.");

    // lcd.setCursor(0, 0);
    // lcd.print(">");

    // // Display songs on LCD
    // for (int i=0; i<numSongs; i++)
    // {
    //   // File song = songFiles[i];
    //   // song.getName(filename, size);
    //   if (i < 4)
    //   {
    //     lcd.setCursor(2, i);
    //     lcd.print(songList[i]);
    //   }
    // }

    displaySongs();

    // Load one song
    // loadSong("Kids");
    // Serial.print("\nLooper # commands: ");
    // Serial.println(numLooperCmds);
    // for (int i; i<numLooperCmds; i++)
    // {
    //   Serial.print("Beat: ");
    //   Serial.print(looperBeats[i]);
    //   Serial.print(" Cmd: ");
    //   Serial.println(looperCmds[i]);
    // }
    // // synth
    // Serial.print("\nSynth # commands: ");
    // Serial.println(numSynthCmds);
    // for (int i; i<numSynthCmds; i++)
    // {
    //   Serial.print("Beat: ");
    //   Serial.print(synthBeats[i]);
    //   Serial.print(" Preset: ");
    //   Serial.print(synthPresets[i]);
    //   Serial.print(" Cmd: ");
    //   Serial.println(synthCmds[i]);
    // }
  }
  else
    Serial.println("Failed to load songs");
}

//------------------------------------------------------------------------------
// Start
void loop() {
  myEnc.update();

}
