#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include <EEPROM.h>

#include "calibration.h"
#include "config.h"

CalibrationData g_calibration;

void calibration_init() {
    EEPROM.begin(sizeof(CalibrationData));
    EEPROM.get(EEPROM_START_ADDR, g_calibration);

    bool invalid = (g_calibration.magic != EEPROM_MAGIC);
    if (!invalid) {
        for (int i = 0; i < 4; i++) {
            if (g_calibration.axes[i].min_val >= g_calibration.axes[i].max_val ||
                g_calibration.axes[i].center_val <= g_calibration.axes[i].min_val ||
                g_calibration.axes[i].center_val >= g_calibration.axes[i].max_val ||
                g_calibration.axes[i].center_val < 500 || g_calibration.axes[i].center_val > 3500) {
                invalid = true;
                break;
            }
        }
    }

    // If magic or calibration values are invalid, load default calibration values for full ADC range 18..4095
    if (invalid) {
        g_calibration.magic = EEPROM_MAGIC;
        g_calibration.deadzone_percent = DEFAULT_DEADZONE_PERCENT;

        for (int i = 0; i < 4; i++) {
            g_calibration.axes[i].center_val = ADC_DEFAULT_CENTER; // 2056
            g_calibration.axes[i].min_val = ADC_MIN_VALUE;         // Full physical min ADC (18)
            g_calibration.axes[i].max_val = ADC_MAX_VALUE;         // Full physical max ADC (4095)
        }
        calibration_save();
    }
}

void calibration_save() {
    EEPROM.put(EEPROM_START_ADDR, g_calibration);
    EEPROM.commit();
}

void run_calibration_mode(Adafruit_NeoPixel &pixels) {
    // -------------------------------------------------------------
    // Step 1: Center Position Measurement (Sticks Untouched)
    // -------------------------------------------------------------
    pixels.setPixelColor(0, pixels.Color(255, 255, 0)); // Yellow LED: Center capture
    pixels.show();
    delay(1000);

    int32_t center_sum[4] = {0, 0, 0, 0};
    const int SAMPLES = 200;

    for (int s = 0; s < SAMPLES; s++) {
        center_sum[0] += analogRead(PIN_STICK_LX);
        center_sum[1] += analogRead(PIN_STICK_LY);
        center_sum[2] += analogRead(PIN_STICK_RX);
        center_sum[3] += analogRead(PIN_STICK_RY);

        // Flash Yellow fast
        if ((s / 10) % 2 == 0) {
            pixels.setPixelColor(0, pixels.Color(255, 255, 0));
        } else {
            pixels.setPixelColor(0, pixels.Color(0, 0, 0));
        }
        pixels.show();
        delay(15);
    }

    for (int i = 0; i < 4; i++) {
        g_calibration.axes[i].center_val = center_sum[i] / SAMPLES;
        g_calibration.axes[i].min_val = g_calibration.axes[i].center_val - 100;
        g_calibration.axes[i].max_val = g_calibration.axes[i].center_val + 100;
    }

    // -------------------------------------------------------------
    // Step 2: Full Motion Sweep (Rotate Sticks to Extremes)
    // -------------------------------------------------------------
    pixels.setPixelColor(0, pixels.Color(255, 0, 255)); // Magenta LED: Range sweep
    pixels.show();
    delay(1000);

    unsigned long startTime = millis();
    // Calibration sweep lasts 6 seconds
    while (millis() - startTime < 6000) {
        int raw[4];
        raw[0] = analogRead(PIN_STICK_LX);
        raw[1] = analogRead(PIN_STICK_LY);
        raw[2] = analogRead(PIN_STICK_RX);
        raw[3] = analogRead(PIN_STICK_RY);

        for (int i = 0; i < 4; i++) {
            if (raw[i] < ADC_MIN_VALUE) raw[i] = ADC_MIN_VALUE;
            if (raw[i] > ADC_MAX_VALUE) raw[i] = ADC_MAX_VALUE;

            if (raw[i] < g_calibration.axes[i].min_val) {
                g_calibration.axes[i].min_val = raw[i];
            }
            if (raw[i] > g_calibration.axes[i].max_val) {
                g_calibration.axes[i].max_val = raw[i];
            }
        }

        // Pulse Magenta LED
        uint8_t pulse = (millis() / 5) % 255;
        pixels.setPixelColor(0, pixels.Color(pulse, 0, pulse));
        pixels.show();
        delay(10);
    }

    // Sanity checks on range: default to full physical range (18..4095) if stick wasn't swept
    for (int i = 0; i < 4; i++) {
        if (g_calibration.axes[i].center_val - g_calibration.axes[i].min_val < 800) {
            g_calibration.axes[i].min_val = ADC_MIN_VALUE;
        }
        if (g_calibration.axes[i].max_val - g_calibration.axes[i].center_val < 800) {
            g_calibration.axes[i].max_val = ADC_MAX_VALUE;
        }
    }

    // Save calibration to EEPROM
    calibration_save();

    // -------------------------------------------------------------
    // Step 3: Blink Green 3 times to signal completion
    // -------------------------------------------------------------
    for (int b = 0; b < 3; b++) {
        pixels.setPixelColor(0, pixels.Color(0, 255, 0));
        pixels.show();
        delay(250);
        pixels.setPixelColor(0, pixels.Color(0, 0, 0));
        pixels.show();
        delay(250);
    }
}

static float s_ema_adc[4] = {-1.0f, -1.0f, -1.0f, -1.0f};

int16_t read_adc_ema(uint8_t pin, uint8_t axis_index) {
    if (axis_index >= 4) return analogRead(pin);

    int raw = analogRead(pin);
    if (raw < ADC_MIN_VALUE) raw = ADC_MIN_VALUE;
    if (raw > ADC_MAX_VALUE) raw = ADC_MAX_VALUE;

    if (s_ema_adc[axis_index] < 0.0f) {
        s_ema_adc[axis_index] = (float)raw;
    } else {
        s_ema_adc[axis_index] = s_ema_adc[axis_index] * (1.0f - EMA_ALPHA) + (float)raw * EMA_ALPHA;
    }
    return (int16_t)(s_ema_adc[axis_index] + 0.5f);
}

static float normalize_axis_raw(int16_t raw_adc, const AxisCalibration &cal) {
    float norm = 0.0f;
    if (raw_adc < cal.center_val) {
        int16_t span = cal.center_val - cal.min_val;
        if (span < 100) span = 100;
        norm = (float)(raw_adc - cal.center_val) / (float)span;
    } else {
        int16_t span = cal.max_val - cal.center_val;
        if (span < 100) span = 100;
        norm = (float)(raw_adc - cal.center_val) / (float)span;
    }
    if (norm < -1.0f) norm = -1.0f;
    if (norm > 1.0f) norm = 1.0f;
    return norm;
}

void process_stick_radial(int16_t raw_x, int16_t raw_y, const AxisCalibration &cal_x, const AxisCalibration &cal_y, uint8_t deadzone_percent, int16_t &out_x, int16_t &out_y) {
    // 1. Normalize raw ADC to square domain [-1.0, 1.0]
    float u = normalize_axis_raw(raw_x, cal_x);
    float v = normalize_axis_raw(raw_y, cal_y);

    // 2. Map Square domain [-1.0, 1.0] to Circular domain [-1.0, 1.0] (Elliptical Grid Mapping)
    float circle_x = u * sqrtf(1.0f - (v * v * 0.5f));
    float circle_y = v * sqrtf(1.0f - (u * u * 0.5f));

    // 3. Radial Deadzone in Circular Domain
    float magnitude = sqrtf(circle_x * circle_x + circle_y * circle_y);
    float deadzone = (float)deadzone_percent / 100.0f;

    if (magnitude <= deadzone || magnitude < 0.0001f) {
        out_x = 0;
        out_y = 0;
        return;
    }

    float range = 1.0f - deadzone;
    if (range <= 0.0001f) {
        out_x = 0;
        out_y = 0;
        return;
    }

    float scaled_magnitude = (magnitude - deadzone) / range;
    if (scaled_magnitude > 1.0f) scaled_magnitude = 1.0f;

    float norm_u = (circle_x / magnitude) * scaled_magnitude;
    float norm_v = (circle_y / magnitude) * scaled_magnitude;

    if (norm_u < -1.0f) norm_u = -1.0f;
    if (norm_u > 1.0f) norm_u = 1.0f;
    if (norm_v < -1.0f) norm_v = -1.0f;
    if (norm_v > 1.0f) norm_v = 1.0f;

    out_x = (int16_t)(norm_u * (float)AXIS_MAX_VALUE);
    out_y = (int16_t)(norm_v * (float)AXIS_MAX_VALUE);
}

int16_t process_axis(int16_t raw_adc, const AxisCalibration &cal, uint8_t deadzone_percent) {
    float norm = normalize_axis_raw(raw_adc, cal);

    int32_t val = (int32_t)(norm * (float)AXIS_MAX_VALUE);
    int32_t deadzone_cutoff = ((int32_t)AXIS_MAX_VALUE * deadzone_percent) / 100L;

    if (abs(val) <= deadzone_cutoff) {
        return 0;
    }

    int32_t range = (int32_t)AXIS_MAX_VALUE - deadzone_cutoff;
    if (range <= 0) {
        return 0;
    }

    int32_t scaled = 0;
    if (val > 0) {
        scaled = ((val - deadzone_cutoff) * (int32_t)AXIS_MAX_VALUE) / range;
    } else {
        scaled = ((val + deadzone_cutoff) * (int32_t)AXIS_MAX_VALUE) / range;
    }

    if (scaled < AXIS_MIN_VALUE) scaled = AXIS_MIN_VALUE;
    if (scaled > AXIS_MAX_VALUE) scaled = AXIS_MAX_VALUE;

    return (int16_t)scaled;
}

// Elliptical Grid Mapping: Maps square stick input space [-1023, 1023] to a circular domain
void map_square_to_circle(int16_t &x, int16_t &y) {
    if (x == 0 && y == 0) return;

    float u = (float)x / (float)AXIS_MAX_VALUE;
    float v = (float)y / (float)AXIS_MAX_VALUE;

    float circle_x = u * sqrtf(1.0f - (v * v * 0.5f));
    float circle_y = v * sqrtf(1.0f - (u * u * 0.5f));

    if (circle_x > 1.0f) circle_x = 1.0f;
    if (circle_x < -1.0f) circle_x = -1.0f;
    if (circle_y > 1.0f) circle_y = 1.0f;
    if (circle_y < -1.0f) circle_y = -1.0f;

    x = (int16_t)(circle_x * (float)AXIS_MAX_VALUE);
    y = (int16_t)(circle_y * (float)AXIS_MAX_VALUE);
}
