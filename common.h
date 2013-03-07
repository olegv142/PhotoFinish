#pragma once

//#define SILENT

#define LED_BIT  BIT0
#define IR_BITS (BIT5|BIT6)
#define BTN_BIT  BIT4

#ifndef SILENT
#define BEEP_BIT BIT7
#else
#define BEEP_BIT 0
#endif

#define REPEAT_MSGS 2
#define REPEAT_MSGS_DELAY 4
