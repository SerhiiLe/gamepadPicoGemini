#ifndef GAMEPAD_MODES_H
#define GAMEPAD_MODES_H

#include <Arduino.h>

enum GamepadMode : uint8_t {
    MODE_PC = 0,        // PC / Steam / Linux Desktop (Left: X/Y, Right: Rx/Ry, Triggers: Z/Rz 0..255)
    MODE_ANDROID = 1,   // Android / Google Spec (Left: X/Y, Right: Z/Rz, Triggers: Brake/Gas 0..255)
    MODE_DS4 = 2,       // Sony DualShock 4 (Native DS4 USB Report with Steam/Linux Hardware Rumble)
    MODE_NINTENDO = 3   // Nintendo Switch (HORI Pokken / Switch Native HID 0x0F0D:0x0092)
};

// Base LED mode colors at low idle brightness (15 out of 255)
// Mode PC: Cyan (R=0, G=15, B=15)
#define COLOR_PC_R         0
#define COLOR_PC_G         5
#define COLOR_PC_B         5

// Mode Android: Orange/Amber (R=15, G=6, B=0)
#define COLOR_ANDROID_R    8
#define COLOR_ANDROID_G    3
#define COLOR_ANDROID_B    0

// Mode DualShock 4: Dim White initially upon boot (R=5, G=5, B=5)
#define COLOR_DS4_R        5
#define COLOR_DS4_G        5
#define COLOR_DS4_B        5

// Mode Nintendo Switch: Red (R=8, G=0, B=0)
#define COLOR_NINTENDO_R   8
#define COLOR_NINTENDO_G   0
#define COLOR_NINTENDO_B   0

#endif // GAMEPAD_MODES_H

