/****************************************************************************
 * FileName:        TypeC.c
 * Processor:       PIC32MX250F128B
 * Compiler:        MPLAB XC32
 * Company:         Fairchild Semiconductor
 *
 * Author           Date          Comment
 *~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 * M. Smith         12/04/2014    Initial Version
 *
 *
 *~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 * Software License Agreement:
 *
 * The software supplied herewith by Fairchild Semiconductor (the ?Company?)
 * is supplied to you, the Company's customer, for exclusive use with its
 * USB Type C / USB PD products.  The software is owned by the Company and/or
 * its supplier, and is protected under applicable copyright laws.
 * All rights are reserved. Any use in violation of the foregoing restrictions
 * may subject the user to criminal sanctions under applicable laws, as well
 * as to civil liability for the breach of the terms and conditions of this
 * license.
 *
 * THIS SOFTWARE IS PROVIDED IN AN ?AS IS? CONDITION. NO WARRANTIES,
 * WHETHER EXPRESS, IMPLIED OR STATUTORY, INCLUDING, BUT NOT LIMITED
 * TO, IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE APPLY TO THIS SOFTWARE. THE COMPANY SHALL NOT,
 * IN ANY CIRCUMSTANCES, BE LIABLE FOR SPECIAL, INCIDENTAL OR
 * CONSEQUENTIAL DAMAGES, FOR ANY REASON WHATSOEVER.
 *
 *****************************************************************************/
#include <linux/printk.h>
#include <linux/kernel.h>
#include <linux/power_supply.h>
#include "TypeC.h"
#include "fusb30X.h"
#include "AlternateModes.h"
#ifdef CONFIG_FSUSB42_MUX
#include <linux/fsusb42.h>
#endif
#include "PDPolicy.h"

#ifdef FSC_HAVE_VDM
#include "vdm/vdm_config.h"
#endif // FSC_HAVE_VDM

#ifdef FSC_DEBUG
#include "Log.h"
#endif // FSC_DEBUG
#include "core.h"
#include "../Platform_Linux/fusb30x_global.h"
/////////////////////////////////////////////////////////////////////////////
//      Variables accessible outside of the TypeC state machine
/////////////////////////////////////////////////////////////////////////////

DeviceReg_t Registers = { {0} };	// Variable holding the current status of the device registers

FSC_BOOL USBPDActive;		// Variable to indicate whether the USB PD state machine is active or not
FSC_BOOL USBPDEnabled;		// Variable to indicate whether USB PD is enabled (by the host)
FSC_U32 PRSwapTimer;		// Timer used to bail out of a PR_Swap from the Type-C side if necessary
SourceOrSink sourceOrSink;	// Are we currently a source or a sink?
FSC_BOOL g_Idle;		// Set to be woken by interrupt_n

USBTypeCPort PortType;		// Variable indicating which type of port we are implementing
FSC_BOOL blnCCPinIsCC1;		// Flag to indicate if the CC1 pin has been detected as the CC pin
FSC_BOOL blnCCPinIsCC2;		// Flag to indicate if the CC2 pin has been detected as the CC pin
FSC_BOOL blnSMEnabled = FALSE;	// Flag to indicate whether the TypeC state machine is enabled
ConnectionState ConnState;	// Variable indicating the current connection state
FSC_U8 TypeCSubState = 0;	// Substate to allow for non-blocking checks
#ifdef FSC_DEBUG
StateLog TypeCStateLog;		// Log for tracking state transitions and times
volatile FSC_U16 Timer_S;	// Tracks seconds elapsed for log timestamp
volatile FSC_U16 Timer_tms;	// Tracks tenths of milliseconds elapsed for log timestamp
#endif // FSC_DEBUG
FSC_BOOL gChargerAuthenticated;
/////////////////////////////////////////////////////////////////////////////
//      Variables accessible only inside TypeC state machine
/////////////////////////////////////////////////////////////////////////////

#ifdef FSC_HAVE_DRP
static FSC_BOOL blnSrcPreferred;	// Flag to indicate whether we prefer the Src role when in DRP
static FSC_BOOL blnSnkPreferred;	// Flag to indicate whether we prefer the Snk role when in DRP
#endif // FSC_HAVE_DRP

#ifdef FSC_HAVE_ACCMODE
FSC_BOOL blnAccSupport;		// Flag to indicate whether the port supports accessories
#endif // FSC_HAVE_ACCMODE

FSC_U16 StateTimer;		// Timer used to validate proceeding to next state
FSC_U16 PDDebounce;		// Timer used for first level debouncing
FSC_U16 CCDebounce;		// Timer used for second level debouncing
FSC_U16 ToggleTimer;		// Timer used for CC swapping in the device
FSC_U16 DRPToggleTimer;		// Timer used for swapping from UnattachedSrc and UnattachedSnk
FSC_U16 OverPDDebounce;		// Timer used to ignore traffic less than tPDDebounce
CCTermType CC1TermPrevious;	// Active CC1 termination value
CCTermType CC2TermPrevious;	// Active CC2 termination value
CCTermType CC1TermCCDebounce;	// Debounced CC1 termination value
CCTermType CC2TermCCDebounce;	// Debounced CC2 termination value
CCTermType CC1TermPDDebounce;
CCTermType CC2TermPDDebounce;
CCTermType CC1TermPDDebouncePrevious;
CCTermType CC2TermPDDebouncePrevious;

USBTypeCCurrent SinkCurrent;	// Variable to indicate the current capability we have received
static USBTypeCCurrent SourceCurrent;	// Variable to indicate the current capability we are broadcasting
struct power_supply usbc_psy;
/* Flag to indicate Data lines on USB are disabled */
static bool usbDataDisabled;
#ifdef FM150911A
static FSC_U8 alternateModes = 1;	// Set to 1 to enable alternate modes
#else
static FSC_U8 alternateModes = 0;	// Set to 1 to enable alternate modes
#endif
FSC_U32 gRequestOpCurrent = 400;/*set default 4000mA*/
static regMask_t Mask;
static regMaskAdv_t MaskAdv;
static FSC_U8 bc_lvl;
int fusb_power_supply_set_property(struct power_supply *psy,
				 enum power_supply_property prop,
				 const union power_supply_propval *val)
{
	switch (prop) {
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		gRequestOpCurrent = val->intval/10000;
		core_send_sink_request();
		return 0;
	case POWER_SUPPLY_PROP_DISABLE_USB:
		usbDataDisabled = !!val->intval;
		break;
	case POWER_SUPPLY_PROP_WAKEUP:
		platform_run_wake_thread();
		break;
	case POWER_SUPPLY_PROP_MASK_INT:
		if (val->intval > 0) {
			Mask.byte = 0xFF;
			MaskAdv.byte[0] = 0xBF;
			MaskAdv.byte[1] = 0x01;
			DeviceWrite(regMask, 1, &Mask.byte);
			DeviceWrite(regMaska, 1, &MaskAdv.byte[0]);
			DeviceWrite(regMaskb, 1, &MaskAdv.byte[1]);
		} else {
			DeviceWrite(regMask, 1, &Registers.Mask.byte);
			DeviceWrite(regMaska, 1, &Registers.MaskAdv.byte[0]);
			DeviceWrite(regMaskb, 1, &Registers.MaskAdv.byte[1]);
		}
		return 0;
	default:
		return -EINVAL;
	}
	power_supply_changed(&usbc_psy);
	return 0;
}

int fusb_power_supply_is_writeable(struct power_supply *psy,
				 enum power_supply_property prop)
{
	int rc;

	switch (prop) {
	case POWER_SUPPLY_PROP_CURRENT_MAX:
	case POWER_SUPPLY_PROP_DISABLE_USB:
	case POWER_SUPPLY_PROP_MASK_INT:
	case POWER_SUPPLY_PROP_WAKEUP:
		rc = 1;
		break;
	default:
		rc = 0;
		break;
	}
	return rc;
}

int fusb_power_supply_get_property(struct power_supply *psy,
			    enum power_supply_property psp,
			    union power_supply_propval *val)
{
	switch (psp) {
	case POWER_SUPPLY_PROP_PRESENT:
		if ((ConnState > Unattached) && (ConnState < DelayUnattached))
			val->intval = 1;
		else
			val->intval = 0;
		break;
	case POWER_SUPPLY_PROP_ONLINE:
		if ((ConnState == PoweredAccessory) ||
		    (ConnState == UnsupportedAccessory) ||
		    (ConnState == DebugAccessory) ||
		    (ConnState == AudioAccessory) ||
		    (ConnState == AttachedSink) ||
		    (ConnState == AttachedSource))
			val->intval = 1;
		else
			val->intval = 0;
		break;
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		if (ConnState != AttachedSink)
			val->intval = 0;
		else if (SinkCurrent == utcc1p5A)
			val->intval = 1500000;
		else if (SinkCurrent == utcc3p0A)
			val->intval = 3000000;
		else if (SinkCurrent == utccDefault)
			val->intval = 500000;
		if (PolicyHasContract)
			val->intval = gChargerMaxCurrent * 10000;
		break;
	case POWER_SUPPLY_PROP_AUTHENTIC:
		val->intval = gChargerAuthenticated;
		break;
	case POWER_SUPPLY_PROP_TYPE:
		val->intval = psy->type;
		break;
	case POWER_SUPPLY_PROP_DISABLE_USB:
		val->intval = usbDataDisabled;
		break;
	case POWER_SUPPLY_PROP_WAKEUP:
		val->intval = 0;
		break;
	case POWER_SUPPLY_PROP_MASK_INT:
		DeviceRead(regMask, 1, &Mask.byte);
		DeviceRead(regMaska, 1, &MaskAdv.byte[0]);
		DeviceRead(regMaskb, 1, &MaskAdv.byte[1]);
		val->intval = (Mask.byte << 16) |
					(MaskAdv.byte[0] << 8) |
					MaskAdv.byte[1];
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX:
		if (gChargerAuthenticated)
			val->intval = gChargerOpCurrent*10000;
		else
			val->intval = 0;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

/////////////////////////////////////////////////////////////////////////////
// Tick at 1ms
/////////////////////////////////////////////////////////////////////////////
void TypeCTickAt100us(void)
{
	if ((StateTimer < T_TIMER_DISABLE) && (StateTimer > 0))
		StateTimer--;
	if ((PDDebounce < T_TIMER_DISABLE) && (PDDebounce > 0))
		PDDebounce--;
	if ((CCDebounce < T_TIMER_DISABLE) && (CCDebounce > 0))
		CCDebounce--;
	if ((ToggleTimer < T_TIMER_DISABLE) && (ToggleTimer > 0))
		ToggleTimer--;
	if (PRSwapTimer)
		PRSwapTimer--;
	if ((OverPDDebounce < T_TIMER_DISABLE) && (OverPDDebounce > 0))
		OverPDDebounce--;
	if ((DRPToggleTimer < T_TIMER_DISABLE) && (DRPToggleTimer > 0))
		DRPToggleTimer--;
}

/////////////////////////////////////////////////////////////////////////////
// Tick at 0.1ms
/////////////////////////////////////////////////////////////////////////////
#ifdef FSC_DEBUG
void LogTickAt100us(void)
{
	Timer_tms++;
	if (Timer_tms == 1000) {
		Timer_S++;
		Timer_tms = 0;
	}
}
#endif // FSC_DEBUG

// HEADER: Reads all the initial values from the 302
void InitializeRegisters(void)
{
	DeviceRead(regDeviceID, 1, &Registers.DeviceID.byte);
	DeviceRead(regSwitches0, 1, &Registers.Switches.byte[0]);
	DeviceRead(regSwitches1, 1, &Registers.Switches.byte[1]);
	DeviceRead(regMeasure, 1, &Registers.Measure.byte);
	DeviceRead(regSlice, 1, &Registers.Slice.byte);
	DeviceRead(regControl0, 1, &Registers.Control.byte[0]);
	DeviceRead(regControl1, 1, &Registers.Control.byte[1]);
	DeviceRead(regControl2, 1, &Registers.Control.byte[2]);
	DeviceRead(regControl3, 1, &Registers.Control.byte[3]);
	DeviceRead(regMask, 1, &Registers.Mask.byte);
	DeviceRead(regPower, 1, &Registers.Power.byte);
	DeviceRead(regReset, 1, &Registers.Reset.byte);
	DeviceRead(regOCPreg, 1, &Registers.OCPreg.byte);
	DeviceRead(regMaska, 1, &Registers.MaskAdv.byte[0]);
	DeviceRead(regMaskb, 1, &Registers.MaskAdv.byte[1]);
	DeviceRead(regControl4, 1, &Registers.Control4.byte);
	DeviceRead(regStatus0a, 1, &Registers.Status.byte[0]);
	DeviceRead(regStatus1a, 1, &Registers.Status.byte[1]);
	//DeviceRead(regInterrupta, 1, &Registers.Status.byte[2]); //Disabled reading interrupts
	//DeviceRead(regInterruptb, 1, &Registers.Status.byte[3]);
	DeviceRead(regStatus0, 1, &Registers.Status.byte[4]);
	DeviceRead(regStatus1, 1, &Registers.Status.byte[5]);
	//DeviceRead(regInterrupt, 1, &Registers.Status.byte[6]);
	Registers.Control4.TOG_USRC_EXIT = 1;
	DeviceWrite(regControl4, 1, &Registers.Control4.byte);
}

/*******************************************************************************
 * Function:        InitializeTypeCVariables
 * Input:           None
 * Return:          None
 * Description:     Initializes the TypeC state machine variables
 ******************************************************************************/
void InitializeTypeCVariables(void)
{
	InitializeRegisters();	// Copy 302 registers to register struct

	Registers.Control.INT_MASK = 0;	// Enable interrupt Pin
	DeviceWrite(regControl0, 1, &Registers.Control.byte[0]);

	Registers.Control.TOG_RD_ONLY = 1;	// Do not stop toggle for Ra
	DeviceWrite(regControl2, 1, &Registers.Control.byte[2]);

	SourceCurrent = utcc1p5A;	// Set 3.0A host current
	updateSourceCurrent();

	blnSMEnabled = FALSE;	// Enable the TypeC state machine by default

#ifdef FSC_HAVE_ACCMODE
	blnAccSupport = TRUE;	// Enable accessory support by default
#endif // FSC_HAVE_ACCMODE

	blnSMEnabled = FALSE;	// Enable the TypeC state machine by default

#ifdef FSC_HAVE_DRP
	blnSrcPreferred = FALSE;	// Clear the source preferred flag by default
	blnSnkPreferred = FALSE;	// Clear the sink preferred flag by default
#endif // FSC_HAVE_DRP

	g_Idle = FALSE;

	// Try setting this based on 302 config
	PortType = USBTypeC_UNDEFINED;

	switch (Registers.Control.MODE) {
	case 0b10:
#ifdef FSC_HAVE_SNK
		PortType = USBTypeC_Sink;
#endif // FSC_HAVE_SNK
		break;
	case 0b11:
#ifdef FSC_HAVE_SRC
		PortType = USBTypeC_Source;
#endif // FSC_HAVE_SRC
		break;
	case 0b01:
#ifdef FSC_HAVE_DRP
		PortType = USBTypeC_DRP;
#endif // FSC_HAVE_DRP
		break;
	default:
#ifdef FSC_HAVE_DRP
		PortType = USBTypeC_DRP;
#endif // FSC_HAVE_DRP
		break;
	}

	// If the setting isn't supported... One of these has to work.
	// Note that this gives an implicit priority of SNK->SRC->DRP.
	if (PortType == USBTypeC_UNDEFINED) {
#ifdef FSC_HAVE_SNK
		PortType = USBTypeC_Sink;
#endif // FSC_HAVE_SNK
#ifdef FSC_HAVE_SRC
		PortType = USBTypeC_Source;
#endif // FSC_HAVE_SRC
#ifdef FSC_HAVE_DRP
		PortType = USBTypeC_DRP;
#endif // FSC_HAVE_DRP
	}
	PortType = USBTypeC_DRP;
	//Disabled??
	//This gets changed anyway in init to delayUnattached
	ConnState = Disabled;	// Initialize to the disabled state    
	blnCCPinIsCC1 = FALSE;	// Clear the flag to indicate CC1 is CC
	blnCCPinIsCC2 = FALSE;	// Clear the flag to indicate CC2 is CC
	StateTimer = T_TIMER_DISABLE;	// Disable the state timer
	PDDebounce = T_TIMER_DISABLE;	// Disable the 1st debounce timer
	CCDebounce = T_TIMER_DISABLE;	// Disable the 2nd debounce timer
	ToggleTimer = T_TIMER_DISABLE;	// Disable the toggle timer
	resetDebounceVariables();	// Set all debounce variables to undefined

#ifdef FSC_HAVE_SNK
	SinkCurrent = utccNone;	// Clear the current advertisement initially
#endif // FSC_HAVE_SNK

#ifdef FSC_HAVE_SRC
	SourceCurrent = utccDefault;	// Set the current advertisement to the default
	updateSourceCurrent();
#endif // FSC_HAVE_SRC

	USBPDActive = FALSE;	// Clear the USB PD active flag
	USBPDEnabled = TRUE;	// Set the USB PD enabled flag until enabled by the host
	PRSwapTimer = 0;	// Clear the PR Swap timer
	IsHardReset = FALSE;	// Initialize to no Hard Reset
	TypeCSubState = 0;	// Initialize substate to 0
	SwitchState = 0;
	gChargerAuthenticated = FALSE;
}

void InitializeTypeC(void)
{
#ifdef FSC_DEBUG
	Timer_tms = 0;
	Timer_S = 0;
	InitializeStateLog(&TypeCStateLog);	// Initialize log
#endif // FSC_DEBUG

	SetStateDelayUnattached();
}

void DisableTypeCStateMachine(void)
{
	blnSMEnabled = FALSE;
}

void EnableTypeCStateMachine(void)
{
	blnSMEnabled = TRUE;

#ifdef FSC_DEBUG
	Timer_tms = 0;
	Timer_S = 0;
#endif // FSC_DEBUG
}
/*******************************************************************************
 * Function:        StateMachineTypeC
 * Input:           None
 * Return:          None
 * Description:     This is the state machine for the entire USB PD
 *                  This handles all the communication between the master and
 *                  slave.  This function is called by the Timer3 ISR on a
 *                  sub-interval of the 1/4 UI in order to meet timing
 *                  requirements.
 ******************************************************************************/
void WakeStateMachineTypeC(void)
{
	FUSB_LOG("TypeC Kicking ConnState is %d USBPDActive is %d\n",
			 ConnState, USBPDActive);
	StateMachineTypeCImp();
	FUSB_LOG("TypeC End Kicking ConnState is %d USBPDActive is %d\n",
			 ConnState, USBPDActive);
}
void StateMachineTypeCImp(void)
{
	if (!blnSMEnabled)
		return;

	/*
	*Read the interrupta, interruptb, status0,
	*status1 and interrupt registers
	*/
	if (platform_get_device_irq_state())
		DeviceRead(regStatus0a, 7, &Registers.Status.byte[0]);
#ifdef FSC_INTERRUPT_TRIGGERED
	else if (PolicyState != PE_BIST_Test_Data) {
		/*Read the status bytes to update RX_EMPTY*/
		DeviceRead(regStatus1, 1, &Registers.Status.byte[5]);
	}
#endif
	/*
	*Only call the USB PD routines
	*if we have enabled the block
	*/
	if (USBPDActive) {
		/*
		*Call the protocol state machine
		*to handle any timing critical operations
		*/
		USBPDProtocol();
		/*
		*Once we have handled any Type-C
		*and protocol events,
		*call the USB PD Policy Engine
		*/
		USBPDPolicyEngine();
	}
	switch (ConnState) {
	case Disabled:
		StateMachineDisabled();
		break;
	case ErrorRecovery:
		StateMachineErrorRecovery();
		break;
	case Unattached:
		StateMachineUnattached();
		break;
#ifdef FSC_HAVE_SNK
	case AttachWaitSink:
		StateMachineAttachWaitSink();
		break;
	case AttachedSink:
		StateMachineAttachedSink();
		break;
#ifdef FSC_HAVE_DRP
	case TryWaitSink:
		StateMachineTryWaitSink();
		break;
	case TrySink:
		stateMachineTrySink();
		break;
#endif
#endif
#ifdef FSC_HAVE_SRC
	case AttachWaitSource:
		StateMachineAttachWaitSource();
		break;
	case AttachedSource:
		StateMachineAttachedSource();
		break;
#ifdef FSC_HAVE_DRP
	case TryWaitSource:
		stateMachineTryWaitSource();
		break;
	case TrySource:
		StateMachineTrySource();
		break;
#endif
	case UnattachedSource:
		stateMachineUnattachedSource();
		break;
#endif
#ifdef FSC_HAVE_ACCMODE
	case AudioAccessory:
		StateMachineAudioAccessory();
		break;
	case DebugAccessory:
		StateMachineDebugAccessory();
		break;
	case AttachWaitAccessory:
		StateMachineAttachWaitAccessory();
		break;
	case PoweredAccessory:
		StateMachinePoweredAccessory();
		break;
	case UnsupportedAccessory:
		StateMachineUnsupportedAccessory();
		break;
#endif
	case DelayUnattached:
		StateMachineDelayUnattached();
		break;
	default:
		/*
		*We shouldn't get here
		*so go to the unattached state just in case
		*/
		SetStateDelayUnattached();
		break;
	}
	/*
	*Clear the interrupt register
	*once we've gone through the state machines
	*/
	Registers.Status.Interrupt1 = 0;
	/*
	*Clear the advanced interrupt registers
	*once we've gone through the state machines
	*/
	Registers.Status.InterruptAdv = 0;
}

void StateMachineTypeC(void)
{
	FUSB_LOG("TypeC Start ConnState is %d USBPDActive is %d\n",
			 ConnState, USBPDActive);
#ifdef FSC_INTERRUPT_TRIGGERED
	do {
#endif
		StateMachineTypeCImp();
#ifdef FSC_INTERRUPT_TRIGGERED
		platform_delay_10us(SLEEP_DELAY);
	} while (g_Idle == FALSE);
#endif
	FUSB_LOG("TypeC End ConnState is %d USBPDActive is %d\n",
			 ConnState, USBPDActive);
}

void StateMachineDisabled(void)
{
	// Do nothing until directed to go to some other state...
}

void StateMachineErrorRecovery(void)
{
	if (StateTimer == 0) {
		SetStateDelayUnattached();
	}
}

void StateMachineDelayUnattached(void)
{
	if (StateTimer == 0) {
		SetStateUnattached();
	}
}

void StateMachineUnattached(void)	//TODO: Update to account for Ra detection (TOG_RD_ONLY == 0)
{
	if (alternateModes) {
		StateMachineAlternateUnattached();
		return;
	}

	if (Registers.Control.HOST_CUR != 0b01)	// Host current must be set to default for Toggle Functionality
	{
		Registers.Control.HOST_CUR = 0b01;
		DeviceWrite(regControl0, 1, &Registers.Control.byte[0]);
	}

	if (Registers.Status.I_TOGDONE) {
		switch (Registers.Status.TOGSS) {
#ifdef FSC_HAVE_SNK
		case 0b101:	// Rp detected on CC1
			blnCCPinIsCC1 = TRUE;
			blnCCPinIsCC2 = FALSE;
			SetStateAttachWaitSink();	// Go to the AttachWaitSnk state
			break;
		case 0b110:	// Rp detected on CC2
			blnCCPinIsCC1 = FALSE;
			blnCCPinIsCC2 = TRUE;
			SetStateAttachWaitSink();	// Go to the AttachWaitSnk state
			break;
#endif // FSC_HAVE_SNK
#ifdef FSC_HAVE_SRC
		case 0b001:	// Rd detected on CC1
			blnCCPinIsCC1 = TRUE;
			blnCCPinIsCC2 = FALSE;
#ifdef FSC_HAVE_ACCMODE
			if ((PortType == USBTypeC_Sink) && (blnAccSupport))	// If we are configured as a sink and support accessories...
				checkForAccessory();	// Go to the AttachWaitAcc state
			else	// Otherwise we must be configured as a source or DRP
#endif // FSC_HAVE_ACCMODE
				SetStateAttachWaitSource();	// So go to the AttachWaitSrc state
			break;
		case 0b010:	// Rd detected on CC2
			blnCCPinIsCC1 = FALSE;
			blnCCPinIsCC2 = TRUE;
#ifdef FSC_HAVE_ACCMODE
			if ((PortType == USBTypeC_Sink) && (blnAccSupport))	// If we are configured as a sink and support accessories...
				checkForAccessory();	// Go to the AttachWaitAcc state
			else	// Otherwise we must be configured as a source or DRP
#endif // FSC_HAVE_ACCMODE
				SetStateAttachWaitSource();	// So go to the AttachWaitSrc state
			break;
		case 0b111:	// Ra detected on both CC1 and CC2
			blnCCPinIsCC1 = FALSE;
			blnCCPinIsCC2 = FALSE;
#ifdef FSC_HAVE_ACCMODE
			if ((PortType == USBTypeC_Sink) && (blnAccSupport))	// If we are configured as a sink and support accessories...
				SetStateAttachWaitAccessory();	// Go to the AttachWaitAcc state
			else	// Otherwise we must be configured as a source or DRP
#endif // FSC_HAVE_ACCMODE
				SetStateAttachWaitSource();	// So go to the AttachWaitSnk state
			break;
#endif // FSC_HAVE_SRC
		default:	// Shouldn't get here, but just in case reset everything...
			Registers.Control.TOGGLE = 0;	// Disable the toggle in order to clear...
			DeviceWrite(regControl2, 1, &Registers.Control.byte[2]);	// Commit the control state
			platform_delay_10us(1);
			Registers.Control.TOGGLE = 1;	// Re-enable the toggle state machine... (allows us to get another I_TOGDONE interrupt)
			DeviceWrite(regControl2, 1, &Registers.Control.byte[2]);	// Commit the control state
			break;
		}
	}
// TODO - Comment on why we need this
#ifdef FPGA_BOARD
	rand();
#endif /* FPGA_BOARD */
}

#ifdef FSC_HAVE_SNK
void StateMachineAttachWaitSink(void)
{
	debounceCC();

	if ((CC1TermPDDebounce == CCTypeOpen) && (CC2TermPDDebounce == CCTypeOpen))	// If we have detected SNK.Open for atleast tPDDebounce on both pins...
	{
#ifdef FSC_HAVE_DRP
		if (PortType == USBTypeC_DRP) {
			SetStateUnattachedSource();	// Go to the unattached state
		} else
#endif // FSC_HAVE_DRP
		{
			SetStateDelayUnattached();
		}
	} else if (Registers.Status.VBUSOK)	// If we have detected VBUS and we have detected an Rp for >tCCDebounce...
	{
		if ((CC1TermCCDebounce > CCTypeOpen) && (CC2TermCCDebounce == CCTypeOpen))	// If exactly one CC is open
		{
			blnCCPinIsCC1 = TRUE;	// CC1 Pin is CC
			blnCCPinIsCC2 = FALSE;
#ifdef FSC_HAVE_DRP
			if ((PortType == USBTypeC_DRP) && blnSrcPreferred)	// If we are configured as a DRP and prefer the source role...
				SetStateTrySource();	// Go to the Try.Src state
			else	// Otherwise we are free to attach as a sink
#endif // FSC_HAVE_DRP
			{
				SetStateAttachedSink();	// Go to the Attached.Snk state               
			}
		} else if ((CC1TermCCDebounce == CCTypeOpen) && (CC2TermCCDebounce > CCTypeOpen))	// If exactly one CC is open
		{
			blnCCPinIsCC1 = FALSE;	// CC2 Pin is CC
			blnCCPinIsCC2 = TRUE;
#ifdef FSC_HAVE_DRP
			if ((PortType == USBTypeC_DRP) && blnSrcPreferred)	// If we are configured as a DRP and prefer the source role...
				SetStateTrySource();	// Go to the Try.Src state
			else	// Otherwise we are free to attach as a sink
#endif // FSC_HAVE_DRP
			{
				SetStateAttachedSink();	// Go to the Attached.Snk State
			}
		}
	}
}
#endif // FSC_HAVE_SNK

#ifdef FSC_HAVE_SRC
void StateMachineAttachWaitSource(void)
{
	debounceCC();
	bc_lvl = Registers.Status.BC_LVL;
	if (blnCCPinIsCC1)	// Check other line before attaching
	{
		if (CC1TermCCDebounce != CCTypeUndefined) {
			peekCC2Source();
		}
	} else if (blnCCPinIsCC2) {
		if (CC2TermCCDebounce != CCTypeUndefined) {
			peekCC1Source();
		}
	}
#ifdef FSC_HAVE_ACCMODE
	if (blnAccSupport)	// If accessory support is enabled
	{
		if ((CC1TermCCDebounce == CCTypeRa) && (CC2TermCCDebounce == CCTypeRa))	// If both pins are Ra, it's an audio accessory
			SetStateAudioAccessory();
		else if ((CC1TermCCDebounce >= CCTypeRdUSB) && (CC1TermCCDebounce < CCTypeUndefined) && (CC2TermCCDebounce >= CCTypeRdUSB) && (CC2TermCCDebounce < CCTypeUndefined))	// If both pins are Rd, it's a debug accessory
			SetStateDebugAccessory();
	}
#endif // FSC_HAVE_ACCMODE

	if (((CC1TermCCDebounce >= CCTypeRdUSB) && (CC1TermCCDebounce < CCTypeUndefined)) && ((CC2TermCCDebounce == CCTypeOpen) || (CC2TermCCDebounce == CCTypeRa)))	// If CC1 is Rd and CC2 is not...
	{
		if (VbusVSafe0V()) {	// Check for VBUS to be at VSafe0V first...
#ifdef FSC_HAVE_DRP
			if (blnSnkPreferred) {
				SetStateTrySink();
			} else
#endif // FSC_HAVE_DRP
			{
				SetStateAttachedSource();	// Go to the Attached.Src state
			}	// Go to the Attached.Src state
		}
	} else if (((CC2TermCCDebounce >= CCTypeRdUSB) && (CC2TermCCDebounce < CCTypeUndefined)) && ((CC1TermCCDebounce == CCTypeOpen) || (CC1TermCCDebounce == CCTypeRa)))	// If CC2 is Rd and CC1 is not...
	{
		if (VbusVSafe0V()) {	// Check for VBUS to be at VSafe0V first...
#ifdef FSC_HAVE_DRP
			if (blnSnkPreferred) {
				SetStateTrySink();
			} else
#endif // FSC_HAVE_DRP
			{
				SetStateAttachedSource();	// Go to the Attached.Src state
			}
		}
	} else if ((CC1TermPrevious == CCTypeOpen) && (CC2TermPrevious == CCTypeOpen))	// If our debounced signals are both open, go to the unattached state
		SetStateDelayUnattached();
	else if ((CC1TermPrevious == CCTypeOpen) && (CC2TermPrevious == CCTypeRa))	// If exactly one pin is open and the other is Ra, go to the unattached state
		SetStateDelayUnattached();
	else if ((CC1TermPrevious == CCTypeRa) && (CC2TermPrevious == CCTypeOpen))	// If exactly one pin is open and the other is Ra, go to the unattached state
		SetStateDelayUnattached();

}
#endif // FSC_HAVE_SRC

#ifdef FSC_HAVE_ACCMODE
void StateMachineAttachWaitAccessory(void)
{
	debounceCC();

	if ((CC1TermCCDebounce == CCTypeRa) && (CC2TermCCDebounce == CCTypeRa))	// If they are both Ra, it's an audio accessory
	{
		SetStateAudioAccessory();
	} else if ((CC1TermCCDebounce >= CCTypeRdUSB) && (CC1TermCCDebounce < CCTypeUndefined) && (CC2TermCCDebounce >= CCTypeRdUSB) && (CC2TermCCDebounce < CCTypeUndefined))	// If they are both Rd, it's a debug accessory
	{
		SetStateDebugAccessory();
	} else if ((CC1TermPrevious == CCTypeOpen) && (CC2TermPrevious == CCTypeOpen))	// If either pin is open, it's considered a detach
	{
		SetStateDelayUnattached();
	} else if ((CC1TermCCDebounce >= CCTypeRdUSB) && (CC1TermCCDebounce < CCTypeUndefined) && (CC2TermCCDebounce == CCTypeRa))	// If CC1 is Rd and CC2 is Ra, it's a powered accessory (CC1 is CC)
	{
		SetStatePoweredAccessory();
	} else if ((CC1TermCCDebounce == CCTypeRa) && (CC2TermCCDebounce >= CCTypeRdUSB) && (CC2TermCCDebounce < CCTypeUndefined))	// If CC1 is Ra and CC2 is Rd, it's a powered accessory (CC2 is CC)
	{
		SetStatePoweredAccessory();
	}
}
#endif // FSC_HAVE_ACCMODE

#ifdef FSC_HAVE_SNK
void StateMachineAttachedSink(void)
{
	//debounceCC();

#ifdef COMPLIANCE
	if ((!IsPRSwap) && (IsHardReset == FALSE) && VbusUnder5V())	// If VBUS is removed and we are not in the middle of a power role swap...
#else
	if ((!IsPRSwap) && (IsHardReset == FALSE) && !Registers.Status.VBUSOK)	// If VBUS is removed and we are not in the middle of a power role swap...   
#endif // COMPLIANCE
	{
		SetStateDelayUnattached();	// Go to the unattached state
	}

	if (blnCCPinIsCC1) {
		UpdateSinkCurrent(CC1TermCCDebounce);	// Update the advertised current
	} else if (blnCCPinIsCC2) {
		UpdateSinkCurrent(CC2TermCCDebounce);	// Update the advertised current
	}

}
#endif // FSC_HAVE_SNK

#ifdef FSC_HAVE_SRC
void StateMachineAttachedSource(void)
{
	struct power_supply *usb_psy = power_supply_get_by_name("usb");

	USBPDDisable(TRUE);
	switch (TypeCSubState) {
	default:
	case 0:
		debounceCC();

		if (Registers.Switches.MEAS_CC1) {
			if ((CC1TermPrevious == CCTypeOpen) && (!IsPRSwap))	// If the debounced CC pin is detected as open and we aren't in the middle of a PR_Swap
			{
				/* Notify USB driver to exit host mode */
				if (usb_psy)
					power_supply_set_usb_otg(usb_psy, 0);
#ifdef FSC_HAVE_DRP
				if ((PortType == USBTypeC_DRP) && blnSrcPreferred)	// Check to see if we need to go to the TryWait.SNK state...
					SetStateTryWaitSink();
				else	// Otherwise we are going to the unattached state
#endif // FSC_HAVE_DRP
				{
					platform_set_vbus_lvl_enable(VBUS_LVL_ALL, FALSE, FALSE);	// Disable the vbus outputs
					Registers.Switches.byte[0] = 0x00;	// Disabled until vSafe0V
					DeviceWrite(regSwitches0, 1,
						    &Registers.Switches.
						    byte[0]);
					TypeCSubState++;
					g_Idle = FALSE;	// Don't idle so we can reenter into the next substate
				}
			} else {
/*Idle until COMP because CC timer has reset to Ra*/
				g_Idle = TRUE;
				Registers.Mask.byte = 0xFF;
				Registers.Mask.M_COMP_CHNG = 0;
				DeviceWrite(regMask, 1, &Registers.Mask.byte);
				Registers.MaskAdv.byte[0] = 0xFF;
				DeviceWrite(regMaska, 1,
					&Registers.MaskAdv.byte[0]);
				Registers.MaskAdv.M_GCRCSENT = 1;
				DeviceWrite(regMaskb, 1,
					&Registers.MaskAdv.byte[1]);
				platform_enable_timer(FALSE);
			}
		} else if (Registers.Switches.MEAS_CC2) {
			if ((CC2TermPrevious == CCTypeOpen) && (!IsPRSwap))	// If the debounced CC pin is detected as open and we aren't in the middle of a PR_Swap
			{
				/* Notify USB driver to exit host mode */
				if (usb_psy)
					power_supply_set_usb_otg(usb_psy, 0);
#ifdef FSC_HAVE_DRP
				if ((PortType == USBTypeC_DRP) && blnSrcPreferred)	// Check to see if we need to go to the TryWait.SNK state...
					SetStateTryWaitSink();
				else	// Otherwise we are going to the unattached state
#endif // FSC_HAVE_DRP
				{
					platform_set_vbus_lvl_enable(VBUS_LVL_ALL, FALSE, FALSE);	// Disable the vbus outputs
					Registers.Switches.byte[0] = 0x00;	// Disabled until vSafe0V
					DeviceWrite(regSwitches0, 1,
						    &Registers.Switches.
						    byte[0]);
					TypeCSubState++;
					g_Idle = FALSE;	// Don't idle so we can reenter into the next substate
				}
			} else {
/*Idle until COMP because CC timer has reset to Ra*/
				g_Idle = TRUE;
				Registers.Mask.byte = 0xFF;
				Registers.Mask.M_COMP_CHNG = 0;
				DeviceWrite(regMask, 1, &Registers.Mask.byte);
				Registers.MaskAdv.byte[0] = 0xFF;
				DeviceWrite(regMaska, 1,
					&Registers.MaskAdv.byte[0]);
				Registers.MaskAdv.M_GCRCSENT = 1;
				DeviceWrite(regMaskb, 1,
					&Registers.MaskAdv.byte[1]);
				platform_enable_timer(FALSE);
			}
		}
		break;
	case 1:
		if (VbusVSafe0V() ||
			((CC2TermPrevious == CCTypeOpen) &&
			(CC1TermPrevious == CCTypeOpen))
		   ) {
			/* Notify USB driver to exit host mode */
			if (usb_psy)
				power_supply_set_usb_otg(usb_psy, 0);
			SetStateDelayUnattached();
		}
		break;
	}
}
#endif // FSC_HAVE_SRC

#ifdef FSC_HAVE_DRP
void StateMachineTryWaitSink(void)
{
	debounceCC();

	if ((StateTimer == 0) && (CC1TermPrevious == CCTypeOpen) && (CC2TermPrevious == CCTypeOpen))	// If tDRPTryWait has expired and we detected open on both pins...
		SetStateDelayUnattached();	// Go to the unattached state
	else if (Registers.Status.VBUSOK)	// If we have detected VBUS and we have detected an Rp for >tCCDebounce...
	{
		if ((CC1TermCCDebounce > CCTypeOpen) && (CC2TermCCDebounce == CCTypeOpen))	// If Rp is detected on CC1
		{		//
			SetStateAttachedSink();	// Go to the Attached.Snk state
		} else if ((CC1TermCCDebounce == CCTypeOpen) && (CC2TermCCDebounce > CCTypeOpen))	// If Rp is detected on CC2
		{
			SetStateAttachedSink();	// Go to the Attached.Snk State
		}
	}
}
#endif // FSC_HAVE_DRP

#ifdef FSC_HAVE_DRP
void StateMachineTrySource(void)
{
	debounceCC();
	bc_lvl = Registers.Status.BC_LVL;
	if ((CC1TermPDDebounce > CCTypeRa) && (CC1TermPDDebounce < CCTypeUndefined) && ((CC2TermPDDebounce == CCTypeOpen) || (CC2TermPDDebounce == CCTypeRa)))	// If the CC1 pin is Rd for atleast tPDDebounce...
	{
		SetStateAttachedSource();	// Go to the Attached.Src state
	} else if ((CC2TermPDDebounce > CCTypeRa) && (CC2TermPDDebounce < CCTypeUndefined) && ((CC1TermPDDebounce == CCTypeOpen) || (CC1TermPDDebounce == CCTypeRa)))	// If the CC2 pin is Rd for atleast tPDDebounce...
	{
		SetStateAttachedSource();	// Go to the Attached.Src state
	} else if (StateTimer == 0)	// If we haven't detected Rd on exactly one of the pins and we have waited for tDRPTry...
		SetStateTryWaitSink();	// Move onto the TryWait.Snk state to not get stuck in here
}
#endif // FSC_HAVE_DRP

#ifdef FSC_HAVE_ACCMODE
void StateMachineDebugAccessory(void)
{
	debounceCC();

	if ((CC1TermCCDebounce == CCTypeOpen) || (CC2TermCCDebounce == CCTypeOpen))	// If we have detected an open for > tCCDebounce 
	{
#ifdef FSC_HAVE_SRC
		if (PortType == USBTypeC_Source) {
			SetStateUnattachedSource();
		} else
#endif // FSC_HAVE_SRC
		{
			SetStateDelayUnattached();
		}
	}

}

void StateMachineAudioAccessory(void)
{
	debounceCC();
	if ((CC1TermCCDebounce == CCTypeOpen) || (CC2TermCCDebounce == CCTypeOpen))	// If we have detected an open for > tCCDebounce 
	{
#ifdef FSC_HAVE_SRC
		if (PortType == USBTypeC_Source) {
			SetStateUnattachedSource();
		} else
#endif // FSC_HAVE_SRC
		{
			SetStateDelayUnattached();
		}
	}
#ifdef FSC_INTERRUPT_TRIGGERED
	if (((CC1TermPrevious == CCTypeOpen) || (CC2TermPrevious == CCTypeOpen))
	    && (g_Idle == TRUE)) {
		g_Idle = FALSE;	// Mask all - debounce CC as open
		Registers.Mask.byte = 0xFF;
		DeviceWrite(regMask, 1, &Registers.Mask.byte);
		Registers.MaskAdv.byte[0] = 0xFF;
		DeviceWrite(regMaska, 1, &Registers.MaskAdv.byte[0]);
		Registers.MaskAdv.M_GCRCSENT = 1;
		DeviceWrite(regMaskb, 1, &Registers.MaskAdv.byte[1]);
		platform_enable_timer(TRUE);
	} else if ((CC1TermPrevious == CCTypeOpen)
			   || (CC2TermPrevious == CCTypeOpen)) {
		SetStateDelayUnattached();
	} else
	if ((CC1TermPDDebounce == CCTypeRa) && (CC2TermPDDebounce == CCTypeRa))	// && because one Pin will always stay detected as Ra
	{
		g_Idle = TRUE;	// Idle until COMP because CC timer has reset to Ra
		Registers.Mask.byte = 0xFF;
		Registers.Mask.M_COMP_CHNG = 0;
		DeviceWrite(regMask, 1, &Registers.Mask.byte);
		Registers.MaskAdv.byte[0] = 0xFF;
		DeviceWrite(regMaska, 1, &Registers.MaskAdv.byte[0]);
		Registers.MaskAdv.M_GCRCSENT = 1;
		DeviceWrite(regMaskb, 1, &Registers.MaskAdv.byte[1]);
		platform_enable_timer(FALSE);
	}
#endif // FSC_INTERRUPT_TRIGGERED
}

void StateMachinePoweredAccessory(void)	//TODO: Update VCONN-Powered Accessory logic
{
	debounceCC();

	if (blnCCPinIsCC1 && (CC1TermPrevious == CCTypeOpen))	// Transition to Unattached.Snk when monitored CC pin is Open
	{
		SetStateDelayUnattached();
	} else if (blnCCPinIsCC2 && (CC2TermPrevious == CCTypeOpen))	// Transition to Unattached.Snk when monitored CC pin is Open
	{
		SetStateDelayUnattached();
	}
	/* else if not support PD or no response to powered accessory pd commands */
//    else if(!PolicyHasContract)
//    {
//        SetStateDelayUnattached();
//    }  
	else if (StateTimer == 0)	// If we have timed out (tAMETimeout) and haven't entered an alternate mode...
		SetStateDelayUnattached();	//~Rick: Testing this for compliance
	//SetStateUnsupportedAccessory();                                         // Go to the Unsupported.Accessory state
}

void StateMachineUnsupportedAccessory(void)
{
	debounceCC();

	if ((blnCCPinIsCC1) && (CC1TermPrevious == CCTypeOpen))	// Transition to Unattached.Snk when monitored CC pin is Open
	{
		SetStateDelayUnattached();
	} else if ((blnCCPinIsCC2) && (CC2TermPrevious == CCTypeOpen))	// Transition to Unattached.Snk when monitored CC pin is Open
	{
		SetStateDelayUnattached();
	}
}
#endif // FSC_HAVE_ACCMODE

#ifdef FSC_HAVE_DRP
void stateMachineTrySink(void)
{
	if (StateTimer == 0) {
		debounceCC();
	}

	if (Registers.Status.VBUSOK) {
		if ((CC1TermPDDebounce >= CCTypeRdUSB) && (CC1TermPDDebounce < CCTypeUndefined) && (CC2TermPDDebounce == CCTypeOpen))	// If the CC1 pin is Rd for atleast tPDDebounce...
		{
			SetStateAttachedSink();	// Go to the Attached.Src state
		} else if ((CC2TermPDDebounce >= CCTypeRdUSB) && (CC2TermPDDebounce < CCTypeUndefined) && (CC1TermPDDebounce == CCTypeOpen))	// If the CC2 pin is Rd for atleast tPDDebounce...
		{
			SetStateAttachedSink();	// Go to the Attached.Src state
		}
	}

	if ((CC1TermPDDebounce == CCTypeOpen)
	    && (CC2TermPDDebounce == CCTypeOpen)) {
		SetStateTryWaitSource();
	}
}
#endif // FSC_HAVE_DRP

#ifdef FSC_HAVE_DRP
void stateMachineTryWaitSource(void)
{
	debounceCC();
	bc_lvl = Registers.Status.BC_LVL;
	if (VbusVSafe0V()) {
		if (((CC1TermPDDebounce >= CCTypeRdUSB) && (CC1TermPDDebounce < CCTypeUndefined)) && ((CC2TermPDDebounce == CCTypeRa) || CC2TermPDDebounce == CCTypeOpen))	// If the CC1 pin is Rd for atleast tPDDebounce...
		{
			SetStateAttachedSource();	// Go to the Attached.Src state
		} else if (((CC2TermPDDebounce >= CCTypeRdUSB) && (CC2TermPDDebounce < CCTypeUndefined)) && ((CC1TermPDDebounce == CCTypeRa) || CC1TermPDDebounce == CCTypeOpen))	// If the CC2 pin is Rd for atleast tPDDebounce...
		{
			SetStateAttachedSource();	// Go to the Attached.Src state
		}
	}

	if (StateTimer == 0) {
		if ((CC1TermPrevious == CCTypeOpen)
		    && (CC1TermPrevious == CCTypeOpen)) {
			SetStateDelayUnattached();
		}
	}
}
#endif // FSC_HAVE_DRP

#ifdef FSC_HAVE_SRC
void stateMachineUnattachedSource(void)
{
	if (alternateModes) {
		StateMachineAlternateUnattachedSource();
		return;
	}

	debounceCC();

	if ((CC1TermPrevious == CCTypeRa) && (CC2TermPrevious == CCTypeRa)) {
		SetStateAttachWaitSource();
	}

	if ((CC1TermPrevious >= CCTypeRdUSB) && (CC1TermPrevious < CCTypeUndefined) && ((CC2TermPrevious == CCTypeRa) || CC2TermPrevious == CCTypeOpen))	// If the CC1 pin is Rd for atleast tPDDebounce...
	{
		blnCCPinIsCC1 = TRUE;	// The CC pin is CC1
		blnCCPinIsCC2 = FALSE;
		SetStateAttachWaitSource();	// Go to the Attached.Src state
	} else if ((CC2TermPrevious >= CCTypeRdUSB) && (CC2TermPrevious < CCTypeUndefined) && ((CC1TermPrevious == CCTypeRa) || CC1TermPrevious == CCTypeOpen))	// If the CC2 pin is Rd for atleast tPDDebounce...
	{
		blnCCPinIsCC1 = FALSE;	// The CC pin is CC2
		blnCCPinIsCC2 = TRUE;
		SetStateAttachWaitSource();	// Go to the Attached.Src state
	}

	if (DRPToggleTimer == 0) {
		SetStateDelayUnattached();
	}

}
#endif // FSC_HAVE_SRC

/////////////////////////////////////////////////////////////////////////////
//                      State Machine Configuration
/////////////////////////////////////////////////////////////////////////////
#ifdef FSC_DEBUG
void SetStateDisabled(void)
{
#ifdef FSC_INTERRUPT_TRIGGERED
	g_Idle = FALSE;		// Unmask all
	Registers.Mask.byte = 0x00;
	DeviceWrite(regMask, 1, &Registers.Mask.byte);
	Registers.MaskAdv.byte[0] = 0x00;
	DeviceWrite(regMaska, 1, &Registers.MaskAdv.byte[0]);
	Registers.MaskAdv.M_GCRCSENT = 0;
	DeviceWrite(regMaskb, 1, &Registers.MaskAdv.byte[1]);
	platform_enable_timer(TRUE);
#endif // FSC_INTERRUPT_TRIGGERED
	platform_set_vbus_lvl_enable(VBUS_LVL_ALL, FALSE, FALSE);	// Disable the vbus outputs
	Registers.Power.PWR = 0x1;	// Enter low power state
	Registers.Control.TOGGLE = 0;	// Disable the toggle state machine
	Registers.Control.HOST_CUR = 0b00;	// Disable the currents for the pull-ups (not used for UFP)
	Registers.Switches.byte[0] = 0x00;	// Disable all pull-ups and pull-downs on the CC pins and disable the BMC transmitters
	DeviceWrite(regPower, 1, &Registers.Power.byte);	// Commit the power state
	DeviceWrite(regControl0, 3, &Registers.Control.byte[0]);	// Commit the control state
	DeviceWrite(regSwitches0, 1, &Registers.Switches.byte[0]);	// Commit the switch state
	USBPDDisable(TRUE);	// Disable the USB PD state machine 
	resetDebounceVariables();
	blnCCPinIsCC1 = FALSE;	// Clear the CC1 pin flag
	blnCCPinIsCC2 = FALSE;	// Clear the CC2 pin flag
	ConnState = Disabled;	// Set the state machine variable to Disabled
	StateTimer = T_TIMER_DISABLE;	// Disable the state timer (not used in this state)
	PDDebounce = T_TIMER_DISABLE;	// Disable the 1st level debounce timer
	CCDebounce = T_TIMER_DISABLE;	// Disable the 2nd level debounce timer
	ToggleTimer = T_TIMER_DISABLE;	// Disable the toggle timer
	OverPDDebounce = T_TIMER_DISABLE;	// Disable PD filter timer
	WriteStateLog(&TypeCStateLog, ConnState, Timer_tms, Timer_S);
}
#endif // FSC_DEBUG

void SetStateErrorRecovery(void)
{
#ifdef FSC_INTERRUPT_TRIGGERED
	g_Idle = FALSE;		// Unmask all
	Registers.Mask.byte = 0x00;
	DeviceWrite(regMask, 1, &Registers.Mask.byte);
	Registers.MaskAdv.byte[0] = 0x00;
	DeviceWrite(regMaska, 1, &Registers.MaskAdv.byte[0]);
	Registers.MaskAdv.M_GCRCSENT = 0;
	DeviceWrite(regMaskb, 1, &Registers.MaskAdv.byte[1]);
	platform_enable_timer(TRUE);
#endif // FSC_INTERRUPT_TRIGGERED
	platform_set_vbus_lvl_enable(VBUS_LVL_ALL, FALSE, FALSE);	// Disable the vbus outputs
	Registers.Power.PWR = 0x1;	// Enter low power state
	Registers.Control.TOGGLE = 0;	// Disable the toggle state machine
	Registers.Control.HOST_CUR = 0b00;	// Disable the currents for the pull-ups (not used for UFP)
	Registers.Switches.byte[0] = 0x00;	// Disable all pull-ups and pull-downs on the CC pins and disable the BMC transmitters
	DeviceWrite(regPower, 1, &Registers.Power.byte);	// Commit the power state
	DeviceWrite(regControl0, 3, &Registers.Control.byte[0]);	// Commit the control state
	DeviceWrite(regSwitches0, 1, &Registers.Switches.byte[0]);	// Commit the switch state
	USBPDDisable(TRUE);	// Disable the USB PD state machine 
	resetDebounceVariables();
	blnCCPinIsCC1 = FALSE;	// Clear the CC1 pin flag
	blnCCPinIsCC2 = FALSE;	// Clear the CC2 pin flag
	ConnState = ErrorRecovery;	// Set the state machine variable to ErrorRecovery
	StateTimer = tErrorRecovery;	// Load the tErrorRecovery duration into the state transition timer
	PDDebounce = T_TIMER_DISABLE;	// Disable the 1st level debounce timer
	CCDebounce = T_TIMER_DISABLE;	// Disable the 2nd level debounce timer
	ToggleTimer = T_TIMER_DISABLE;	// Disable the toggle timer
	OverPDDebounce = T_TIMER_DISABLE;	// Disable PD filter timer
	IsHardReset = FALSE;
#ifdef FSC_DEBUG
	WriteStateLog(&TypeCStateLog, ConnState, Timer_tms, Timer_S);
#endif // FSC_DEBUG
}

void SetStateDelayUnattached(void)
{

	platform_disableSuperspeedUSB();
	platform_toggleAudioSwitch(fsa_lpm);
#ifdef CONFIG_FSUSB42_MUX
	if (fsusb42_get_state() != FSUSB_STATE_EXT)
		fsusb42_set_state(FSUSB_OFF);
#endif
#ifndef FPGA_BOARD
	SetStateUnattached();
	return;
#else
#ifdef FSC_INTERRUPT_TRIGGERED
	g_Idle = FALSE;		// Mask all
	Registers.Mask.byte = 0xFF;
	DeviceWrite(regMask, 1, &Registers.Mask.byte);
	Registers.MaskAdv.byte[0] = 0xFF;
	DeviceWrite(regMaska, 1, &Registers.MaskAdv.byte[0]);
	Registers.MaskAdv.M_GCRCSENT = 1;
	DeviceWrite(regMaskb, 1, &Registers.MaskAdv.byte[1]);
	platform_enable_timer(TRUE);
#endif /* INTERRUPT */
	// This state is only here because of the precision timing source we have with the FPGA
	// We are trying to avoid having the toggle state machines in sync with each other
	// Causing the tDRPAdvert period to overlap causing the devices to not attach for a period of time
	platform_set_vbus_lvl_enable(VBUS_LVL_ALL, FALSE, FALSE);	// Disable the vbus outputs
	Registers.Power.PWR = 0x1;	// Enter low power state
	Registers.Control.TOGGLE = 0;	// Disable the toggle state machine
	Registers.Switches.word &= 0x6800;	// Disable all pull-ups and pull-downs on the CC pins and disable the BMC transmitters
	DeviceWrite(regPower, 1, &Registers.Power.byte);	// Commit the power state
	DeviceWrite(regControl0, 3, &Registers.Control.byte[0]);	// Commit the control state
	DeviceWrite(regSwitches0, 1, &Registers.Switches.byte[0]);	// Commit the switch state
	USBPDDisable(TRUE);	// Disable the USB PD state machine 
	resetDebounceVariables();
	blnCCPinIsCC1 = FALSE;	// Clear the CC1 pin flag
	blnCCPinIsCC2 = FALSE;	// Clear the CC2 pin flag
	ConnState = DelayUnattached;	// Set the state machine variable to delayed unattached
	StateTimer = rand() % 64;	// Set the state timer to a random value to not synchronize the toggle start (use a multiple of RAND_MAX+1 as the modulus operator)
	PDDebounce = T_TIMER_DISABLE;	// Disable the 1st level debounce timer
	CCDebounce = T_TIMER_DISABLE;	// Disable the 2nd level debounce timer
	ToggleTimer = T_TIMER_DISABLE;	// Disable the toggle timer
	OverPDDebounce = T_TIMER_DISABLE;	// Disable PD filter timer
#ifdef FSC_DEBUG
	WriteStateLog(&TypeCStateLog, ConnState, Timer_tms, Timer_S);
#endif // FSC_DEBUG
#endif /* FPGA_BOARD */
}

void SetStateUnattached(void)
{
	if (alternateModes) {
		SetStateAlternateUnattached();
		return;
	}
#ifdef FSC_INTERRUPT_TRIGGERED
	g_Idle = TRUE;		// Idle until I_TOGDONE
	Registers.Mask.byte = 0xFF;
	DeviceWrite(regMask, 1, &Registers.Mask.byte);
	Registers.MaskAdv.byte[0] = 0xFF;
	Registers.MaskAdv.M_TOGDONE = 0;
	DeviceWrite(regMaska, 1, &Registers.MaskAdv.byte[0]);
	Registers.MaskAdv.M_GCRCSENT = 1;
	DeviceWrite(regMaskb, 1, &Registers.MaskAdv.byte[1]);
	platform_enable_timer(FALSE);
#endif // FSC_INTERRUPT_TRIGGERED
	// This function configures the Toggle state machine in the device to handle all of the unattached states.
	// This allows for the MCU to be placed in a low power mode until the device wakes it up upon detecting something
	Registers.MaskAdv.M_TOGDONE = 0;
	DeviceWrite(regMaska, 1, &Registers.MaskAdv.byte[0]);
	Registers.Control.TOGGLE = 0;
	DeviceWrite(regControl2, 1, &Registers.Control.byte[2]);	// Commit the toggle
	Registers.Switches.byte[0] = 0x03;	// Enable the pull-downs on the CC pins
	DeviceWrite(regSwitches0, 1, &Registers.Switches.byte[0]);	// Commit the switch state

	if (PortType == USBTypeC_DRP)	// If we are a DRP
		Registers.Control.MODE = 0b01;	// We need to enable the toggling functionality for Rp/Rd
#ifdef FSC_HAVE_ACCMODE
	else if ((PortType == USBTypeC_Sink) && (blnAccSupport))	// If we are a sink supporting accessories
		Registers.Control.MODE = 0b01;	// We need to enable the toggling functionality for Rp/Rd
#endif // FSC_HAVE_ACCMODE
	else if (PortType == USBTypeC_Source)	// If we are strictly a Source
		Registers.Control.MODE = 0b11;	// We just need to look for Rd
	else			// Otherwise we are a UFP
		Registers.Control.MODE = 0b10;	// So we need to only look for Rp

	Registers.Power.PWR = 0x7;	// Enable everything except internal oscillator
	DeviceWrite(regPower, 1, &Registers.Power.byte);	// Commit the power state
	Registers.Control.HOST_CUR = 0b01;	// Set current to default (required for toggle))
	Registers.Control.TOGGLE = 1;	// Enable the toggle
	platform_delay_10us(1);	// Delay before re-enabling toggle
	DeviceWrite(regControl0, 3, &Registers.Control.byte[0]);	// Commit the control state 
	USBPDDisable(TRUE);	// Disable the USB PD state machine 
	ConnState = Unattached;
	SinkCurrent = utccNone;
	resetDebounceVariables();
	blnCCPinIsCC1 = FALSE;	// Clear the CC1 pin flag 
	blnCCPinIsCC2 = FALSE;	// Clear the CC2 pin flag
	StateTimer = T_TIMER_DISABLE;	// Disable the state timer, not used in this state
	PDDebounce = T_TIMER_DISABLE;	// Disable the 1st level debounce timer, not used in this state
	CCDebounce = T_TIMER_DISABLE;	// Disable the 2nd level debounce timer, not used in this state
	ToggleTimer = T_TIMER_DISABLE;	// Disable the toggle timer, not used in this state
	OverPDDebounce = T_TIMER_DISABLE;	// Disable PD filter timer
	usbc_psy.type = POWER_SUPPLY_TYPE_USBC;
	power_supply_changed(&usbc_psy);
	platform_toggleAudioSwitch(fsa_lpm);
	gChargerAuthenticated = FALSE;
#ifdef FSC_DEBUG
	WriteStateLog(&TypeCStateLog, ConnState, Timer_tms, Timer_S);
#endif // FSC_DEBUG
}

#ifdef FSC_HAVE_SNK
void SetStateAttachWaitSink(void)
{
#ifdef FSC_INTERRUPT_TRIGGERED
	g_Idle = FALSE;		// Mask all
	Registers.Mask.byte = 0xFF;
	DeviceWrite(regMask, 1, &Registers.Mask.byte);
	Registers.MaskAdv.byte[0] = 0xFF;
	DeviceWrite(regMaska, 1, &Registers.MaskAdv.byte[0]);
	Registers.MaskAdv.M_GCRCSENT = 1;
	DeviceWrite(regMaskb, 1, &Registers.MaskAdv.byte[1]);
	platform_enable_timer(TRUE);
#endif
	platform_set_vbus_lvl_enable(VBUS_LVL_ALL, FALSE, FALSE);	// Disable the vbus outputs
	ConnState = AttachWaitSink;	// Set the state machine variable to AttachWait.Snk
	sourceOrSink = Sink;
	Registers.Control.TOGGLE = 0;	// Disable the toggle
	DeviceWrite(regControl2, 1, &Registers.Control.byte[2]);	// Commit the toggle state
	updateSourceCurrent();
	Registers.Power.PWR = 0x7;	// Enable everything except internal oscillator
	DeviceWrite(regPower, 1, &Registers.Power.byte);	// Commit the power state
	Registers.Switches.byte[0] = 0x07;	// Enable the pull-downs on the CC pins
	DeviceWrite(regSwitches0, 1, &Registers.Switches.byte[0]);	// Commit the switch state
	resetDebounceVariables();
	SinkCurrent = utccNone;	// Set the current advertisment variable to none until we determine what the current is  
	StateTimer = T_TIMER_DISABLE;	// Disable the state timer, not used in this state
	PDDebounce = tPDDebounce;	// Set the tPDDebounce for validating signals to transition to
	CCDebounce = tCCDebounce;	// Disable the 2nd level debouncing until the first level has been debounced
	ToggleTimer = tDeviceToggle;	// Set the toggle timer to look at each pin for tDeviceToggle duration
	OverPDDebounce = T_TIMER_DISABLE;	// Disable PD filter timer
	usbc_psy.type = POWER_SUPPLY_TYPE_USBC;
	power_supply_changed(&usbc_psy);
#ifdef FSC_DEBUG
	WriteStateLog(&TypeCStateLog, ConnState, Timer_tms, Timer_S);
#endif // FSC_DEBUG
}
#endif // FSC_HAVE_SNK

#ifdef FSC_HAVE_SRC
void SetStateAttachWaitSource(void)
{
#ifdef FSC_INTERRUPT_TRIGGERED
	g_Idle = FALSE;		// Mask all
	Registers.Mask.byte = 0xFF;
	DeviceWrite(regMask, 1, &Registers.Mask.byte);
	Registers.MaskAdv.byte[0] = 0xFF;
	DeviceWrite(regMaska, 1, &Registers.MaskAdv.byte[0]);
	Registers.MaskAdv.M_GCRCSENT = 1;
	DeviceWrite(regMaskb, 1, &Registers.MaskAdv.byte[1]);
	platform_enable_timer(TRUE);
#endif
	platform_set_vbus_lvl_enable(VBUS_LVL_ALL, FALSE, FALSE);	// Disable the vbus outputs
	Registers.Control.TOGGLE = 0;	// Disable the toggle
	DeviceWrite(regControl2, 1, &Registers.Control.byte[2]);	// Commit the toggle

	updateSourceCurrent();
	ConnState = AttachWaitSource;	// Set the state machine variable to AttachWait.Src
	sourceOrSink = Source;
	Registers.Power.PWR = 0x7;	// Enable everything except internal oscillator
	DeviceWrite(regPower, 1, &Registers.Power.byte);	// Commit the power state
	if ((blnCCPinIsCC1 == FALSE) && (blnCCPinIsCC2 == FALSE))	//For automated testing
	{
		DetectCCPinSource();
	}
	if (blnCCPinIsCC1)	// If we detected CC1 as an Rd
	{
		peekCC2Source();
		Registers.Switches.byte[0] = 0x44;	// Enable CC1 pull-up and measure
		setDebounceVariablesCC1(CCTypeUndefined);
	} else {
		peekCC1Source();
		Registers.Switches.byte[0] = 0x88;	// Enable CC2 pull-up and measure
		setDebounceVariablesCC2(CCTypeUndefined);
	}

	DeviceWrite(regSwitches0, 1, &Registers.Switches.byte[0]);	// Commit the switch state    
	SinkCurrent = utccNone;	// Not used in Src
	OverPDDebounce = T_TIMER_DISABLE;	// Disable PD filter timer
	StateTimer = T_TIMER_DISABLE;	// Disable the state timer, not used in this state
	PDDebounce = tPDDebounce;	// Only debounce the lines for tPDDebounce so that we can debounce a detach condition
	CCDebounce = tCCDebounce;	// Disable the 2nd level debouncing initially to force completion of a 1st level debouncing
	ToggleTimer = T_TIMER_DISABLE;	// No toggle on sources
	usbc_psy.type = POWER_SUPPLY_TYPE_USBC;
	power_supply_changed(&usbc_psy);
#ifdef FSC_DEBUG
	WriteStateLog(&TypeCStateLog, ConnState, Timer_tms, Timer_S);
#endif // FSC_DEBUG
}
#endif // FSC_HAVE_SRC

#ifdef FSC_HAVE_ACCMODE
void SetStateAttachWaitAccessory(void)
{
#ifdef FSC_INTERRUPT_TRIGGERED
	g_Idle = FALSE;		// Mask all
	Registers.Mask.byte = 0xFF;
	DeviceWrite(regMask, 1, &Registers.Mask.byte);
	Registers.MaskAdv.byte[0] = 0xFF;
	DeviceWrite(regMaska, 1, &Registers.MaskAdv.byte[0]);
	Registers.MaskAdv.M_GCRCSENT = 1;
	DeviceWrite(regMaskb, 1, &Registers.MaskAdv.byte[1]);
	platform_enable_timer(TRUE);
#endif
	platform_set_vbus_lvl_enable(VBUS_LVL_ALL, FALSE, FALSE);	// Disable the vbus outputs
	ConnState = AttachWaitAccessory;	// Set the state machine variable to AttachWait.Accessory
	sourceOrSink = Source;
	updateSourceCurrent();
	Registers.Control.TOGGLE = 0;	// Disable the toggle
	DeviceWrite(regControl2, 1, &Registers.Control.byte[2]);	// Commit the toggle
	Registers.Power.PWR = 0x7;	// Enable everything except internal oscillator
	DeviceWrite(regPower, 1, &Registers.Power.byte);	// Commit the power state
	if ((blnCCPinIsCC1 == FALSE) && (blnCCPinIsCC2 == FALSE))	//For automated testing
	{
		DetectCCPinSource();
	}
	if (blnCCPinIsCC1)	// If we detected CC1 as an Rd
	{
		peekCC2Source();
		Registers.Switches.byte[0] = 0x44;	// Enable CC1 pull-up and measure
		setDebounceVariablesCC1(CCTypeUndefined);
	} else {
		peekCC1Source();
		Registers.Switches.byte[0] = 0x88;	// Enable CC2 pull-up and measure
		setDebounceVariablesCC2(CCTypeUndefined);
	}
	DeviceWrite(regSwitches0, 1, &Registers.Switches.byte[0]);	// Commit the switch state
	SinkCurrent = utccNone;	// Not used in accessories
	OverPDDebounce = T_TIMER_DISABLE;	// Disable PD filter timer
	StateTimer = T_TIMER_DISABLE;	// Disable the state timer, not used in this state
	PDDebounce = tCCDebounce;	// Once in this state, we are waiting for the lines to be stable for tCCDebounce before changing states
	CCDebounce = tCCDebounce;	// Disable the 2nd level debouncing initially to force completion of a 1st level debouncing
	ToggleTimer = T_TIMER_DISABLE;	// No toggle on sources
	usbc_psy.type = POWER_SUPPLY_TYPE_USBC;
	power_supply_changed(&usbc_psy);
#ifdef FSC_DEBUG
	WriteStateLog(&TypeCStateLog, ConnState, Timer_tms, Timer_S);
#endif // FSC_DEBUG
}
#endif // FSC_HAVE_ACCMODE

#ifdef FSC_HAVE_SRC
void SetStateAttachedSource(void)
{
	struct power_supply *usb_psy = power_supply_get_by_name("usb");
#ifdef FSC_INTERRUPT_TRIGGERED
	g_Idle = TRUE;		// Mask for COMP
	Registers.Mask.byte = 0xFF;
	Registers.Mask.M_COMP_CHNG = 0;
	DeviceWrite(regMask, 1, &Registers.Mask.byte);
	Registers.MaskAdv.byte[0] = 0xFF;
	DeviceWrite(regMaska, 1, &Registers.MaskAdv.byte[0]);
	Registers.MaskAdv.M_GCRCSENT = 1;
	DeviceWrite(regMaskb, 1, &Registers.MaskAdv.byte[1]);
	platform_enable_timer(FALSE);
#endif
	platform_set_vbus_lvl_enable(VBUS_LVL_5V, TRUE, TRUE);	// Enable only the 5V output
	ConnState = AttachedSource;	// Set the state machine variable to Attached.Src
	TypeCSubState = 0;
	sourceOrSink = Source;
	updateSourceCurrent();
	Registers.Power.PWR = 0x7;	// Enable everything except internal oscillator
	DeviceWrite(regPower, 1, &Registers.Power.byte);	// Commit the power state
	if ((blnCCPinIsCC1 == FALSE) && (blnCCPinIsCC2 == FALSE))	// For automated testing
	{
		DetectCCPinSource();
	}
	if (blnCCPinIsCC1)	// If CC1 is detected as the CC pin...
	{
		peekCC2Source();
		Registers.Switches.byte[0] = 0x44;	// Configure VCONN on CC2, pull-up on CC1, measure CC1
		//if(CC2TermPrevious == CCTypeRa)                                         // Turn on VCONN if there is Ra
		{
			Registers.Switches.VCONN_CC2 = 1;
		}
		setDebounceVariablesCC1(CCTypeUndefined);
	} else			// Otherwise we are assuming CC2 is CC
	{
		peekCC1Source();
		Registers.Switches.byte[0] = 0x88;	// Configure VCONN on CC1, pull-up on CC2, measure CC2
		//if(CC1TermPrevious == CCTypeRa)                                         // Turn on VCONN if there is Ra
		{
			Registers.Switches.VCONN_CC1 = 1;
		}
		setDebounceVariablesCC2(CCTypeUndefined);
	}
	DeviceWrite(regSwitches0, 1, &Registers.Switches.byte[0]);	// Commit the switch state
	platform_enableSuperspeedUSB(blnCCPinIsCC1, blnCCPinIsCC2);
	platform_toggleAudioSwitch(fsa_usb_mode);
#ifdef CONFIG_FSUSB42_MUX
	if (fsusb42_get_state() != FSUSB_STATE_EXT)
		fsusb42_set_state(FSUSB_STATE_USB);
#endif
	USBPDEnable(TRUE, TRUE);	// Enable the USB PD state machine if applicable (no need to write to Device again), set as DFP
	SinkCurrent = utccNone;	// Set the Sink current to none (not used in source)
	StateTimer = T_TIMER_DISABLE;	// Disable the state timer, not used in this state
	PDDebounce = tPDDebounce;	// Set the debounce timer to tPDDebounceMin for detecting a detach
	CCDebounce = tCCDebounce;	// Disable the 2nd level debouncing, not needed in this state
	ToggleTimer = T_TIMER_DISABLE;	// No toggle on sources
	OverPDDebounce = T_TIMER_DISABLE;	// Disable PD filter timer
	usbc_psy.type = POWER_SUPPLY_TYPE_USBC_SRC;
	power_supply_changed(&usbc_psy);
	/* Notify USB driver to switch to host mode */
	/* Only equal or below Rd*/
	if (usb_psy && (bc_lvl < 2))
		power_supply_set_usb_otg(usb_psy, 1);
#ifdef FSC_DEBUG
	WriteStateLog(&TypeCStateLog, ConnState, Timer_tms, Timer_S);
#endif // FSC_DEBUG
}
#endif // FSC_HAVE_SRC

#ifdef FSC_HAVE_SNK
void SetStateAttachedSink(void)
{
#ifdef FSC_INTERRUPT_TRIGGERED
	g_Idle = TRUE;		// Mask for VBUSOK
	Registers.Mask.byte = 0xFF;
	Registers.Mask.M_VBUSOK = 0;
	DeviceWrite(regMask, 1, &Registers.Mask.byte);
	Registers.MaskAdv.byte[0] = 0xFF;
	DeviceWrite(regMaska, 1, &Registers.MaskAdv.byte[0]);
	Registers.MaskAdv.M_GCRCSENT = 1;
	DeviceWrite(regMaskb, 1, &Registers.MaskAdv.byte[1]);
	platform_enable_timer(FALSE);
#endif
	platform_set_vbus_lvl_enable(VBUS_LVL_ALL, FALSE, FALSE);	// Disable the vbus outputs
	ConnState = AttachedSink;	// Set the state machine variable to Attached.Sink
	sourceOrSink = Sink;
	Registers.Power.PWR = 0x7;	// Enable everything except internal oscillator
	DeviceWrite(regPower, 1, &Registers.Power.byte);	// Commit the power state
	if ((blnCCPinIsCC1 == FALSE) && (blnCCPinIsCC2 == FALSE))	//For automated testing
	{
		DetectCCPinSink();
	}
	if (blnCCPinIsCC1)	// If CC1 is detected as the CC pin...
	{
		peekCC2Sink();
		peekCC1Sink();
		Registers.Switches.byte[0] = 0x07;
		UpdateSinkCurrent(CC1TermCCDebounce);
	} else			// Otherwise we are assuming CC2 is CC
	{
		peekCC1Sink();
		peekCC2Sink();
		Registers.Switches.byte[0] = 0x0B;
		UpdateSinkCurrent(CC2TermCCDebounce);
	}
	DeviceWrite(regSwitches0, 1, &Registers.Switches.byte[0]);	// Commit the switch state
	platform_enableSuperspeedUSB(blnCCPinIsCC1, blnCCPinIsCC2);
	platform_toggleAudioSwitch(fsa_usb_mode);
#ifdef CONFIG_FSUSB42_MUX
	if (fsusb42_get_state() != FSUSB_STATE_EXT)
		fsusb42_set_state(FSUSB_STATE_USB);
#endif
	USBPDEnable(TRUE, FALSE);	// Enable the USB PD state machine (no need to write Device again since we are doing it here)
	StateTimer = T_TIMER_DISABLE;	// Disable the state timer, not used in this state
	PDDebounce = tPDDebounce;	// Set the debounce timer to tPDDebounceMin for detecting changes in advertised current
	CCDebounce = T_TIMER_DISABLE;	// Disable the 2nd level debounce timer, not used in this state
	ToggleTimer = T_TIMER_DISABLE;	// Disable the toggle timer, not used in this state
	OverPDDebounce = T_TIMER_DISABLE;	// Disable PD filter timer
	usbc_psy.type = POWER_SUPPLY_TYPE_USBC_SINK;
	power_supply_changed(&usbc_psy);
#ifdef FSC_DEBUG
	WriteStateLog(&TypeCStateLog, ConnState, Timer_tms, Timer_S);
#endif // FSC_DEBUG
}
#endif // FSC_HAVE_SNK

#ifdef FSC_HAVE_DRP
void RoleSwapToAttachedSink(void)
{
	ConnState = AttachedSink;	// Set the state machine variable to Attached.Sink   
	sourceOrSink = Sink;
	updateSourceCurrent();
	if (blnCCPinIsCC1)	// If the CC pin is CC1...
	{
		// Maintain VCONN
		Registers.Switches.PU_EN1 = 0;	// Disable the pull-up on CC1
		Registers.Switches.PDWN1 = 1;	// Enable the pull-down on CC1
	} else {
		// Maintain VCONN
		Registers.Switches.PU_EN2 = 0;	// Disable the pull-up on CC2
		Registers.Switches.PDWN2 = 1;	// Enable the pull-down on CC2                                   
	}
	DeviceWrite(regSwitches0, 1, &Registers.Switches.byte[0]);	// Commit the switch state
	SinkCurrent = utccNone;	// Set the current advertisment variable to none until we determine what the current is
	StateTimer = T_TIMER_DISABLE;	// Disable the state timer, not used in this state
	PDDebounce = tPDDebounce;	// Set the debounce timer to tPDDebounceMin for detecting changes in advertised current
	CCDebounce = tCCDebounce;	// Disable the 2nd level debounce timer, not used in this state
	ToggleTimer = T_TIMER_DISABLE;
	OverPDDebounce = T_TIMER_DISABLE;	// Disable PD filter timer
#ifdef FSC_DEBUG
	WriteStateLog(&TypeCStateLog, ConnState, Timer_tms, Timer_S);
#endif // FSC_DEBUG
}
#endif // FSC_HAVE_DRP

#ifdef FSC_HAVE_DRP
void RoleSwapToAttachedSource(void)
{
	platform_set_vbus_lvl_enable(VBUS_LVL_5V, TRUE, TRUE);	// Enable only the 5V output
	ConnState = AttachedSource;	// Set the state machine variable to Attached.Src
	TypeCSubState = 0;
	sourceOrSink = Source;
	updateSourceCurrent();
	if (blnCCPinIsCC1)	// If the CC pin is CC1...
	{
		Registers.Switches.PU_EN1 = 1;	// Enable the pull-up on CC1
		Registers.Switches.PDWN1 = 0;	// Disable the pull-down on CC1
		Registers.Switches.MEAS_CC1 = 1;
		setDebounceVariablesCC1(CCTypeUndefined);
	} else {
		Registers.Switches.PU_EN2 = 1;	// Enable the pull-up on CC2
		Registers.Switches.PDWN2 = 0;	// Disable the pull-down on CC2
		Registers.Switches.MEAS_CC2 = 1;
		setDebounceVariablesCC2(CCTypeUndefined);
	}
	DeviceWrite(regSwitches0, 1, &Registers.Switches.byte[0]);	// Commit the switch state
	SinkCurrent = utccNone;	// Set the Sink current to none (not used in Src)
	StateTimer = T_TIMER_DISABLE;	// Disable the state timer, not used in this state
	PDDebounce = tPDDebounce;	// Set the debounce timer to tPDDebounceMin for detecting a detach
	CCDebounce = tCCDebounce;	// Disable the 2nd level debouncing, not needed in this state
	ToggleTimer = T_TIMER_DISABLE;	// Disable the toggle timer, not used in this state
	OverPDDebounce = T_TIMER_DISABLE;	// Disable PD filter timer
#ifdef FSC_DEBUG
	WriteStateLog(&TypeCStateLog, ConnState, Timer_tms, Timer_S);
#endif // FSC_DEBUG
}
#endif // FSC_HAVE_DRP

#ifdef FSC_HAVE_DRP
void SetStateTryWaitSink(void)
{
#ifdef FSC_INTERRUPT_TRIGGERED
	g_Idle = FALSE;		// Mask all
	Registers.Mask.byte = 0xFF;
	DeviceWrite(regMask, 1, &Registers.Mask.byte);
	Registers.MaskAdv.byte[0] = 0xFF;
	DeviceWrite(regMaska, 1, &Registers.MaskAdv.byte[0]);
	Registers.MaskAdv.M_GCRCSENT = 1;
	DeviceWrite(regMaskb, 1, &Registers.MaskAdv.byte[1]);
	platform_enable_timer(TRUE);
#endif
	USBPDDisable(TRUE);
	platform_set_vbus_lvl_enable(VBUS_LVL_ALL, FALSE, FALSE);	// Disable the vbus outputs
	ConnState = TryWaitSink;	// Set the state machine variable to TryWait.Snk
	sourceOrSink = Sink;
	updateSourceCurrent();
	Registers.Switches.byte[0] = 0x07;	// Enable the pull-downs on the CC pins
	Registers.Power.PWR = 0x7;	// Enable everything except internal oscillator
	resetDebounceVariables();
	DeviceWrite(regSwitches0, 1, &Registers.Switches.byte[0]);	// Commit the switch state
	DeviceWrite(regPower, 1, &Registers.Power.byte);	// Commit the power state
	SinkCurrent = utccNone;	// Set the current advertisment variable to none until we determine what the current is
	StateTimer = tDRPTryWait;	// Set the state timer to tDRPTryWait to timeout if Rp isn't detected
	PDDebounce = tPDDebounce;	// The 1st level debouncing is based upon tPDDebounce
	CCDebounce = T_TIMER_DISABLE;	// Disable the 2nd level debouncing initially until we validate the 1st level
	ToggleTimer = tDeviceToggle;	// Toggle the measure quickly (tDeviceToggle) to see if we detect an Rp on either
	OverPDDebounce = T_TIMER_DISABLE;	// Disable PD filter timer
	usbc_psy.type = POWER_SUPPLY_TYPE_USBC;
	power_supply_changed(&usbc_psy);
#ifdef FSC_DEBUG
	WriteStateLog(&TypeCStateLog, ConnState, Timer_tms, Timer_S);
#endif // FSC_DEBUG
}
#endif // FSC_HAVE_DRP

#ifdef FSC_HAVE_DRP
void SetStateTrySource(void)
{
#ifdef FSC_INTERRUPT_TRIGGERED
	g_Idle = FALSE;		// Mask all
	Registers.Mask.byte = 0xFF;
	DeviceWrite(regMask, 1, &Registers.Mask.byte);
	Registers.MaskAdv.byte[0] = 0xFF;
	DeviceWrite(regMaska, 1, &Registers.MaskAdv.byte[0]);
	Registers.MaskAdv.M_GCRCSENT = 1;
	DeviceWrite(regMaskb, 1, &Registers.MaskAdv.byte[1]);
	platform_enable_timer(TRUE);
#endif
	platform_set_vbus_lvl_enable(VBUS_LVL_ALL, FALSE, FALSE);	// Disable the vbus outputs
	ConnState = TrySource;	// Set the state machine variable to Try.Src
	sourceOrSink = Source;
	updateSourceCurrent();
	Registers.Power.PWR = 0x7;	// Enable everything except internal oscillator
	DeviceWrite(regPower, 1, &Registers.Power.byte);	// Commit the power state
	if ((blnCCPinIsCC1 == FALSE) && (blnCCPinIsCC2 == FALSE))	//For automated testing
	{
		DetectCCPinSource();
	}
	if (blnCCPinIsCC1)	// If we detected CC1 as an Rd
	{
		peekCC2Source();
		Registers.Switches.byte[0] = 0x44;	// Enable the pull-up on CC1
		setDebounceVariablesCC1(CCTypeUndefined);
	} else {
		peekCC1Source();
		Registers.Switches.byte[0] = 0x88;	// Enable the pull-up on CC1\2
		setDebounceVariablesCC2(CCTypeUndefined);
	}

	DeviceWrite(regSwitches0, 1, &Registers.Switches.byte[0]);	// Commit the switch state
	SinkCurrent = utccNone;	// Set current to none
	OverPDDebounce = T_TIMER_DISABLE;	// Disable PD filter timer
	StateTimer = tDRPTry;	// Set the state timer to tDRPTry to timeout if Rd isn't detected
	PDDebounce = tPDDebounce;	// Debouncing is based solely off of tPDDebounce
	CCDebounce = T_TIMER_DISABLE;	// Disable the 2nd level since it's not needed
	ToggleTimer = T_TIMER_DISABLE;	// No toggle on sources
	usbc_psy.type = POWER_SUPPLY_TYPE_USBC;
	power_supply_changed(&usbc_psy);
#ifdef FSC_DEBUG
	WriteStateLog(&TypeCStateLog, ConnState, Timer_tms, Timer_S);
#endif // FSC_DEBUG
}
#endif // FSC_HAVE_DRP

#ifdef FSC_HAVE_DRP
void SetStateTrySink(void)
{
#ifdef FSC_INTERRUPT_TRIGGERED
	g_Idle = FALSE;		// Mask all
	Registers.Mask.byte = 0xFF;
	DeviceWrite(regMask, 1, &Registers.Mask.byte);
	Registers.MaskAdv.byte[0] = 0xFF;
	DeviceWrite(regMaska, 1, &Registers.MaskAdv.byte[0]);
	Registers.MaskAdv.M_GCRCSENT = 1;
	DeviceWrite(regMaskb, 1, &Registers.MaskAdv.byte[1]);
	platform_enable_timer(TRUE);
#endif
	platform_set_vbus_lvl_enable(VBUS_LVL_ALL, FALSE, FALSE);	// Disable the vbus outputs
	ConnState = TrySink;	// Set the state machine variable to Try.Snk
	sourceOrSink = Sink;
	updateSourceCurrent();
	Registers.Power.PWR = 0x7;	// Enable everything except internal oscillator
	DeviceWrite(regPower, 1, &Registers.Power.byte);	// Commit the power state
	Registers.Switches.byte[0] = 0x07;	// Set Rd on both CC
	DeviceWrite(regSwitches0, 1, &Registers.Switches.byte[0]);	// Commit the switch state
	resetDebounceVariables();
	SinkCurrent = utccNone;	// Not used in Try.Src
	StateTimer = tDRPTry;	// Set the state timer to tDRPTry to timeout if Rd isn't detected
	PDDebounce = tPDDebounce;	// Debouncing is based solely off of tPDDebounce
	CCDebounce = T_TIMER_DISABLE;	// Disable the 2nd level since it's not needed
	ToggleTimer = tDeviceToggle;	// Keep the pull-ups on for the max tPDDebounce to ensure that the other side acknowledges the pull-up
	OverPDDebounce = T_TIMER_DISABLE;	// Disable PD filter timer
	usbc_psy.type = POWER_SUPPLY_TYPE_USBC;
	power_supply_changed(&usbc_psy);
#ifdef FSC_DEBUG
	WriteStateLog(&TypeCStateLog, ConnState, Timer_tms, Timer_S);
#endif // FSC_DEBUG
}
#endif // FSC_HAVE_DRP

#ifdef FSC_HAVE_DRP
void SetStateTryWaitSource(void)
{
#ifdef FSC_INTERRUPT_TRIGGERED
	g_Idle = FALSE;		// Mask all
	Registers.Mask.byte = 0xFF;
	DeviceWrite(regMask, 1, &Registers.Mask.byte);
	Registers.MaskAdv.byte[0] = 0xFF;
	DeviceWrite(regMaska, 1, &Registers.MaskAdv.byte[0]);
	Registers.MaskAdv.M_GCRCSENT = 1;
	DeviceWrite(regMaskb, 1, &Registers.MaskAdv.byte[1]);
	platform_enable_timer(TRUE);
#endif
	platform_set_vbus_lvl_enable(VBUS_LVL_ALL, FALSE, FALSE);	// Disable the vbus outputs
	updateSourceCurrent();
	ConnState = TryWaitSource;	// Set the state machine variable to AttachWait.Src
	sourceOrSink = Source;
	Registers.Power.PWR = 0x7;	// Enable everything except internal oscillator
	DeviceWrite(regPower, 1, &Registers.Power.byte);	// Commit the power state
	if ((blnCCPinIsCC1 == FALSE) && (blnCCPinIsCC2 == FALSE))	//For automated testing
	{
		DetectCCPinSource();
	}
	if (blnCCPinIsCC1)	// If we detected CC1 as an Rd
	{
		peekCC2Source();
		Registers.Switches.byte[0] = 0x44;	// Enable CC1 pull-up and measure
		setDebounceVariablesCC1(CCTypeUndefined);
	} else {
		peekCC1Source();
		Registers.Switches.byte[0] = 0x88;	// Enable CC2 pull-up and measure  
		setDebounceVariablesCC2(CCTypeUndefined);
	}

	DeviceWrite(regSwitches0, 1, &Registers.Switches.byte[0]);	// Commit the switch state    
	SinkCurrent = utccNone;	// Not used in Src
	OverPDDebounce = T_TIMER_DISABLE;	// Disable PD filter timer
	StateTimer = tDRPTry;	// Disable the state timer, not used in this state
	PDDebounce = tPDDebounce;	// Only debounce the lines for tPDDebounce so that we can debounce a detach condition
	CCDebounce = T_TIMER_DISABLE;	// Not needed in this state
	ToggleTimer = T_TIMER_DISABLE;	// No toggle on sources
	usbc_psy.type = POWER_SUPPLY_TYPE_USBC;
	power_supply_changed(&usbc_psy);
#ifdef FSC_DEBUG
	WriteStateLog(&TypeCStateLog, ConnState, Timer_tms, Timer_S);
#endif // FSC_DEBUG
}
#endif // FSC_HAVE_DRP

#ifdef FSC_HAVE_ACCMODE
void SetStateDebugAccessory(void)
{
#ifdef FSC_INTERRUPT_TRIGGERED
	g_Idle = TRUE;		// Mask for COMP
	Registers.Mask.byte = 0xFF;
	Registers.Mask.M_COMP_CHNG = 0;
	DeviceWrite(regMask, 1, &Registers.Mask.byte);
	Registers.MaskAdv.byte[0] = 0xFF;
	DeviceWrite(regMaska, 1, &Registers.MaskAdv.byte[0]);
	Registers.MaskAdv.M_GCRCSENT = 1;
	DeviceWrite(regMaskb, 1, &Registers.MaskAdv.byte[1]);
	platform_enable_timer(FALSE);
#endif
	platform_set_vbus_lvl_enable(VBUS_LVL_ALL, FALSE, FALSE);	// Disable the vbus outputs
	ConnState = DebugAccessory;	// Set the state machine variable to Debug.Accessory
	sourceOrSink = Source;
	updateSourceCurrent();
	Registers.Power.PWR = 0x7;	// Enable everything except internal oscillator
	DeviceWrite(regPower, 1, &Registers.Power.byte);	// Commit the power state
	if ((blnCCPinIsCC1 == FALSE) && (blnCCPinIsCC2 == FALSE))	//For automated testing
	{
		DetectCCPinSource();
	}
	if (blnCCPinIsCC1)	// If CC1 is detected as the CC pin...
	{
		peekCC2Source();
		Registers.Switches.byte[0] = 0x44;	// Configure VCONN on CC2, pull-up on CC1, measure CC1
		setDebounceVariablesCC1(CCTypeUndefined);
	} else			// Otherwise we are assuming CC2 is CC
	{
		peekCC1Source();
		setDebounceVariablesCC2(CCTypeUndefined);
		Registers.Switches.byte[0] = 0x88;	// Configure VCONN on CC1, pull-up on CC2, measure CC2
	}

	DeviceWrite(regSwitches0, 1, &Registers.Switches.byte[0]);	// Commit the switch state
	SinkCurrent = utccNone;	// Not used in accessories
	StateTimer = T_TIMER_DISABLE;	// Disable the state timer, not used in this state
	PDDebounce = T_TIMER_DISABLE;	// Once in this state, we are waiting for the lines to be stable for tCCDebounce before changing states
	CCDebounce = T_TIMER_DISABLE;	// Disable the 2nd level debouncing initially to force completion of a 1st level debouncing
	ToggleTimer = T_TIMER_DISABLE;	// Once we are in the debug.accessory state, we are going to stop toggling and only monitor CC1
	OverPDDebounce = T_TIMER_DISABLE;	// Disable PD filter timer
	usbc_psy.type = POWER_SUPPLY_TYPE_USBC_DBG;
	power_supply_changed(&usbc_psy);
	platform_toggleAudioSwitch(fsa_usb_mode);
#ifdef FSC_DEBUG
	WriteStateLog(&TypeCStateLog, ConnState, Timer_tms, Timer_S);
#endif // FSC_DEBUG
}

void SetStateAudioAccessory(void)
{
#ifdef FSC_INTERRUPT_TRIGGERED
	g_Idle = FALSE;		// Mask all
	Registers.Mask.byte = 0xFF;
	DeviceWrite(regMask, 1, &Registers.Mask.byte);
	Registers.MaskAdv.byte[0] = 0xFF;
	DeviceWrite(regMaska, 1, &Registers.MaskAdv.byte[0]);
	Registers.MaskAdv.M_GCRCSENT = 1;
	DeviceWrite(regMaskb, 1, &Registers.MaskAdv.byte[1]);
	platform_enable_timer(TRUE);
#endif
	if (alternateModes) {
		SetStateAlternateAudioAccessory();
		return;
	}
	platform_set_vbus_lvl_enable(VBUS_LVL_ALL, FALSE, FALSE);	// Disable the vbus outputs
	ConnState = AudioAccessory;	// Set the state machine variable to Audio.Accessory
	sourceOrSink = Source;
	updateSourceCurrent();
	Registers.Power.PWR = 0x7;	// Enable everything except internal oscillator
	DeviceWrite(regPower, 1, &Registers.Power.byte);	// Commit the power state
	if ((blnCCPinIsCC1 == FALSE) && (blnCCPinIsCC2 == FALSE))	//For automated testing
	{
		DetectCCPinSource();
	}
	if (blnCCPinIsCC1)	// If CC1 is detected as the CC pin...
	{
		peekCC2Source();
		Registers.Switches.byte[0] = 0x44;	// Configure VCONN on CC2, pull-up on CC1, measure CC1
		setDebounceVariablesCC1(CCTypeUndefined);
	} else			// Otherwise we are assuming CC2 is CC
	{
		peekCC1Source();
		setDebounceVariablesCC2(CCTypeUndefined);
		Registers.Switches.byte[0] = 0x88;	// Configure VCONN on CC1, pull-up on CC2, measure CC2
	}
	DeviceWrite(regSwitches0, 1, &Registers.Switches.byte[0]);	// Commit the switch state
	if (Registers.Status.VBUSOK && debug_audio) {
		FUSB_LOG("Audio Debug Accessory, Enable SS\n");
		usbc_psy.type = POWER_SUPPLY_TYPE_USBC_SINK;
		platform_enableSuperspeedUSB(blnCCPinIsCC1, blnCCPinIsCC2);
	} else
		usbc_psy.type = POWER_SUPPLY_TYPE_USBC_AUDIO;
	SinkCurrent = utccNone;	// Not used in accessories
	OverPDDebounce = T_TIMER_DISABLE;	// Disable PD filter timer
	StateTimer = T_TIMER_DISABLE;	// Disable the state timer, not used in this state
	PDDebounce = tCCDebounce;	// Once in this state, we are waiting for the lines to be stable for tCCDebounce before changing states
	CCDebounce = T_TIMER_DISABLE;	// Disable the 2nd level debouncing initially to force completion of a 1st level debouncing
	ToggleTimer = T_TIMER_DISABLE;	// Once we are in the audio.accessory state, we are going to stop toggling and only monitor CC1
	platform_toggleAudioSwitch(fsa_audio_mode);
	power_supply_changed(&usbc_psy);
#ifdef FSC_DEBUG
	WriteStateLog(&TypeCStateLog, ConnState, Timer_tms, Timer_S);
#endif // FSC_DEBUG
}

void SetStatePoweredAccessory(void)
{
#ifdef FSC_INTERRUPT_TRIGGERED
	g_Idle = FALSE;		// Mask all
	Registers.Mask.byte = 0xFF;
	DeviceWrite(regMask, 1, &Registers.Mask.byte);
	Registers.MaskAdv.byte[0] = 0xFF;
	DeviceWrite(regMaska, 1, &Registers.MaskAdv.byte[0]);
	Registers.MaskAdv.M_GCRCSENT = 1;
	DeviceWrite(regMaskb, 1, &Registers.MaskAdv.byte[1]);
	platform_enable_timer(TRUE);
#endif
	platform_set_vbus_lvl_enable(VBUS_LVL_ALL, FALSE, FALSE);	// Disable the vbus outputs
	ConnState = PoweredAccessory;	// Set the state machine variable to powered.accessory
	sourceOrSink = Source;
	Registers.Power.PWR = 0x7;
	DeviceWrite(regPower, 1, &Registers.Power.byte);	// Commit the power state
	if (SourceCurrent == utccDefault)	// If the current is default set it to 1.5A advert (Must be 1.5 or 3.0)
	{
		Registers.Control.HOST_CUR = 0b10;
		DeviceWrite(regControl0, 1, &Registers.Control.byte[0]);
	} else {
		updateSourceCurrent();
	}
	if ((blnCCPinIsCC1 == FALSE) && (blnCCPinIsCC2 == FALSE))	//For automated testing
	{
		DetectCCPinSource();
	}
	if (blnCCPinIsCC1)	// If CC1 is detected as the CC pin...
	{
		peekCC2Source();
		Registers.Switches.byte[0] = 0x64;	// Configure VCONN on CC2, pull-up on CC1, measure CC1
		setDebounceVariablesCC1(CCTypeUndefined);
	} else			// Otherwise we are assuming CC2 is CC
	{
		blnCCPinIsCC2 = TRUE;
		peekCC1Source();
		setDebounceVariablesCC2(CCTypeUndefined);
		Registers.Switches.byte[0] = 0x98;	// Configure VCONN on CC1, pull-up on CC2, measure CC2
	}
	DeviceWrite(regSwitches0, 1, &Registers.Switches.byte[0]);	// Commit the switch state
	// TODO: The line below will be uncommented once we have full support for VDM's and can enter an alternate mode as needed for Powered.Accessories
	//USBPDEnable(TRUE, TRUE);  //~Rick: Disabled for compliance test  

	SinkCurrent = utccNone;	// Set the Sink current to none (not used in source)
	StateTimer = tAMETimeout;	// Set the state timer to tAMETimeout (need to enter alternate mode by this time)
	PDDebounce = tPDDebounce;	// Set the debounce timer to the minimum tPDDebounce to check for detaches
	CCDebounce = T_TIMER_DISABLE;	// Disable the 2nd level debounce timer, not used in this state
	ToggleTimer = T_TIMER_DISABLE;	// Disable the toggle timer, only looking at the actual CC line
	OverPDDebounce = T_TIMER_DISABLE;	// Disable PD filter timer
	usbc_psy.type = POWER_SUPPLY_TYPE_USBC_AUDIO;
	power_supply_changed(&usbc_psy);
#ifdef FSC_DEBUG
	WriteStateLog(&TypeCStateLog, ConnState, Timer_tms, Timer_S);
#endif // FSC_DEBUG
}

void SetStateUnsupportedAccessory(void)
{
#ifdef FSC_INTERRUPT_TRIGGERED
	g_Idle = TRUE;		// Mask for COMP
	Registers.Mask.byte = 0xFF;
	Registers.Mask.M_COMP_CHNG = 0;
	DeviceWrite(regMask, 1, &Registers.Mask.byte);
	Registers.MaskAdv.byte[0] = 0xFF;
	DeviceWrite(regMaska, 1, &Registers.MaskAdv.byte[0]);
	Registers.MaskAdv.M_GCRCSENT = 1;
	DeviceWrite(regMaskb, 1, &Registers.MaskAdv.byte[1]);
	platform_enable_timer(FALSE);
#endif
	platform_set_vbus_lvl_enable(VBUS_LVL_ALL, FALSE, FALSE);	// Disable the vbus outputs
	ConnState = UnsupportedAccessory;	// Set the state machine variable to unsupported.accessory
	sourceOrSink = Source;
	Registers.Control.HOST_CUR = 0b01;	// Must advertise default current
	DeviceWrite(regControl0, 1, &Registers.Control.byte[0]);
	Registers.Power.PWR = 0x7;
	DeviceWrite(regPower, 1, &Registers.Power.byte);	// Commit the power state
	if ((blnCCPinIsCC1 == FALSE) && (blnCCPinIsCC2 == FALSE))	//For automated testing
	{
		DetectCCPinSource();
	}
	if (blnCCPinIsCC1)	// If CC1 is detected as the CC pin...
	{
		peekCC2Source();
		Registers.Switches.byte[0] = 0x44;
		setDebounceVariablesCC1(CCTypeUndefined);
	} else			// Otherwise we are assuming CC2 is CC
	{
		blnCCPinIsCC2 = TRUE;
		peekCC1Source();
		Registers.Switches.byte[0] = 0x88;
		setDebounceVariablesCC2(CCTypeUndefined);
	}
	DeviceWrite(regSwitches0, 1, &Registers.Switches.byte[0]);	// Commit the switch state
	SinkCurrent = utccNone;	// Set the Sink current to none (not used in source)
	StateTimer = T_TIMER_DISABLE;	// Disable the state timer, not used in this state
	PDDebounce = tPDDebounce;	// Set the debounce timer to the minimum tPDDebounce to check for detaches
	CCDebounce = T_TIMER_DISABLE;	// Disable the 2nd level debounce timer, not used in this state
	ToggleTimer = T_TIMER_DISABLE;	// Disable the toggle timer, only looking at the actual CC line
	OverPDDebounce = T_TIMER_DISABLE;	// Disable PD filter timer
	usbc_psy.type = POWER_SUPPLY_TYPE_USBC_UNSUPP;
	power_supply_changed(&usbc_psy);
#ifdef FSC_DEBUG
	WriteStateLog(&TypeCStateLog, ConnState, Timer_tms, Timer_S);
#endif // FSC_DEBUG
}
#endif // FSC_HAVE_ACCMODE

#ifdef FSC_HAVE_SRC
void SetStateUnattachedSource(void)	// Currently only implemented for transitioning from AttachWaitSnk to Unattached for DRP
{
	if (alternateModes) {
		SetStateAlternateUnattachedSource();
		return;
	}
#ifdef FSC_INTERRUPT_TRIGGERED
	g_Idle = FALSE;		// Mask all
	Registers.Mask.byte = 0xFF;
	DeviceWrite(regMask, 1, &Registers.Mask.byte);
	Registers.MaskAdv.byte[0] = 0xFF;
	DeviceWrite(regMaska, 1, &Registers.MaskAdv.byte[0]);
	Registers.MaskAdv.M_GCRCSENT = 1;
	DeviceWrite(regMaskb, 1, &Registers.MaskAdv.byte[1]);
	platform_enable_timer(TRUE);
#endif
	platform_set_vbus_lvl_enable(VBUS_LVL_ALL, FALSE, FALSE);	// Disable the vbus outputs
	ConnState = UnattachedSource;	// Set the state machine variable to unattached
	sourceOrSink = Source;
	Registers.Switches.byte[0] = 0x44;	// Enable the pull-up and measure on CC1
	Registers.Power.PWR = 0x7;	// Enable everything except internal oscillator
	updateSourceCurrent();	// Updates source current
	DeviceWrite(regPower, 1, &Registers.Power.byte);	// Commit the power state
	DeviceWrite(regSwitches0, 1, &Registers.Switches.byte[0]);	// Commit the switch state
	USBPDDisable(TRUE);	// Disable the USB PD state machine (no need to write Device again since we are doing it here)
	SinkCurrent = utccNone;
	resetDebounceVariables();
	blnCCPinIsCC1 = FALSE;	// Clear the CC1 pin flag 
	blnCCPinIsCC2 = FALSE;	// Clear the CC2 pin flag
	StateTimer = T_TIMER_DISABLE;	// Disable the state timer, not used in this state
	PDDebounce = tPDDebounce;	// enable the 1st level debounce timer, not used in this state
	CCDebounce = tCCDebounce;	// enable the 2nd level debounce timer, not used in this state
	ToggleTimer = tDeviceToggle;	// enable the toggle timer
	DRPToggleTimer = tTOG2;	// Timer to switch from unattachedSrc to unattachedSnk in DRP
	OverPDDebounce = tPDDebounce;	// enable PD filter timer
	usbc_psy.type = POWER_SUPPLY_TYPE_USBC;
	power_supply_changed(&usbc_psy);
#ifdef FSC_DEBUG
	WriteStateLog(&TypeCStateLog, ConnState, Timer_tms, Timer_S);
#endif // FSC_DEBUG
}
#endif // FSC_HAVE_SRC

void updateSourceCurrent(void)
{
	switch (SourceCurrent) {
	case utccDefault:
		Registers.Control.HOST_CUR = 0b01;	// Set the host current to reflect the default USB power
		break;
	case utcc1p5A:
		Registers.Control.HOST_CUR = 0b10;	// Set the host current to reflect 1.5A
		break;
	case utcc3p0A:
		Registers.Control.HOST_CUR = 0b11;	// Set the host current to reflect 3.0A
		break;
	default:		// This assumes that there is no current being advertised
		Registers.Control.HOST_CUR = 0b00;	// Set the host current to disabled
		break;
	}
	DeviceWrite(regControl0, 1, &Registers.Control.byte[0]);	// Commit the host current
}

void updateSourceMDACHigh(void)
{
	switch (SourceCurrent) {
	case utccDefault:
		Registers.Measure.MDAC = MDAC_1P6V;	// Set up DAC threshold to 1.6V (default USB current advertisement)
		break;
	case utcc1p5A:
		Registers.Measure.MDAC = MDAC_1P6V;	// Set up DAC threshold to 1.6V
		break;
	case utcc3p0A:
		Registers.Measure.MDAC = MDAC_2P6V;	// Set up DAC threshold to 2.6V
		break;
	default:		// This assumes that there is no current being advertised
		Registers.Measure.MDAC = MDAC_1P6V;	// Set up DAC threshold to 1.6V (default USB current advertisement)
		break;
	}
	DeviceWrite(regMeasure, 1, &Registers.Measure.byte);	// Commit the DAC threshold
}

void updateSourceMDACLow(void)
{
	switch (SourceCurrent) {
	case utccDefault:
		Registers.Measure.MDAC = MDAC_0P2V;	// Set up DAC threshold to 1.6V (default USB current advertisement)
		break;
	case utcc1p5A:
		Registers.Measure.MDAC = MDAC_0P4V;	// Set up DAC threshold to 1.6V
		break;
	case utcc3p0A:
		Registers.Measure.MDAC = MDAC_0P8V;	// Set up DAC threshold to 2.6V
		break;
	default:		// This assumes that there is no current being advertised
		Registers.Measure.MDAC = MDAC_1P6V;	// Set up DAC threshold to 1.6V (default USB current advertisement)
		break;
	}
	DeviceWrite(regMeasure, 1, &Registers.Measure.byte);	// Commit the DAC threshold
}

/////////////////////////////////////////////////////////////////////////////
//                        Type C Support Routines
/////////////////////////////////////////////////////////////////////////////

void ToggleMeasureCC1(void)
{
	if (!alternateModes) {
		Registers.Switches.PU_EN1 = Registers.Switches.PU_EN2;	// If the pull-up was enabled on CC2, enable it for CC1
		Registers.Switches.PU_EN2 = 0;	// Disable the pull-up on CC2 regardless, since we aren't measuring CC2 (prevent short)
	}
	Registers.Switches.MEAS_CC1 = 1;	// Set CC1 to measure
	Registers.Switches.MEAS_CC2 = 0;	// Clear CC2 from measuring
	DeviceWrite(regSwitches0, 1, &Registers.Switches.byte[0]);	// Set the switch to measure

//    Delay10us(25);                                                          // Delay the reading of the COMP and BC_LVL to allow time for settling
//    DeviceRead(regStatus0, 2, &Registers.Status.byte[4]);                  // Read back the status to get the current COMP and BC_LVL
}

void ToggleMeasureCC2(void)
{
	if (!alternateModes) {
		Registers.Switches.PU_EN2 = Registers.Switches.PU_EN1;	// If the pull-up was enabled on CC1, enable it for CC2
		Registers.Switches.PU_EN1 = 0;	// Disable the pull-up on CC1 regardless, since we aren't measuring CC1 (prevent short)
	}
	Registers.Switches.MEAS_CC1 = 0;	// Clear CC1 from measuring
	Registers.Switches.MEAS_CC2 = 1;	// Set CC2 to measure
	DeviceWrite(regSwitches0, 1, &Registers.Switches.byte[0]);	// Set the switch to measure

//    Delay10us(25);                                                          // Delay the reading of the COMP and BC_LVL to allow time for settling
//    DeviceRead(regStatus0, 2, &Registers.Status.byte[4]);                  // Read back the status to get the current COMP and BC_LVL
}

CCTermType DecodeCCTermination(void)
{
//    if(alternateModes)
//    {
//        AlternateDRPSourceSinkSwap();
//    }
	switch (sourceOrSink) {
#ifdef FSC_HAVE_SRC
	case Source:
//            if(alternateModes)
//            {
//                AlternateDRPSourceSinkSwap();
//            }
		return DecodeCCTerminationSource();
#endif // FSC_HAVE_SRC
#ifdef FSC_HAVE_SNK
	case Sink:
//            if(alternateModes)
//            {
//                AlternateDRPSourceSinkSwap();
//            }
		return DecodeCCTerminationSink();
#endif // FSC_HAVE_SNK
	default:
		return CCTypeUndefined;
	}
}

#if defined(FSC_HAVE_SRC) || (defined(FSC_HAVE_SNK) && defined(FSC_HAVE_ACCMODE))
CCTermType DecodeCCTerminationSource(void)
{
	regMeasure_t saved_measure;
	CCTermType Termination = CCTypeUndefined;	// By default set it to undefined
	saved_measure = Registers.Measure;

	updateSourceMDACHigh();
	platform_delay_10us(25);	// Delay to allow measurement to settle
	DeviceRead(regStatus0, 1, &Registers.Status.byte[4]);

	if (Registers.Status.COMP == 1) {
		Termination = CCTypeOpen;
		return Termination;
	}
	// Optimisation to assume CC pin in Attached Source is either Open or Rd
	else if ((ConnState == AttachedSource)
		 && ((Registers.Switches.MEAS_CC1 && blnCCPinIsCC1)
		     || (Registers.Switches.MEAS_CC2 && blnCCPinIsCC2))) {
		switch (SourceCurrent) {
		case utccDefault:
			Termination = CCTypeRdUSB;
			break;
		case utcc1p5A:
			Termination = CCTypeRd1p5;
			break;
		case utcc3p0A:
			Termination = CCTypeRd3p0;
			break;
		case utccNone:
			break;
		}
		return Termination;
	}

	updateSourceMDACLow();
	platform_delay_10us(25);	// Delay to allow measurement to settle
	DeviceRead(regStatus0, 1, &Registers.Status.byte[4]);

	if (Registers.Status.COMP == 0) {
		Termination = CCTypeRa;
	} else {
		switch (SourceCurrent) {
		case utccDefault:
			Termination = CCTypeRdUSB;
			break;
		case utcc1p5A:
			Termination = CCTypeRd1p5;
			break;
		case utcc3p0A:
			Termination = CCTypeRd3p0;
			break;
		case utccNone:
			break;
		}
	}
	Registers.Measure = saved_measure;
	DeviceWrite(regMeasure, 1, &Registers.Measure.byte);
	return Termination;	// Return the termination type
}
#endif // FSC_HAVE_SRC || (FSC_HAVE_SNK && FSC_HAVE_ACCMODE)

#ifdef FSC_HAVE_SNK
CCTermType DecodeCCTerminationSink(void)	// Port asserts Rd
{
	CCTermType Termination;
	platform_delay_10us(25);	// Delay to allow measurement to settle
	DeviceRead(regStatus0, 1, &Registers.Status.byte[4]);

	{
		switch (Registers.Status.BC_LVL)	// Determine which level
		{
		case 0b00:	// If BC_LVL is lowest it's open
			Termination = CCTypeOpen;
			break;
		case 0b01:	// If BC_LVL is 1, it's default
			Termination = CCTypeRdUSB;
			break;
		case 0b10:	// If BC_LVL is 2, it's vRd1p5
			Termination = CCTypeRd1p5;
			break;
		default:	// Otherwise it's vRd3p0
			Termination = CCTypeRd3p0;
			break;
		}
	}
	return Termination;	// Return the termination type
}
#endif // FSC_HAVE_SNK

#ifdef FSC_HAVE_SNK
void UpdateSinkCurrent(CCTermType Termination)
{
	switch (Termination) {
	case CCTypeRdUSB:	// If we detect the default...
		SinkCurrent = utccDefault;
		break;
	case CCTypeRd1p5:	// If we detect 1.5A
		SinkCurrent = utcc1p5A;
		break;
	case CCTypeRd3p0:	// If we detect 3.0A
		SinkCurrent = utcc3p0A;
		break;
	default:
		SinkCurrent = utccNone;
		break;
	}
}
#endif // FSC_HAVE_SNK

/////////////////////////////////////////////////////////////////////////////
//                     Externally Accessible Routines
/////////////////////////////////////////////////////////////////////////////
#ifdef FSC_DEBUG

void ConfigurePortType(FSC_U8 Control)
{
	FSC_U8 value;
	FSC_BOOL setUnattached = FALSE;
	DisableTypeCStateMachine();
	value = Control & 0x03;
	if (PortType != value) {
		switch (value) {
		case 1:
#ifdef FSC_HAVE_SRC
			PortType = USBTypeC_Source;
#endif // FSC_HAVE_SRC
			break;
		case 2:
#ifdef FSC_HAVE_DRP
			PortType = USBTypeC_DRP;
#endif // FSC_HAVE_DRP
			break;
		default:
#ifdef FSC_HAVE_SNK
			PortType = USBTypeC_Sink;
#endif // FSC_HAVE_SNK
			break;
		}

		setUnattached = TRUE;
	}
#ifdef FSC_HAVE_ACCMODE
	if (((Control & 0x04) >> 2) != blnAccSupport) {
		if (Control & 0x04) {
			blnAccSupport = TRUE;
		} else {
			blnAccSupport = FALSE;
		}
		setUnattached = TRUE;
	}
#endif // FSC_HAVE_ACCMODE

#ifdef FSC_HAVE_DRP
	if (((Control & 0x08) >> 3) != blnSrcPreferred) {
		if (Control & 0x08) {
			blnSrcPreferred = TRUE;
		} else {
			blnSrcPreferred = FALSE;
		}
		setUnattached = TRUE;
	}

	if (((Control & 0x40) >> 5) != blnSnkPreferred) {
		if (Control & 0x40) {
			blnSnkPreferred = TRUE;
		} else {
			blnSnkPreferred = FALSE;
		}
		setUnattached = TRUE;
	}
#endif // FSC_HAVE_DRP

	if (setUnattached) {
		SetStateDelayUnattached();
	}
#ifdef FSC_HAVE_SRC
	value = (Control & 0x30) >> 4;
	if (SourceCurrent != value) {
		switch (value) {
		case 1:
			SourceCurrent = utccDefault;
			break;
		case 2:
			SourceCurrent = utcc1p5A;
			break;
		case 3:
			SourceCurrent = utcc3p0A;
			break;
		default:
			SourceCurrent = utccNone;
			break;
		}
		updateSourceCurrent();
	}
#endif // FSC_HAVE_SRC

	if (Control & 0x80)
		EnableTypeCStateMachine();
}

#ifdef FSC_HAVE_SRC
void UpdateCurrentAdvert(FSC_U8 Current)
{
	switch (Current) {
	case 1:
		SourceCurrent = utccDefault;
		break;
	case 2:
		SourceCurrent = utcc1p5A;
		break;
	case 3:
		SourceCurrent = utcc3p0A;
		break;
	default:
		SourceCurrent = utccNone;
		break;
	}
	updateSourceCurrent();
}
#endif // FSC_HAVE_SRC

void GetDeviceTypeCStatus(FSC_U8 abytData[])
{
	FSC_S32 intIndex = 0;
	abytData[intIndex++] = GetTypeCSMControl();	// Grab a snapshot of the top level control
	abytData[intIndex++] = ConnState & 0xFF;	// Get the current state
	abytData[intIndex++] = GetCCTermination();	// Get the current CC termination
	abytData[intIndex++] = SinkCurrent;	// Set the sink current capability detected
}

FSC_U8 GetTypeCSMControl(void)
{
	FSC_U8 status = 0;
	status |= (PortType & 0x03);	// Set the type of port that we are configured as
	switch (PortType)	// Set the port type that we are configured as
	{
#ifdef FSC_HAVE_SRC
	case USBTypeC_Source:
		status |= 0x01;	// Set Source type
		break;
#endif // FSC_HAVE_SRC
#ifdef FSC_HAVE_DRP
	case USBTypeC_DRP:
		status |= 0x02;	// Set DRP type
		break;
#endif // FSC_HAVE_DRP
	default:		// If we are not DRP or Source, we are Sink which is a value of zero as initialized
		break;
	}

#ifdef FSC_HAVE_ACCMODE
	if (blnAccSupport)	// Set the flag if we support accessories 
		status |= 0x04;
#endif // FSC_HAVE_ACCMODE

#ifdef FSC_HAVE_DRP
	if (blnSrcPreferred)	// Set the flag if we prefer Source mode (as a DRP)
		status |= 0x08;
	status |= (SourceCurrent << 4);
	if (blnSnkPreferred)
		status |= 0x40;
#endif // FSC_HAVE_DRP

	if (blnSMEnabled)	// Set the flag if the state machine is enabled
		status |= 0x80;
	return status;
}

FSC_U8 GetCCTermination(void)
{
	FSC_U8 status = 0;

	status |= (CC1TermPrevious & 0x07);	// Set the current CC1 termination
	//    if (blnCC1Debounced)                    // Set the flag if the CC1 pin has been debounced
	//        status |= 0x08;
	status |= ((CC2TermPrevious & 0x07) << 4);	// Set the current CC2 termination
	//    if (blnCC2Debounced)                    // Set the flag if the CC2 pin has been debounced
	//        status |= 0x80;

	return status;
}
#endif // FSC_DEBUG

/////////////////////////////////////////////////////////////////////////////
//                        Device I2C Routines
/////////////////////////////////////////////////////////////////////////////
FSC_BOOL VbusVSafe0V(void)
{
#ifdef FPGA_BOARD
	DeviceRead(regStatus0, 1, &Registers.Status.byte[4]);
	if (Registers.Status.VBUSOK) {
		return FALSE;
	} else {
		return TRUE;
	}
#else
	regSwitches_t switches;
	regSwitches_t saved_switches;

	regMeasure_t measure;
	regMeasure_t saved_measure;

	FSC_U8 val;
	FSC_BOOL ret;

	DeviceRead(regSwitches0, 1, &(switches.byte[0]));	// Save state of switches
	saved_switches = switches;
	switches.MEAS_CC1 = 0;	// Clear out measure CC
	switches.MEAS_CC2 = 0;
	DeviceWrite(regSwitches0, 1, &(switches.byte[0]));	// Write it into device

	DeviceRead(regMeasure, 1, &measure.byte);	// Save state of measure
	saved_measure = measure;
	measure.MEAS_VBUS = 1;	// Measure VBUS
	measure.MDAC = VBUS_MDAC_0P8V;	// VSAFE0V = 0.8V max
	DeviceWrite(regMeasure, 1, &measure.byte);	// Write it into device

	platform_delay_10us(25);	// Delay to allow measurement to settle

	DeviceRead(regStatus0, 1, &val);	// get COMP result
	val &= 0x20;		// COMP = bit 5 of status0 (Device specific?)

	if (val)
		ret = FALSE;	// Determine return value based on COMP
	else
		ret = TRUE;

	DeviceWrite(regSwitches0, 1, &(saved_switches.byte[0]));	// restore register values
	DeviceWrite(regMeasure, 1, &saved_measure.byte);
	platform_delay_10us(25);	// allow time to settle in measurement block
	return ret;
#endif
}

#ifdef FSC_HAVE_SNK
FSC_BOOL VbusUnder5V(void)	// Returns true when Vbus < ~3.8V
{
	regMeasure_t measure;

	FSC_U8 val;
	FSC_BOOL ret;

	measure = Registers.Measure;
	measure.MEAS_VBUS = 1;	// Measure VBUS
	measure.MDAC = VBUS_MDAC_3p8;
	DeviceWrite(regMeasure, 1, &measure.byte);	// Write it into device

	platform_delay_10us(35);	// Delay to allow measurement to settle

	DeviceRead(regStatus0, 1, &val);	// get COMP result
	val &= 0x20;		// COMP = bit 5 of status0 (Device specific?)

	if (val)
		ret = FALSE;	// Determine return value based on COMP
	else
		ret = TRUE;

	// restore register values
	DeviceWrite(regMeasure, 1, &Registers.Measure.byte);
	return ret;
}
#endif // FSC_HAVE_SNK

FSC_BOOL isVSafe5V(void)	// Returns true when Vbus > ~4.6V
{
	regMeasure_t measure;

	FSC_U8 val;
	FSC_BOOL ret;

	measure = Registers.Measure;
	measure.MEAS_VBUS = 1;	// Measure VBUS
	measure.MDAC = VBUS_MDAC_4p6;
	DeviceWrite(regMeasure, 1, &measure.byte);	// Write it into device

	platform_delay_10us(35);	// Delay to allow measurement to settle

	DeviceRead(regStatus0, 1, &val);	// get COMP result
	val &= 0x20;		// COMP = bit 5 of status0 (Device specific?)

	if (val)
		ret = TRUE;	// Determine return value based on COMP
	else
		ret = FALSE;

	// restore register values
	DeviceWrite(regMeasure, 1, &Registers.Measure.byte);
	return ret;
}

FSC_BOOL isVBUSOverVoltage(FSC_U8 vbusMDAC)	// Returns true when Vbus > ~4.6V
{
	regMeasure_t measure;

	FSC_U8 val;
	FSC_BOOL ret;

	measure = Registers.Measure;
	measure.MEAS_VBUS = 1;	// Measure VBUS
	measure.MDAC = vbusMDAC;
	DeviceWrite(regMeasure, 1, &measure.byte);	// Write it into device

	platform_delay_10us(35);	// Delay to allow measurement to settle

	DeviceRead(regStatus0, 1, &val);	// get COMP result
	val &= 0x20;		// COMP = bit 5 of status0 (Device specific?)

	if (val)
		ret = TRUE;	// Determine return value based on COMP
	else
		ret = FALSE;

	// restore register values
	DeviceWrite(regMeasure, 1, &Registers.Measure.byte);
	return ret;
}

void DetectCCPinSource(void)	//Patch for directly setting states
{
	CCTermType CCTerm;

	Registers.Switches.byte[0] = 0x44;
	DeviceWrite(regSwitches0, 1, &(Registers.Switches.byte[0]));
	CCTerm = DecodeCCTermination();
	if ((CCTerm >= CCTypeRdUSB) && (CCTerm < CCTypeUndefined)) {
		blnCCPinIsCC1 = TRUE;	// The CC pin is CC1
		blnCCPinIsCC2 = FALSE;
		return;
	}

	Registers.Switches.byte[0] = 0x88;
	DeviceWrite(regSwitches0, 1, &(Registers.Switches.byte[0]));
	CCTerm = DecodeCCTermination();
	if ((CCTerm >= CCTypeRdUSB) && (CCTerm < CCTypeUndefined)) {

		blnCCPinIsCC1 = FALSE;	// The CC pin is CC2
		blnCCPinIsCC2 = TRUE;
		return;
	}
}

void DetectCCPinSink(void)	//Patch for directly setting states
{
	CCTermType CCTerm;

	Registers.Switches.byte[0] = 0x07;
	DeviceWrite(regSwitches0, 1, &(Registers.Switches.byte[0]));
	CCTerm = DecodeCCTermination();
	if ((CCTerm > CCTypeRa) && (CCTerm < CCTypeUndefined)) {
		blnCCPinIsCC1 = TRUE;	// The CC pin is CC1
		blnCCPinIsCC2 = FALSE;
		return;
	}

	Registers.Switches.byte[0] = 0x0B;
	DeviceWrite(regSwitches0, 1, &(Registers.Switches.byte[0]));
	CCTerm = DecodeCCTermination();
	if ((CCTerm > CCTypeRa) && (CCTerm < CCTypeUndefined)) {

		blnCCPinIsCC1 = FALSE;	// The CC pin is CC2
		blnCCPinIsCC2 = TRUE;
		return;
	}
}

void resetDebounceVariables(void)
{
	CC1TermPrevious = CCTypeUndefined;
	CC2TermPrevious = CCTypeUndefined;
	CC1TermCCDebounce = CCTypeUndefined;
	CC2TermCCDebounce = CCTypeUndefined;
	CC1TermPDDebounce = CCTypeUndefined;
	CC2TermPDDebounce = CCTypeUndefined;
	CC1TermPDDebouncePrevious = CCTypeUndefined;
	CC2TermPDDebouncePrevious = CCTypeUndefined;
}

void setDebounceVariablesCC1(CCTermType term)
{
	CC1TermPrevious = term;
	CC1TermCCDebounce = term;
	CC1TermPDDebounce = term;
	CC1TermPDDebouncePrevious = term;

}

void setDebounceVariablesCC2(CCTermType term)
{

	CC2TermPrevious = term;
	CC2TermCCDebounce = term;
	CC2TermPDDebounce = term;
	CC2TermPDDebouncePrevious = term;
}

////////////////////////////////////////////////////////////////////////////
//                     
////////////////////////////////////////////////////////////////////////////
#ifdef FSC_DEBUG
FSC_BOOL GetLocalRegisters(FSC_U8 * data, FSC_S32 size)	// Returns local registers as data array
{
	if (size != 23)
		return FALSE;

	data[0] = Registers.DeviceID.byte;
	data[1] = Registers.Switches.byte[0];
	data[2] = Registers.Switches.byte[1];
	data[3] = Registers.Measure.byte;
	data[4] = Registers.Slice.byte;
	data[5] = Registers.Control.byte[0];
	data[6] = Registers.Control.byte[1];
	data[7] = Registers.Control.byte[2];
	data[8] = Registers.Control.byte[3];
	data[9] = Registers.Mask.byte;
	data[10] = Registers.Power.byte;
	data[11] = Registers.Reset.byte;
	data[12] = Registers.OCPreg.byte;
	data[13] = Registers.MaskAdv.byte[0];
	data[14] = Registers.MaskAdv.byte[1];
	data[15] = Registers.Control4.byte;
	data[16] = Registers.Status.byte[0];
	data[17] = Registers.Status.byte[1];
	data[18] = Registers.Status.byte[2];
	data[19] = Registers.Status.byte[3];
	data[20] = Registers.Status.byte[4];
	data[21] = Registers.Status.byte[5];
	data[22] = Registers.Status.byte[6];

	return TRUE;
}

FSC_BOOL GetStateLog(FSC_U8 * data)
{				// Loads log into byte array
	FSC_S32 i;
	FSC_S32 entries = TypeCStateLog.Count;
	FSC_U16 state_temp;
	FSC_U16 time_tms_temp;
	FSC_U16 time_s_temp;

	for (i = 0; ((i < entries) && (i < 12)); i++) {
		ReadStateLog(&TypeCStateLog, &state_temp, &time_tms_temp,
			     &time_s_temp);

		data[i * 5 + 1] = state_temp;
		data[i * 5 + 2] = (time_tms_temp >> 8);
		data[i * 5 + 3] = (FSC_U8) time_tms_temp;
		data[i * 5 + 4] = (time_s_temp) >> 8;
		data[i * 5 + 5] = (FSC_U8) time_s_temp;
	}

	data[0] = i;		// Send number of log packets

	return TRUE;
}
#endif // FSC_DEBUG

void debounceCC(void)
{
	//PD Debounce (filter)
	CCTermType CCTermCurrent = DecodeCCTermination();	// Grab the latest CC termination value
	if (Registers.Switches.MEAS_CC1)	// If we are looking at CC1
	{
		if (CC1TermPrevious != CCTermCurrent)	// Check to see if the value has changed...
		{
			CC1TermPrevious = CCTermCurrent;	// If it has, update the value
			PDDebounce = tPDDebounce;	// Restart the debounce timer with tPDDebounce (wait 10ms before detach)
			if (OverPDDebounce == T_TIMER_DISABLE)	// Start debounce filter if it is not already enabled
			{
				OverPDDebounce = tPDDebounce;
			}
		}
	} else if (Registers.Switches.MEAS_CC2)	// Otherwise we are looking at CC2
	{
		if (CC2TermPrevious != CCTermCurrent)	// Check to see if the value has changed...
		{
			CC2TermPrevious = CCTermCurrent;	// If it has, update the value
			PDDebounce = tPDDebounce;	// Restart the debounce timer with tPDDebounce (wait 10ms before detach)
			if (OverPDDebounce == T_TIMER_DISABLE)	// Start debounce filter if it is not already enabled
			{
				OverPDDebounce = tPDDebounce;
			}
		}
	}
	if (PDDebounce == 0)	// Check to see if our debounce timer has expired...
	{
		CC1TermPDDebounce = CC1TermPrevious;	// Update the CC1 debounced values
		CC2TermPDDebounce = CC2TermPrevious;
		PDDebounce = T_TIMER_DISABLE;
		OverPDDebounce = T_TIMER_DISABLE;
	}
	if (OverPDDebounce == 0) {
		CCDebounce = tCCDebounce;
	}
	//CC debounce
	if ((CC1TermPDDebouncePrevious != CC1TermPDDebounce) || (CC2TermPDDebouncePrevious != CC2TermPDDebounce))	//If the PDDebounce values have changed
	{
		CC1TermPDDebouncePrevious = CC1TermPDDebounce;	//Update the previous value
		CC2TermPDDebouncePrevious = CC2TermPDDebounce;
		CCDebounce = tCCDebounce - tPDDebounce;	// reset the tCCDebounce timers
		CC1TermCCDebounce = CCTypeUndefined;	// Set CC debounce values to undefined while it is being debounced
		CC2TermCCDebounce = CCTypeUndefined;
	}
	if (CCDebounce == 0) {
		CC1TermCCDebounce = CC1TermPDDebouncePrevious;	// Update the CC debounced values
		CC2TermCCDebounce = CC2TermPDDebouncePrevious;
		CCDebounce = T_TIMER_DISABLE;
		OverPDDebounce = T_TIMER_DISABLE;
	}

	if (ToggleTimer == 0)	// If are toggle timer has expired, it's time to swap detection
	{
		if (Registers.Switches.MEAS_CC1)	// If we are currently on the CC1 pin...
			ToggleMeasureCC2();	// Toggle over to look at CC2
		else		// Otherwise assume we are using the CC2...
			ToggleMeasureCC1();	// So toggle over to look at CC1
		ToggleTimer = tDeviceToggle;	// Reset the toggle timer to our default toggling (<tPDDebounce to avoid disconnecting the other side when we remove pull-ups)
	}
}

#if defined(FSC_HAVE_SRC) || (defined(FSC_HAVE_SNK) && defined(FSC_HAVE_ACCMODE))
void peekCC1Source(void)
{
	FSC_U8 saveRegister = Registers.Switches.byte[0];	// Save current Switches

	Registers.Switches.byte[0] = 0x44;	//Measure CC2 and set CC Terms
	DeviceWrite(regSwitches0, 1, &Registers.Switches.byte[0]);
	setDebounceVariablesCC1(DecodeCCTerminationSource());

	Registers.Switches.byte[0] = saveRegister;	// Restore Switches
	DeviceWrite(regSwitches0, 1, &Registers.Switches.byte[0]);
}

void peekCC2Source(void)
{
	FSC_U8 saveRegister = Registers.Switches.byte[0];	// Save current Switches

	Registers.Switches.byte[0] = 0x88;	//Measure CC2
	DeviceWrite(regSwitches0, 1, &Registers.Switches.byte[0]);
	setDebounceVariablesCC2(DecodeCCTerminationSource());

	Registers.Switches.byte[0] = saveRegister;	// Restore Switches
	DeviceWrite(regSwitches0, 1, &Registers.Switches.byte[0]);
}
#endif // FSC_HAVE_SRC || (FSC_HAVE_SNK && FSC_HAVE_ACCMODE))

#ifdef FSC_HAVE_SNK
void peekCC1Sink(void)
{
	FSC_U8 saveRegister = Registers.Switches.byte[0];	// Save current Switches

	Registers.Switches.byte[0] = 0x07;
	DeviceWrite(regSwitches0, 1, &Registers.Switches.byte[0]);
	setDebounceVariablesCC1(DecodeCCTermination());

	Registers.Switches.byte[0] = saveRegister;	// Restore Switches
	DeviceWrite(regSwitches0, 1, &Registers.Switches.byte[0]);
}

void peekCC2Sink(void)
{
	FSC_U8 saveRegister = Registers.Switches.byte[0];	// Save current Switches

	Registers.Switches.byte[0] = 0x0B;
	DeviceWrite(regSwitches0, 1, &Registers.Switches.byte[0]);
	setDebounceVariablesCC2(DecodeCCTermination());

	Registers.Switches.byte[0] = saveRegister;	// Restore Switches
	DeviceWrite(regSwitches0, 1, &Registers.Switches.byte[0]);
}
#endif // FSC_HAVE_SNK

#ifdef FSC_HAVE_ACCMODE
void checkForAccessory(void)
{
	Registers.Control.TOGGLE = 0;
	DeviceWrite(regControl2, 1, &Registers.Control.byte[2]);
	peekCC1Source();
	peekCC2Source();

	if ((CC1TermPrevious > CCTypeOpen) && (CC2TermPrevious > CCTypeOpen))	// Transition to AttachWaitAccessory if not open on both pins
	{
		SetStateAttachWaitAccessory();
	} else			// Else get back to toggling
	{
		SetStateDelayUnattached();
	}
}
#endif // FSC_HAVE_ACCMODE

#ifdef FSC_DEBUG
void ProcessTypeCPDStatus(FSC_U8 * MsgBuffer, FSC_U8 * retBuffer)
{
	if (MsgBuffer[1] != 1)	// Check to see that the version is 1
		retBuffer[1] = 0x01;	// If it wasn't one, return that the version is not recognized
	else {
		GetDeviceTypeCStatus((FSC_U8 *) & retBuffer[4]);	// Return the status of the USB Type C state machine
		GetUSBPDStatus((FSC_U8 *) & retBuffer[8]);	// Return the status of the USB PD state machine
	}
}

void ProcessTypeCPDControl(FSC_U8 * MsgBuffer, FSC_U8 * retBuffer)
{
	if (MsgBuffer[1] != 0) {
		retBuffer[1] = 0x01;	// Return that the version is not recognized
		return;
	}
	switch (MsgBuffer[4])	// Determine command type
	{
	case 0x01:		// Reset the state machine
		DisableTypeCStateMachine();
		EnableTypeCStateMachine();
		break;
	case 0x02:		// Disable state machine
		DisableTypeCStateMachine();
		break;
	case 0x03:		// Enable state machine
		EnableTypeCStateMachine();
		break;
	case 0x04:		// Configure port type
		ConfigurePortType(MsgBuffer[5]);
		break;
#ifdef FSC_HAVE_SRC
	case 0x05:		// Update current advertisement
		UpdateCurrentAdvert(MsgBuffer[5]);
		break;
#endif // FSC_HAVE_SRC
	case 0x06:		// Enable USB PD
		EnableUSBPD();
		break;
	case 0x07:		// Disable USB PD
		DisableUSBPD();
		break;
	case 0x08:		// Send USB PD Command/Message
		SendUSBPDMessage((FSC_U8 *) & MsgBuffer[5]);
		break;
#ifdef FSC_HAVE_SRC
	case 0x09:		// Update the source capabilities
		WriteSourceCapabilities((FSC_U8 *) & MsgBuffer[5]);
		break;
	case 0x0A:		// Read the source capabilities
		retBuffer[4] = MsgBuffer[4];	// Echo back the actual command for verification
		ReadSourceCapabilities((FSC_U8 *) & retBuffer[5]);
		break;
#endif // FSC_HAVE_SRC
#ifdef FSC_HAVE_SNK
	case 0x0B:		// Update the sink capabilities
		WriteSinkCapabilities((FSC_U8 *) & MsgBuffer[5]);
		break;
	case 0x0C:		// Read the sink capabilities
		retBuffer[4] = MsgBuffer[4];	// Echo back the actual command for verification
		ReadSinkCapabilities((FSC_U8 *) & retBuffer[5]);
		break;
	case 0x0D:		// Update the default sink settings
		WriteSinkRequestSettings((FSC_U8 *) & MsgBuffer[5]);
		break;
	case 0x0E:		// Read the default sink settings
		retBuffer[4] = MsgBuffer[4];	// Echo back the actual command for verification
		ReadSinkRequestSettings((FSC_U8 *) & retBuffer[5]);
		break;
#endif // FSC_HAVE_SNK
	case 0x0F:		// Send USB PD Hard Reset
		retBuffer[4] = MsgBuffer[4];	// Echo back the actual command for verification
		SendUSBPDHardReset();	// Send a USB PD Hard Reset
		break;

#ifdef FSC_HAVE_VDM
	case 0x10:
		retBuffer[4] = MsgBuffer[4];	// Echo back the actual command for verification
		ConfigureVdmResponses((FSC_U8 *) & MsgBuffer[5]);	// Configure SVIDs/Modes
		break;
	case 0x11:
		retBuffer[4] = MsgBuffer[4];	// Echo back the actual command for verification
		ReadVdmConfiguration((FSC_U8 *) & retBuffer[5]);	// Read SVIDs/Modes
		break;
#endif // FSC_HAVE_VDM

#ifdef FSC_HAVE_DP
	case 0x12:
		retBuffer[4] = MsgBuffer[4];
		WriteDpControls((FSC_U8 *) & MsgBuffer[5]);
		break;
	case 0x13:
		retBuffer[4] = MsgBuffer[4];
		ReadDpControls((FSC_U8 *) & retBuffer[5]);
		break;
	case 0x14:
		retBuffer[4] = MsgBuffer[4];
		ReadDpStatus((FSC_U8 *) & retBuffer[5]);
		break;
#endif // FSC_HAVE_DP

	default:
		break;
	}
}

void ProcessLocalRegisterRequest(FSC_U8 * MsgBuffer, FSC_U8 * retBuffer)	// Send local register values
{
	if (MsgBuffer[1] != 0) {
		retBuffer[1] = 0x01;	// Return that the version is not recognized
		return;
	}

	GetLocalRegisters(&retBuffer[3], 23);	// Writes local registers to send buffer [3] - [25]
}

void ProcessSetTypeCState(FSC_U8 * MsgBuffer, FSC_U8 * retBuffer)	// Set state machine
{
	ConnectionState state = (ConnectionState) MsgBuffer[3];

	if (MsgBuffer[1] != 0) {
		retBuffer[1] = 0x01;	// Return that the version is not recognized
		return;
	}

	switch (state) {
	case (Disabled):
		SetStateDisabled();
		break;
	case (ErrorRecovery):
		SetStateErrorRecovery();
		break;
	case (Unattached):
		SetStateUnattached();
		break;
#ifdef FSC_HAVE_SNK
	case (AttachWaitSink):
		SetStateAttachWaitSink();
		break;
	case (AttachedSink):
		SetStateAttachedSink();
		break;
#ifdef FSC_HAVE_DRP
	case (TryWaitSink):
		SetStateTryWaitSink();
		break;
	case (TrySink):
		SetStateTrySink();
		break;
#endif // FSC_HAVE_DRP
#endif // FSC_HAVE_SNK
#ifdef FSC_HAVE_SRC
	case (AttachWaitSource):
		SetStateAttachWaitSource();
		break;
	case (AttachedSource):
		SetStateAttachedSource();
		break;
#ifdef FSC_HAVE_DRP
	case (TrySource):
		SetStateTrySource();
		break;
	case (TryWaitSource):
		SetStateTryWaitSource();
		break;
#endif // FSC_HAVE_DRP
	case (UnattachedSource):
		SetStateUnattachedSource();
		break;
#endif // FSC_HAVE_SRC
#ifdef FSC_HAVE_ACCMODE
	case (AudioAccessory):
		SetStateAudioAccessory();
		break;
	case (DebugAccessory):
		SetStateDebugAccessory();
		break;
	case (AttachWaitAccessory):
		SetStateAttachWaitAccessory();
		break;
	case (PoweredAccessory):
		SetStatePoweredAccessory();
		break;
	case (UnsupportedAccessory):
		SetStateUnsupportedAccessory();
		break;
#endif // FSC_HAVE_ACCMODE
	case (DelayUnattached):
		SetStateDelayUnattached();
		break;
	default:
		SetStateDelayUnattached();
		break;
	}
}

/*
 * Buffer[3] = # of log entries(0-12)
 * Buffer[4+] = entries
 * Format (5 byte):
 * [command][s(h)][s(l)][tms(h)][tms(l)]
*/
void ProcessReadTypeCStateLog(FSC_U8 * MsgBuffer, FSC_U8 * retBuffer)
{
	if (MsgBuffer[1] != 0) {
		retBuffer[1] = 0x01;	// Return that the version is not recognized
		return;
	}

	GetStateLog(&retBuffer[3]);	// Designed for 64 byte buffer
}

void setAlternateModes(FSC_U8 mode)
{
	alternateModes = mode;
}

FSC_U8 getAlternateModes(void)
{
	return alternateModes;
}

#endif // FSC_DEBUG
