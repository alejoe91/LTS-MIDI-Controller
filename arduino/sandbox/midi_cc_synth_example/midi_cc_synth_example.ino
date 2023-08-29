#include <MIDI.h>

// Test MIDI control of IK Multimedia Synth PRO
// 1. Change Preset
// 2. Start-Stop
// 3. Play note

/*
https://www.ikmultimedia.com/products/unosynthpro/index.php?p=manuals

CC0   Bank Select (MSB) (0-1)
CC32  Bank Select (LSB)
CC120 All Sound off
*/

MIDI_CREATE_DEFAULT_INSTANCE();
const int inChannelSynth = 3;
int LED = 9;


int initialDelayMs = 10000;
int delayMs = 2000;
int numIter = 0;
int maxIter = 20;
int bankMSB;
int program;

void setup()
{
    pinMode(LED, OUTPUT);

    MIDI.begin(inChannelSynth);                      // Launch MIDI and listen to channel 4
    Serial.begin(9600);
    while (! Serial) {}
}


void loop()
{
    if (numIter <= maxIter)
    {
      delay(delayMs);
      Serial.println("\n\n");
      Serial.print("Starting control: iter ");
      Serial.print(numIter);
      Serial.print(" out of ");
      Serial.print(maxIter);
      Serial.println("\n\n");
    }

    if (numIter < maxIter)
      {
      // Change to Preset 12
      Serial.println("Changing Preset 12");
      digitalWrite(LED, HIGH);
      delay(100);
      digitalWrite(LED, LOW);

      bankMSB = 0;
      program = 11;
      MIDI.sendControlChange(0, bankMSB, inChannelSynth);
      MIDI.sendProgramChange(program, inChannelSynth);
      MIDI.sendNoteOn(42, 127, inChannelSynth);    // Send a Note (pitch 42, velo 127 on channel 1)
      delay(1000);		            // Wait for a second
      MIDI.sendNoteOff(42, 0, inChannelSynth);     // Stop the note
      delay(delayMs);
      // Start Sequencer - needs clock
      /*

      Serial.println("Start Sequencer");
      MIDI.sendStart();
      delay(delayMs);
      // Stop Looper 1
      Serial.println("Stop sequencer");
      MIDI.sendStop();
      delay(delayMs);
      */

      // Bank MSB 1
      Serial.println("Changing Preset 129 (Kids)");
      digitalWrite(LED, HIGH);
      delay(100);
      digitalWrite(LED, LOW);
      
      bankMSB = 1;
      program = 0;
      MIDI.sendControlChange(0, bankMSB, inChannelSynth);
      MIDI.sendProgramChange(program, inChannelSynth);
      MIDI.sendNoteOn(42, 127, inChannelSynth);    // Send a Note (pitch 42, velo 127 on channel 1)
      delay(1000);		            // Wait for a second
      MIDI.sendNoteOff(42, 0, inChannelSynth);     // Stop the note
      delay(delayMs);
      // Start Sequencer (needs clock!)
      /*
      Serial.println("Start Sequencer");
      MIDI.sendStart();
      delay(delayMs);
      // Stop Looper 1
      Serial.println("Stop sequencer");
      MIDI.sendStop();
      delay(delayMs);
      */

      numIter++;
    }
    else if (numIter == maxIter)
    {
      Serial.println("Stop all");
      MIDI.sendControlChange(120, 127, inChannelSynth);
    }
}
