#include <SPI.h> //Read and Write the Shift Registers
#include <Wire.h> //I2C connection with the MIDIbox core

#include "colors.h" //approximations for the colors of ableton push

#include "ring-buffer.h" // buffer for the buttons presses
#include "button-event.h"
#include "change-color-event.h"

#define COLS 10
#define ROWS 6

//latch pin of the 74HC595s and 74HC165s shift registers
int latchPin = 10;
//SCK of 74HC595 and 74HC165
//Pin connected: int clockPin = 13; //default of SPI.h
//DS of 74HC595
//default pin: int dataPin = 11; //default SPI.h (74HC595)
//Pin connected to input of the 74HC165
//int inputPin = 12; //default of SPI.h (74HC165)

//shift register positions of the different colors
#define RGB_RED    0
#define RGB_GREEN  1
#define RGB_BLUE   2
#define NUMBER_OF_COLORS 3

#define BAM_STAGES           4
#define BAM_INIT_COUNT 0b10000 //start offset for the initial BAM timing; shifted in each following BAM stage by one
volatile uint8_t rgb[BAM_STAGES][COLS][NUMBER_OF_COLORS] = { 0 }; //storage for precalculated BAM output

//stores the input of the shift registers; get only changed when the input state has changed
#define BUTTON_INPUT_DEBOUNCE_CONSTANT 2
volatile uint8_t lastButtonInput[COLS] = { 0 }; //XXX currently only rows <= 8 are supported!
volatile uint8_t buttonInputCounter[COLS] = { 0 }; //used for debouncing
volatile uint8_t buttonInputChanged[COLS] = { 0 }; //stores if the button has changed, since the last iteration

//circular buffer for the button press events
#define BUTTON_EVENT_BUFFER_SIZE 10
RingBuffer<ButtonEvent> *buttonEventBuffer;

//sets the color of the desired button
//red, green, and blue between 0-15
//row and col from 0 to MAX-1
void setColor(uint8_t row, uint8_t col, uint8_t red, uint8_t green, uint8_t blue)  {
    //range checks
    if(row >= ROWS)
        return;
    if(col >= COLS)
        return;

    //we precalculate the register values inside the different stages
    //to save time inside the time-critical ISR
    for(uint8_t i = 0; i < 4; i++)  { //i is BAM position
        uint8_t bamMask = (1 << i);
        uint8_t rowMask = (1 << row);
        //unset all and reset if needed
        rgb[i][col][RGB_RED] &= ~rowMask;
        rgb[i][col][RGB_BLUE] &= ~rowMask;
        rgb[i][col][RGB_GREEN] &= ~rowMask;
        if(red & bamMask)
            rgb[i][col][RGB_RED] |= rowMask;
        if(green & bamMask)
            rgb[i][col][RGB_GREEN] |= rowMask;
        if(blue & bamMask)
            rgb[i][col][RGB_BLUE] |= rowMask;
    }
}

// helper that allows to use a color index from colors.h
// to set a button color
void setColor(uint8_t row, uint8_t col, uint8_t color)  {
    setColor(row, col, colors[color][0], colors[color][1], colors[color][2]);
}

// MIOS core requests a color change
void receiveEvent(int bytes) {
    if(bytes == 4)  {
        ChangeColorEvent changeColorEvent;
        changeColorEvent.row = Wire.read();
        changeColorEvent.column = Wire.read();
        changeColorEvent.isOn = Wire.read();
        changeColorEvent.color = Wire.read();    // read one character from the I2C

        if(changeColorEvent.isOn)
            setColor(changeColorEvent.row, changeColorEvent.column, changeColorEvent.color);
        else
            setColor(changeColorEvent.row, changeColorEvent.column, 0, 0, 0);
    }
}

// MIOS core asks if a button was pressed
void requestEvent()  {
    ButtonEvent btEvent;
    if(buttonEventBuffer->read(&btEvent))  { //returns false when there is no new button event
        Wire.write((uint8_t *) &btEvent, sizeof(btEvent));
    }
    else  {
        //inform the master that there was no button press
        Wire.write(0xFF);
        Wire.write(0xFF);
        Wire.write(0xFF);
    }
}

void setup()  {
    cli();//stop interrupts

    Serial.begin(115200); // debug output

    pinMode(latchPin, OUTPUT);
    SPI.begin();
    SPI.setBitOrder(MSBFIRST);

    // Setup timer1
    TCCR1A = 0;// set entire TCCR1A register to 0
    TCCR1B = 0;// same for TCCR1B
    TCNT1  = 0;//initialize counter value to 0
    OCR1A = BAM_INIT_COUNT << (BAM_STAGES - 2);
    // turn on CTC mode
    TCCR1B |= (1 << WGM12);
    // prescaler: 256
    TCCR1B |= (1 << CS11) | (1 << CS10);
    // enable timer compare interrupt
    TIMSK1 |= (1 << OCIE1A);

    // Start the I2C Bus as Slave on address 8
    Wire.begin(0x08);
    Wire.setClock(100000); //use fast mode
    // Function triggered when the master sends data (RGB lighting commands)
    Wire.onReceive(receiveEvent);
    // Function triggered when the master request data (checks frequently for button events)
    Wire.onRequest(requestEvent);

    //buffer for the button presses (only one is fetched each time via I2C)
    buttonEventBuffer = new RingBuffer<ButtonEvent>(BUTTON_EVENT_BUFFER_SIZE);

    Serial.println("Started");

    sei();//allow interrupts
}

void loop() {
    //initialize the button matrix with some nice colors
    for (uint8_t i = 0; i < 60; i++)  {
        setColor(i / COLS, i % COLS, i + 60);
    }

    while(1)  {}
}

//volatile, since used inside the ISR
volatile uint8_t column = COLS - 1;
volatile uint8_t bamCount = BAM_STAGES - 1;

ISR(TIMER1_COMPA_vect)  {
    cli();
    if(OCR1A >= BAM_INIT_COUNT << (BAM_STAGES - 1))
        OCR1A = BAM_INIT_COUNT;
    else
        OCR1A = OCR1A << 1; //wait longer next time

    if(bamCount >= BAM_STAGES - 1)
        column = (column + 1) % COLS;
    bamCount = (bamCount + 1) % BAM_STAGES;

    uint16_t tmpCol = (1 << column);

    uint8_t dataIn; //storage for the input buttons

    //update the column
    digitalWrite(latchPin, 0);
    digitalWrite(latchPin, 1); //capture data on the 165 input
    //write out to 595, while reading in from the 165 with the old pin mapping s.t. we read the last last column not the current one!
    dataIn = SPI.transfer(~rgb[bamCount][column][RGB_RED]);
    SPI.transfer(~rgb[bamCount][column][RGB_BLUE]);
    SPI.transfer(~rgb[bamCount][column][RGB_GREEN]);
    SPI.transfer((uint8_t) (tmpCol >> 8));
    SPI.transfer((uint8_t) tmpCol);

    digitalWrite(latchPin, 0);
    digitalWrite(latchPin, 1); //move data out of the 595s


    //store the button inputs inside the input array, but only when it has changed
    //also change the hasInputChanged flag to true s.t. we can inform the master MCU via I2C
    if(bamCount == 0)  { //detect button input changes only at the first bam stage
        if(dataIn != lastButtonInput[column])  { //changes in contrast to the last button input have changed
            //we can debounce the complete column at once, since multiple changes inside a column are not time critical in our scenario,
            //because a column represents a channel in ableton live in which only one clip can be triggered at the same time
            uint8_t changes = dataIn ^ lastButtonInput[column]; //changed 1; not changed 0
            if(changes != buttonInputChanged[column])  {
                buttonInputCounter[column] = 0;
                buttonInputChanged[column] = changes;
            }
            else  { //changes to the last button input are still the same
                if(buttonInputCounter[column] < BUTTON_INPUT_DEBOUNCE_CONSTANT)  { //do not increase counter when change already send
                    if(++buttonInputCounter[column] >= BUTTON_INPUT_DEBOUNCE_CONSTANT)  {
                        //input seems to be stable, so add the button event to the ring buffer
                        for(uint8_t row = 0; row < ROWS; row++)  {
                            if(buttonInputChanged[column] & (1 << row))  { //check if button in this column and row has changed
                                //used to store the button event inside the buffer
                                ButtonEvent btEvent;
                                //column for the input represents the last column, since we read with the last set input pos before updating it to the current one
                                btEvent.column = (column + COLS - 1) % COLS;
                                btEvent.row = row;
                                //determine if the button has been pressed or depressed
                                if(lastButtonInput[column] & (1 << row))
                                    btEvent.isPressed = 0;
                                else
                                    btEvent.isPressed = 1;
                                if(!buttonEventBuffer->write(&btEvent))  {
                                    while(1)  {
                                        Serial.println("ERROR: circular buffer reading is to slow!!!");
                                        //TODO all buttons red?
                                    }
                                }
                            }
                        }
                        lastButtonInput[column] = dataIn;
                    }
                }
            }
        }
        else  {
            buttonInputCounter[column] = 0;
        }
    }
    sei();
}

