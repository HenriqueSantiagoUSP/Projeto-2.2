// can_ids.h
#ifndef __CAN_IDS_H__
#define __CAN_IDS_H__

#include <stdint.h>

typedef struct {
    uint16_t HCSR501;
    uint16_t LM35;
    uint16_t REEDSWITCH;
} can_grupo_sensores_t;

typedef struct {
    can_grupo_sensores_t grupo_1;
    can_grupo_sensores_t grupo_2;
    can_grupo_sensores_t grupo_3;
    can_grupo_sensores_t grupo_4;
    can_grupo_sensores_t grupo_5;
} can_id_t;

extern const can_id_t CAN_ID;

#endif