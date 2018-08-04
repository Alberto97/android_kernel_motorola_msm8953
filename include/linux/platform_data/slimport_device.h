/*
 * Copyright(c) 2016, Motorola Mobility LLC. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef SLIMPORT_DEVICE
#define SLIMPORT_DEVICE
unchar sp_get_rx_bw(void);
int slimport_read_edid_block(int block, uint8_t *edid_buf);
void anx7816_force_mydp_det(bool connected);
#endif /* SLIMPORT_DEVICE */
