/*
 *
 * FocalTech fts TouchScreen driver.
 *
 * Copyright (c) 2010-2017, Focaltech Ltd. All rights reserved.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

/*****************************************************************************
*
* File Name: focaltech_upgrade_ft8006u.c
*
* Author: Focaltech Driver Team
*
* Created: 2017-07-22
*
* Abstract:
*
* Reference:
*
*****************************************************************************/

/*****************************************************************************
* 1.Included header files
*****************************************************************************/
#include "focaltech_flash.h"

/*****************************************************************************
* Global variable or extern global variabls/functions
*****************************************************************************/

#define FT8006U_PRAMBOOT_FW_NAME		"FT8006U_Pramboot.bin"

/*****************************************************************************
* Private constant and macro definitions using #define
*****************************************************************************/
#define MAX_BANK_DATA               0x80
#define MAX_GAMMA_LEN               0x180
#define LIC_CHECKSUM_H_OFF          0x01
#define LIC_CHECKSUM_L_OFF          0x00
#define LIC_LCD_ECC_H_OFF           0x05
#define LIC_LCD_ECC_L_OFF           0x04
#define LIC_BANKECC_H_OFF           0x0F
#define LIC_BANKECC_L_OFF           0x0E
#define LIC_REG_2                   0xB2
#define LIC_BANK_START_ADDR         0x0A

static int gamma_enable[] = { 0x042c, 0x91, 0x80, 0x00, 0x12, 0x01 };
bool gamma_has_enable = false;

union short_bits {
	u16 dshort;
	struct bits {
		u16 bit0: 1;
		u16 bit1: 1;
		u16 bit2: 1;
		u16 bit3: 1;
		u16 bit4: 1;
		u16 bit5: 1;
		u16 bit6: 1;
		u16 bit7: 1;
		u16 bit8: 1;
		u16 bit9: 1;
		u16 bit10: 1;
		u16 bit11: 1;
		u16 bit12: 1;
		u16 bit13: 1;
		u16 bit14: 1;
		u16 bit15: 1;
	} bits;
};

/*****************************************************************************
* Static function prototypes
*****************************************************************************/

/* calculate lcd init code ecc */
static int cal_lcdinitcode_ecc(u8 *buf, u16 *ecc_val)
{
	u32 bank_crc_en = 0;
	u8 bank_data[MAX_BANK_DATA] = { 0 };
	u16 bank_len = 0;
	u16 bank_addr = 0;
	u32 bank_num = 0;
	u16 file_len = 0;
	u16 pos = 0;
	int i = 0;
	union short_bits ecc;
	union short_bits ecc_last;
	union short_bits temp_byte;
	u8 bank_mapping[] = {0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7, 0x8,
						0x9, 0xA, 0xB, 0xC, 0xD, 0xE, 0xF, 0x10, 0x11, 0x12, 0x13, 0x14,
						0x18, 0x19, 0x1A, 0x1C, 0x1D, 0x1E, 0x1F, 0x20, 0x22, 0x23, 0x24
						}; /* Actaul mipi bank */
	u8 banknum = 0;

	ecc.dshort = 0;
	ecc_last.dshort = 0;
	temp_byte.dshort = 0;

	file_len = (u16)(((u16)buf[3] << 8) + buf[2]);
	if ((file_len >= FTS_MAX_LEN_SECTOR) || (file_len <= FTS_MIN_LEN)) {
		FTS_ERROR("host lcd init code len(%x) is too large", file_len);
		return -EINVAL;
	}

	bank_crc_en = (u32)(((u32)buf[9] << 24) + ((u32)buf[8] << 16) + \
					((u32)buf[7] << 8) + (u32)buf[6]);
	FTS_INFO("lcd init code len=%x bank en=%x", file_len, bank_crc_en);

	pos = LIC_BANK_START_ADDR; /*  addr of first bank */
	while (pos < file_len) {
		bank_addr = (u16)(((u16)buf[pos + 0] << 8 ) + buf[pos + 1]);
		bank_len = (u16)(((u16)buf[pos + 2] << 8 ) + buf[pos + 3]);
		if (bank_len > MAX_BANK_DATA)
			return -EINVAL;
		memset(bank_data, 0, MAX_BANK_DATA);
		memcpy(bank_data, buf + pos + 4, bank_len);

		bank_num = (bank_addr - 0x8000) / MAX_BANK_DATA;
		if ((bank_num == 0x15) || (bank_num == 0x16))
			bank_num = 0x14;
		if (bank_num == 0x1B)
			bank_num = 0x1A;
		for (i = 0; i < sizeof(bank_mapping) / sizeof(u8); i++) {
			if (bank_num == bank_mapping[i]) {
				banknum = i;
				break;
			}
		}
		if (i >= sizeof(bank_mapping) / sizeof(u8)) {
			FTS_INFO("actual mipi bank(%d) not find in bank mapping, need jump", bank_num);
		} else {
			if ((bank_crc_en >> banknum) & 0x01) {
				for (i = 0; i < bank_len; i++) {
					temp_byte.dshort = (u16)bank_data[i];

					ecc.bits.bit0 = ecc_last.bits.bit8 ^ ecc_last.bits.bit9 ^ ecc_last.bits.bit10 ^ ecc_last.bits.bit11
							^ ecc_last.bits.bit12 ^ ecc_last.bits.bit13 ^ ecc_last.bits.bit14 ^ ecc_last.bits.bit15
							^ temp_byte.bits.bit0 ^ temp_byte.bits.bit1 ^ temp_byte.bits.bit2 ^ temp_byte.bits.bit3
							^ temp_byte.bits.bit4 ^ temp_byte.bits.bit5 ^ temp_byte.bits.bit6 ^ temp_byte.bits.bit7;

					ecc.bits.bit1 = ecc_last.bits.bit9 ^ ecc_last.bits.bit10 ^ ecc_last.bits.bit11 ^ ecc_last.bits.bit12
							^ ecc_last.bits.bit13 ^ ecc_last.bits.bit14 ^ ecc_last.bits.bit15
							^ temp_byte.bits.bit1 ^ temp_byte.bits.bit2 ^ temp_byte.bits.bit3 ^ temp_byte.bits.bit4
							^ temp_byte.bits.bit5 ^ temp_byte.bits.bit6 ^ temp_byte.bits.bit7;

					ecc.bits.bit2 = ecc_last.bits.bit8 ^ ecc_last.bits.bit9 ^ temp_byte.bits.bit0 ^ temp_byte.bits.bit1;

					ecc.bits.bit3 = ecc_last.bits.bit9 ^ ecc_last.bits.bit10 ^ temp_byte.bits.bit1 ^ temp_byte.bits.bit2;

					ecc.bits.bit4 = ecc_last.bits.bit10 ^ ecc_last.bits.bit11 ^ temp_byte.bits.bit2 ^ temp_byte.bits.bit3;

					ecc.bits.bit5 = ecc_last.bits.bit11 ^ ecc_last.bits.bit12 ^ temp_byte.bits.bit3 ^ temp_byte.bits.bit4;

					ecc.bits.bit6 = ecc_last.bits.bit12 ^ ecc_last.bits.bit13 ^ temp_byte.bits.bit4 ^ temp_byte.bits.bit5;

					ecc.bits.bit7 = ecc_last.bits.bit13 ^ ecc_last.bits.bit14 ^ temp_byte.bits.bit5 ^ temp_byte.bits.bit6;

					ecc.bits.bit8 = ecc_last.bits.bit0 ^ ecc_last.bits.bit14 ^ ecc_last.bits.bit15 ^ temp_byte.bits.bit6 ^ temp_byte.bits.bit7;

					ecc.bits.bit9 = ecc_last.bits.bit1 ^ ecc_last.bits.bit15 ^ temp_byte.bits.bit7;

					ecc.bits.bit10 = ecc_last.bits.bit2;

					ecc.bits.bit11 = ecc_last.bits.bit3;

					ecc.bits.bit12 = ecc_last.bits.bit4;

					ecc.bits.bit13 = ecc_last.bits.bit5;

					ecc.bits.bit14 = ecc_last.bits.bit6;

					ecc.bits.bit15 = ecc_last.bits.bit7 ^ ecc_last.bits.bit8 ^ ecc_last.bits.bit9 ^ ecc_last.bits.bit10
							 ^ ecc_last.bits.bit11 ^ ecc_last.bits.bit12 ^ ecc_last.bits.bit13 ^ ecc_last.bits.bit14 ^ ecc_last.bits.bit15
							 ^ temp_byte.bits.bit0 ^ temp_byte.bits.bit1 ^ temp_byte.bits.bit2 ^ temp_byte.bits.bit3
							 ^ temp_byte.bits.bit4 ^ temp_byte.bits.bit5 ^ temp_byte.bits.bit6 ^ temp_byte.bits.bit7;

					ecc_last.dshort = ecc.dshort;

				}
			}
		}
		pos += bank_len + 4;
	}

	*ecc_val = ecc.dshort;
	return 0;
}


/* calculate lcd init code checksum */
static u16 cal_lcdinitcode_checksum(u8 *ptr , int length)
{
	/* CRC16 */
	u16 cfcs = 0;
	int i = 0;
	int j = 0;

	length = (length % 2 == 0) ? length : (length - 1);

	for (i = 0; i < length; i += 2) {
		cfcs ^= ((ptr[i] << 8) + ptr[i + 1]);
		for (j = 0; j < 16; j++) {
			if (cfcs & 1)
				cfcs = (u16)((cfcs >> 1) ^ ((1 << 15) + (1 << 10) + (1 << 3)));
			else
				cfcs >>= 1;
		}
	}
	return cfcs;
}

/*
 * check_initial_code_valid - check initial code valid or not
 */
static int check_initial_code_valid(struct i2c_client *client, u8 *buf)
{
	int ret = 0;
	u16 initcode_ecc = 0;
	u16 buf_ecc = 0;
	u16 initcode_checksum = 0;
	u16 buf_checksum = 0;
	u16 hlic_len = 0;

	hlic_len = (u16)(((u16)buf[3]) << 8) + buf[2];
	if ((hlic_len >= FTS_MAX_LEN_SECTOR) || (hlic_len <= FTS_MIN_LEN)) {
		FTS_ERROR("host lcd init code len(%x) is too large", hlic_len);
		return -EINVAL;
	}

	initcode_checksum = cal_lcdinitcode_checksum(buf + 2, hlic_len - 2);
	buf_checksum = ((u16)((u16)buf[1] << 8) + buf[0]);
	FTS_DEBUG("lcd init code calc checksum:0x%04x,0x%04x", initcode_checksum, buf_checksum);
	if (initcode_checksum != buf_checksum) {
		FTS_ERROR("Initial Code checksum fail");
		return -EINVAL;
	}

	ret = cal_lcdinitcode_ecc(buf, &initcode_ecc);
	if (ret < 0) {
		FTS_ERROR("lcd init code ecc calculate fail");
		return ret;
	}
	buf_ecc = ((u16)((u16)buf[5] << 8) + buf[4]);
	FTS_DEBUG("lcd init code cal ecc:%04x, %04x", initcode_ecc, buf_ecc);
	if (initcode_ecc != buf_ecc) {
		FTS_ERROR("Initial Code ecc check fail");
		return -EINVAL;
	}

	return 0;
}

static int print_data(u8 *buf, u32 len)
{
	int i = 0;
	int n = 0;
	u8 *p = NULL;

	p = kmalloc(len * 4, GFP_KERNEL);
	memset(p, 0, len * 4);

	for (i = 0; i < len; i++)
		n += sprintf(p + n, "%02x ", buf[i]);

	FTS_DEBUG("%s", p);

	kfree(p);
	return 0;
}

/*
 * description : find the address of one bank in initcode
 *
 * parameters :
 *      initcode : initcode
 *      bank_start_addr - the address of one bank, search bank from this address
 *      bank_sign - bank signature, 2 bytes
 *      bank_pos - return the position of the bank
 * return: return 0 if success, otherwise return error code
 */
static int find_bank(
	u8 *initcode,
	u16 bank_start_addr,
	u16 bank_sign,
	u16 *bank_pos)
{
	u16 file_len = 0;
	u16 pos = 0;
	u8 bank[2] = { 0 };

	file_len = (u16)(((u16)initcode[3] << 8) + initcode[2]);
	if ((file_len >= FTS_MAX_LEN_SECTOR) || (file_len <= FTS_MIN_LEN)) {
		FTS_ERROR("host lcd init code len(%x) is too large", file_len);
		return -EINVAL;
	}

	bank[0] = bank_sign >> 8;
	bank[1] = bank_sign;
	pos = bank_start_addr;
	while (pos < file_len) {
		if ((initcode[pos] == bank[0])
			&& (initcode[pos + 1] == bank[1])) {
			FTS_INFO("bank(%x %x) find", bank[0], bank[1]);
			*bank_pos = pos;
			return 0;
		} else {
			pos += ((u16)initcode[pos + 2] << 8 ) + initcode[pos + 3] + 4;
		}
	}

	return -ENODATA;
}

static int read_3gamma(struct i2c_client *client, u8 **gamma, u16 *len)
{
	int ret = 0;
	int i = 0;
	int packet_num = 0;
	int packet_len = 0;
	int remainder = 0;
	u8 cmd[4] = { 0 };
	u32 addr = 0x01D000;
	u8 gamma_header[0x20] = { 0 };
	u16 gamma_len = 0;
	u16 gamma_len_n = 0;
	u16 pos = 0;
	u8 *pgamma = NULL;
	int j = 0;
	u8 gamma_ecc = 0;

	cmd[0] = 0x03;
	cmd[1] = (u8)(addr >> 16);
	cmd[2] = (u8)(addr >> 8);
	cmd[3] = (u8)addr;
	ret = fts_i2c_write(client, cmd, 4);
	msleep(10);
	ret = fts_i2c_read(client, NULL, 0, gamma_header, 0x20);
	if (ret < 0) {
		FTS_ERROR("read 3-gamma header fail");
		return ret;
	}

	gamma_len = (u16)((u16)gamma_header[0] << 8) + gamma_header[1];
	gamma_len_n = (u16)((u16)gamma_header[2] << 8) + gamma_header[3];

	if ((gamma_len + gamma_len_n) != 0xFFFF) {
		FTS_INFO("gamma length check fail:%x %x", gamma_len, gamma_len);
		return -EIO;
	}

	if ((gamma_header[4] + gamma_header[5]) != 0xFF) {
		FTS_INFO("gamma ecc check fail:%x %x", gamma_header[4], gamma_header[5]);
		return -EIO;
	}

	if (gamma_len > MAX_GAMMA_LEN) {
		FTS_ERROR("gamma data len(%d) is too long", gamma_len);
		return -EINVAL;
	}

	*gamma = kmalloc(MAX_GAMMA_LEN, GFP_KERNEL);
	if (NULL == *gamma) {
		FTS_ERROR("malloc gamma memory fail");
		return -ENOMEM;
	}
	pgamma = *gamma;

	packet_num = gamma_len / 256;
	packet_len = 256;
	remainder = gamma_len % 256;
	if (remainder)
		packet_num++;
	FTS_INFO("3-gamma len:%d", gamma_len);
	cmd[0] = 0x03;
	addr += 0x20;
	for (i = 0; i < packet_num; i++) {
		addr += i * 256;
		cmd[1] = (u8)(addr >> 16);
		cmd[2] = (u8)(addr >> 8);
		cmd[3] = (u8)addr;
		if ((i == packet_num - 1) && remainder)
			packet_len = remainder;
		ret = fts_i2c_write(client, cmd, 4);
		msleep(10);
		ret = fts_i2c_read(client, NULL, 0, pgamma + i * 256, packet_len);
		if (ret < 0) {
			FTS_ERROR("read 3-gamma data fail");
			return ret;
		}
	}

	/*  ecc */
	for (j = 0; j < gamma_len; j++)
		gamma_ecc ^= pgamma[j];
	FTS_DEBUG("backup_3gamma_ecc: 0x%x, 0x%x", gamma_ecc, gamma_header[0x04]);
	if (gamma_ecc != gamma_header[0x04]) {
		FTS_ERROR("back gamma ecc check fail:%x %x", gamma_ecc, gamma_header[0x04]);
		return -EIO;
	}

	/* check last byte is 91 80 00 19 01 */
	pos = gamma_len - 5;
	if ((gamma_enable[1] == pgamma[pos]) && (gamma_enable[2] == pgamma[pos + 1])
		&& (gamma_enable[3] == pgamma[pos + 2]) && (gamma_enable[4] == pgamma[pos + 3])) {
		gamma_has_enable = true;
	}

	if (false == gamma_has_enable) {
		FTS_INFO("3-gamma has no gamma enable info");
		pgamma[gamma_len++] = gamma_enable[1];
		pgamma[gamma_len++] = gamma_enable[2];
		pgamma[gamma_len++] = gamma_enable[3];
		pgamma[gamma_len++] = gamma_enable[4];
		pgamma[gamma_len++] = gamma_enable[5];
	}

	*len = gamma_len;

	FTS_DEBUG("read 3-gamma data:");
	print_data(*gamma, gamma_len);

	return 0;
}

static int replace_3gamma(u8 *initcode, u8 *gamma, u16 gamma_len)
{
	int ret = 0;
	u16 gamma_pos = 0;
	int gamma_analog[] = {0x003B, 0x85, 0x00, 0x00, 0x2D, 0x2D};
	int gamma_digital1[] = {0x0374, 0x8D, 0x00, 0x00, 0x80, 0x80};
	int gamma_digital2[] = {0x03F8, 0x8D, 0x80, 0x00, 0x14, 0x14};
	u16 bank_addr = 0;
	u16 bank_saddr = 0;
	u16 bank_sign = 0;
	u16 bank_len = 0;

	/* Analog Gamma */
	bank_saddr = LIC_BANK_START_ADDR;
	bank_sign = ((u16)gamma_analog[1] << 8) + gamma_analog[2];
	ret = find_bank(initcode, LIC_BANK_START_ADDR, bank_sign, &bank_addr);
	if (ret < 0) {
		FTS_ERROR("find bank analog gamma fail");
		goto find_gamma_bank_err;
	}
	bank_len = ((u16)initcode[bank_addr + 2] << 8 ) + initcode[bank_addr + 3];
	memcpy(initcode + bank_addr + 4 , gamma + gamma_pos + 4, bank_len);
	initcode[bank_addr + 4] = 0xA5;
	gamma_pos += bank_len + 4;

	/* Digital1 Gamma */
	bank_saddr = bank_addr;
	bank_sign = ((u16)gamma_digital1[1] << 8) + gamma_digital1[2];
	ret = find_bank(initcode, bank_saddr, bank_sign, &bank_addr);
	if (ret < 0) {
		FTS_ERROR("find bank analog gamma fail");
		goto find_gamma_bank_err;
	}
	bank_len = ((u16)initcode[bank_addr + 2] << 8 ) + initcode[bank_addr + 3];
	memcpy(initcode + bank_addr + 4 , gamma + gamma_pos + 4, bank_len);
	gamma_pos += bank_len + 4;

	/* Digital2 Gamma */
	bank_saddr = bank_addr;
	bank_sign = ((u16)gamma_digital2[1] << 8) + gamma_digital2[2];
	ret = find_bank(initcode, bank_saddr, bank_sign, &bank_addr);
	if (ret < 0) {
		FTS_ERROR("find bank analog gamma fail");
		goto find_gamma_bank_err;
	}
	bank_len = ((u16)initcode[bank_addr + 2] << 8 ) + initcode[bank_addr + 3];
	memcpy(initcode + bank_addr + 4 , gamma + gamma_pos + 4, bank_len);
	gamma_pos += bank_len + 4;

	/* enable Gamma */
	bank_saddr = bank_addr;
	bank_sign = ((u16)gamma_enable[1] << 8) + gamma_enable[2];
	ret = find_bank(initcode, bank_saddr, bank_sign, &bank_addr);
	if (ret < 0) {
		FTS_ERROR("find bank analog gamma fail");
		goto find_gamma_bank_err;
	}
	if (gamma[gamma_pos + 4])
		initcode[bank_addr + 4 + 15] |= 0x01;
	else
		initcode[bank_addr + 4 + 15] &= 0xFE;
	gamma_pos += 1 + 4;

	FTS_DEBUG("replace 3-gamma data:");
	print_data(initcode, 1100);

	return 0;

find_gamma_bank_err:
	FTS_INFO("3-gamma bank(%02x %02x) not find",
			gamma[gamma_pos], gamma[gamma_pos + 1]);
	return -ENODATA;
}

static int cal_replace_ecc(u8 *initcode)
{
	int ret = 0;
	u16 initcode_ecc = 0;
	u16 bank31 = 0x9200;
	u16 bank31_addr = 0;

	ret = cal_lcdinitcode_ecc(initcode, &initcode_ecc);
	if (ret < 0) {
		FTS_ERROR("lcd init code ecc calculate fail");
		return ret;
	}
	FTS_INFO("lcd init code cal ecc:%04x", initcode_ecc);
	initcode[LIC_LCD_ECC_H_OFF] = (u8)(initcode_ecc >> 8);
	initcode[LIC_LCD_ECC_L_OFF] = (u8)(initcode_ecc);
	ret = find_bank(initcode, LIC_BANK_START_ADDR, bank31, &bank31_addr);
	if (ret < 0) {
		FTS_ERROR("find bank 31 fail");
		return ret;
	}
	FTS_INFO("lcd init code ecc bank addr:0x%04x", bank31_addr);
	initcode[bank31_addr + LIC_BANKECC_H_OFF + 4] = (u8)(initcode_ecc >> 8);
	initcode[bank31_addr + LIC_BANKECC_L_OFF + 4] = (u8)(initcode_ecc);

	return 0;
}

/*
 * read_replace_3gamma - read and replace 3-gamma data
 */
static int read_replace_3gamma(struct i2c_client *client, u8 *buf, bool flag)
{
	int ret = 0;
	u16 initcode_checksum = 0;
	u8 *tmpbuf = NULL;
	u8 *gamma = NULL;
	u16 gamma_len = 0;
	u16 hlic_len = 0;
	int base_addr = 0;
	int i = 0;

	FTS_FUNC_ENTER();

	ret = read_3gamma(client, &gamma, &gamma_len);
	if (ret < 0) {
		FTS_INFO("no vaid 3-gamma data, not replace");
		if (gamma) {
			kfree(gamma);
			gamma = NULL;
		}
		return 0;
	}

	base_addr = 0;
	for (i = 0; i < 2; i++) {
		if (i == 1) {
			if (flag == true)
				base_addr = 0x7C0;
			else
				break;
		}

		tmpbuf = buf + base_addr;
		ret = replace_3gamma(tmpbuf, gamma, gamma_len);
		if (ret < 0) {
			FTS_ERROR("replace 3-gamma fail");
			goto REPLACE_GAMMA_ERR;
		}

		ret = cal_replace_ecc(tmpbuf);
		if (ret < 0) {
			FTS_ERROR("lcd init code ecc calculate/replace fail");
			goto REPLACE_GAMMA_ERR;
		}

		hlic_len = (u16)(((u16)tmpbuf[3]) << 8) + tmpbuf[2];
		if ((hlic_len >= FTS_MAX_LEN_SECTOR) || (hlic_len <= FTS_MIN_LEN)) {
			FTS_ERROR("host lcd init code len(%x) is too large", hlic_len);
			ret = -EINVAL;
			goto REPLACE_GAMMA_ERR;
		}
		initcode_checksum = cal_lcdinitcode_checksum(tmpbuf + 2, hlic_len - 2);
		FTS_INFO("lcd init code calc checksum:0x%04x", initcode_checksum);
		tmpbuf[LIC_CHECKSUM_H_OFF] = (u8)(initcode_checksum >> 8);
		tmpbuf[LIC_CHECKSUM_L_OFF] = (u8)(initcode_checksum);
	}

	if (gamma) {
		kfree(gamma);
		gamma = NULL;
	}

	FTS_FUNC_EXIT();
	return 0;

REPLACE_GAMMA_ERR:
	if (gamma) {
		kfree(gamma);
		gamma = NULL;
	}
	return ret;
}

static int fts_ft8006u_upgrade_mode(
	struct i2c_client *client,
	enum FW_FLASH_MODE mode,
	u8 *buf,
	u32 len)
{
	int ret = 0;
	bool flag = false;
	u32 start_addr = 0;
	u8 cmd[4] = { 0 };
	u32 delay = 0;
	int ecc_in_host = 0;
	int ecc_in_tp = 0;

	if ((NULL == buf) || (len < FTS_MIN_LEN)) {
		FTS_ERROR("buffer/len(%x) is invalid", len);
		return -EINVAL;
	}

	/* enter into upgrade environment */
	ret = fts_fwupg_enter_into_boot(client);
	if (ret < 0) {
		FTS_ERROR("enter into pramboot/bootloader fail,ret=%d", ret);
		goto fw_reset;
	}

	cmd[0] = FTS_CMD_FLASH_MODE;
	cmd[1] = FLASH_MODE_UPGRADE_VALUE;
	start_addr = upgrade_func_ft8006u.appoff;
	if (FLASH_MODE_LIC == mode) {
		/* lcd initial code upgrade */
		/* read replace 3-gamma yet   */
		ret = read_replace_3gamma(client, buf, flag);
		if (ret < 0) {
			FTS_ERROR("replace 3-gamma fail, not upgrade lcd init code");
			goto fw_reset;
		}
		cmd[1] = FLASH_MODE_LIC_VALUE;
		start_addr = upgrade_func_ft8006u.licoff;
	} else if (FLASH_MODE_PARAM == mode) {
		cmd[1] = FLASH_MODE_PARAM_VALUE;
		start_addr = upgrade_func_ft8006u.paramcfgoff;
	}
	FTS_DEBUG("flash mode:0x%02x, start addr=0x%04x", cmd[1], start_addr);

	ret = fts_i2c_write(client, cmd, 2);
	if (ret < 0) {
		FTS_ERROR("upgrade mode(09) cmd write fail");
		goto fw_reset;
	}

	delay = FTS_ERASE_SECTOR_DELAY * (len / FTS_MAX_LEN_SECTOR);
	ret = fts_fwupg_erase(client, delay);
	if (ret < 0) {
		FTS_ERROR("erase cmd write fail");
		goto fw_reset;
	}

	/* write app */
	ecc_in_host = fts_flash_write_buf(client, start_addr, buf, len, 1);
	if (ecc_in_host < 0 ) {
		FTS_ERROR("lcd initial code write fail");
		goto fw_reset;
	}

	/* ecc */
	ecc_in_tp = fts_fwupg_ecc_cal(client, start_addr, len);
	if (ecc_in_tp < 0 ) {
		FTS_ERROR("ecc read fail");
		goto fw_reset;
	}

	FTS_INFO("ecc in tp:%x, host:%x", ecc_in_tp, ecc_in_host);
	if (ecc_in_tp != ecc_in_host) {
		FTS_ERROR("ecc check fail");
		goto fw_reset;
	}

	FTS_INFO("upgrade success, reset to normal boot");
	ret = fts_fwupg_reset_in_boot(client);
	if (ret < 0)
		FTS_ERROR("reset to normal boot fail");

	msleep(400);
	return 0;

fw_reset:
	return -EIO;
}

/*
 * fts_ft8006u_get_hlic_ver - read host lcd init code version
 *
 * return 0 if host lcd init code is valid, otherwise return error code
 */
static int fts_ft8006u_get_hlic_ver(u8 *initcode)
{
	u8 *hlic_buf = initcode;
	u16 hlic_len = 0;
	u8 hlic_ver[2] = { 0 };

	hlic_len = (u16)(((u16)hlic_buf[3]) << 8) + hlic_buf[2];
	FTS_INFO("host lcd init code len:%x", hlic_len);
	if ((hlic_len >= FTS_MAX_LEN_SECTOR) || (hlic_len <= FTS_MIN_LEN)) {
		FTS_ERROR("host lcd init code len(%x) is too large", hlic_len);
		return -EINVAL;
	}

	hlic_ver[0] = hlic_buf[hlic_len];
	hlic_ver[1] = hlic_buf[hlic_len + 1];

	FTS_DEBUG("host lcd init code ver:%x %x", hlic_ver[0], hlic_ver[1]);
	if (0xFF != (hlic_ver[0] + hlic_ver[1])) {
		FTS_ERROR("host lcd init code version check fail");
		return -EINVAL;
	}

	return hlic_ver[0];
	}

/************************************************************************
* Name: fts_ft8006u_upgrade
* Brief:
* Input:
* Output:
* Return: return 0 if success, otherwise return error code
***********************************************************************/
static int fts_ft8006u_upgrade(struct i2c_client *client, u8 *buf, u32 len)
{
	int ret = 0;
	u8 *tmpbuf = NULL;
	u32 app_len = 0;

	FTS_DEBUG("fw app upgrade...");
	if (NULL == buf) {
		FTS_ERROR("fw buf is null");
		return -EINVAL;
	}

	if ((len < FTS_MIN_LEN) || (len > FTS_MAX_LEN_FILE)) {
		FTS_ERROR("fw buffer len(%x) fail", len);
		return -EINVAL;
	}

	app_len = len - upgrade_func_ft8006u.appoff;
	tmpbuf = buf + upgrade_func_ft8006u.appoff;
	ret = fts_ft8006u_upgrade_mode(client, FLASH_MODE_APP, tmpbuf, app_len);
	if (ret < 0) {
		FTS_INFO("fw upgrade fail,reset to normal boot");
		if (fts_fwupg_reset_in_boot(client) < 0)
			FTS_ERROR("reset to normal boot fail");
		return ret;
	}

	return 0;
}

/************************************************************************
* Name: fts_ft8006u_lic_upgrade
* Brief:
* Input:
* Output:
* Return: return 0 if success, otherwise return error code
***********************************************************************/
static int fts_ft8006u_lic_upgrade(struct i2c_client *client, u8 *buf, u32 len)
{
	int ret = 0;
	u8 *tmpbuf = NULL;
	u32 lic_len = 0;

	FTS_DEBUG("lcd initial code upgrade...");
	if (NULL == buf) {
		FTS_ERROR("lcd initial code buffer is null");
		return -EINVAL;
	}

	if ((len < FTS_MIN_LEN) || (len > FTS_MAX_LEN_FILE)) {
		FTS_ERROR("lcd initial code buffer len(%x) fail", len);
		return -EINVAL;
	}

	ret = check_initial_code_valid(client, buf);
	if (ret < 0) {
		FTS_ERROR("initial code invalid, not upgrade lcd init code");
		return -EINVAL;
	}

	/* remalloc memory for initcode, need change content of initcode afterwise */
	lic_len = FTS_MAX_LEN_SECTOR;
	tmpbuf = kmalloc(lic_len, GFP_KERNEL);
	if (NULL == tmpbuf) {
		FTS_INFO("initial code buf malloc fail");
		return -EINVAL;
	}
	memcpy(tmpbuf, buf, lic_len);

	ret = fts_ft8006u_upgrade_mode(client, FLASH_MODE_LIC, tmpbuf, lic_len);
	if (ret < 0) {
		FTS_INFO("lcd initial code upgrade fail,reset to normal boot");
		if (fts_fwupg_reset_in_boot(client) < 0)
			FTS_ERROR("reset to normal boot fail");
		if (tmpbuf) {
			kfree(tmpbuf);
			tmpbuf = NULL;
		}
		return ret;
	}

	if (tmpbuf) {
		kfree(tmpbuf);
		tmpbuf = NULL;
	}
	return 0;
}

/************************************************************************
 * Name: fts_ft8006u_param_upgrade
 * Brief:
 * Input: buf - all.bin
 *        len - len of all.bin
 * Output:
 * Return: return 0 if success, otherwise return error code
 ***********************************************************************/
static int fts_ft8006u_param_upgrade(struct i2c_client *client, u8 *buf, u32 len)
{
	int ret = 0;
	u8 *tmpbuf = NULL;
	u32 param_length = 0;

	FTS_DEBUG("parameter configure upgrade...");
	if (NULL == buf) {
		FTS_ERROR("fw file buffer is null");
		return -EINVAL;
	}

	if ((len < FTS_MIN_LEN) || (len > FTS_MAX_LEN_FILE)) {
		FTS_ERROR("fw file buffer len(%x) fail", len);
		return -EINVAL;
	}

	tmpbuf = buf + upgrade_func_ft8006u.paramcfgoff;
	param_length = len - upgrade_func_ft8006u.paramcfgoff;
	ret = fts_ft8006u_upgrade_mode(client, FLASH_MODE_PARAM, tmpbuf, param_length);
	if (ret < 0) {
		FTS_INFO("fw upgrade fail,reset to normal boot");
		if (fts_fwupg_reset_in_boot(client) < 0)
			FTS_ERROR("reset to normal boot fail");
		return ret;
	}

	return 0;
}

/************************************************************************
 * fts_ft8006u_pram_write_remap - write pramboot to pram and start pramboot
 *
 * return 0 if success, otherwise return error code
***********************************************************************/
int fts_ft8006u_pram_write_remap(struct i2c_client *client)
{
	int ret = 0;
	int ecc_in_host = 0;
	int ecc_in_tp = 0;
	u8 *pb_buf = NULL;
	u32 pb_len = 0;
	const struct firmware *fw = NULL;
	//struct fts_upgrade *upg = fwupgrade;

	FTS_INFO("[UPGRADE]write pram and remap\n");

	ret = request_firmware(&fw, FT8006U_PRAMBOOT_FW_NAME, &client->dev);
	if (ret < 0) {
		FTS_ERROR("[UPGRADE] Request pramboot failed - %s (%d)\n",
				FT8006U_PRAMBOOT_FW_NAME, ret);
		return ret;
	}

	if (fw->size < FTS_MIN_LEN) {
		FTS_ERROR("[UPGRADE]pramboot length(%x) fail\n", fw->size);
		return -EINVAL;
	}

	pb_buf = (u8*)fw->data;
	pb_len= (u32)fw->size;

	/* write pramboot to pram */
	ecc_in_host = fts_pram_write_buf(client, pb_buf, pb_len);
	if (ecc_in_host < 0) {
		FTS_ERROR( "write pramboot fail");
		return ecc_in_host;
	}

	/* read out checksum */
	ecc_in_tp = fts_pram_ecc_cal(client, 0, pb_len);
	if (ecc_in_tp < 0) {
		FTS_ERROR( "read pramboot ecc fail");
		return ecc_in_tp;
	}

	FTS_DEBUG("[UPGRADE]pram ecc in tp:%x, host:%x\n", ecc_in_tp, ecc_in_host);
	/*  pramboot checksum != fw checksum, upgrade fail */
	if (ecc_in_host != ecc_in_tp) {
		FTS_ERROR("[UPGRADE]pramboot ecc check fail\n");
		return -EIO;
	}

	/*start pram*/
	ret = fts_pram_start(client);
	if (ret < 0) {
		FTS_ERROR("[UPGRADE]pram start fail\n");
		return ret;
	}

	return 0;
}

struct upgrade_func upgrade_func_ft8006u = {
	.ctype = {0x0B},
	.fwveroff = 0x210E,
	.fwcfgoff = 0x1F80,
	.appoff = 0x2000,
	.licoff = 0x0000,
	.paramcfgoff = 0x12000,
	.paramcfgveroff = 0x12004,
	.pramboot_supported = true,
	.hid_supported = false,
	.fts_8006u = true,
	.upgrade = fts_ft8006u_upgrade,
	.get_hlic_ver = fts_ft8006u_get_hlic_ver,
	.lic_upgrade = fts_ft8006u_lic_upgrade,
	.param_upgrade = fts_ft8006u_param_upgrade,
};
