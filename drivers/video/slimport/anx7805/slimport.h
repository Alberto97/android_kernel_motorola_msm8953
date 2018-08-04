/*
 * Copyright(c) 2012, Analogix semi. All rights reserved.
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

#ifndef _SLIMPORT_H
#define _SLIMPORT_H

#define SW_VERSION 1.14
struct anx7805_platform_data
{
	int gpio_p_dwn;
	int gpio_reset;
	int gpio_int;
	int gpio_cbl_det;
	const char *vdd10_name;
	const char *avdd33_name;
	const char *vdd18_name;
};


#if 0
#define SSC_1
#define EYE_TEST
#define EDID_DEBUG_PRINT
#endif


#define keil_51_code


/*
 * Below five GPIOs are example  for AP to control the Slimport chip ANX7805. 
 * Different AP needs to configure these control pins to corresponding GPIOs of AP.
 */

/*******************Slimport Control************************/
/*
#define SP_TX_PWR_V10_CTRL              (104)//AP IO Control - Power+V12
#define SP_TX_HW_RESET                      (84)//AP IO Control - Reset 
#define SLIMPORT_CABLE_DETECT         (103)//AP IO Input - Cable detect 
#define SP_TX_CHIP_PD_CTRL                (109)//AP IO Control - CHIP_PW_HV 

#define CONFIG_I2C_GPIO
#define SET_INT_MODE_EN
*/
//#define SSC_EN
//#define SSC_1    
//#define HDCP_EN

#ifdef SET_INT_MODE_EN
#ifndef SLEEP_MILLI_SEC  

#define SLEEP_MILLI_SEC(nMilliSec) \
	do { \
		long timeout = (nMilliSec) * HZ / 1000; \
		while(timeout > 0) \
		{ \
			timeout = schedule_timeout(timeout); \
		} \
	}while(0);  
#endif  
#endif




#define D(fmt, arg...) printk("<1>```%s:%d: " fmt, __FUNCTION__, __LINE__, ##arg)
#define debug_printf(fmt, arg...) printk(fmt,##arg)
#define debug_puts(fmt) printk(fmt)
#define delay_ms(time) mdelay(time)

#define MIPI_EN  1
#define EN_3D    0
#define MIPI_LANE_SEL_0  1
#define MIPI_LANE_SEL_1  1
/*Audio interface select*/
/*  AUD_IN_SEL_1/AUD_IN_SEL_2:
 *                      00: SPDIF input
 *                            01: I2S input
 *                                  10: Slimbus input
 *                                  */
#define AUD_IN_SEL_1  0    
#define AUD_IN_SEL_2  1
#define SSC_EN   1
/* Disable HDCP */
#define HDCP_EN   1

#define BIST_EN 0 

#define AUX_ERR  1
#define AUX_OK   0

#define CBL_910K 11

#if defined CONFIG_SLIMPORT_ANX7808
#define LOG_TAG "[anx7808]"
#elif defined CONFIG_SLIMPORT_ANX7816
#define LOG_TAG "[anx7816]"
#elif defined CONFIG_SLIMPORT_ANX7805
#define LOG_TAG "[anx7805]"
#else
#define LOG_TAG "[anxNULL]"
#endif

#ifdef CONFIG_SLIMPORT_ANX7808
extern bool sp_tx_hw_lt_done;
extern bool  sp_tx_hw_lt_enable;
extern bool	sp_tx_link_config_done ;
extern unchar  sp_tx_rx_type_backup;
extern unchar sp_tx_pd_mode;
extern unchar sp_tx_hw_hdcp_en;
extern unchar bedid_break;
#endif
//extern enum SP_TX_System_State sp_tx_system_state;



int sp_read_reg(uint8_t slave_addr, uint8_t offset, uint8_t *buf);
int sp_write_reg(uint8_t slave_addr, uint8_t offset, uint8_t value);
void sp_tx_hardware_poweron(void);
void sp_tx_hardware_powerdown(void);
int slimport_read_edid_block(int block, uint8_t *edid_buf);


#ifdef CONFIG_SLIMPORT_DYNAMIC_HPD
void slimport_set_hdmi_hpd(int on);
#endif

#if defined (CONFIG_SLIMPORT_ANX7808)  || defined (CONFIG_SLIMPORT_ANX7805)
bool slimport_is_connected(void);
#else
static inline bool slimport_is_connected(void)
{
	return false;
}
#endif
#endif
