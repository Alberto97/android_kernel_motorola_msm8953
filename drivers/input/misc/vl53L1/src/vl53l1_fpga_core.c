
/*
* Copyright (c) 2016, STMicroelectronics - All Rights Reserved
*
* This file is part of VL53L1 Core and is dual licensed, either 'STMicroelectronics Proprietary license'
* or 'BSD 3-clause "New" or "Revised" License' , at your option.
*
********************************************************************************
*
* 'STMicroelectronics Proprietary license'
*
********************************************************************************
*
* License terms: STMicroelectronics Proprietary in accordance with licensing terms at www.st.com/sla0044
*
* STMicroelectronics confidential
* Reproduction and Communication of this document is strictly prohibited unless
* specifically authorized in writing by STMicroelectronics.
*
*
********************************************************************************
*
* Alternatively, VL53L1 Core may be distributed under the terms of
* 'BSD 3-clause "New" or "Revised" License', in which case the following provisions apply instead of the ones
* mentioned above :
*
********************************************************************************
*
* License terms: BSD 3-clause "New" or "Revised" License.
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





































#include "vl53l1_ll_def.h"
#include "vl53l1_platform.h"
#include "vl53l1_register_map.h"
#include "vl53l1_register_settings.h"
#include "vl53l1_core.h"
#include "vl53l1_fpga_core.h"


#define LOG_FUNCTION_START(fmt, ...) \
	_LOG_FUNCTION_START(VL53L1_TRACE_MODULE_CORE, fmt, ##__VA_ARGS__)
#define LOG_FUNCTION_END(status, ...) \
	_LOG_FUNCTION_END(VL53L1_TRACE_MODULE_CORE, status, ##__VA_ARGS__)
#define LOG_FUNCTION_END_FMT(status, fmt, ...) \
	_LOG_FUNCTION_END_FMT(VL53L1_TRACE_MODULE_CORE, status, fmt, ##__VA_ARGS__)


#define trace_print(level, ...) \
	VL53L1_trace_print_module_function(VL53L1_TRACE_MODULE_CORE, level, VL53L1_TRACE_FUNCTION_NONE, ##__VA_ARGS__)



VL53L1_Error VL53L1_FCTN_00143(
	VL53L1_DEV     Dev,
	uint8_t       *pready)
{








	VL53L1_Error status = VL53L1_ERROR_NONE;
	VL53L1_LLDriverData_t *pdev = VL53L1DevStructGetLLDriverHandle(Dev);

	uint8_t  comms_buffer[VL53L1_MAX_I2C_XFER_SIZE];

	LOG_FUNCTION_START("");




	status = VL53L1_ReadMulti(
					Dev,
					VL53L1_DEF_00169,
					comms_buffer,
					5);

	if (status == VL53L1_ERROR_NONE) {

		pdev->VL53L1_PRM_00047.VL53L1_PRM_00315 =
				comms_buffer[0];
		pdev->VL53L1_PRM_00047.VL53L1_PRM_00316 =
				comms_buffer[1];
		pdev->VL53L1_PRM_00047.VL53L1_PRM_00317 =
				comms_buffer[2];
		pdev->VL53L1_PRM_00047.VL53L1_PRM_00318 =
				comms_buffer[3];
		pdev->VL53L1_PRM_00047.VL53L1_PRM_00319 =
				comms_buffer[4];

		if (((pdev->VL53L1_PRM_00047.VL53L1_PRM_00315 & 0x1F) == 0x1F) &&
			((pdev->VL53L1_PRM_00047.VL53L1_PRM_00316   & 0x1F) == 0x1F))
			*pready = 0x01;
		else
			*pready = 0x00;
	}

	LOG_FUNCTION_END(status);

	return status;
}
