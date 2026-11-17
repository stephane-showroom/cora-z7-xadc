#include "PWM.h"
#include "xparameters.h"
#include "sleep.h"
#include "stdio.h"
#include "xgpio.h"
#include "xstatus.h"
#include "xil_printf.h"
#include "xil_types.h"
#include "debounce.h"
#include "xsysmon.h" // System Monitor driver for PL XADC

#ifndef SDT
#define GPIO_DEVICE_ID   XPAR_AXI_GPIO_0_DEVICE_ID
#define SYSMON_DEVICE_ID XPAR_SYSMON_0_DEVICE_ID // Check xparameters.h if your IP has a custom name (e.g., XPAR_XADC_WIZ_0_DEVICE_ID)
#else
#define GPIO_DEVICE_ID   XPAR_AXI_GPIO_0_BASEADDR
#define SYSMON_DEVICE_ID XPAR_XADC_WIZ_0_BASEADDR
#define RGBLED_BASEADDR  XPAR_PWM_0_BASEADDR
#endif

const u32 RGBLED_BaseAddr = RGBLED_BASEADDR;

#define NUMBER_OF_CHANNELS 10
const u8 Channel_List[NUMBER_OF_CHANNELS] = {
    3,  // Start with VP/VN
    28, 16, 24, // Diff. Channels in ascending order
    17, 25, 22, 31, 21, 29 // Single-Ended Channels in ascending order
};

// Channel on board connection
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

void RGBLED_SetColor(u32 base_address, u16 r, u16 g, u16 b) {
    Xil_Out32(base_address + PWM_AXI_DUTY_REG_OFFSET + (4*0), b);
    Xil_Out32(base_address + PWM_AXI_DUTY_REG_OFFSET + (4*1), g);
    Xil_Out32(base_address + PWM_AXI_DUTY_REG_OFFSET + (4*2), r);
}

void RGBLED_Init(u32 base_address) {
    Xil_Out32(base_address + PWM_AXI_PERIOD_REG_OFFSET, 0xffff);
    RGBLED_SetColor(base_address, 0, 0, 0);
    Xil_Out32(base_address + PWM_AXI_CTRL_REG_OFFSET, 1);
}

int SysMon_Init(XSysMon *InstancePtr, UINTPTR DeviceId) {
    XSysMon_Config *ConfigPtr;
    ConfigPtr = XSysMon_LookupConfig(DeviceId);
    if (ConfigPtr == NULL)
        return XST_FAILURE;
    
    if (XSysMon_CfgInitialize(InstancePtr, ConfigPtr, ConfigPtr->BaseAddress) != XST_SUCCESS)
        return XST_FAILURE;

    // Run self-test on the SysMon device
    if (XSysMon_SelfTest(InstancePtr) != XST_SUCCESS)
        return XST_FAILURE;

    // Set sequencer mode to continuous pass
    XSysMon_SetSequencerMode(InstancePtr, XSM_SEQ_MODE_CONTINPASS);

    return XST_SUCCESS;
}

void SysMon_Demo(XSysMon *InstancePtr, u8 ChannelSelect) {
    u32 RawData;
    float VoltageData;

    // Read raw ADC data from the selected channel using the SysMon driver
    RawData = XSysMon_GetAdcData(InstancePtr, ChannelSelect);
    VoltageData = XSysMon_RawToVoltage(RawData);
    
    printf("Analog Input %s (Chan %d): %.3fV\r\n", Channel_Names[ChannelSelect], ChannelSelect, VoltageData);
    
    if (VoltageData > 0.5)
        RGBLED_SetColor(RGBLED_BaseAddr, 0x0000, 0x8000, 0x0000);
    else if (VoltageData < 0.1)
        RGBLED_SetColor(RGBLED_BaseAddr, 0x8000, 0x0000, 0x0000);
    else
        RGBLED_SetColor(RGBLED_BaseAddr, 0x0000, 0x0000, 0x0000);
}

int Btn_Init(XGpio *InstancePtr, UINTPTR DeviceId) {
    int Status = XGpio_Initialize(InstancePtr, DeviceId);
    if (Status != XST_SUCCESS)
        return XST_FAILURE;

    XGpio_SetDataDirection(InstancePtr, 1, 0x3);
    return XST_SUCCESS;
}

int main() {
    XSysMon SysMonInst;
    u8 ChannelIndex = 0;
    XGpio Btn;
    u32 Btn_Data;
    u8 Btn0_Data, Btn1_Data;
    Debounce Btn0_Db, Btn1_Db;
    u32 time_count = 0;

    if (SysMon_Init(&SysMonInst, SYSMON_DEVICE_ID) != XST_SUCCESS) {
        xil_printf("SysMon PL init failed\r\n");
        return XST_FAILURE;
    }
    if (Btn_Init(&Btn, GPIO_DEVICE_ID) != XST_SUCCESS) {
        xil_printf("GPIO init failed\r\n");
        return XST_FAILURE;
    }
    RGBLED_Init(RGBLED_BaseAddr);
    Debounce_Init(&Btn0_Db, 10000);
    Debounce_Init(&Btn1_Db, 10000);

    xil_printf("Cora PL SysMon Demo Initialized!\r\n");

    while (1) {
        Btn_Data = XGpio_DiscreteRead(&Btn, 1);
        Btn0_Data = (u8)(Btn_Data & 0x01);
        Btn1_Data = (u8)((Btn_Data >> 1) & 0x01);

        Debounce_Update(&Btn0_Db, Btn0_Data);
        Debounce_Update(&Btn1_Db, Btn1_Data);

        if (Btn1_Db.Flag == 1 && Btn0_Db.Flag == 0) {
            if (ChannelIndex + 1 < NUMBER_OF_CHANNELS)
                ChannelIndex++;
            else
                ChannelIndex = 0;
        } else if (Btn1_Db.Flag == 0 && Btn0_Db.Flag == 1) {
            if (ChannelIndex > 0)
                ChannelIndex--;
            else
                ChannelIndex = NUMBER_OF_CHANNELS - 1;
        }

        time_count++;
        if (time_count == 100000) {
            time_count = 0;
            SysMon_Demo(&SysMonInst, Channel_List[ChannelIndex]);
        }
        usleep(1);
    }
}