#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include "config.h"
#include "calibration.h"
#include "gamepad_hid.h"

// NeoPixel WS2812 RGB LED on GP16
static Adafruit_NeoPixel pixels(NUM_LEDS, PIN_RGB_LED, NEO_GRB + NEO_KHZ800);

// Eager Debouncing variables (Immediate trigger + Lockout)
static bool g_button_state[NUM_BUTTONS] = {false};
static unsigned long g_last_debounce_time[NUM_BUTTONS] = {0};

void setup() {
    // 1. Initialize Button Pins (Active LOW with Internal Pull-Ups)
    for (int i = 0; i < NUM_BUTTONS; i++) {
        pinMode(BUTTON_PINS[i], INPUT_PULLUP);
        g_button_state[i] = false;
        g_last_debounce_time[i] = 0;
    }

    // 2. Initialize Secondary Rumble PWM Output on GP18
    pinMode(PIN_RUMBLE_PWM, OUTPUT);
    analogWrite(PIN_RUMBLE_PWM, 0);

    // 3. Initialize ADC Resolution (12-bit: 0..4095)
    analogReadResolution(12);

    // 4. Initialize WS2812 RGB LED
    pixels.begin();
    pixels.setBrightness(128);
    pixels.clear();
    pixels.show();

    // 5. Initialize EEPROM & Calibration Data
    calibration_init();

    // 6. Initialize TinyUSB HID Gamepad Stack (Must be before calibration to keep USB active)
    gamepad_hid_init();

    // 7. Check if Central Key (Xbox/Sony Key) is held down at startup for Calibration Mode
    // Small delay to allow pin pull-up stabilization
    delay(50);
    if (digitalRead(BUTTON_PINS[INDEX_BTN_CENTRAL]) == LOW) {
        // Central button is pressed -> Enter Calibration Mode!
        run_calibration_mode(pixels);
    }
}

void loop() {
    unsigned long now = millis();

    // -------------------------------------------------------------
    // 1. Debounce and Read All 17 Buttons
    // -------------------------------------------------------------
    uint32_t button_mask = 0;

    for (int i = 0; i < NUM_BUTTONS; i++) {
        bool raw_pressed = (digitalRead(BUTTON_PINS[i]) == LOW);

        // Eager Debounce: Immediate state trigger + Lockout period for noise suppression
        if (raw_pressed != g_button_state[i] && (now - g_last_debounce_time[i] >= DEBOUNCE_DELAY_MS)) {
            g_button_state[i] = raw_pressed;
            g_last_debounce_time[i] = now;
        }

        if (g_button_state[i]) {
            button_mask |= (1UL << i);
        }
    }

    // -------------------------------------------------------------
    // 2. Read ADC with EMA Filtering and Process Radial Deadzone
    // -------------------------------------------------------------
    int16_t raw_lx = read_adc_ema(PIN_STICK_LX, 0);
    int16_t raw_ly = read_adc_ema(PIN_STICK_LY, 1);
    int16_t raw_rx = read_adc_ema(PIN_STICK_RX, 2);
    int16_t raw_ry = read_adc_ema(PIN_STICK_RY, 3);

    int16_t lx = 0, ly = 0, rx = 0, ry = 0;
    process_stick_radial(raw_lx, raw_ly, g_calibration.axes[0], g_calibration.axes[1], g_calibration.deadzone_percent, lx, ly);
    process_stick_radial(raw_rx, raw_ry, g_calibration.axes[2], g_calibration.axes[3], g_calibration.deadzone_percent, rx, ry);

#if INVERT_DIRECTION_LX
    lx = -lx;
#endif

#if INVERT_DIRECTION_LY
    ly = -ly;
#endif

#if INVERT_DIRECTION_RX
    rx = -rx;
#endif

#if INVERT_DIRECTION_RY
    ry = -ry;
#endif

    // -------------------------------------------------------------
    // 3. Send HID Input Report over USB
    // -------------------------------------------------------------
    gamepad_hid_send_report(button_mask, lx, ly, rx, ry);

    // -------------------------------------------------------------
    // 4. Feedback Outputs: WS2812 RGB LED & GP18 PWM Rumble Motor
    //    Red   = Host Status / LED Intensity
    //    Blue  = Host Rumble Intensity
    //    Purple = Both Active
    // -------------------------------------------------------------
    static uint8_t last_led = 255;
    static uint8_t last_rumble = 255;

    if (g_led_intensity != last_led || g_rumble_intensity != last_rumble) {
        if (g_rumble_intensity != last_rumble) {
            analogWrite(PIN_RUMBLE_PWM, g_rumble_intensity);
        }
        last_led = g_led_intensity;
        last_rumble = g_rumble_intensity;

        pixels.setPixelColor(0, pixels.Color(g_led_intensity, 0, g_rumble_intensity));
        pixels.show();
    }

    delay(4);
}
