/* =====================================================================*/
/* Program: device.c                                                    */
/*                                                                      */
/* Project: PV-Batteriewechselrichter                                   */
/*                                                                      */
/* Description: Hardware driver                                         */
/*                                                                      */
/* Last Edited: 2018-11-06                                              */
/*                                                                      */
/* Author: Falk Kyburz                                                  */
/*                                                                      */
/* ==================================================================== */
/*    (c) 2018 by Interstaatliche Hochschule f�r Technik Buchs NTB      */
/* ==================================================================== */


/* ==================================================================== */
/* ========================== include files =========================== */
/* ==================================================================== */
#include <stdbool.h>
#include <stddef.h>
#include "application.h"
#include "device.h"
#include "defines.h"
//#include "ringbuffer.h"
#include "DSP2803x_Cla_typedefs.h"// DSP2803x CLA Type definitions

#include "DSP2803x_Device.h"      // DSP2803x Headerfile Include File


/* ==================================================================== */
/* ============================ constants ============================= */
/* ==================================================================== */

/* none */




/* ==================================================================== */
/* ======================== global variables ========================== */
/* ==================================================================== */

/* none */




/* ==================================================================== */
/* ========================== private data ============================ */
/* ==================================================================== */

/* none */




/* ==================================================================== */
/* ====================== private functions =========================== */
/* ==================================================================== */
static int32_t device_initSysCtl(void);
static int32_t device_initCPUTimer(void);
static int32_t device_initCPUMemory(void);
static int32_t device_initClocks(void);
static int32_t device_initDAC(void);
static int32_t device_serialEcho(ringbuffer_t *rbufrx, ringbuffer_t *rbuftx);
static int32_t device_initSCI(void);
static int32_t device_initEPWM(void);
static int32_t device_initEPWM3phInterleaved(void);
static int32_t device_initEPWM3phNIBBSpecialModulation(void);
static int32_t device_initADC(void);
static int32_t device_initGPIO(void);
static int32_t device_initGPIO3phInterleaved(void);
void __error__(char *filename, uint32_t line);



/* ==================================================================== */
/* ================= interrupt service functions ====================== */
/* ==================================================================== */

__interrupt void cpu_timer0_isr(void);
//__interrupt void sciaTxIsr(void);
__interrupt void sciaRXISR(void);
__interrupt void ISR_ILLEGAL(void);


/* ==================================================================== */
/* ===================== All functions by section ===================== */
/* ==================================================================== */


/*
 * Perform initialization of all peripherals in the correct order
 */
int32_t device_init(void)
{
    int32_t err = NO_ERROR;

    /* Initialize PLL, calibrations and clock system*/
    device_initSysCtl();

    /* Initialize CPU Memory */
    device_initCPUMemory();

    /* Initialize board related peripherals */
    // Connect ePWM1, ePWM2, ePWM3 to GPIO pins, so that in not necessary toggle function in ISR
    device_initGPIO();
    // Setup only the GPI/O only for SCI-A and SCI-B functionality
    // This function is found in DSP2803x_Sci.c
    InitSciaGpio();

    /* Init CPU Timers */
    device_initCPUTimer();

    /* Init watchdog */
    //    device_initWatchdog();

    /* Initialize the CLA Memory */

    /* Initialize all peripherals */
    device_initSCI();
    device_initEPWM();
    device_initADC();
    device_initDAC();

    /* Clear all TZ flags before starting*/
    //device_setALLClearTZFlags();

    /* Register and enable all used interrupts at PIE*/
    //device_registerInterrupts();
    // Initialize the PIE control registers to their default state.
    // The default state is all PIE interrupts disabled and flags are cleared.
    InitPieCtrl();
    // Initialize the PIE vector table with pointers to the shell Interrupt
    // Service Routines (ISR).
    InitPieVectTable();

    EALLOW;  // This is needed to write to EALLOW protected registers
    PieVectTable.SCIRXINTA = &sciaRXISR;
    //PieVectTable.SCITXINTA = &sciaTxIsr;
    PieVectTable.TINT0 = &cpu_timer0_isr;
    EDIS;    // This is needed to disable write to EALLOW protected registers

    // Enable CPU INT1 which is connected to CPU-Timer 0:
    IER |= M_INT1;
    // Enable TINT0 in the PIE: Group 1 interrupt 7
    PieCtrlRegs.PIEIER1.bit.INTx7 = 1;
    // Enable CPU INT3 which is connected to EPWM1-3 INT:
    IER |= M_INT3;
    // Enable EPWM INTn in the PIE: Group 3 interrupt 1-3
    PieCtrlRegs.PIEIER3.bit.INTx1 = 1;
    PieCtrlRegs.PIEIER3.bit.INTx2 = 1;
    PieCtrlRegs.PIEIER3.bit.INTx3 = 1;

    // Enable interrupts required for this example
    PieCtrlRegs.PIECTRL.bit.ENPIE = 1;   // Enable the PIE block
    PieCtrlRegs.PIEIER9.bit.INTx1=1;     // PIE Group 9, INT1
    PieCtrlRegs.PIEIER9.bit.INTx2=1;     // PIE Group 9, INT2
    IER = 0x100; // Enable CPU INT
    EINT;

    // Enable global Interrupts and higher priority real-time debug events:
    EINT;   // Enable Global interrupt INTM
    ERTM;   // Enable Global realtime interrupt DBGM

    // Initialize counters

    /** Error handling **/
    // TODO: see falk stuff

    return err;
}

static int32_t device_initSysCtl(void)
{
    // PLL, WatchDog, enable Peripheral Clocks
    InitSysCtrl();

    return NO_ERROR;
}

/* it starts also the timers */
static int32_t device_initCPUTimer(void)
{
    EALLOW;
    // Timing sync for background loops
    // Timer period definitions found in PeripheralHeaderIncludes.h
    CpuTimer0Regs.PRD.all =  mSec0_5 ;      // A tasks
    CpuTimer1Regs.PRD.all =  mSec50;    // B tasks
    //CpuTimer2Regs.PRD.all =  mSec10;  // C tasks

    // Configure CPU-Timer 2 to interrupt every second:
    // 60MHz CPU Freq, Period (in uSeconds)
    // Configure CPU Timer interrupt for 20KHz interrupt
    CpuTimer2Regs.PRD.all = mSec1 * 0.1;

    // Set pre-scale counter to divide by 1 (SYSCLKOUT):
    CpuTimer2Regs.TPR.all  = 0;
    CpuTimer2Regs.TPRH.all  = 0;

    // Initialize timer control register:
    CpuTimer2Regs.TCR.bit.TSS = 1;      // 1 = Stop timer, 0 = Start/Restart Timer
    CpuTimer2Regs.TCR.bit.TRB = 1;      // 1 = reload timer
    CpuTimer2Regs.TCR.bit.SOFT = 0;
    CpuTimer2Regs.TCR.bit.FREE = 0;     // Timer Free Run Disabled
    CpuTimer2Regs.TCR.bit.TIE = 1;      // 0 = Disable/ 1 = Enable Timer Interrupt
    CpuTimer2Regs.TCR.all = 0x4001; // Use write-only instruction to set TSS bit = 0
    EDIS;

    return NO_ERROR;

}


/*
 * Set CPU Memory and copy RAM functions to RAM.
 */
static int32_t device_initCPUMemory(void)
{
    // Only used if running from FLASH
    // Note that the variable FLASH is defined by the compiler with -d FLASH
#ifdef FLASH
    // Copy time critical code and Flash setup code to RAM
    // The  RamfuncsLoadStart, RamfuncsLoadEnd, and RamfuncsRunStart
    // symbols are created by the linker. Refer to the linker files.
    MemCopy(&RamfuncsLoadStart, &RamfuncsLoadEnd, &RamfuncsRunStart);

    // Call Flash Initialization to setup flash waitstates
    // This function must reside in RAM
    InitFlash();    // Call the flash wrapper init function
#endif //(FLASH)

    return NO_ERROR;
}


/*
 * Initialize all clocks
 */
static int32_t device_initClocks(void)
{
    EALLOW;

    InitPeripheralClocks;

    //    // LOW SPEED CLOCKS prescale register settings
    //    SysCtrlRegs.LOSPCP.all = 0x0002;     // Sysclk / 4 (15 MHz)
    //    SysCtrlRegs.XCLK.bit.XCLKOUTDIV=2;
    //
    //    // PERIPHERAL CLOCK ENABLES
    //    //---------------------------------------------------
    //    // If you are not using a peripheral you may want to switch
    //    // the clock off to save power, i.e. set to =0
    //    //
    //    // Note: not all peripherals are available on all 280x derivates.
    //    // Refer to the datasheet for your particular device.
    //
    //    SysCtrlRegs.PCLKCR0.bit.ADCENCLK = 1;    // ADC
    //    //------------------------------------------------
    //    SysCtrlRegs.PCLKCR3.bit.COMP1ENCLK = 1;  // COMP1
    //    SysCtrlRegs.PCLKCR3.bit.COMP2ENCLK = 1;  // COMP2
    //    SysCtrlRegs.PCLKCR3.bit.COMP3ENCLK = 1;  // COMP3
    //    //------------------------------------------------
    //    SysCtrlRegs.PCLKCR1.bit.ECAP1ENCLK = 0;  //eCAP1
    //    //------------------------------------------------
    //    SysCtrlRegs.PCLKCR0.bit.ECANAENCLK=0;   // eCAN-A
    //    //------------------------------------------------
    //    SysCtrlRegs.PCLKCR1.bit.EQEP1ENCLK = 0;  // eQEP1
    //    //------------------------------------------------
    //    SysCtrlRegs.PCLKCR1.bit.EPWM1ENCLK = 1;  // ePWM1
    //    SysCtrlRegs.PCLKCR1.bit.EPWM2ENCLK = 1;  // ePWM2
    //    SysCtrlRegs.PCLKCR1.bit.EPWM3ENCLK = 1;  // ePWM3
    //    SysCtrlRegs.PCLKCR1.bit.EPWM4ENCLK = 1;  // ePWM4
    //    SysCtrlRegs.PCLKCR1.bit.EPWM5ENCLK = 1;  // ePWM5
    //    SysCtrlRegs.PCLKCR1.bit.EPWM6ENCLK = 0;  // ePWM6
    //    SysCtrlRegs.PCLKCR1.bit.EPWM7ENCLK = 1;  // ePWM7
    //    SysCtrlRegs.PCLKCR0.bit.HRPWMENCLK = 0;    // HRPWM
    //    //------------------------------------------------
    //    SysCtrlRegs.PCLKCR0.bit.I2CAENCLK = 1;   // I2C
    //    //------------------------------------------------
    //    SysCtrlRegs.PCLKCR0.bit.LINAENCLK = 0;   // LIN-A
    //    //------------------------------------------------
    //    SysCtrlRegs.PCLKCR3.bit.CLA1ENCLK = 0;   // CLA1
    //    //------------------------------------------------
    //    SysCtrlRegs.PCLKCR0.bit.SCIAENCLK = 1;   // SCI-A
    //    //------------------------------------------------
    //    SysCtrlRegs.PCLKCR0.bit.SPIAENCLK = 0;   // SPI-A
    //    SysCtrlRegs.PCLKCR0.bit.SPIBENCLK = 0;   // SPI-B
    //    //------------------------------------------------
    //    SysCtrlRegs.PCLKCR0.bit.TBCLKSYNC = 1;   // Enable TBCLK
    //    //------------------------------------------------

    EDIS;   // Disable register access

    return NO_ERROR;
}


/*
 * DAC initialization
 * USAGE: DAC_setShadowValue(DACA_BASE, value)
 */
static int32_t device_initDAC(void)
{

    return NO_ERROR;
}

/*
 * SCIA is used for communication to the PC
 * A ring buffer is used without the internal FIFO
 */
static int32_t device_initSCI(void)
{
    extern ringbuffer_t SCIATXBufferStruct;
    extern ringbuffer_t SCIARXBufferStruct;
    extern uint16_t SCIATXBuffer[SCI_BUFFER_SIZE];
    extern uint16_t SCIARXBuffer[SCI_BUFFER_SIZE];

    /*init peripherical*/
    SciaRegs.SCICCR.all =0x0007;   // 1 stop bit,  No loopback  No parity,8 char bits, async mode, idle-line protocol
    SciaRegs.SCICTL1.all =0x0003;  // enable TX, RX, internal SCICLK,
    // Disable RX ERR, SLEEP, TXWAKE
    //SciaRegs.SCICTL2.bit.TXINTENA =0;
    SciaRegs.SCICTL2.bit.RXBKINTENA =1;
    SciaRegs.SCIHBAUD = 0x0000;
    SciaRegs.SCILBAUD = 0x00C2; //9600;
    SciaRegs.SCICCR.bit.LOOPBKENA =0; // Enable loop back
    SciaRegs.SCICTL1.all = 0x0023;     // Relinquish SCI from Reset

    /* Initialize ring buffer */
    SCIATXBufferStruct.buffer = SCIATXBuffer;
    SCIARXBufferStruct.buffer = SCIARXBuffer;
    SCIATXBufferStruct.size = SCI_BUFFER_SIZE;
    SCIARXBufferStruct.size = SCI_BUFFER_SIZE;
    ringbuffer_reset(&SCIATXBufferStruct);
    ringbuffer_reset(&SCIARXBufferStruct);

    return NO_ERROR;
}

/* not misra compliant */
static int32_t device_serialEcho(ringbuffer_t *rbufrx, ringbuffer_t *rbuftx)
{
    uint16_t temp;
    while(!ringbuffer_empty(rbufrx))
    {
        ringbuffer_get(rbufrx, &temp);
        ringbuffer_put(rbuftx, temp);
    }
    return NO_ERROR;
}

static int32_t device_initEPWM(void)
{
    //=====================================================================
    // Config
    // Initialization Time
    //===========================================================================
    // EPWM Module 1 config
    EPwm1Regs.TBPRD = EPWM_A_INIT_PERIOD; // Period = 900 TBCLK counts
    EPwm1Regs.TBPHS.half.TBPHS = EPWMx_INIT_PHASE; // Set Phase register to zero
    EPwm1Regs.TBCTL.bit.CTRMODE = TB_COUNT_UPDOWN; // Symmetrical mode
    EPwm1Regs.TBCTL.bit.PHSEN = TB_DISABLE; // Master module
    EPwm1Regs.TBCTL.bit.PRDLD = TB_SHADOW;
    EPwm1Regs.TBCTL.bit.SYNCOSEL = TB_CTR_ZERO; // Sync down-stream module
    EPwm1Regs.TBCTL.bit.HSPCLKDIV = TB_DIV1; // TBCLK = SYSCLKOUT
    EPwm1Regs.TBCTL.bit.CLKDIV = TB_DIV1;
    EPwm1Regs.CMPCTL.bit.SHDWAMODE = CC_SHADOW;
    EPwm1Regs.CMPCTL.bit.SHDWBMODE = CC_SHADOW;
    EPwm1Regs.CMPCTL.bit.LOADAMODE = CC_CTR_ZERO; // load on CTR=Zero
    EPwm1Regs.CMPCTL.bit.LOADBMODE = CC_CTR_ZERO; // load on CTR=Zero
    EPwm1Regs.AQCTLA.bit.CAU = AQ_SET; // set actions for EPWM1A
    EPwm1Regs.AQCTLA.bit.CAD = AQ_CLEAR;
    EPwm1Regs.DBCTL.bit.OUT_MODE = DB_FULL_ENABLE; // enable Dead-band module
    EPwm1Regs.DBCTL.bit.POLSEL = DB_ACTV_HIC; // Active Hi complementary
    EPwm1Regs.DBFED = EPWM_A_INIT_DEADBAND; // FED = 20 TBCLKs
    EPwm1Regs.DBRED = EPWM_A_INIT_DEADBAND; // RED = 20 TBCLKs
//    EPwm1Regs.AQCTLA.bit.CAU = AQ_SET; // set actions for EPWM1A
//    EPwm1Regs.AQCTLA.bit.CAD = AQ_CLEAR;
//    EPwm1Regs.AQCTLB.bit.CAU = AQ_SET; // set actions for EPWM1A
//    EPwm1Regs.AQCTLB.bit.CAD = AQ_CLEAR;
//    EPwm1Regs.DBCTL.bit.IN_MODE = DBA_RED_DBB_FED;
//    EPwm1Regs.DBCTL.bit.OUT_MODE = DB_FULL_ENABLE; // enable Dead-band module
//    EPwm1Regs.DBCTL.bit.POLSEL = DB_ACTV_HIC; // Active Hi complementary
//    EPwm1Regs.DBFED = EPWM_A_INIT_DEADBAND; // FED = 20 TBCLKs
//    EPwm1Regs.DBRED = EPWM_A_INIT_DEADBAND; // RED = 20 TBCLKs

    // EPWM Module 2 config
    EPwm2Regs.TBPRD = EPWM_B_INIT_PERIOD; // Period = 900 TBCLK counts
    EPwm2Regs.TBPHS.half.TBPHS = EPWM_B_INIT_PHASE; // Phase = 300/900 * 360 = 120 deg
    EPwm2Regs.TBCTL.bit.CTRMODE = TB_COUNT_UPDOWN; // Symmetrical mode
    EPwm2Regs.TBCTL.bit.PHSEN = TB_ENABLE; // Slave module
    EPwm2Regs.TBCTL.bit.PHSDIR = TB_DOWN; // Count DOWN on sync (=120 deg)
    EPwm2Regs.TBCTL.bit.PRDLD = TB_SHADOW;
    EPwm2Regs.TBCTL.bit.SYNCOSEL = TB_SYNC_IN; // sync flow-through
    EPwm2Regs.TBCTL.bit.HSPCLKDIV = TB_DIV1; // TBCLK = SYSCLKOUT
    EPwm2Regs.TBCTL.bit.CLKDIV = TB_DIV1;
    EPwm2Regs.CMPCTL.bit.SHDWAMODE = CC_SHADOW;
    EPwm2Regs.CMPCTL.bit.SHDWBMODE = CC_SHADOW;
    EPwm2Regs.CMPCTL.bit.LOADAMODE = CC_CTR_ZERO; // load on CTR=Zero
    EPwm2Regs.CMPCTL.bit.LOADBMODE = CC_CTR_ZERO; // load on CTR=Zero
    EPwm2Regs.AQCTLA.bit.CAU = AQ_SET; // set actions for EPWM2A
    EPwm2Regs.AQCTLA.bit.CAD = AQ_CLEAR;
    EPwm2Regs.DBCTL.bit.OUT_MODE = DB_FULL_ENABLE; // enable dead-band module
    EPwm2Regs.DBCTL.bit.POLSEL = DB_ACTV_HIC; // Active Hi Complementary
    EPwm2Regs.DBFED = EPWM_B_INIT_DEADBAND; // FED = 20 TBCLKs
    EPwm2Regs.DBRED = EPWM_B_INIT_DEADBAND; // RED = 20 TBCLKs

    // Run Time (Note: Example execution of one run-time instant)
    //===========================================================
    EPwm1Regs.CMPA.half.CMPA = EPWM_A_INIT_CMPA; // adjust duty for output EPWM1A
    EPwm2Regs.CMPA.half.CMPA = EPWM_B_INIT_CMPA; // adjust duty for output EPWM2A

    return NO_ERROR;
}

static int32_t device_initEPWM3phInterleaved(void)
{
    //=====================================================================
    // Config
    // Initialization Time
    //===========================================================================
    // EPWM Module 1 config
    EPwm1Regs.TBPRD = EPWM_A_INIT_PERIOD; // Period = 900 TBCLK counts
    EPwm1Regs.TBPHS.half.TBPHS = 0; // Set Phase register to zero
    EPwm1Regs.TBCTL.bit.CTRMODE = TB_COUNT_UPDOWN; // Symmetrical mode
    EPwm1Regs.TBCTL.bit.PHSEN = TB_DISABLE; // Master module
    EPwm1Regs.TBCTL.bit.PRDLD = TB_SHADOW;
    EPwm1Regs.TBCTL.bit.SYNCOSEL = TB_CTR_ZERO; // Sync down-stream module
    EPwm1Regs.TBCTL.bit.HSPCLKDIV = TB_DIV1; // TBCLK = SYSCLKOUT
    EPwm1Regs.TBCTL.bit.CLKDIV = TB_DIV1;
    EPwm1Regs.CMPCTL.bit.SHDWAMODE = CC_SHADOW;
    EPwm1Regs.CMPCTL.bit.SHDWBMODE = CC_SHADOW;
    EPwm1Regs.CMPCTL.bit.LOADAMODE = CC_CTR_ZERO; // load on CTR=Zero
    EPwm1Regs.CMPCTL.bit.LOADBMODE = CC_CTR_ZERO; // load on CTR=Zero
    EPwm1Regs.AQCTLA.bit.CAU = AQ_SET; // set actions for EPWM1A
    EPwm1Regs.AQCTLA.bit.CAD = AQ_CLEAR;
    EPwm1Regs.DBCTL.bit.OUT_MODE = DB_FULL_ENABLE; // enable Dead-band module
    EPwm1Regs.DBCTL.bit.POLSEL = DB_ACTV_HIC; // Active Hi complementary
    EPwm1Regs.DBFED = EPWM_A_INIT_DEADBAND; // FED = 20 TBCLKs
    EPwm1Regs.DBRED = EPWM_A_INIT_DEADBAND; // RED = 20 TBCLKs
    
        // EPWM Module 2 config
    EPwm2Regs.TBPRD = EPWM_B_INIT_PERIOD; // Period = 900 TBCLK counts
    EPwm2Regs.TBPHS.half.TBPHS = EPWMx_INIT_PHASE; // Phase = 300/900 * 360 = 120 deg
    EPwm2Regs.TBCTL.bit.CTRMODE = TB_COUNT_UPDOWN; // Symmetrical mode
    EPwm2Regs.TBCTL.bit.PHSEN = TB_ENABLE; // Slave module
    EPwm2Regs.TBCTL.bit.PHSDIR = TB_DOWN; // Count DOWN on sync (=120 deg)
    EPwm2Regs.TBCTL.bit.PRDLD = TB_SHADOW;
    EPwm2Regs.TBCTL.bit.SYNCOSEL = TB_SYNC_IN; // sync flow-through
    EPwm2Regs.TBCTL.bit.HSPCLKDIV = TB_DIV1; // TBCLK = SYSCLKOUT
    EPwm2Regs.TBCTL.bit.CLKDIV = TB_DIV1;
    EPwm2Regs.CMPCTL.bit.SHDWAMODE = CC_SHADOW;
    EPwm2Regs.CMPCTL.bit.SHDWBMODE = CC_SHADOW;
    EPwm2Regs.CMPCTL.bit.LOADAMODE = CC_CTR_ZERO; // load on CTR=Zero
    EPwm2Regs.CMPCTL.bit.LOADBMODE = CC_CTR_ZERO; // load on CTR=Zero
    EPwm2Regs.AQCTLA.bit.CAU = AQ_SET; // set actions for EPWM2A
    EPwm2Regs.AQCTLA.bit.CAD = AQ_CLEAR;
    EPwm2Regs.DBCTL.bit.OUT_MODE = DB_FULL_ENABLE; // enable dead-band module
    EPwm2Regs.DBCTL.bit.POLSEL = DB_ACTV_HIC; // Active Hi Complementary
    EPwm2Regs.DBFED = EPWM_B_INIT_DEADBAND; // FED = 20 TBCLKs
    EPwm2Regs.DBRED = EPWM_B_INIT_DEADBAND; // RED = 20 TBCLKs

    // EPWM Module 3 config
    EPwm3Regs.TBPRD = EPWM_B_INIT_PERIOD; // Period = 900 TBCLK counts
    EPwm3Regs.TBPHS.half.TBPHS = EPWMx_INIT_PHASE; // Phase = 300/900 * 360 = 120 deg
    EPwm3Regs.TBCTL.bit.CTRMODE = TB_COUNT_UPDOWN; // Symmetrical mode
    EPwm3Regs.TBCTL.bit.PHSEN = TB_ENABLE; // Slave module
    EPwm3Regs.TBCTL.bit.PHSDIR = TB_DOWN; // Count DOWN on sync (=120 deg)
    EPwm3Regs.TBCTL.bit.PRDLD = TB_SHADOW;
    EPwm3Regs.TBCTL.bit.SYNCOSEL = TB_SYNC_IN; // sync flow-through
    EPwm3Regs.TBCTL.bit.HSPCLKDIV = TB_DIV1; // TBCLK = SYSCLKOUT
    EPwm3Regs.TBCTL.bit.CLKDIV = TB_DIV1;
    EPwm3Regs.CMPCTL.bit.SHDWAMODE = CC_SHADOW;
    EPwm3Regs.CMPCTL.bit.SHDWBMODE = CC_SHADOW;
    EPwm3Regs.CMPCTL.bit.LOADAMODE = CC_CTR_ZERO; // load on CTR=Zero
    EPwm3Regs.CMPCTL.bit.LOADBMODE = CC_CTR_ZERO; // load on CTR=Zero
    EPwm3Regs.AQCTLA.bit.CAU = AQ_SET; // set actions for EPWM2A
    EPwm3Regs.AQCTLA.bit.CAD = AQ_CLEAR;
    EPwm3Regs.DBCTL.bit.OUT_MODE = DB_FULL_ENABLE; // enable dead-band module
    EPwm3Regs.DBCTL.bit.POLSEL = DB_ACTV_HIC; // Active Hi Complementary
    EPwm3Regs.DBFED = EPWM_B_INIT_DEADBAND; // FED = 20 TBCLKs
    EPwm3Regs.DBRED = EPWM_B_INIT_DEADBAND; // RED = 20 TBCLKs
    
    // EPWM Module 3 config

    // Run Time (Note: Example execution of one run-time instant)
    //===========================================================
    EPwm1Regs.CMPA.half.CMPA = EPWM_A_INIT_CMPA; // adjust duty for output EPWM1A
    EPwm2Regs.CMPA.half.CMPA = EPWM_A_INIT_CMPA; // adjust duty for output EPWM2A
    EPwm3Regs.CMPA.half.CMPA = EPWM_A_INIT_CMPA; // adjust duty for output EPWM2A

    return NO_ERROR;
}

static int32_t device_initEPWM3phNIBBSpecialModulation(void)
{
    // This modulation is used by inverted power has suggestion,
    // is similar to a NIBB but the PH1 and PH3 are never overlapping (L store energy)
    // the phase PH1/PH2 are interleaving
    
    // EPWM Module 1 config
    EPwm1Regs.TBCTL.bit.PRDLD = TB_IMMEDIATE;
    EPwm1Regs.TBCTL.bit.CTRMODE = TB_COUNT_UP; // Asymmetrical mode
    EPwm1Regs.TBPRD = EPWM_B_INIT_PERIOD; // Period = TBCLK counts
    EPwm1Regs.CMPA.half.CMPA = EPWM_B_INIT_DEADBAND; // adjust duty for output EPWM1A
    EPwm1Regs.CMPB = EPWM_A_INIT_CMPA; // adjust duty for output EPWM1A

    EPwm1Regs.TBPHS.half.TBPHS = 0; // Set Phase register to zero
    EPwm1Regs.TBCTL.bit.PHSEN = TB_DISABLE; // Phase loading disabled


    EPwm1Regs.TBCTL.bit.SYNCOSEL = TB_CTR_CMPB;
    EPwm1Regs.TBCTL.bit.HSPCLKDIV = TB_DIV1; // TBCLK = SYSCLKOUT
    EPwm1Regs.TBCTL.bit.CLKDIV = TB_DIV1;

    EPwm1Regs.CMPCTL.bit.SHDWAMODE = CC_SHADOW;
    EPwm1Regs.CMPCTL.bit.SHDWBMODE = CC_SHADOW;
    EPwm1Regs.CMPCTL.bit.LOADAMODE = CC_CTR_ZERO; // load on CTR=Zero
    EPwm1Regs.CMPCTL.bit.LOADBMODE = CC_CTR_ZERO; // load on CTR=Zero
    EPwm1Regs.AQCTLA.bit.CAU = AQ_SET; // every period set PWM
    EPwm1Regs.AQCTLA.bit.CBU = AQ_CLEAR ; //if PWMA=CMPA on rising edge disable PWM

    EPwm1Regs.DBCTL.bit.OUT_MODE = DB_FULL_ENABLE; // enable Dead-band module
    EPwm1Regs.DBCTL.bit.POLSEL = DB_ACTV_HIC; // Active Hi complementary
    EPwm1Regs.DBFED = EPWM_B_INIT_DEADBAND; // FED = 20 TBCLKs
    EPwm1Regs.DBRED = EPWM_B_INIT_DEADBAND; // RED = 20 TBCLKs

    // EPWM Module 2 config
    EPwm2Regs.TBCTL.bit.PRDLD = TB_IMMEDIATE;
    EPwm2Regs.TBCTL.bit.CTRMODE = TB_COUNT_UP; // Asymmetrical mode
    EPwm2Regs.TBPRD = EPWM_B_INIT_PERIOD; // Period = 1401 TBCLK counts
    EPwm1Regs.CMPA.half.CMPA = EPWM_B_INIT_DEADBAND;
    EPwm2Regs.CMPB = EPWM_A_INIT_CMPA; // adjust duty for output EPWM2A

    EPwm2Regs.TBPHS.half.TBPHS = 0; // Set Phase register to zero
    EPwm2Regs.TBCTL.bit.PHSEN = TB_ENABLE; // Phase loading disabled

    EPwm2Regs.TBCTL.bit.PHSDIR = TB_DOWN;

    EPwm2Regs.TBCTL.bit.SYNCOSEL = TB_CTR_CMPB;
    EPwm2Regs.TBCTL.bit.HSPCLKDIV = TB_DIV1; // TBCLK = SYSCLKOUT
    EPwm2Regs.TBCTL.bit.CLKDIV = TB_DIV1;

    EPwm2Regs.CMPCTL.bit.SHDWAMODE = CC_SHADOW;
    EPwm2Regs.CMPCTL.bit.SHDWBMODE = CC_SHADOW;
    EPwm2Regs.CMPCTL.bit.LOADAMODE = CC_CTR_ZERO; // load on CTR=Zero
    EPwm2Regs.CMPCTL.bit.LOADBMODE = CC_CTR_ZERO; // load on CTR=Zero
    EPwm2Regs.AQCTLA.bit.CAU = AQ_SET; // every period set PWM
    EPwm2Regs.AQCTLA.bit.CBU = AQ_CLEAR ; //if PWMA=CMPA on rising edge disable PWM

    EPwm2Regs.DBCTL.bit.OUT_MODE = DB_FULL_ENABLE; // enable dead-band module
    EPwm2Regs.DBCTL.bit.POLSEL = DB_ACTV_HIC; // Active Hi Complementary
    EPwm2Regs.DBFED = EPWM_B_INIT_DEADBAND; // FED = 20 TBCLKs
    EPwm2Regs.DBRED = EPWM_B_INIT_DEADBAND; // RED = 20 TBCLKs

    // EPWM Module 3 config
    EPwm3Regs.TBCTL.bit.PRDLD = TB_IMMEDIATE;
    EPwm3Regs.TBCTL.bit.CTRMODE = TB_COUNT_UP;
    EPwm3Regs.TBPRD = EPWM_B_INIT_PERIOD; // Period = 801 TBCLK counts
    EPwm1Regs.CMPA.half.CMPA = EPWM_B_INIT_DEADBAND;
    EPwm3Regs.CMPB = EPWM_B_INIT_PERIOD-(2*EPWM_A_INIT_CMPA); // adjust duty for output EPWM3A

    EPwm3Regs.TBPHS.half.TBPHS = 0; // Set Phase register to zero
    EPwm3Regs.TBCTL.bit.PHSEN = TB_ENABLE; // Phase loading disabled
    EPwm3Regs.TBCTL.bit.PHSDIR = TB_DOWN; // Count DOWN on sync (=120 deg)


    EPwm3Regs.TBCTL.bit.SYNCOSEL = TB_SYNC_IN;
    EPwm3Regs.TBCTL.bit.HSPCLKDIV = TB_DIV1; // TBCLK = SYSCLKOUT
    EPwm3Regs.TBCTL.bit.CLKDIV = TB_DIV1;

    EPwm3Regs.CMPCTL.bit.SHDWAMODE = CC_SHADOW;
    EPwm3Regs.CMPCTL.bit.SHDWBMODE = CC_SHADOW;
    EPwm3Regs.CMPCTL.bit.LOADAMODE = CC_CTR_ZERO; // load on CTR=Zero
    EPwm3Regs.CMPCTL.bit.LOADBMODE = CC_CTR_ZERO; // load on CTR=Zero
    EPwm3Regs.AQCTLA.bit.CAU = AQ_SET;
    EPwm3Regs.AQCTLA.bit.CBU = AQ_CLEAR;

    EPwm3Regs.DBCTL.bit.OUT_MODE = DB_FULL_ENABLE; // enable Dead-band module
    EPwm3Regs.DBCTL.bit.POLSEL = DB_ACTV_HIC; // Active Hi complementary
    EPwm3Regs.DBFED = EPWM_B_INIT_DEADBAND; // FED = 20 TBCLKs
    EPwm3Regs.DBRED = EPWM_B_INIT_DEADBAND; // RED = 20 TBCLKs
}

/* sciaRXFIFOISR - SCIA Receive FIFO ISR */
__interrupt void sciaRXISR(void)
{
    extern ringbuffer_t SCIARXBufferStruct;
    uint16_t data;

    /* Read data from RX FIFO */
    data = SciaRegs.SCIRXBUF.all;
    /* Write data to ring buffer */
    ringbuffer_put(&SCIARXBufferStruct, data);

    SciaRegs.SCIFFRX.bit.RXFFOVRCLR=1;   // Clear Overflow flag
    SciaRegs.SCIFFRX.bit.RXFFINTCLR=1;   // Clear Interrupt flag

    PieCtrlRegs.PIEACK.all|=0x100;       // Issue PIE ack
}


static int32_t device_initADC(void)
{

    // ADC CALIBRATION
    //---------------------------------------------------
    // The Device_cal function, which copies the ADC & oscillator calibration values
    // from TI reserved OTP into the appropriate trim registers, occurs automatically
    // in the Boot ROM. If the boot ROM code is bypassed during the debug process, the
    // following function MUST be called for the ADC and oscillators to function according
    // to specification.

    SysCtrlRegs.PCLKCR0.bit.ADCENCLK = 1; // Enable ADC peripheral clock
    (*Device_cal)();                      // Auto-calibrate from TI OTP
    SysCtrlRegs.PCLKCR0.bit.ADCENCLK = 0; // Return ADC clock to original state

    return NO_ERROR;
}

static int32_t device_initGPIO(void)
{
    EALLOW;
    //--------------------------------------------------------------------------------------
    // GPIO (GENERAL PURPOSE I/O) CONFIG
    //--------------------------------------------------------------------------------------
    //-----------------------
    // QUICK NOTES on USAGE:
    //-----------------------
    // If GpioCtrlRegs.GP?MUX?bit.GPIO?= 1, 2 or 3 (i.e. Non GPIO func), then leave
    //  rest of lines commented
    // If GpioCtrlRegs.GP?MUX?bit.GPIO?= 0 (i.e. GPIO func), then:
    //  1) uncomment GpioCtrlRegs.GP?DIR.bit.GPIO? = ? and choose pin to be IN or OUT
    //  2) If IN, can leave next to lines commented
    //  3) If OUT, uncomment line with ..GPACLEAR.. to force pin LOW or
    //             uncomment line with ..GPASET.. to force pin HIGH or
    //--------------------------------------------------------------------------------------
    //--------------------------------------------------------------------------------------
    //  GPIO-00 - PIN FUNCTION = PWM_ULS
    GpioCtrlRegs.GPAPUD.bit.GPIO0 = 1;    // Disable pull-up on GPIO0 (EPWM1A)
    GpioCtrlRegs.GPAMUX1.bit.GPIO0 = 1;     // 0=GPIO,  1=EPWM1A,  2=Resv,  3=Resv
    //  GpioCtrlRegs.GPADIR.bit.GPIO0 = 1;      // 1=OUTput,  0=INput
    //  GpioDataRegs.GPACLEAR.bit.GPIO0 = 1;    // uncomment if --> Set Low initially
    //  GpioDataRegs.GPASET.bit.GPIO0 = 1;      // uncomment if --> Set High initially
    //--------------------------------------------------------------------------------------
    //  GPIO-01 - PIN FUNCTION = PWM_UHS
    GpioCtrlRegs.GPAPUD.bit.GPIO1 = 1;    // Disable pull-up on GPIO1 (EPWM1B)
    GpioCtrlRegs.GPAMUX1.bit.GPIO1 = 1;     // 0=GPIO,  1=EPWM1B,  2=Resv,  3=COMP1OUT
    //  GpioCtrlRegs.GPADIR.bit.GPIO1 = 1;      // 1=OUTput,  0=INput
    //  GpioDataRegs.GPACLEAR.bit.GPIO1 = 1;    // uncomment if --> Set Low initially
    //  GpioDataRegs.GPASET.bit.GPIO1 = 1;      // uncomment if --> Set High initially
    //--------------------------------------------------------------------------------------
    //  GPIO-02 - PIN FUNCTION = PWM_VLS
    GpioCtrlRegs.GPAPUD.bit.GPIO2 = 1;    // Disable pull-up on GPIO2 (EPWM2A)
    GpioCtrlRegs.GPAMUX1.bit.GPIO2 = 1;     // 0=GPIO,  1=EPWM2A,  2=Resv,  3=Resv
    //  GpioCtrlRegs.GPADIR.bit.GPIO2 = 0;      // 1=OUTput,  0=INput
    //  GpioDataRegs.GPACLEAR.bit.GPIO2 = 1;    // uncomment if --> Set Low initially
    //  GpioDataRegs.GPASET.bit.GPIO2 = 1;      // uncomment if --> Set High initially
    //--------------------------------------------------------------------------------------
    //  GPIO-03 - PIN FUNCTION = PWM_VHS
    GpioCtrlRegs.GPAPUD.bit.GPIO3 = 1;    // Disable pull-up on GPIO3 (EPWM2B)
    GpioCtrlRegs.GPAMUX1.bit.GPIO3 = 1;     // 0=GPIO,  1=EPWM2B,  2=SPISOMI-A,  3=COMP2OUT
    //  GpioCtrlRegs.GPADIR.bit.GPIO3 = 1;      // 1=OUTput,  0=INput
    //  GpioDataRegs.GPACLEAR.bit.GPIO3 = 1;    // uncomment if --> Set Low initially
    //  GpioDataRegs.GPASET.bit.GPIO3 = 1;      // uncomment if --> Set High initially
    //--------------------------------------------------------------------------------------
    //  GPIO-04 - PIN FUNCTION = RST_ULS
    GpioCtrlRegs.GPAMUX1.bit.GPIO4 = 0;     // 0=GPIO,  1=EPWM3B,  2=Resv,  3=COMP1OUT
    GpioCtrlRegs.GPADIR.bit.GPIO4 = 1;      // 1=OUTput,  0=INput
    GpioDataRegs.GPACLEAR.bit.GPIO4 = 1;    // uncomment if --> Set Low initially
    //  GpioDataRegs.GPASET.bit.GPIO4 = 1;      // uncomment if --> Set High initially
    //--------------------------------------------------------------------------------------
    //  GPIO-05 - PIN FUNCTION = RST_UHS
    GpioCtrlRegs.GPAMUX1.bit.GPIO5 = 0;     // 0=GPIO,  1=EPWM3B,  2=SPISOMI-A,  3=COMP2OUT
    GpioCtrlRegs.GPADIR.bit.GPIO5 = 1;      // 1=OUTput,  0=INput
    GpioDataRegs.GPACLEAR.bit.GPIO5 = 1;    // uncomment if --> Set Low initially
    //  GpioDataRegs.GPASET.bit.GPIO5 = 1;      // uncomment if --> Set High initially
    //--------------------------------------------------------------------------------------
    //  GPIO-06 - PIN FUNCTION = Enable PWM2A
    GpioCtrlRegs.GPAMUX1.bit.GPIO6 = 0;     // 0=GPIO,  1=EPWM4A,  2=SYNCI,  3=SYNCO
    GpioCtrlRegs.GPADIR.bit.GPIO6 = 1;      // 1=OUTput,  0=INput
    GpioDataRegs.GPACLEAR.bit.GPIO6 = 1;    // uncomment if --> Set Low initially
    //  GpioDataRegs.GPASET.bit.GPIO6 = 1;      // uncomment if --> Set High initially
    //--------------------------------------------------------------------------------------
    //  GPIO-07 - PIN FUNCTION = Enable PWM2B
    GpioCtrlRegs.GPAMUX1.bit.GPIO7 = 0;     // 0=GPIO,  1=EPWM4B,  2=SCIRX-A,  3=Resv
    //  GpioCtrlRegs.GPADIR.bit.GPIO7 = 0;      // 1=OUTput,  0=INput
    //  GpioDataRegs.GPACLEAR.bit.GPIO7 = 1;    // uncomment if --> Set Low initially
    //  GpioDataRegs.GPASET.bit.GPIO7 = 1;      // uncomment if --> Set High initially
    //--------------------------------------------------------------------------------------
    //  GPIO-08 - PIN FUNCTION = --Spare--
    GpioCtrlRegs.GPAMUX1.bit.GPIO8 = 0;     // 0=GPIO,  1=EPWM5A,  2=Resv,  3=ADCSOC-A
    //  GpioCtrlRegs.GPADIR.bit.GPIO8 = 0;      // 1=OUTput,  0=INput
    //  GpioDataRegs.GPACLEAR.bit.GPIO8 = 1;    // uncomment if --> Set Low initially
    //  GpioDataRegs.GPASET.bit.GPIO8 = 1;      // uncomment if --> Set High initially
    //--------------------------------------------------------------------------------------
    //  GPIO-09 - PIN FUNCTION = --Spare--
    GpioCtrlRegs.GPAMUX1.bit.GPIO9 = 0;     // 0=GPIO,  1=EPWM5B,  2=LINTX-A,  3=Resv
    //  GpioCtrlRegs.GPADIR.bit.GPIO9 = 0;      // 1=OUTput,  0=INput
    //  GpioDataRegs.GPACLEAR.bit.GPIO9 = 1;    // uncomment if --> Set Low initially
    //  GpioDataRegs.GPASET.bit.GPIO9 = 1;      // uncomment if --> Set High initially
    //--------------------------------------------------------------------------------------
    //  GPIO-10 - PIN FUNCTION = --Spare--
    GpioCtrlRegs.GPAMUX1.bit.GPIO10 = 0;    // 0=GPIO,  1=EPWM6A,  2=Resv,  3=ADCSOC-B
    GpioCtrlRegs.GPADIR.bit.GPIO10 = 0;     // 1=OUTput,  0=INput
    //  GpioDataRegs.GPACLEAR.bit.GPIO10 = 1;   // uncomment if --> Set Low initially
    //  GpioDataRegs.GPASET.bit.GPIO10 = 1;     // uncomment if --> Set High initially
    //--------------------------------------------------------------------------------------
    //  GPIO-11 - PIN FUNCTION = RST_VLS
    GpioCtrlRegs.GPAMUX1.bit.GPIO11 = 0;    // 0=GPIO,  1=EPWM6B,  2=LINRX-A,  3=Resv
    GpioCtrlRegs.GPADIR.bit.GPIO11 = 1;     // 1=OUTput,  0=INput
    GpioDataRegs.GPACLEAR.bit.GPIO11 = 1;   // uncomment if --> Set Low initially
    //  GpioDataRegs.GPASET.bit.GPIO11 = 1;     // uncomment if --> Set High initially
//    //--------------------------------------------------------------------------------------
//    //  GPIO-12 - PIN FUNCTION = --Spare--
//    GpioCtrlRegs.GPAMUX1.bit.GPIO12 = 0;    // 0=GPIO,  1=TZ1,  2=SCITX-A,  3=SPISIMO-B
//    GpioCtrlRegs.GPADIR.bit.GPIO12 = 1;     // 1=OUTput,  0=INput
//    GpioDataRegs.GPACLEAR.bit.GPIO12 = 1;   // uncomment if --> Set Low initially
//    //  GpioDataRegs.GPASET.bit.GPIO12 = 1;     // uncomment if --> Set High initially
//    //--------------------------------------------------------------------------------------
//    //  GPIO-13 - PIN FUNCTION = --Spare--
//    GpioCtrlRegs.GPAMUX1.bit.GPIO13 = 0;    // 0=GPIO,  1=TZ2,  2=Resv,  3=SPISOMI-B
//    GpioCtrlRegs.GPADIR.bit.GPIO13 = 0;     // 1=OUTput,  0=INput
//    //  GpioDataRegs.GPACLEAR.bit.GPIO13 = 1;   // uncomment if --> Set Low initially
//    //  GpioDataRegs.GPASET.bit.GPIO13 = 1;     // uncomment if --> Set High initially
//    //--------------------------------------------------------------------------------------
//    //  GPIO-14 - PIN FUNCTION = --Spare--
//    GpioCtrlRegs.GPAMUX1.bit.GPIO14 = 0;    // 0=GPIO,  1=TZ3,  2=LINTX-A,  3=SPICLK-B
//    GpioCtrlRegs.GPADIR.bit.GPIO14 = 0;     // 1=OUTput,  0=INput
//    //  GpioDataRegs.GPACLEAR.bit.GPIO14 = 1;   // uncomment if --> Set Low initially
//    //  GpioDataRegs.GPASET.bit.GPIO14 = 1;     // uncomment if --> Set High initially
//    //--------------------------------------------------------------------------------------
//    //  GPIO-15 - PIN FUNCTION = --Spare--
//    GpioCtrlRegs.GPAMUX1.bit.GPIO15 = 0;    // 0=GPIO,  1=TZ1,  2=LINRX-A,  3=SPISTE-B
//    GpioCtrlRegs.GPADIR.bit.GPIO15 = 0;     // 1=OUTput,  0=INput
//    //  GpioDataRegs.GPACLEAR.bit.GPIO15 = 1;   // uncomment if --> Set Low initially
//    //  GpioDataRegs.GPASET.bit.GPIO15 = 1;     // uncomment if --> Set High initially
//    //--------------------------------------------------------------------------------------
//    //  GPIO-16 - PIN FUNCTION = --Spare--
//    GpioCtrlRegs.GPAMUX2.bit.GPIO16 = 0;    // 0=GPIO,  1=SPISIMO-A,  2=Resv,  3=TZ2
//    GpioCtrlRegs.GPADIR.bit.GPIO16 = 0;     // 1=OUTput,  0=INput
//    //  GpioDataRegs.GPACLEAR.bit.GPIO16 = 1;   // uncomment if --> Set Low initially
//    //  GpioDataRegs.GPASET.bit.GPIO16 = 1;     // uncomment if --> Set High initially
//    //--------------------------------------------------------------------------------------
//    //  GPIO-17 - PIN FUNCTION = --Spare--
//    GpioCtrlRegs.GPAMUX2.bit.GPIO17 = 0;    // 0=GPIO,  1=SPISOMI-A,  2=Resv,  3=TZ3
//    GpioCtrlRegs.GPADIR.bit.GPIO17 = 0;     // 1=OUTput,  0=INput
//    //  GpioDataRegs.GPACLEAR.bit.GPIO17 = 1;   // uncomment if --> Set Low initially
//    //  GpioDataRegs.GPASET.bit.GPIO17 = 1;     // uncomment if --> Set High initially
//    //--------------------------------------------------------------------------------------
//    //  GPIO-18 - PIN FUNCTION = --Spare--
//    GpioCtrlRegs.GPAMUX2.bit.GPIO18 = 0;    // 0=GPIO,  1=SPICLK-A,  2=LINTX-A,  3=XCLKOUT
//    GpioCtrlRegs.GPADIR.bit.GPIO18 = 0;     // 1=OUTput,  0=INput
//    //  GpioDataRegs.GPACLEAR.bit.GPIO18 = 1;   // uncomment if --> Set Low initially
//    //  GpioDataRegs.GPASET.bit.GPIO18 = 1;     // uncomment if --> Set High initially
//    //--------------------------------------------------------------------------------------
//    //  GPIO-19 - PIN FUNCTION = --Spare--
//    GpioCtrlRegs.GPAMUX2.bit.GPIO19 = 0;    // 0=GPIO,  1=SPISTE-A,  2=LINRX-A,  3=ECAP1
//    GpioCtrlRegs.GPADIR.bit.GPIO19 = 0;     // 1=OUTput,  0=INput
//    //  GpioDataRegs.GPACLEAR.bit.GPIO19 = 1;   // uncomment if --> Set Low initially
//    //  GpioDataRegs.GPASET.bit.GPIO19 = 1;     // uncomment if --> Set High initially
//    //--------------------------------------------------------------------------------------
//    //  GPIO-20 - PIN FUNCTION = --Spare--
//    GpioCtrlRegs.GPAMUX2.bit.GPIO20 = 0;    // 0=GPIO,  1=EQEPA-1,  2=Resv,  3=COMP1OUT
//    GpioCtrlRegs.GPADIR.bit.GPIO20 = 0;     // 1=OUTput,  0=INput
//    //  GpioDataRegs.GPACLEAR.bit.GPIO20 = 1;   // uncomment if --> Set Low initially
//    //  GpioDataRegs.GPASET.bit.GPIO20 = 1;     // uncomment if --> Set High initially
//    //--------------------------------------------------------------------------------------
//    //  GPIO-21 - PIN FUNCTION = --Spare--
//    GpioCtrlRegs.GPAMUX2.bit.GPIO21 = 0;    // 0=GPIO,  1=EQEPB-1,  2=Resv,  3=COMP2OUT
//    GpioCtrlRegs.GPADIR.bit.GPIO21 = 0;     // 1=OUTput,  0=INput
//    //  GpioDataRegs.GPACLEAR.bit.GPIO21 = 1;   // uncomment if --> Set Low initially
//    //  GpioDataRegs.GPASET.bit.GPIO21 = 1;     // uncomment if --> Set High initially
//    //--------------------------------------------------------------------------------------
//    //  GPIO-22 - PIN FUNCTION = --Spare--
//    GpioCtrlRegs.GPAMUX2.bit.GPIO22 = 0;    // 0=GPIO,  1=EQEPS-1,  2=Resv,  3=LINTX-A
//    GpioCtrlRegs.GPADIR.bit.GPIO22 = 0;     // 1=OUTput,  0=INput
//    //  GpioDataRegs.GPACLEAR.bit.GPIO22 = 1;   // uncomment if --> Set Low initially
//    //  GpioDataRegs.GPASET.bit.GPIO22 = 1;     // uncomment if --> Set High initially
//    //--------------------------------------------------------------------------------------
//    //  GPIO-23 - PIN FUNCTION = --Spare--
//    GpioCtrlRegs.GPAMUX2.bit.GPIO23 = 0;    // 0=GPIO,  1=EQEPI-1,  2=Resv,  3=LINRX-A
//    GpioCtrlRegs.GPADIR.bit.GPIO23 = 0;     // 1=OUTput,  0=INput
//    //  GpioDataRegs.GPACLEAR.bit.GPIO23 = 1;   // uncomment if --> Set Low initially
//    //  GpioDataRegs.GPASET.bit.GPIO23 = 1;     // uncomment if --> Set High initially
//    //--------------------------------------------------------------------------------------
//    //  GPIO-24 - PIN FUNCTION = --Spare--
//    GpioCtrlRegs.GPAMUX2.bit.GPIO24 = 0;    // 0=GPIO,  1=ECAP1,  2=Resv,  3=SPISIMO-B
//    GpioCtrlRegs.GPADIR.bit.GPIO24 = 1;     // 1=OUTput,  0=INput
//    //  GpioDataRegs.GPACLEAR.bit.GPIO24 = 1;   // uncomment if --> Set Low initially
//    GpioDataRegs.GPASET.bit.GPIO24 = 1;     // uncomment if --> Set High initially
//    //--------------------------------------------------------------------------------------
//    //  GPIO-25 - PIN FUNCTION = --Spare--
//    GpioCtrlRegs.GPAMUX2.bit.GPIO25 = 0;    // 0=GPIO,  1=Resv,  2=Resv,  3=SPISOMI-B
//    GpioCtrlRegs.GPADIR.bit.GPIO25 = 0;     // 1=OUTput,  0=INput
//    //  GpioDataRegs.GPACLEAR.bit.GPIO25 = 1;   // uncomment if --> Set Low initially
//    //  GpioDataRegs.GPASET.bit.GPIO25 = 1;     // uncomment if --> Set High initially
//    //--------------------------------------------------------------------------------------
//    //  GPIO-26 - PIN FUNCTION = --Spare--
//    GpioCtrlRegs.GPAMUX2.bit.GPIO26 = 0;    // 0=GPIO,  1=Resv,  2=Resv,  3=SPICLK-B
//    GpioCtrlRegs.GPADIR.bit.GPIO26 = 0;     // 1=OUTput,  0=INput
//    //  GpioDataRegs.GPACLEAR.bit.GPIO26 = 1;   // uncomment if --> Set Low initially
//    //  GpioDataRegs.GPASET.bit.GPIO26 = 1;     // uncomment if --> Set High initially
//    //--------------------------------------------------------------------------------------
//    //  GPIO-27 - PIN FUNCTION = --Spare--
//    GpioCtrlRegs.GPAMUX2.bit.GPIO27 = 0;    // 0=GPIO,  1=Resv,  2=Resv,  3=SPISTE-B
//    GpioCtrlRegs.GPADIR.bit.GPIO27 = 0;     // 1=OUTput,  0=INput
//    //  GpioDataRegs.GPACLEAR.bit.GPIO27 = 1;   // uncomment if --> Set Low initially
//    //  GpioDataRegs.GPASET.bit.GPIO27 = 1;     // uncomment if --> Set High initially
//    //--------------------------------------------------------------------------------------
    //  GPIO-28 - PIN FUNCTION = SCI-RX
    GpioCtrlRegs.GPAMUX2.bit.GPIO28 = 1;    // 0=GPIO,  1=SCIRX-A,  2=I2CSDA-A,  3=TZ2
    //  GpioCtrlRegs.GPADIR.bit.GPIO28 = 1;     // 1=OUTput,  0=INput
    //  GpioDataRegs.GPACLEAR.bit.GPIO28 = 1;   // uncomment if --> Set Low initially
    //  GpioDataRegs.GPASET.bit.GPIO28 = 1;     // uncomment if --> Set High initially
    //--------------------------------------------------------------------------------------
    //  GPIO-29 - PIN FUNCTION = SCI-TX
    GpioCtrlRegs.GPAMUX2.bit.GPIO29 = 1;    // 0=GPIO,  1=SCITXD-A,  2=I2CSCL-A,  3=TZ3
    //  GpioCtrlRegs.GPADIR.bit.GPIO29 = 0;     // 1=OUTput,  0=INput
    //  GpioDataRegs.GPACLEAR.bit.GPIO29 = 1;   // uncomment if --> Set Low initially
    //  GpioDataRegs.GPASET.bit.GPIO29 = 1;     // uncomment if --> Set High initially
//    //--------------------------------------------------------------------------------------
//    //  GPIO-30 - PIN FUNCTION = --Spare--
//    GpioCtrlRegs.GPAMUX2.bit.GPIO30 = 0;    // 0=GPIO,  1=CANRX-A,  2=Resv,  3=Resv
//    GpioCtrlRegs.GPADIR.bit.GPIO30 = 0;     // 1=OUTput,  0=INput
//    //  GpioDataRegs.GPACLEAR.bit.GPIO30 = 1;   // uncomment if --> Set Low initially
//    //  GpioDataRegs.GPASET.bit.GPIO30 = 1;     // uncomment if --> Set High initially
//    //--------------------------------------------------------------------------------------
//    //  GPIO-31 - PIN FUNCTION = LED2 on controlCARD
//    GpioCtrlRegs.GPAMUX2.bit.GPIO31 = 0;    // 0=GPIO,  1=CANTX-A,  2=Resv,  3=Resv
//    GpioCtrlRegs.GPADIR.bit.GPIO31 = 1;     // 1=OUTput,  0=INput
//    //  GpioDataRegs.GPACLEAR.bit.GPIO31 = 1;   // uncomment if --> Set Low initially
//    GpioDataRegs.GPASET.bit.GPIO31 = 1;     // uncomment if --> Set High initially
//    //--------------------------------------------------------------------------------------
//    //  GPIO-32 - PIN FUNCTION = I2CSDA
//    GpioCtrlRegs.GPBMUX1.bit.GPIO32 = 1;    // 0=GPIO,  1=I2CSDA-A,  2=SYNCI,  3=ADCSOCA
//    //  GpioCtrlRegs.GPBDIR.bit.GPIO32 = 1;     // 1=OUTput,  0=INput
//    //  GpioDataRegs.GPBCLEAR.bit.GPIO32 = 1;   // uncomment if --> Set Low initially
//    //  GpioDataRegs.GPBSET.bit.GPIO32 = 1;     // uncomment if --> Set High initially
//    //--------------------------------------------------------------------------------------
//    //  GPIO-33 - PIN FUNCTION = PFC-DrvEnable
//    GpioCtrlRegs.GPBMUX1.bit.GPIO33 = 1;    // 0=GPIO,  1=I2CSCL-A,  2=SYNCO,  3=ADCSOCB
//    //  GpioCtrlRegs.GPBDIR.bit.GPIO33 = 1;     // 1=OUTput,  0=INput
//    //  GpioDataRegs.GPBCLEAR.bit.GPIO33 = 1;   // uncomment if --> Set Low initially
//    //  GpioDataRegs.GPBSET.bit.GPIO33 = 1;     // uncomment if --> Set High initially
//    //--------------------------------------------------------------------------------------
    //  GPIO-34 - PIN FUNCTION = LED3 on controlCARD
    GpioCtrlRegs.GPBMUX1.bit.GPIO34 = 0;    // 0=GPIO,  1=Resv,  2=Resv,  3=Resv
    GpioCtrlRegs.GPBDIR.bit.GPIO34 = 1;     // 1=OUTput,  0=INput
    //  GpioDataRegs.GPBCLEAR.bit.GPIO34 = 1;   // uncomment if --> Set Low initially
    GpioDataRegs.GPBSET.bit.GPIO34 = 1;     // uncomment if --> Set High initially
    //--------------------------------------------------------------------------------------
    //--------------------------------------------------------------------------------------
    // GPIO 35-38 are defaulted to JTAG usage, and are not shown here to enforce JTAG debug
    // usage.
    //--------------------------------------------------------------------------------------
    //--------------------------------------------------------------------------------------
    //  GPIO-39 - PIN FUNCTION = --Spare--
    GpioCtrlRegs.GPBMUX1.bit.GPIO39 = 0;    // 0=GPIO,  1=Resv,  2=Resv,  3=Resv
    GpioCtrlRegs.GPBDIR.bit.GPIO39 = 0;     // 1=OUTput,  0=INput
    //  GpioDataRegs.GPBCLEAR.bit.GPIO39 = 1;   // uncomment if --> Set Low initially
    //  GpioDataRegs.GPBSET.bit.GPIO39 = 1;     // uncomment if --> Set High initially
    //--------------------------------------------------------------------------------------
    //  GPIO-40 - PIN FUNCTION = --Spare--
    GpioCtrlRegs.GPBMUX1.bit.GPIO40 = 0;    // 0=GPIO,  1=EPWM7A,  2=Resv,  3=Resv
    GpioCtrlRegs.GPBDIR.bit.GPIO40 = 0;     // 1=OUTput,  0=INput
    //  GpioDataRegs.GPBCLEAR.bit.GPIO40 = 1;   // uncomment if --> Set Low initially
    //  GpioDataRegs.GPBSET.bit.GPIO40 = 1;     // uncomment if --> Set High initially
    //--------------------------------------------------------------------------------------
    //  GPIO-41 - PIN FUNCTION = --Spare--
    GpioCtrlRegs.GPBMUX1.bit.GPIO41 = 0;    // 0=GPIO,  1=EPWM7B,  2=Resv,  3=Resv
    GpioCtrlRegs.GPBDIR.bit.GPIO41 = 0;     // 1=OUTput,  0=INput
    //  GpioDataRegs.GPBCLEAR.bit.GPIO41 = 1;   // uncomment if --> Set Low initially
    //  GpioDataRegs.GPBSET.bit.GPIO41 = 1;     // uncomment if --> Set High initially
    //--------------------------------------------------------------------------------------
    //  GPIO-42 - PIN FUNCTION = LED2
    GpioCtrlRegs.GPBMUX1.bit.GPIO42 = 0;    // 0=GPIO,  1=Resv,  2=Resv,  3=COMP1OUT
    GpioCtrlRegs.GPBDIR.bit.GPIO42 = 1;     // 1=OUTput,  0=INput
    //  GpioDataRegs.GPBCLEAR.bit.GPIO42 = 1;   // uncomment if --> Set Low initially
    GpioDataRegs.GPBSET.bit.GPIO42 = 1;     // uncomment if --> Set High initially
    //--------------------------------------------------------------------------------------
    //  GPIO-43 - PIN FUNCTION = RST_VHS
    GpioCtrlRegs.GPBMUX1.bit.GPIO43 = 0;    // 0=GPIO,  1=Resv,  2=Resv,  3=COMP2OUT
    GpioCtrlRegs.GPBDIR.bit.GPIO43 = 1;     // 1=OUTput,  0=INput
    GpioDataRegs.GPBCLEAR.bit.GPIO43 = 1;   // uncomment if --> Set Low initially
    //  GpioDataRegs.GPBSET.bit.GPIO43 = 1;     // uncomment if --> Set High initially
    //--------------------------------------------------------------------------------------
    //  GPIO-44 - PIN FUNCTION = LED1
    GpioCtrlRegs.GPBMUX1.bit.GPIO44 = 0;    // 0=GPIO,  1=Resv,  2=Resv,  3=Resv
    GpioCtrlRegs.GPBDIR.bit.GPIO44 = 1;     // 1=OUTput,  0=INput
    //  GpioDataRegs.GPBCLEAR.bit.GPIO44 = 1;   // uncomment if --> Set Low initially
    GpioDataRegs.GPBSET.bit.GPIO44 = 1;     // uncomment if --> Set High initially
    //--------------------------------------------------------------------------------------
    //--------------------------------------------------------------------------------------
    EDIS;

    return NO_ERROR;
}

static int32_t device_initGPIO3phInterleaved(void)
{
    EALLOW;
}

void updateDutyEPwm(uint16_t duty)
{
    EPwm1Regs.CMPA.half.CMPA = duty;
    EPwm2Regs.CMPA.half.CMPA = duty;
    EPwm3Regs.CMPA.half.CMPA = duty;
}


// interrupt routine for led blinking
__interrupt void cpu_timer0_isr(void)
{
    CpuTimer0.InterruptCount++;
    // Toggle GPIO34 once per 500 milliseconds


    // Acknowledge this interrupt to receive more interrupts from group 1
    PieCtrlRegs.PIEACK.all = PIEACK_GROUP1;
}




/* Error handling function to be called when an ASSERT is violated */
void __error__(char *filename, uint32_t line)
{
    //
    // An ASSERT condition was evaluated as false. You can use the filename and
    // line parameters to determine what went wrong.
    //
    ESTOP0;
}


__interrupt void ISR_ILLEGAL(void)   // Illegal operation TRAP
{
    // Insert ISR Code here
    //TODO: all gate drivers off

    // Next two lines for debug only to halt the processor here
    // Remove after inserting ISR Code
    asm("          ESTOP0");
    for(;;);

}

