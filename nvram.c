#include "nvram.h"
#include "flash.h"

#define NV_BUFF_SZ FLASH_SEG_SZ

#pragma data_alignment=FLASH_SEG_SZ
static const char nv_buff[NV_BUFF_SZ] @ "CODE";

typedef unsigned nv_sz_t;

#define HDR_SZ     sizeof(nv_sz_t)
#define ALIGN(sz)  (((sz)+sizeof(nv_sz_t)-1)&~(sizeof(nv_sz_t)-1))
#define REC_SZ(sz) (HDR_SZ+ALIGN(sz))

void const* nv_get(nv_sz_t sz)
{
	void const* ptr = 0;
	nv_sz_t off, curr_sz, *h;
	for (off = 0; off < NV_BUFF_SZ;) {
		h = (nv_sz_t*)&nv_buff[off];
		curr_sz = *h;
		if (!~curr_sz)
			break;
		if (curr_sz == sz)
			ptr = h + 1;
		off += REC_SZ(curr_sz);
	}
	return ptr;
}

void nv_put(void const* data, nv_sz_t sz)
{
	nv_sz_t off, curr_sz, *h;
	if (HDR_SZ + sz > NV_BUFF_SZ)
		return;
	for (off = 0; off < NV_BUFF_SZ;) {
		h = (nv_sz_t*)&nv_buff[off];
		curr_sz = *h;
		if (!~curr_sz)
			break;
		off += REC_SZ(curr_sz);
	}
	if (off + HDR_SZ + sz > NV_BUFF_SZ) {
		flash_erase(nv_buff, 1);
		off = 0;
	}
	flash_write(&nv_buff[off], &sz, HDR_SZ);
	off += HDR_SZ;
	flash_write(&nv_buff[off], data, sz);
}

