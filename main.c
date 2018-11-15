/**
 * main.c
 *
 * ECE 3849 Lab 0 Starter Project
 * This version is using the new hardware for B2017: the EK-TM4C1294XL LaunchPad with BOOSTXL-EDUMKII BoosterPack.
 *
 */

#include <stdint.h>
#include <stdbool.h>
#include "driverlib/fpu.h"
#include "driverlib/sysctl.h"
#include "driverlib/interrupt.h"
#include "driverlib/timer.h"
#include "inc/hw_memmap.h"
#include "Crystalfontz128x128_ST7735.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "buttons.h"

#define HALF_SCREEN 64 //the half screen size

//ADC sample scaling
#define ADC_OFFSET         2045 //(2810 + 1310)/2;
#define VIN_RANGE          3.3  //total Vin range in volts
#define PIXELS_PER_DIV     20   //LCD pixels per voltage division
#define ADC_BITS           12   //??number of bits in the ADC sample (look it up in the datasheet)


//----CPU load counters--------
uint32_t count_unloaded = 0;
uint32_t count_loaded = 0;
float cpu_load = 0.0;

uint32_t gSystemClock; // [Hz] system clock frequency
volatile uint32_t gTime = 8345; // time in hundredths of a second
volatile uint16_t localBuffer[HALF_SCREEN * 2]; //local buffer for saving triggers

volatile int isRising = 0;
//-----volt---------
volatile int index_volt = 0;
const char * const gVoltageScaleStr[] = {
                                         "100 mV", "200 mV", "500 mV", "1 V"};
double gVoltageScaleFloat[] = {0.1, 0.2, 0.5, 1};

uint32_t int_to_int(uint32_t k) { //changes to binary using recursion
    if (k == 0) return 0; // gets 0
    if (k == 1) return 1; // gets 1
    return (k % 2) + 10 * int_to_int(k / 2); //Iteration
}


uint32_t cpu_load_count(void);

int main(void)
{
    //    uint32_t minutes, seconds, fseconds = 0;

    IntMasterDisable();

    // Enable the Floating Point Unit, and permit ISRs to use it
    FPUEnable();
    FPULazyStackingEnable();

    // Initialize the system clock to 120 MHz
    gSystemClock = SysCtlClockFreqSet(SYSCTL_XTAL_25MHZ | SYSCTL_OSC_MAIN | SYSCTL_USE_PLL | SYSCTL_CFG_VCO_480, 120000000);

    Crystalfontz128x128_Init(); // Initialize the LCD display driver
    Crystalfontz128x128_SetOrientation(LCD_ORIENTATION_UP); // set screen orientation

    tContext sContext;
    GrContextInit(&sContext, &g_sCrystalfontz128x128); // Initialize the grlib graphics context
    GrContextFontSet(&sContext, &g_sFontFixed6x8); // select font

    // full-screen rectangle
    tRectangle rectFullScreen = {0, 0, GrContextDpyWidthGet(&sContext)-1, GrContextDpyHeightGet(&sContext)-1};
    ButtonInit(); //initialize the button

    //////////////CPU Load/////////////////
    SysCtlPeripheralEnable(SYSCTL_PERIPH_TIMER3);
    TimerDisable(TIMER3_BASE, TIMER_BOTH);
    TimerConfigure(TIMER3_BASE, TIMER_CFG_ONE_SHOT);
    TimerLoadSet(TIMER3_BASE, TIMER_A, gSystemClock/100 - 1); // 10 ms interval

    count_unloaded = cpu_load_count();

    IntMasterEnable();


    //================================while(1)=================================
    while (1) {
        int32_t curIndex = gADCBufferIndex - HALF_SCREEN;
        int32_t curIndexTemp = curIndex;
        //        uint16_t lowVal=1310, highVal=2810;
        uint16_t trigLevel = ADC_OFFSET;
        uint16_t foundflag = 0;

        char button_ID = 'N';
        int x = 0;
        int readIndex = 0;
        float fVoltsPerDiv;
        GrContextForegroundSet(&sContext, ClrOrange); // Blue text

        //////////////BUTTONS//////////////////////
        while (fifo_get(&button_ID) == 1){
            switch (button_ID){
            //>>>>>right case>>>>
            case 'D':
                index_volt++;
                fVoltsPerDiv = gVoltageScaleFloat[index_volt %= 4];
                break;
                //<<<<<left case<<<<<
            case 'A':
                index_volt--;
                //                if (index_volt<0) index_volt=3;
                index_volt = index_volt<0 ? 3: index_volt;
                fVoltsPerDiv = gVoltageScaleFloat [index_volt %= 4];
                break;

            case 'W':
                isRising = 1;
                break;

            case 'S':
                isRising = 0;
                break;

                //No read
            case 'N':
                fVoltsPerDiv = gVoltageScaleFloat[index_volt %= 4];
                break;

            }
        }


        //traverse the right half of gADCBuffer to find the trigger
        while ((foundflag != 1) && (curIndex >= 1024)){
            uint16_t curVal = gADCBuffer[curIndex];
            uint16_t priorVal = gADCBuffer[curIndex - 1];
            uint16_t nextVal = gADCBuffer[curIndex + 1];

            if ((isRising && (curVal >= trigLevel-2))
                    && (curVal <= trigLevel+2)
                    && (curVal > priorVal)
                    && (curVal < nextVal)){

                //make copy of the buffer
                int32_t cpyIndex = 0;
                int32_t tempIndex = curIndex - HALF_SCREEN;
                while (tempIndex != curIndex + HALF_SCREEN){
                    localBuffer[cpyIndex++] = gADCBuffer[tempIndex++];
                }
                foundflag = 1;

            }

            else if ((!isRising && (curVal >= trigLevel-2))
                    && (curVal <= trigLevel+2)
                    && (curVal < priorVal)
                    && (curVal > nextVal)){

                //make copy of the buffer
                int32_t cpyIndex = 0;
                int32_t tempIndex = curIndex - HALF_SCREEN;
                while (tempIndex != curIndex + HALF_SCREEN){
                    localBuffer[cpyIndex++] = gADCBuffer[tempIndex++];
                }
                foundflag = 1;

            }
            curIndex--;
        }

        //reset the trigger index to its initial location
        if (foundflag != 1){
            curIndex = curIndexTemp;
        }


        GrContextForegroundSet(&sContext, ClrBlack);
        GrRectFill(&sContext, &rectFullScreen); // fill screen with black


        GrContextForegroundSet(&sContext, ClrWhite); // Blue text
        GrStringDraw(&sContext, gVoltageScaleStr[index_volt %= 4], /*length*/ -1, /*x*/ 10, /*y*/ 10, /*opaque*/ false);

        GrStringDraw(&sContext, "20us", /*length*/ -1, /*x*/ 60, /*y*/ 10, /*opaque*/ false); //??




        //////////////SINE WAVE//////////////////////

        float fScale = (VIN_RANGE * PIXELS_PER_DIV)/((1 << ADC_BITS) * fVoltsPerDiv); //(3.3 * 20)/((1 << 12) * 0.2);

        while(readIndex+1 < LCD_HORIZONTAL_MAX){ // change this to for loop with less things in the middle
            volatile uint16_t sample = localBuffer[readIndex];//??raw ADC sample
            volatile uint16_t sample_2 = localBuffer[++readIndex];//??raw ADC sample

            volatile int y = LCD_VERTICAL_MAX/2 - (int)roundf(fScale * ((int)sample - ADC_OFFSET));
            volatile int y_2 = LCD_VERTICAL_MAX/2 - (int)roundf(fScale * ((int)sample_2 - ADC_OFFSET));

            GrContextForegroundSet(&sContext, ClrYellow); // yellow text

            GrLineDraw(&sContext, /*x1*/ x, /*y1*/ y, /*x2*/ ++x, /*y2*/ y_2);

        }

        // yellow text
        int y_unit_inc = LCD_VERTICAL_MAX/2 +21;
        int y_unit_dec = LCD_VERTICAL_MAX/2 -21;

        GrContextForegroundSet(&sContext, ClrMistyRose);
        //y
        GrLineDraw(&sContext, /*x1*/ LCD_VERTICAL_MAX/2, /*y1*/ 0, /*x2*/LCD_VERTICAL_MAX/2, /*y2*/ LCD_VERTICAL_MAX);

        //x
        GrLineDraw(&sContext, /*x1*/ 0, /*y1*/ LCD_VERTICAL_MAX/2 , /*x2*/LCD_VERTICAL_MAX, /*y2*/ LCD_VERTICAL_MAX/2);

        GrLineDraw(&sContext, /*x1*/ 0, /*y1*/  y_unit_inc , /*x2*/LCD_VERTICAL_MAX, /*y2*/ y_unit_inc);
        GrLineDraw(&sContext, /*x1*/ 0, /*y1*/  y_unit_dec , /*x2*/LCD_VERTICAL_MAX, /*y2*/  y_unit_dec);
        GrLineDraw(&sContext, /*x1*/ 0, /*y1*/  y_unit_inc+21 , /*x2*/LCD_VERTICAL_MAX, /*y2*/ y_unit_inc+21);
        GrLineDraw(&sContext, /*x1*/ 0, /*y1*/  y_unit_dec-21 , /*x2*/LCD_VERTICAL_MAX, /*y2*/  y_unit_dec-21);
        GrLineDraw(&sContext, /*x1*/ 0, /*y1*/  y_unit_inc+ 42 , /*x2*/LCD_VERTICAL_MAX, /*y2*/ y_unit_inc+42);
        GrLineDraw(&sContext, /*x1*/ 0, /*y1*/  y_unit_dec-42 , /*x2*/LCD_VERTICAL_MAX, /*y2*/  y_unit_dec-42);

        GrLineDraw(&sContext, /*x1*/  /*y1*/  y_unit_inc ,0, /*x2*/ /*y2*/ y_unit_inc, LCD_VERTICAL_MAX);
        GrLineDraw(&sContext, /*x1*/ /*y1*/  y_unit_dec, 0,  /*x2*//*y2*/  y_unit_dec, LCD_VERTICAL_MAX);
        GrLineDraw(&sContext, /*x1*/  /*y1*/  y_unit_inc+21 , 0,/*x2*/ /*y2*/ y_unit_inc+21, LCD_VERTICAL_MAX);
        GrLineDraw(&sContext, /*x1*/  /*y1*/  y_unit_dec-21 ,0, /*x2*/  y_unit_dec-21, LCD_VERTICAL_MAX);
        GrLineDraw(&sContext, /*x1*/  /*y1*/  y_unit_inc+ 42 ,0,  /*y2*/ y_unit_inc+42, LCD_VERTICAL_MAX);
        GrLineDraw(&sContext, y_unit_dec-42 , 0,  y_unit_dec-42 ,LCD_VERTICAL_MAX);

        ///////////////CPU//////////////////
        count_loaded = cpu_load_count();
        cpu_load = (1.0f - (float)count_loaded/count_unloaded)* 100; // compute CPU load
        char str[50];   // string buffer
        char* percentage = "%";
        snprintf(str, sizeof(str), "CPU Load = %.2f", cpu_load); // convert time to string
        strcat(str, percentage);
        GrContextForegroundSet(&sContext, ClrSilver);
        GrStringDraw(&sContext, str, /*length*/ -1, /*x*/ 5, /*y*/ 120, /*opaque*/ false);

        if (isRising){
            int temp = 20;
            GrLineDraw(&sContext, /*x1*/ 70+temp, /*y1*/  15 , /*x2*/80+temp, /*y2*/  15); //_
            GrLineDraw(&sContext, /*x1*/ 80+ temp, /*y1*/  15 , /*x2*/80+ temp, /*y2*/  5);//|
            GrLineDraw(&sContext, /*x1*/ 80+ temp, /*y1*/  5 , /*x2*/90+ temp, /*y2*/  5);//-
            GrLineDraw(&sContext, /*x1*/ 73+ temp, /*y1*/  10 , /*x2*/80+ temp, /*y2*/  8);
            GrLineDraw(&sContext, /*x1*/ 83+ temp, /*y1*/ 10 , /*x2*/80+ temp, /*y2*/  8);
        }
        else{
            int temp = 20;
            GrLineDraw(&sContext, /*x1*/ 70+ temp, /*y1*/  5 , /*x2*/80+ temp, /*y2*/  5); //-
            GrLineDraw(&sContext, /*x1*/ 80+ temp, /*y1*/  5 , /*x2*/80+ temp, /*y2*/  15);//|
            GrLineDraw(&sContext, /*x1*/ 80+ temp, /*y1*/  15 , /*x2*/90+ temp, /*y2*/  15);//_
            GrLineDraw(&sContext, /*x1*/ 73+ temp, /*y1*/  8 , /*x2*/80+ temp, /*y2*/  10);
            GrLineDraw(&sContext, /*x1*/ 83+ temp, /*y1*/  8 , /*x2*/80+ temp, /*y2*/  10);
        }

        GrFlush(&sContext); // flush the frame buffer to the LCD

    }
}

uint32_t cpu_load_count(void)
{
    uint32_t i = 0;
    TimerIntClear(TIMER3_BASE, TIMER_TIMA_TIMEOUT);
    TimerEnable(TIMER3_BASE, TIMER_A); // start one-shot timer
    while (!(TimerIntStatus(TIMER3_BASE, false) & TIMER_TIMA_TIMEOUT))
        i++;
    return i;
}

