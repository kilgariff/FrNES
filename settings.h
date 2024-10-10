#ifndef SETTINGS_H_
#define SETTINGS_H_

#include <inttypes.h>
#include <stdbool.h>

typedef struct __attribute__ ((packed, aligned(4))) ControllerSettings_s {
    bool analogEnabled;
    uint8_t selectKey;
    uint8_t aKey;
    uint8_t bKey;
} ControllerSettings_t;

#define DC_CONTROLLER_BUTTON_A     0
#define DC_CONTROLLER_BUTTON_B     1
#define DC_CONTROLLER_BUTTON_X     2
#define DC_CONTROLLER_BUTTON_Y     3
#define DC_CONTROLLER_BUTTON_LTRIG 4

// Controllers:
extern ControllerSettings_t controllerSettings[];

// Video:
extern uint16_t opt_Stretch;
extern uint16_t opt_Filter;
extern uint8_t opt_ClipVars[4];

// Sound:
extern uint16_t opt_SoundEnabled;
extern uint16_t opt_FrameSkip;
extern uint16_t opt_AutoFrameSkip;
extern uint16_t opt_ShowFrameRate;
extern int16_t opt_VMUPort;
extern uint16_t opt_SRAM;

#endif