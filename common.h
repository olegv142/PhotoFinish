#pragma once

//#define SILENT

#define LED_BIT       BIT0       // On-board LED (P1)
#define RX_BIT        BIT0       // IR Receiver  (P2)
#define IR_BITS      (BIT5|BIT6) // IR LEDs      (P1)
#define BTN_BIT       BIT4       // User button  (P1)
#define START_BTN_BIT BIT3       // Start button (P1)
#define PING_BTN      BIT1       // Ping button (P1)
#define CALIB_LED     BIT3       // IR calibration indicator (P1)
#define CALIB_SW      BIT4       // IR calibration switch (P1)
#define BATT_SENSE    BIT2       // battery sense input (P2)

#ifndef SILENT
// Beeper (P1)
#define BEEP_BIT BIT7
#else
#define BEEP_BIT 0
#endif

#define IR_BURST_PULSES 16

#define REPEAT_MSGS 2
#define REPEAT_MSGS_DELAY 4

#define SHORT_DELAY_TICKS 500

/* We are expecting 793.5 Hz watchdog interrupt rate (26MHz / (4*8192)) */
#define WD_HZ  793
