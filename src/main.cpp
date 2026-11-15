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

static void get_mode_color(GamepadMode mode, uint8_t &r, uint8_t &g, uint8_t &b) {
    switch (mode) {
        case MODE_ANDROID:
            r = COLOR_ANDROID_R;
            g = COLOR_ANDROID_G;
            b = COLOR_ANDROID_B;
            break;
        case MODE_DS4:
            r = COLOR_DS4_R;
            g = COLOR_DS4_G;
            b = COLOR_DS4_B;
            break;
        case MODE_PC:
        default:
            r = COLOR_PC_R;
            g = COLOR_PC_G;
            b = COLOR_PC_B;
            break;
    }
}

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
    pixels.setBrightness(255); // Full resolution, colors set directly
    pixels.clear();
    pixels.show();

    // 5. Initialize EEPROM & Calibration Data (Loads saved mode)
    calibration_init();

    // Small delay to allow pin pull-ups to stabilize before reading boot buttons
    delay(50);

    // 6. Check Boot Mode Selection Keys:
    //    - Hold Central Key -> Boot Calibration Mode
    //    - Hold Button X    -> Switch to MODE_PC
    //    - Hold Button B    -> Switch to MODE_ANDROID
    //    - Hold Button Y    -> Switch to MODE_DS4
    bool mode_changed = false;

    if (digitalRead(BUTTON_PINS[INDEX_BTN_CENTRAL]) == LOW) {
        run_calibration_mode(pixels);
    } else if (digitalRead(BUTTON_PINS[2]) == LOW) { // Button X held
        if (g_calibration.gamepad_mode != MODE_PC) {
            g_calibration.gamepad_mode = MODE_PC;
            mode_changed = true;
        }
    } else if (digitalRead(BUTTON_PINS[1]) == LOW) { // Button B held
        if (g_calibration.gamepad_mode != MODE_ANDROID) {
            g_calibration.gamepad_mode = MODE_ANDROID;
            mode_changed = true;
        }
    } else if (digitalRead(BUTTON_PINS[3]) == LOW) { // Button Y held
        if (g_calibration.gamepad_mode != MODE_DS4) {
            g_calibration.gamepad_mode = MODE_DS4;
            mode_changed = true;
        }
    }

    if (mode_changed) {
        calibration_save();
    }

    // 7. Initialize TinyUSB with the chosen active gamepad profile
    gamepad_hid_init((GamepadMode)g_calibration.gamepad_mode);

    // 8. Display active mode color on WS2812 LED
    uint8_t mr, mg, mb;
    get_mode_color((GamepadMode)g_calibration.gamepad_mode, mr, mg, mb);
    pixels.setPixelColor(0, pixels.Color(mr, mg, mb));
    pixels.show();
}

void loop() {
    unsigned long now = millis();

    // -------------------------------------------------------------
    // 1. Debounce and Read All 17 Buttons
    // -------------------------------------------------------------
    uint32_t button_mask = 0;

    for (int i = 0; i < NUM_BUTTONS; i++) {
        bool raw_pressed = (digitalRead(BUTTON_PINS[i]) == LOW);

        // Eager Debounce: Immediate trigger + Lockout period for noise suppression
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
    //    Priority:
    //    - When Rumble is active (left or right motor > 0):
    //        Left motor  = Red intensity   (target_r = g_rumble_left)
    //        Right motor = Blue intensity  (target_b = g_rumble_right)
    //        Both motors = Purple          (target_r = g_rumble_left, target_b = g_rumble_right)
    //    - When Rumble is NOT active (idle):
    //        Displays active mode color (at brightness 15 out of 255)
    // -------------------------------------------------------------
    static uint8_t last_r = 255;
    static uint8_t last_g = 255;
    static uint8_t last_b = 255;
    static uint8_t last_pwm = 255;

    uint8_t target_r = 0, target_g = 0, target_b = 0;

    if (g_rumble_left > 0 || g_rumble_right > 0) {
        // Active rumble: Left = Red, Right = Blue, Both = Purple
        target_r = g_rumble_left;
        target_g = 0;
        target_b = g_rumble_right;
    } else {
        // Idle: Base mode color at low brightness (15 out of 255)
        get_mode_color(gamepad_hid_get_mode(), target_r, target_g, target_b);
    }

    uint8_t target_pwm = (g_rumble_left > g_rumble_right) ? g_rumble_left : g_rumble_right;

    if (target_pwm != last_pwm) {
        analogWrite(PIN_RUMBLE_PWM, target_pwm);
        last_pwm = target_pwm;
    }

    if (target_r != last_r || target_g != last_g || target_b != last_b) {
        last_r = target_r;
        last_g = target_g;
        last_b = target_b;

        pixels.setPixelColor(0, pixels.Color(target_r, target_g, target_b));
        pixels.show();
    }

    delay(1);
}
