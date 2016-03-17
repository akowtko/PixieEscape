/*
 * Pixie Escape Spell Game Code
 * By:  Nicole Kowtko
 * For: ECS511U
 */

// Shift Register pin locations
// Data pin is MOSI (Uno and earlier: 11)
// Clock pin is SCK (Uno and earlier: 13)

#define IRLED 7             //IR LED pin number
#define numSensors 16       //number of IR sensors
float IR[numSensors];       //Stores calibrated IR value to send to RGB LED
const int ShiftPWM_latchPin=8; //Latch pin for Shift Registers
const bool ShiftPWM_invertOutputs = false; //More defaults for Shift Registers
const bool ShiftPWM_balanceLoad = false;   // "                              "

#include <ShiftPWM.h>   // Shift Register PWM library by: Elco Jacobs

unsigned char maxBrightness = 255;  //Maximum RGB brightness
unsigned char pwmFrequency = 75;    //PWM frequency for RGBs
int numRegisters = 6;               //Number of shift registers
int numRGBleds = numRegisters*8/3;  //Number of RGB LEDs (16)
int numReadings = 4.0;              //How many numbers to conduct running average over.
int maxValues[16] = {-1};           //Array to store IR Sensor's max value, set during calibration
int readings[16][4];                // the readings from the analog input (for "smooth" function)
int readIndex[16] = {0};            // the index of the current reading (for "smooth" function)
int total[16] = {0};                // the running total (for "smooth" function)
int average[16] = {0};              // the average (for "smooth" function)
int normalize[16] = {0};            //The base noise for each IR Sensor, calculated during calibration
int selectionTimer[16] = {0};       //Counts how many loops we've detected an IR value above the threshold for a sensor
int selected[16] = {0};             //If the sensor is selected, set the row to 1.
int selectedThreshold = 40;         //IR threshold over which the IR sensor is considered selected by the user
int loopCounter = 0;                //Counts number of loops the sketch has run, for calibration purposes
int deselectTimer[16] = {0};        //Counts number of loops since that RGB LED has been in the "selected" state
int wingardium[9] = {0, 4, 10, 14, 11, 15, 12, 8, 3}; //Order of pins for wingardium leviosa spell
int pixies[3] = {0};                //Array storing pin location of the pixies
int pixiesKilled = 0;               //Keeps track of the number of pixies killed
int missOrHit[16] = {0};            //Array storing info about that RGB pin and if it's 0=not yet guessed, 1=a miss, 2 = a hit
int won = 0;                        //Stores state of game. Won = 1 means the game is over and the user won

//                     A  B  C  D
const int channel[] = {5, 4, 3, 2}; //Order of the pins for the MUX channels
//Bit table for selecting each of the pins for the multiplexer
int bitTable[16][4] = {{0,0,0,0},
                      {1,0,0,0},
                      {0,1,0,0},
                      {1,1,0,0},
                      {0,0,1,0},
                      {1,0,1,0},
                      {0,1,1,0},
                      {1,1,1,0},
                      {0,0,0,1},
                      {1,0,0,1},
                      {0,1,0,1},
                      {1,1,0,1},
                      {0,0,1,1},
                      {1,0,1,1},
                      {0,1,1,1},
                      {1,1,1,1}};
                      
const int inputPin = A0;            //Analog pin for MUX's input

void setup() {
  pinMode(IRLED, OUTPUT);           //Sets IR LED as output
  digitalWrite(IRLED, HIGH);        //Turns on IR LED
  Serial.begin(9600);
  for (int thisPin = 2; thisPin < 6; thisPin++) {
    pinMode(thisPin, OUTPUT);       //Sets MUX pins as outputs in turn
  }
  pinMode(inputPin, INPUT);         //makes A0 the MUX input pin

  clearSmoothingArray();            //clears the array used for the running average smoothing
  //Various settings for shift register
  ShiftPWM.SetAmountOfRegisters(numRegisters);
  ShiftPWM.SetPinGrouping(1);
  ShiftPWM.Start(pwmFrequency,maxBrightness);
  ShiftPWM.SetAll(0);
  calibrateChannels();
  randomSeed(analogRead(1));        //Randomizes based on noise from A1 pin
}

void loop() {
  //Loops through the 16 IR sensors
  for (int thisChannel = 0; thisChannel < 16; thisChannel++) {
    //If this pin hasn't been guessed yet, then continue
    if(missOrHit[thisChannel] == 0){
      muxWrite(thisChannel);        //force the MUX to switch to this pin
      measureIR(thisChannel);
      //Checks to see if we're beyond the calibration phase
      if (loopCounter > 165){
        //The pin is considered "selected" if the IR sensor reads above the threshold 6 times in a row
        isSelected(thisChannel, 6);
        //Light up any selected pins (channels)
        highlightSelected(thisChannel);
        //if the pin is lit up
        if(selected[thisChannel] == 1){
          //Make the pin white for a moment so the user knows they've selected the pin
          ShiftPWM.SetRGB(thisChannel, 255, 255, 255);
          delay(750);
          //Turn off all the pins in preparation for casting the spell         
          deselectAll();
          traceWingardium(1);
          //Check if the guess is a hit or miss, then if the game has been won or not
          checkGuess(thisChannel);
          if(won==1){
            break;
          }
          else{
            //Makes previous hits and misses show up on the board after the spell has been cast
            reselect();
          }
        }
      }
      else{
        highlightSelected(thisChannel);
      }
    }
  }
  if(loopCounter == 165){
    Serial.println("Begin game!");
    tutorial();
    startGame();
    loopCounter++;
  } 
  //Calibration phase is the first 165 loops used for setting the max values on the IR sensors
  else if(loopCounter < 165){
    loopCounter++;
  }
}

/************************************************************************
 * tutorial()
 * Function that runs the tutorials: choosing an LED and casting a spell
 ************************************************************************/
void tutorial(){
  chooseRGBTutorial();
  flashWingardium();
  traceWingardium(0);
}

/******************************************************************
 * startGame()
 * Initiates game by sparkling the LEDs and generating the 3 pixies
 ******************************************************************/
void startGame(){
  sparkle();
  generatePixies();
}

/************************************************************************
 * chooseRGBTutorial()
 * Teaches the user how to select RGB LEDs as guesses for where pixie is located by
 * flashing an LED until the user selects it.
 ************************************************************************/
void chooseRGBTutorial(){
  int RGB = random(1,16);
  muxWrite(RGB);
  //flashes random RGB LED
  flashLED(RGB, 200);
  int counter = 0;
  //Keeps flashing the pin until the user has selected it.
  while(1){
    measureIR(RGB);
    if(IR[RGB] > selectedThreshold + 10){
      highlightSelected(RGB);
      if(counter > 3){
        highlightSelected(RGB);
        delay(600);
        break;
        }
      else{counter ++;}
    }
    else{flashLED(RGB, 200);}
  }
  //After it's been selected turn the LED off.
  ShiftPWM.SetRGB(RGB, 0, 0, 0);
}

/************************************************************************
 * measureIR(int thisChannel)
 * Measures and processes the value for an IR sensor
 ************************************************************************/
void measureIR(int thisChannel){ 
  int averageNum;
  int smoothedNum;
  int normalizedNum;
  averageNum = analogRead(inputPin);
  //Conducts running average on the last 4 values
  smoothedNum = smooth(thisChannel, averageNum);
  //If this number is greater than the previous maximums, set it as the maximum
  if(smoothedNum > maxValues[thisChannel]){
      maxValues[thisChannel] = smoothedNum;
  }
  //Normalize the IR value by subtracting the channel's noise from it
  normalizedNum =  smoothedNum - (normalize[thisChannel])*1.0;
  /*
  Serial.print("IR Sensor ");
  Serial.print(thisChannel);
  Serial.print(": ");
  Serial.println(normalizedNum);
  */
     
  //Anything less than 40 is considered noise 
  if(normalizedNum < 40){
    normalizedNum = 0; 
  }
  //Divide the normalized IR value by the maximum, then multiply by 255 so the value ranges from 0 to 255
  IR[thisChannel] = ( normalizedNum / (maxValues[thisChannel]*1.0) )*255.0;
  //Ensure no IR -> RGB value is above 255
  if(IR[thisChannel] > 255){
    IR[thisChannel] = 255;
  }
}

/************************************************************************
 * clearSmoothingArray()
 * Clears the arrays used for the running average over IR values (smoothing)
 ************************************************************************/
void clearSmoothingArray(){
  //Clearing out the smoothing array
  for (int row = 0; row < 16; row++){
      for (int thisReading = 0; thisReading < numReadings; thisReading++) {
        readings[row][thisReading] = 0;
        total[row] = 0;
        average[row] = 0;
        normalize[row] = 0;
      }
  }
}

/************************************************************************
 * isSelected(int thisChannel, int threshCount)
 * Inputs: thisChannel (current pin/IR sensor), 
 *         threshCount (number of times threshold must be triggered before pin "selected"
 * Sets whether the current IR sensor is selected by the user or not
 ************************************************************************/
void isSelected(int thisChannel, int threshCount){
  //Check if IR sensor above threshold
  if(IR[thisChannel] > selectedThreshold){
    //Increment the selection timer
    selectionTimer[thisChannel] = selectionTimer[thisChannel] + 1;
    //Once the sensor's been above the thresh a certain number of times...
    if(selectionTimer[thisChannel] > threshCount){
      //The pin is "selected"
      selected[thisChannel] = 1;
      selectionTimer[thisChannel] = 0;
    }
  }
  //IR Sensor wasn't triggered, but previously was, so reset the timer
  else if(selectionTimer[thisChannel] != 0){
    selectionTimer[thisChannel] = 0;
  }
}

/************************************************************************
 * highlightSelected(int thisChannel)
 * Turns selected RGBs gold, and non-selected ones blue/green depending on the intensity
 * of incident IR light.
 ************************************************************************/
void highlightSelected(int thisChannel){
  //If the RGB is selected, turn it gold
  if(selected[thisChannel] == 1){
    ShiftPWM.SetRGB(thisChannel, 255, 0, 128);
    deselectTimer[thisChannel] = deselectTimer[thisChannel] + 1;
  }
  //If not selected, the RGB is blue/green depending on the IR intensity
  else{
    ShiftPWM.SetRGB(thisChannel, 0, IR[thisChannel], IR[thisChannel]/2.0);
  }
}

/************************************************************************
 * deselect(int thisChannel)
 * Turns off the specified RGB and deselects it.
 ************************************************************************/
void deselect(int thisChannel){
  selected[thisChannel] = 0;
  deselectTimer[thisChannel] = 0;
  ShiftPWM.SetRGB(thisChannel, 0, 0, 0);
}

/************************************************************************
 * deselectAll()
 * Turns off all the RGBs
 ************************************************************************/
void deselectAll(){
  for(int i=0; i<16; i++){
    deselect(i);
  }
}

/************************************************************************
 * reselect()
 * Rehighlights missed or hit pixies after conducting the spell
 ************************************************************************/
void reselect(){
  for(int i=0; i<16; i++){
    //If this pin was a miss, make it Red
    if(missOrHit[i] == 1){
      ShiftPWM.SetRGB(i, 255, 0, 0);
    }
    //If the pin was a hit, make it Green
    else if(missOrHit[i] == 2){
      ShiftPWM.SetRGB(i, 0, 0, 255);
    }
  }
}

/************************************************************************
 * maxCalc(int num, int thisChannel)
 * Checks whether the input 'num' is greater than the previously stored
 * maximum value for the pin 'thisChannel'. If so, set 'num' as the new
 * maximum.
 ************************************************************************/
void maxCalc(int num, int thisChannel){          
  if(num > maxValues[thisChannel]){
    maxValues[thisChannel] = num;
  }   
}

/************************************************************************
 * calibrateChannels()
 * Averages the ambient light received by the IR sensor over 'numReadings' data points
 * to determine the base value (or noise) for each sensor, storedin the 'normalize'
 * array.
 ************************************************************************/
void calibrateChannels(){
    for (int thisChannel = 0; thisChannel < 16; thisChannel++) {
      // set the channel pins based on the channel you want:
      muxWrite(thisChannel);
      
      for (int i=0; i<numReadings; i++){
        readings[thisChannel][i] = analogRead(inputPin);
        total[thisChannel] = total[thisChannel] + readings[thisChannel][i];
        delay(1);
      }
      normalize[thisChannel] = total[thisChannel]/numReadings;
      /*
      Serial.print(thisChannel);
      Serial.print(": ");
      Serial.println(normalize[thisChannel]);
      */
  }  
}

/************************************************************************
 * checkGuess(int thisChannel)
 * Checks whether there is a pixie at the current channel. Highlights the
 * RGB accordingly for a hit or miss.
 ************************************************************************/
void checkGuess(int thisChannel){
  int hit = 0;
  //Checks all three pixie locations in the pixie array
  for(int i=0; i<3; i++){
    //If statement if the guess was a hit
    if(pixies[i] == thisChannel){
      pixiesKilled = pixiesKilled + 1;
      missOrHit[thisChannel] = 2;
      hit = 1;
      ShiftPWM.SetRGB(thisChannel, 0, 0, 255);
      checkWon();
    }
  }
  //If after looping through the pixie array there was no hit,
  //the guess was a miss.
  if(hit == 0){
    missOrHit[thisChannel] = 1;
    ShiftPWM.SetRGB(thisChannel, 255, 0, 0);
  }
}

/************************************************************************
 * checkWon()
 * If all three pixies were killed, you've won; make the board sparkle.
 ************************************************************************/
void checkWon(){
  if(pixiesKilled == 3){
    won = 1;
    sparkle();
  }
}

/************************************************************************
 * traceWingardium(int mode)
 * Inputs:  mode  0 for tutorial mode, making the 'W' figure flash after tracing
 * Allows user to trace 'wingardium' figure, only accepting the next pin in the series
 * as input until the figure is complete.
 ************************************************************************/
void traceWingardium(int mode){
  int start;
  if(mode == 0){
    start = 1;
  }
  else{
    start = 0;
  }
  for(int i=start; i<9; i++){
    muxWrite(wingardium[i]);
    //Keep measuring IR values until the user has selected the next IR sensor
    //in the figure.
    while(selected[wingardium[i]] == 0){
      measureIR(wingardium[i]);
      isSelected(wingardium[i], 3);
      if(selected[wingardium[i]] == 1){
        highlightSelected(wingardium[i]);
        break;
      }
    }
  }
  deselectAll();
  if(mode == 0){
    for(int i=0; i<4; i++){
      flashArray(wingardium, 400, 9);
    }
  }
}

/************************************************************************
 * flashWingardium()
 * RGBs light up one at a time to 'trace' the wingardium spell. The system
 * only reads input from IR Sensor 0 waiting for the user to start tracing
 * the spell.
 ************************************************************************/
void flashWingardium(){
  muxWrite(wingardium[0]);
  while(selected[wingardium[0]] == 0){
    for(int i=0; i<9; i++){
      flashLED(wingardium[i], 100);
      measureIR(wingardium[0]);
      if(IR[wingardium[0]] > selectedThreshold){
        selected[wingardium[0]] = 1;
        highlightSelected(wingardium[0]);
        break;
      }
    }
  }
}

/************************************************************************
 * flashArray(int ledArray[], int delayTime, int len)
 * Flashes all pins in the array 'ledArray[]' with speed 'delayTime'.
 ************************************************************************/
void flashArray(int ledArray[], int delayTime, int len){
  for(int i=0; i<len; i++){
    ShiftPWM.SetRGB(ledArray[i],0,255,255);
  }
  delay(delayTime);
  for(int i=0; i<len; i++){
    ShiftPWM.SetRGB(ledArray[i],0,0,0);
  }
  delay(delayTime);
}

/************************************************************************
 * flashLED(int led, int delayTime)
 * Flashes a single led at a certain rate determined by 'delayTime'
 ************************************************************************/
void flashLED(int led, int delayTime){
  ShiftPWM.SetRGB(led,0,255,255);
  delay(delayTime);
  ShiftPWM.SetRGB(led,0,0,0);
  delay(delayTime);
}

/************************************************************************
 * sparkle()
 * Turns RGBs on and off randomly for random durations.
 ************************************************************************/
void sparkle(){
  int led;
  int delayTime;
  int ledoff;
  for(int i=0; i< 200; i++){
    led = random(16);
    delayTime = random(20);
    ledoff = random(16);
    ShiftPWM.SetRGB(led,0,255,255);
    ShiftPWM.SetRGB(ledoff,0,0,0);
    delay(delayTime);    
  }
  for (int i=0; i<16; i++){
    ShiftPWM.SetRGB(i,0,0,0);
  }
}

/************************************************************************
 * generatePixies()
 * Randomly generates three pixies, ensuring all three are at separate locations.
 ************************************************************************/
void generatePixies(){
  int pixie;
  int one;
  int two;
  for(int i=0; i<3; i++){
    pixie = random(16);
    if(i==0){
      one = pixie;
    }
    //Checks to ensure Pixie 1 is not in the same location as Pixie 0
    else if(i==1){
      if(pixie == one){
        while(pixie == one){
          pixie = random(16);
        }
      }
      two = pixie;
    }
    //Check to ensure Pixie 2 is not in the same location as Pixie 0 or Pixie 1
    else{
      if(pixie == one || pixie == two){
        while(pixie == one || pixie == two){
          pixie = random(16);
        }
      }
    }
    pixies[i] = pixie;
    //Prints pixie locations to the serial monitor
    Serial.print("Pixie ");
    Serial.print(i);
    Serial.print(" at LED ");
    Serial.println(pixie);
  }
}

/************************************************************************
 * smooth(int thisChannel, int averageNum)
 * Conducts a running average over 'numReadings' readings. 'averageNum' is the IR value.
 * Code obtained from: https://www.arduino.cc/en/Tutorial/Smoothing
 ************************************************************************/
int smooth(int thisChannel, int averageNum){
  total[thisChannel] = total[thisChannel] - readings[thisChannel][readIndex[thisChannel]];
  // read from the sensor:
  readings[thisChannel][readIndex[thisChannel]] = averageNum;
  // add the reading to the total:
  total[thisChannel] = total[thisChannel] + readings[thisChannel][readIndex[thisChannel]];
  // advance to the next position in the array:
  readIndex[thisChannel] = readIndex[thisChannel] + 1;

  // if we're at the end of the array...
  if (readIndex[thisChannel] >= numReadings) {
    // ...wrap around to the beginning:
    readIndex[thisChannel] = 0;
  }
  // calculate the average:
  average[thisChannel] = total[thisChannel] / numReadings;

  delay(1);
  return average[thisChannel];
}

/************************************************************************
 * muxWrite(int whichChannel)
 * Sets the MUX's channel based on the input 'whichChannel' and the bitTable.
 ************************************************************************/
void muxWrite(int whichChannel) {
  //Set A to
  digitalWrite(channel[0], bitTable[whichChannel][0]);
  delay(1);
  digitalWrite(channel[1], bitTable[whichChannel][1]);
  delay(1);
  digitalWrite(channel[2], bitTable[whichChannel][2]);
  delay(1);
  digitalWrite(channel[3], bitTable[whichChannel][3]);
  delay(1);
  /*
  Serial.print(bitTable[whichChannel][0]);
  Serial.print(bitTable[whichChannel][1]);
  Serial.print(bitTable[whichChannel][2]);
  Serial.println(bitTable[whichChannel][3]);
  */
}
