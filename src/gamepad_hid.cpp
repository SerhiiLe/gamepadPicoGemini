#include <Arduino.h>
#include <Adafruit_TinyUSB.h>

#include "config.h"
#include "gamepad_hid.h"

// Feedback variables received from Host
volatile uint8_t g_rumble_left     = 0; // Heavy motor (0..255)
volatile uint8_t g_rumble_right    = 0; // Soft motor (0..255)
volatile uint8_t g_host_led_r      = 0;
volatile uint8_t g_host_led_g      = 0;
volatile uint8_t g_host_led_b      = 0;
volatile bool    g_host_led_active = false;

static GamepadMode s_active_mode = MODE_PC;
static Adafruit_USBD_HID usb_hid;

// Output report callback (Rumble & LED feedback from host)
static void hid_set_report_cb(uint8_t report_id, hid_report_type_t report_type, uint8_t const* buffer, uint16_t bufsize) {
    if (report_type != HID_REPORT_TYPE_OUTPUT) return;

    if (s_active_mode == MODE_DS4) {
        // Sony DualShock 4 Output Report (Report ID 0x05 on USB)
        // buffer[0] may be Report ID 0x05 or payload starts directly depending on stack
        uint8_t offset = (buffer[0] == 0x05) ? 1 : 0;
        if (bufsize >= offset + 8) {
            uint8_t flags = buffer[offset + 0];
            // Motor right = soft (high-freq), motor left = heavy (low-freq)
            g_rumble_right = buffer[offset + 2];
            g_rumble_left  = buffer[offset + 3];

            // LED colors
            g_host_led_r = buffer[offset + 4];
            g_host_led_g = buffer[offset + 5];
            g_host_led_b = buffer[offset + 6];
            g_host_led_active = (g_host_led_r > 0 || g_host_led_g > 0 || g_host_led_b > 0);
        }
    } else {
        // Generic PC / Android Output Report (Byte 0: Rumble, Byte 1: LED)
        if (bufsize >= 1) {
            g_rumble_left  = buffer[0];
            g_rumble_right = buffer[0];
            if (bufsize >= 2) {
                g_host_led_r = buffer[1];
                g_host_led_g = buffer[1];
                g_host_led_b = buffer[1];
                g_host_led_active = (buffer[1] > 0);
            }
        }
    }
}

// Feature & Input GET_REPORT callback (Crucial for Sony DS4 handshake on Linux/Steam)
static uint16_t hid_get_report_cb(uint8_t report_id, hid_report_type_t report_type, uint8_t* buffer, uint16_t reqlen) {
    if (s_active_mode == MODE_DS4) {
        if (report_type == HID_REPORT_TYPE_FEATURE) {
            if (report_id == 0x12) {
                // Feature Report 0x12: DS4 MAC / Pairing Info (15 bytes payload)
                memset(buffer, 0, reqlen);
                // Fake DS4 Bluetooth MAC: 00:1A:7D:DA:71:12
                buffer[0] = 0x00; buffer[1] = 0x1A; buffer[2] = 0x7D;
                buffer[3] = 0xDA; buffer[4] = 0x71; buffer[5] = 0x12;
                // Constant bytes expected by Linux hid-playstation/hid-sony
                if (reqlen >= 9) {
                    buffer[6] = 0x08;
                    buffer[7] = 0x25;
                    buffer[8] = 0x00;
                }
                return (reqlen < 15) ? reqlen : 15;
            }
            if (report_id == 0xA3) {
                // Feature Report 0xA3: DS4 Firmware / Build Info (48 bytes payload)
                memset(buffer, 0, reqlen);
                if (reqlen >= 48) {
                    // hw_version at byte 34, fw_version at byte 40 (0x0100)
                    buffer[34] = 0x00; buffer[35] = 0x01;
                    buffer[40] = 0x00; buffer[41] = 0x01;
                }
                return (reqlen < 48) ? reqlen : 48;
            }
            if (report_id == 0x02) {
                // Feature Report 0x02: DS4 IMU Calibration Data (36 bytes payload)
                memset(buffer, 0, reqlen);
                if (reqlen >= 36) {
                    int16_t const calib_pos = 8192;
                    int16_t const calib_neg = -8192;
                    // buffer[0..5]: gyro biases (0)
                    // buffer[6..17]: gyro pitch/yaw/roll plus/minus
                    memcpy(&buffer[6],  &calib_pos, 2);
                    memcpy(&buffer[8],  &calib_neg, 2);
                    memcpy(&buffer[10], &calib_pos, 2);
                    memcpy(&buffer[12], &calib_neg, 2);
                    memcpy(&buffer[14], &calib_pos, 2);
                    memcpy(&buffer[16], &calib_neg, 2);
                    // buffer[18..21]: gyro speed plus/minus
                    memcpy(&buffer[18], &calib_pos, 2);
                    memcpy(&buffer[20], &calib_neg, 2);
                    // buffer[22..33]: accel x/y/z plus/minus
                    memcpy(&buffer[22], &calib_pos, 2);
                    memcpy(&buffer[24], &calib_neg, 2);
                    memcpy(&buffer[26], &calib_pos, 2);
                    memcpy(&buffer[28], &calib_neg, 2);
                    memcpy(&buffer[30], &calib_pos, 2);
                    memcpy(&buffer[32], &calib_neg, 2);
                }
                return (reqlen < 36) ? reqlen : 36;
            }
        } else if (report_type == HID_REPORT_TYPE_INPUT && report_id == 0x01) {
            memset(buffer, 0, reqlen);
            if (reqlen >= 8) {
                buffer[0] = 128; // x
                buffer[1] = 128; // y
                buffer[2] = 128; // z
                buffer[3] = 128; // rz
                buffer[4] = 0x08; // neutral dpad
            }
            return (reqlen < 63) ? reqlen : 63;
        }
    } else {
        if (report_type == HID_REPORT_TYPE_INPUT) {
            memset(buffer, 0, reqlen);
            return reqlen;
        }
    }
    return 0;
}

// Compute 8-Way Hat Switch for PC and Android (1=Up, 2=Up-Right, ..., 8=Up-Left, 0=Neutral)
static uint8_t compute_hat_standard(uint32_t button_mask) {
    bool up    = (button_mask & (1UL << 13)) != 0;
    bool down  = (button_mask & (1UL << 14)) != 0;
    bool left  = (button_mask & (1UL << 15)) != 0;
    bool right = (button_mask & (1UL << 16)) != 0;

    // SOCD Cleaning
    if (up && down)    { up = false; down = false; }
    if (left && right) { left = false; right = false; }

    if (up && right)   return 2; // Up-Right (45 deg)
    if (down && right) return 4; // Down-Right (135 deg)
    if (down && left)  return 6; // Down-Left (225 deg)
    if (up && left)    return 8; // Up-Left (315 deg)
    if (up)            return 1; // Up (0 deg)
    if (right)         return 3; // Right (90 deg)
    if (down)          return 5; // Down (180 deg)
    if (left)          return 7; // Left (270 deg)

    return 0; // Neutral
}

// Compute 4-bit Hat Switch for DualShock 4 (0=Up, 1=Up-Right, ..., 7=Up-Left, 8=Neutral)
static uint8_t compute_hat_ds4(uint32_t button_mask) {
    bool up    = (button_mask & (1UL << 13)) != 0;
    bool down  = (button_mask & (1UL << 14)) != 0;
    bool left  = (button_mask & (1UL << 15)) != 0;
    bool right = (button_mask & (1UL << 16)) != 0;

    // SOCD Cleaning
    if (up && down)    { up = false; down = false; }
    if (left && right) { left = false; right = false; }

    if (up && right)   return 1; // Up-Right
    if (right && down) return 3; // Down-Right
    if (down && left)  return 5; // Down-Left
    if (left && up)    return 7; // Up-Left
    if (up)            return 0; // Up
    if (right)         return 2; // Right
    if (down)          return 4; // Down
    if (left)          return 6; // Left

    return 8; // Neutral
}

void gamepad_hid_init(GamepadMode mode) {
    s_active_mode = mode;

    if (TinyUSBDevice.mounted() || TinyUSBDevice.isInitialized()) {
        TinyUSBDevice.detach();
        delay(100);
    }

    switch (mode) {
        case MODE_ANDROID:
            TinyUSBDevice.setID(USBD_VID_ANDROID, USBD_PID_ANDROID);
            TinyUSBDevice.setManufacturerDescriptor("RP2040");
            TinyUSBDevice.setProductDescriptor(USBD_PRODUCT_ANDROID);
            usb_hid.setReportDescriptor(desc_hid_report_android, sizeof(desc_hid_report_android));
            break;

        case MODE_DS4:
            TinyUSBDevice.setID(USBD_VID_DS4, USBD_PID_DS4);
            TinyUSBDevice.setManufacturerDescriptor(USBD_MANUFACTURER_DS4);
            TinyUSBDevice.setProductDescriptor(USBD_PRODUCT_DS4);
            usb_hid.setReportDescriptor(desc_hid_report_ds4, sizeof(desc_hid_report_ds4));
            break;

        case MODE_PC:
        default:
            s_active_mode = MODE_PC;
            TinyUSBDevice.setID(USBD_VID_PC, USBD_PID_PC);
            TinyUSBDevice.setManufacturerDescriptor("RP2040");
            TinyUSBDevice.setProductDescriptor(USBD_PRODUCT_PC);
            usb_hid.setReportDescriptor(desc_hid_report_pc, sizeof(desc_hid_report_pc));
            break;
    }

    usb_hid.setPollInterval(1); // 1000Hz (1ms)
    usb_hid.setReportCallback(hid_get_report_cb, hid_set_report_cb);
    usb_hid.begin();

    TinyUSBDevice.attach();
}

GamepadMode gamepad_hid_get_mode() {
    return s_active_mode;
}

// Convert signed 16-bit axis (-32767..32767) to unsigned 8-bit (0..255, center 128)
static inline uint8_t axis16_to_uint8(int16_t v) {
    int32_t val = (int32_t)v + 32767;
    int32_t mapped = (val * 255) / 65534;
    if (mapped < 0) mapped = 0;
    if (mapped > 255) mapped = 255;
    return (uint8_t)mapped;
}

bool gamepad_hid_send_report(uint32_t button_mask, int16_t lx, int16_t ly, int16_t rx, int16_t ry) {
    if (!usb_hid.ready()) {
        return false;
    }

    // Smooth trigger ramping (0 .. 255)
    static float s_lt_axis = 0.0f;
    static float s_rt_axis = 0.0f;
    static unsigned long s_last_trigger_time = 0;

    unsigned long now = millis();
    unsigned long dt = (s_last_trigger_time == 0) ? 1 : (now - s_last_trigger_time);
    s_last_trigger_time = now;
    if (dt > 50) dt = 50;

    float step = (255.0f / (float)TRIGGER_RAMP_MS) * (float)dt;

    bool lt_pressed = (button_mask & (1UL << 6)) != 0;
    bool rt_pressed = (button_mask & (1UL << 7)) != 0;

    if (lt_pressed) {
        s_lt_axis += step;
        if (s_lt_axis > 255.0f) s_lt_axis = 255.0f;
    } else {
        s_lt_axis -= step;
        if (s_lt_axis < 0.0f) s_lt_axis = 0.0f;
    }

    if (rt_pressed) {
        s_rt_axis += step;
        if (s_rt_axis > 255.0f) s_rt_axis = 255.0f;
    } else {
        s_rt_axis -= step;
        if (s_rt_axis < 0.0f) s_rt_axis = 0.0f;
    }

    uint8_t lt_val = (uint8_t)(s_lt_axis + 0.5f);
    uint8_t rt_val = (uint8_t)(s_rt_axis + 0.5f);

    // ==========================================
    // 1. DualShock 4 Mode Report (Report ID 1)
    // ==========================================
    if (s_active_mode == MODE_DS4) {
        GamepadReportDS4_t rep;
        memset(&rep, 0, sizeof(rep));

        rep.x  = axis16_to_uint8(lx);
        rep.y  = axis16_to_uint8(ly);
        rep.z  = axis16_to_uint8(rx);
        rep.rz = axis16_to_uint8(ry);

        // Lower 4 bits: D-Pad
        rep.buttons_dpad = (compute_hat_ds4(button_mask) & 0x0F);

        // Upper 4 bits: Square(X), Cross(A), Circle(B), Triangle(Y)
        if (button_mask & (1UL << 2)) rep.buttons_dpad |= (1 << 4); // Square (X)
        if (button_mask & (1UL << 0)) rep.buttons_dpad |= (1 << 5); // Cross (A)
        if (button_mask & (1UL << 1)) rep.buttons_dpad |= (1 << 6); // Circle (B)
        if (button_mask & (1UL << 3)) rep.buttons_dpad |= (1 << 7); // Triangle (Y)

        // byte 5: L1, R1, L2, R2, Share, Options, L3, R3
        if (button_mask & (1UL << 4))  rep.buttons_misc |= (1 << 0); // L1 (LB)
        if (button_mask & (1UL << 5))  rep.buttons_misc |= (1 << 1); // R1 (RB)
        if (button_mask & (1UL << 6))  rep.buttons_misc |= (1 << 2); // L2 digital (LT)
        if (button_mask & (1UL << 7))  rep.buttons_misc |= (1 << 3); // R2 digital (RT)
        if (button_mask & (1UL << 8))  rep.buttons_misc |= (1 << 4); // Share (Select)
        if (button_mask & (1UL << 9))  rep.buttons_misc |= (1 << 5); // Options (Start)
        if (button_mask & (1UL << 10)) rep.buttons_misc |= (1 << 6); // L3
        if (button_mask & (1UL << 11)) rep.buttons_misc |= (1 << 7); // R3

        // byte 6: PS Button (Central)
        if (button_mask & (1UL << 12)) rep.buttons_sys |= (1 << 0); // PS Button

        rep.l2_trigger = lt_val;
        rep.r2_trigger = rt_val;

        return usb_hid.sendReport(1, &rep, sizeof(rep));
    }

    // ==========================================
    // 2. Android Mode Report
    // ==========================================
    if (s_active_mode == MODE_ANDROID) {
        GamepadReportAndroid_t rep;
        memset(&rep, 0, sizeof(rep));

        // Byte 0: Buttons 1..8 with Sega C/Z gaps (Bits 2 & 5 are 0)
        if (button_mask & (1UL << 0)) rep.buttons[0] |= (1 << 0); // A
        if (button_mask & (1UL << 1)) rep.buttons[0] |= (1 << 1); // B
        // Bit 2 = 0 (C gap)
        if (button_mask & (1UL << 2)) rep.buttons[0] |= (1 << 3); // X
        if (button_mask & (1UL << 3)) rep.buttons[0] |= (1 << 4); // Y
        // Bit 5 = 0 (Z gap)
        if (button_mask & (1UL << 4)) rep.buttons[0] |= (1 << 6); // LB
        if (button_mask & (1UL << 5)) rep.buttons[0] |= (1 << 7); // RB

        // Byte 1: Buttons 9..15
        if (button_mask & (1UL << 6))  rep.buttons[1] |= (1 << 0); // LT
        if (button_mask & (1UL << 7))  rep.buttons[1] |= (1 << 1); // RT
        if (button_mask & (1UL << 8))  rep.buttons[1] |= (1 << 2); // Select
        if (button_mask & (1UL << 9))  rep.buttons[1] |= (1 << 3); // Start
        if (button_mask & (1UL << 12)) rep.buttons[1] |= (1 << 4); // Central (Mode)
        if (button_mask & (1UL << 10)) rep.buttons[1] |= (1 << 5); // L3
        if (button_mask & (1UL << 11)) rep.buttons[1] |= (1 << 6); // R3

        rep.hat = compute_hat_standard(button_mask);
        rep.x  = lx;
        rep.y  = ly;
        rep.z  = rx; // Right Stick X -> Z
        rep.rz = ry; // Right Stick Y -> Rz
        rep.brake       = lt_val;
        rep.accelerator = rt_val;

        return usb_hid.sendReport(0, &rep, sizeof(rep));
    }

    // ==========================================
    // 3. PC / Linux Desktop / Steam Mode Report
    // ==========================================
    GamepadReportPC_t rep;
    memset(&rep, 0, sizeof(rep));

    // Byte 0: Buttons 1..8 with Sega C/Z gaps (Bits 2 & 5 are 0)
    if (button_mask & (1UL << 0)) rep.buttons[0] |= (1 << 0); // Button 1: A
    if (button_mask & (1UL << 1)) rep.buttons[0] |= (1 << 1); // Button 2: B
    // Bit 2 is 0: Button 3 (C gap for Sega/Linux evdev compatibility)
    if (button_mask & (1UL << 2)) rep.buttons[0] |= (1 << 3); // Button 4: X
    if (button_mask & (1UL << 3)) rep.buttons[0] |= (1 << 4); // Button 5: Y
    // Bit 5 is 0: Button 6 (Z gap for Sega/Linux evdev compatibility)
    if (button_mask & (1UL << 4)) rep.buttons[0] |= (1 << 6); // Button 7: LB
    if (button_mask & (1UL << 5)) rep.buttons[0] |= (1 << 7); // Button 8: RB

    // Byte 1: Buttons 9..15
    if (button_mask & (1UL << 6))  rep.buttons[1] |= (1 << 0); // Button 9: LT digital
    if (button_mask & (1UL << 7))  rep.buttons[1] |= (1 << 1); // Button 10: RT digital
    if (button_mask & (1UL << 8))  rep.buttons[1] |= (1 << 2); // Button 11: Select
    if (button_mask & (1UL << 9))  rep.buttons[1] |= (1 << 3); // Button 12: Start
    if (button_mask & (1UL << 12)) rep.buttons[1] |= (1 << 4); // Button 13: Central (Mode / Home)
    if (button_mask & (1UL << 10)) rep.buttons[1] |= (1 << 5); // Button 14: L3
    if (button_mask & (1UL << 11)) rep.buttons[1] |= (1 << 6); // Button 15: R3
    // Bit 7 is 0: Button 16 (Padding)

    rep.hat = compute_hat_standard(button_mask);
    rep.x  = lx;
    rep.y  = ly;
    rep.rx = rx; // Right Stick X -> Rx
    rep.ry = ry; // Right Stick Y -> Ry
    rep.z  = lt_val;
    rep.rz = rt_val;

    return usb_hid.sendReport(0, &rep, sizeof(rep));
}
