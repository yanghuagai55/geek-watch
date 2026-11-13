#ifndef BMP580_H
#define BMP580_H

#include "hal.h"

int bmp580_init(void);
int bmp580_present(void);
int bmp580_read(hal_baro_t *out);

#endif