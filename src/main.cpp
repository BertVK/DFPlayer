/*
PINOUT
 5 : internal LED
17 : Serial2 Tx
16 : Serial2 Rx
15 : interrupt line
21 : SDA
22 : SCL
23 : busy signal from DFPlayer
*/

#include <Arduino.h>
#include <DFRobotDFPlayerMini.h>
#include <Wire.h>
#include <MCP23017.h>
#include <CircularBuffer.h> //https://github.com/rlogiacco/CircularBuffer

// define pins
#define INT_PIN GPIO_NUM_15
#define SDA_PIN GPIO_NUM_21
#define SCL_PIN GPIO_NUM_22
#define BUSY_PIN GPIO_NUM_23

#define DEBUG true

DFRobotDFPlayerMini dfPlayer;
uint8_t volume;
bool mcpAvailable[8] = {false, false, false, false, false, false, false, false};
MCP23017 mcp0 = MCP23017(0x20);
MCP23017 mcp1 = MCP23017(0x21);
MCP23017 mcp2 = MCP23017(0x22);
MCP23017 mcp3 = MCP23017(0x23);
MCP23017 mcp4 = MCP23017(0x24);
MCP23017 mcp5 = MCP23017(0x25);
MCP23017 mcp6 = MCP23017(0x26);
MCP23017 mcp7 = MCP23017(0x27);
bool interrupted = false;
CircularBuffer<byte, 100> playlist;
uint8_t lastButtonPressed;
unsigned long timer = 0;
const unsigned long timeout = 10000;

void userInput()
{
    interrupted = true;
}

void findMcpDevices()
{
    byte error, address;
    for (int i = 0; i < 8; i++)
    {
        address = 0x20 + i;
        Wire.beginTransmission(address);
        Wire.write((byte)0x0a);
        error = Wire.endTransmission();
        if (error == 0)
        {
            mcpAvailable[i] = true;
        }
        else
        {
            mcpAvailable[i] = false;
        }
    }
}

void initMcp(MCP23017 thisMcp)
{
    thisMcp.init();
    //Set both ports as input
    thisMcp.portMode(MCP23017Port::A, 0b11111111);
    thisMcp.portMode(MCP23017Port::B, 0b11111111);
    //set interrupt to trigger on either bank
    thisMcp.interruptMode(MCP23017InterruptMode::Or);
    //Set interrupt on falling edge (start of pressing button)
    thisMcp.interrupt(MCP23017Port::A, FALLING);
    thisMcp.interrupt(MCP23017Port::B, FALLING);
    //Set polarity
    thisMcp.writeRegister(MCP23017Register::IPOL_A, 0x00);
    thisMcp.writeRegister(MCP23017Register::IPOL_B, 0x00);
    //Init bank reghisters to 0
    thisMcp.writeRegister(MCP23017Register::GPIO_A, 0x00);
    thisMcp.writeRegister(MCP23017Register::GPIO_B, 0x00);
    //Clear interrupt, ready to rumble
    thisMcp.clearInterrupts();
}

void initMcpDevices()
{
    if (mcpAvailable[0])
        initMcp(mcp0);
    if (mcpAvailable[1])
        initMcp(mcp1);
    if (mcpAvailable[2])
        initMcp(mcp2);
    if (mcpAvailable[3])
        initMcp(mcp3);
    if (mcpAvailable[4])
        initMcp(mcp4);
    if (mcpAvailable[5])
        initMcp(mcp5);
    if (mcpAvailable[6])
        initMcp(mcp6);
    if (mcpAvailable[7])
        initMcp(mcp7);
}

void clearMcpInterupts()
{
    if (mcpAvailable[0])
        mcp0.clearInterrupts();
    if (mcpAvailable[1])
        mcp1.clearInterrupts();
    if (mcpAvailable[2])
        mcp2.clearInterrupts();
    if (mcpAvailable[3])
        mcp3.clearInterrupts();
    if (mcpAvailable[4])
        mcp4.clearInterrupts();
    if (mcpAvailable[5])
        mcp5.clearInterrupts();
    if (mcpAvailable[6])
        mcp6.clearInterrupts();
    if (mcpAvailable[7])
        mcp7.clearInterrupts();
}

//Check the buffer and queue the number of the button in the playlist
void queueButtons(uint8_t buffer, bool isBufferA, uint8_t chipNumber)
{
    uint8_t buttonNumber = 0;
    //detect the first bit
    for (int position = 0; position < 8; position++)
    {
        if (1 == ((buffer >> position) & 1))
        {
            //Calculate the button number
            buttonNumber = (isBufferA) ? (chipNumber * 16) + (position + 1) : (chipNumber * 16) + 8 + (position + 1);
            if (DEBUG)
            {
                Serial.print("Detected button nr ");
                Serial.print(buttonNumber);
                Serial.print(" - Last button nr ");
                Serial.println(lastButtonPressed);
            }
            //We do not want twice the same button in the queue. It has to finish playing before we can play it again.
            if (lastButtonPressed != buttonNumber)
            {
                //Add the button to the queue
                if (DEBUG)
                {
                    Serial.print("Added button nr ");
                    Serial.println(buttonNumber);
                }
                playlist.push(buttonNumber);
            }
            lastButtonPressed = buttonNumber;
        }
    }
}

void getButtonsPressed()
{
    uint8_t a, b;
    uint8_t chipCounter = 0;
    for (int i = 0; i < 8; i++)
    {
        if (mcpAvailable[i])
        {
            if (i == 0)
                mcp0.interruptedBy(a, b);
            if (i == 1)
                mcp1.interruptedBy(a, b);
            if (i == 2)
                mcp2.interruptedBy(a, b);
            if (i == 3)
                mcp3.interruptedBy(a, b);
            if (i == 4)
                mcp4.interruptedBy(a, b);
            if (i == 5)
                mcp5.interruptedBy(a, b);
            if (i == 6)
                mcp6.interruptedBy(a, b);
            if (i == 7)
                mcp7.interruptedBy(a, b);
            //The interrupt of this mcp has been triggered, so get the number of the button
            if (a != 0)
                queueButtons(a, true, chipCounter);
            if (b != 0)
                queueButtons(a, false, chipCounter);
            chipCounter++;
        }
    }
}

bool isPlayerBusy()
{
    if (digitalRead(BUSY_PIN))
        return true;
    return false;
}

void playAudio()
{
    if (!playlist.isEmpty())
    {
        //There's a request on the queue, play it when the player is not playing
        if (isPlayerBusy())
        {
            uint8_t songNr = playlist.pop();
            dfPlayer.play(songNr);
            delay(100);
            if (DEBUG)
            {
                Serial.print("Playing song Nr ");
                Serial.println(songNr);
            }
            //keep track of when we started playing the last audio file.
            timer = millis();
        }
    }
    else
    {
        //When last audio is played, we can play the same one again
        lastButtonPressed = 0;
    }
}

void setup()
{
    timer = millis();
    volume = 10;
    //Set power parameters
    setCpuFrequencyMhz(80);

    if (DEBUG)
    {
        Serial.begin(115200);
    }
    //Init variables
    interrupted = false;
    lastButtonPressed = 0;
    pinMode(LED_BUILTIN, OUTPUT);
    pinMode(BUSY_PIN, INPUT);
    digitalWrite(BUILTIN_LED, HIGH);

    Serial2.begin(9600);
    Wire.begin(SDA_PIN, SCL_PIN, 100000L);
    findMcpDevices();
    initMcpDevices();
    attachInterrupt(digitalPinToInterrupt(INT_PIN), userInput, RISING);

    //Give the DFPlayer a little time to initialize
    delay(1000);
    if (!dfPlayer.begin(Serial2))
    {
        if (DEBUG)
        {
            Serial.println(F("Unable to begin"));
            Serial.println(F("1. Please rechek the connection"));
            Serial.println(F("2. Please insert the SD card"));
        }
        while (true)
        {
            digitalWrite(BUILTIN_LED, LOW);
        }
    }
    if (DEBUG)
        Serial.println(F("DFPlayer Mini is Online"));
    dfPlayer.volume(volume);

    if (DEBUG)
    {
        Serial.print("Volume set to ");
        Serial.println(volume);
    }

    if (DEBUG)
    {
        Serial.println("Setup done");
    }
}

void loop()
{
    if (!interrupted)
    {
        // just to be sure that arduino and mcp are in the "same state" regarding interrupts
        clearMcpInterupts();
    }
    else
    {
        digitalWrite(BUILTIN_LED, LOW);

        getButtonsPressed();
        //debouncing
        delay(200);

        digitalWrite(BUILTIN_LED, HIGH);
        interrupted = false;
    }
    playAudio();
}