

#include "PWM.h"
#include "xparameters.h"
#include "sleep.h"
#include "stdio.h"
#include "xgpiops.h"
#include "xstatus.h"
#include "xil_printf.h"
#include "xil_types.h"
#include "debounce.h"
#include "xadcps.h" // Using the Zynq PS XADC driver

/*
 * Classic flow (XSCT): drivers are identified by DEVICE_ID.
 * SDT flow (Vitis Unified): drivers are identified by their base address.
 */
#ifndef SDT
#define GPIO_DEVICE_ID  XPAR_XGPIOPS_0_DEVICE_ID
#define XADC_DEVICE_ID  XPAR_XADCPS_0_DEVICE_ID
#else
#define GPIO_DEVICE_ID  XPAR_XGPIOPS_0_BASEADDR
#define XADC_DEVICE_ID  XPAR_XXADCPS_0_BASEADDR
#define RGBLED_BASEADDR XPAR_PWM_0_BASEADDR
#endif

const u32 RGBLED_BaseAddr = RGBLED_BASEADDR;

#define LED_ON_DUTY 0x3FFF
#define LED_OFF_DUTY 0x3000
/*
 * PS GPIO pin numbers for the buttons.
 * MIO: 0..53, EMIO: 54..117 (bank 2 = EMIO 0..31).
 * The Cora Z7 buttons are on the PL: they must be routed via the EMIO GPIO
 * of the processing_system7 (here EMIO 0 and 1 -> pins 54 and 55).
 */
#define BTN0_PIN 54
#define BTN1_PIN 55

#define NUMBER_OF_CHANNELS 10
const u8 Channel_List[NUMBER_OF_CHANNELS] = {
	3, // Start with VP/VN
	28, 16, 24, // Diff. Channels in ascending order
	17, 25, 22, 31, 21, 29 // Single-Ended Channels in ascending order
}; // 00008
const char *Channel_Names[32] = {
	"", "", "", "VP-VN",
	"", "", "", "",
	"", "", "", "",
	"", "", "", "",
	"A8-A9", "A0", "", "",
	"", "A4", "A2", "",
	"A10-A11", "A1", "", "",
	"A6-A7", "A5", "", "A3"
};

#define Test_Bit(VEC,BIT) ((VEC&(1<<BIT))!=0)
void RGBLED_SetColor(u32 base_address, u16 r, u16 g, u16 b) {
    Xil_Out32(base_address + PWM_AXI_DUTY_REG_OFFSET + (4*0), b);
    Xil_Out32(base_address + PWM_AXI_DUTY_REG_OFFSET + (4*1), g);
    Xil_Out32(base_address + PWM_AXI_DUTY_REG_OFFSET + (4*2), r);
}
void RGBLED_Init(u32 base_address) {
	//PWM_Set_Period(base_address, 0xffff);
    Xil_Out32(base_address + PWM_AXI_PERIOD_REG_OFFSET, 0xffff);
	RGBLED_SetColor(base_address, 0, 0, 0);
	//PWM_Enable(base_address);
    Xil_Out32(base_address + PWM_AXI_CTRL_REG_OFFSET, 1);
}

int Xadc_Init(XAdcPs *InstancePtr, UINTPTR DeviceId) {
    XAdcPs_Config *ConfigPtr;
    ConfigPtr = XAdcPs_LookupConfig(DeviceId);
    if (ConfigPtr == NULL)
        return XST_FAILURE;
    if (XAdcPs_CfgInitialize(InstancePtr, ConfigPtr, ConfigPtr->BaseAddress) != XST_SUCCESS)
        return XST_FAILURE;

    // Disable the sequencer for configuration
    XAdcPs_SetSequencerMode(InstancePtr, XADCPS_SEQ_MODE_SAFE);
    // Configure the input mode and scanning mode according to your needs
    XAdcPs_SetSequencerMode(InstancePtr, XADCPS_SEQ_MODE_CONTINPASS);
    return XST_SUCCESS;
}

void Xadc_Demo(XAdcPs *InstancePtr, u8 ChannelSelect) {
    u32 RawData;
    float VoltageData;

    RawData = XAdcPs_GetAdcData(InstancePtr, ChannelSelect);
    
	if (Test_Bit(RawData, ChannelSelect)) {
		VoltageData = XAdcPs_RawToVoltage(RawData);
        printf("Analog Input %s: %.3fV\r\n", Channel_Names[ChannelSelect], VoltageData);
		if (VoltageData > 0.5)
			RGBLED_SetColor(RGBLED_BaseAddr, 0x0000, 0x8000, 0x0000);
		else if (VoltageData < -0.5)
			RGBLED_SetColor(RGBLED_BaseAddr, 0x8000, 0x0000, 0x0000);
		else
			RGBLED_SetColor(RGBLED_BaseAddr, 0x0000, 0x0000, 0x0000);
	} else {
		printf("Channel %d (%s) Not Available\r\n", (int)ChannelSelect, Channel_Names[ChannelSelect]);
		RGBLED_SetColor(RGBLED_BaseAddr, 0, 0, 0);
	} 
}

int Btn_Init(XGpioPs *InstancePtr, UINTPTR DeviceId) {
    XGpioPs_Config *ConfigPtr = XGpioPs_LookupConfig(DeviceId);
    if (ConfigPtr == NULL)
        return XST_FAILURE;
    if (XGpioPs_CfgInitialize(InstancePtr, ConfigPtr, ConfigPtr->BaseAddr) != XST_SUCCESS)
        return XST_FAILURE;

    // Button pins as inputs (0 = input)
    XGpioPs_SetDirectionPin(InstancePtr, BTN0_PIN, 0);
    XGpioPs_SetDirectionPin(InstancePtr, BTN1_PIN, 0);
    return XST_SUCCESS;
}

int main () {
    XAdcPs Xadc;
    u8 ChannelIndex = 0;
    XGpioPs Btn;
    u8 Btn0_Data, Btn1_Data;
    Debounce Btn0_Db, Btn1_Db;
    u32 time_count = 0;
    
    
    if (Xadc_Init(&Xadc, XADC_DEVICE_ID) != XST_SUCCESS) {
        xil_printf("XADC init failed");
        return XST_FAILURE;
    }
    if (Btn_Init(&Btn, GPIO_DEVICE_ID) != XST_SUCCESS) {
        xil_printf("GPIO init failed");
        return XST_FAILURE;
    }
    RGBLED_Init(RGBLED_BaseAddr);
    Debounce_Init(&Btn0_Db, 10000);
    Debounce_Init(&Btn1_Db, 10000);

    xil_printf("Cora XADC Demo Initialized!\r\n");

    while(1) {
        Btn0_Data = (u8)XGpioPs_ReadPin(&Btn, BTN0_PIN);
        Btn1_Data = (u8)XGpioPs_ReadPin(&Btn, BTN1_PIN);
        Debounce_Update(&Btn0_Db, Btn0_Data);
        Debounce_Update(&Btn1_Db, Btn1_Data);

        if (Btn1_Db.Flag == 1 && Btn0_Db.Flag == 0) {
            if (ChannelIndex + 1 < NUMBER_OF_CHANNELS)
                ChannelIndex ++;
            else
                ChannelIndex = 0;
        } else if (Btn1_Db.Flag == 0 && Btn0_Db.Flag == 1) {
            if (ChannelIndex > 0)
                ChannelIndex --;
            else
                ChannelIndex = NUMBER_OF_CHANNELS-1;
        }

        time_count ++;
        if (time_count == 100000) {
            time_count = 0;
            Xadc_Demo(&Xadc, Channel_List[ChannelIndex]);
        }
        usleep(1);
    }
}