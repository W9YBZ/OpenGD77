/*
 * Copyright (C)2026
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef _OPENGD77_MDC1200_H_
#define _OPENGD77_MDC1200_H_

#include <stdbool.h>
#include <stdint.h>

#define MDC1200_FEC_K                 7U
#define MDC1200_PREAMBLE_BYTES        7U
#define MDC1200_SYNC_BYTES            5U
#define MDC1200_ENCODED_PAYLOAD_BYTES (MDC1200_FEC_K * 2U)
#define MDC1200_PACKET_BYTES          (MDC1200_PREAMBLE_BYTES + MDC1200_SYNC_BYTES + MDC1200_ENCODED_PAYLOAD_BYTES)

typedef enum
{
	MDC1200_OP_CODE_PTT_ID = 0x01,
	MDC1200_OP_CODE_POST_ID = 0x01
} mdc1200OpCode_t;

unsigned int mdc1200EncodeSinglePacket(uint8_t *data, uint8_t op, uint8_t arg, uint16_t unitId);
bool mdc1200TransmitEOTPostId(uint16_t unitId);

#endif
