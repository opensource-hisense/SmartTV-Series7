/*-----------------------------------------------------------------------------
 *\drivers\usb\host\mtk_cust.h
 *
 * MT53xx USB driver
 *
 * Copyright (c) 2008-2012 MediaTek Inc.
 * $Author: dtvbm11 $
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 * http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
 *
 *---------------------------------------------------------------------------*/

#ifndef MTK_CUST
#define MTK_CUST

#if defined(CONFIG_ARCH_MT85XX)
#include <mach/mt85xx.h>
#include <config/arch/chip_ver.h>
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 0, 0)
#include "../usbhostconf.h"
#include <linux/usb_cus_config.h>
#else
#include <linux/usb_config.h>
#endif
#else
#include <mach/hardware.h>
#endif


#if defined (CONFIG_ARCH_MT85XX)
#if(CONFIG_CHIP_VER_CURR == CONFIG_CHIP_VER_MT8580)
    #define CONFIG_ARCH_MT8580 1	
	#define USB_IRQ_LOCK 1
#elif(CONFIG_CHIP_VER_CURR == CONFIG_CHIP_VER_MT8563)
	#define CONFIG_ARCH_MT8563 1
#endif
#endif

#if defined(CONFIG_ARCH_MT85XX)
  #if defined(CONFIG_ARCH_MT8563)
    #define MUSB_BASE                 (IO_VIRT + 0x37000)
  #else
	#define MUSB_BASE                 (IO_VIRT + 0xE000)
  #endif
  #if defined(CONFIG_ARCH_MT8580)
    #define MUSB_BASE1                (IO_VIRT + 0x3C000)
    #define MUSB_BASE2                (IO_VIRT + 0x37000)
    #define MUSB_PHYBASE              (0x1800)
    #define MUSB_PWD_PHYBASE          (-0x1800)
  #elif defined(CONFIG_ARCH_MT8563)
	#define MUSB_BASE1                (IO_VIRT + 0xE000)
    #define MUSB_BASE2                (IO_VIRT + 0xF000)
    #define MUSB_PHYBASE              (0x0)
    #define MUSB_PWD_PHYBASE          (0x0)
  #else
    #error MUSB_BASEn not defined !
  #endif

  #if defined(CONFIG_ARCH_MT8563)
  #define VECTOR_USB   (82)
  #define VECTOR_USB2   (22)
  #define VECTOR_USB3   (23)
  #else
  #ifndef VECTOR_USB
    #define VECTOR_USB   (54)
  #endif
  #ifndef VECTOR_USB2
    #define VECTOR_USB2  (117)
  #endif
  #ifndef VECTOR_USB3
    #define VECTOR_USB3  (114)
  #endif
  #endif
#else
    #define MUSB_BASE					(IO_VIRT + 0x50000)
    #define MUSB_BASE1					(IO_VIRT + 0x51000)
    #define MUSB_BASE2					(IO_VIRT + 0x52000)
    #define MUSB_BASE3					(IO_VIRT + 0x53000)
    #define MUSB_PHYBASE				(0x9800)

#ifndef VECTOR_USB0
    #define VECTOR_USB0 (15)
#endif 
#ifndef VECTOR_USB1
    #define VECTOR_USB1 (11)
#endif 

#ifndef VECTOR_USB2
	#define VECTOR_USB2  (32 + 16)
#endif
#ifndef VECTOR_USB3
	#define VECTOR_USB3  (32 + 17)
#endif

#endif


#if defined(CONFIG_ARCH_MT8563)
	#define MUSB_PORT0_PHYBASE       (0x35800)
	#define MUSB_PORT1_PHYBASE       (0x48800)
	#define MUSB_PORT2_PHYBASE       (0x49800)
	#define MUSB_PORT3_PHYBASE       (0x300)	
#else
    #define MUSB_PORT0_PHYBASE       (0)
	#define MUSB_PORT1_PHYBASE       (0x100) //    -- MAC 1    
	#define MUSB_PORT2_PHYBASE       (0x200) //    -- MAC 2
	#if defined(CONFIG_ARCH_MT5890) || defined(CONFIG_ARCH_MT5861)
	#define MUSB_PORT3_PHYBASE		 (0x200)	
	#else
	#define MUSB_PORT3_PHYBASE       (0x300)
	#endif
#endif

/* 
        dtv
        	P0, P1, P2, P3: 8+1, Note: This Is Physical Endpoint Number.
        	P0, P1, P2, P3 Fifosize = ((5*(512+512)) + 64) Bytes. 
*/
#if !defined(CONFIG_ARCH_MT85XX)
#define MUSB_C_NUM_EPS  (9)
#define MUSB_C_NUM_TX_EPS  (9)
#define MUSB_C_NUM_RX_EPS  (9) 
#else
#define MUSB_C_NUM_EPS  (16)
#define MUSB_C_NUM_TX_EPS  (16)
#define MUSB_C_NUM_RX_EPS  (16) 
#endif

#if defined(CONFIG_ARCH_MT5399) || defined(CONFIG_ARCH_MT5890) || defined(CONFIG_ARCH_MT5861) || defined(CONFIG_ARCH_MT5882)
#define MUC_NUM_PLATFORM_DEV        (4)
#else
#define MUC_NUM_PLATFORM_DEV         (2)
#endif
#define MUC_NUM_MAX_CONTROLLER      (4)


#if defined(CONFIG_ARCH_MT85XX)
#define CONFIG_MUSB_PM
#define MGC_HWREWARE_DOWN_SUPPORT
#define USB_READ_WRITE_TEST
#endif

extern int MGC_PhyReset(void * pBase, void * pPhyBase);
extern void MGC_VBusControl(uint8_t bPortNum, uint8_t bOn);


#ifdef CONFIG_ARCH_MT85XX
#define POWER_ON 1
#define POWER_OFF  0
extern int MUC_hcd_power_on(int power_on);
extern int  usb_power_onoff(int bValue);
extern int usb_power_reset(void);
extern void MGC_hareware_down(void);
extern void vBdpDramBusy(unsigned char fgBool);
extern int __init usb_init_procfs(void);
extern void __exit usb_uninit_procfs(void);

#endif



#endif
