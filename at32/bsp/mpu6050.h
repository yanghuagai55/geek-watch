#ifndef MPU6050_H
#define MPU6050_H

#include "hal.h"

int mpu6050_init(void);
int mpu6050_present(void);
int mpu6050_read(hal_imu_t *out);

#endif