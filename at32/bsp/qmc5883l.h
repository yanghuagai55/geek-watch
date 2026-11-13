#ifndef QMC5883L_H
#define QMC5883L_H

#include "hal.h"

int qmc5883l_init(void);
int qmc5883l_present(void);
int qmc5883l_read(hal_mag_t *out);
/* 返回探测到的型号：0=无, 1=QMC5883L(0x0D), 2=QMC5883P(0x2C)。 */
int qmc5883l_chip(void);
/* 调试用：一行寄存器快照（地址、ID、状态、控制寄存器）。 */
void qmc5883l_debug(char *out, int cap);

#endif