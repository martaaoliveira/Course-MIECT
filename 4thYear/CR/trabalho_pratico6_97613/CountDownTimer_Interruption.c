/******************************************************************************
*
* Copyright (C) 2009 - 2014 Xilinx, Inc.  All rights reserved.
*
* Permission is hereby granted, free of charge, to any person obtaining a copy
* of this software and associated documentation files (the "Software"), to deal
* in the Software without restriction, including without limitation the rights
* to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
* copies of the Software, and to permit persons to whom the Software is
* furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* Use of the Software is limited solely to applications:
* (a) running on a Xilinx device, or
* (b) that interact with a Xilinx device through a bus or interconnect.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
* XILINX  BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
* WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF
* OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
* SOFTWARE.
*
* Except as contained in this notice, the name of the Xilinx shall not be used
* in advertising or otherwise to promote the sale, use or other dealings in
* this Software without prior written authorization from Xilinx.
*
******************************************************************************/

/*
 * helloworld.c: simple test application
 *
 * This application configures UART 16550 to baud rate 9600.
 * PS7 UART (Zynq) is not initialized by this application, since
 * bootrom/bsp configures it to baud rate 115200
 *
 * ------------------------------------------------
 * | UART TYPE   BAUD RATE                        |
 * ------------------------------------------------
 *   uartns550   9600
 *   uartlite    Configurable only in HW design
 *   ps7_uart    115200 (configured by bootrom/bsp)
 */

#include <stdio.h>
#include "xil_printf.h"
#include "xstatus.h"
#include "platform.h"
#include "xparameters.h" // definitions of processor, masks, peripherals, memory, GPIOs ...
#include "xintc_l.h"
#include "xil_exception.h"
#include "xgpio_l.h"
#include "xtmrctr_l.h"

#include "stdbool.h" ///C99

/******************* Macros for conditional compilation **********************/
//experimentar com os dois timeres - descomentar esta linh
//#define __USE_AXI_HW_TIMER__ 	// define if AXI timer is used for timebase
								// do not define (comment) if FIT timer is used instead

/************************** Constant Definitions *****************************/

// The following constants map to the XPAR parameters created in the
// xparameters.h file. They are defined here such that shorter names can be
// used in the code below. IMPORTANT NOTE: they are dependent on the
// conditional compilation based on the concrete hardware timer used.

#define INTC_BASEADDR			XPAR_INTC_0_BASEADDR	//base address of interrupt controller
#define INTC_DEVICE_ID			XPAR_INTC_0_DEVICE_ID	//ID of interrupt controller

#ifdef __USE_AXI_HW_TIMER__				// if AXI hardware timer is used
	#define HARDWARE_TIMER_INT_ID		XPAR_MICROBLAZE_0_AXI_INTC_AXI_TIMER_0_INTERRUPT_INTR	//ID
	#define HARDWARE_TIMER_INT_MASK		XPAR_AXI_TIMER_0_INTERRUPT_MASK							//mask
#else									// if FIT hardware timer is used
	#define HARDWARE_TIMER_INT_ID		XPAR_MICROBLAZE_0_AXI_INTC_FIT_TIMER_0_INTERRUPT_INTR
	#define HARDWARE_TIMER_INT_MASK		XPAR_FIT_TIMER_0_INTERRUPT_MASK
#endif

#define BUTTONS_INT_ID			XPAR_MICROBLAZE_0_AXI_INTC_AXI_GPIO_BUTTONS_IP2INTC_IRPT_INTR
#define BUTTONS_INT_MASK		XPAR_AXI_GPIO_BUTTONS_IP2INTC_IRPT_MASK

/******************************** Data types *********************************/

// State machine data type
typedef enum {Stopped, Started, SetLSSec, SetMSSec, SetLSMin, SetMSMin} TFSMState;

// Buttons GPIO masks
#define BUTTON_UP_MASK		0x01
#define BUTTON_DOWN_MASK	0x04
#define BUTTON_RIGHT_MASK	0x08
#define BUTTON_CENTER_MASK	0x10

// Data structure to store buttons status
typedef struct SButtonStatus
{
	bool upPressed;
	bool downPressed;
	bool setPressed;
	bool startPressed;

	bool setPrevious;
	bool startPrevious;
} TButtonStatus;

// Data structure to store countdown timer value
typedef struct STimerValue
{
	unsigned int minMSValue;
	unsigned int minLSValue;
	unsigned int secMSValue;
	unsigned int secLSValue;
} TTimerValue;

/********************** Global variables (module scope) **********************/

//sao globais pq precisamos delas em varias funcoes
//static - so pode ser consultado neste ficheiro e nao em mais
static TButtonStatus buttonStatus   = {false, false, false, false, false, false};
static TTimerValue   timerValue     = {5, 9, 5, 9};
static bool          zeroFlag       = false;

/***************************** Helper functions ******************************/

// 7 segment decoder
unsigned char Hex2Seg(unsigned int value, bool dp) //converts a 4-bit number [0..15] to 7-segments; dp is the decimal point
{
	static const char Hex2SegLUT[] = {0x40, 0x79, 0x24, 0x30, 0x19, 0x12, 0x02, 0x78,
									  0x00, 0x10, 0x08, 0x03, 0x46, 0x21, 0x06, 0x0E};
	return dp ? Hex2SegLUT[value] : (0x80 | Hex2SegLUT[value]);
}

// Rising edge detection and clear
bool DetectAndClearRisingEdge(bool* pOldValue, bool newValue)
{
	bool retValue;

	retValue = (!(*pOldValue)) && newValue; //&& - AND lógico as we work with boolean values
	*pOldValue = newValue;
	return retValue;
}

// Modular increment
bool ModularInc(unsigned int* pValue, unsigned int modulo)
{
	if (*pValue < modulo - 1)
	{
		(*pValue)++;
		return false;
	}
	else
	{
		*pValue = 0;
		return true;
	}
}

// Modular decrement
bool ModularDec(unsigned int* pValue, unsigned int modulo)
{
	if (*pValue > 0)
	{
		(*pValue)--;
		return false;
	}
	else
	{
		*pValue = modulo - 1;
		return true;
	}
}

// Tests if all timer digits are zero
bool IsTimerValueZero(const TTimerValue* pTimerValue)
{
	return ((pTimerValue->secLSValue == 0) && (pTimerValue->secMSValue == 0) &&
			(pTimerValue->minLSValue == 0) && (pTimerValue->minMSValue == 0));
}

// Conversion of the countdown timer values stored in a structure to an array of digits
void TimerValue2DigitValues(const TTimerValue* pTimerValue, unsigned int digitValues[8])
{
	digitValues[0] = 0;
	digitValues[1] = 0;
	digitValues[2] = pTimerValue->secLSValue;
	digitValues[3] = pTimerValue->secMSValue;
	digitValues[4] = pTimerValue->minLSValue;
	digitValues[5] = pTimerValue->minMSValue;
	digitValues[6] = 0;
	digitValues[7] = 0;
}

/******************* Countdown timer operations functions ********************/

//all enables come in positive logic, this function has to be invoked at correct frequency (e.g. 800Hz)
void RefreshDisplays(unsigned char digitEnables, const unsigned int digitValues[8], unsigned char decPtEnables)
{
	static unsigned int digitRefreshIdx = 0; // static variable - is preserved across calls

	// Insert your code here...
	///*** STEP 1
	unsigned int an = 0x01;
	an = an << digitRefreshIdx; 	// select the right display to refresh (rotatively)
	an = an & digitEnables;			// check if the selected display is enabled
	bool dp = an & decPtEnables;	// check if the selected dot is enabled
	XGpio_WriteReg(XPAR_AXI_GPIO_DISPLAYS_BASEADDR, XGPIO_DATA_OFFSET, ~an); //an
	XGpio_WriteReg(XPAR_AXI_GPIO_DISPLAYS_BASEADDR, XGPIO_DATA2_OFFSET, Hex2Seg(digitValues[digitRefreshIdx], dp)); //seg

	digitRefreshIdx++;
	digitRefreshIdx &= 0x07; // AND bitwise
}

void ReadButtons(TButtonStatus* pButtonStatus) //to be invoked only on interrupt from a push button
{
	unsigned int buttonsPattern;

	//buttonsPattern = // Insert your code here...
	///*** STEP 2
	buttonsPattern = XGpio_ReadReg(XPAR_AXI_GPIO_BUTTONS_BASEADDR, XGPIO_DATA_OFFSET);

	pButtonStatus->upPressed    = buttonsPattern & BUTTON_UP_MASK;
	pButtonStatus->downPressed  = buttonsPattern & BUTTON_DOWN_MASK;
	pButtonStatus->setPressed   = buttonsPattern & BUTTON_RIGHT_MASK;
	pButtonStatus->startPressed = buttonsPattern & BUTTON_CENTER_MASK;
}

void UpdateStateMachine(TFSMState* pFSMState, TButtonStatus* pButtonStatus,
						bool zeroFlag, unsigned char* pSetFlags)
{
	// Insert your code here...
	///*** STEP 4
	switch(*pFSMState)
		{
			case Stopped:
				*pSetFlags = 0x00;
				if(DetectAndClearRisingEdge(&(pButtonStatus->startPrevious), pButtonStatus->startPressed) && zeroFlag==0){ // se se pressionar o botao btnC
					*pFSMState = Started;
				}else if (DetectAndClearRisingEdge(&(pButtonStatus->setPrevious), pButtonStatus->setPressed)){ //se se pressionar o botao btnR
					*pFSMState = SetMSMin;
				}else{
					*pFSMState=Stopped;
				}
				break;

			case Started:
				*pSetFlags = 0x00;
				if(DetectAndClearRisingEdge(&(pButtonStatus->startPrevious), pButtonStatus->startPressed) || zeroFlag==1){ // se se pressionar o botao btnC
					*pFSMState = Stopped;
				}else if (DetectAndClearRisingEdge(&(pButtonStatus->setPrevious), pButtonStatus->setPressed)){ //se se pressionar o botao btnR
					*pFSMState = SetMSMin;
				}else{
					*pFSMState=Started;
				}
				break;

			case SetMSMin: // DM
				*pSetFlags = 0x08;
				if (DetectAndClearRisingEdge(&(pButtonStatus->setPrevious), pButtonStatus->setPressed)){ //se se pressionar o botao btnR
					*pFSMState = SetLSMin;
				}else{
					*pFSMState=SetMSMin;
				}
				break;

			case SetLSMin: // UM
				*pSetFlags = 0x04;
				if (DetectAndClearRisingEdge(&(pButtonStatus->setPrevious), pButtonStatus->setPressed)){ //se se pressionar o botao btnR
					*pFSMState = SetMSSec;
				}else{
					*pFSMState=SetLSMin;
				}
				break;
			case SetMSSec: //DS
				*pSetFlags = 0x02;
				if (DetectAndClearRisingEdge(&(pButtonStatus->setPrevious), pButtonStatus->setPressed)){ //se se pressionar o botao btnR
					*pFSMState = SetLSSec;
				}else{
					*pFSMState=SetMSSec;
				}
				break;
			case SetLSSec: //US
				*pSetFlags = 0x01;
				if (DetectAndClearRisingEdge(&(pButtonStatus->setPrevious), pButtonStatus->setPressed)){ //se se pressionar o botao btnR
					*pFSMState = Stopped;
				}else{
					*pFSMState=SetLSSec;
				}
				break;
		}
}

void SetCountDownTimer(TFSMState fsmState, const TButtonStatus* pButtonStatus,
					   TTimerValue* pTimerValue)
{
	// Insert your code here...
	///*** STEP 5
	switch(fsmState)
		{
			case Stopped:
				break;
			case Started:
				break;
			case SetMSMin: //DM
				if(pButtonStatus->upPressed){
					ModularInc(&pTimerValue->minMSValue, 6);
				}else if(pButtonStatus->downPressed){
					ModularDec(&pTimerValue->minMSValue, 6);
				}
				break;
			case SetLSMin: //UM
				if(pButtonStatus->upPressed){
					ModularInc(&pTimerValue->minLSValue, 10);
				}else if(pButtonStatus->downPressed){
					ModularDec(&pTimerValue->minLSValue, 10);
				}
				break;
			case SetMSSec: //DS
				if(pButtonStatus->upPressed){
					ModularInc(&pTimerValue->secMSValue, 6);
				}else if(pButtonStatus->downPressed){
					ModularDec(&pTimerValue->secMSValue, 6);
				}
				break;
			case SetLSSec: //US
				if(pButtonStatus->upPressed){
					ModularInc(&pTimerValue->secLSValue, 10);
				}else if(pButtonStatus->downPressed){
					ModularDec(&pTimerValue->secLSValue, 10);
				}
				break;
		}
}

void DecCountDownTimer(TFSMState fsmState, TTimerValue* pTimerValue)
{
	// Insert your code here...
	///*** STEP 3
	// so se decrementa o timer normalmente se o estado for o started
	if(fsmState == Started){
		bool us_zero = ModularDec(&pTimerValue->secLSValue, 10); // o digito das us chegou a 0?
		if(us_zero){
			bool ds_zero = ModularDec(&pTimerValue->secMSValue, 6); // o digito das ds chegou a 0?
			if(ds_zero){
				bool um_zero = ModularDec(&pTimerValue->minLSValue, 10); // o digito das um chegou a 0?
				if(um_zero){
					ModularDec(&pTimerValue->minMSValue, 6); // o digito das dm chegou a 0?
				}
			}
		}
	}
}

/************************ Interrupt callback functions ***********************/

/* These functions are designed to look like an interrupt handler in a device
   driver. These are typically 2nd level handlers that are called from the
   interrupt controller interrupt handler. These handlers would typically
   perform device specific processing such as reading and writing the
   registers of the device to transfer data and to clear the interrupt
   condition (in the device) and pass any data to an application.

  @param	callbackParam is passed back to the device driver's interrupt
 		handler by the XIntc driver.  It was given to the XIntc driver
 		in the XIntc_Connect() function call. It is typically a pointer
 		to the device driver instance variable if using the Xilinx Level
 		1 device drivers. In this example, we do not care about the
 		callback reference, so we passed it a 0 when connecting the
 		handler to the XIntc driver and we make no use of it here.

  @return	None.

  @note		None.

IMPORTANT NOTE: time consuming operations must not be executed here!!!
 	 	 	 	ISR and interrupt callbacks must perform the strictly
 	 	 	 	necessary operations and return as quick as possible.
 	 	 	 	Leave time consuming operations for the main function
 	 	 	 	(or functions invoked in the context of the application -
 	 	 	 	 not in the ISR or device driver context)
 	 	 	 	 ***For example, do not use prints to terminal especially if baud rate is low

******************************************************************************/
// as instrucoes lentas (prints,etc) nao devem ser executados aqui
// This function will be called back by the INTC ISR at every timer IRQ
// é invocada sempre que uns dos timeres de uma volta completa a frequencia de 800hz
// faz a mesma coisa que a nossa antiga main fazia (polling)
//FAZER----------------------------
void TimerIntCallbackHandler(void* callbackParam)
{
	// Timer event software counter
	static unsigned hwTmrEventCount = 0;
    hwTmrEventCount++;

    // Countdown timer status variables (static variables) - serve para preservar os conteudos das variaveis entre as diferentes invocacoes da funcao
    static TFSMState     fsmState       = Stopped;
    static unsigned char setFlags       = 0x0;

    static unsigned char digitEnables   = 0x3C;
    static unsigned int  digitValues[8] = {0, 0, 9, 5, 9, 5, 0, 0};
    static unsigned char decPtEnables   = 0x00;

    static bool          blink1HzStat   = false;
    static bool          blink2HzStat   = false;


	// Put here operations that must be performed at 800Hz rate
	// Refresh displays
    ///***

    RefreshDisplays(digitEnables, digitValues, decPtEnables);

	if (hwTmrEventCount % 100 == 0) // 8Hz
	{
		// Put here operations that must be performed at 8Hz rate
		// Update state machine
		UpdateStateMachine(&fsmState, &buttonStatus, zeroFlag, &setFlags);

		if ((setFlags == 0x0) || (blink2HzStat))
		{
			digitEnables = 0x3C; // All digits active
		}
		else
		{
			digitEnables = (~(setFlags << 2)) & 0x3C; // Setting digit inactive
		}


		if (hwTmrEventCount % 200 == 0) // 4Hz
		{
			// Put here operations that must be performed at 4Hz rate
			// Switch digit set 2Hz blink status
			blink2HzStat = !blink2HzStat;


			if (hwTmrEventCount % 400 == 0) // 2Hz
			{
				// Put here operations that must be performed at 2Hz rate
				// Switch point 1Hz blink status

				blink1HzStat = !blink1HzStat;
				if (fsmState == Started)
					decPtEnables = (blink1HzStat ? 0x00 : 0x10);

				// Digit set increment/decrement
				//timerValue - structure holding the current countdown timer value
				SetCountDownTimer(fsmState, &buttonStatus, &timerValue);


				if (hwTmrEventCount == 800) // 1Hz
				{
					// Put here operations that must be performed at 1Hz rate
					// Count down timer normal operation
					DecCountDownTimer(fsmState, &timerValue);
					// Reset hwTmrEventCount every second
					hwTmrEventCount = 0;
					///***
				}
			}
		}
	}

    zeroFlag = IsTimerValueZero(&timerValue);
    TimerValue2DigitValues(&timerValue, digitValues);

//tbm devemos limpar o bit de interrupcao se __USE_AXI_HW_TIMER__ como nos botoes
	//o puldso do fixed Interval Time é gerado automaticamente
#ifdef __USE_AXI_HW_TIMER__		// if AXI hardware timer is used
	// Clear hardware timer event (interrupt request flag)
	unsigned int tmrCtrlStatReg = XTmrCtr_GetControlStatusReg(XPAR_AXI_TIMER_0_BASEADDR, 0);
	XTmrCtr_SetControlStatusReg(XPAR_AXI_TIMER_0_BASEADDR, 0, tmrCtrlStatReg | XTC_CSR_INT_OCCURED_MASK);
#endif
}

// This function will be called back by the INTC ISR whenever a button is pressed or released
//FAZER---------------------------------------------------
void ButtonsIntCallbackHandler(void* callbackParam) //parameter is not used
{
	// Read push buttons
	// Insert your code here...
	ReadButtons(&buttonStatus);
	//ler o estado atual dos botoes e limpar a flag de GPIO das instrucoes pq se nao nunca mais vai ser gerado interrupcoes dos botoes
	// le o rising_edge do sinal de interrupcao entao temos de limpar se nao nunca mais acontece

	// Clear GPIO interrupt request flag
	XGpio_WriteReg(XPAR_AXI_GPIO_BUTTONS_BASEADDR, XGPIO_ISR_OFFSET, XGPIO_IR_CH1_MASK); //if not cleared, no more interrupts from this source will be generated
}

/************************* Interrupt Setup function **************************/

//interrupcoes dos timeres e dos botoes
//indica quando é para se fazer um interrupcao - timeres ou botoes neste caso
int SetupInterrupts(unsigned int intcBaseAddress) //esta feita - nao e preciso mexer - implementar a TimerIntCallbackHandler e a ButtonsIntCallbackHandler
{
	//preparar as interrupcoes dos timeres e botoes

	// Connect a callback handler that will be called by the ISR when
	// an interrupt for the timer occurs,
	// to perform the specific interrupt processing for that device
	//inturrupcoes do timer
	XIntc_RegisterHandler(intcBaseAddress, HARDWARE_TIMER_INT_ID,
						  (XInterruptHandler)TimerIntCallbackHandler, (void *)0);

#ifdef __USE_AXI_HW_TIMER__		// if AXI hardware timer is used
	// Enable interrupt requests at the AXI timer
	unsigned int tmrCtrlStatReg = XTmrCtr_GetControlStatusReg(XPAR_AXI_TIMER_0_BASEADDR, 0);
	XTmrCtr_SetControlStatusReg(XPAR_AXI_TIMER_0_BASEADDR, 0, tmrCtrlStatReg | XTC_CSR_ENABLE_INT_MASK);
#endif

	// Connect a callback handler that will be called by the ISR when
	// an interrupt for the buttons GPIO occurs,
	// to perform the specific interrupt processing for that device
	//interrupcoes dos botoes
	XIntc_RegisterHandler(intcBaseAddress, BUTTONS_INT_ID,
						  (XInterruptHandler)ButtonsIntCallbackHandler, (void *)0);

	//a funcao XIntc_RegisterHandler permite selecionar quais as interrupcoes a ser usadas - vai reagir a estas interrupcoes

	// Enable interrupt requests at the buttons GPIO
	XGpio_WriteReg(XPAR_AXI_GPIO_BUTTONS_BASEADDR, XGPIO_IER_OFFSET, XGPIO_IR_CH1_MASK);
	XGpio_WriteReg(XPAR_AXI_GPIO_BUTTONS_BASEADDR, XGPIO_GIE_OFFSET, XGPIO_GIE_GINTR_ENABLE_MASK);



	//controlador das interrupcoes - enable e excecoes
	// Enable interrupts for all the peripheral devices that cause interrupts,
	XIntc_EnableIntr(intcBaseAddress, HARDWARE_TIMER_INT_MASK | BUTTONS_INT_MASK);

	// Set the hardware and the master interrupt enable bits at the INTC
	XIntc_Out32(intcBaseAddress + XIN_MER_OFFSET, XIN_INT_HARDWARE_ENABLE_MASK |
												  XIN_INT_MASTER_ENABLE_MASK);

	// This step is processor specific, connect the handler for the
	// interrupt controller to the interrupt source for the processor
	// Este passo é específico do processador, liga o manipulador para o
	// controlador de interrupção para a fonte de interrupção do processador
	Xil_ExceptionInit();

	// Register the interrupt controller handler with the exception table
	Xil_ExceptionRegisterHandler(XIL_EXCEPTION_ID_INT,
								 (Xil_ExceptionHandler)XIntc_DeviceInterruptHandler,
								 INTC_DEVICE_ID);

	// Enable exceptions
	Xil_ExceptionEnable();

	return XST_SUCCESS;
}

/******************************* Main function *******************************/

int main()
{
	int status;

	init_platform();

	xil_printf("\n\n\rCount down timer - interrupt based version.\n\rConfiguring...");

    //	GPIO tri-state configuration
    //	Inputs
    XGpio_WriteReg(XPAR_AXI_GPIO_SWITCHES_BASEADDR, XGPIO_TRI_OFFSET,  0xFFFFFFFF);
    XGpio_WriteReg(XPAR_AXI_GPIO_BUTTONS_BASEADDR,  XGPIO_TRI_OFFSET,  0xFFFFFFFF);

    //	Outputs
    XGpio_WriteReg(XPAR_AXI_GPIO_LEDS_BASEADDR,     XGPIO_TRI_OFFSET,  0xFFFF0000);
    XGpio_WriteReg(XPAR_AXI_GPIO_DISPLAYS_BASEADDR,  XGPIO_TRI_OFFSET,  0xFFFFFF00);
    XGpio_WriteReg(XPAR_AXI_GPIO_DISPLAYS_BASEADDR,  XGPIO_TRI2_OFFSET, 0xFFFFFF00);

    xil_printf("\n\rIOs configured.");

    /*************TIMER CONFIGURATION******************/
    // Only program AXI timer registers if this timer is used
    //o que comeca com o # é diretivas do processador
    //se a __USE_AXI_HW_TIMER__ é usado este codigo e compilado se nao nao é
    // ifdef = compilacao condicional
#ifdef __USE_AXI_HW_TIMER__		// if AXI hardware timer is used
    // Disable hardware timer
	XTmrCtr_SetControlStatusReg(XPAR_AXI_TIMER_0_BASEADDR, 0, 0x00000000);
	// Set hardware timer load value
	XTmrCtr_SetLoadReg(XPAR_AXI_TIMER_0_BASEADDR, 0, 125000); // Counter will wrap around every 1.25 ms
	XTmrCtr_SetControlStatusReg(XPAR_AXI_TIMER_0_BASEADDR, 0, XTC_CSR_LOAD_MASK);
	// Enable hardware timer, down counting with auto reload
	XTmrCtr_SetControlStatusReg(XPAR_AXI_TIMER_0_BASEADDR, 0, XTC_CSR_ENABLE_TMR_MASK  |
															  XTC_CSR_AUTO_RELOAD_MASK |
															  XTC_CSR_DOWN_COUNT_MASK);
	xil_printf("\n\rAXI timer configured.");
 // otherwise the fixed interval timer is configures statically in Vivado
#else						// if FIT hardware timer is used
	xil_printf("\n\rUsing FIT timer.");
#endif

	// Setup interrupts, pass the INTC Base Address specified in xparameters.h
	status = SetupInterrupts(INTC_BASEADDR);
	if (status != XST_SUCCESS)
	{
		xil_printf("\n\rInterrupts setup failed\r\nExiting...");
		cleanup_platform();
		return XST_FAILURE;
	}
	xil_printf("\n\rInterrupts setup successful.");

	xil_printf("\n\rSystem running.\n\r");

	//Reset the count down timer on CPU reset
	timerValue.secLSValue = 9;
	timerValue.secMSValue = 5;
	timerValue.minLSValue = 9;
	timerValue.minMSValue = 5;

	//arrancamos o processo
	while (1)
	{

		// Put here operations that are performed whenever possible

		//colocar o led ligado se o contador for 00.00
		if (zeroFlag){
			XGpio_WriteReg(XPAR_AXI_GPIO_LEDS_BASEADDR, XGPIO_DATA_OFFSET, 0x0001);
		}else{
			XGpio_WriteReg(XPAR_AXI_GPIO_LEDS_BASEADDR, XGPIO_DATA_OFFSET, 0x0000);
		}
		//Demonstration purposes
		xil_printf("\r%d%d:%d%d", timerValue.minMSValue, timerValue.minLSValue, timerValue.secMSValue, timerValue.secLSValue);

	}

    cleanup_platform();
    return XST_SUCCESS;
}