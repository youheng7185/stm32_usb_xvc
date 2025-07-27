/*
 * xvc.c
 *
 *  Created on: Jul 27, 2025
 *      Author: lapchong
 */


#include "xvc.h"
#include "stm32h7xx_hal.h"
#include "main.h"
#include "usbd_cdc.h"
#include <stdbool.h>

// xvc_vector_len is 512bit
// usbfs can handle 64 byte transfer packet at once

/*
 * only need to implement shift: command here,
 *
 * in endpoint
 * first packet: send the length in binary
 * second until last packet: tdi data
 *
 * out endpoint
 * first until last packet: tdo data
 */

#define TDI_HIGH GPIOG->BSRR = GPIO_BSRR_BS14
#define TDI_LOW  GPIOG->BSRR = GPIO_BSRR_BR14
#define TCK_HIGH GPIOG->BSRR = GPIO_BSRR_BS11
#define TCK_LOW  GPIOG->BSRR = GPIO_BSRR_BR11
#define TMS_HIGH GPIOG->BSRR = GPIO_BSRR_BS13
#define TMS_LOW  GPIOG->BSRR = GPIO_BSRR_BR13

#define TDO_READ ((GPIOG->IDR & GPIO_IDR_ID12) ? 1 : 0)

typedef enum {
	HEADER,
	RECEIVE_DATA_PACKET_TMS,
	RECEIVE_DATA_PACKET_TDI,
	TOGGLE_GPIO_SEND_TO_PC
} XVC_STATE_T;

XVC_STATE_T xvc_state;

uint32_t data_packet_length = 0; // in bytes
uint32_t data_packet_length_bits = 0;
uint8_t tms_data[1024], tdi_data[1024], tdo_data[1024];
uint32_t current_frame_number = 0;
bool done_receive = false;

void parse_receive_data(uint8_t* Buf, uint16_t Len) {
	switch (xvc_state) {
		case HEADER:
			data_packet_length_bits = Buf[0] | (Buf[1] << 8) | (Buf[2] << 16) | (Buf[3] << 24);
			data_packet_length = (data_packet_length_bits + 7) / 8;
			memset(tms_data, 0x00, 1024);
			memset(tdi_data, 0x00, 1024);
			memset(tdo_data, 0x00, 1024);
			xvc_state = RECEIVE_DATA_PACKET_TMS;
			break;

		case RECEIVE_DATA_PACKET_TMS:
			// receive tms data
			for (uint32_t i = 0; i < CDC_DATA_FS_MAX_PACKET_SIZE; i++) {
				tms_data[i + (64 * current_frame_number)] = Buf[i];
			}
			current_frame_number++;
			if (current_frame_number * 64 > data_packet_length) {
				xvc_state = RECEIVE_DATA_PACKET_TDI;
				current_frame_number = 0;
			}
			break;

		case RECEIVE_DATA_PACKET_TDI:
			// receive tdi data
			for (uint32_t i = 0; i < CDC_DATA_FS_MAX_PACKET_SIZE; i++) {
				tdi_data[i + (64 * current_frame_number)] = Buf[i];
			}
			current_frame_number++;
			if (current_frame_number * CDC_DATA_FS_MAX_PACKET_SIZE >= data_packet_length) {
				done_receive = true;
				current_frame_number = 0;
			}

			if (done_receive) {
				// bit bang jtag based on tms and tdi, also read tdo input and store
				for (uint32_t i = 0; i < data_packet_length_bits; i++) {
					TCK_LOW;
					if (tdi_data[i / 8] & (0b1 << (i % 8)) ) {
						TDI_HIGH;
					} else {
						TDI_LOW;
					}
					if (tms_data[i / 8] & (0b1 << (i % 8)) ) {
						TMS_HIGH;
					} else {
						TMS_LOW;
					}
					TCK_HIGH;
					tdo_data[i / 8] |= (TDO_READ << (i % 8));
				}

				CDC_Transmit_HS(&tdo_data, data_packet_length); // send tdo back
				xvc_state = HEADER;
				done_receive = false;
			}
			break;

		default:
			printf("wont reach here\r\n");
			break;
	}
}


