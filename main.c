/******************************************************************************
*
* MSP432 BOLT IV MCU PROGRAM
*
* Authors:
* William Campbell
* Ethan Brooks
* Patrick Graybeal
* Logan Richardson
*
******************************************************************************
*
* This file is the empty main for the BOLT MCU. See can_example.c for details
* when implementing CAN functionality.
*
******************************************************************************
* Potentially useful functions:
* GPIOPinTypeADC(uint32_t ui32Port, uint8_t ui8Pins)
* GPIOPinTypeCAN(uint32_t ui32Port, uint8_t ui8Pins)
* GPIOPinTypeGPIOInput(uint32_t ui32Port, uint8_t ui8Pins)
* GPIOPinTypeGPIOOutput(uint32_t ui32Port, uint8_t ui8Pins)
*
******************************************************************************
* Notes:
* Must include "MAP_" before driverlib function calls, I don't know why, but
*   otherwise it doesn't actually see the function and doesn't compile.
*
* So that UART2 is connected to the XDS110 and CAN0 is enabled to RX/TX,
*   the jumpers on the MCU must be horizontal (see silkscreen graphic)
*
******************************************************************************/

#include "msp.h"

/* Standard driverlib include - can be more specific if needed */
#include <globals.h>
#include <ti/devices/msp432e4/driverlib/driverlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "uartstdio.h"

#include "can_comms.h"
#include "uart_comms.h"
#include "bike_gpio.h"
#include "bike_adc.h"

#define IGNIT_CUTOFF_DELAY	70
#define IMU_RECEIVE_BAUD 57600
#define XBEE_BAUD_RATE 57600

// IMU data struct
typedef struct {
	uint8_t xAcc[imuLength];
	uint8_t yAcc[imuLength];
	uint8_t zAcc[imuLength];
	uint8_t xGyro[imuLength];
	uint8_t yGyro[imuLength];
	uint8_t zGyro[imuLength];
	uint8_t pitch[imuLength];
	uint8_t roll[imuLength];
	uint8_t yaw[imuLength];
	uint8_t compass[imuLength];
} IMUTransmitData_t;

// Instantiate IMU object
IMUTransmitData_t gIMUData;

// IMU receive buffer
char gIMUReceiveBuf[imuLength];
uint32_t gIMUBufIndex = 0;

// Pump Voltage
uint8_t pumpVoltage[voltageLength];

// Global variable to count milliseconds
uint8_t msCount = 0;

// ISR Flags
uint8_t g_ui8xbeeFlag = 0;
uint32_t g_ui32Flags;

/* Temp */
uint8_t CANCount = 0;
uint8_t g_ui8canFlag = 0;


typedef enum states { PCB, ACC, IGN, MAX_STATES } states_t;

// Required second count to delay switching off ACC and IGN if voltage dips below for a brief second.
// We want to calculate the # of cycles to delay x seconds from the formula below:
// x seconds delay = (# of Cycles) * (1/frequency)
// So plug in the second count that you want delayed into the formula below:
// (120MHz)*(x second delay) = # of cycles
// The current value is for a 3 second delay: (120MHz)*(3s)=360000000
#define REQSECCOUNT 360000000

// Function prototypes
void timerSetup();
void timerRun();

void initTimers(uint32_t);
void TIMER1A_IRQHandler();

void xbeeTransmit(CANTransmitData_t, IMUTransmitData_t, uint8_t*, uint8_t*);
void imuParse(char c);

int main(void)
{
    /* Configure system clock for 120 MHz */
    systemClock = MAP_SysCtlClockFreqSet((SYSCTL_XTAL_25MHZ | SYSCTL_OSC_MAIN |
                                          SYSCTL_USE_PLL | SYSCTL_CFG_VCO_480),
                                          120000000);
    // Set up the timer system
    timerSetup();
    initTimers(systemClock);

    // Instantiate CAN objects
    static CANTransmitData_t CANData;
    tCANMsgObject sCANMessage;
    uint8_t msgDataIndex;
    uint8_t msgData[8] = {0x00, 0x00, 0x00, 0x00,
                          0x00, 0x00, 0x00, 0x00};

    // Initialize IMU object
    memset(gIMUData.xAcc, '0', imuLength);
    memset(gIMUData.yAcc, '0', imuLength);
    memset(gIMUData.zAcc, '0', imuLength);
    memset(gIMUData.xGyro, '0', imuLength);
    memset(gIMUData.yGyro, '0', imuLength);
    memset(gIMUData.zGyro, '0', imuLength);
    memset(gIMUData.roll, '0', imuLength);
    memset(gIMUData.pitch, '0', imuLength);
    memset(gIMUData.yaw, '0', imuLength);
    memset(gIMUData.compass, '0', imuLength);


    memset(auxVoltage, '0', sizeof(auxVoltage));
    uint32_t auxBatVoltage[1];
    uint32_t auxBatAdjusted; //no decimal, accurate value

    while(!(MAP_SysCtlPeripheralReady(SYSCTL_PERIPH_ADC0)))

    ADCSetup();
    UART7Setup();
    UART6Setup();
    accIgnDESetup();
    configureCAN();
    canSetup(&sCANMessage);

    // Enable interrupts globally
    MAP_IntMasterEnable();

    enableUARTprintf();
    //UARTprintf("UARTprintf enabled\n");

    states_t present = PCB;

#ifdef XBEE_PLACEHOLDER_DATA
	strncpy((char* )CANData.SOC, "50", sizeof(CANData.SOC));
	strncpy((char* )CANData.FPV, "300", sizeof(CANData.FPV));
	strncpy((char* )CANData.highTemp, "80", sizeof(CANData.highTemp));
	strncpy((char* )CANData.lowTemp, "70", sizeof(CANData.lowTemp));
	strncpy((char* )CANData.highVoltage, "6.8", sizeof(CANData.highVoltage));
	strncpy((char* )CANData.lowVoltage, "6.4", sizeof(CANData.lowVoltage));
	strncpy((char* )IMUData.xAcc, "1.5", sizeof(IMUData.xAcc));
	strncpy((char* )IMUData.yAcc, "1.2", sizeof(IMUData.yAcc));
	strncpy((char* )IMUData.zAcc, "1", sizeof(IMUData.zAcc));
	strncpy((char* )IMUData.xGyro, "20", sizeof(IMUData.xGyro));
	strncpy((char* )IMUData.yGyro, "3", sizeof(IMUData.yGyro));
	strncpy((char* )IMUData.zGyro, "0", sizeof(IMUData.zGyro));
	strncpy((char* )pumpVoltage, "12", sizeof(pumpVoltage));
	strncpy((char* )auxVoltage, "16", sizeof(auxVoltage));
#endif

	//UARTprintf("Entering loop");
    // Loop forever.
    while (1)
    {

    	if (g_ui8xbeeFlag) {
    		xbeeTransmit(CANData, gIMUData, pumpVoltage, auxVoltage);
    		g_ui8xbeeFlag = 0;
    	}

        // As long as the PCB is on, CAN should be read
    	if (g_ui8canFlag) {
    	    canReceive(&sCANMessage, &CANData, msgDataIndex, msgData);
    	    g_ui8canFlag = 0;
    	    //UARTprintf("Compare value: %i\n", auxADCSend(auxBatVoltage));
            //UARTprintf("Pump: %i", pumpADCSend(pumpVoltage));
    	}

        switch(present)
        {

        /* ACC state of FSM */
        case ACC:

            // Output HIGH to ACC Relay
            MAP_GPIOPinWrite(GPIO_PORTK_BASE, GPIO_PIN_4, GPIO_PIN_4);
            // Output HIGH to ACC Dash
            MAP_GPIOPinWrite(GPIO_PORTK_BASE, GPIO_PIN_6, GPIO_PIN_6);

            // Output LOW to IGN Relay
            MAP_GPIOPinWrite(GPIO_PORTH_BASE, GPIO_PIN_0, ~GPIO_PIN_0);
            // Output LOW to IGN Dash
            MAP_GPIOPinWrite(GPIO_PORTK_BASE, GPIO_PIN_7, ~GPIO_PIN_7);

            // Output HIGH to PSI LED
            MAP_GPIOPinWrite(GPIO_PORTP_BASE, GPIO_PIN_2, GPIO_PIN_2);
            // Output HIGH to PSI Dash
            MAP_GPIOPinWrite(GPIO_PORTM_BASE, GPIO_PIN_2, GPIO_PIN_2);


            // Aux battery voltage stored as volts * 1000
            auxBatAdjusted = auxADCSend(auxBatVoltage);

            //UARTprintf("Aux Bat: %i", auxBatAdjusted);
            //UARTprintf("Pump: %i", pumpADCSend(pumpVoltage));

            if (auxBatAdjusted <= 1200) {

                //  Wait 3 seconds to check value, ensure constant value
                timerRun();

                auxBatAdjusted = auxADCSend(auxBatVoltage);
                if (auxBatAdjusted <= 1200) {
                    present = PCB;
                }

            } else if (!accPoll()) {

                present = PCB;

            } else if (ignitPoll()){

                present = IGN;
            }
            break;

        /* IGN state of FSM */
        case IGN:

            if (!DEPoll()) {

                // Output HIGH to ACC Relay
                MAP_GPIOPinWrite(GPIO_PORTK_BASE, GPIO_PIN_4, GPIO_PIN_4);
                // Output HIGH to ACC Dash
                MAP_GPIOPinWrite(GPIO_PORTK_BASE, GPIO_PIN_6, GPIO_PIN_6);

                // Output HIGH to IGN Relay
                MAP_GPIOPinWrite(GPIO_PORTH_BASE, GPIO_PIN_0, GPIO_PIN_0);
                // Output HIGH to IGN Dash
                MAP_GPIOPinWrite(GPIO_PORTK_BASE, GPIO_PIN_7, GPIO_PIN_7);

                // Output HIGH to PSI LED
                MAP_GPIOPinWrite(GPIO_PORTP_BASE, GPIO_PIN_2, GPIO_PIN_2);
                // Output HIGH to PSI LED
                MAP_GPIOPinWrite(GPIO_PORTM_BASE, GPIO_PIN_2, GPIO_PIN_2);


                auxBatAdjusted = auxADCSend(auxBatVoltage);

                //UARTprintf("Pump: %i", pumpADCSend(pumpVoltage));

                if (auxBatAdjusted <= 1200) {

                //  Wait 3 seconds to check value, ensure constant value
                    timerRun();

                    auxBatAdjusted = auxADCSend(auxBatVoltage);
                    if (auxBatAdjusted <= 1200) {
                        present = ACC;
                    }

                //} else if (/*Low pump current*/) {

                    //present = ACC;

                } else if (!ignitPoll()) {

                    present = ACC;

                } else if (!accPoll()) {

                    present = ACC;

                }

            } else {

                present = ACC;
            }
            break;

        /* PCB state of FSM */
        default:

            // Output LOW to ACC Relay
            MAP_GPIOPinWrite(GPIO_PORTK_BASE, GPIO_PIN_4, ~GPIO_PIN_4);
            // Output LOW to ACC Dash
            MAP_GPIOPinWrite(GPIO_PORTK_BASE, GPIO_PIN_6, ~GPIO_PIN_6);

            // Output LOW to IGN Relay
            MAP_GPIOPinWrite(GPIO_PORTH_BASE, GPIO_PIN_0, ~GPIO_PIN_0);
            // Output LOW to IGN Dash
            MAP_GPIOPinWrite(GPIO_PORTK_BASE, GPIO_PIN_7, ~GPIO_PIN_7);

            // Output LOW to PSI LED
            MAP_GPIOPinWrite(GPIO_PORTP_BASE, GPIO_PIN_2, ~GPIO_PIN_2);
            //Output LOW to PSI Dash
            MAP_GPIOPinWrite(GPIO_PORTM_BASE, GPIO_PIN_2, ~GPIO_PIN_2);

            if (accPoll()) {
                present = ACC;
            }
            break;

        }
    }
}

void initTimers(uint32_t sysClock)
{
    // Enable the peripherals used by this example.
    MAP_SysCtlPeripheralEnable(SYSCTL_PERIPH_TIMER1);

    // Configure timer 1
    MAP_TimerConfigure(TIMER1_BASE, TIMER_CFG_PERIODIC);
    MAP_TimerLoadSet(TIMER1_BASE, TIMER_A, sysClock / 1000);

    // Setup the interrupts for the timer timeouts.
    MAP_IntEnable(INT_TIMER1A);
    MAP_TimerIntEnable(TIMER1_BASE, TIMER_TIMA_TIMEOUT);

    // Enable the timers.
    MAP_TimerEnable(TIMER1_BASE, TIMER_A);
}

void timerSetup() {
    // Set the 32-bit timer Peripheral.
    MAP_SysCtlPeripheralEnable(SYSCTL_PERIPH_TIMER0);
    // Configure the timer to be one-shot.
    MAP_TimerConfigure(TIMER0_BASE, TIMER_CFG_ONE_SHOT);
}

void timerRun() {
    // Load the required second count into the timer.
    MAP_TimerLoadSet(TIMER0_BASE, TIMER_A, REQSECCOUNT);

    // Enable the timer.
    MAP_TimerEnable(TIMER0_BASE, TIMER_A);

    // Wait for 3 seconds to have the timer delay the system
    while(MAP_TimerValueGet(TIMER0_BASE, TIMER_A) != REQSECCOUNT) {}
}

void CAN0_IRQHandler(void) // Uses CAN0, on J5
{
    uint32_t canStatus;

    /* Read the CAN interrupt status to find the cause of the interrupt */
    canStatus = MAP_CANIntStatus(CAN0_BASE, CAN_INT_STS_CAUSE);

    /* If the cause is a controller status interrupt, then get the status */
    if(canStatus == CAN_INT_INTID_STATUS)
    {
        /* Read the controller status.  This will return a field of status
         * error bits that can indicate various errors */
        canStatus = MAP_CANStatusGet(CAN0_BASE, CAN_STS_CONTROL);

        /* Set a flag to indicate some errors may have occurred */
        errFlag = true;
/*
        UARTprintf("canStatus: %08X\n", canStatus);

        uint32_t rxErr, txErr;
        MAP_CANErrCntrGet(CAN0_BASE, &rxErr, &txErr);
        UARTprintf("RX Error: %08X\n", rxErr);
        UARTprintf("TX Error: %08X\n", txErr);*/
    }

    /* Check if the cause is message object 1, which what we are using for
     * receiving messages */
    else if(canStatus == 1)
    {
        /* Getting to this point means that the RX interrupt occurred on
         * message object 1, and the message RX is complete.  Clear the
         * message object interrupt */
        MAP_CANIntClear(CAN0_BASE, 1);

        /* Increment a counter to keep track of how many messages have been
         * sent. In a real application this could be used to set flags to
         * indicate when a message is sent */
        msgCount++;

        /* Set flag to indicate received message is pending */
        rxMsg = true;

        /* Since the message was sent, clear any error flags */
        errFlag = false;
    }
    else
    {
    }
}

void xbeeTransmit(CANTransmitData_t CANData, IMUTransmitData_t IMUData, uint8_t* pumpVoltage, uint8_t* auxVoltage)
{
	// Send CAN Data
	UARTSendStr(UART7_BASE, CANData.SOC, sizeof(CANData.SOC));
	UARTSendChar(UART7_BASE, ',');
	UARTSendStr(UART7_BASE, CANData.FPV, sizeof(CANData.FPV));
	UARTSendChar(UART7_BASE, ',');
	UARTSendStr(UART7_BASE, CANData.highTemp, sizeof(CANData.highTemp));
	UARTSendChar(UART7_BASE, ',');
	UARTSendStr(UART7_BASE, CANData.lowTemp, sizeof(CANData.lowTemp));
	UARTSendChar(UART7_BASE, ',');
	UARTSendStr(UART7_BASE, CANData.highVoltage, sizeof(CANData.highVoltage));
	UARTSendChar(UART7_BASE, ',');
	UARTSendStr(UART7_BASE, CANData.lowVoltage, sizeof(CANData.lowVoltage));
	UARTSendChar(UART7_BASE, ',');
	UARTSendStr(UART7_BASE, CANData.RPM, sizeof(CANData.RPM));
	UARTSendChar(UART7_BASE, ',');
	UARTSendStr(UART7_BASE, CANData.motorTemp, sizeof(CANData.motorTemp));
	UARTSendChar(UART7_BASE, ',');
	UARTSendStr(UART7_BASE, CANData.dcBusCurrent, sizeof(CANData.dcBusCurrent));
	UARTSendChar(UART7_BASE, ',');
	UARTSendStr(UART7_BASE, CANData.motorTorque, sizeof(CANData.motorTorque));
	UARTSendChar(UART7_BASE, ',');
	UARTSendStr(UART7_BASE, CANData.motorCtrlTemp, sizeof(CANData.motorCtrlTemp));
	UARTSendChar(UART7_BASE, ',');

	// Send Pump Voltage
	//UARTSend(pumpVoltage, sizeof(pumpVoltage));
	//UART_SendComma();

	// Send AUX pack voltage
	UARTSendStr(UART7_BASE, auxVoltage, sizeof(auxVoltage));
	UARTSendChar(UART7_BASE, ',');

	// Send IMU Data
	UARTSendStr(UART7_BASE, IMUData.xAcc, sizeof(IMUData.xAcc));
	UARTSendChar(UART7_BASE, ',');
	UARTSendStr(UART7_BASE, IMUData.yAcc, sizeof(IMUData.yAcc));
	UARTSendChar(UART7_BASE, ',');
	UARTSendStr(UART7_BASE, IMUData.zAcc, sizeof(IMUData.zAcc));
	UARTSendChar(UART7_BASE, ',');
	UARTSendStr(UART7_BASE, IMUData.xGyro, sizeof(IMUData.xGyro));
	UARTSendChar(UART7_BASE, ',');
	UARTSendStr(UART7_BASE, IMUData.yGyro, sizeof(IMUData.yGyro));
	UARTSendChar(UART7_BASE, ',');
	UARTSendStr(UART7_BASE, IMUData.zGyro, sizeof(IMUData.zGyro));
	UARTSendChar(UART7_BASE, ',');
	UARTSendStr(UART7_BASE, IMUData.roll, sizeof(IMUData.roll));
	UARTSendChar(UART7_BASE, ',');
	UARTSendStr(UART7_BASE, IMUData.pitch, sizeof(IMUData.pitch));
}

void imuParse(char c)
{
#define DEFAULT 0
	enum {
		ACCEL = 'a',
		GYRO = 'g',
		EULER = 'e',
		ACCEL_X = 'a' + 'x',
		ACCEL_Y = 'a' + 'y',
		ACCEL_Z = 'a' + 'z',
		GYRO_X = 'g' + 'x',
		GYRO_Y = 'g' + 'y',
		GYRO_Z = 'g' + 'z',
		EULER_R = 'e' + 'r',
		EULER_Y = 'e' + 'y',
		EULER_P = 'e' + 'p',
		COMPASS = 'c'
	};
	static int state = DEFAULT; // initial state will be default state

	switch (state)
	{
	case ACCEL:
		state = 'a' + c;
		break;

	case ACCEL_X:
		memcpy(gIMUData.xAcc, gIMUReceiveBuf, imuLength);
		memset(gIMUReceiveBuf, '0', imuLength);
		gIMUBufIndex = 0;
		//UARTprintf(" -- Accel-X\n\r");
		state = DEFAULT;
		break;

	case ACCEL_Y:
		memcpy(gIMUData.yAcc, gIMUReceiveBuf, imuLength);
		memset(gIMUReceiveBuf, '0', imuLength);
		gIMUBufIndex = 0;
		//UARTprintf(" -- Accel-Y\n\r");
		state = DEFAULT;
		break;

	case ACCEL_Z:
		memcpy(gIMUData.zAcc, gIMUReceiveBuf, imuLength);
		memset(gIMUReceiveBuf, '0', imuLength);
		gIMUBufIndex = 0;
		//UARTprintf(" -- Accel-Z\n\r");
		state = DEFAULT;
		break;

	case GYRO:
		state = 'g' + c;
		break;

	case GYRO_X:
		memcpy(gIMUData.xGyro, gIMUReceiveBuf, imuLength);
		memset(gIMUReceiveBuf, '0', imuLength);
		gIMUBufIndex = 0;
		//UARTprintf(" -- Gyro-X\n\r");
		state = DEFAULT;
		break;

	case GYRO_Y:
		memcpy(gIMUData.yGyro, gIMUReceiveBuf, imuLength);
		memset(gIMUReceiveBuf, '0', imuLength);
		gIMUBufIndex = 0;
		//UARTprintf(" -- Gyro-Y\n\r");
		state = DEFAULT;
		break;

	case GYRO_Z:
		memcpy(gIMUData.zGyro, gIMUReceiveBuf, imuLength);
		memset(gIMUReceiveBuf, '0', imuLength);
		gIMUBufIndex = 0;
		//UARTprintf(" -- Gyro-Z\n\r");
		state = DEFAULT;
		break;

	case EULER:
		state = 'e' + c;
		break;

	case EULER_R:
		memcpy(gIMUData.roll, gIMUReceiveBuf, imuLength);
		memset(gIMUReceiveBuf, '0', imuLength);
		gIMUBufIndex = 0;
		//UARTprintf(" -- Roll\n\r");
		state = DEFAULT;
		break;

	case EULER_Y:
		memcpy(gIMUData.yaw, gIMUReceiveBuf, imuLength);
		memset(gIMUReceiveBuf, '0', imuLength);
		gIMUBufIndex = 0;
		//UARTprintf(" -- Yaw\n\r");
		state = DEFAULT;
		break;

	case EULER_P:
		memcpy(gIMUData.pitch, gIMUReceiveBuf, imuLength);
		memset(gIMUReceiveBuf, '0', imuLength);
		gIMUBufIndex = 0;
		//UARTprintf(" -- Pitch\n\r");
		state = DEFAULT;
		break;

	case COMPASS:
		memcpy(gIMUData.compass, gIMUReceiveBuf, imuLength);
		memset(gIMUReceiveBuf, '0', imuLength);
		gIMUBufIndex = 0;
		//UARTprintf(" -- Compass\n\r");
		state = DEFAULT;
		break;

	default:
		if ((c >= 0x30 && c <= 0x39) || c == '.' || c == '-') { // Check if the character is an ASCII number
			// Protect against buffer overflow
			if (gIMUBufIndex >= imuLength) {
				memset(gIMUReceiveBuf, '0', imuLength);
				//UARTprintf("Buffer overflow detected\n\r");
				gIMUBufIndex = 0;
			}
			else {
				gIMUReceiveBuf[gIMUBufIndex++] = c;
				//UARTprintf("%c", c);
			}
		}
		else {
			state = c;
		}
		break;
	}
}

//void UART_SendComma()
//{
//	UARTSend(",", 1);
//}

void TIMER1A_IRQHandler()
{
    // Clear the timer interrupt.
	intTimer1_flag = 1;
    MAP_TimerIntClear(TIMER1_BASE, TIMER_TIMA_TIMEOUT);

    // toggle LED every 0.05 seconds
    if (CANCount >= 50) {
        g_ui8canFlag = 1;
        CANCount = 0;
    }
    if (msCount >= 250) {
        // Toggle the flag for the first timer.
        HWREGBITW(&g_ui32Flags, 1) ^= 1;
        g_ui8xbeeFlag = 1;

        if(g_ui32Flags) {
        	MAP_GPIOPinWrite(GPIO_PORTN_BASE, GPIO_PIN_0, GPIO_PIN_0);
        }
        else {
        	MAP_GPIOPinWrite(GPIO_PORTN_BASE, GPIO_PIN_0, ~GPIO_PIN_0);
        }

        msCount = 0;
    }
    msCount++;
    CANCount++;
}
