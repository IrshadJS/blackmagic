/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2018 Uwe Bonnes (bon@elektron.ikp.physik.tu-darmstadt.de)
 * Written by Gareth McMullin <gareth@blacksphere.co.nz>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/* MPSSE bit-banging SW-DP interface over FTDI with loop unrolled.
 * Speed is sensible.
 */

#include <stdio.h>
#include <assert.h>

#include "general.h"
#include "swdptap.h"

static uint8_t olddir = 0;

#define MPSSE_MASK (MPSSE_TDI | MPSSE_TDO | MPSSE_TMS)
#define MPSSE_TD_MASK (MPSSE_TDI | MPSSE_TDO)
#define MPSSE_TMS_SHIFT (MPSSE_WRITE_TMS | MPSSE_LSB |\
						 MPSSE_BITMODE | MPSSE_WRITE_NEG)

int swdptap_init(void)
{
	if (!active_cable->bitbang_tms_in_pin) {
		DEBUG("SWD not possible or missing item in cable description.\n");
		return -1;
	}
	int err = ftdi_usb_purge_buffers(ftdic);
	if (err != 0) {
		fprintf(stderr, "ftdi_usb_purge_buffer: %d: %s\n",
			err, ftdi_get_error_string(ftdic));
		abort();
	}
	/* Reset MPSSE controller. */
	err = ftdi_set_bitmode(ftdic, 0,  BITMODE_RESET);
	if (err != 0) {
		fprintf(stderr, "ftdi_set_bitmode: %d: %s\n",
			err, ftdi_get_error_string(ftdic));
		return -1;;
	}
	/* Enable MPSSE controller. Pin directions are set later.*/
	err = ftdi_set_bitmode(ftdic, 0, BITMODE_MPSSE);
	if (err != 0) {
		fprintf(stderr, "ftdi_set_bitmode: %d: %s\n",
			err, ftdi_get_error_string(ftdic));
		return -1;;
	}
	uint8_t ftdi_init[9] = {TCK_DIVISOR, 0x01, 0x00, SET_BITS_LOW, 0,0,
				SET_BITS_HIGH, 0,0};
	ftdi_init[4]=  active_cable->dbus_data |  MPSSE_MASK;
	ftdi_init[5]= active_cable->dbus_ddr   & ~MPSSE_TD_MASK;
	ftdi_init[7]= active_cable->cbus_data;
	ftdi_init[8]= active_cable->cbus_ddr;
	platform_buffer_write(ftdi_init, 9);
	platform_buffer_flush();

	return 0;
}

static void swdptap_turnaround(uint8_t dir)
{
	if (dir == olddir)
		return;
	olddir = dir;
	uint8_t cmd[6];
	int index = 0;

	if(dir)	  { /* SWDIO goes to input */
		cmd[index++] = SET_BITS_LOW;
		if (active_cable->bitbang_swd_dbus_read_data)
			cmd[index] = active_cable->bitbang_swd_dbus_read_data;
		else
			cmd[index] = active_cable->dbus_data;
		index++;
		cmd[index++] = active_cable->dbus_ddr & ~MPSSE_MASK;
	}
	/* One clock cycle */
	cmd[index++] = MPSSE_TMS_SHIFT;
	cmd[index++] = 0;
	cmd[index++] = 0;
	if (!dir) {
		cmd[index++] = SET_BITS_LOW;
		cmd[index++] = active_cable->dbus_data |  MPSSE_MASK;
		cmd[index++] = active_cable->dbus_ddr  & ~MPSSE_TD_MASK;
	}
	platform_buffer_write(cmd, index);
}

bool swdptap_bit_in(void)
{
	swdptap_turnaround(1);
	uint8_t cmd[4];
	int index = 0;

	cmd[index++] = active_cable->bitbang_tms_in_port_cmd;
	cmd[index++] = MPSSE_TMS_SHIFT;
	cmd[index++] = 0;
	cmd[index++] = 0;
	platform_buffer_write(cmd, index);
	uint8_t data[1];
	platform_buffer_read(data, 1);
	return (data[0] &= active_cable->bitbang_tms_in_pin);
}

void swdptap_bit_out(bool val)
{
	swdptap_turnaround(0);
	uint8_t cmd[3];

	cmd[0] = MPSSE_TMS_SHIFT;
	cmd[1] = 0;
	cmd[2] = (val)? 1 : 0;
	platform_buffer_write(cmd, 3);
}

bool swdptap_seq_in_parity(uint32_t *res, int ticks)
{
	int index = ticks + 1;
	uint8_t cmd[4];
	unsigned int parity = 0;

	cmd[0] = active_cable->bitbang_tms_in_port_cmd;
	cmd[1] = MPSSE_TMS_SHIFT;
	cmd[2] = 0;
	cmd[3] = 0;
	swdptap_turnaround(1);
	while (index--) {
		platform_buffer_write(cmd, 4);
	}
	uint8_t data[33];
	unsigned int ret = 0;
	platform_buffer_read(data, ticks + 1);
	if (data[ticks] & active_cable->bitbang_tms_in_pin)
		parity ^= 1;
	while (ticks--) {
		if (data[ticks] & active_cable->bitbang_tms_in_pin) {
			parity ^= 1;
			ret |= (1 << ticks);
		}
	}
	*res = ret;
	return parity;
}

uint32_t swdptap_seq_in(int ticks)
{
	int index = ticks;
	uint8_t cmd[4];

	cmd[0] = active_cable->bitbang_tms_in_port_cmd;
	cmd[1] = MPSSE_TMS_SHIFT;
	cmd[2] = 0;
	cmd[3] = 0;

	swdptap_turnaround(1);
	while (index--) {
		platform_buffer_write(cmd, 4);
	}
	uint8_t data[32];
	uint32_t ret = 0;
	platform_buffer_read(data, ticks);
	while (ticks--) {
		if (data[ticks] & active_cable->bitbang_tms_in_pin)
			ret |= (1 << ticks);
	}
	return ret;
}

void swdptap_seq_out(uint32_t MS, int ticks)
{
	uint8_t cmd[15];
	unsigned int index = 0;
	swdptap_turnaround(0);
	while (ticks) {
		cmd[index++] = MPSSE_TMS_SHIFT;
		if (ticks >= 7) {
			cmd[index++] = 6;
			cmd[index++] = MS & 0x7f;
			MS >>= 7;
			ticks -= 7;
		} else {
			cmd[index++] = ticks - 1;
			cmd[index++] = MS & 0x7f;
			ticks = 0;
		}
	}
	platform_buffer_write(cmd, index);
}

void swdptap_seq_out_parity(uint32_t MS, int ticks)
{
	uint8_t parity = 0;
	int steps = ticks;
	uint8_t cmd[18];
	unsigned int index = 0;
	uint32_t data = MS;
	swdptap_turnaround(0);
	while (steps) {
		cmd[index++] = MPSSE_TMS_SHIFT;
		if (steps >= 7) {
			cmd[index++] = 6;
			cmd[index++] = data & 0x7f;
			data >>= 7;
			steps -= 7;
		} else {
			cmd[index++] = steps - 1;
			cmd[index++] = data & 0x7f;
			steps = 0;
		}
	}
	while (ticks--) {
		parity ^= MS;
		MS >>= 1;
	}
	cmd[index++] = MPSSE_TMS_SHIFT;
	cmd[index++] = 0;
	cmd[index++] = parity;
	platform_buffer_write(cmd, index);
}
