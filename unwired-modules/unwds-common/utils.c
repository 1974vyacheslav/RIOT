/*
 * Copyright (C) 2016 Unwired Devices [info@unwds.com]
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

/**
 * @defgroup    
 * @ingroup     
 * @brief       
 * @{
 * @file
 * @brief       
 * @author      Evgeniy Ponomarev
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "fmt.h"
#include "utils.h"
#include "board.h"
#include "unwds-common.h"
#include "periph/gpio.h"
#include "rtctimers-millis.h"

#include "rtctimers.h"

void blink_led(gpio_t led)
{
    /*
    int i;
    for (i = 0; i < 4; i++) {
        gpio_toggle(led);
        rtctimers_millis_sleep(50);
    }
    gpio_clear(led);
    */
    (void)led;
    puts("LED LED LED");
}

void print_logo(void)
{
	puts("*****************************************");
	puts("Unwired Range firmware by Unwired Devices");
	puts("www.unwds.com - info@unwds.com");
#ifdef NO_RIOT_BANNER
    puts("powered by RIOT - www.riot-os.org");
#endif
	puts("*****************************************");
    printf("Version: %s (%s %s)\n", FIRMWARE_VERSION, __DATE__, __TIME__);
    char cpu_model[20];
    switch (get_cpu_category()) {
        case 1:
            snprintf(cpu_model, 20, "STM32L151CB");
            break;
        case 2:
            snprintf(cpu_model, 20, "STM32L151CB-A");
            break;
        case 3:
            snprintf(cpu_model, 20, "STM32L151CC");
            break;
    }
    printf("%s %lu MHz (%s clock)\n", cpu_model,
                                      cpu_clock_global/1000000,
                                      cpu_clock_source);
    printf("%lu KB RAM, %lu KB flash, %lu KB EEPROM\n\n", get_cpu_ram_size()/1024,
                                                          get_cpu_flash_size()/1024,
                                                          get_cpu_eeprom_size()/1024);
}

bool hex_to_bytes(char *hexstr, uint8_t *bytes, bool reverse_order) {
    uint32_t len = strlen(hexstr);
    while(true) {
        if (hexstr[len-1] == '\r' || hexstr[len-1] == '\n') {
            len--;
        } else {
            break;
        }
    }
    
	return hex_to_bytesn(hexstr, len, bytes, reverse_order);
}

static uint8_t ascii_to_number(char ascii) {
    if (ascii < 'A') {
        return (ascii - '0');
    } else if (ascii < 'a') {
        return (ascii - 'A' + 10);
    } else {
        return (ascii - 'a' + 10);
    }
}

static uint8_t hex_to_number(char *hex) {
    return ((ascii_to_number(*hex) << 4) | ascii_to_number(*(hex + 1)));
}

bool hex_to_bytesn(char *hexstr, int len, uint8_t *bytes, bool reverse_order) {
	/* Length must be even */
	if (len % 2 != 0)
		return false;

	/* Move in string by two characters */
	char *ptr = &(*hexstr);
	int i = 0;
	if (reverse_order) {
		ptr += len - 2;

		for (; (len >> 1) - i; ptr -= 2) {
			uint8_t v = 0;
            v = hex_to_number(ptr);
			bytes[i++] = v;
		}
	} else {
		for (; *ptr; ptr += 2) {
			uint8_t v = 0;
			v = hex_to_number(ptr);

			bytes[i++] = v;
		}
	}

	return true;
}

void bytes_to_hex(uint8_t *bytes, size_t num_bytes, char *str, bool reverse_order) {
    if (reverse_order) {
        fmt_bytes_hex_reverse(str, bytes, num_bytes);
    } else {
        fmt_bytes_hex(str, bytes, num_bytes);
    }
}

bool is_number(char* str) {
    char *endptr = NULL;
    strtol(str, &endptr, 0);
    
    if ( &str[strlen(str)] == endptr  ) {
        return true;
    } else {
        return false;
    }
}

#ifdef __cplusplus
}
#endif
