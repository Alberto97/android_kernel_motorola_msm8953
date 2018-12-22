/* Himax Android Driver Sample Code for HX83102 chipset
*
* Copyright (C) 2017 Himax Corporation.
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

#include "himax_ic.h"

extern int i2c_error_count;

extern unsigned long	FW_VER_MAJ_FLASH_ADDR;
extern unsigned long 	FW_VER_MIN_FLASH_ADDR;
extern unsigned long 	CFG_VER_MAJ_FLASH_ADDR;
extern unsigned long 	CFG_VER_MIN_FLASH_ADDR;
extern unsigned long 	CID_VER_MAJ_FLASH_ADDR;
extern unsigned long 	CID_VER_MIN_FLASH_ADDR;

extern unsigned long 	FW_VER_MAJ_FLASH_LENG;
extern unsigned long 	FW_VER_MIN_FLASH_LENG;
extern unsigned long 	CFG_VER_MAJ_FLASH_LENG;
extern unsigned long 	CFG_VER_MIN_FLASH_LENG;
extern unsigned long 	CID_VER_MAJ_FLASH_LENG;
extern unsigned long 	CID_VER_MIN_FLASH_LENG;

#ifdef HX_AUTO_UPDATE_FW
	extern int g_i_FW_VER;
	extern int g_i_CFG_VER;
	extern int g_i_CID_MAJ;
	extern int g_i_CID_MIN;
	extern unsigned char i_CTPM_FW[];
#endif

extern unsigned char	IC_TYPE;
extern unsigned char	IC_CHECKSUM;

extern bool DSRAM_Flag;

extern struct himax_ic_data* ic_data;
extern struct himax_ts_data *private_ts;

int himax_touch_data_size = 128;

#ifdef HX_TP_PROC_2T2R
bool Is_2T2R = false;
#endif

#ifdef HX_USB_DETECT_GLOBAL
//extern kal_bool upmu_is_chr_det(void);
extern void himax_cable_detect_func(bool force_renew);
#endif

int himax_get_touch_data_size(void)
{
	return himax_touch_data_size;
}

#ifdef HX_RST_PIN_FUNC
extern int himax_report_data_init(void);
extern u8 HX_HW_RESET_ACTIVATE;

void himax_pin_reset(void)
{
		I("%s: Now reset the Touch chip.\n", __func__);
		//nothing to be done, need to refer to display
}

void himax_reload_config(void)
{
	if(himax_report_data_init())
		E("%s: allocate data fail\n",__func__);
	himax_power_on_init(private_ts->client);
}

void himax_irq_switch(int switch_on)
{
	int ret = 0;
	if(switch_on)
	{
		
		if (private_ts->use_irq)
			himax_int_enable(private_ts->client->irq,switch_on);
		else
			hrtimer_start(&private_ts->timer, ktime_set(1, 0), HRTIMER_MODE_REL);
	}
	else
	{
		if (private_ts->use_irq)
			himax_int_enable(private_ts->client->irq,switch_on);
		else
		{
			hrtimer_cancel(&private_ts->timer);
			ret = cancel_work_sync(&private_ts->work);
		}
	}
}

void himax_ic_reset(uint8_t loadconfig,uint8_t int_off)
{
	struct himax_ts_data *ts = private_ts;

	HX_HW_RESET_ACTIVATE = 1;

	I("%s,status: loadconfig=%d,int_off=%d\n",__func__,loadconfig,int_off);
	
	if (ts->rst_gpio)
	{
		if(int_off)
		{
			himax_irq_switch(0);
		}

		himax_pin_reset();
		if(loadconfig)
		{
			himax_reload_config();
		}
		if(int_off)
		{
			himax_irq_switch(1);
		}
	}

}
#endif

#if defined(HX_ESD_RECOVERY)
int g_zero_event_count = 0;
int himax_ic_esd_recovery(int hx_esd_event,int hx_zero_event,int length)
{
	
	if(hx_esd_event == length)
	{
		goto checksum_fail;
	}
	else if(hx_zero_event == length)
	{
		g_zero_event_count++;
		I("[HIMAX TP MSG]: ESD event ALL Zero is %d times.\n",g_zero_event_count);
		goto err_workqueue_out;
	}
	
	if(g_zero_event_count > 5)
	{
		I("[HIMAX TP MSG]: ESD event checked - ALL Zero.\n");
		goto checksum_fail;
	}
checksum_fail:
	return CHECKSUM_FAIL;
err_workqueue_out:
	return WORK_OUT;
}
void himax_esd_ic_reset(void)
{
	/*Nothing to do in incell,need to follow display reset*/
}
#endif

int himax_hand_shaking(struct i2c_client *client)    //0:Running, 1:Stop, 2:I2C Fail
{
	int result = 0;
	
	return result;
}

void himax_idle_mode(struct i2c_client *client,int disable)
{
	int retry = 20;
	uint8_t tmp_addr[4];
	uint8_t tmp_data[4];
	uint8_t switch_cmd = 0x00;

	I("%s:entering\n",__func__);
	do{

		I("%s,now %d times\n!",__func__,retry);

		tmp_addr[3] = 0x10; tmp_addr[2] = 0x00; tmp_addr[1] = 0x70; tmp_addr[0] = 0x88;
		himax_register_read(client, tmp_addr, 4, tmp_data, false);
		
		if(disable)
			switch_cmd = 0x17;
		else
			switch_cmd = 0x1F;

		tmp_data[0] = switch_cmd;
		himax_flash_write_burst(client, tmp_addr, tmp_data);

		himax_register_read(client, tmp_addr, 4, tmp_data, false);
		I("%s:After turn ON/OFF IDLE Mode [0] = 0x%02X,[1] = 0x%02X,[2] = 0x%02X,[3] = 0x%02X\n", __func__,tmp_data[0],tmp_data[1],tmp_data[2],tmp_data[3]);

		retry--;
		msleep(10);

	}while((tmp_data[0] != switch_cmd) && retry > 0);
	
	I("%s: setting OK!\n",__func__);
	
}

int himax_write_read_reg(struct i2c_client *client,uint8_t *tmp_addr,uint8_t *tmp_data,uint8_t hb,uint8_t lb)
{
	int cnt = 0;
	
	do{
		himax_flash_write_burst(client, tmp_addr, tmp_data);
		
		msleep(10);
		himax_register_read(client, tmp_addr, 4, tmp_data, false);
		//I("%s:Now tmp_data[0]=0x%02X,[1]=0x%02X,[2]=0x%02X,[3]=0x%02X\n",__func__,tmp_data[0],tmp_data[1],tmp_data[2],tmp_data[3]);
	}while((tmp_data[1] != hb && tmp_data[0] != lb) && cnt++ < 100);
	
	if(cnt == 99)
		return -1;
	
	I("Now register 0x%08X : high byte=0x%02X,low byte=0x%02X\n",tmp_addr[3],tmp_data[1],tmp_data[0]);
	return NO_ERR;
}

int himax_determin_diag_rawdata(int diag_command)
{
	return diag_command%10;
}

int himax_determin_diag_storage(int diag_command)
{
	return diag_command/10;
}

int himax_switch_mode(struct i2c_client *client,int mode)
{
	uint8_t tmp_addr[4];
	uint8_t tmp_data[4];
	uint8_t mode_wirte_cmd;
	uint8_t mode_read_cmd;
	int result = -1;
	int retry = 200;

	I("%s: Entering\n",__func__);
	
	if(mode == 0) /*normal mode*/
	{
		mode_wirte_cmd = 0x00;
		mode_read_cmd = 0x99;
	}
	else		/*sorting mode*/
	{
		mode_wirte_cmd = 0xAA;
		mode_read_cmd = 0xCC;
	}
	
	himax_sense_off(client);

	//himax_interface_on(client);
	
	/*  */
	
	/* clean up FW status */
	tmp_addr[3] = 0x10; tmp_addr[2] = 0x00; tmp_addr[1] = 0x00; tmp_addr[0] = 0x00;
	tmp_data[3] = 0x00; tmp_data[2] = 0x00; tmp_data[1] = 0x00; tmp_data[0] = 0x00;
	himax_flash_write_burst(client, tmp_addr, tmp_data);
	//msleep(30); 
	
	tmp_addr[3] = 0x10; tmp_addr[2] = 0x00; tmp_addr[1] = 0x7F; tmp_addr[0] = 0x04;
	tmp_data[3] = 0x00; tmp_data[2] = 0x00; tmp_data[1] = mode_wirte_cmd; tmp_data[0] = mode_wirte_cmd;
	himax_flash_write_burst(client, tmp_addr, tmp_data);
	//msleep(30);
	
	himax_idle_mode(client,1);
	
	// To stable the sorting
	if(mode)
	{
		tmp_addr[3] = 0x10; tmp_addr[2] = 0x00; tmp_addr[1] = 0x70; tmp_addr[0] = 0xF4;
		tmp_data[3] = 0x00; tmp_data[2] = 0x00; tmp_data[1] = 0x00; tmp_data[0] = 0x08;
		himax_flash_write_burst(client, tmp_addr, tmp_data);
	}
	else
	{
		tmp_addr[3] = 0x10; tmp_addr[2] = 0x00; tmp_addr[1] = 0x70; tmp_addr[0] = 0xF4;
		tmp_data[3] = 0x00; tmp_data[2] = 0x00; tmp_data[1] = 0x00; tmp_data[0] = 0x14;
		himax_flash_write_burst(client, tmp_addr, tmp_data);
	}
	
	himax_sense_on(client,0x01);
	I("mode_wirte_cmd(0)=0x%2.2X,mode_wirte_cmd(1)=0x%2.2X\n",tmp_data[0],tmp_data[1]);
	while(retry!=0)
	{
		I("[%d]Read 10007F04!\n",retry);
		tmp_addr[3] = 0x10; tmp_addr[2] = 0x00; tmp_addr[1] = 0x7F; tmp_addr[0] = 0x04;
		himax_register_read(client, tmp_addr, 4, tmp_data, false);
		msleep(100);
		I("mode_read_cmd(0)=0x%2.2X,mode_read_cmd(1)=0x%2.2X\n",tmp_data[0],tmp_data[1]);
		if(tmp_data[0] == mode_read_cmd && tmp_data[1] == mode_read_cmd)
		{
			I("Read OK!\n");
			result = 0;
			break;
		}
		tmp_addr[3] = 0x90; tmp_addr[2] = 0x00; tmp_addr[1] = 0x00; tmp_addr[0] = 0xA8;
		himax_register_read(client, tmp_addr, 4, tmp_data, false);
		if(tmp_data[0] == 0x00 && tmp_data[1] == 0x00 && tmp_data[2] == 0x00 && tmp_data[3] == 0x00)
		{
			E("%s,: FW Stop!\n",__func__);
			break;
		}
		retry--;
	}

	if(result == 0)
	{
		if(mode == 0)
			return 1;  //normal mode
		else
			return 2;  //sorting mode
	}
	else
		return -1;    //change mode fail
}

void himax_return_event_stack(struct i2c_client *client)
{
	int retry = 20;
	uint8_t tmp_addr[4];
	uint8_t tmp_data[4];
	
	I("%s:entering\n",__func__);
	do{
		
		I("%s,now %d times\n!",__func__,retry);
		tmp_addr[3] = 0x10; tmp_addr[2] = 0x00; tmp_addr[1] = 0x00; tmp_addr[0] = 0x00;
		tmp_data[3] = 0x00; tmp_data[2] = 0x00; tmp_data[1] = 0x00; tmp_data[0] = 0x00;
		himax_flash_write_burst(client, tmp_addr, tmp_data);
		
		himax_register_read(client, tmp_addr, 4, tmp_data, false);
		retry--;
		msleep(10);
		
	}while((tmp_data[1] != 0x00 && tmp_data[0] != 0x00) && retry > 0);
	
	I("%s: End of setting!\n",__func__);
	
}

void himax_diag_register_set(struct i2c_client *client, uint8_t diag_command)
{
	uint8_t tmp_addr[4];
	uint8_t tmp_data[4];

	I("diag_command = %d\n", diag_command );

	himax_interface_on(client);
	
	tmp_addr[3] = 0x80; tmp_addr[2] = 0x02; tmp_addr[1] = 0x04; tmp_addr[0] = 0xB4;
	tmp_data[3] = 0x00; tmp_data[2] = 0x00; tmp_data[1] = 0x00; tmp_data[0] = diag_command;
	himax_flash_write_burst(client, tmp_addr, tmp_data);
	
	himax_register_read(client, tmp_addr, 4, tmp_data, false);
	I("%s: tmp_data[3]=0x%02X,tmp_data[2]=0x%02X,tmp_data[1]=0x%02X,tmp_data[0]=0x%02X!\n",
	__func__,tmp_data[3],tmp_data[2],tmp_data[1],tmp_data[0]);
	
}

void himax_init_psl(struct i2c_client *client) //power saving level 
{
    uint8_t tmp_addr[4];
	uint8_t tmp_data[4];

    //==============================================================
    // SCU_Power_State_PW : 0x9000_00A0 ==> 0x0000_0000 (Reset PSL)
    //==============================================================
    tmp_addr[3] = 0x90; tmp_addr[2] = 0x00; tmp_addr[1] = 0x00; tmp_addr[0] = 0xA0;
    tmp_data[3] = 0x00; tmp_data[2] = 0x00; tmp_data[1] = 0x00; tmp_data[0] = 0x00;
	himax_register_write(client, tmp_addr, 4, tmp_data, false);
	
	I("%s: power saving level reset OK!\n",__func__);
}

void himax_flash_dump_func(struct i2c_client *client, uint8_t local_flash_command, int Flash_Size, uint8_t *flash_buffer)
{
	//struct himax_ts_data *ts = container_of(work, struct himax_ts_data, flash_work);

//	uint8_t sector = 0;
//	uint8_t page = 0;
	uint8_t tmp_addr[4];
	uint8_t tmp_data[4];
	//uint8_t out_buffer[20];
	//uint8_t in_buffer[260];
	uint8_t buffer[256];
	int page_prog_start = 0;
	int cnt = 0;
	//int i = 0;

	himax_sense_off(client);
	himax_burst_enable(client, 0);
	
	/*  init psl */
	/* himax_init_psl(client);*/
	
	/*=============Dump Flash Start=============*/
	//=====================================
	// SPI Transfer Format : 0x8000_0010 ==> 0x0002_0780
	//=====================================
	/*tmp_addr[3] = 0x80; tmp_addr[2] = 0x00; tmp_addr[1] = 0x00; tmp_addr[0] = 0x10;
	tmp_data[3] = 0x00; tmp_data[2] = 0x02; tmp_data[1] = 0x07; tmp_data[0] = 0x80;
	himax_flash_write_burst(client, tmp_addr, tmp_data);*/
	
	
	do
		{
		//===========================================
		// AHB address auto +4		: 0x0D ==> 0x11
		// Do not AHB address auto +4 : 0x0D ==> 0x10
		//===========================================
		tmp_data[0] = (0x11);
		if ( i2c_himax_write(client, 0x0D ,tmp_data, 1, HIMAX_I2C_RETRY_TIMES) < 0) {
			E("%s: i2c access fail!\n", __func__);
			return;
		}
		// Check cmd
		i2c_himax_read(client, 0x0D, tmp_data,1,HIMAX_I2C_RETRY_TIMES);
	   
		if (tmp_data[0] == 0x11)
		{
			break;
		}
		msleep(1);
	} while (++cnt < 10);
	   
	if (cnt > 0)
		I("%s:Polling auto+4 mode: %d times", __func__,cnt);

	for (page_prog_start = 0; page_prog_start < Flash_Size; page_prog_start = page_prog_start + 128)
	{
		//=================================
		// SPI Transfer Control
		// Set 256 bytes page read : 0x8000_0020 ==> 0x6940_02FF
		// Set read start address  : 0x8000_0028 ==> 0x0000_0000
		// Set command			   : 0x8000_0024 ==> 0x0000_003B
		//=================================
		/*tmp_addr[3] = 0x80; tmp_addr[2] = 0x00; tmp_addr[1] = 0x00; tmp_addr[0] = 0x20;
		tmp_data[3] = 0x69; tmp_data[2] = 0x40; tmp_data[1] = 0x02; tmp_data[0] = 0xFF;
		himax_flash_write_burst(client, tmp_addr, tmp_data);

		tmp_addr[3] = 0x80; tmp_addr[2] = 0x00; tmp_addr[1] = 0x00; tmp_addr[0] = 0x28;
		if (page_prog_start < 0x100)
		{
			tmp_data[3] = 0x00;
			tmp_data[2] = 0x00;
			tmp_data[1] = 0x00;
			tmp_data[0] = (uint8_t)page_prog_start;
		}
		else if (page_prog_start >= 0x100 && page_prog_start < 0x10000)
		{
			tmp_data[3] = 0x00;
			tmp_data[2] = 0x00;
			tmp_data[1] = (uint8_t)(page_prog_start >> 8);
			tmp_data[0] = (uint8_t)page_prog_start;
		}
		else if (page_prog_start >= 0x10000 && page_prog_start < 0x1000000)
		{
			tmp_data[3] = 0x00;
			tmp_data[2] = (uint8_t)(page_prog_start >> 16);
			tmp_data[1] = (uint8_t)(page_prog_start >> 8);
			tmp_data[0] = (uint8_t)page_prog_start;
		}
		himax_flash_write_burst(client, tmp_addr, tmp_data);

		tmp_addr[3] = 0x80; tmp_addr[2] = 0x00; tmp_addr[1] = 0x00; tmp_addr[0] = 0x24;
		tmp_data[3] = 0x00; tmp_data[2] = 0x00; tmp_data[1] = 0x00; tmp_data[0] = 0x3B;
		himax_flash_write_burst(client, tmp_addr, tmp_data);

		//==================================
		// AHB_I2C Burst Read
		// Set SPI data register : 0x8000_002C ==> 0x00
		//==================================
		out_buffer[0] = 0x2C;
		out_buffer[1] = 0x00;
		out_buffer[2] = 0x00;
		out_buffer[3] = 0x80;
		i2c_himax_write(client, 0x00 ,out_buffer, 4, 3);

		//==================================
		// Read access : 0x0C ==> 0x00
		//==================================
		out_buffer[0] = 0x00;
		i2c_himax_write(client, 0x0C ,out_buffer, 1, 3);

		//==================================
		// Read 128 bytes two times
		//==================================
		i2c_himax_read(client, 0x08 ,in_buffer, 128, 3);
		for (i = 0; i < 128; i++)
			flash_buffer[i + page_prog_start] = in_buffer[i];

		i2c_himax_read(client, 0x08 ,in_buffer, 128, 3);
		for (i = 0; i < 128; i++)
			flash_buffer[(i + 128) + page_prog_start] = in_buffer[i];

		I("%s:Verify Progress: %x\n", __func__, page_prog_start);*/
		tmp_addr[0] = page_prog_start % 0x100;
		tmp_addr[1] = (page_prog_start >> 8) % 0x100;
		tmp_addr[2] = (page_prog_start >> 16) % 0x100;
		tmp_addr[3] = page_prog_start / 0x1000000;
		himax_register_read(client, tmp_addr ,128,buffer,false);
		memcpy(&flash_buffer[page_prog_start],buffer,128);
		/*himax_register_read(client, tmp_addr ,128,buffer,false);
		memcpy(&flash_buffer[page_prog_start],buffer,128);*/
	}

/*=============Dump Flash End=============*/
		//msleep(100);
		/*
		for( i=0 ; i<8 ;i++)
		{
			for(j=0 ; j<64 ; j++)
			{
				setFlashDumpProgress(i*32 + j);
			}
		}
		*/
		
	do
		{
		//===========================================
		// AHB address auto +4		: 0x0D ==> 0x11
		// Do not AHB address auto +4 : 0x0D ==> 0x10
		//===========================================
		tmp_data[0] = (0x10);
		if ( i2c_himax_write(client, 0x0D ,tmp_data, 1, HIMAX_I2C_RETRY_TIMES) < 0) {
			E("%s: i2c access fail!\n", __func__);
			return;
		}
		// Check cmd
		i2c_himax_read(client, 0x0D, tmp_data,1,HIMAX_I2C_RETRY_TIMES);
	   
		if (tmp_data[0] == 0x10)
		{
			break;
		}
		msleep(1);
	} while (++cnt < 10);
	himax_sense_on(client, 0x01);

	return;

}

int himax_chip_self_test(struct i2c_client *client)
{
	uint8_t tmp_addr[4];
	uint8_t tmp_data[128];
	uint8_t self_test_info[24];
	int pf_value=0x00;
	uint8_t test_result_id = 0;
	int i;

	memset(tmp_addr, 0x00, sizeof(tmp_addr));
	memset(tmp_data, 0x00, sizeof(tmp_data));

	himax_interface_on(client);
	himax_sense_off(client);

	
	himax_burst_enable(client, 1);
	
	// 0x10007f18 -> 0x00006AA6
	tmp_addr[3] = 0x10; tmp_addr[2] = 0x00; tmp_addr[1] = 0x7F; tmp_addr[0] = 0x18;
	tmp_data[3] = 0x00; tmp_data[2] = 0x00; tmp_data[1] = 0x6A; tmp_data[0] = 0xA6;
	himax_flash_write_burst(client, tmp_addr, tmp_data);

	//Set criteria 0x10007F1C [0,1]=aa/up,down=, [2-3]=key/up,down, [4-5]=avg/up,down
	tmp_addr[3] = 0x10; tmp_addr[2] = 0x00; tmp_addr[1] = 0x7F; tmp_addr[0] = 0x1C;
	tmp_data[3] = 0x00; tmp_data[2] = 0xFF; tmp_data[1] = 0x00; tmp_data[0] = 0xFF;
	tmp_data[7] = 0x00; tmp_data[6] = 0x00; tmp_data[5] = 0x00; tmp_data[4] = 0xFF;
	himax_flash_write_burst_lenth(client, tmp_addr, tmp_data, 8);

	// 0x10007294 -> 0x0000190  //SET IIR_MAX FRAMES
	tmp_addr[3] = 0x10; tmp_addr[2] = 0x00; tmp_addr[1] = 0x7F; tmp_addr[0] = 0x00;
	tmp_data[3] = 0x00; tmp_data[2] = 0x00; tmp_data[1] = 0x01; tmp_data[0] = 0x90;
	himax_flash_write_burst(client, tmp_addr, tmp_data);
	
	//Disable IDLE Mode
	himax_idle_mode(client,1);
	
	// 0x10007f00 -> 0x0000A55A //Diable Flash Reload
	tmp_addr[3] = 0x10; tmp_addr[2] = 0x00; tmp_addr[1] = 0x7F; tmp_addr[0] = 0x00;
	tmp_data[3] = 0x00; tmp_data[2] = 0x00; tmp_data[1] = 0xA5; tmp_data[0] = 0x5A;
	himax_flash_write_burst(client, tmp_addr, tmp_data);
	
	
	//start selftest // leave safe mode
	himax_sense_on(client, 1);

	//Hand shaking -> 0x10007f18 waiting 0xA66A
	for(i = 0;i < 1000;i++)
	{
		tmp_addr[3] = 0x10; tmp_addr[2] = 0x00; tmp_addr[1] = 0x7F; tmp_addr[0] = 0x18;
		himax_register_read(client, tmp_addr, 4, tmp_data, false);
		I("%s: tmp_data[0] = 0x%02X,tmp_data[1] = 0x%02X,tmp_data[2] = 0x%02X,tmp_data[3] = 0x%02X, cnt=%d\n", __func__,tmp_data[0],tmp_data[1],tmp_data[2],tmp_data[3],i);
		msleep(10);
		if(tmp_data[1] == 0xA6 && tmp_data[0] == 0x6A )
			{
				I("%s Data ready goto moving data\n", __func__);
				break;
			}
	}

	himax_sense_off(client);
	msleep(20);

	//=====================================
	// Read test result ID : 0x10007f24 ==> bit[2][1][0] = [key][AA][avg] => 0xF = PASS
	//=====================================
	tmp_addr[3] = 0x10; tmp_addr[2] = 0x00; tmp_addr[1] = 0x7F; tmp_addr[0] = 0x24;
	himax_register_read(client, tmp_addr, 24, self_test_info, false);

	test_result_id = self_test_info[0];

	I("%s: check test result, test_result_id=%x, test_result=%x\n", __func__
	,test_result_id,self_test_info[0]);
	
	I("raw top 1 = %d\n",self_test_info[3]*256+self_test_info[2]);
	I("raw top 2 = %d\n",self_test_info[5]*256+self_test_info[4]);
	I("raw top 3 = %d\n",self_test_info[7]*256+self_test_info[6]);
	
	I("raw last 1 = %d\n",self_test_info[9]*256+self_test_info[8]);
	I("raw last 2 = %d\n",self_test_info[11]*256+self_test_info[10]);
	I("raw last 3 = %d\n",self_test_info[13]*256+self_test_info[12]);
	
	I("raw key 1 = %d\n",self_test_info[15]*256+self_test_info[14]);
	I("raw key 2 = %d\n",self_test_info[17]*256+self_test_info[16]);
	I("raw key 3 = %d\n",self_test_info[19]*256+self_test_info[18]);

	if (test_result_id==0xAA) {
		I("[Himax]: self-test pass\n");
		pf_value = 0x1;
	} else {
		E("[Himax]: self-test fail\n");
		/* E("[Himax]: bank_avg = %d, bank_max = %d,%d,%d, bank_min = %d,%d,%d, key = %d,%d,%d\n",
		tmp_data[1],tmp_data[2],tmp_data[3],tmp_data[4],tmp_data[5],tmp_data[6],tmp_data[7],
		tmp_data[8],tmp_data[9],tmp_data[10]); */
		pf_value = 0x0;
	}
	
	//Enable IDLE Mode
	himax_idle_mode(client,0);
	
	// 0x10007f00 -> 0x00000000 //Enable Flash Reload //recovery
	tmp_addr[3] = 0x10; tmp_addr[2] = 0x00; tmp_addr[1] = 0x7F; tmp_addr[0] = 0x00;
	tmp_data[3] = 0x00; tmp_data[2] = 0x00; tmp_data[1] = 0x00; tmp_data[0] = 0x00;
	himax_flash_write_burst(client, tmp_addr, tmp_data);
	
	himax_burst_enable(client, 1);
	himax_sense_on(client, 1);
	msleep(120);

	return pf_value;
}

void himax_set_HSEN_enable(struct i2c_client *client, uint8_t HSEN_enable, bool suspended)
{
	//Not implement in 5442
	uint8_t tmp_addr[4];
	uint8_t tmp_data[4];
	uint8_t retry_cnt = 0;

	do
	{
		himax_burst_enable(client, 0);
		tmp_addr[3] = 0x90; tmp_addr[2] = 0x08; tmp_addr[1] = 0x80; tmp_addr[0] = 0x50;
		tmp_data[3] = 0x00; tmp_data[2] = 0x00; tmp_data[1] = 0x00; tmp_data[0] = HSEN_enable;
		himax_flash_write_burst(client, tmp_addr, tmp_data);

		himax_register_read(client, tmp_addr, 4, tmp_data, false);
		retry_cnt++;
	}while (tmp_data[0] != HSEN_enable && retry_cnt < HIMAX_REG_RETRY_TIMES);
	
}

int himax_palm_detect(uint8_t *buf)
{

	return GESTURE_DETECT_FAIL;

}

void himax_set_SMWP_enable(struct i2c_client *client, uint8_t SMWP_enable, bool suspended)
{
	//Not implement in 5442
	uint8_t tmp_addr[4];
	uint8_t tmp_data[4];
	uint8_t back_data[4];
	uint8_t retry_cnt = 0;

		//Enable:0x10007F10 = 0xA55AA55A
	do
	{
		if(SMWP_enable)
		{
			tmp_addr[3] = 0x10; tmp_addr[2] = 0x00; tmp_addr[1] = 0x7F; tmp_addr[0] = 0x10;  //RAM
			tmp_data[3] = 0xA5; tmp_data[2] = 0x5A; tmp_data[1] = 0xA5; tmp_data[0] = 0x5A;
			himax_flash_write_burst(client, tmp_addr, tmp_data);
			back_data[3] = 0XA5;
			back_data[2] = 0X5A;
			back_data[1] = 0XA5;
			back_data[0] = 0X5A;
		}
		else
		{
			tmp_addr[3] = 0x10; tmp_addr[2] = 0x00; tmp_addr[1] = 0x7F; tmp_addr[0] = 0x10;
			tmp_data[3] = 0x00; tmp_data[2] = 0x00; tmp_data[1] = 0x00; tmp_data[0] = SMWP_enable;
			himax_flash_write_burst(client, tmp_addr, tmp_data);
			back_data[3] = 0X00;
			back_data[2] = 0X00;
			back_data[1] = 0X00;
			back_data[0] = SMWP_enable;
		}
		himax_register_read(client, tmp_addr, 4, tmp_data, false);
		I("%s: set_SMWP_enable tmp_data[0]=%d,tmp_data[1]=%d, tmp_data[2]=%d, tmp_data[3]=%d,  SMWP_enable=%d, retry_cnt=%d \n", __func__, tmp_data[0],tmp_data[1],tmp_data[2],tmp_data[3],SMWP_enable,retry_cnt);
		retry_cnt++;
	}while((tmp_data[3] != back_data[3] || tmp_data[2] != back_data[2] || tmp_data[1] != back_data[1]  || tmp_data[0] != back_data[0] ) && retry_cnt < HIMAX_REG_RETRY_TIMES);
	
}

void himax_usb_detect_set(struct i2c_client *client,uint8_t *cable_config)
{
	//Not implement in 5442
	//I("%s: cable_config[0]=%d, cable_config[1]=%d \n", __func__, cable_config[0],cable_config[1]);
}

void himax_burst_enable(struct i2c_client *client, uint8_t auto_add_4_byte)
{
	uint8_t tmp_data[4];

	tmp_data[0] = 0x31;
	if ( i2c_himax_write(client, 0x13 ,tmp_data, 1, HIMAX_I2C_RETRY_TIMES) < 0) {
		E("%s: i2c access fail!\n", __func__);
		return;
	}
	
	tmp_data[0] = (0x10 | auto_add_4_byte);
	if ( i2c_himax_write(client, 0x0D ,tmp_data, 1, HIMAX_I2C_RETRY_TIMES) < 0) {
		E("%s: i2c access fail!\n", __func__);
		return;
	}

	//isBusrtOn = true;
}

void himax_register_read(struct i2c_client *client, uint8_t *read_addr, int read_length, uint8_t *read_data, bool cfg_flag)
{
	uint8_t tmp_data[4];
	int i = 0;
	int address = 0;

	if(read_length>256)
	{
		E("%s: read len over 256!\n", __func__);
		return;
	}
	if (read_length > 4)
		himax_burst_enable(client, 1);
	else
		himax_burst_enable(client, 0);

	address = (read_addr[3] << 24) + (read_addr[2] << 16) + (read_addr[1] << 8) + read_addr[0];
	i = address;
	tmp_data[0] = (uint8_t)i;
	tmp_data[1] = (uint8_t)(i >> 8);
	tmp_data[2] = (uint8_t)(i >> 16);
	tmp_data[3] = (uint8_t)(i >> 24);
	if ( i2c_himax_write(client, 0x00 ,tmp_data, 4, HIMAX_I2C_RETRY_TIMES) < 0) {
			E("%s: i2c access fail!\n", __func__);
			return;
	}
	tmp_data[0] = 0x00;
	if ( i2c_himax_write(client, 0x0C ,tmp_data, 1, HIMAX_I2C_RETRY_TIMES) < 0) {
			E("%s: i2c access fail!\n", __func__);
			return;
	}

	if ( i2c_himax_read(client, 0x08 ,read_data, read_length, HIMAX_I2C_RETRY_TIMES) < 0) {
		E("%s: i2c access fail!\n", __func__);
		return;
	}
	if (read_length > 4)
		himax_burst_enable(client, 0);
}

void himax_flash_write_burst(struct i2c_client *client, uint8_t * reg_byte, uint8_t * write_data)
{
    uint8_t data_byte[8];
	int i = 0, j = 0;

     for (i = 0; i < 4; i++)
     { 
         data_byte[i] = reg_byte[i];
     }
     for (j = 4; j < 8; j++)
     {
         data_byte[j] = write_data[j-4];
     }
	 
	 if ( i2c_himax_write(client, 0x00 ,data_byte, 8, HIMAX_I2C_RETRY_TIMES) < 0) {
		 E("%s: i2c access fail!\n", __func__);
		 return;
	 }

}

void himax_flash_write_burst_lenth(struct i2c_client *client, uint8_t *reg_byte, uint8_t *write_data, int length)
{
    uint8_t data_byte[256];
	int i = 0, j = 0;

    for (i = 0; i < 4; i++)
    {
        data_byte[i] = reg_byte[i];
    }
    for (j = 4; j < length + 4; j++)
    {
        data_byte[j] = write_data[j - 4];
    }
   
	if ( i2c_himax_write(client, 0x00 ,data_byte, length + 4, HIMAX_I2C_RETRY_TIMES) < 0) {
		 E("%s: i2c access fail!\n", __func__);
		 return;
	}
}

void himax_register_write(struct i2c_client *client, uint8_t *write_addr, int write_length, uint8_t *write_data, bool cfg_flag)
{
	int i =0, address = 0;

	address = (write_addr[3] << 24) + (write_addr[2] << 16) + (write_addr[1] << 8) + write_addr[0];

	for (i = address; i < address + write_length; i++)
	{
		if (write_length > 4)
		{
			himax_burst_enable(client, 1);
		}
		else
		{
			himax_burst_enable(client, 0);
		}
		himax_flash_write_burst_lenth(client, write_addr, write_data, write_length);
	}
}

bool himax_sense_off(struct i2c_client *client)
{
	uint8_t cnt = 0;
	uint8_t tmp_addr[4];
	uint8_t tmp_data[4];

	do
	{
		//===========================================
		//  0x31 ==> 0x27
		//===========================================
		tmp_data[0] = 0x27;
		if ( i2c_himax_write(client, 0x31 ,tmp_data, 1, HIMAX_I2C_RETRY_TIMES) < 0) {
			E("%s: i2c access fail!\n", __func__);
			return false;
		}
		//===========================================
		//  0x32 ==> 0x95
		//===========================================
		tmp_data[0] = 0x95;
		if ( i2c_himax_write(client, 0x32 ,tmp_data, 1, HIMAX_I2C_RETRY_TIMES) < 0) {
			E("%s: i2c access fail!\n", __func__);
			return false;
		}

		// ======================
		// Check enter_save_mode
		// ======================
		tmp_addr[3] = 0x90; tmp_addr[2] = 0x00; tmp_addr[1] = 0x00; tmp_addr[0] = 0xA8;
		himax_register_read(client, tmp_addr, 4, tmp_data, false);

		I("%s: Check enter_save_mode data[0]=%X \n", __func__,tmp_data[0]);

		if (tmp_data[0] == 0x0C)
		{
			//=====================================
			// Reset TCON
			//=====================================
			tmp_addr[3] = 0x80; tmp_addr[2] = 0x02; tmp_addr[1] = 0x00; tmp_addr[0] = 0x20;
			tmp_data[3] = 0x00; tmp_data[2] = 0x00; tmp_data[1] = 0x00; tmp_data[0] = 0x00;
			himax_flash_write_burst(client, tmp_addr, tmp_data);
			msleep(1);
			tmp_data[3] = 0x00; tmp_data[2] = 0x00; tmp_data[1] = 0x00; tmp_data[0] = 0x01;
			himax_flash_write_burst(client, tmp_addr, tmp_data);
			
			//=====================================
			// Reset ADC
			//=====================================
			tmp_addr[3] = 0x80; tmp_addr[2] = 0x02; tmp_addr[1] = 0x00; tmp_addr[0] = 0x94;
			tmp_data[3] = 0x00; tmp_data[2] = 0x00; tmp_data[1] = 0x00; tmp_data[0] = 0x00;
			himax_flash_write_burst(client, tmp_addr, tmp_data);
			msleep(1);
			tmp_data[3] = 0x00; tmp_data[2] = 0x00; tmp_data[1] = 0x00; tmp_data[0] = 0x01;
			himax_flash_write_burst(client, tmp_addr, tmp_data);
			return true;
		}
		else
			msleep(10);
	} while (cnt++ < 15);

	return false;
}

void himax_interface_on(struct i2c_client *client)
{
	uint8_t tmp_data[5];
	uint8_t tmp_data2[2];
	int cnt = 0;

	//Read a dummy register to wake up I2C.
	if ( i2c_himax_read(client, 0x08, tmp_data,4,HIMAX_I2C_RETRY_TIMES) < 0) {// to knock I2C
		E("%s: i2c access fail!\n", __func__);
		return;
	}

	do
		{
		//===========================================
		// Enable continuous burst mode : 0x13 ==> 0x31
		//===========================================
		tmp_data[0] = 0x31;
		if ( i2c_himax_write(client, 0x13 ,tmp_data, 1, HIMAX_I2C_RETRY_TIMES) < 0) {
			E("%s: i2c access fail!\n", __func__);
			return;
		}
		//===========================================
		// AHB address auto +4		: 0x0D ==> 0x11
		// Do not AHB address auto +4 : 0x0D ==> 0x10
		//===========================================
		tmp_data[0] = (0x10);
		if ( i2c_himax_write(client, 0x0D ,tmp_data, 1, HIMAX_I2C_RETRY_TIMES) < 0) {
			E("%s: i2c access fail!\n", __func__);
			return;
		}

		// Check cmd
		i2c_himax_read(client, 0x13, tmp_data,1,HIMAX_I2C_RETRY_TIMES);
		i2c_himax_read(client, 0x0D, tmp_data2,1,HIMAX_I2C_RETRY_TIMES);
	   
		if (tmp_data[0] == 0x31 && tmp_data2[0] == 0x10)
		{
			//isBusrtOn = true;
			break;
		}
		msleep(1);
	} while (++cnt < 10);
	   
	if (cnt > 0)
		I("%s:Polling burst mode: %d times", __func__,cnt);
	   
}

bool wait_wip(struct i2c_client *client, int Timing)
{
	uint8_t tmp_addr[4];
	uint8_t tmp_data[4];
	uint8_t in_buffer[10];
	//uint8_t out_buffer[20];
	int retry_cnt = 0;

	//=====================================
	// SPI Transfer Format : 0x8000_0010 ==> 0x0002_0780
	//=====================================
	tmp_addr[3] = 0x80; tmp_addr[2] = 0x00; tmp_addr[1] = 0x00; tmp_addr[0] = 0x10;
	tmp_data[3] = 0x00; tmp_data[2] = 0x02; tmp_data[1] = 0x07; tmp_data[0] = 0x80;
	himax_flash_write_burst(client, tmp_addr, tmp_data);

	in_buffer[0] = 0x01;

	do
	{
		//=====================================
		// SPI Transfer Control : 0x8000_0020 ==> 0x4200_0003
		//=====================================
		tmp_addr[3] = 0x80; tmp_addr[2] = 0x00; tmp_addr[1] = 0x00; tmp_addr[0] = 0x20;
		tmp_data[3] = 0x42; tmp_data[2] = 0x00; tmp_data[1] = 0x00; tmp_data[0] = 0x03;
		himax_flash_write_burst(client, tmp_addr, tmp_data);

		//=====================================
		// SPI Command : 0x8000_0024 ==> 0x0000_0005
		// read 0x8000_002C for 0x01, means wait success
		//=====================================
		tmp_addr[3] = 0x80; tmp_addr[2] = 0x00; tmp_addr[1] = 0x00; tmp_addr[0] = 0x24;
		tmp_data[3] = 0x00; tmp_data[2] = 0x00; tmp_data[1] = 0x00; tmp_data[0] = 0x05;
		himax_flash_write_burst(client, tmp_addr, tmp_data);

		in_buffer[0] = in_buffer[1] = in_buffer[2] = in_buffer[3] = 0xFF;
		tmp_addr[3] = 0x80; tmp_addr[2] = 0x00; tmp_addr[1] = 0x00; tmp_addr[0] = 0x2C;
		himax_register_read(client, tmp_addr, 4, in_buffer, false);
		
		if ((in_buffer[0] & 0x01) == 0x00)
			return true;

		retry_cnt++;
		
		if (in_buffer[0] != 0x00 || in_buffer[1] != 0x00 || in_buffer[2] != 0x00 || in_buffer[3] != 0x00)
        	I("%s:Wait wip retry_cnt:%d, buffer[0]=%d, buffer[1]=%d, buffer[2]=%d, buffer[3]=%d \n", __func__, 
            retry_cnt,in_buffer[0],in_buffer[1],in_buffer[2],in_buffer[3]);

		if (retry_cnt > 100)
        {        	
			E("%s: Wait wip error!\n", __func__);
            return false;
        }
		msleep(Timing);
	}while ((in_buffer[0] & 0x01) == 0x01);
	return true;
}

void himax_sense_on(struct i2c_client *client, uint8_t FlashMode)
{
	uint8_t tmp_addr[4];
	uint8_t tmp_data[4];
	//int status = 0;
	int retry = 0;
	
	I("Enter %s  \n", __func__);
	
	himax_interface_on(client);

	
	do
	{
		tmp_addr[3] = 0x90; tmp_addr[2] = 0x00; tmp_addr[1] = 0x00; tmp_addr[0] = 0x98;
		tmp_data[3] = 0x00; tmp_data[2] = 0x00; tmp_data[1] = 0x00; tmp_data[0] = 0x53;
		himax_register_write(client, tmp_addr, 4, tmp_data, false);
	
		tmp_addr[0] = 0xE4;
		himax_register_read(client, tmp_addr, 4, tmp_data, false);
		
		I("%s:Read status from IC = %X,%X\n", __func__, tmp_data[0],tmp_data[1]);
		
	}while((tmp_data[1] != 0x01 || tmp_data[0] != 0x00) && retry++ < 5);

	if(retry >= 5)
	{
		E("%s: Fail:\n", __func__);
		//===AHBI2C_SystemReset==========
		tmp_addr[3] = 0x90; tmp_addr[2] = 0x00; tmp_addr[1] = 0x00; tmp_addr[0] = 0x18;
		tmp_data[3] = 0x00; tmp_data[2] = 0x00; tmp_data[1] = 0x00; tmp_data[0] = 0x55;
		himax_register_write(client, tmp_addr, 4, tmp_data, false);
	}
	else
	{
		I("%s:OK and Read status from IC = %X,%X\n", __func__, tmp_data[0],tmp_data[1]);

		/* reset code*/
		tmp_data[0] = 0x00;
		if ( i2c_himax_write(client, 0x31 ,tmp_data, 1, HIMAX_I2C_RETRY_TIMES) < 0){
			E("%s: i2c access fail!\n", __func__);
		}
		if ( i2c_himax_write(client, 0x32 ,tmp_data, 1, HIMAX_I2C_RETRY_TIMES) < 0){
			E("%s: i2c access fail!\n", __func__);
		}

		tmp_addr[3] = 0x90; tmp_addr[2] = 0x00; tmp_addr[1] = 0x00; tmp_addr[0] = 0x98;
		tmp_data[3] = 0x00; tmp_data[2] = 0x00; tmp_data[1] = 0x00; tmp_data[0] = 0x00;
		himax_register_write(client, tmp_addr, 4, tmp_data, false);
	}

}

void himax_chip_erase(struct i2c_client *client)
{
	uint8_t tmp_addr[4];
	uint8_t tmp_data[4];

	himax_interface_on(client);

	/* init psl */
	himax_init_psl(client);
	
	//=====================================
	// SPI Transfer Format : 0x8000_0010 ==> 0x0002_0780
	//=====================================
	tmp_addr[3] = 0x80; tmp_addr[2] = 0x00; tmp_addr[1] = 0x00; tmp_addr[0] = 0x10;
	tmp_data[3] = 0x00; tmp_data[2] = 0x02; tmp_data[1] = 0x07; tmp_data[0] = 0x80;
	himax_flash_write_burst(client, tmp_addr, tmp_data);

	//=====================================
	// Chip Erase
	// Write Enable : 1. 0x8000_0020 ==> 0x4700_0000
	//				  2. 0x8000_0024 ==> 0x0000_0006
	//=====================================
	tmp_addr[3] = 0x80; tmp_addr[2] = 0x00; tmp_addr[1] = 0x00; tmp_addr[0] = 0x20;
	tmp_data[3] = 0x47; tmp_data[2] = 0x00; tmp_data[1] = 0x00; tmp_data[0] = 0x00;
	himax_flash_write_burst(client, tmp_addr, tmp_data);

	tmp_addr[3] = 0x80; tmp_addr[2] = 0x00; tmp_addr[1] = 0x00; tmp_addr[0] = 0x24;
	tmp_data[3] = 0x00; tmp_data[2] = 0x00; tmp_data[1] = 0x00; tmp_data[0] = 0x06;
	himax_flash_write_burst(client, tmp_addr, tmp_data);

	//=====================================
	// Chip Erase
	// Erase Command : 0x8000_0024 ==> 0x0000_00C7
	//=====================================
	tmp_addr[3] = 0x80; tmp_addr[2] = 0x00; tmp_addr[1] = 0x00; tmp_addr[0] = 0x24;
	tmp_data[3] = 0x00; tmp_data[2] = 0x00; tmp_data[1] = 0x00; tmp_data[0] = 0xC7;
	himax_flash_write_burst(client, tmp_addr, tmp_data);
	
	msleep(2000);
	
	if (!wait_wip(client, 100))
		E("%s:83102_Chip_Erase Fail\n", __func__);

}

bool himax_block_erase(struct i2c_client *client)
{
	uint8_t tmp_addr[4];
	uint8_t tmp_data[4];

	himax_burst_enable(client, 0);

	//=====================================
	// SPI Transfer Format : 0x8000_0010 ==> 0x0002_0780
	//=====================================
	tmp_addr[3] = 0x80; tmp_addr[2] = 0x00; tmp_addr[1] = 0x00; tmp_addr[0] = 0x10;
	tmp_data[3] = 0x00; tmp_data[2] = 0x02; tmp_data[1] = 0x07; tmp_data[0] = 0x80;
	himax_flash_write_burst(client, tmp_addr, tmp_data);

	//=====================================
	// Chip Erase
	// Write Enable : 1. 0x8000_0020 ==> 0x4700_0000
	//				  2. 0x8000_0024 ==> 0x0000_0006
	//=====================================
	tmp_addr[3] = 0x80; tmp_addr[2] = 0x00; tmp_addr[1] = 0x00; tmp_addr[0] = 0x20;
	tmp_data[3] = 0x47; tmp_data[2] = 0x00; tmp_data[1] = 0x00; tmp_data[0] = 0x00;
	himax_flash_write_burst(client, tmp_addr, tmp_data);

	tmp_addr[3] = 0x80; tmp_addr[2] = 0x00; tmp_addr[1] = 0x00; tmp_addr[0] = 0x24;
	tmp_data[3] = 0x00; tmp_data[2] = 0x00; tmp_data[1] = 0x00; tmp_data[0] = 0x06;
	himax_flash_write_burst(client, tmp_addr, tmp_data);

	//=====================================
	// Block Erase
	// Erase Command : 0x8000_0028 ==> 0x0000_0000 //SPI addr
	//				   0x8000_0020 ==> 0x6700_0000 //control
	//				   0x8000_0024 ==> 0x0000_0052 //BE
	//=====================================
	tmp_addr[3] = 0x80; tmp_addr[2] = 0x00; tmp_addr[1] = 0x00; tmp_addr[0] = 0x28;
	tmp_data[3] = 0x00; tmp_data[2] = 0x00; tmp_data[1] = 0x00; tmp_data[0] = 0x00;
	himax_flash_write_burst(client, tmp_addr, tmp_data);
	
	tmp_addr[3] = 0x80; tmp_addr[2] = 0x00; tmp_addr[1] = 0x00; tmp_addr[0] = 0x20;
	tmp_data[3] = 0x67; tmp_data[2] = 0x00; tmp_data[1] = 0x00; tmp_data[0] = 0x00;
	himax_flash_write_burst(client, tmp_addr, tmp_data);
	
	tmp_addr[3] = 0x80; tmp_addr[2] = 0x00; tmp_addr[1] = 0x00; tmp_addr[0] = 0x24;
	tmp_data[3] = 0x00; tmp_data[2] = 0x00; tmp_data[1] = 0x00; tmp_data[0] = 0x52;
	himax_flash_write_burst(client, tmp_addr, tmp_data);

	msleep(1000);

	if (!wait_wip(client, 100))
	{
		E("%s:83102_Erase Fail\n", __func__);
		return false;
	}
	else
	{
		return true;
	}

}

bool himax_sector_erase(struct i2c_client *client, int start_addr)
{
	uint8_t tmp_addr[4];
	uint8_t tmp_data[4];
	int page_prog_start = 0;

	himax_burst_enable(client, 0);

	//=====================================
	// SPI Transfer Format : 0x8000_0010 ==> 0x0002_0780
	//=====================================
	tmp_addr[3] = 0x80; tmp_addr[2] = 0x00; tmp_addr[1] = 0x00; tmp_addr[0] = 0x10;
	tmp_data[3] = 0x00; tmp_data[2] = 0x02; tmp_data[1] = 0x07; tmp_data[0] = 0x80;
	himax_flash_write_burst(client, tmp_addr, tmp_data);
	for (page_prog_start = start_addr; page_prog_start < start_addr + 0x0F000; page_prog_start = page_prog_start + 0x1000)
		{
			//=====================================
			// Chip Erase
			// Write Enable : 1. 0x8000_0020 ==> 0x4700_0000
			//				  2. 0x8000_0024 ==> 0x0000_0006
			//=====================================
			tmp_addr[3] = 0x80; tmp_addr[2] = 0x00; tmp_addr[1] = 0x00; tmp_addr[0] = 0x20;
			tmp_data[3] = 0x47; tmp_data[2] = 0x00; tmp_data[1] = 0x00; tmp_data[0] = 0x00;
			himax_flash_write_burst(client, tmp_addr, tmp_data);

			tmp_addr[3] = 0x80; tmp_addr[2] = 0x00; tmp_addr[1] = 0x00; tmp_addr[0] = 0x24;
			tmp_data[3] = 0x00; tmp_data[2] = 0x00; tmp_data[1] = 0x00; tmp_data[0] = 0x06;
			himax_flash_write_burst(client, tmp_addr, tmp_data);

			//=====================================
			// Sector Erase
			// Erase Command : 0x8000_0028 ==> 0x0000_0000 //SPI addr
			// 				0x8000_0020 ==> 0x6700_0000 //control
			// 				0x8000_0024 ==> 0x0000_0020 //SE
			//=====================================
			tmp_addr[3] = 0x80; tmp_addr[2] = 0x00; tmp_addr[1] = 0x00; tmp_addr[0] = 0x28;
			if (page_prog_start < 0x100)
			{
			 tmp_data[3] = 0x00; tmp_data[2] = 0x00; tmp_data[1] = 0x00; tmp_data[0] = (uint8_t)page_prog_start;
			}
			else if (page_prog_start >= 0x100 && page_prog_start < 0x10000)
			{
			 tmp_data[3] = 0x00; tmp_data[2] = 0x00; tmp_data[1] = (uint8_t)(page_prog_start >> 8); tmp_data[0] = (uint8_t)page_prog_start;
			}
			else if (page_prog_start >= 0x10000 && page_prog_start < 0x1000000)
			{
			 tmp_data[3] = 0x00; tmp_data[2] = (uint8_t)(page_prog_start >> 16); tmp_data[1] = (uint8_t)(page_prog_start >> 8); tmp_data[0] = (uint8_t)page_prog_start;
			}
			himax_flash_write_burst(client, tmp_addr, tmp_data);

			tmp_addr[3] = 0x80; tmp_addr[2] = 0x00; tmp_addr[1] = 0x00; tmp_addr[0] = 0x20;
			tmp_data[3] = 0x67; tmp_data[2] = 0x00; tmp_data[1] = 0x00; tmp_data[0] = 0x00;
			himax_flash_write_burst(client, tmp_addr, tmp_data);

			tmp_addr[3] = 0x80; tmp_addr[2] = 0x00; tmp_addr[1] = 0x00; tmp_addr[0] = 0x24;
			tmp_data[3] = 0x00; tmp_data[2] = 0x00; tmp_data[1] = 0x00; tmp_data[0] = 0x20;
			himax_flash_write_burst(client, tmp_addr, tmp_data);

			msleep(200);

			if (!wait_wip(client, 100))
			{
				E("%s:83102_Erase Fail\n", __func__);
				return false;
			}
		}	
		return true;
}

void himax_sram_write(struct i2c_client *client, uint8_t *FW_content)
{
	int i = 0;
	uint8_t tmp_addr[4];
	uint8_t tmp_data[128];
	int FW_length = 0x4000; // 0x4000 = 16K bin file
	
	himax_sense_off(client);

	for (i = 0; i < FW_length; i = i + 128) 
	{
		himax_burst_enable(client, 1);

		if (i < 0x100)
		{
			tmp_addr[3] = 0x08; 
			tmp_addr[2] = 0x00; 
			tmp_addr[1] = 0x00; 
			tmp_addr[0] = i;
		}
		else if (i >= 0x100 && i < 0x10000)
		{
			tmp_addr[3] = 0x08; 
			tmp_addr[2] = 0x00; 
			tmp_addr[1] = (i >> 8); 
			tmp_addr[0] = i;
		}

		memcpy(&tmp_data[0], &FW_content[i], 128);
		himax_flash_write_burst_lenth(client, tmp_addr, tmp_data, 128);

	}

	if (!wait_wip(client, 100))
		E("%s:83102_Sram_Write Fail\n", __func__);
}

bool himax_sram_verify(struct i2c_client *client, uint8_t *FW_File, int FW_Size)
{
	int i = 0;
	uint8_t out_buffer[20];
	uint8_t in_buffer[128];
	uint8_t *get_fw_content;

	get_fw_content = kzalloc(0x4000*sizeof(uint8_t), GFP_KERNEL);

	for (i = 0; i < 0x4000; i = i + 128)
	{
		himax_burst_enable(client, 1);

		//==================================
		//	AHB_I2C Burst Read
		//==================================
		if (i < 0x100)
		{
			out_buffer[3] = 0x08; 
			out_buffer[2] = 0x00; 
			out_buffer[1] = 0x00; 
			out_buffer[0] = i;
		}
		else if (i >= 0x100 && i < 0x10000)
		{
			out_buffer[3] = 0x08; 
			out_buffer[2] = 0x00; 
			out_buffer[1] = (i >> 8); 
			out_buffer[0] = i;
		}

		if ( i2c_himax_write(client, 0x00 ,out_buffer, 4, HIMAX_I2C_RETRY_TIMES) < 0) {
			E("%s: i2c access fail!\n", __func__);
			return false;
		}

		out_buffer[0] = 0x00;		
		if ( i2c_himax_write(client, 0x0C ,out_buffer, 1, HIMAX_I2C_RETRY_TIMES) < 0) {
			E("%s: i2c access fail!\n", __func__);
			return false;
		}

		if ( i2c_himax_read(client, 0x08 ,in_buffer, 128, HIMAX_I2C_RETRY_TIMES) < 0) {
			E("%s: i2c access fail!\n", __func__);
			return false;
		}
		memcpy(&get_fw_content[i], &in_buffer[0], 128);
	}

	for (i = 0; i < FW_Size; i++)
		{
	        if (FW_File[i] != get_fw_content[i])
	        	{
					E("%s: fail! SRAM[%x]=%x NOT CRC_ifile=%x\n", __func__,i,get_fw_content[i],FW_File[i]);
		            return false;
	        	}
		}

	kfree(get_fw_content);

	return true;
}

void himax_flash_programming(struct i2c_client *client, uint8_t *FW_content, int FW_Size)
{
	int page_prog_start = 0;
	int program_length = 48;
	int i = 0, j = 0, k = 0;
	uint8_t tmp_addr[4];
	uint8_t tmp_data[4];
	uint8_t buring_data[256];    // Read for flash data, 128K
									 // 4 bytes for 0x80002C padding

	himax_interface_on(client);
	//himax_burst_enable(client, 0);

	//=====================================
	// SPI Transfer Format : 0x8000_0010 ==> 0x0002_0780
	//=====================================
	tmp_addr[3] = 0x80; tmp_addr[2] = 0x00; tmp_addr[1] = 0x00; tmp_addr[0] = 0x10;
	tmp_data[3] = 0x00; tmp_data[2] = 0x02; tmp_data[1] = 0x07; tmp_data[0] = 0x80;
	himax_flash_write_burst(client, tmp_addr, tmp_data);

	for (page_prog_start = 0; page_prog_start < FW_Size; page_prog_start = page_prog_start + 256)
	{
		//msleep(5);
		//=====================================
		// Write Enable : 1. 0x8000_0020 ==> 0x4700_0000
		//				  2. 0x8000_0024 ==> 0x0000_0006
		//=====================================
		tmp_addr[3] = 0x80; tmp_addr[2] = 0x00; tmp_addr[1] = 0x00; tmp_addr[0] = 0x20;
		tmp_data[3] = 0x47; tmp_data[2] = 0x00; tmp_data[1] = 0x00; tmp_data[0] = 0x00;
		himax_flash_write_burst(client, tmp_addr, tmp_data);

		tmp_addr[3] = 0x80; tmp_addr[2] = 0x00; tmp_addr[1] = 0x00; tmp_addr[0] = 0x24;
		tmp_data[3] = 0x00; tmp_data[2] = 0x00; tmp_data[1] = 0x00; tmp_data[0] = 0x06;
		himax_flash_write_burst(client, tmp_addr, tmp_data);

		//=================================
		// SPI Transfer Control
		// Set 256 bytes page write : 0x8000_0020 ==> 0x610F_F000
		// Set read start address	: 0x8000_0028 ==> 0x0000_0000			
		//=================================
		tmp_addr[3] = 0x80; tmp_addr[2] = 0x00; tmp_addr[1] = 0x00; tmp_addr[0] = 0x20;
		tmp_data[3] = 0x61; tmp_data[2] = 0x0F; tmp_data[1] = 0xF0; tmp_data[0] = 0x00;
		// data bytes should be 0x6100_0000 + ((word_number)*4-1)*4096 = 0x6100_0000 + 0xFF000 = 0x610F_F000
		// Programmable size = 1 page = 256 bytes, word_number = 256 byte / 4 = 64
		himax_flash_write_burst(client, tmp_addr, tmp_data);

		tmp_addr[3] = 0x80; tmp_addr[2] = 0x00; tmp_addr[1] = 0x00; tmp_addr[0] = 0x28;
		//tmp_data[3] = 0x00; tmp_data[2] = 0x00; tmp_data[1] = 0x00; tmp_data[0] = 0x00; // Flash start address 1st : 0x0000_0000

		if (page_prog_start < 0x100)
		{
			tmp_data[3] = 0x00; 
			tmp_data[2] = 0x00; 
			tmp_data[1] = 0x00; 
			tmp_data[0] = (uint8_t)page_prog_start;
		}
		else if (page_prog_start >= 0x100 && page_prog_start < 0x10000)
		{
			tmp_data[3] = 0x00; 
			tmp_data[2] = 0x00; 
			tmp_data[1] = (uint8_t)(page_prog_start >> 8); 
			tmp_data[0] = (uint8_t)page_prog_start;
		}
		else if (page_prog_start >= 0x10000 && page_prog_start < 0x1000000)
		{
			tmp_data[3] = 0x00; 
			tmp_data[2] = (uint8_t)(page_prog_start >> 16); 
			tmp_data[1] = (uint8_t)(page_prog_start >> 8); 
			tmp_data[0] = (uint8_t)page_prog_start;
		}
		
		himax_flash_write_burst(client, tmp_addr, tmp_data);


		//=================================
		// Send 16 bytes data : 0x8000_002C ==> 16 bytes data	  
		//=================================
		buring_data[0] = 0x2C;
		buring_data[1] = 0x00;
		buring_data[2] = 0x00;
		buring_data[3] = 0x80;
		
		for (i = /*0*/page_prog_start, j = 0; i < 16 + page_prog_start/**/; i++, j++)	/// <------ bin file
		{
			buring_data[j + 4] = FW_content[i];
		}


		if ( i2c_himax_write(client, 0x00 ,buring_data, 20, HIMAX_I2C_RETRY_TIMES) < 0) {
			E("%s: i2c access fail!\n", __func__);
			return;
		}
		//=================================
		// Write command : 0x8000_0024 ==> 0x0000_0002
		//=================================
		tmp_addr[3] = 0x80; tmp_addr[2] = 0x00; tmp_addr[1] = 0x00; tmp_addr[0] = 0x24;
		tmp_data[3] = 0x00; tmp_data[2] = 0x00; tmp_data[1] = 0x00; tmp_data[0] = 0x02;
		himax_flash_write_burst(client, tmp_addr, tmp_data);

		//=================================
		// Send 240 bytes data : 0x8000_002C ==> 240 bytes data	 
		//=================================

		for (j = 0; j < 5; j++)
		{
			for (i = (page_prog_start + 16 + (j * 48)), k = 0; i < (page_prog_start + 16 + (j * 48)) + program_length; i++, k++)   /// <------ bin file
			{
				buring_data[k+4] = FW_content[i];//(byte)i;
			}

			if ( i2c_himax_write(client, 0x00 ,buring_data, program_length+4, HIMAX_I2C_RETRY_TIMES) < 0) {
				E("%s: i2c access fail!\n", __func__);
				return;
			}

		}

		if (!wait_wip(client, 1))
			E("%s:83102_Flash_Programming Fail\n", __func__);
	}
}


bool himax_check_chip_version(struct i2c_client *client)
{
	uint8_t tmp_addr[4];
	uint8_t tmp_data[4];
	uint8_t ret_data = false;
	int i = 0;
	
	for (i = 0; i < 5; i++)
	{
		// Product ID
        // Touch 
		tmp_addr[3] = 0x90; tmp_addr[2] = 0x00; tmp_addr[1] = 0x00; tmp_addr[0] = 0xD0;
		himax_register_read(client, tmp_addr, 4, tmp_data, false);
		
		I("%s:Read driver IC ID = %X,%X,%X\n", __func__, tmp_data[3],tmp_data[2],tmp_data[1]);
		if ((tmp_data[3] == 0x83) && (tmp_data[2] == 0x10) && (tmp_data[1] == 0x2a))
		{
			IC_TYPE         = HX_83102A_SERIES_PWON;
			ret_data = true;
			break;
		}
		else
		{
			ret_data = false;
			E("%s:Read driver ID register Fail:\n", __func__);
		}
	}
	
	return ret_data;
}

bool Calculate_CRC_with_AP(unsigned char *FW_content , int CRC_from_FW, int mode)
{
//	uint8_t tmp_data[4];
	int i, j;
	int fw_data;
	int fw_data_2;
	int CRC = 0xFFFFFFFF;
	int PolyNomial = 0x82F63B78;
	int length = 0;
	
	if (mode == fw_image_128k)
		length = 0x8000;
	else if (mode == fw_image_124k)
		length = 0x7C00;
	else if (mode == fw_image_64k)
		length = 0x4000;
	else if (mode == fw_image_60k)
		length = 0x3C00;
	else if (mode == fw_image_48k)
		length = 0x3000;
  else
		length = 9;

	for (i = 0; i < length; i++)
	{
		fw_data = FW_content[i * 4 ];
		
		for (j = 1; j < 4; j++)
		{
			fw_data_2 = FW_content[i * 4 + j];
			fw_data += (fw_data_2) << (8 * j);
		}

		CRC = fw_data ^ CRC;

		for (j = 0; j < 32; j++)
		{
			if ((CRC % 2) != 0)
			{
				CRC = ((CRC >> 1) & 0x7FFFFFFF) ^ PolyNomial;				
			}
			else
			{
				CRC = (((CRC >> 1) & 0x7FFFFFFF)/*& 0x7FFFFFFF*/);
			}
		}
	}

	I("%s: CRC calculate from bin file is %x \n", __func__, CRC);
/*
	tmp_data[0] = (uint8_t)(CRC >> 24);
	tmp_data[1] = (uint8_t)(CRC >> 16);
	tmp_data[2] = (uint8_t)(CRC >> 8);
	tmp_data[3] = (uint8_t) CRC;

	CRC = tmp_data[0];
	CRC += tmp_data[1] << 8;			
	CRC += tmp_data[2] << 16;
	CRC += tmp_data[3] << 24;

	I("%s: CRC calculate from bin file REVERSE %x \n", __func__, CRC);
*/
	I("%s: CRC calculate from FWis %x \n", __func__, CRC_from_FW);
	if (CRC_from_FW == CRC)
		return true;
	else
		return false;
}

uint32_t himax_hw_check_CRC(struct i2c_client *client, uint8_t *start_addr, int reload_length)
 {
	uint32_t result = 0;
	uint8_t tmp_addr[4];
	uint8_t tmp_data[4];
	int cnt = 0;
	int length = reload_length / 4;

	//CRC4 // 0x8005_0020 <= from, 0x8005_0028 <= 0x0099_length
	tmp_addr[3] = 0x80; tmp_addr[2] = 0x05; tmp_addr[1] = 0x00; tmp_addr[0] = 0x20;
	//tmp_data[3] = 0x00; tmp_data[2] = 0x00; tmp_data[1] = 0xFB; tmp_data[0] = 0x00;
	himax_flash_write_burst(client, tmp_addr, start_addr);

	tmp_addr[3] = 0x80; tmp_addr[2] = 0x05; tmp_addr[1] = 0x00; tmp_addr[0] = 0x28;
	tmp_data[3] = 0x00; tmp_data[2] = 0x99; tmp_data[1] = (length >> 8); tmp_data[0] = length;
	himax_flash_write_burst(client, tmp_addr, tmp_data);

	cnt = 0;
	tmp_addr[3] = 0x80; tmp_addr[2] = 0x05; tmp_addr[1] = 0x00; tmp_addr[0] = 0x00;
	do
	{
		himax_register_read(client, tmp_addr, 4, tmp_data, false);

		if ((tmp_data[0] & 0x01) != 0x01)
		{
			tmp_addr[3] = 0x80; tmp_addr[2] = 0x05; tmp_addr[1] = 0x00; tmp_addr[0] = 0x18;
			himax_register_read(client, tmp_addr, 4, tmp_data, false);
			I("%s: tmp_data[3]=%X, tmp_data[2]=%X, tmp_data[1]=%X, tmp_data[0]=%X  \n", __func__, tmp_data[3], tmp_data[2], tmp_data[1], tmp_data[0]);
			result = ((tmp_data[3] << 24) + (tmp_data[2] << 16) + (tmp_data[1] << 8) + tmp_data[0]);
			break;
		}
	} while (cnt++ < 100);

	 return result;
}

void himax_flash_page_write(struct i2c_client *client, uint8_t *write_addr, uint8_t *write_data)
{
	uint8_t tmp_addr[4];
	uint8_t tmp_data[4];
	uint8_t buring_data[256]; // buffer for burning from FW_content
	int start_address = (write_addr[3] << 24) + (write_addr[2] << 16) + (write_addr[1] << 8) + write_addr[0];
	int page_prog_start,program_length;
	int i,j,k;
	
	himax_burst_enable(client, 0);

	//=====================================
	// SPI Transfer Format : 0x8000_0010 ==> 0x0002_0780
	//=====================================
	tmp_addr[3] = 0x80; tmp_addr[2] = 0x00; tmp_addr[1] = 0x00; tmp_addr[0] = 0x10;
	tmp_data[3] = 0x00; tmp_data[2] = 0x02; tmp_data[1] = 0x07; tmp_data[0] = 0x80;
	himax_flash_write_burst(client, tmp_addr, tmp_data);

	for (page_prog_start = start_address; page_prog_start < 36 + start_address; page_prog_start = page_prog_start + 256)
	{
		//=====================================
		// Write Enable : 1. 0x8000_0020 ==> 0x4700_0000
		//				  2. 0x8000_0024 ==> 0x0000_0006
		//=====================================
		tmp_addr[3] = 0x80; tmp_addr[2] = 0x00; tmp_addr[1] = 0x00; tmp_addr[0] = 0x20;
		tmp_data[3] = 0x47; tmp_data[2] = 0x00; tmp_data[1] = 0x00; tmp_data[0] = 0x00;
		himax_flash_write_burst(client, tmp_addr, tmp_data);

		tmp_addr[3] = 0x80; tmp_addr[2] = 0x00; tmp_addr[1] = 0x00; tmp_addr[0] = 0x24;
		tmp_data[3] = 0x00; tmp_data[2] = 0x00; tmp_data[1] = 0x00; tmp_data[0] = 0x06;
		himax_flash_write_burst(client, tmp_addr, tmp_data);

		//=================================
		// SPI Transfer Control
		// Set 256 bytes page write : 0x8000_0020 ==> 0x610F_F000
		// Set read start address	: 0x8000_0028 ==> 0x0000_0000
		//=================================
		tmp_addr[3] = 0x80; tmp_addr[2] = 0x00; tmp_addr[1] = 0x00; tmp_addr[0] = 0x20;
		tmp_data[3] = 0x61; tmp_data[2] = 0x0F; tmp_data[1] = 0xF0; tmp_data[0] = 0x00;
		// data bytes should be 0x6100_0000 + ((word_number)*4-1)*4096 = 0x6100_0000 + 0xFF000 = 0x610F_F000
		// Programmable size = 1 page = 256 bytes, word_number = 256 byte / 4 = 64
		himax_flash_write_burst(client, tmp_addr, tmp_data);

		tmp_addr[3] = 0x80; tmp_addr[2] = 0x00; tmp_addr[1] = 0x00; tmp_addr[0] = 0x28;
		//tmp_data[3] = 0x00; tmp_data[2] = 0x00; tmp_data[1] = 0x00; tmp_data[0] = 0x00; // Flash start address 1st : 0x0000_0000
		if (page_prog_start < 0x100)
		{
			tmp_data[3] = 0x00; tmp_data[2] = 0x00; tmp_data[1] = 0x00; tmp_data[0] = page_prog_start;
		}
		else if (page_prog_start >= 0x100 && page_prog_start < 0x10000)
		{
			tmp_data[3] = 0x00; tmp_data[2] = 0x00; tmp_data[1] = (page_prog_start >> 8); tmp_data[0] = page_prog_start;
		}
		else if (page_prog_start >= 0x10000 && page_prog_start < 0x1000000)
		{
			tmp_data[3] = 0x00; tmp_data[2] = (page_prog_start >> 16); tmp_data[1] = (page_prog_start >> 8); tmp_data[0] = page_prog_start;
		}

		himax_flash_write_burst(client, tmp_addr, tmp_data);

		for (i = 0; i < 256; i++)
			buring_data[i] = 0xFF;

		//=================================
		// Send 16 bytes data : 0x8000_002C ==> 16 bytes data
		//=================================
		buring_data[0] = 0x2C;
		buring_data[1] = 0x00;
		buring_data[2] = 0x00;
		buring_data[3] = 0x80;

		for (i = page_prog_start - start_address, j = 0; i < 16 + page_prog_start - start_address/**/; i++, j++)   /// <------ bin file
		{
			buring_data[j + 4] = write_data[i];//(byte)i;
		}
		//===========================================
		//  0x00 ==> buring_data
		//===========================================
		if ( i2c_himax_write(client, 0x00 ,buring_data, 20, 3) < 0) {
			E("%s: i2c access fail!\n", __func__);
			return;
		}

		//=================================
		// Write command : 0x8000_0024 ==> 0x0000_0002
		//=================================
		tmp_addr[3] = 0x80; tmp_addr[2] = 0x00; tmp_addr[1] = 0x00; tmp_addr[0] = 0x24;
		tmp_data[3] = 0x00; tmp_data[2] = 0x00; tmp_data[1] = 0x00; tmp_data[0] = 0x02;
		himax_flash_write_burst(client, tmp_addr, tmp_data);

		//=================================
		// Send 240 bytes data : 0x8000_002C ==> 240 bytes data
		//=================================
		//for (i = /**/page_prog_start + 16, j = 0; i < 256 + page_prog_start/**/; i++, j++)   /// <------ bin file
		//{
		//	  buring_data[j + 4] = FW_content[i];//(byte)i;
		//}
		//RegisterWrite83110("00", 244, buring_data);

		// Send 48 bytes each time, total 48 * 5 = 240
		program_length = 48;
		for (j = 0; j < 5; j++)
		{
			if (i < 36)
				for (i = (page_prog_start - start_address + 16 + (j * 48)), k = 0; i < 48 && i < (page_prog_start - start_address + 16 + (j * 48)) + program_length; i++, k++)	/// <------ bin file
				{
					buring_data[k + 4] = write_data[i];//(byte)i;
				}
			else{
				for (k = 0; k < 48; k++){
					buring_data[k + 4] = 0xFF;
				}
			}
			//===========================================
			//  0x00 ==> buring_data
			//===========================================
			if ( i2c_himax_write(client, 0x00 ,buring_data, program_length + 4, 3) < 0) {
				E("%s: i2c access fail!\n", __func__);
				return;
			}
		}

		if (!wait_wip(client, 1))
		{
			E("%s: Fail\n", __func__);
		}
	}

}

void himax_set_reload_cmd(uint8_t *write_data, int idx, uint32_t cmd_from, uint32_t cmd_to, uint32_t cmd_beat)
{
	int index = idx * 12;
	int i;
	for (i = 3; i >= 0; i--)
	{
		write_data[index + i] = (cmd_from >> (8 * i));
		write_data[index + 4 + i] = (cmd_to >> (8 * i));
		write_data[index + 8 + i] = (cmd_beat >> (8 * i));
	}
}

bool himax_program_reload(struct i2c_client *client)
{
	uint32_t hw_crc;
	int cmd_size = 36;
	uint8_t write_addr[4];
	uint8_t write_data[cmd_size];

	write_addr[3] = 0x00; write_addr[2] = 0x00; write_addr[1] = 0xFB; write_addr[0] = 0x00;

	// Copy fw in flash to ISRAM
	himax_set_reload_cmd(write_data, 0, 0x00000000, 0x08000000, 0x0A3C3000);
	// Compare fw in flash with fw in ISRAM
	himax_set_reload_cmd(write_data, 1, 0x00000000, 0x08000000, 0x04663000);
	// Break
	himax_set_reload_cmd(write_data, 2, 0x00000000, 0x00000000, 0x15E75678);
	himax_flash_page_write(client, write_addr, write_data);

	hw_crc = himax_hw_check_CRC(client, write_addr, cmd_size);
	return Calculate_CRC_with_AP(write_data, hw_crc, 10);
}

int fts_ctpm_fw_upgrade_with_sys_fs_32k(struct i2c_client *client, unsigned char *fw, int len, bool change_iref)
{
	/* Not use */
	return 0;
}

int fts_ctpm_fw_upgrade_with_sys_fs_60k(struct i2c_client *client, unsigned char *fw, int len, bool change_iref) 
{
	/* Not use */
	return 0;
}

int fts_ctpm_fw_upgrade_with_sys_fs_64k(struct i2c_client *client, unsigned char *fw, int len, bool change_iref) //Alice - Un
{
	
	//int CRC_from_FW = 0;
	int burnFW_success = 0;
	uint8_t tmp_addr[4];
	uint8_t tmp_data[4];
	
	if (len != FW_SIZE_64k)   //64k
	{
    	E("%s: The file size is not 64K bytes\n", __func__);
    	return false;
	}

	tmp_addr[3] = 0x90; tmp_addr[2] = 0x00; tmp_addr[1] = 0x00; tmp_addr[0] = 0x18;
	tmp_data[3] = 0x00; tmp_data[2] = 0x00; tmp_data[1] = 0x00; tmp_data[0] = 0x55;
	himax_register_write(client,tmp_addr, 4, tmp_data, false);

	himax_sense_off(client);	
	himax_chip_erase(client);
	himax_flash_programming(client, fw, FW_SIZE_64k);

	tmp_data[3] = 0x00; tmp_data[2] = 0x00; tmp_data[1] = 0x00; tmp_data[0] = 0x00;

	if(himax_hw_check_CRC(client,tmp_data, FW_SIZE_64k) == 0)
	{
		burnFW_success = 1;
	}
	else
	{
		burnFW_success = 0;
	}
	//System reset
	tmp_addr[3] = 0x90; tmp_addr[2] = 0x00; tmp_addr[1] = 0x00; tmp_addr[0] = 0x18;
	tmp_data[3] = 0x00; tmp_data[2] = 0x00; tmp_data[1] = 0x00; tmp_data[0] = 0x55;
	himax_register_write(client,tmp_addr, 4, tmp_data, false);
	return burnFW_success;

}

int fts_ctpm_fw_upgrade_with_sys_fs_124k(struct i2c_client *client, unsigned char *fw, int len, bool change_iref)
{
	/* Not use */
	return 0;
}

int fts_ctpm_fw_upgrade_with_sys_fs_128k(struct i2c_client *client, unsigned char *fw, int len, bool change_iref)
{
	/* Not use */
	return 0;
}

void himax_touch_information(struct i2c_client *client)
{
#ifndef HX_FIX_TOUCH_INFO
	uint8_t cmd[4];
	char data[12] = {0};
	int retry = 200;
	int reload_status = 0;

	I("%s:IC_TYPE =%d\n", __func__,IC_TYPE);

	if(IC_TYPE == HX_83102A_SERIES_PWON)
	{
		
		while(reload_status == 0)
		{
			cmd[3] = 0x10; cmd[2] = 0x00; cmd[1] = 0x7f; cmd[0] = 0x00;
			himax_register_read(client, cmd, 4, data, false);
			
			if(data[3]==0x00 && data[2]==0x00 && data[1]==0x3A && data[0]==0xA3 )
			{
				I("reload OK! \n");
				reload_status = 1;
				break;
			}
			else if(retry == 0)
			{
				I("reload 20 times! fail \n");
				break;
			}
			else
			{
				retry --;
				msleep(10);
				I("reload fail ,delay 10ms retry=%d\n",retry);
			}
		}
		I("%s : data[0]=0x%2.2X,data[1]=0x%2.2X,data[2]=0x%2.2X,data[3]=0x%2.2X\n",__func__,data[0],data[1],data[2],data[3]);
		I("reload_status=%d\n",reload_status);
		
		
		cmd[3] = 0x10; cmd[2] = 0x00; cmd[1] = 0x70; cmd[0] = 0xF4;
		himax_register_read(client, cmd, 8, data, false);
		ic_data->HX_RX_NUM				= data[2];
		ic_data->HX_TX_NUM				= data[3];
		ic_data->HX_MAX_PT				= data[4];
		//I("%s : HX_RX_NUM=%d,ic_data->HX_TX_NUM=%d,ic_data->HX_MAX_PT=%d\n",__func__,ic_data->HX_RX_NUM,ic_data->HX_TX_NUM,ic_data->HX_MAX_PT);

		cmd[3] = 0x10; cmd[2] = 0x00; cmd[1] = 0x70; cmd[0] = 0xFA;
		himax_register_read(client, cmd, 4, data, false);
		//I("%s : c_data->HX_XY_REVERSE=0x%2.2X\n",__func__,data[1]);
		if((data[1] & 0x04) == 0x04)
		{
			ic_data->HX_XY_REVERSE = true;
		}
		else 
		{
			ic_data->HX_XY_REVERSE = false;
		}
		
		cmd[3] = 0x10; cmd[2] = 0x00; cmd[1] = 0x70; cmd[0] = 0xFC;
		himax_register_read(client, cmd, 4, data, false);
		ic_data->HX_Y_RES = data[0]*256;
		ic_data->HX_Y_RES = ic_data->HX_Y_RES + data[1];
		ic_data->HX_X_RES = data[2]*256 + data[3];
		//I("%s : ic_data->HX_Y_RES=%d,ic_data->HX_X_RES=%d \n",__func__,ic_data->HX_Y_RES,ic_data->HX_X_RES);
		
		cmd[3] = 0x10; cmd[2] = 0x00; cmd[1] = 0x70; cmd[0] = 0x89;
		himax_register_read(client, cmd, 4, data, false);
		//I("%s : data[0]=0x%2.2X,data[1]=0x%2.2X,data[2]=0x%2.2X,data[3]=0x%2.2X\n",__func__,data[0],data[1],data[2],data[3]);
		//I("data[0] & 0x01 = %d\n",(data[0] & 0x01));
		if((data[0] & 0x01) == 1)
		{
			ic_data->HX_INT_IS_EDGE = true;
		}
		else
		{
			ic_data->HX_INT_IS_EDGE = false;
		}
	  
	  if (ic_data->HX_RX_NUM > 40)
			ic_data->HX_RX_NUM = 32;
	  if (ic_data->HX_TX_NUM > 20)
			ic_data->HX_TX_NUM = 18;
	  if (ic_data->HX_MAX_PT > 10)
			ic_data->HX_MAX_PT = 10;
	  if (ic_data->HX_Y_RES > 2000)
			ic_data->HX_Y_RES = 1280;
	  if (ic_data->HX_X_RES > 2000)
			ic_data->HX_X_RES = 720;
#ifdef HX_EN_MUT_BUTTON
		cmd[3] = 0x08; cmd[2] = 0x00; cmd[1] = 0x00; cmd[0] = 0xE8;
		himax_register_read(client, cmd, 4, data, false);
		ic_data->HX_BT_NUM				= data[3];
#endif
	}
	else
	{
		ic_data->HX_RX_NUM				= 0;
		ic_data->HX_TX_NUM				= 0;
		ic_data->HX_BT_NUM				= 0;
		ic_data->HX_X_RES				= 0;
		ic_data->HX_Y_RES				= 0;
		ic_data->HX_MAX_PT				= 0;
		ic_data->HX_XY_REVERSE		= false;
		ic_data->HX_INT_IS_EDGE		= false;
	}
#else
		ic_data->HX_RX_NUM				= FIX_HX_RX_NUM;
		ic_data->HX_TX_NUM				= FIX_HX_TX_NUM;
		ic_data->HX_BT_NUM				= FIX_HX_BT_NUM;
		ic_data->HX_X_RES				= FIX_HX_X_RES;
		ic_data->HX_Y_RES				= FIX_HX_Y_RES;
		ic_data->HX_MAX_PT				= FIX_HX_MAX_PT;
		ic_data->HX_XY_REVERSE			= FIX_HX_XY_REVERSE;
		ic_data->HX_INT_IS_EDGE			= FIX_HX_INT_IS_EDGE;
#endif
	I("%s:HX_RX_NUM =%d,HX_TX_NUM =%d,HX_MAX_PT=%d \n", __func__,ic_data->HX_RX_NUM,ic_data->HX_TX_NUM,ic_data->HX_MAX_PT);
	I("%s:HX_XY_REVERSE =%d,HX_Y_RES =%d,HX_X_RES=%d \n", __func__,ic_data->HX_XY_REVERSE,ic_data->HX_Y_RES,ic_data->HX_X_RES);
	I("%s:HX_INT_IS_EDGE =%d \n", __func__,ic_data->HX_INT_IS_EDGE);
}

int himax_read_i2c_status(struct i2c_client *client)
{
	return i2c_error_count; //
}

int himax_read_ic_trigger_type(struct i2c_client *client)
{
	uint8_t cmd[4];
	char data[12] = {0};
	int trigger_type = false;

	cmd[3] = 0x10; cmd[2] = 0x00; cmd[1] = 0x70; cmd[0] = 0x89;
	himax_register_read(client, cmd, 4, data, false);
	if((data[0] & 0x01) == 1)
	{
		trigger_type = true;
	}
	else
	{
		trigger_type = false;
	}

	return trigger_type;
}

void himax_read_FW_ver(struct i2c_client *client)
{
	uint8_t cmd[4];
	uint8_t data[64];

	//=====================================
	// Read FW version : 0x1000_7004  but 05,06 are the real addr for FW Version 
	//=====================================
	
	cmd[3] = 0x10; cmd[2] = 0x00; cmd[1] = 0x70; cmd[0] = 0x04;
	himax_register_read(client, cmd, 4, data, false);
	
	ic_data->vendor_panel_ver =  data[0];
	ic_data->vendor_fw_ver = data[1] << 8 | data[2];

	I("PANEL_VER : %X \n",ic_data->vendor_panel_ver);
	I("FW_VER : %X \n",ic_data->vendor_fw_ver);

	
	cmd[3] = 0x10; cmd[2] = 0x00; cmd[1] = 0x70; cmd[0] = 0x84;
	himax_register_read(client, cmd, 4, data, false);

	ic_data->vendor_config_ver = data[2] << 8 | data[3];
	//I("CFG_VER : %X \n",ic_data->vendor_config_ver);
	ic_data->vendor_touch_cfg_ver = data[2];
	I("TOUCH_VER : %X \n",ic_data->vendor_touch_cfg_ver);
	ic_data->vendor_display_cfg_ver = data[3];
	I("DISPLAY_VER : %X \n",ic_data->vendor_display_cfg_ver);


	cmd[3] = 0x10; cmd[2] = 0x00; cmd[1] = 0x70; cmd[0] = 0x00;
	himax_register_read(client, cmd, 4, data, false);
	
	ic_data->vendor_cid_maj_ver = data[2] ;
	ic_data->vendor_cid_min_ver = data[3];
	
	I("CID_VER : %X \n",(ic_data->vendor_cid_maj_ver << 8 | ic_data->vendor_cid_min_ver));
	
	return;
}

bool himax_ic_package_check(struct i2c_client *client)
{
	uint8_t tmp_addr[4];
	uint8_t tmp_data[4];
	uint8_t ret_data = 0x00;
	int i = 0;

	for (i = 0; i < 5; i++)
	{
// Product ID
        // Touch 
		tmp_addr[3] = 0x90; tmp_addr[2] = 0x00; tmp_addr[1] = 0x00; tmp_addr[0] = 0xD0;
		himax_register_read(client, tmp_addr, 4, tmp_data, false);
		
		I("%s:Read driver IC ID = %X,%X,%X\n", __func__, tmp_data[3],tmp_data[2],tmp_data[1]);
		if ((tmp_data[3] == 0x83) && (tmp_data[2] == 0x10) && ((tmp_data[1] == 0x2a)||(tmp_data[1] == 0x2b)))
		{
			IC_TYPE         		= HX_83102A_SERIES_PWON;
		  	IC_CHECKSUM 			= HX_TP_BIN_CHECKSUM_CRC;
		  	//Himax: Set FW and CFG Flash Address
		  	FW_VER_MAJ_FLASH_ADDR   = 49157;  //0x00C005
		  	FW_VER_MAJ_FLASH_LENG   = 1;
		  	FW_VER_MIN_FLASH_ADDR   = 49158;  //0x00C006
		  	FW_VER_MIN_FLASH_LENG   = 1;
		  	CFG_VER_MAJ_FLASH_ADDR 	= 49408;  //0x00C100
		  	CFG_VER_MAJ_FLASH_LENG 	= 1;
		  	CFG_VER_MIN_FLASH_ADDR 	= 49409;  //0x00C101
		  	CFG_VER_MIN_FLASH_LENG 	= 1;
			CID_VER_MAJ_FLASH_ADDR	= 49154;  //0x00C002
			CID_VER_MAJ_FLASH_LENG	= 1;
			CID_VER_MIN_FLASH_ADDR	= 49155;  //0x00C003
			CID_VER_MIN_FLASH_LENG	= 1;
			//PANEL_VERSION_ADDR		= 49156;  //0x00C004
			//PANEL_VERSION_LENG		= 1;
#ifdef HX_AUTO_UPDATE_FW
			g_i_FW_VER = i_CTPM_FW[FW_VER_MAJ_FLASH_ADDR]<<8 |i_CTPM_FW[FW_VER_MIN_FLASH_ADDR];
			g_i_CFG_VER = i_CTPM_FW[CFG_VER_MAJ_FLASH_ADDR]<<8 |i_CTPM_FW[CFG_VER_MIN_FLASH_ADDR];
			g_i_CID_MAJ = i_CTPM_FW[CID_VER_MAJ_FLASH_ADDR];
			g_i_CID_MIN = i_CTPM_FW[CID_VER_MIN_FLASH_ADDR];
#endif
			I("Himax IC package 83102_in\n");
			ret_data = true;
			break;
		}
		else
		{
			ret_data = false;
			E("%s:Read driver ID register Fail:\n", __func__);
		}
	}
	// 4. After read finish, set DDREG_Req = 0 (0x9000_0020 = 0x0000_0000) (Unlock register R/W from driver)
	tmp_addr[3] = 0x90; tmp_addr[2] = 0x00; tmp_addr[1] = 0x00; tmp_addr[0] = 0x20;
	tmp_data[3] = 0x00; tmp_data[2] = 0x00; tmp_data[1] = 0x00; tmp_data[0] = 0x00;
	himax_register_write(client, tmp_addr, 4, tmp_data, false);

	return ret_data;
}

void himax_power_on_init(struct i2c_client *client)
{
	I("%s:\n", __func__);
	himax_touch_information(client);
	//himax_sense_on(client, 0x01);
}

bool himax_read_event_stack(struct i2c_client *client, 	uint8_t *buf, uint8_t length) //Alice - Un
{
	uint8_t cmd[4];

	//  AHB_I2C Burst Read Off
	cmd[0] = 0x00;		
	if ( i2c_himax_write(client, 0x11 ,cmd, 1, HIMAX_I2C_RETRY_TIMES) < 0)
	{
		E("%s: i2c access fail!\n", __func__);
		return 0;
	}
	
	i2c_himax_read(client, 0x30, buf, length,HIMAX_I2C_RETRY_TIMES);

	//  AHB_I2C Burst Read On
	cmd[0] = 0x01;
	if ( i2c_himax_write(client, 0x11 ,cmd, 1, 3) < 0) {
		E("%s: i2c access fail!\n", __func__);
		return 0;
	}
	return 1;
}

void himax_get_DSRAM_data(struct i2c_client *client, uint8_t *info_data)
{
	int i = 0;
	//int cnt = 0;
	unsigned char tmp_addr[4];
	unsigned char tmp_data[4];
	uint8_t max_i2c_size = 128;
	uint8_t x_num = ic_data->HX_RX_NUM;
	uint8_t y_num = ic_data->HX_TX_NUM;
	int m_key_num = 0;
	int total_size = (x_num * y_num + x_num + y_num) * 2 + 4;
	int total_size_temp;
	int mutual_data_size = x_num * y_num * 2;
	int total_read_times = 0;
	int address = 0;
	uint8_t  temp_info_data[total_size + 8]; //max mkey size = 8
	uint16_t check_sum_cal = 0;
	int fw_run_flag = -1;
	//uint16_t temp_check_sum_cal = 0;

	/*1. Read number of MKey R100070E8H to determin data size*/
	tmp_addr[3] = 0x10; tmp_addr[2] = 0x00; tmp_addr[1] = 0x70; tmp_addr[0] = 0xE8;
	himax_register_read(client, tmp_addr, 4,tmp_data , false);
	//I("%s: tmp_data[0] = 0x%02X,tmp_data[1] = 0x%02X,tmp_data[2] = 0x%02X,tmp_data[3] = 0x%02X\n", __func__,tmp_data[0],tmp_data[1],tmp_data[2],tmp_data[3]);
	m_key_num = tmp_data[0] & 0x03;
	//I("%s,m_key_num=%d\n",__func__,m_key_num);
	total_size += m_key_num*2;

	/* 2. Start DSRAM Rawdata and Wait Data Ready */
	tmp_addr[3] = 0x10; tmp_addr[2] = 0x00; tmp_addr[1] = 0x00; tmp_addr[0] = 0x00;
	tmp_data[3] = 0x00; tmp_data[2] = 0x00; tmp_data[1] = 0x5A; tmp_data[0] = 0xA5;
	fw_run_flag = himax_write_read_reg(client,tmp_addr,tmp_data,0xA5,0x5A);
	
	if(fw_run_flag < 0)
	{
		I("%s Data NOT ready => bypass \n", __func__);
		return;
	}
	
	/* 3. Read RawData */
	total_size_temp = total_size;
	tmp_addr[3] = 0x10; tmp_addr[2] = 0x00; tmp_addr[1] = 0x00; tmp_addr[0] = 0x00;

	if (total_size % max_i2c_size == 0)
	{
		total_read_times = total_size / max_i2c_size;
	}
	else
	{
		total_read_times = total_size / max_i2c_size + 1;
	}

	for (i = 0; i < (total_read_times); i++)
	{
		if ( total_size_temp >= max_i2c_size)
		{
			himax_register_read(client, tmp_addr, max_i2c_size, &temp_info_data[i*max_i2c_size], false);
			total_size_temp = total_size_temp - max_i2c_size;
		}
		else
		{	
			//I("last total_size_temp=%d\n",total_size_temp);
			himax_register_read(client, tmp_addr, total_size_temp % max_i2c_size, &temp_info_data[i*max_i2c_size], false);
		}

		address = ((i+1)*max_i2c_size);
		tmp_addr[1] = (uint8_t)((address>>8)&0x00FF);
		tmp_addr[0] = (uint8_t)((address)&0x00FF);
	}
	
	/* 4. FW stop outputing */
	//I("DSRAM_Flag=%d\n",DSRAM_Flag);
	if(DSRAM_Flag == false)
	{
		//I("Return to Event Stack!\n");
		tmp_addr[3] = 0x10; tmp_addr[2] = 0x00; tmp_addr[1] = 0x00; tmp_addr[0] = 0x00;
		tmp_data[3] = 0x00; tmp_data[2] = 0x00; tmp_data[1] = 0x00; tmp_data[0] = 0x00;
		himax_flash_write_burst(client, tmp_addr, tmp_data);
	}
	else
	{
		//I("Continue to SRAM!\n");
		tmp_addr[3] = 0x10; tmp_addr[2] = 0x00; tmp_addr[1] = 0x00; tmp_addr[0] = 0x00;
		tmp_data[3] = 0x11; tmp_data[2] = 0x22; tmp_data[1] = 0x33; tmp_data[0] = 0x44;
		himax_flash_write_burst(client, tmp_addr, tmp_data);
	}
	
	/* 5. Data Checksum Check */
	for (i = 2; i < total_size; i=i+2)/* 2:PASSWORD NOT included */
	{
		check_sum_cal += (temp_info_data[i+1]*256 + temp_info_data[i]);
	}

	if (check_sum_cal % 0x10000 != 0)
	{
		I("%s check_sum_cal fail=%2X \n", __func__, check_sum_cal);
		return;
	} 
	else
	{
		memcpy(info_data, &temp_info_data[4], mutual_data_size * sizeof(uint8_t));
		//I("%s checksum PASS \n", __func__);
	}
	
}

bool himax_calculateChecksum(struct i2c_client *client, bool change_iref)
{
	uint8_t CRC_result = 0;
	uint8_t tmp_data[4];

	tmp_data[3] = 0x00; tmp_data[2] = 0x00; tmp_data[1] = 0x00; tmp_data[0] = 0x00;

	CRC_result = himax_hw_check_CRC(client,tmp_data, FW_SIZE_64k);

	return !CRC_result;
}

//ts_work
int cal_data_len(int raw_cnt_rmd, int HX_MAX_PT, int raw_cnt_max){
	int RawDataLen;
	if (raw_cnt_rmd != 0x00) {
		RawDataLen = 128 - ((HX_MAX_PT+raw_cnt_max+3)*4) - 1;
	}else{
		RawDataLen = 128 - ((HX_MAX_PT+raw_cnt_max+2)*4) - 1;
	}
	return RawDataLen;
}

bool diag_check_sum( struct himax_report_data *hx_touch_data ) //return checksum value
{
	uint16_t check_sum_cal = 0;
	int i;

	//Check 128th byte CRC
	for (i = 0, check_sum_cal = 0; i < (hx_touch_data->touch_all_size - hx_touch_data->touch_info_size); i=i+2)
	{
		check_sum_cal += (hx_touch_data->hx_rawdata_buf[i+1]*256 + hx_touch_data->hx_rawdata_buf[i]);
	}
	if (check_sum_cal % 0x10000 != 0)
	{
		I("%s fail=%2X \n", __func__, check_sum_cal);
		return 0;
		//goto bypass_checksum_failed_packet;
	}
	
	return 1;
}


void diag_parse_raw_data(struct himax_report_data *hx_touch_data,int mul_num, int self_num,uint8_t diag_cmd, int16_t *mutual_data, int16_t *self_data)
{
	int RawDataLen_word;
	int index = 0;
	int temp1, temp2,i;

	if (hx_touch_data->hx_rawdata_buf[0] == 0x3A
	&& hx_touch_data->hx_rawdata_buf[1] == 0xA3
	&& hx_touch_data->hx_rawdata_buf[2] > 0
	&& hx_touch_data->hx_rawdata_buf[3] == diag_cmd )
	{
		RawDataLen_word = hx_touch_data->rawdata_size/2;
		index = (hx_touch_data->hx_rawdata_buf[2] - 1) * RawDataLen_word;
		//I("Header[%d]: %x, %x, %x, %x, mutual: %d, self: %d\n", index, buf[56], buf[57], buf[58], buf[59], mul_num, self_num);
		//I("RawDataLen=%d , RawDataLen_word=%d , hx_touch_info_size=%d\n", RawDataLen, RawDataLen_word, hx_touch_info_size);
		for (i = 0; i < RawDataLen_word; i++)
		{
			temp1 = index + i;

			if (temp1 < mul_num)
			{ //mutual
				mutual_data[index + i] = hx_touch_data->hx_rawdata_buf[i*2 + 4 + 1]*256 + hx_touch_data->hx_rawdata_buf[i*2 + 4];	//4: RawData Header, 1:HSB
			}
			else
			{//self
				temp1 = i + index;
				temp2 = self_num + mul_num;

				if (temp1 >= temp2)
				{
					break;
				}
				self_data[i+index-mul_num] = hx_touch_data->hx_rawdata_buf[i*2 + 4];	//4: RawData Header
				self_data[i+index-mul_num+1] = hx_touch_data->hx_rawdata_buf[i*2 + 4 + 1];
			}
		}
	}

}
uint8_t himax_read_DD_status(uint8_t *cmd_set, uint8_t *tmp_data)
{
	int cnt = 0;
	uint8_t req_size = cmd_set[0];
	uint8_t cmd_addr[4] = {0xFC, 0x00, 0x00, 0x90}; //0x900000FC -> cmd and hand shaking
	uint8_t tmp_addr[4] = {0x80, 0x7F, 0x00, 0x10}; //0x10007F80 -> data space

	cmd_set[3] = 0xAA;
	himax_register_write(private_ts->client, cmd_addr, 4, cmd_set, 0);

	I("%s: cmd_set[0] = 0x%02X,cmd_set[1] = 0x%02X,cmd_set[2] = 0x%02X,cmd_set[3] = 0x%02X\n", __func__,cmd_set[0],cmd_set[1],cmd_set[2],cmd_set[3]);

	for (cnt = 0; cnt < 100; cnt++) //doing hand shaking 0xAA -> 0xBB
	{
		himax_register_read(private_ts->client, cmd_addr, 4, tmp_data, false);
		//I("%s: tmp_data[0] = 0x%02X,tmp_data[1] = 0x%02X,tmp_data[2] = 0x%02X,tmp_data[3] = 0x%02X, cnt=%d\n", __func__,tmp_data[0],tmp_data[1],tmp_data[2],tmp_data[3],cnt);
		msleep(10);
		if(tmp_data[3] == 0xBB)
		{
			I("%s Data ready goto moving data\n", __func__);
			break;
		}
		else if(cnt >= 99)
		{
			I("%s Data not ready in FW \n", __func__);
			return FW_NOT_READY;
		}
	}
	himax_register_read(private_ts->client, tmp_addr, req_size, tmp_data, false);
	return NO_ERR;
}

int himax_read_FW_status(uint8_t *state_addr, uint8_t *tmp_addr)
{
	//0x10007F44
	tmp_addr[3] = 0x10;
	tmp_addr[2] = 0x00;
	tmp_addr[1] = 0x7F;
	tmp_addr[0] = 0x44;
	//0x900000F8
	state_addr[3] = 0x90;
	state_addr[2] = 0x00;
	state_addr[1] = 0x00;
	state_addr[0] = 0xF8;

	return NO_ERR;

}

#if defined(HX_SMART_WAKEUP)||defined(HX_HIGH_SENSE)||defined(HX_USB_DETECT_GLOBAL)
void himax_resend_cmd_func(bool suspended)
{
	struct himax_ts_data *ts;

	ts = private_ts;

#ifdef HX_SMART_WAKEUP
	himax_set_SMWP_enable(ts->client,ts->SMWP_enable,suspended);
#endif
#ifdef HX_HIGH_SENSE
	himax_set_HSEN_enable(ts->client,ts->HSEN_enable,suspended);
#endif
#ifdef HX_USB_DETECT_GLOBAL
	himax_cable_detect_func(true);
#endif
}
#endif

void himax_resume_ic_action(struct i2c_client *client)
{
	return;
}

void himax_suspend_ic_action(struct i2c_client *client)
{
	return;
}