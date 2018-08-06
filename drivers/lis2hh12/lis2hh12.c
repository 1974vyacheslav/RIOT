/*
 * Copyright (C) 2018 Freie Universität Berlin
 *
 * This file is subject to the terms and conditions of the GNU Lesser General
 * Public License v2.1. See the file LICENSE in the top level directory for more
 * details.
 */

/**
 * @ingroup     drivers_lis2dh12
 * @{
 *
 * @file
 * @brief       LIS2DH12 accelerometer driver implementation
 *
 * @author      Hauke Petersen <hauke.petersen@fu-berlin.de>
 * @}
 */

#include "lis2hh12.h"
#include "include/lis2hh12_internal.h"

#define ENABLE_DEBUG        (0)
#include "debug.h"

/* shortcuts for I2C bus parameters */
#define DEV_I2C            (dev->params.i2c)
#define DEV_ADDR           (dev->params.addr)

/**
 * @brief Takes an unsigned value representing a two's complement number
 *        and returns the signed number it represents
 *
 * @param[in] value    value which represents a two's complement number
 *
 * @return             the converted signed number of 'value'
 */
 static inline int16_t _twos_complement(int16_t value)
{
    if (value & MASK_INT16_MSB) {
        value = ~(value & MASK_INT16_NMSB) + 1;
        return ~(value & MASK_INT16_NMSB);
    }
    else {
        return value;
    }
}

int lis2dh12_init(lis2hh12_t *dev, const lis2hh12_params_t *params)
{
    dev->params = *params;

    uint8_t tmp;

    puts("Acquire bus");
    
    i2c_acquire(DEV_I2C);

    if (i2c_read_reg(DEV_I2C, DEV_ADDR, LIS2HH12_WHO_AM_I, &tmp, 0) < 0)
        return LIS2HH12_NOBUS;

    if (tmp != WHO_AM_I_VAL) {
        DEBUG("LIS2HH12: Identification failed, %02X != %02X\n", tmp, WHO_AM_I_VAL);
        return LIS2HH12_NOBUS;
    }
    
    tmp = ( LIS2HH12_MASK_CTRL1_BDU_EN      /* enable block data update (registers not updated until MSB and LSB read) */
          | LIS2HH12_MASK_CTRL1_XYZ_EN      /* enable  x-, y, z-axis  */
          | dev->params.odr);             /* set output data rate */
    
    if (i2c_write_reg(DEV_I2C, DEV_ADDR, LIS2HH12_CTRL1, tmp, 0) < 0)
        return LIS2HH12_NOBUS;
    
    /* Disable HP filter */
    if (i2c_write_reg(DEV_I2C, DEV_ADDR, LIS2HH12_CTRL2, 0x00, 0) < 0)
        return LIS2HH12_NOBUS;

    /* Disable INT1 interrupt sources */
    if (i2c_write_reg(DEV_I2C, DEV_ADDR, LIS2HH12_CTRL3, 0x00, 0) < 0)
        return LIS2HH12_NOBUS;

    /* Set Full-scale configuration */
    tmp = (dev->params.scale);
    if (i2c_write_reg(DEV_I2C, DEV_ADDR, LIS2HH12_CTRL4, tmp, 0) < 0)
        return LIS2HH12_NOBUS;

    i2c_release(DEV_I2C);

    return LIS2HH12_OK;
}

int lis2hh12_read_xyz(const lis2hh12_t *dev, lis2hh12_data_t *data)
{
    uint8_t tmp[2] = {0, 0};

    i2c_acquire(DEV_I2C);

    if (i2c_read_regs(DEV_I2C, DEV_ADDR, LIS3MDL_OUT_X_L_REG, &tmp[0], 2, 0) < 0)
        return LIS2HH12_NOBUS;

    int16_t x = ((tmp[1] << 8) | tmp[0]);
    x = _twos_complement(x);

    if(i2c_read_regs(DEV_I2C, DEV_ADDR, LIS3MDL_OUT_Y_L_REG, &tmp[0], 2, 0) < 0)
        return LIS2HH12_NOBUS;

    int16_t y = ((tmp[1] << 8) | tmp[0]);
    y = _twos_complement(y);


    if (i2c_read_regs(DEV_I2C, DEV_ADDR, LIS3MDL_OUT_Z_L_REG, &tmp[0], 2, 0) < 0)
        return LIS2HH12_NOBUS;

    int16_t z = ((tmp[1] << 8) | tmp[0]);
    z = _twos_complement(z);

    int16_t scale = 0;
    switch (dev->params.scale) {
        case LIS2HH12_SCALE_2G:
            scale = 61;
            break;
        case LIS2HH12_SCALE_4G:
            scale = 122;
            break;
        case LIS2HH12_SCALE_8G:
            scale = 244;
            break;
        default:
            scale = 61;
            break;
    }

    data->x_axis = x * scale / 1000;
    data->y_axis = y * scale / 1000;
    data->z_axis = z * scale / 1000;

    i2c_release(DEV_I2C);

    return LIS2HH12_OK;
}

int lis2hh12_read_temp(const lis2hh12_t *dev, int16_t *value)
{
    i2c_acquire(DEV_I2C);
    
    if (i2c_read_regs(DEV_I2C, DEV_ADDR, LIS2HH12_TEMP_L, (uint8_t*)value, 2, 0) < 0)
        return LIS2HH12_NOBUS;

    i2c_release(DEV_I2C);

    *value = _twos_complement(*value);

    return LIS2DH12_OK;
}

int lis2hh12_poweron(const lis2hh12_t *dev)
{
    i2c_acquire(DEV_I2C);
    tmp = ( LIS2HH12_MASK_CTRL1_BDU_EN      /* enable block data update (registers not updated until MSB and LSB read) */
          | LIS2HH12_MASK_CTRL1_XYZ_EN      /* enable  x-, y, z-axis  */
          | dev->params.odr);             /* set output data rate */
    
    if(i2c_write_reg(DEV_I2C, DEV_ADDR, LIS2HH12_CTRL1, tmp, 0) < 0)
        return LIS2HH12_NOBUS;
    
    i2c_release(DEV_I2C);

    return LIS2HH12_OK;
}

int lis2hh12_poweroff(const lis2hh12_t *dev)
{
    uint8_t tmp = LIS2HH12_ODR_PWRDWN;  /**< enable power-down mode */

    i2c_acquire(DEV_I2C);

    if(i2c_write_reg(DEV_I2C, DEV_ADDR, LIS2HH12_CTRL1, tmp, 0) < 0)
        return LIS2HH12_NOBUS;
    
    i2c_release(DEV_I2C);
    
    return LIS2HH12_OK;
}
