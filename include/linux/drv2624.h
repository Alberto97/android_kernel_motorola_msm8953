#ifndef __DRV2624_H__
#define __DRV2624_H__
/*
** =============================================================================
** Copyright (c)2016  Texas Instruments Inc.
**
** This program is free software; you can redistribute it and/or
** modify it under the terms of the GNU General Public License
** as published by the Free Software Foundation; either version 2
** of the License, or (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** File:
**     drv2624.h
**
** Description:
**     Header file for drv2624.c
**
** =============================================================================
*/

#include <linux/regmap.h>
#include <linux/timer.h>
#include <linux/workqueue.h>
#include <../../../drivers/staging/android/timed_output.h>
#include <linux/hrtimer.h>
#include <linux/wakelock.h>
#include <linux/mutex.h>
#include <linux/cdev.h>
#include <linux/i2c.h>

#define HAPTICS_DEVICE_NAME "drv2624"

#define	DRV2624_REG_ID				0x00
#define	DRV2624_ID					0x02

#define	DRV2624_REG_STATUS			0x01
#define	DIAG_MASK					0x80
#define	DIAG_SUCCESS				0x00
#define	DIAG_SHIFT					0x07
#define	INT_MASK					0x1f
#define	PRG_ERR_MASK				0x10
#define	PROCESS_DONE_MASK			0x08
#define	ULVO_MASK					0x04
#define	OVERTEMPRATURE_MASK			0x02
#define	OVERCURRENT_MASK			0x01

#define	DRV2624_REG_INT_ENABLE		0x02
#define	INT_MASK_ALL				0x1f
#define	INT_ENABLE_ALL				0x00

#define	DRV2624_REG_DIAG_Z			0x03

#define	DRV2624_REG_MODE			0x07
#define	DRV2624_MODE_MASK				0x03
#define	MODE_RTP					0x00
#define	MODE_WAVEFORM_SEQUENCER		0x01
#define	MODE_DIAGNOSTIC				0x02
#define	MODE_CALIBRATION			0x03
#define	PINFUNC_MASK				0x0c
#define	PINFUNC_INT					0x02
#define	PINFUNC_SHIFT				0x02

#define	DRV2624_REG_CONTROL1		0x08
#define	ACTUATOR_MASK				0x80
#define	ACTUATOR_SHIFT				7
#define	LOOP_MASK					0x40
#define	LOOP_SHIFT					6
#define	AUTOBRK_OK_MASK				0x10
#define	AUTOBRK_OK_ENABLE			0x10

#define	DRV2624_REG_GO				0x0c

#define	DRV2624_REG_CONTROL2		0x0d
#define	LIB_LRA						0x02
#define	LIB_ERM						0x03
#define	LIB_MASK					0xc0
#define	LIB_SHIFT					0x06
#define	SCALE_MASK					0x03
#define	INTERVAL_MASK				0x20
#define	INTERVAL_SHIFT				0x05

#define	DRV2624_REG_RTP_INPUT		0x0e

#define	DRV2624_REG_SEQUENCER_1		0x0f

#define	DRV2624_REG_SEQ_LOOP_1		0x17

#define	DRV2624_REG_SEQ_LOOP_2		0x18

#define	DRV2624_REG_MAIN_LOOP		0x19

#define	DRV2624_REG_RATED_VOLTAGE	0x1f

#define	DRV2624_REG_OVERDRIVE_CLAMP	0x20

#define	DRV2624_REG_CAL_COMP		0x21

#define	DRV2624_REG_CAL_BEMF		0x22

#define	DRV2624_REG_LOOP_CONTROL	0x23
#define	BEMFGAIN_MASK				0x03

#define	DRV2624_REG_DRIVE_TIME		0x27
#define	DRIVE_TIME_MASK				0x1f

#define	DRV2624_REG_SAMPLE_TIME		0x29
#define	SAMPLE_TIME_MASK				0x0c

#define	MINFREQ_SEL_45HZ			0x01
#define	MINFREQ_SEL_MASK			0x80
#define	MINFREQ_SEL_SHIFT			0x07

#define	DRV2624_REG_AUTO_CAL_TIME		0x2A

#define	DRV2624_REG_OL_PERIOD_H		0x2e

#define	DRV2624_REG_OL_PERIOD_L		0x2f

#define	DRV2624_REG_DIAG_K			0x30

#define	DRV2624_REG_RAM_ADDR_UPPER		0xfd
#define	DRV2624_REG_RAM_ADDR_LOWER		0xfe
#define	DRV2624_REG_RAM_DATA			0xff

#define GO_BIT_POLL_INTERVAL    15
#define STANDBY_WAKE_DELAY      1
#define WAKE_STANDBY_DELAY      3

/* Commands */
#define HAPTIC_CMDID_PLAY_SINGLE_EFFECT     0x01
#define HAPTIC_CMDID_PLAY_EFFECT_SEQUENCE   0x02
#define HAPTIC_CMDID_PLAY_TIMED_EFFECT      0x03
#define HAPTIC_CMDID_GET_DEV_ID             0x04
#define HAPTIC_CMDID_RUN_DIAG               0x05
#define HAPTIC_CMDID_AUDIOHAPTIC_ENABLE     0x06
#define HAPTIC_CMDID_AUDIOHAPTIC_DISABLE    0x07
#define HAPTIC_CMDID_AUDIOHAPTIC_GETSTATUS  0x08

#define HAPTIC_CMDID_REG_WRITE		0x09
#define HAPTIC_CMDID_REG_READ		0x0a
#define HAPTIC_CMDID_REG_SETBIT		0x0b

#define HAPTIC_CMDID_PATTERN_RTP      0x0c
#define HAPTIC_CMDID_RTP_SEQUENCE	  0x0d
#define HAPTIC_CMDID_GET_EFFECT_COUNT		0x10
#define HAPTIC_CMDID_UPDATE_FIRMWARE		0x11
#define HAPTIC_CMDID_READ_FIRMWARE			0x12
#define	HAPTIC_CMDID_AUTO_CAL				0x13

#define HAPTIC_CMDID_STOP                   0xFF

#define MAX_TIMEOUT	10000	/* 10s */
#define	MAX_READ_BYTES	0xff
#define	DRV2624_SEQUENCER_SIZE	8

#define	WORK_IDLE					0
#define	WORK_VIBRATOR				0x01
#define	WORK_IRQ					0x02

#define	YES		1
#define NO		0
#define	GO		1
#define STOP	0

enum actuator_type {
	ERM,
	LRA
};

enum work_mode {
	NORMAL,
	REDUCED
};

enum loop_type {
	CLOSE_LOOP,
	OPEN_LOOP
};

struct actuator_data {
	enum actuator_type meActuatorType;
	enum work_mode meWorkMode;
	unsigned char mnRatedVoltage;
	unsigned char mnOverDriveClampVoltage;
	unsigned char mnRatedVoltageReduced;
	unsigned char mnOverDriveClampVoltageReduced;
	unsigned char mnLRAFreq;
	unsigned char mnSampleTime;

};

enum wave_seq_loop {
	SEQ_NO_LOOP,
	SEQ_LOOP_ONCE,
	SEQ_LOOP_TWICE,
	SEQ_LOOP_TRIPPLE
};

enum wave_main_loop {
	MAIN_NO_LOOP,
	MAIN_LOOP_ONCE,
	MAIN_LOOP_TWICE,
	MAIN_LOOP_3_TIMES,
	MAIN_LOOP_4_TIMES,
	MAIN_LOOP_5_TIMES,
	MAIN_LOOP_6_TIMES,
	MAIN_LOOP_INFINITELY
};

enum wave_main_scale {
	PERCENTAGE_100,
	PERCENTAGE_75,
	PERCENTAGE_50,
	PERCENTAGE_25
};

enum wave_main_interval {
	INTERVAL_5MS,
	INTERVAL_1MS
};

struct drv2624_waveform {
	enum wave_seq_loop meLoop;
	unsigned char mnIndex;
};

struct drv2624_waveform_sequencer {
	enum wave_main_loop meLoop;
	struct drv2624_waveform msWaveform[DRV2624_SEQUENCER_SIZE];
};

struct drv2624_platform_data {
	int mnGpioNRST;
	int mnGpioNPWR;
	int mnGpioINT;
	int mnGpioVCTRL;
	enum loop_type meLoop;
	struct actuator_data msActuator;
	bool auto_cal;
	u8 auto_cal_time;
};

#define DRV2624_MAGIC	0x2624

struct drv2624_fw_header {
	unsigned int fw_magic;
	unsigned int fw_size;
	unsigned int fw_date;
	unsigned int fw_chksum;
	unsigned int fw_effCount;
};

struct drv2624_data {
	struct drv2624_platform_data msPlatData;
	unsigned char mnDeviceID;
	struct i2c_client *i2c_client;
	struct device *dev;
	struct regmap *mpRegmap;
	unsigned char mnIntStatus;
	struct drv2624_waveform_sequencer msWaveformSequencer;
	unsigned char mnFileCmd;
	int mnVibratorPlaying;
	char mnWorkMode;
	unsigned char mnCurrentReg;
	int mnCurrentVibrationTime;
	bool factory_mode;
	bool reduced_pwr_capable;

	atomic_t reduce_pwr;

	struct wake_lock wklock;
	struct hrtimer timer;
	struct mutex lock;
	struct work_struct vibrator_work;
	struct timed_output_dev to_dev;

	struct drv2624_fw_header msFwHeader;
	unsigned char mnFwAddUpper;
	unsigned char mnFwAddLower;
};

struct drv2624_seq_loop {
	enum wave_seq_loop mpLoop[DRV2624_SEQUENCER_SIZE];
};

struct drv2624_wave_seq {
	unsigned char mpWaveIndex[DRV2624_SEQUENCER_SIZE];
};

struct drv2624_wave_setting {
	enum wave_main_loop meLoop;
	enum wave_main_scale meScale;
	enum wave_main_interval meInterval;
};

struct drv2624_autocal_result {
	int mnFinished;
	unsigned char mnResult;
	unsigned char mnCalComp;
	unsigned char mnCalBemf;
	unsigned char mnCalGain;
};

struct drv2624_diag_result {
	int mnFinished;
	unsigned char mnResult;
	unsigned char mnDiagZ;
	unsigned char mnDiagK;
};

#define	DRV2624_MAGIC_NUMBER	0x32363234	/* '2624' */

#define	DRV2624_SET_SEQ_LOOP	_IOWR(DRV2624_MAGIC_NUMBER, 1, \
					struct drv2624_seq_loop *)
#define	DRV2624_SET_MAIN	_IOWR(DRV2624_MAGIC_NUMBER, 2, \
					struct drv2624_wave_setting *)
#define	DRV2624_SET_WAV_SEQ	_IOWR(DRV2624_MAGIC_NUMBER, 3, \
					struct drv2624_wave_seq *)
#define	DRV2624_WAVSEQ_PLAY	_IOWR(DRV2624_MAGIC_NUMBER, 4, unsigned long)
#define	DRV2624_STOP		_IOWR(DRV2624_MAGIC_NUMBER, 5, unsigned long)
#define	DRV2624_RUN_DIAGNOSTIC	_IOWR(DRV2624_MAGIC_NUMBER, 6, unsigned long)
#define	DRV2624_GET_DIAGRESULT	_IOWR(DRV2624_MAGIC_NUMBER, 7, \
					struct drv2624_diag_result *)
#define	DRV2624_RUN_AUTOCAL	_IOWR(DRV2624_MAGIC_NUMBER, 8, unsigned long)
#define	DRV2624_GET_CALRESULT	_IOWR(DRV2624_MAGIC_NUMBER, 9, \
					 struct drv2624_autocal_result *)

#endif
