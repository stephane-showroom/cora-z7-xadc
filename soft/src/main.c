#include "PWM.h"
#include "xparameters.h"
#include "sleep.h"
#include "stdio.h"
#include "xgpiops.h"
#include "xstatus.h"
#include "xil_printf.h"
#include "xil_types.h"
#include "debounce.h"
#include "xadcps.h" // Utilisation du pilote XADC du PS Zynq

/*
 * Flux classique (XSCT) : les drivers sont identifiés par DEVICE_ID.
 * Flux SDT (Vitis Unified) : les drivers sont identifiés par leur adresse de base.
 */
#ifndef SDT
#define GPIO_DEVICE_ID  XPAR_XGPIOPS_0_DEVICE_ID
#define XADC_DEVICE_ID  XPAR_XADCPS_0_DEVICE_ID
#else
#define GPIO_DEVICE_ID  XPAR_XGPIOPS_0_BASEADDR
#define XADC_DEVICE_ID  XPAR_XXADCPS_0_BASEADDR
#endif

/*
 * Numéros de broches GPIO PS des boutons.
 * MIO : 0..53, EMIO : 54..117 (banque 2 = EMIO 0..31).
 * Les boutons du Cora Z7 sont sur le PL : il faut les router via l'EMIO GPIO
 * du processing_system7 (ici EMIO 0 et 1 -> broches 54 et 55).
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


int Xadc_Init(XAdcPs *InstancePtr, UINTPTR DeviceId) {
    XAdcPs_Config *ConfigPtr;
    ConfigPtr = XAdcPs_LookupConfig(DeviceId);
    if (ConfigPtr == NULL)
        return XST_FAILURE;
    if (XAdcPs_CfgInitialize(InstancePtr, ConfigPtr, ConfigPtr->BaseAddress) != XST_SUCCESS)
        return XST_FAILURE;

    // Désactiver le séquenceur pour la configuration
    XAdcPs_SetSequencerMode(InstancePtr, XADCPS_SEQ_MODE_SAFE);
    // Configurer le mode d'entrée et le mode de balayage selon vos besoins
    XAdcPs_SetSequencerMode(InstancePtr, XADCPS_SEQ_MODE_CONTINPASS);
    return XST_SUCCESS;
}

void Xadc_Demo(XAdcPs *InstancePtr, u8 ChannelSelect) {
    u32 RawData;
    float VoltageData;

    RawData = XAdcPs_GetAdcData(InstancePtr, ChannelSelect);
    VoltageData = XAdcPs_RawToVoltage(RawData);
    
    // Affichage formaté (xil_printf ne gérant pas les floats directement)
    int IntPart = (int)VoltageData;
    int FraqPart = (int)((VoltageData - IntPart) * 1000);
    if (FraqPart < 0) FraqPart = -FraqPart;

    xil_printf("Analog Input Channel: %d -> Voltage: %d.%03dV\r\n", ChannelSelect, IntPart, FraqPart);
}

int Btn_Init(XGpioPs *InstancePtr, UINTPTR DeviceId) {
    XGpioPs_Config *ConfigPtr = XGpioPs_LookupConfig(DeviceId);
    if (ConfigPtr == NULL)
        return XST_FAILURE;
    if (XGpioPs_CfgInitialize(InstancePtr, ConfigPtr, ConfigPtr->BaseAddr) != XST_SUCCESS)
        return XST_FAILURE;

    // Broches des boutons en entrée (0 = entrée)
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