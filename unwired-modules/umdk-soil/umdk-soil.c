/*
 * Copyright (C) 2018 Unwired Devices LLC <info@unwds.com>

 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the Software
 * is furnished to do so, subject to the following conditions:

 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.

 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
 * PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
 * FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

/**
 * @defgroup
 * @ingroup
 * @brief
 * @{
 * @file		umdk-soil.c
 * @brief       umdk-soil module implementation
 * @author      Oleg Artamonov
 */

#ifdef __cplusplus
extern "C" {
#endif

/* define is autogenerated, do not change */
#undef _UMDK_MID_
#define _UMDK_MID_ UNWDS_SOIL_MODULE_ID

/* define is autogenerated, do not change */
#undef _UMDK_NAME_
#define _UMDK_NAME_ "soil"

#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#include "board.h"

#include "umdk-ids.h"
#include "unwds-common.h"
#include "include/umdk-soil.h"
#include "periph/uart.h"

#include "thread.h"
#include "xtimer.h"
#include "rtctimers-millis.h"

#define ENABLE_DEBUG (0)
#include "debug.h"

static uwnds_cb_t *callback;

typedef struct {
    uint32_t publish_period_sec;
} umdk_soil_config_t;

static umdk_soil_config_t umdk_soil_config = { .publish_period_sec = 1800};

static bool is_polled = false;
static rtctimers_millis_t timer;
static msg_t timer_msg = {};
static kernel_pid_t timer_pid;

#define START_BYTE                  0x55 // byte 0
#define ADDRESS_SIZE                8   // bytes 1-8 are for the device address
#define OFFSET_TYPE                 9   // byte 9 is device type
#define OFFSET_CMD                  10  // byte 10 is command code
#define OFFSET_SIZE                 11  // byte 11 is data size
#define OFFSET_BYTE_MOISTURE        12  // byte 12 is for moisture
#define OFFSET_BYTE_TEMP            13  // byte 13 is for temperature
#define OFFSET_BYTE_CRC             14  // bytes 13-14 is for CRC
#define CRC_SIZE                    2
#define BUF_SIZE                    OFFSET_BYTE_CRC + CRC_SIZE

enum {
    TYPE_SOIL_SENSOR    = 1,
} data_types_t;

static volatile int rx_cnt = 0;
static volatile bool rx_started = false;
static volatile bool rx_done = false;
static volatile uint8_t rx_buf[BUF_SIZE - 1]; /* without start byte */

static void rx_cb(void *arg, uint8_t data)
{
    (void)arg;
    
    if (!rx_started) {
        if (data == START_BYTE) {
            rx_started = true;
            rx_cnt = 0;
        }
    } else {
        rx_buf[rx_cnt] = data;
        if (++rx_cnt == (BUF_SIZE - 1)) {
            rx_started = false;
            rx_done = true;
        }
    }
}

static int prepare_result(module_data_t *data) {
    gpio_clear(UMDK_SOIL_POWEREN);
    rx_started = false;
    rx_done = false;
    
    rtctimers_millis_sleep(2500);
    uint32_t start = rtctimers_millis_now();
    while (!rx_done) {
        /* timeout 5 seconds */
        if (rtctimers_millis_now() > start + 2500) {
            gpio_set(UMDK_SOIL_POWEREN);
            puts("[umdk-" _UMDK_NAME_ "] Sensor timeout");
            return -1;
        }
    }
    
    gpio_set(UMDK_SOIL_POWEREN);
    
    if (rx_buf[OFFSET_TYPE - 1] == TYPE_SOIL_SENSOR) {
        uint8_t moist = rx_buf[OFFSET_BYTE_MOISTURE - 1];
        int8_t temp = rx_buf[OFFSET_BYTE_TEMP  - 1] - 50;
        
        printf("[umdk-" _UMDK_NAME_ "] Water: %d %%; temperature: %d C\n", moist, temp);
        
        if (data) {
            data->data[0] = _UMDK_MID_;
            data->data[1] = UMDK_SOIL_DATA;
            data->data[2] = moist;
            data->data[3] = temp;
            data->length = 4;
        }
    } else {
        puts("[umdk-" _UMDK_NAME_ "] Unknown data");
        return -2;
    }
    
    return 0;
}

static void *timer_thread(void *arg) {
    (void)arg;

    msg_t msg;
    msg_t msg_queue[4];
    msg_init_queue(msg_queue, 4);

    puts("[umdk-" _UMDK_NAME_ "] Periodic publisher thread started");

    while (1) {
        msg_receive(&msg);

        module_data_t data = {};
        data.as_ack = is_polled;
        is_polled = false;
        
        int res = prepare_result(&data);
        if (res != 0) {
            data.data[0] = _UMDK_MID_;
            data.data[0] = UMDK_SOIL_DATA_ERR;
            data.data[1] = res;
            data.length = 3;
        }

        /* Notify the application */
        callback(&data);

        /* Restart after delay */
        rtctimers_millis_set_msg(&timer, 1000 * umdk_soil_config.publish_period_sec, &timer_msg, timer_pid);
    }
    
    return NULL;
}

static void reset_config(void) {
    umdk_soil_config.publish_period_sec = 1800;
}

static void init_config(void) {
	reset_config();

	if (!unwds_read_nvram_config(_UMDK_MID_, (uint8_t *) &umdk_soil_config, sizeof(umdk_soil_config)))
		reset_config();
}

static void save_config(void) {
	unwds_write_nvram_config(_UMDK_MID_, (uint8_t *) &umdk_soil_config, sizeof(umdk_soil_config));
}

static void set_period(uint32_t period) {
    umdk_soil_config.publish_period_sec = period;
    printf("[umdk-" _UMDK_NAME_ "] Period set to %" PRIu32 " sec\n", umdk_soil_config.publish_period_sec);
    save_config();
}

int umdk_soil_shell_cmd(int argc, char **argv) {
    if (argc == 1) {
        puts ("soil get - obtain data from sensor");
        puts ("soil send - obtain and send data");
        puts ("soil period <period> - set publishing period in seconds");
        puts ("soil reset - reset settings to default");
        return 0;
    }
    
    char *cmd = argv[1];
    
    if (strcmp(cmd, "get") == 0) {
        prepare_result(NULL);
    }
    
    if (strcmp(cmd, "send") == 0) {
        /* Send signal to publisher thread */
		msg_send(&timer_msg, timer_pid);
    }
    
    if (strcmp(cmd, "period") == 0) {
        char *val = argv[2];
        set_period(atoi(val));
    }
    
    if (strcmp(cmd, "reset") == 0) {
        reset_config();
        save_config();
    }
    
    return 1;
}

void umdk_soil_init(uwnds_cb_t *event_callback)
{
    callback = event_callback;

    init_config();
    /*
    char *reader_stack = (uint8_t *) allocate_stack(UMDK_SOIL_READER_STACK_SIZE);
    if (!reader_stack) {
        return;
    }
    reader_pid = thread_create(reader_stack, UMDK_SOIL_READER_STACK_SIZE, THREAD_PRIORITY_MAIN - 1, THREAD_CREATE_STACKTEST, reader, dev, "umdk-soil UART reader");
    */
    
    gpio_init(UMDK_SOIL_POWEREN, GPIO_OUT);
    gpio_set(UMDK_SOIL_POWEREN);
    
    uart_init(UMDK_SOIL_UART, 9600, rx_cb, NULL);
    
    char *timer_stack = (char *) allocate_stack(UMDK_SOIL_STACK_SIZE);
    if (!timer_stack) {
        return;
    }
    timer_pid = thread_create(timer_stack, UMDK_SOIL_STACK_SIZE, THREAD_PRIORITY_MAIN - 1, THREAD_CREATE_STACKTEST, timer_thread, NULL, "umdk-soil timer thread");
    /* Start publishing timer */
    rtctimers_millis_set_msg(&timer, 1000*umdk_soil_config.publish_period_sec, &timer_msg, timer_pid);

    printf("[umdk-" _UMDK_NAME_ "] Period %" PRIu32 " sec\n", umdk_soil_config.publish_period_sec);
    
    unwds_add_shell_command("soil", "type 'soil' for commands list", umdk_soil_shell_cmd);
    
    /* quick dirty hack for the unwd-range-l0-round board */
    gpio_init(GPIO_PIN(PORT_A, 12), GPIO_OUT);
    gpio_clear(GPIO_PIN(PORT_A, 12));
    gpio_init(GPIO_PIN(PORT_A, 13), GPIO_OUT);
    gpio_clear(GPIO_PIN(PORT_A, 13));
    gpio_init(GPIO_PIN(PORT_B, 1), GPIO_OUT);
    gpio_clear(GPIO_PIN(PORT_B, 1));
    /* --- */
}

static void reply_fail(module_data_t *reply) {
	reply->length = 2;
	reply->data[0] = _UMDK_MID_;
	reply->data[1] = UMDK_SOIL_FAIL;
}

static void reply_ok(module_data_t *reply) {
	reply->data[0] = _UMDK_MID_;
	reply->data[1] = UMDK_SOIL_CONFIG;
    reply->length = 2;
    
    uint16_t period = umdk_soil_config.publish_period_sec;
    convert_to_be_sam((void *)&period, sizeof(period));
    memcpy(&reply->data[reply->length], (void *)&period, sizeof(period));
    reply->length += sizeof(period);
}

bool umdk_soil_cmd(module_data_t *data, module_data_t *reply)
{
    if (data->length < 1) {
        reply_fail(reply);
        return true;
    }

    if ((data->data[0] == UMDK_SOIL_CONFIG) && (data->length == 3)) {
        umdk_soil_config.publish_period_sec = data->data[1];
        
        uint32_t period = data->data[1] | data->data[2] << 8;
        convert_from_be_sam((void *)&period, sizeof(period));
        if (period > 0) {
            set_period(period);
        } else {
            puts("[umdk-" _UMDK_NAME_ "] period: do not change");
        }
        
        reply_ok(reply);
    } else {
        puts("[umdk-" _UMDK_NAME_ "] Incorrect command");
        reply_fail(reply);
    }

    return true;
}

#ifdef __cplusplus
}
#endif
