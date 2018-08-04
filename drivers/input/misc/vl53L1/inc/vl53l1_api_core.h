
/*
* Copyright (c) 2016, STMicroelectronics - All Rights Reserved
*
* This file is part of VL53L1 Core and is dual licensed, either
* 'STMicroelectronics Proprietary license'
* or 'BSD 3-clause "New" or "Revised" License' , at your option.
*
********************************************************************************
*
* 'STMicroelectronics Proprietary license'
*
********************************************************************************
*
*License terms : STMicroelectronics Proprietary in accordance with licensing
* terms at www.st.com/sla0044
*
* STMicroelectronics confidential
* Reproduction and Communication of this document is strictly prohibited unless
* specifically authorized in writing by STMicroelectronics.
*
*
********************************************************************************
*
* Alternatively, VL53L1 Core may be distributed under the terms of
* 'BSD 3-clause "New" or "Revised" License', in which case the following
* provisions apply instead of the ones
* mentioned above :
*
********************************************************************************
*
*License terms : BSD 3-clause "New" or "Revised" License.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are met:
*
* 1. Redistributions of source code must retain the above copyright notice, this
* list of conditions and the following disclaimer.
*
* 2. Redistributions in binary form must reproduce the above copyright notice,
* this list of conditions and the following disclaimer in the documentation
* and/or other materials provided with the distribution.
*
* 3. Neither the name of the copyright holder nor the names of its contributors
* may be used to endorse or promote products derived from this software
* without specific prior written permission.
*
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
* AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
* IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
* DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
* FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
* DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
* SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
* CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
* OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
* OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*
*
********************************************************************************
*
*/




































#ifndef _VL53L1_API_CORE_H_
#define _VL53L1_API_CORE_H_

#include "vl53l1_platform.h"

#ifdef __cplusplus
extern "C" {
#endif












VL53L1_Error VL53L1_get_version(
	VL53L1_DEV            Dev,
	VL53L1_ll_version_t  *pversion);
















VL53L1_Error VL53L1_is_fpga_system(
	VL53L1_DEV        Dev,
	uint8_t          *pfpga_system);












VL53L1_Error VL53L1_get_device_firmware_version(
	VL53L1_DEV         Dev,
	uint16_t          *pfw_version);
















VL53L1_Error VL53L1_data_init(
	VL53L1_DEV         Dev,
	uint8_t            read_p2p_data);

















VL53L1_Error VL53L1_read_p2p_data(
	VL53L1_DEV      Dev);













VL53L1_Error VL53L1_software_reset(
	VL53L1_DEV      Dev);

























VL53L1_Error VL53L1_set_part_to_part_data(
	VL53L1_DEV                            Dev,
	VL53L1_customer_nvm_managed_t        *pcustomer,
	VL53L1_additional_offset_cal_data_t  *poff_cal_data,
	VL53L1_dmax_calibration_data_t       *pdmax_cal,
	VL53L1_xtalk_histogram_data_t        *pxtalkhisto);























VL53L1_Error VL53L1_get_part_to_part_data(
	VL53L1_DEV                            Dev,
	VL53L1_customer_nvm_managed_t        *pcustomer,
	VL53L1_additional_offset_cal_data_t  *poff_cal_data,
	VL53L1_dmax_calibration_data_t       *pdmax_cal,
	VL53L1_xtalk_histogram_data_t        *pxtalkhisto);















VL53L1_Error VL53L1_set_inter_measurement_period_ms(
	VL53L1_DEV          Dev,
	uint32_t            inter_measurement_period_ms);















VL53L1_Error VL53L1_get_inter_measurement_period_ms(
	VL53L1_DEV          Dev,
	uint32_t           *pinter_measurement_period_ms);

















VL53L1_Error VL53L1_set_timeouts_us(
	VL53L1_DEV          Dev,
	uint32_t            mm_config_timeout_us,
	uint32_t            range_config_timeout_us);

















VL53L1_Error VL53L1_get_timeouts_us(
	VL53L1_DEV          Dev,
	uint32_t           *pmm_config_timeout_us,
	uint32_t           *prange_config_timeout_us);






















VL53L1_Error VL53L1_set_calibration_repeat_period(
	VL53L1_DEV          Dev,
	uint16_t            cal_config__repeat_period);













VL53L1_Error VL53L1_get_calibration_repeat_period(
	VL53L1_DEV          Dev,
	uint16_t           *pcal_config__repeat_period);














VL53L1_Error VL53L1_set_sequence_config_bit(
	VL53L1_DEV                   Dev,
	VL53L1_DeviceSequenceConfig  bit_id,
	uint8_t                      value);














VL53L1_Error VL53L1_get_sequence_config_bit(
	VL53L1_DEV                   Dev,
	VL53L1_DeviceSequenceConfig  bit_id,
	uint8_t                     *pvalue);













VL53L1_Error VL53L1_set_interrupt_polarity(
	VL53L1_DEV                       Dev,
	VL53L1_DeviceInterruptPolarity  interrupt_polarity);













VL53L1_Error VL53L1_get_interrupt_polarity(
	VL53L1_DEV                      Dev,
	VL53L1_DeviceInterruptPolarity  *pinterrupt_polarity);












VL53L1_Error VL53L1_set_range_ignore_threshold(
	VL53L1_DEV              Dev,
	uint16_t                range_ignore_threshold_mcps);

















VL53L1_Error VL53L1_get_range_ignore_threshold(
	VL53L1_DEV              Dev,
	uint16_t               *prange_ignore_threshold_mcps_internal,
	uint16_t               *prange_ignore_threshold_mcps_current);













VL53L1_Error VL53L1_set_user_zone(
	VL53L1_DEV          Dev,
	VL53L1_user_zone_t *puser_zone);













VL53L1_Error VL53L1_get_user_zone(
	VL53L1_DEV          Dev,
	VL53L1_user_zone_t *puser_zone);













VL53L1_Error VL53L1_get_mode_mitigation_roi(
	VL53L1_DEV          Dev,
	VL53L1_user_zone_t *pmm_roi);


















VL53L1_Error VL53L1_set_zone_config(
	VL53L1_DEV             Dev,
	VL53L1_zone_config_t  *pzone_cfg);















VL53L1_Error VL53L1_get_zone_config(
	VL53L1_DEV             Dev,
	VL53L1_zone_config_t  *pzone_cfg);































VL53L1_Error VL53L1_set_preset_mode(
	VL53L1_DEV                   Dev,
	VL53L1_DevicePresetModes     device_preset_mode,
	uint32_t                     mm_config_timeout_us,
	uint32_t                     range_config_timeout_us,
	uint32_t                     inter_measurement_period_ms);













VL53L1_Error VL53L1_set_zone_preset(
	VL53L1_DEV               Dev,
	VL53L1_DeviceZonePreset  zone_preset);














VL53L1_Error VL53L1_enable_xtalk_compensation(
	VL53L1_DEV                 Dev);














VL53L1_Error VL53L1_disable_xtalk_compensation(
	VL53L1_DEV                 Dev);














void VL53L1_get_xtalk_compensation_enable(
	VL53L1_DEV    Dev,
	uint8_t       *pcrosstalk_compensation_enable);













































VL53L1_Error VL53L1_init_and_start_range(
	VL53L1_DEV                      Dev,
	uint8_t                         measurement_mode,
	VL53L1_DeviceConfigLevel        device_config_level);













VL53L1_Error VL53L1_stop_range(
	VL53L1_DEV  Dev);




























VL53L1_Error VL53L1_get_measurement_results(
	VL53L1_DEV                  Dev,
	VL53L1_DeviceResultsLevel   device_result_level);




























VL53L1_Error VL53L1_get_device_results(
	VL53L1_DEV                 Dev,
	VL53L1_DeviceResultsLevel  device_result_level,
	VL53L1_range_results_t    *prange_results);




























VL53L1_Error VL53L1_clear_interrupt_and_enable_next_range(
	VL53L1_DEV       Dev,
	uint8_t          measurement_mode);










VL53L1_Error VL53L1_get_histogram_bin_data(
	VL53L1_DEV                   Dev,
	VL53L1_histogram_bin_data_t *phist_data);











void VL53L1_copy_sys_and_core_results_to_range_results(
	VL53L1_system_results_t          *psys,
	VL53L1_core_results_t            *pcore,
	VL53L1_range_results_t           *presults);









VL53L1_Error VL53L1_set_zone_dss_config(
	  VL53L1_DEV                      Dev,
	  VL53L1_zone_private_dyn_cfg_t  *pzone_dyn_cfg);
























VL53L1_Error VL53L1_calc_ambient_dmax(
	VL53L1_DEV    Dev,
	uint16_t      target_reflectance,
	int16_t      *pambient_dmax_mm);


















VL53L1_Error VL53L1_set_GPIO_interrupt_config(
	VL53L1_DEV                      Dev,
	VL53L1_GPIO_Interrupt_Mode	intr_mode_distance,
	VL53L1_GPIO_Interrupt_Mode	intr_mode_rate,
	uint8_t				intr_new_measure_ready,
	uint8_t				intr_no_target,
	uint8_t				intr_combined_mode,
	uint16_t			thresh_distance_high,
	uint16_t			thresh_distance_low,
	uint16_t			thresh_rate_high,
	uint16_t			thresh_rate_low
	);









VL53L1_Error VL53L1_set_GPIO_interrupt_config_struct(
	VL53L1_DEV                      Dev,
	VL53L1_GPIO_interrupt_config_t	intconf);










VL53L1_Error VL53L1_get_GPIO_interrupt_config(
	VL53L1_DEV                      Dev,
	VL53L1_GPIO_interrupt_config_t	*pintconf);

#ifdef __cplusplus
}
#endif

#endif

