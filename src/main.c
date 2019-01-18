/**
 * @file    main.c
 * @brief   Hello World!
 * @details This example uses the UART to print to a terminal and flashes an LED.
 */

/*******************************************************************************
 * Copyright (C) 2016 Maxim Integrated Products, Inc., All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL MAXIM INTEGRATED BE LIABLE FOR ANY CLAIM, DAMAGES
 * OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Except as contained in this notice, the name of Maxim Integrated
 * Products, Inc. shall not be used except as stated in the Maxim Integrated
 * Products, Inc. Branding Policy.
 *
 * The mere transfer of this software does not imply any licenses
 * of trade secrets, proprietary technology, copyrights, patents,
 * trademarks, maskwork rights, or any other form of intellectual
 * property whatsoever. Maxim Integrated Products, Inc. retains all
 * ownership rights.
 *
 * $Date: 2018-08-09 23:45:02 +0000 (Thu, 09 Aug 2018) $
 * $Revision: 36818 $
 *
 ******************************************************************************/

/***** Includes *****/
#include <stdio.h>
#include <stdint.h>
#include "mxc_config.h"
#include "mxc_delay.h"
#include "tmr.h"
#include "tmr_utils.h"
#include "gpio.h"

/***** Definitions *****/
#define F_CPU	96000000

#define DHT_PIN	PIN_7

#define DHTLIB_OK                   0
#define DHTLIB_ERROR_CHECKSUM       -1
#define DHTLIB_ERROR_TIMEOUT        -2
#define DHTLIB_ERROR_CONNECT        -3
#define DHTLIB_ERROR_ACK_L          -4
#define DHTLIB_ERROR_ACK_H          -5

#define DHTLIB_DHT11_WAKEUP         18
#define DHTLIB_DHT_WAKEUP           1

#define DHTLIB_DHT11_LEADING_ZEROS  1
#define DHTLIB_DHT_LEADING_ZEROS    6

#define DHT11 11
#define DHT22 22
#define DHT21 21
#define AM2301 21

// max timeout is 100 usec.
// For a 16 Mhz proc 100 usec is 1600 clock cycles
// loops using DHTLIB_TIMEOUT use at least 4 clock cycli
// so 100 us takes max 400 loops
// so by dividing F_CPU by 40000 we "fail" as fast as possible
#ifndef F_CPU
#define DHTLIB_TIMEOUT 1000  // ahould be approx. clock/40000
#else
#define DHTLIB_TIMEOUT (F_CPU/40000)
#endif

/***** Globals *****/
float humidity;
float temperature;
uint8_t bits[5];

gpio_cfg_t dht_in = {
		PORT_0,
		DHT_PIN,
		GPIO_FUNC_IN,
		GPIO_PAD_PULL_UP
};

gpio_cfg_t dht_out = {
		PORT_0,
		DHT_PIN,
		GPIO_FUNC_OUT,
		GPIO_PAD_NONE
};
/***** Functions *****/
int8_t DHT_read(uint8_t pin, uint8_t type);
int8_t readSensor(uint8_t pin, uint8_t wakeupDelay, uint8_t leadingZeroBits);

int main(void)
{
	GPIO_Config(&dht_in);
	while (1) {
		int8_t result = DHT_read(DHT_PIN, DHT22);
		printf("\nRead result: %d\n", result);
		printf("Temperature C: %.2f\t", temperature);
		printf("Humidity %: %.2f\n", humidity);
		TMR_Delay(MXC_TMR0, SEC(2), NULL); // 2 sec interval between read
	}
}

int8_t DHT_read(uint8_t pin, uint8_t type)
{
    // READ VALUES
    int8_t result = readSensor(pin, type == DHT11 ? DHTLIB_DHT11_WAKEUP : DHTLIB_DHT_WAKEUP, type == DHT11 ? DHTLIB_DHT11_LEADING_ZEROS : DHTLIB_DHT_LEADING_ZEROS);
    uint8_t sum;
    switch (type) {
    case DHT11:
    	// these bits are always zero, masking them reduces errors.
    	bits[0] &= 0x7F;
    	bits[2] &= 0x7F;

    	// CONVERT AND STORE
    	humidity    = bits[0];  // bits[1] == 0;
    	temperature = bits[2];  // bits[3] == 0;

    	// TEST CHECKSUM
    	sum = bits[0] + bits[1] + bits[2] + bits[3];
    	if (bits[4] != sum)
    	{
    		return DHTLIB_ERROR_CHECKSUM;
    	}
    	break;

    case DHT22:
    case DHT21:
    	// these bits are always zero, masking them reduces errors.
    	bits[0] &= 0x03;
    	bits[2] &= 0x83;

    	// CONVERT AND STORE
    	humidity = (bits[0]*256 + bits[1]) * 0.1;
    	temperature = ((bits[2] & 0x7F)*256 + bits[3]) * 0.1;
    	if (bits[2] & 0x80)  // negative temperature
    	{
    		temperature = -temperature;
    	}

    	// TEST CHECKSUM
    	sum = bits[0] + bits[1] + bits[2] + bits[3];
    	if (bits[4] != sum)
    	{
    		return DHTLIB_ERROR_CHECKSUM;
    	}
    	break;
    }

    return result;
}

int8_t readSensor(uint8_t pin, uint8_t wakeupDelay, uint8_t leadingZeroBits)
{
    // INIT BUFFERVAR TO RECEIVE DATA
    uint8_t mask = 128;
    uint8_t idx = 0;

    uint8_t data = 0;
    uint8_t state = 0;
    uint8_t pstate = 0;
    uint16_t zeroLoop = DHTLIB_TIMEOUT;
    uint16_t delta = 0;

    leadingZeroBits = 40 - leadingZeroBits; // reverse counting...

    // REQUEST SAMPLE
    GPIO_Config(&dht_out);
    GPIO_OutClr(&dht_out); // T-be
    TMR_Delay(MXC_TMR0, MSEC(wakeupDelay), NULL);
    // digitalWrite(pin, HIGH); // T-go
    GPIO_Config(&dht_in);

    uint16_t loopCount = DHTLIB_TIMEOUT * 2;  // 200uSec max
    // while(digitalRead(pin) == HIGH)
    while ((MXC_GPIO0->in & DHT_PIN) != 0 )
    {
        if (--loopCount == 0) return DHTLIB_ERROR_CONNECT;
    }

    // GET ACKNOWLEDGE or TIMEOUT
    loopCount = DHTLIB_TIMEOUT;
    // while(digitalRead(pin) == LOW)
    while ((MXC_GPIO0->in & DHT_PIN) == 0 )  // T-rel
    {
        if (--loopCount == 0) return DHTLIB_ERROR_ACK_L;
    }

    loopCount = DHTLIB_TIMEOUT;
    // while(digitalRead(pin) == HIGH)
    while ((MXC_GPIO0->in & DHT_PIN) != 0 )  // T-reh
    {
        if (--loopCount == 0) return DHTLIB_ERROR_ACK_H;
    }

    loopCount = DHTLIB_TIMEOUT;

    // READ THE OUTPUT - 40 BITS => 5 BYTES
    for (uint8_t i = 40; i != 0; )
    {
        // WAIT FOR FALLING EDGE
        state = (MXC_GPIO0->in & DHT_PIN);
        if (state == 0 && pstate != 0)
        {
            if (i > leadingZeroBits) // DHT22 first 6 bits are all zero !!   DHT11 only 1
            {
                zeroLoop = (zeroLoop > loopCount ? loopCount : zeroLoop);
                delta = (DHTLIB_TIMEOUT - zeroLoop)/4;
            }
            else if ( loopCount <= (zeroLoop - delta) ) // long -> one
            {
                data |= mask;
            }
            mask >>= 1;
            if (mask == 0)   // next byte
            {
                mask = 128;
                bits[idx] = data;
                idx++;
                data = 0;
            }
            // next bit
            --i;

            // reset timeout flag
            loopCount = DHTLIB_TIMEOUT;
        }
        pstate = state;
        // Check timeout
        if (--loopCount == 0)
        {
            return DHTLIB_ERROR_TIMEOUT;
        }

    }

    return DHTLIB_OK;
}
