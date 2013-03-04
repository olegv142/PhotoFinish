#pragma once

//#define SILENT

#define LED_BIT  BIT0
#define IR_BIT   BIT6
#define BTN_BIT  BIT5

#ifndef SILENT
#define BEEP_BIT BIT7
#else
#define BEEP_BIT 0
#endif