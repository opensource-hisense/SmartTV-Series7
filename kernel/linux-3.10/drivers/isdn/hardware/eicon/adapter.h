/* $Id: //DTV/MP_BR/DTV_X_IDTV1401_002306_12_001_37_007/l-pdk/device/mediatek_common/vm_linux/chiling/kernel/linux-3.10/drivers/isdn/hardware/eicon/adapter.h#1 $ */

#ifndef __DIVA_USER_MODE_IDI_ADAPTER_H__
#define __DIVA_USER_MODE_IDI_ADAPTER_H__

#define DIVA_UM_IDI_ADAPTER_REMOVED 0x00000001

typedef struct _diva_um_idi_adapter {
	struct list_head link;
	DESCRIPTOR d;
	int adapter_nr;
	struct list_head entity_q;	/* entities linked to this adapter */
	dword status;
} diva_um_idi_adapter_t;


#endif
