#ifndef QMC5883L_H
#define QMC5883L_H

#include "hal.h"

int qmc5883l_init(void);
int qmc5883l_present(void);
int qmc5883l_read(hal_mag_t *out);

#endif