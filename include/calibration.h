#ifndef CALIBRATION_H
#define CALIBRATION_H

struct AxisCalibration {
    int16_t min_val;
    int16_t center_val;
    int16_t max_val;
};

struct CalibrationData {
    uint32_t magic;
    AxisCalibration axes[4]; // 0: LX, 1: LY, 2: RX, 3: RY
    uint8_t deadzone_percent;
    uint8_t gamepad_mode;    // 0: MODE_PC, 1: MODE_ANDROID, 2: MODE_DS4
    uint8_t reserved[2];
};

extern CalibrationData g_calibration;

void calibration_init();
void calibration_save();
void run_calibration_mode(Adafruit_NeoPixel &pixels);
int16_t read_adc_ema(uint8_t pin, uint8_t axis_index);
int16_t process_axis(int16_t raw_adc, const AxisCalibration &cal, uint8_t deadzone_percent);
void process_stick_radial(int16_t raw_x, int16_t raw_y, const AxisCalibration &cal_x, const AxisCalibration &cal_y, uint8_t deadzone_percent, int16_t &out_x, int16_t &out_y);
void map_square_to_circle(int16_t &x, int16_t &y);

#endif // CALIBRATION_H
