/*
 * Copyright (C) 2016-2018 Unwired Devices LLC <info@unwds.com>

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
 * @file        umdk-st95.c
 * @brief       umdk-st95 module implementation
 * @author      Mikhail Perkov

 */

#ifdef __cplusplus
extern "C" {
#endif

/* define is autogenerated, do not change */
#undef _UMDK_MID_
#define _UMDK_MID_ UNWDS_ST95_MODULE_ID

/* define is autogenerated, do not change */
#undef _UMDK_NAME_
#define _UMDK_NAME_ "st95"

#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <limits.h>

#include "board.h"

#include "st95.h"

#include "umdk-ids.h"
#include "unwds-common.h"
#include "include/umdk-st95.h"

#include "thread.h"
#include "rtctimers-millis.h"

#define ENABLE_DEBUG (1)
#include "debug.h"

uint8_t test_data[] = {
0x3E, 0x3E, 0x3E, 0x20, 0x23, 0x23, 0x23, 0x20, 0x54, 0x65, 0x73, 0x74, 0x20, 0x53, 0x54, 0x39,
0x35, 0x68, 0x66, 0x3A, 0x20, 0x4E, 0x46, 0x43, 0x20, 0x46, 0x6F, 0x72, 0x75, 0x6D, 0x20, 0x54,
0x61, 0x67, 0x20, 0x74, 0x79, 0x70, 0x65, 0x20, 0x34, 0x61, 0x20, 0x5B, 0x57, 0x72, 0x69, 0x74,
0x65, 0x2F, 0x52, 0x65, 0x61, 0x64, 0x20, 0x4E, 0x44, 0x45, 0x46, 0x5D, 0x3A, 0x20, 0x46, 0x55,
0x43, 0x4B, 0x21, 0x20, 0x0D, 0x0A, 0x54, 0x68, 0x69, 0x73, 0x20, 0x66, 0x75, 0x63, 0x6B, 0x69,
0x6E, 0x67, 0x20, 0x61, 0x6E, 0x74, 0x65, 0x6E, 0x6E, 0x61, 0x20, 0x64, 0x6F, 0x65, 0x73, 0x20,
0x6E, 0x6F, 0x74, 0x20, 0x77, 0x61, 0x6E, 0x74, 0x20, 0x74, 0x6F, 0x20, 0x77, 0x6F, 0x72, 0x6B,
0x20, 0x6E, 0x6F, 0x72, 0x6D, 0x61, 0x6C, 0x6C, 0x79, 0x21, 0x0D, 0x0A, 0x20, 0x52, 0x45, 0x50,
0x45, 0x41, 0x54, 0x21, 0x20, 0x0D, 0x0A, 0x20, 0x54, 0x68, 0x69, 0x73, 0x20, 0x66, 0x75, 0x63,
0x6B, 0x69, 0x6E, 0x67, 0x20, 0x61, 0x6E, 0x74, 0x65, 0x6E, 0x6E, 0x61, 0x20, 0x64, 0x6F, 0x65,
0x73, 0x20, 0x6E, 0x6F, 0x74, 0x20, 0x77, 0x61, 0x6E, 0x74, 0x20, 0x74, 0x6F, 0x20, 0x77, 0x6F,
0x72, 0x6B, 0x20, 0x6E, 0x6F, 0x72, 0x6D, 0x61, 0x6C, 0x6C, 0x79, 0x21, 0x20, 0x0D, 0x0A, 0x20,
0x42, 0x55, 0x4C, 0x4C, 0x20, 0x53, 0x48, 0x49, 0x54, 0x21, 0x20, 0x0D, 0x0A, 0x20, 0x23, 0x23,
0x23, 0x20, 0x3C, 0x3C, 0x3C
};

static msg_t msg_wu = { .type = UMDK_ST95_MSG_WAKE_UP, };
static msg_t msg_rx = { .type = UMDK_ST95_MSG_UID, };

static kernel_pid_t radio_pid;
static uwnds_cb_t *callback;

static st95_t dev;

static st95_params_t st95_params = { .iface = ST95_IFACE_UART,
                                .uart = UMDK_ST95_UART_DEV, .baudrate = UMDK_ST95_UART_BAUD_DEF,
                                .spi = UMDK_ST95_SPI_DEV, .cs_spi = UMDK_ST95_SPI_CS, 
                                .irq_in = UMDK_ST95_IRQ_IN, .irq_out = UMDK_ST95_IRQ_OUT, 
                                .ssi_0 = UMDK_ST95_SSI_0, .ssi_1 = UMDK_ST95_SSI_1,
                                .vcc = UMDK_ST95_VCC_ENABLE };

static uint8_t length_uid = 0;
static uint8_t uid_full[10];
static uint8_t sak = 0;

static volatile uint8_t mode = UMDK_ST95_MODE_GET_UID;
static volatile uint8_t status = UMDK_ST95_STATUS_READY;

static uint8_t ndef_data[255] = { 0x00 };


static void umdk_st95_get_uid(void);

#if ENABLE_DEBUG
    #define PRINTBUFF _printbuff
    static void _printbuff(uint8_t *buff, unsigned len)
    {
        while (len) {
            len--;
            printf("%02X ", *buff++);
        }
        printf("\n");
    }
#else
    #define PRINTBUFF(...)
#endif


static void *radio_send(void *arg)
{
    (void) arg;
    msg_t msg;
    msg_t msg_queue[8];
    msg_init_queue(msg_queue, 8);
      
    while (1) {
        msg_receive(&msg);
        
        module_data_t data;
        data.as_ack = false;
        data.data[0] = _UMDK_MID_;
        data.length = 1;

        switch(msg.type) {
            case UMDK_ST95_MSG_WAKE_UP: {
                if(st95_is_wake_up(&dev) == ST95_WAKE_UP) {
                    umdk_st95_get_uid();   
                }                             
                break;
            }
            case UMDK_ST95_MSG_UID: {
                if(msg.content.value == UMDK_ST95_UID_OK) {
                   
                    memcpy(data.data + 1, uid_full, length_uid);
                    data.length += length_uid;
                }
                else {
                    DEBUG("[ERROR]: Invalid UID\n");
                    PRINTBUFF(uid_full, length_uid);
                    
                    data.data[1] = UMDK_ST95_ERROR_REPLY;
                    data.length = 2;
                }
                
                DEBUG("RADIO: ");
                PRINTBUFF(data.data, data.length);

                callback(&data);
                
                if(mode == UMDK_ST95_MODE_DETECT_TAG) {
                    rtctimers_millis_sleep(UMDK_ST95_DELAY_DETECT_MS);
                    st95_sleep(&dev);
                }
                status = UMDK_ST95_STATUS_READY;
                break;
            }
                        
            default: 
            break;            
        }
    }
    return NULL;
}

static void umdk_st95_get_uid(void)
{
    length_uid = 0;
    sak = 0;
    memset(uid_full, 0x00, sizeof(uid_full));

    if(st95_get_uid(&dev, &length_uid, uid_full, &sak) == ST95_OK) {
        msg_rx.content.value = UMDK_ST95_UID_OK;        
    }
    else {
        length_uid = 0;
        msg_rx.content.value = UMDK_ST95_UID_ERROR;
    }
    
    msg_try_send(&msg_rx, radio_pid);
}

static void wake_up_cb(void * arg)
{
    (void) arg;

    msg_try_send(&msg_wu, radio_pid);
}

void umdk_st95_init(uwnds_cb_t *event_callback)
{
    (void)event_callback;
    callback = event_callback;
   
    dev.cb = wake_up_cb;
                                                                   
     /* Create handler thread */
    char *stack = (char *) allocate_stack(UMDK_ST95_STACK_SIZE);
    if (!stack) {
        return;
    }

    radio_pid = thread_create(stack, UMDK_ST95_STACK_SIZE, THREAD_PRIORITY_MAIN - 1, THREAD_CREATE_STACKTEST, radio_send, NULL, "st95 thread");
    
    st95_params.iface = ST95_IFACE_UART;

    if(st95_init(&dev, &st95_params) != ST95_OK){
        puts("[umdk-" _UMDK_NAME_ "] st95 driver initialization error");
        return;
    }
    else {   
        puts("[umdk-" _UMDK_NAME_ "] st95 driver initialization success");
        mode = UMDK_ST95_MODE_DETECT_TAG;
        st95_sleep(&dev);
    }
 
}

static inline void reply_code(module_data_t *reply, uint8_t code) 
{
    reply->as_ack = false;
    reply->length = 2;
    reply->data[0] = _UMDK_MID_;
        /* Add reply-code */
    reply->data[1] = code;
}

bool umdk_st95_cmd(module_data_t *cmd, module_data_t *reply)
{         
    puts("\t>>> [UMDK CMD] <<<");
    if(cmd->length < 1) {
        reply_code(reply, UMDK_ST95_ERROR_REPLY);
        return true;        
    }
    
    if(cmd->data[0] == UMDK_ST95_DETECT_TAG) {
        if(cmd->length != 1) {
            reply_code(reply, UMDK_ST95_ERROR_REPLY);
            return true;        
        }
        mode = UMDK_ST95_MODE_DETECT_TAG;
        status = UMDK_ST95_STATUS_PROCCESSING;
        st95_sleep(&dev);
        
        // reply_code(reply, UMDK_ST95_OK_REPLY);
        // return true;
        return false;
    }
    else if(cmd->data[0] == UMDK_ST95_GET_UID) {
        if(cmd->length != 1) {
            reply_code(reply, UMDK_ST95_ERROR_REPLY);
            return true;        
        }
        
        status = UMDK_ST95_STATUS_PROCCESSING;
        
        if(mode == UMDK_ST95_MODE_DETECT_TAG) {
            mode = UMDK_ST95_MODE_GET_UID;
            st95_sleep(&dev);
        }
        else {
            mode = UMDK_ST95_MODE_GET_UID;
            umdk_st95_get_uid();
        } 
        // reply_code(reply, UMDK_ST95_OK_REPLY);
        // return true;       
        return false;       
    }
    else if(cmd->data[0] == UMDK_ST95_READ_DATA) {
        if(cmd->length < 2) {
            reply_code(reply, UMDK_ST95_ERROR_REPLY);
            return true;        
        }
        
        uint16_t length = (cmd->data[1] << 8) | cmd->data[2];
        
        status = UMDK_ST95_STATUS_PROCCESSING;
        
        if(st95_read_data(&dev, ndef_data, length) == ST95_OK) {
            DEBUG("Data [%d]: ", length);
            PRINTBUFF(ndef_data, length);            
            for(uint16_t i = 0; i < length; i++) {
                printf("%c", ndef_data[i]);
            }
            printf("\n");
            reply_code(reply, UMDK_ST95_OK_REPLY);
        }
        else {
            DEBUG("Reading error\n");
            reply_code(reply, UMDK_ST95_ERROR_REPLY); 
        }
        status = UMDK_ST95_STATUS_READY;
        return true;
    }
    else if(cmd->data[0] == UMDK_ST95_WRITE_DATA) {
        uint16_t length = (cmd->data[1] << 8) | cmd->data[2];
        status = UMDK_ST95_STATUS_PROCCESSING;
        memcpy(ndef_data, test_data, length);
        if(st95_write_data(&dev, ndef_data, length) == ST95_OK) {
            DEBUG("Writing completed\n");
            reply_code(reply, UMDK_ST95_OK_REPLY);
        }
        else {
            DEBUG("Writing error\n");
            reply_code(reply, UMDK_ST95_ERROR_REPLY);              
        }      
        
        status = UMDK_ST95_STATUS_READY;
        return true;
    }
    else if (cmd->data[0] == UMDK_ST95_CARD_EMUL){
        st95_set_uid(&dev, &length_uid, uid_full, &sak);
        // return false;       
        reply_code(reply, UMDK_ST95_OK_REPLY);
        return true;
    }
    else {
        reply_code(reply, UMDK_ST95_ERROR_REPLY);
        return true;  
    }
   
    return false;
}


#ifdef __cplusplus
}
#endif
