/*
 * Copyright (C) 2014 FU Berlin
 * Copyright (C) 2017 Unwired Devices LLC <info@unwds.com>
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

/**
 * @ingroup     cpu_stm32l1
 * @ingroup     drivers_periph_i2c
 * @{
 *
 * @file
 * @brief       Low-level I2C driver implementation
 *
 * @note This implementation only implements the 7-bit addressing mode.
 *
 * @author      Peter Kietzmann <peter.kietzmann@haw-hamburg.de>
 * @author      Hauke Petersen <hauke.petersen@fu-berlin.de>
 * @author      Thomas Eichinger <thomas.eichinger@fu-berlin.de>
 * @author      Oleg Artamonov <oleg@unwds.com>
 *
 * @}
 */

#include <stdint.h>

#include "cpu.h"
#include "mutex.h"
#include "periph/i2c.h"
#include "periph/gpio.h"
#include "periph_conf.h"
#include "xtimer.h"

#define ENABLE_DEBUG    (0)
#include "debug.h"



/* number of nsec per usec */
#define NSEC_PER_USEC 1000

/* number of microseconds per millisecond */
#define USEC_PER_MSEC 1000

/* number of milliseconds per second */
#define MSEC_PER_SEC 1000

/* number of microseconds per second */
#define USEC_PER_SEC ((USEC_PER_MSEC) * (MSEC_PER_SEC))

/* number of nanoseconds per second */
#define NSEC_PER_SEC ((NSEC_PER_USEC) * (USEC_PER_MSEC) * (MSEC_PER_SEC))



/* static function definitions */
static void _i2c_init(I2C_TypeDef *i2c, uint32_t timing);
static int _start(I2C_TypeDef *i2c, uint8_t address, uint8_t rw_flag);
static inline void _clear_addr(I2C_TypeDef *i2c);
static inline void _write(I2C_TypeDef *i2c, const uint8_t *data, int length);
static inline void _stop(I2C_TypeDef *i2c);
static inline void _i2c_irq(I2C_TypeDef *i2c);

static volatile int8_t i2c_bus_error = 0;
static int8_t i2c_bus_result = 0;

/**
 * @brief Array holding one pre-initialized mutex for each I2C device
 */
static mutex_t locks[] =  {
#if I2C_0_EN
    [I2C_0] = MUTEX_INIT,
#endif
#if I2C_1_EN
    [I2C_1] = MUTEX_INIT,
#endif
#if I2C_2_EN
    [I2C_2] = MUTEX_INIT,
#endif
#if I2C_3_EN
    [I2C_3] = MUTEX_INIT
#endif
};

int i2c_init_master(i2c_t dev, i2c_speed_t speed)
{
    uint32_t i2c_clk;
    uint32_t i2c_h_min_time;
    uint32_t i2c_l_min_time;
    uint32_t i2c_hold_time_min;
    uint32_t i2c_setup_time_min;
    uint32_t presc = 1;
    uint32_t timing;

    if ((unsigned int)dev >= I2C_NUMOF) {
        return -1;
    }

    I2C_TypeDef *i2c = i2c_config[dev].dev;

    /* enable I2C clock */
    i2c_poweron(dev);
	
	/* disable device */
	/* operations on running I2C device will result in BERR error */
    i2c->CR1 &= ~I2C_CR1_PE;

    /* set IRQn priority */
    NVIC_SetPriority(i2c_config[dev].er_irqn, I2C_IRQ_PRIO);

    /* enable IRQn */
    NVIC_EnableIRQ(i2c_config[dev].er_irqn);

    /* configure pins */
    gpio_init(i2c_config[dev].scl, i2c_config[dev].pin_mode);
    gpio_init_af(i2c_config[dev].scl, i2c_config[dev].af);
    gpio_init(i2c_config[dev].sda, i2c_config[dev].pin_mode);
    gpio_init_af(i2c_config[dev].sda, i2c_config[dev].af);

    i2c_clk = I2C_APBCLK;
    /* read speed configuration */
    switch (speed) {
        case I2C_SPEED_NORMAL:
            i2c_h_min_time = 4000;
            i2c_l_min_time = 4700;
            i2c_hold_time_min = 500;
            i2c_setup_time_min = 1250;
            break;

        case I2C_SPEED_FAST:
            i2c_h_min_time = 600;
            i2c_l_min_time = 1300;
            i2c_hold_time_min = 375;
            i2c_setup_time_min = 500;
            break;

        default:
            return -2;
    }

    /* Calculate period until prescaler matches */
    do {
        uint32_t t_presc = i2c_clk / presc;
        uint32_t ns_presc = NSEC_PER_SEC / t_presc;
        uint32_t sclh = i2c_h_min_time / ns_presc;
        uint32_t scll = i2c_l_min_time / ns_presc;
        uint32_t sdadel = i2c_hold_time_min / ns_presc;
        uint32_t scldel = i2c_setup_time_min / ns_presc;

        if ((sclh - 1) > 255 ||
            (scll - 1) > 255 ||
            sdadel > 15 ||
            (scldel - 1) > 15) {
            ++presc;
            continue;
        }


        /* prepare the timing register value */
        timing = (((presc - 1) << 28) | ((scldel - 1) << 20) | (sdadel << 16) | ((sclh - 1) << 8) | (scll - 1));

        break;
    } while (presc < 16);

    if (presc >= 16) {
        DEBUG("I2C:failed to find prescaler value");
        return 2;
    }


    /* configure device */
    _i2c_init(i2c, timing);

    return 0;
}

static void _i2c_init(I2C_TypeDef *i2c, uint32_t timing)
{
    /* disable device*/
    i2c->CR1 &= ~I2C_CR1_PE;
    
    /* configure filters */
    /* configure analog noise filter */
    i2c->CR1 |= ~(I2C_CR1_ANFOFF);

    // /* configure digital noise filter */
    // i2c->CR1 |= I2C_CR1_DNF;

    /* set timing registers */
    i2c->TIMINGR = timing;

    /* configure clock stretching */
    i2c->CR1 &= ~(I2C_CR1_NOSTRETCH);
    
    /* configure device */
    i2c->OAR1 = 0;              /* makes sure we are in 7-bit address mode */
    
    /* enable device */
    i2c->CR1 |= I2C_CR1_PE;
}

int i2c_acquire(i2c_t dev)
{
    if (dev >= I2C_NUMOF) {
        return -1;
    }
    mutex_lock(&locks[dev]);
    return 0;
}

int i2c_release(i2c_t dev)
{
    if (dev >= I2C_NUMOF) {
        return -1;
    }
    mutex_unlock(&locks[dev]);
    return 0;
}

int i2c_read_byte(i2c_t dev, uint8_t address, void *data)
{
    return i2c_read_bytes(dev, address, data, 1);
}

int i2c_read_bytes(i2c_t dev, uint8_t address, void *data, int length)
{
    unsigned int state;
    int i = 0;
    uint8_t *my_data = data;

    if ((unsigned int)dev >= I2C_NUMOF) {
        return -1;
    }

    I2C_TypeDef *i2c = i2c_config[dev].dev;
    switch (length) {
        case 1:
            DEBUG("Send Slave address and wait for ADDR == 1\n");
           	i2c_bus_result = _start(i2c, address, I2C_FLAG_READ);
			if (i2c_bus_result < 0) {
				return i2c_bus_result;
			}

            DEBUG("Set ACK = 0\n");
            i2c->CR1 &= ~(I2C_CR1_ACK);

            DEBUG("Clear ADDR and set STOP = 1\n");
            state = irq_disable();
            _clear_addr(i2c);
            i2c->CR1 |= (I2C_CR1_STOP);
            irq_restore(state);

            DEBUG("Wait for RXNE == 1\n");

            while (!(i2c->SR1 & I2C_SR1_RXNE)) {}

            DEBUG("Read received data\n");
            *my_data = i2c->DR;

            /* wait until STOP is cleared by hardware */
            while (i2c->CR1 & I2C_CR1_STOP) {}

            /* reset ACK to be able to receive new data */
            i2c->CR1 |= (I2C_CR1_ACK);
            break;

        case 2:
            DEBUG("Send Slave address and wait for ADDR == 1\n");
           	i2c_bus_result = _start(i2c, address, I2C_FLAG_READ);
			if (i2c_bus_result < 0) {
				return i2c_bus_result;
			}
            DEBUG("Set POS bit\n");
            i2c->CR1 |= (I2C_CR1_POS | I2C_CR1_ACK);
            DEBUG("Crit block: Clear ADDR bit and clear ACK flag\n");
            state = irq_disable();
            _clear_addr(i2c);
            i2c->CR1 &= ~(I2C_CR1_ACK);
            irq_restore(state);

            DEBUG("Wait for transfer to be completed\n");

            while (!(i2c->SR1 & I2C_SR1_BTF)) {}

            DEBUG("Crit block: set STOP and read first byte\n");
            state = irq_disable();
            i2c->CR1 |= (I2C_CR1_STOP);
            my_data[0] = i2c->DR;
            irq_restore(state);

            DEBUG("read second byte\n");
            my_data[1] = i2c->DR;

            DEBUG("wait for STOP bit to be cleared again\n");

            while (i2c->CR1 & I2C_CR1_STOP) {}

            DEBUG("reset POS = 0 and ACK = 1\n");
            i2c->CR1 &= ~(I2C_CR1_POS);
            i2c->CR1 |= (I2C_CR1_ACK);
            break;

        default:
            DEBUG("Send Slave address and wait for ADDR == 1\n");
			i2c_bus_result = _start(i2c, address, I2C_FLAG_READ);
			if (i2c_bus_result < 0) {
				return i2c_bus_result;
			}
            _clear_addr(i2c);

            while (i < (length - 3)) {
                DEBUG("Wait until byte was received\n");

                while (!(i2c->SR1 & I2C_SR1_RXNE)) {}

                DEBUG("Copy byte from DR\n");
                my_data[i++] = i2c->DR;
            }

            DEBUG("Reading the last 3 bytes, waiting for BTF flag\n");

            while (!(i2c->SR1 & I2C_SR1_BTF)) {}

            DEBUG("Disable ACK\n");
            i2c->CR1 &= ~(I2C_CR1_ACK);

            DEBUG("Crit block: set STOP and read N-2 byte\n");
            state = irq_disable();
            my_data[i++] = i2c->DR;
            i2c->CR1 |= (I2C_CR1_STOP);
            irq_restore(state);

            DEBUG("Read N-1 byte\n");
            my_data[i++] = i2c->DR;

            while (!(i2c->SR1 & I2C_SR1_RXNE)) {}

            DEBUG("Read last byte\n");

            my_data[i++] = i2c->DR;

            DEBUG("wait for STOP bit to be cleared again\n");

            while (i2c->CR1 & I2C_CR1_STOP) {}

            DEBUG("reset POS = 0 and ACK = 1\n");
            i2c->CR1 &= ~(I2C_CR1_POS);
            i2c->CR1 |= (I2C_CR1_ACK);
    }

    return length;
}

int i2c_read_reg(i2c_t dev, uint8_t address, uint8_t reg, void *data)
{
    return i2c_read_regs(dev, address, reg, data, 1);
}

int i2c_read_regs(i2c_t dev, uint8_t address, uint8_t reg, void *data, int length)
{
    if ((unsigned int)dev >= I2C_NUMOF) {
        return -1;
    }

    I2C_TypeDef *i2c = i2c_config[dev].dev;

    /* send start condition and slave address */
    DEBUG("Send slave address and clear ADDR flag\n");
    i2c_bus_result = _start(i2c, address, I2C_FLAG_WRITE);
    if (i2c_bus_result < 0) {
		return i2c_bus_result;
	}
    _clear_addr(i2c);
    DEBUG("Write reg into DR\n");
    i2c->DR = reg;
    _stop(i2c);
    DEBUG("Now start a read transaction\n");
    return i2c_read_bytes(dev, address, data, length);
}

int i2c_write_byte(i2c_t dev, uint8_t address, uint8_t data)
{
    return i2c_write_bytes(dev, address, &data, 1);
}

int i2c_write_bytes(i2c_t dev, uint8_t address, const void *data, int length)
{
    if ((unsigned int)dev >= I2C_NUMOF) {
        return -1;
    }

    I2C_TypeDef *i2c = i2c_config[dev].dev;

    /* start transmission and send slave address */
    DEBUG("sending start sequence\n");
	i2c_bus_result = _start(i2c, address, I2C_FLAG_WRITE);
    if (i2c_bus_result < 0) {
		return i2c_bus_result;
	}
    _clear_addr(i2c);
    /* send out data bytes */
    _write(i2c, data, length);
    /* end transmission */
    DEBUG("Ending transmission\n");
    _stop(i2c);
    DEBUG("STOP condition was send out\n");
    return length;
}

int i2c_write_reg(i2c_t dev, uint8_t address, uint8_t reg, uint8_t data)
{
    return i2c_write_regs(dev, address, reg, &data, 1);
}

int i2c_write_regs(i2c_t dev, uint8_t address, uint8_t reg, const void *data, int length)
{
    if ((unsigned int)dev >= I2C_NUMOF) {
        return -1;
    }

    I2C_TypeDef *i2c = i2c_config[dev].dev;

    /* start transmission and send slave address */
	i2c_bus_result = _start(i2c, address, I2C_FLAG_WRITE);
    if (i2c_bus_result < 0) {
		return i2c_bus_result;
	}
    _clear_addr(i2c);
    /* send register address and wait for complete transfer to be finished*/
    _write(i2c, &reg, 1);
    /* write data to register */
    _write(i2c, data, length);
    /* finish transfer */
    _stop(i2c);
    /* return number of bytes send */
    return length;
}

void i2c_poweron(i2c_t dev)
{
    if ((unsigned int)dev < I2C_NUMOF) {
        periph_clk_en(APB1, (RCC_APB1ENR1_I2C1EN << dev));
    }
}

void i2c_poweroff(i2c_t dev)
{
    if ((unsigned int)dev < I2C_NUMOF) {
        while (i2c_config[dev].dev->SR2 & I2C_SR2_BUSY) {}
        periph_clk_dis(APB1, (RCC_APB1ENR1_I2C1EN << dev));
    }
}

static void _i2c_reset(I2C_TypeDef *i2c) {
    DEBUG("I2C: Resetting the bus\n");           
    
    uint16_t ccr = i2c->CCR;

    i2c->CR1 |= I2C_CR1_SWRST;
    i2c->CR1 &= ~I2C_CR1_SWRST;           
    
    _i2c_init(i2c, ccr);
}

static int _start(I2C_TypeDef *i2c, uint8_t address, uint8_t rw_flag)
{
    /* wait for device to be ready */
    DEBUG("Wait for device to be ready\n");

    uint32_t time_now = xtimer_now_usec();
    while (i2c->SR2 & I2C_SR2_BUSY) {
        /* 100 ms timeout */
        if (xtimer_now_usec() - time_now > 100000) {
            DEBUG("Timeout waiting for device, resetting the bus\n");
            _i2c_reset(i2c);
        }
    }

    /* generate start condition */
    DEBUG("Generate start condition\n");
    i2c->CR1 |= I2C_CR1_START;
    DEBUG("Wait for SB flag to be set\n");

    while (!(i2c->SR1 & I2C_SR1_SB)) {}

    /* send address and read/write flag */
    DEBUG("Send address\n");
	i2c_bus_error = 0;
    i2c->DR = (address << 1) | rw_flag;
    /* clear ADDR flag by reading first SR1 and then SR2 */
    DEBUG("Wait for ADDR flag to be set\n");

    while (!(i2c->SR1 & I2C_SR1_ADDR)) {
		/* I2C bus failure */
		if (i2c_bus_error) {
            /* reset I2C bus */
            _i2c_reset(i2c);
            
            return -1;
		}
	}
	return 0;
}

static inline void _clear_addr(I2C_TypeDef *i2c)
{
    i2c->SR1;
    i2c->SR2;
    DEBUG("Cleared address\n");
}

static inline void _write(I2C_TypeDef *i2c, const uint8_t *data, int length)
{
    DEBUG("Looping through bytes\n");

    for (int i = 0; i < length; i++) {
        /* write data to data register */
        i2c->TXDR = data[i];
        DEBUG("Written %i byte to data reg, now waiting for DR to be empty again\n", i);

        /* wait for transfer to finish */
        while (!(i2c->ISR & I2C_ISR_TXE)) {}

        DEBUG("DR is now empty again\n");
    }
}

static inline void _stop(I2C_TypeDef *i2c)
{
    /* make sure transfer is complete */
    DEBUG("Wait for transfer to be complete\n");
    while (!(i2c->ISR & I2C_ISR_TC)) {}

    /* send STOP condition */
    DEBUG("Generate stop condition\n");
    i2c->CR2 |= I2C_CR2_STOP;
}

static inline void _i2c_irq(I2C_TypeDef *i2c) {
	unsigned state = i2c->ISR;
	DEBUG("\n\n### I2C ERROR OCCURED ###\n");
	/* printf("status: %08x\n", state); */
    if (state & I2C_ISR_OVR) {
		i2c_bus_error = -1;
    	DEBUG("OVR\n");
    }
    if (state & I2C_ISR_NACKF) {
		i2c_bus_error = -2;
    	i2c->ISR &= ~I2C_ISR_NACKF;
    	DEBUG("NACK");
    }
    if (state & I2C_ISR_ARLO) {
		i2c_bus_error = -3;
    	i2c->ISR &= ~I2C_ISR_ARLO;
    	DEBUG("ARLO\n");
    }
    if (state & I2C_ISR_BERR) {
		i2c_bus_error = -4;
    	DEBUG("BERR\n");
    }
    if (state & I2C_ISR_PECERR) {
		i2c_bus_error = -5;
    	DEBUG("PECERR\n");
    }
    if (state & I2C_ISR_TIMEOUT) {
		i2c_bus_error = -6;
    	DEBUG("TIMEOUT\n");
    }
    if (state & I2C_ISR_ALERT) {
		i2c_bus_error = -7;
    	DEBUG("SMBALERT\n");
    }
}

#if I2C_0_EN
void I2C_0_ERR_ISR(void)
{
	I2C_TypeDef *i2c;
	i2c = I2C1;
	_i2c_irq(i2c);
}
#endif /* I2C_0_EN */

#if I2C_1_EN
void I2C_1_ERR_ISR(void)
{
	I2C_TypeDef *i2c;
	i2c = I2C2;
	_i2c_irq(i2c);
}
#endif /* I2C_1_EN */
