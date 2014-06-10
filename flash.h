#pragma once

#include "msp430.h"

#define FLASH_SEG_SZ 512

static inline void flash_unlock(void)
{
	FCTL3 = FWPW; // Clear Lock bit
}

static inline void flash_lock(void)
{
	FCTL3 = FWPW + LOCK; // Set Lock bit
}

static inline void flash_wr_enable(void)
{
	FCTL1 = FWPW + WRT; // Set Write bit
}

static inline void flash_wr_disable(void)
{
	FCTL1 = FWPW; // Clear Write bit
}

static inline void flash_wait(void)
{
	while (FCTL3 & BUSY) __no_operation();
}

static inline void flash_erase(void const* base, unsigned nsegs)
{
	unsigned i;
	char *ptr = (char*)base;
	flash_wait();
	flash_unlock();
	for (i = 0; i < nsegs; ++i) {
		FCTL1 = FWPW + ERASE;	// Set Erase bit
		// Dummy write to erase segment
		// Since we are executing code from the flash wait is not required
		// The CPU just stops till operation completed
		*ptr = 0;
		ptr += FLASH_SEG_SZ;
		flash_wait();
	}
	flash_lock();
}

static inline void flash_write(void const* addr, void const* data, unsigned sz)
{
	unsigned i;
	char* ptr = (char*)addr;
	const char* src = data;
	flash_wait();
	flash_unlock();
	flash_wr_enable();
	for (i = 0; i < sz; ++i) {
		// Write data to flash
		// Since we are executing code from the flash wait is not required
		// The CPU just stops till operation completed
		*ptr++ = src[i];
		flash_wait();
	}
	flash_wr_disable();
	flash_lock();
}

