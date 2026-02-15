/*
 * Copyright (C)2026
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <string.h>

#include "functions/mdc1200.h"
#include "functions/settings.h"
#include "functions/sound.h"
#include "functions/trx.h"
#include "hardware/AT1846S.h"
#include "interfaces/gpio.h"
#include "interfaces/pit.h"

static const uint8_t mdc1200Preamble[MDC1200_PREAMBLE_BYTES] = { 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U };
static const uint8_t mdc1200Sync[MDC1200_SYNC_BYTES] = { 0x07U, 0x09U, 0x2AU, 0x44U, 0x6FU };
static const uint8_t mdc1200SymbolTicks[3] = { 8U, 8U, 9U };
static const uint8_t mdc1200ToneWriteLeadTicks = 4U;

static void mdc1200SetBitTone(bool bitValue)
{
	// Bell 202 style polarity used by MDC1200: 1 = 1200Hz (mark), 0 = 1800Hz (space).
	trxSetTone1(bitValue ? 1200 : 1800);
}

static uint16_t mdc1200CRC16Xmodem(const uint8_t *data, unsigned int dataLength)
{
	uint16_t crc = 0x0000U;

	for (unsigned int i = 0U; i < dataLength; i++)
	{
		crc ^= ((uint16_t)data[i] << 8);
		for (uint8_t bit = 0U; bit < 8U; bit++)
		{
			if ((crc & 0x8000U) != 0U)
			{
				crc = (uint16_t)((crc << 1) ^ 0x1021U);
			}
			else
			{
				crc = (uint16_t)(crc << 1);
			}
		}
	}

	return crc;
}

static uint8_t *mdc1200EncodeData(uint8_t *data)
{
	uint8_t shiftReg = 0U;

	// R=1/2 K=7 convolutional coding.
	for (unsigned int i = 0U; i < MDC1200_FEC_K; i++)
	{
		uint8_t bo = 0U;
		const uint8_t bi = data[i];

		for (uint8_t bit = 0U; bit < 8U; bit++)
		{
			shiftReg = (uint8_t)((shiftReg << 1) | ((bi >> bit) & 1U));
			bo |= (uint8_t)((((shiftReg >> 6) ^ (shiftReg >> 5) ^ (shiftReg >> 2) ^ (shiftReg >> 0)) & 1U) << bit);
		}

		data[MDC1200_FEC_K + i] = bo;
	}

	// Interleave coded payload bits.
	{
		uint8_t interleaved[MDC1200_ENCODED_PAYLOAD_BYTES * 8U];

		for (unsigned int i = 0U, k = 0U; i < MDC1200_ENCODED_PAYLOAD_BYTES; i++)
		{
			const uint8_t b = data[i];

			for (uint8_t bit = 0U; bit < 8U; bit++)
			{
				interleaved[k] = (uint8_t)((b >> bit) & 1U);
				k += 16U;
				if (k >= sizeof(interleaved))
				{
					k -= (sizeof(interleaved) - 1U);
				}
			}
		}

		for (unsigned int i = 0U, k = 0U; i < MDC1200_ENCODED_PAYLOAD_BYTES; i++)
		{
			uint8_t b = 0U;

			for (int bit = 7; bit >= 0; bit--)
			{
				if (interleaved[k++] != 0U)
				{
					b |= (uint8_t)(1U << bit);
				}
			}

			data[i] = b;
		}
	}

	return (data + MDC1200_ENCODED_PAYLOAD_BYTES);
}

static void mdc1200ApplyXorModulation(uint8_t *data, unsigned int size)
{
	uint8_t previousBit = 0U;

	for (unsigned int i = 0U; i < size; i++)
	{
		const uint8_t in = data[i];
		uint8_t out = 0U;

		for (int bit = 7; bit >= 0; bit--)
		{
			const uint8_t newBit = (uint8_t)((in >> bit) & 1U);
			if (newBit != previousBit)
			{
				out |= (uint8_t)(1U << bit);
			}
			previousBit = newBit;
		}

		data[i] = (uint8_t)(out ^ 0xFFU);
	}
}

static bool mdc1200TransmitPacket(const uint8_t *packet, unsigned int packetSize)
{
	if ((packet == NULL) || (packetSize == 0U))
	{
		return false;
	}

	if ((trxGetMode() != RADIO_MODE_ANALOG) || (trxTransmissionEnabled == false))
	{
		return false;
	}

	const unsigned int totalSymbols = (packetSize * 8U);
	bool bitStream[MDC1200_PACKET_BYTES * 8U];
	const bool rfAmpWasEnabled = ((getAudioAmpStatus() & AUDIO_AMP_MODE_RF) != 0U);
	const uint16_t savedTxCss = ((currentChannelData != NULL) ? currentChannelData->txTone : CODEPLUG_CSS_NONE);
	uint8_t savedFilterHigh = 0U;
	uint8_t savedFilterLow = 0U;
	const bool haveSavedFilter = (AT1846SReadReg2byte(0x58, &savedFilterHigh, &savedFilterLow) == kStatus_Success);
	uint32_t nextSymbolTick;
	bool currentBit;

	if (totalSymbols > (sizeof(bitStream) / sizeof(bitStream[0])))
	{
		return false;
	}

	for (unsigned int i = 0U, symbolIndex = 0U; i < packetSize; i++)
	{
		const uint8_t byte = packet[i];
		for (int bit = 7; bit >= 0; bit--)
		{
			bitStream[symbolIndex++] = (((byte >> bit) & 1U) != 0U);
		}
	}

	// Disable TX CTCSS/DCS during MDC to avoid corrupting the FSK burst.
	trxSetTxCSS(CODEPLUG_CSS_NONE);
	// Disable FM TX emphasis/voice filters during MDC FSK to keep the mark/space tones clean.
	AT1846SWriteReg2byte(0x58, 0xBCU, 0xFDU);

	currentBit = bitStream[0];
	mdc1200SetBitTone(currentBit);
	trxSelectVoiceChannel(AT1846_VOICE_CHANNEL_TONE1);
	enableAudioAmp(AUDIO_AMP_MODE_RF);
	GPIO_PinWrite(GPIO_RX_audio_mux, Pin_RX_audio_mux, 1);

	// Give the tone generator one write lead interval to settle before starting symbol timing.
	nextSymbolTick = PITCounter + mdc1200ToneWriteLeadTicks;
	while ((int32_t)(PITCounter - nextSymbolTick) < 0)
	{
	}

	nextSymbolTick = PITCounter + mdc1200SymbolTicks[0];

	for (unsigned int symbolIndex = 0U; symbolIndex < totalSymbols; symbolIndex++)
	{
		const uint32_t toneUpdateTick = (nextSymbolTick - mdc1200ToneWriteLeadTicks);

		while ((int32_t)(PITCounter - toneUpdateTick) < 0)
		{
		}

		if ((symbolIndex + 1U) < totalSymbols)
		{
			const bool nextBit = bitStream[symbolIndex + 1U];
			if (nextBit != currentBit)
			{
				mdc1200SetBitTone(nextBit);
				currentBit = nextBit;
			}
		}

		while ((int32_t)(PITCounter - nextSymbolTick) < 0)
		{
		}

		if ((symbolIndex + 1U) < totalSymbols)
		{
			nextSymbolTick += mdc1200SymbolTicks[((symbolIndex + 1U) % 3U)];
		}
	}

	trxSetTone1(0);
	trxSelectVoiceChannel(AT1846_VOICE_CHANNEL_MIC);
	trxSetTxCSS(savedTxCss);
	if (haveSavedFilter)
	{
		AT1846SWriteReg2byte(0x58, savedFilterHigh, savedFilterLow);
	}
	else
	{
		AT1846SWriteReg2byte(0x58, 0xBCU, 0x05U);
	}

	if (!rfAmpWasEnabled)
	{
		disableAudioAmp(AUDIO_AMP_MODE_RF);
	}

	return true;
}

unsigned int mdc1200EncodeSinglePacket(uint8_t *data, uint8_t op, uint8_t arg, uint16_t unitId)
{
	if (data == NULL)
	{
		return 0U;
	}

	uint8_t *p = data;

	memcpy(p, mdc1200Preamble, sizeof(mdc1200Preamble));
	p += sizeof(mdc1200Preamble);

	memcpy(p, mdc1200Sync, sizeof(mdc1200Sync));
	p += sizeof(mdc1200Sync);

	p[0] = op;
	p[1] = arg;
	p[2] = (uint8_t)((unitId >> 8) & 0xFFU);
	p[3] = (uint8_t)(unitId & 0xFFU);

	{
		const uint16_t crc = mdc1200CRC16Xmodem(p, 4U);
		p[4] = (uint8_t)(crc & 0xFFU);
		p[5] = (uint8_t)((crc >> 8) & 0xFFU);
	}

	p[6] = 0x00U; // Status field for normal PTT/Post ID packet.

	p = mdc1200EncodeData(p);

	const unsigned int size = (unsigned int)(p - data);
	mdc1200ApplyXorModulation(data, size);

	return size;
}

bool mdc1200TransmitEOTPostId(uint16_t unitId)
{
	uint8_t packet[MDC1200_PACKET_BYTES];
	const unsigned int packetSize = mdc1200EncodeSinglePacket(packet, MDC1200_OP_CODE_POST_ID, 0x00U, unitId);

	return mdc1200TransmitPacket(packet, packetSize);
}
