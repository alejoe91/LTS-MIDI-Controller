#include <MIDI.h>

// Simple tutorial on how to receive and send MIDI messages.
// Here, when receiving any message on channel 4, the Arduino
// will blink a led and play back a note for 1 second.

MIDI_CREATE_DEFAULT_INSTANCE();

int estimatedBPM = 0;
long elapsedClockTime;
long previousClockTime;

long timeLedOn = 0;
int LED = 9;
int clockCount = 0;

bool isLedOn = false;
int ledDurationMs = 100;

void handleClock(){
    clockCount++;

    if (clockCount == 24)
    {
      elapsedClockTime = millis() - previousClockTime;
      previousClockTime = millis();
      Serial.print("Clock received - time from previous: ");
      Serial.print(elapsedClockTime);
      estimatedBPM = int(60 / (float(elapsedClockTime) / 1000.));
      Serial.print(" ms. Estimated BPM = ");
      Serial.println(estimatedBPM);
      digitalWrite(LED, HIGH);
      isLedOn = true;
      timeLedOn = millis();
      clockCount = 0;
    }
}

void handleStart(){}

void handleStop(){}


void setup()
{
    pinMode(LED, OUTPUT);

    MIDI.begin(MIDI_CHANNEL_OMNI); // Launch MIDI and listen to all channels
    Serial.begin(9600);
    while (! Serial) {}
    previousClockTime = millis();

    Serial.println("Waiting for MIDI messages!");

    MIDI.setHandleClock(handleClock);
    MIDI.setHandleStart(handleStart);
    MIDI.setHandleStop(handleStop);
}

void loop()
{
    if ((isLedOn) && ((millis() - timeLedOn) > ledDurationMs))
    {
      digitalWrite(LED, LOW);
      isLedOn = false;
    }
    if (MIDI.read())                    // If we have received a message
    {
        switch(MIDI.getType())      // Get the type of the message we caught
        {
            case midi::Clock:       // If it is a Program Change,
                break;
            case midi::Tick:       // If it is a Program Change,
                Serial.println("Tick received");
                break;
            // See the online reference for other message types
            case midi::Start:       // If it is a Program Change,
                Serial.println("Start received");
                clockCount = 0;
                break;
            case midi::Stop:       // If it is a Program Change,
                Serial.println("Stop received");
                break;
            // See the online reference for other message types
            default:
                break;
        }
    }
}
