#ifndef CONFIG_H
#define CONFIG_H

// ==========================================
// Hardware Pin Mappings (RP2040-Zero)
// ==========================================

// WS2812 RGB LED (RP2040-Zero onboard NeoPixel)
#define PIN_RGB_LED         16
#define NUM_LEDS            1

// Secondary Rumble Motor PWM Output
#define PIN_RUMBLE_PWM      18

// Analog Stick ADC Pins
#define PIN_STICK_LX        26  // ADC0
#define PIN_STICK_LY        27  // ADC1
#define PIN_STICK_RX        29  // ADC3
#define PIN_STICK_RY        28  // ADC2

#define INVERT_DIRECTION_LX     true
#define INVERT_DIRECTION_LY     false
#define INVERT_DIRECTION_RX     true
#define INVERT_DIRECTION_RY     false

// Total Buttons
#define NUM_BUTTONS         17

// Button Pin Definitions (Active LOW with INPUT_PULLUP)
// Order corresponds to HID Button 1..17
#define PIN_BTN_A           8   //0   // Button 1: A / Cross
#define PIN_BTN_B           7   //1   // Button 2: B / Circle
#define PIN_BTN_X           9   //2   // Button 3: X / Square
#define PIN_BTN_Y           6   //3   // Button 4: Y / Triangle

#define PIN_BTN_LB          2   //4   // Button 5: LB / L1
#define PIN_BTN_RB          3   //5   // Button 6: RB / R1

#define PIN_BTN_LT          0   //6   // Button 7: LT / L2 (Digital Trigger)
#define PIN_BTN_RT          1   //7   // Button 8: RT / R2 (Digital Trigger)

#define PIN_BTN_SELECT      15  //8   // Button 9: Select / Back / Share
#define PIN_BTN_START       5   //9   // Button 10: Start / Menu / Options

#define PIN_BTN_L3          19  //10  // Button 11: L3 (Left Stick Press)
#define PIN_BTN_R3          4   //11  // Button 12: R3 (Right Stick Press)

#define PIN_BTN_CENTRAL     10  //12  // Button 13: Central / Home (Xbox/Sony Key) - Holds Boot Calibration

#define PIN_BTN_DPAD_UP     14  //13  // Button 14: D-Pad Up
#define PIN_BTN_DPAD_DOWN   12  //14  // Button 15: D-Pad Down
#define PIN_BTN_DPAD_LEFT   13  //15  // Button 16: D-Pad Left
#define PIN_BTN_DPAD_RIGHT  11  //16  // Button 17: D-Pad Right

// Index of Central Key for Boot Calibration Check
#define INDEX_BTN_CENTRAL   12

// Array of button pins for looping initialization & reading
const uint8_t BUTTON_PINS[NUM_BUTTONS] = {
    PIN_BTN_A,          // 0
    PIN_BTN_B,          // 1
    PIN_BTN_X,          // 2
    PIN_BTN_Y,          // 3
    PIN_BTN_LB,         // 4
    PIN_BTN_RB,         // 5
    PIN_BTN_LT,         // 6
    PIN_BTN_RT,         // 7
    PIN_BTN_SELECT,     // 8
    PIN_BTN_START,      // 9
    PIN_BTN_L3,         // 10
    PIN_BTN_R3,         // 11
    PIN_BTN_CENTRAL,    // 12 (Central Key)
    PIN_BTN_DPAD_UP,    // 13
    PIN_BTN_DPAD_DOWN,  // 14
    PIN_BTN_DPAD_LEFT,  // 15
    PIN_BTN_DPAD_RIGHT  // 16
};

// ==========================================
// Gamepad Processing & Tuning Parameters
// ==========================================

// Software Debounce time in milliseconds
#define DEBOUNCE_DELAY_MS   15

// Analog Stick Deadzone percentage (0..100)
#define DEFAULT_DEADZONE_PERCENT 8

// Exponential Moving Average (EMA) alpha factor for ADC noise filtering (0.0..1.0)
#define EMA_ALPHA           0.25f

// Trigger smooth transition time in milliseconds (full stroke)
#define TRIGGER_RAMP_MS     90

// USB Device Descriptors
#define GAMEPAD_USBD_VID            0x2E8A
#define GAMEPAD_USBD_PID            0x0014
#define GAMEPAD_USBD_MANUFACTURER   "RP2040"
#define GAMEPAD_USBD_PRODUCT        "Gamepad rp2040"

// ADC Resolution (RP2040 uses 12-bit ADC: 0..4095)
#define ADC_MIN_VALUE       18
#define ADC_MAX_VALUE       4095
#define ADC_DEFAULT_CENTER  2056

// HID Axis Output Range (Standard 16-bit signed: -32767 .. 32767)
#define AXIS_MIN_VALUE      -32767
#define AXIS_MAX_VALUE      32767

// EEPROM Storage Settings
#define EEPROM_MAGIC        0x47504144 // "GPAD"
#define EEPROM_START_ADDR   0

#endif // CONFIG_H
