//*****************************************************************************
//
// Copyright (C) 2014 Texas Instruments Incorporated - http://www.ti.com/
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions
// are met:
//
//  Redistributions of source code must retain the above copyright
//  notice, this list of conditions and the following disclaimer.
//
//  Redistributions in binary form must reproduce the above copyright
//  notice, this list of conditions and the following disclaimer in the
//  documentation and/or other materials provided with the
//  distribution.
//
//  Neither the name of Texas Instruments Incorporated nor the names of
//  its contributors may be used to endorse or promote products derived
//  from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
// MSP432 Low Power main.c
//
//           Description: Source code for the implementation of the Bosch BoosterPack with
//           an MSP432P401R launchpad for low power consumption. BoosterPack includes:
//           - Inertial Measurement Unit (IMU) sensor with an accelerometer and gyroscope
//           - Magnetometer
//           - Environmental sensor with pressure, ambient temperature and humidity
//           - Ambient light sensor
//           - Infrared temperature sensor
//
// Adapted by Michael Arriete
// Further modified by Richard W. Kelnhofer and Anirudh Mallya
//
//****************************************************************************
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <ti/devices/msp432p4xx/inc/msp432.h>
#include "stdio.h"
#include <ti/devices/msp432p4xx/driverlib/driverlib.h>
#include "i2c_driver.h"
#include "demo_sysctl.h"
#include "bmi160_support.h"
#include "bme280_support.h"
#include "tmp007.h"
#include "opt3001.h"
#include "uart_driver.h"
#include <stdio.h>
//#include "msp432.h"
#include "msoe_lib_clk.h"
//#include "msoe_lib_clk.c"

//***** Definitions *****
#define              CPU_FREQ       (48000000) //48MHz
#define USING_BOSCH_BP
#define NUM_AVGR_SUMS                         3 //x^2 frames

#define WDT_A_TIMEOUT RESET_SRC_1
#define INT_TA2N_BIT (1<<13)
#define INT_TA1N_BIT (1<<13)
#define INT_TA3N_BIT (1<<13)
//***** Function Prototypes *****
void startCrystalOscillator(void);
void setSystemClock(uint32_t CPU_Frequency);
void configureGPIO(void);
void startWakeUpTimerA(uint16_t ulClockMS);
void stopWakeUpTimerA(void);
int32_t movingAvg(int prevAvg, int16_t newValue);
float invSqrt(float x);
//
//
//***** Global Data *****
const uint8_t wdtWakeUpPeriod [8] = {
             WDT_A_CLOCKDIVIDER_2G,
             WDT_A_CLOCKDIVIDER_128M,
             WDT_A_CLOCKDIVIDER_8192K,
             WDT_A_CLOCKDIVIDER_512K,
             WDT_A_CLOCKDIVIDER_32K,
             WDT_A_CLOCKDIVIDER_8192,
             WDT_A_CLOCKDIVIDER_512,
             WDT_A_CLOCKDIVIDER_64,
};
//
//Default ~ 1 seconds  1/32KHz * WDT_A_CLOCKDIVIDER_32K
volatile uint8_t wdtWakeUpPeriodIndex = 4;

// BMI160/BMM150
BMI160_RETURN_FUNCTION_TYPE returnValue;
struct bmi160_gyro_t        s_gyroXYZ;
struct bmi160_accel_t       s_accelXYZ;
struct bmi160_mag_xyz_s32_t s_magcompXYZ;
s32 returnRslt;

uint16_t WDTcount = 0;
uint16_t secondcount = 0;
u8 gyro_range;
u8 gyro_range_set = 0x04; //GyroRange to 125 degrees/second, sensitivity of 262.4LSB/degree/sec

//Calibration off-sets
int8_t accel_off_x;
int8_t accel_off_y;
int8_t accel_off_z;
s16 gyro_off_x=0.0;
s16 gyro_off_y=0.0;
s16 gyro_off_z=0.0;

//Initialize the values for the Accel
float a_x = 0.0;
float a_y = 0.0;
float a_z = 0.0;

//Initialize the values for Gyro
float w_x = 0.0;
float w_y = 0.0;
float w_z = 0.0;

//Initialize the values for Magnetometer
float m_x = 0.0;
float m_y = 0.0;
float m_z = 0.0;

//Initialize Euler Angles
float g_phi=0.0; //rotation about x-axis due to gyro, roll.
float g_theta=0.0;   //rotation about y-axis due to gyro, pitch.
float g_psi=0.0;   //rotation about z-axis due to gyro, yaw.
float g_phidot = 0.0;
float g_thetadot = 0.0;
float g_psidot = 0.0;

float sampleRate = 10000; //This is based on the WDT divider
//
//
//Initialize the angles for the Accel
float a_phi=0.0;
float a_theta=0.0;
float a_psi = 0.0;
float g_thetafinal=0.0;
float a_psifinal = 0.0;
//OPT3001
uint16_t rawData; //raw data from the Optical Sensor.  It's possible to use this to set LED PWM
float    convertedLux; //This is data converted to Lux.

// TMP007
uint16_t rawTemp;
uint16_t rawObjTemp;
float    tObjTemp;
float    tObjAmb;



uint16_t gyroAbsX, gyroAbsY, gyroAbsZ;
uint16_t deltaAccelX, deltaAccelY, deltaAccelZ;
int16_t prevAccelX = 0;
int16_t prevAccelY = 0;
int16_t prevAccelZ = 0;
int16_t prevGyroX = 0;
int16_t prevGyroY = 0;
int16_t prevGyroZ = 0;
int16_t stillCount = 0;
int32_t gyroAvgX = 0.0;
int32_t gyroAvgY = 0.0;
int32_t gyroAvgZ = 0.0;
int32_t accelAvgX = 0.0;
int32_t accelAvgY = 0.0;
int32_t accelAvgZ = 0.0;

//Sensor Status Variables
bool BME_on = false;
bool BMI_on = true;
bool TMP_on = true;
bool OPT_on = true;


/***********************************************************
  Function:
*/
void init_TA(void);
int main(void)
{
    float cp,ct, sp, st; //trig function variables
    float sensitivity = 262.4*(180.0/3.14150); //sensitivity in lsb/(rad/sec)
    float recipNorm = 1.0;
    uint8_t inputval;
    P1->DIR &= ~BIT1;     // make P1.1 input
    P1->OUT |= BIT1;    // pull up resistor
    P1->REN |= BIT1;    // enable pullup on P1.1

    //setup pin P6.6 as TA2-OUT3 for DC servo-1
        Clock_Init_48MHz();
        init_TA();

    volatile uint32_t index;
       // Stop WDT and disabling master interrupts
       MAP_WDT_A_holdTimer();
       MAP_Interrupt_disableMaster();

       // Enabling SRAM Bank Retention
       SYSCTL->SRAM_BANKRET |= SYSCTL_SRAM_BANKEN_BNK7_EN;
       for (index = 0;index < 100;index++);
       MAP_SysCtl_enableSRAMBankRetention(SYSCTL_SRAM_BANK1);

#ifdef USE_LPM
       // Configure I/O to minimize power consumption
       configureGPIO();

       // Turn off PSS high-side & low-side supervisors to minimize power consumption
       MAP_PSS_disableLowSide();
       MAP_PSS_disableHighSide();
#endif

       // Configure clocks
    startCrystalOscillator();
       setSystemClock(CPU_FREQ);
       printf("Clocks are started\t%d\n",1);
#ifdef USING_BOSCH_BP
       // Initialize i2c
       initI2C();
       printf("I2C started \t%d\n",1);
#endif

    //
    MAP_WDT_A_initIntervalTimer(WDT_A_CLOCKSOURCE_BCLK,WDT_A_CLOCKDIVIDER_64); //Use the BCLK for LPM3 mode
    // Using the wdt as interval timer to wake up from LPM3
    //enable WDT A for interupts
    MAP_Interrupt_enableInterrupt(INT_WDT_A);
    //MAP_Interrupt_enableSleepOnIsrExit();
    MAP_Interrupt_enableMaster();
    printf("WDT enabled\t%d\n",1);

#ifdef USING_BOSCH_BP
       // Initialize bmi160 sensor //rwk 20171122 We will need to switch from APPLICATION_NAVIGATION to APPLICATION_HEAD_TRACKING
       //The sample rate for APPLICATION_HEAD_TRACKING is 1600Hz for accel and gyro.  See bmi160_support.c
    //still need to determine gyro range
       bmi160_initialize_sensor();
       returnRslt = bmi160_config_running_mode(APPLICATION_HEAD_TRACKING);
       bmi160_accel_foc_trigger_xyz(0x03, 0x03, 0x01, &accel_off_x, &accel_off_y, &accel_off_z);
       bmi160_get_gyro_range(&gyro_range);
       //printf("\nGyro Range:%d",gyro_range);
       bmi160_set_gyro_range(gyro_range_set);
       bmi160_get_gyro_range(&gyro_range);
       //printf("\nNow Gyro Range is:%d",gyro_range);
       bmi160_set_foc_gyro_enable(0x01, &gyro_off_x, &gyro_off_y, &gyro_off_z);
       //printf("\rGyroOffX: %d,\tGyroOffY: %d,\tGyroOffZ: %d",gyro_off_x,gyro_off_y,gyro_off_z);
       //printf("\nBMI160 initialized\t%d\n",1);
       // Initialize bme280 sensor
       bme280_data_readout_template();
       returnRslt = bme280_set_power_mode(BME280_SLEEP_MODE);
       //printf("BME280 initialized\t%d\n",1);
       //Initialize opt3001 sensor
       sensorOpt3001Init();
       //printf("Opt3001 initialized\t%d\n",1);
       //Initialize tmp007 sensor
       sensorTmp007Init();
       //printf("temp sensor initialized\t%d\n",1);
#endif

       //Enable TMP, OPT, and BME sensors
       sensorTmp007Enable(true);
       sensorOpt3001Enable(true);
       returnRslt = bme280_set_power_mode(BME280_NORMAL_MODE);
       //
       MAP_WDT_A_startTimer();
       printf("WDT_A started \t%d\n",1);

       while(1) //Here's the main loop. We just sit here until WDT_A interupts
       {
           inputval = P1->IN & BIT1;
       if(inputval==0){
               TIMER_A1->CCR[3] = 275.5;
               TIMER_A2->CCR[3] = 275.5;}

       else if(inputval!=0){

               //Mag update
           if(BMI_on)
           {
                 //Read Mag value (BMM) through BMI
                 returnValue = bmi160_bmm150_mag_compensate_xyz(&s_magcompXYZ);
                 //printf("\rMagX:\s%d,\tMagY:\s%d,\tMagZ:\s%d",s_magcompXYZ.x,s_magcompXYZ.y,s_magcompXYZ.z);
           }
           //
           //Accel and Gyro update
           if(BMI_on)
           {
                 //Read Accel and Gyro values
                 returnValue = bmi160_read_accel_xyz(&s_accelXYZ);
                 returnValue = bmi160_read_gyro_xyz(&s_gyroXYZ);

                 //Read Mag value (BMM) through BMI
                 returnValue = bmi160_bmm150_mag_compensate_xyz(&s_magcompXYZ);
                 m_x = (float)(s_magcompXYZ.x);
                 m_y = (float)(s_magcompXYZ.y);
                 m_z = (float)(s_magcompXYZ.z);
                 //Compute values based on gyroscope and accelerometer.
                 w_x = (float)(s_gyroXYZ.x)/sensitivity;
                 w_y = (float)(s_gyroXYZ.y)/sensitivity;
                 w_z = (float)(s_gyroXYZ.z)/sensitivity;
                 //
                 a_x = (float)(s_accelXYZ.x)/16384.0;
                 a_y = (float)(s_accelXYZ.y)/16384.0;
                 a_z = (float)(s_accelXYZ.z)/16384.0;

                 //normalize
                 recipNorm = invSqrt(a_x*a_x + a_y*a_y + a_z*a_z);
                 a_x *= recipNorm;
                 a_y *= recipNorm;
                 a_z *= recipNorm;

                 //trigonometric functions assigned to variables
                 cp = cos(g_phi);
                 ct = cos(g_theta);
                 sp = sin(g_phi);
                 st = sin(g_theta);

                 //calculate gyro Euler angles
                 g_phidot = w_x+(sp*st*w_y + cp*st*w_z)/ct;
                 g_thetadot = cp*w_y - sp*w_z;
                 g_psidot = (sp*w_y+cp*w_z)/ct;
                 g_phi += g_phidot*.004;
                 g_theta -= g_thetadot*.004;
                 g_psi += g_psidot*.008;

                 //Calculate accelerometer roll and pitch
                 a_phi = atan2(-a_y,-a_z);
                 a_theta = asin(-a_x);
                 //variables needed for calculating psi
                 float By2 = m_z * sin(a_theta) - m_y * cos(a_theta);
                 float Bz2 = m_y * sin(a_theta) + m_z * cos(a_theta);
                 float Bx3 = m_x * cos(a_phi) + Bz2*sin(a_phi);
                 //Calculate accelerometer yaw
                 a_psi = atan2(By2, Bx3);

                 //final angle computed by combinatorial filter
                 g_phi = 0.99*g_phi + 0.01*a_phi;
                 g_theta = 0.99*g_theta + 0.01*a_theta;
                 g_thetafinal = g_theta*60;
                 a_psifinal = a_psi*57.1;
           }}
           //TMP update
           if(TMP_on)
                       {
                           // Read/convert tmp007 data
                           sensorTmp007Read(&rawTemp, &rawObjTemp);
                           sensorTmp007Convert(rawTemp, rawObjTemp, &tObjTemp, &tObjAmb);
                                       }
           //OPT update
           if(OPT_on)
                       {
                           //Read and convert OPT values
                           sensorOpt3001Read(&rawData);
                           sensorOpt3001Convert(rawData, &convertedLux);
                       }
       WDTcount++;
       if(WDTcount == 250)
       {
           WDTcount = 0;
           secondcount++;
           //print acc due to g experienced on each axes
           printf("\na_x:%f,\ta_y:%f,\ta_z:%f\n",a_x,a_y,a_z);
           //print Pitch and Yaw angles, we dont care about roll
           printf("\rPitch:%f,\tYaw:%f\n",g_thetafinal,a_psifinal);
           printf("Magnetometer(%lf, %lf, %lf)\n", m_x, m_y, m_z);
           //contrsuct JSON string for TMP
           printf("object temp in C\":%5.2f\n",tObjTemp);
           //contrsuct JSON string for OPT
           printf("lum intensity in lux\":%5.2f\n",convertedLux);
           printf("\nElapsed time in seconds \t%d\n",secondcount);
       }
       //
       // For LPM3 Clock Source should be BCLK or VLOCLK
       MAP_PCM_gotoLPM3();
       if(g_thetafinal>40 && g_thetafinal<50){
           TIMER_A2->CCR[3] = 190;     }
       if(g_thetafinal>30 && g_thetafinal<40){
           TIMER_A2->CCR[3] = 209;      }
       if(g_thetafinal>20 && g_thetafinal<30){
           TIMER_A2->CCR[3] = 228;      }
       if(g_thetafinal>10 && g_thetafinal<20){
           TIMER_A2->CCR[3] = 247;      }
       if(g_thetafinal>0 && g_thetafinal<10){
           TIMER_A2->CCR[3] = 266;      }
       if(g_thetafinal>-10 && g_thetafinal<0){
           TIMER_A2->CCR[3] = 285;      }
       if(g_thetafinal>-20 && g_thetafinal<-10){
           TIMER_A2->CCR[3] = 304;      }
       if(g_thetafinal>-30 && g_thetafinal<-20){
           TIMER_A2->CCR[3] = 323;      }
       if(g_thetafinal>-40 && g_thetafinal<-30){
           TIMER_A2->CCR[3] = 342;      }
       if(g_thetafinal>-50 && g_thetafinal<-40){
           TIMER_A2->CCR[3] = 361;      }
       /*if(a_psifinal>40 && a_psifinal<50){
           TIMER_A1->CCR[3] = 361;      }
       if(a_psifinal>30 && a_psifinal<40){
           TIMER_A1->CCR[3] = 342;      }
       if(a_psifinal>20 && a_psifinal<30){
           TIMER_A1->CCR[3] = 323;      }
       if(a_psifinal>10 && a_psifinal<20){
           TIMER_A1->CCR[3] = 304;      }
       if(a_psifinal>0 && a_psifinal<10){
           TIMER_A1->CCR[3] = 285;      }
       if(a_psifinal>-10 && a_psifinal<0){
           TIMER_A1->CCR[3] = 266;      }
       if(a_psifinal>-20 && a_psifinal<-10){
           TIMER_A1->CCR[3] = 247;      }
       if(a_psifinal>-30 && a_psifinal<-20){
           TIMER_A1->CCR[3] = 228;      }
       if(a_psifinal>-40 && a_psifinal<-30){
           TIMER_A1->CCR[3] = 209;      }
       if(a_psifinal>-50 && a_psifinal<-40){
           TIMER_A1->CCR[3] = 190;      }*/

       if(convertedLux >3500){
           TIMER_A3->CCR[3] = 0;
       }
       if(convertedLux <3500 && convertedLux >=1225){
           TIMER_A3->CCR[3] = 1900;
       }
       if(convertedLux <1225 && convertedLux >=525){
           TIMER_A3->CCR[3] = 2280;
       }
       if(convertedLux <525 && convertedLux >=320){
           TIMER_A3->CCR[3] = 2660;
       }
       if(convertedLux <320 && convertedLux >=260){
           TIMER_A3->CCR[3] = 3040;
       }
       if(convertedLux <260){
           TIMER_A3->CCR[3] = 3420;
       }

       }//end while (1)
} //end main

/***********************************************************
  Function:

   Works as a simple moving average filter. Used for gesture recognition.

*/
int32_t movingAvg(int prevAvg, int16_t newValue)
{
       return (((prevAvg << NUM_AVGR_SUMS) + newValue - prevAvg) >> NUM_AVGR_SUMS);



}

/***********************************************************
  Function:

   The following function is responsible for starting XT1 in the
   MSP432 that is used to source the internal FLL that drives the
   MCLK and SMCLK.
*/
void startCrystalOscillator(void)
{
       /* Configuring pins for peripheral/crystal HFXT*/
   MAP_GPIO_setAsPeripheralModuleFunctionOutputPin(GPIO_PORT_PJ,
                    GPIO_PIN3 | GPIO_PIN4, GPIO_PRIMARY_MODULE_FUNCTION);

       /* Configuring pins for peripheral/crystal LFXT*/
   MAP_GPIO_setAsPeripheralModuleFunctionOutputPin(GPIO_PORT_PJ,
                    GPIO_PIN0 | GPIO_PIN1, GPIO_PRIMARY_MODULE_FUNCTION);
}

/***********************************************************
  Function:

   The following function is responsible for setting up the system
   clock at a specified frequency.
*/
void setSystemClock(uint32_t CPU_Frequency)
{
    /* Setting the external clock frequency. This API is optional, but will
    * come in handy if the user ever wants to use the getMCLK/getACLK/etc
    * functions
    */
    MAP_CS_setExternalClockSourceFrequency(32768, CPU_Frequency);
       MAP_CS_setReferenceOscillatorFrequency(CS_REFO_32KHZ);

    /* Before we start we have to change VCORE to 1 to support the 24MHz frequency */
    MAP_PCM_setCoreVoltageLevel(PCM_AM_LDO_VCORE0);
    MAP_FlashCtl_setWaitState(FLASH_BANK0, 1);
    MAP_FlashCtl_setWaitState(FLASH_BANK1, 1);

    /* Starting HFXT and LFXT in non-bypass mode without a timeout. */
    MAP_CS_startHFXT(false);
    MAP_CS_startLFXT(false);

    /* Initializing the clock source as follows:
    *      MCLK = HFXT/2 = 24MHz
    *      ACLK = LFXT = 32KHz
    *      HSMCLK = HFXT/4 = 6MHz
    *      SMCLK = HFXT/2 = 12MHz
    *      BCLK  = REFO = 32kHz
    */
    MAP_CS_initClockSignal(CS_MCLK, CS_HFXTCLK_SELECT, CS_CLOCK_DIVIDER_2);
    MAP_CS_initClockSignal(CS_ACLK, CS_LFXTCLK_SELECT, CS_CLOCK_DIVIDER_1);
    MAP_CS_initClockSignal(CS_HSMCLK, CS_HFXTCLK_SELECT, CS_CLOCK_DIVIDER_8);
    MAP_CS_initClockSignal(CS_SMCLK, CS_HFXTCLK_SELECT, CS_CLOCK_DIVIDER_4);
    MAP_CS_initClockSignal(CS_BCLK, CS_REFOCLK_SELECT, CS_CLOCK_DIVIDER_1);
}

/***********************************************************
  Function:
*/
void configureGPIO(void)
{
       /* Configure I/O to minimize power consumption before going to sleep */
       MAP_GPIO_setOutputLowOnPin(GPIO_PORT_P1, PIN_ALL8);
       MAP_GPIO_setOutputLowOnPin(GPIO_PORT_P2, PIN_ALL8);
       MAP_GPIO_setOutputLowOnPin(GPIO_PORT_P3, PIN_ALL8);
       MAP_GPIO_setOutputLowOnPin(GPIO_PORT_P4, PIN_ALL8);
       MAP_GPIO_setOutputLowOnPin(GPIO_PORT_P5, PIN_ALL8);
       MAP_GPIO_setOutputLowOnPin(GPIO_PORT_P6, PIN_ALL8);
       MAP_GPIO_setOutputLowOnPin(GPIO_PORT_P7, PIN_ALL8);
       MAP_GPIO_setOutputLowOnPin(GPIO_PORT_P8, PIN_ALL8);
       MAP_GPIO_setOutputLowOnPin(GPIO_PORT_P9, PIN_ALL8);
       MAP_GPIO_setOutputLowOnPin(GPIO_PORT_P10, PIN_ALL8);
       MAP_GPIO_setOutputLowOnPin(GPIO_PORT_PJ, GPIO_PIN0 | GPIO_PIN1 | GPIO_PIN2 | GPIO_PIN3);
       MAP_GPIO_setAsOutputPin(GPIO_PORT_P1, PIN_ALL8);
       MAP_GPIO_setAsOutputPin(GPIO_PORT_P2, PIN_ALL8);
       MAP_GPIO_setAsOutputPin(GPIO_PORT_P3, (PIN_ALL8 & ~GPIO_PIN6));
       MAP_GPIO_setAsOutputPin(GPIO_PORT_P4, (PIN_ALL8 & ~(GPIO_PIN1 | GPIO_PIN6)));
       MAP_GPIO_setAsOutputPin(GPIO_PORT_P5, (PIN_ALL8 & ~(GPIO_PIN0 | GPIO_PIN2)));
       MAP_GPIO_setAsOutputPin(GPIO_PORT_P6, (PIN_ALL8 & ~GPIO_PIN7));
       MAP_GPIO_setAsOutputPin(GPIO_PORT_P7, PIN_ALL8);
       MAP_GPIO_setAsOutputPin(GPIO_PORT_P8, PIN_ALL8);
       MAP_GPIO_setAsOutputPin(GPIO_PORT_P9, PIN_ALL8);
       MAP_GPIO_setAsOutputPin(GPIO_PORT_P10, PIN_ALL8);
       MAP_GPIO_setAsOutputPin(GPIO_PORT_PJ, GPIO_PIN0 | GPIO_PIN1 | GPIO_PIN2 | GPIO_PIN3);
}

/***********************************************************
  Function:
*/
void startWakeUpTimerA(uint16_t ulClockMS)
{
       ulClockMS = (ulClockMS * 32768)/1000;

       /* TimerA UpMode Configuration Parameter */
       Timer_A_UpModeConfig upConfig =
       {
                    TIMER_A_CLOCKSOURCE_ACLK,              // ACLK Clock Source
                    TIMER_A_CLOCKSOURCE_DIVIDER_1,         // ACLK/1 = 32KHz
                    ulClockMS,                             // tick period
                    TIMER_A_TAIE_INTERRUPT_DISABLE,        // Disable Timer interrupt
                    TIMER_A_CCIE_CCR0_INTERRUPT_ENABLE ,   // Enable CCR0 interrupt
                    TIMER_A_SKIP_CLEAR                     // Clear value
       };

       MAP_Timer_A_configureUpMode(TIMER_A0_BASE, &upConfig);
       MAP_Timer_A_enableCaptureCompareInterrupt(TIMER_A0_BASE,
                    TIMER_A_CAPTURECOMPARE_REGISTER_0);

       MAP_Interrupt_enableInterrupt(INT_TA0_0);
       MAP_Timer_A_startCounter(TIMER_A0_BASE,TIMER_A_UP_MODE);
}

/***********************************************************
  Function:
*/
void stopWakeUpTimerA(void)
{
       MAP_Interrupt_disableInterrupt(INT_TA0_0);
       MAP_Timer_A_stopTimer(TIMER_A0_BASE);
}

/***********************************************************
  Function: TA0_0_IRQHandler
*/
void TA0_0_IRQHandler(void)
{
       MAP_Timer_A_clearCaptureCompareInterrupt(TIMER_A0_BASE,
                    TIMER_A_CAPTURECOMPARE_REGISTER_0);

#ifdef USE_LPM
       MAP_Interrupt_disableSleepOnIsrExit();
#endif
}

/***********************************************************
  Function: WDT_A_IRQHandler
*/
void WDT_A_IRQHandler(void)
{
       //MAP_GPIO_setOutputHighOnPin(GPIO_PORT_P1, GPIO_PIN0);
       // Waking up from LMP3 take us to PCM_AM_LDO_VCORE0 instead of PCM_AM_LF_VCORE0
//     MAP_PCM_setPowerState(PCM_AM_LDO_VCORE0);
//    MAP_PCM_setCoreVoltageLevel(PCM_AM_DCDC_VCORE0);

#ifdef USE_LPM
       MAP_Interrupt_disableSleepOnIsrExit();
#endif
}

/***********************************************************
  Function: PORT1_IRQHandler
*/
void PORT1_IRQHandler(void)
{
       uint32_t debounce;
       uint32_t status;

       MAP_GPIO_setOutputHighOnPin(GPIO_PORT_P1, GPIO_PIN0);

       status = MAP_GPIO_getEnabledInterruptStatus(GPIO_PORT_P1);

       if(status & GPIO_PIN1)
       {
       }

       /* Delay for switch debounce */
       for(debounce = 0; debounce < 10000; debounce++)
             MAP_GPIO_clearInterruptFlag(GPIO_PORT_P1, status);

#ifdef USE_LPM
       MAP_Interrupt_disableSleepOnIsrExit();
#endif
}

/***********************************************************
  Function: PORT5_IRQHandler
*/
void PORT5_IRQHandler(void)
{
       uint32_t status;

       status = MAP_GPIO_getEnabledInterruptStatus(GPIO_PORT_P5);

       MAP_GPIO_disableInterrupt(GPIO_PORT_P5, GPIO_PIN2);
       MAP_Interrupt_disableInterrupt(INT_PORT5);

       if(status & GPIO_PIN2)
       {
       }

       /* Delay for switch debounce */
       MAP_GPIO_clearInterruptFlag(GPIO_PORT_P5, status);

#ifdef USE_LPM
       MAP_Interrupt_disableSleepOnIsrExit();
#endif
}

/***********************************************************
  Function: _system_pre_init
*/
int _system_pre_init(void)
{
       // stop WDT
       MAP_WDT_A_holdTimer();                        // Hold watchdog timer

       // Perform C/C++ global data initialization
       return 1;
}
// Fast inverse square-root
// See: http://en.wikipedia.org/wiki/Fast_inverse_square_root

float invSqrt(float x) {
    float halfx = 0.5f * x;
    float y = x;
    long i = *(long*)&y;
    i = 0x5f3759df - (i>>1);
    y = *(float*)&i;
    y = y * (1.5f - (halfx * y * y));
    return y;
}

//Function: Initialize TA
void init_TA(void)
{
    // init TA2 for 250 ms interrupt
    P6->SEL0 |= 0x40;
    P6->SEL1 &=~0x10;
    P6->DIR |= 0x40;
    // init TA1 for out3
    P7->SEL0 |= 0x20;
    P7->SEL1 &=~0x20;
    P7->DIR |= 0x20;

    //should provide an 80kHz pwm output to P9.2 and we can change frequency later. init TA3
    P9->SEL0 |= 0x04;
    P9->SEL1 &=~0x10;
    P9->DIR |= 0x04;

    TIMER_A3->CTL |= 0x0210|TIMER_A_CTL_ID__8;
    TIMER_A3->CCTL[3] = 0x00E0;
    TIMER_A3->CCR[0]=  3800;
    TIMER_A3->CCR[3] = 0;
    TIMER_A2->CTL |= 0x0210|TIMER_A_CTL_ID__8;
    TIMER_A2->CCTL[3] = 0x00E0;
    TIMER_A2->CCR[0]=  3800;
    TIMER_A2->CCR[3] = 0;
    TIMER_A2->EX0 |= TIMER_A_EX0_IDEX__8; //divide clock by another factor of 8
    TIMER_A1->CTL |= 0x0210|TIMER_A_CTL_ID__8;
    TIMER_A1->CCTL[3] = 0x00E0;
    TIMER_A1->CCR[0]=  3800;
    TIMER_A1->CCR[3] = 0;  // 250 ms 157
    TIMER_A1->EX0 |= TIMER_A_EX0_IDEX__8; //divide clock by another factor of 8
}

