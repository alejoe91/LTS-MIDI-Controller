/* Encoder Library - Basic Example
 * http://www.pjrc.com/teensy/td_libs_Encoder.html
 *
 * This example code is in the public domain.
 */

#include <Encoder.h>
#include <EncoderButton.h>

#include <LiquidCrystal_I2C.h>

const int pinInput1 = A0;  
const int pinInput2 = A1;
const int pinButton = 5;


// Change these two numbers to the pins connected to your encoder.
//   Best Performance: both pins have interrupt capability
//   Good Performance: only the first pin has interrupt capability
//   Low Performance:  neither pin has interrupt capability
EncoderButton myEnc(pinInput1, pinInput2, pinButton);
LiquidCrystal_I2C lcd(0x27,20,4);

//   avoid using pins with LEDs attached
long oldPosition  = -999;
bool isOn = true;

void onClicked(EncoderButton& eb) {
  lcd.setCursor(0,3);             // Move the cursor to row 3, column 0
  lcd.print("                    ");
  if (isOn)
    {
      lcd.noBacklight();
      isOn = false;
    }
  else
  {
    lcd.backlight();
    isOn = true;
  }
  Serial.print("clickCount: ");
  Serial.println(eb.clickCount());
}

void onEncoder(EncoderButton& eb) {
  int newPosition = eb.position();
  int col;
  if (newPosition >= 0)
    col = 8;
  else
    col = 7;
  lcd.setCursor(0,3);             // Move the cursor to row 3, column 0
  lcd.print("                    ");
  lcd.setCursor(col,3);             // Move the cursor to row 2, column 0
  lcd.print(String(newPosition));
  if (newPosition != oldPosition) {
    oldPosition = newPosition;
    Serial.println(newPosition);
  }
}

void setup() {
  Serial.begin(9600);
  Serial.println("Basic Encoder Test:");
  pinMode(pinInput1, INPUT);
  pinMode(pinInput2, INPUT);
  pinMode(pinButton, INPUT);

  lcd.init(); // LCD driver initialization
  lcd.backlight(); // Open the backlight
  lcd.clear();
  lcd.setCursor(4,0);             // Move the cursor to row 0, column 0
  lcd.print("How much do");          // The print content is displayed on the LCD
  lcd.setCursor(4,1);             // Move the cursor to row 1, column 0
  lcd.print("LTS FUCKING");  // The print content is displayed on the LCD
  lcd.setCursor(7,2);             // Move the cursor to row 2, column 0
  lcd.print("ROCK"); 

  myEnc.setClickHandler(onClicked);
  myEnc.setEncoderHandler(onEncoder);
}

void loop() {
  myEnc.update();
}
