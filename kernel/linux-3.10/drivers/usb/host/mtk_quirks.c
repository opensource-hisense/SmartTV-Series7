/*-----------------------------------------------------------------------------
 *\drivers\usb\host\mtk_quirks.c
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


/** @file mtk_quirks.c
 *  This C file implements the mtk53xx USB host controller driver.
 */
//---------------------------------------------------------------------------
// Include files
//---------------------------------------------------------------------------
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/timer.h>
#include <linux/list.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/usb.h>
#include <linux/kthread.h>
#include <asm/byteorder.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/system.h>
#include <asm/unaligned.h>
#include <asm/uaccess.h>
#include <linux/dma-mapping.h>
#include <linux/usb/hcd.h>

#include "mtk_hcd.h"
#include "mtk_qmu_api.h"

#ifdef MUSB_DEBUG
extern int mgc_debuglevel;
#endif
extern int mgc_epskipchk;
extern int mgc_qmudisable;
extern int mgc_qmuenable;

#ifdef CONFIG_ARCH_MT85XX
extern char usb_cust_model;
extern int gpio_configure(unsigned gpio, int dir, int value);
#endif


/* Quirks: various devices quirks are handled by this table & its flags. */

struct quirk_device_struct {
	uint16_t vendorId;
	uint16_t productId;
	unsigned int device_info;
};

static const struct quirk_device_struct device_quirk_list[] = {
	//{ 0x046D, 0x082B, USB_QUIRK_QMU_DISABLE }, /* Camera Logtech C170, rx length error when video and audio transfer at the same time */
	{ 0x043E, 0x3009, USB_QUIRK_QMU_DISABLE }, /* Camera lg V400 */
    { 0x1460, 0x7290, USB_QUIRK_QMU_DISABLE }, /* Camera MAX */
	{ 0x18EC, 0x3299, USB_QUIRK_QMU_DISABLE }, /* Camera BlueLover */
	{ 0x1D27, 0x0601, USB_QUIRK_3D_CAMERA_WORK}, /* PrimeSense 3D Camera */
	{ 0, 0 , 0}
};

/*
 * Detcte any quirks the device has, and do any special setting for it if needed.
 */
static unsigned int usb_detect_quirks(uint16_t vendor, uint16_t product)
{
	int i;
	unsigned int quirks = 0;

	for (i = 0; device_quirk_list[i].device_info; i++) {
		if (vendor == device_quirk_list[i].vendorId &&
		    product == device_quirk_list[i].productId)
			{
				quirks |= device_quirk_list[i].device_info;
			}
	}
	return quirks;
}

unsigned int MGC_DeviceDetectQuirks(struct urb *pUrb)
{
    struct usb_device *dev = pUrb->dev;
    struct usb_device_descriptor *pDescriptor = &dev->descriptor;
	uint16_t vendorid, productid;
	vendorid = pDescriptor->idVendor;
	productid = pDescriptor->idProduct;

	return usb_detect_quirks(vendorid, productid);

}

#if CONFIG_USB_QMU_SUPPORT
int MGC_DeviceQmuSupported(struct urb *pUrb)
{
    struct usb_device *dev = pUrb->dev;
    struct usb_device_descriptor *pDescriptor = &dev->descriptor;
	uint16_t vendorid, productid;
	vendorid = pDescriptor->idVendor;
	productid = pDescriptor->idProduct;
	
	/*Test parameter to enable all device can support qmu to debug */
	if(mgc_qmuenable)
		return TRUE;

	if(mgc_qmudisable || 
		(usb_detect_quirks(vendorid, productid) & USB_QUIRK_QMU_DISABLE))
	{
		/* This Device can not support USB QMU */
		return FALSE;
	}
	
	return TRUE;
}
#endif

#ifdef MGC_PRE_CHECK_FREE_ENDPOINT
static int MGC_SkipIntfSettingChk(struct usb_device *dev)
{
	return FALSE;
}
static int MGC_SkipEpChk(struct usb_device *dev)
{
    struct usb_device_descriptor *pDescriptor = &dev->descriptor;
	
#ifndef SUPPORT_SHARE_MGC_END_MSD
	struct usb_interface_descriptor *d;
	d = &dev->actconfig->interface[0]->cur_altsetting->desc;
#endif

  	if(mgc_epskipchk)
	{
        DBG(-2,"Skip EpCheck: mgc_epskipchk is true.\n");
        return TRUE;
	}
	
	if(pDescriptor->idVendor == 0x1d6b &&
	   pDescriptor->idProduct == 0x0002 &&
	   pDescriptor->bDeviceClass == 0x09)
	{
        DBG(-2,"Skip EpCheck: MTK Root Hub.\n");
        return TRUE;
	}
#ifndef SUPPORT_SHARE_MGC_END_MSD //move to MGC_CheckFreeEndpoint()
	if( (pDescriptor->bDeviceClass == USB_CLASS_MASS_STORAGE) || 
             (d->bInterfaceClass == USB_CLASS_MASS_STORAGE) )
	{
		DBG(-2,"Skip EpCheck: A Mass Storage Device.\n");
		return TRUE;
	}
#endif
	/*20121009 WIFI Dongle skip endpoint check */
	if(pDescriptor->idVendor == 0x148F ||           /*Ralink Technology, Corp.*/
	   	pDescriptor->idVendor == 0x0E8D||           /*Mediatek, Corp.*/
	    	pDescriptor->idVendor == 0x0CF3 ||			/*Atheros Communications, Inc.*/
		 pDescriptor->idVendor == 0x0bda)			/*Realtek Semiconductor Corp.*/
	{
		DBG(-2, "Skip EpCheck: WIFI Dongle.\n");
		return TRUE;
	}
	/*20121009 Ralink WIFI Dongle has 6 Tx,1 Rx.
	 *The wifi dongle cann't enumerate complete because host not enough tx endpoint.
	 * skip endpoint check step
	 */	
    if(dev->manufacturer==NULL || dev->product==NULL)
    {
        DBG(-2, "[USB]manufacturer OR product is NULL.\n");
        return FALSE;
    }
        
    if(strcmp("Ralink",dev->manufacturer) == 0
    	  && strcmp("11n Adapter",dev->product) == 0)
    {
       DBG(-2, "[USB]Ralink 11n Adapter skip ep checking.\n");
       return TRUE;
    }

    if(strcmp("Ralink",dev->manufacturer) == 0
    	  && strcmp("802.11 n WLAN",dev->product) == 0)
    {
       DBG(-2, "[USB]Ralink 802.11 n WLAN Adapter skip ep checking.\n");
       return TRUE;
    }

   if(0x0421 == pDescriptor->idVendor && 0x051a == pDescriptor->idProduct){
       printk("[usb]NOKIA N9, skip ep checking.\n");
       return TRUE;
   }

    return FALSE;
}

int MGC_CheckFreeEndpoint(struct usb_device *dev, int configuration)
{
    MGC_LinuxCd *pThis = NULL;
    MGC_LinuxLocalEnd *pEnd = NULL;
	struct usb_host_config *config = NULL;
    int aIsLocalEndOccupy[2][MUSB_C_NUM_EPS]; /*0: Rx, 1: Tx*/
	int nintf = 0;
	int endfound[2] = {1, 1}; 
    int nEnd = -1;
	int i = 0;
#ifdef SUPPORT_SHARE_MGC_END_MSD
	struct usb_interface_descriptor *d;
    struct usb_device_descriptor *pDescriptor = &dev->descriptor;
#endif
	
	pThis = hcd_to_musbstruct(bus_to_hcd(dev->bus));
	DBG(-2, "configNum = %d, select configuration = 0x%x.\n",dev->descriptor.bNumConfigurations, configuration);
	
	if (dev->authorized == 0 || configuration == -1)
	{
		configuration = 0;
	}
	else 
	{
		for (i = 0; i < dev->descriptor.bNumConfigurations; i++) 
		{
			if (dev->config[i].desc.bConfigurationValue == configuration) 
			{
				config = &dev->config[i];
				break;
			}
		}
	}
	
	if ((!config && configuration != 0))
		return -EINVAL;

	/* Special Device can't do check enpoint function when enumerate */
	if(MGC_SkipEpChk(dev))
	{
        return 0;
	}
	
#ifdef SUPPORT_SHARE_MGC_END_MSD
	d = &dev->actconfig->interface[0]->cur_altsetting->desc;

	/* use a reserved one for bulk if any */
	//if (usb_pipebulk(pUrb->pipe))
	{		 
		if ( (pDescriptor->bDeviceClass == USB_CLASS_MASS_STORAGE) || 
			 (d->bInterfaceClass == USB_CLASS_MASS_STORAGE) )
		{
			if(pThis->bMsdEndTxNum!=0xff) // MSD rx/tx be config at the same time, 0xff means do not config
				return 0;
		}
	}
#endif

	if(dev->parent)
	{
	    DBG(-2,"[USB:parent]level = 0x%04X,idVendor = 0x%04X,idProduct = 0x%04X,bDeviceClass = 0x%04X\n", 
						dev->parent->level,dev->parent->descriptor.idVendor, 
						dev->parent->descriptor.idProduct, dev->parent->descriptor.bDeviceClass);
	}
	
	/* The USB spec says configuration 0 means unconfigured.
	 * But if a device includes a configuration numbered 0,
	 * we will accept it as a correctly configured state.
	 * Use -1 if you really want to unconfigure the device.
	 */
	if (config && configuration == 0)
		dev_warn(&dev->dev, "config 0 descriptor??\n");

    if(config)
    {
		/*0: Rx, 1: Tx*/			
		for (nEnd = 1; nEnd < pThis->bEndTxCount; nEnd++)
		{
            pEnd = &(pThis->aLocalEnd[1][nEnd]); 
            aIsLocalEndOccupy[1][nEnd] = pEnd->bIsOccupy;
		}
		
        for (nEnd = 1; nEnd < pThis->bEndRxCount; nEnd++)
        {               
            pEnd = &(pThis->aLocalEnd[0][nEnd]);
            aIsLocalEndOccupy[0][nEnd] = pEnd->bIsOccupy;
            DBG(-2, "[USB1] [%d: RX=%d TX= %d]\n", 
                nEnd, aIsLocalEndOccupy[0][nEnd], aIsLocalEndOccupy[1][nEnd]);
    	}
		
        nintf = config->desc.bNumInterfaces;
    	for (i = 0; i < nintf; ++i) 
    	{
    		struct usb_interface *intf = config->interface[i];
        	struct usb_host_interface *iface_desc;
        	struct usb_endpoint_descriptor *ep_desc;
            struct usb_host_endpoint *ep = NULL;
			struct usb_interface_cache *intfc = config->intf_cache[i];
			struct usb_host_interface *alt;
			
			int infaceMaxEndponits = 0;
        	int j = 0, cur_setting = 0;
        	int found = 0, ep_end = 0;
        	int epnum = 0, is_out = 0;

        	iface_desc = intf->cur_altsetting;
			infaceMaxEndponits = iface_desc->desc.bNumEndpoints;
			
			DBG(-2, "[USB1:intf][%d]altsettingNum = %d\n", i, intfc->num_altsetting);

			// check the whole interface setting and select the setting that need maximu endpoint.
			if(!MGC_SkipIntfSettingChk(dev))
			{
				for ((j = 0, alt = &intfc->altsetting[0]);
					  j < intfc->num_altsetting;
					 (++j, ++alt)) 
			 	{
					if(alt->desc.bNumEndpoints > infaceMaxEndponits)
					{
						infaceMaxEndponits = alt->desc.bNumEndpoints;
						iface_desc = alt;
						cur_setting = j;
					}
	    		}
			}
				 
			 DBG(-2, "[USB2:intf][%d]intfNmu = %d, cur_altsetting = %d, bNumEndpoints = 0x%X, bInterfaceClass = 0x%X\n", 
					 i, nintf, cur_setting, infaceMaxEndponits, iface_desc->desc.bInterfaceClass);
			 
        	for (j = 0; j < infaceMaxEndponits; ++j) 
        	{
        		ep_desc = &iface_desc->endpoint[j].desc;
        	    epnum = usb_endpoint_num(ep_desc);
        	    is_out = usb_endpoint_dir_out(ep_desc);
                found = 0;
				
        		if(!endfound[is_out])
    			{
  					goto ep_not_enough;
    			}

        		DBG(-2, "[USB %d] Type: 0x%02X, Addr: 0x%02X, Attr: 0x%02X, Intr: 0x%02X, MaxP: 0x%04X\n", 
        						epnum, ep_desc->bDescriptorType, ep_desc->bEndpointAddress, 
    							ep_desc->bmAttributes, ep_desc->bInterval, ep_desc->wMaxPacketSize);

				ep = is_out ? dev->ep_out[epnum] : dev->ep_in[epnum];
            	
            	if(ep && ep->hcpriv)
            	{
                    pEnd = (MGC_LinuxLocalEnd *)ep->hcpriv;            	
                    found = 1;
                    continue;
            	}
				DBG(-2, "[USB]%s Ep Dev_leve = %d, intfClass = 0x%x.\n",
							((is_out) ? "Tx" : "Rx"), dev->level,iface_desc->desc.bInterfaceClass);  

                ep_end = is_out ? pThis->bEndTxCount : pThis->bEndRxCount; 
				
                for (nEnd = 1; nEnd < ep_end; nEnd++)
                {  
                    if(!aIsLocalEndOccupy[is_out][nEnd])
                	{
						// Occupy this endpoint.
                        aIsLocalEndOccupy[is_out][nEnd] = TRUE;
						found = 1;
						break;
                	}                   
                }
				
 				ep_not_enough:
                if(!found)
                {
                	endfound[is_out] = 0;
                    INFO("[USB] !!!!!!!!WARNING: Endpoint not enough! %s Ep\n", ((is_out) ? "Tx" : "Rx"));
                    //Set Device Base Class as a miscellaneous
                    //Set InterfaceClass & InterfaceSubClass to 0xEE as an 
                    //unsupport device.
                    if(iface_desc)
                    {
                        ep_desc->bEndpointAddress = 0xEE;
                        iface_desc->desc.bInterfaceClass = 0xEE;
                        iface_desc->desc.bInterfaceSubClass = 0xEE;                    
                    }
                }
        	}
    	}
    }

    for (nEnd = 1; nEnd < pThis->bEndRxCount; nEnd++)
    {
        DBG(-2, "[USB_EP_End] [%d: RX=%d TX= %d]\n", 
                nEnd, aIsLocalEndOccupy[0][nEnd], aIsLocalEndOccupy[1][nEnd]);
    }
    return 0;
}
#endif

#ifdef CONFIG_DEVICE_IDENTIFY_SUPPORT
int MGC_IsUvcDevice(struct usb_device_descriptor *pDescriptor, struct usb_device *pDev)
{
    struct usb_host_config *config = NULL;
    struct usb_interface_assoc_descriptor *iad = NULL;
    int i = 0;
    
    if(!pDescriptor || !pDev)
    {
        return FALSE;
    }
    
	if(pDescriptor->bDeviceClass == 0xEF && //Multi-interface Function Code Device
	   pDescriptor->bDeviceSubClass == 0x02 &&  //This is the Common Class Sub Class
	   pDescriptor->bDeviceProtocol == 0x01) //This is the Interface Association Descriptor protocol
	{
	    return TRUE;
    }

	if(pDev->config)
	{
	    config = pDev->config;

	    if(config)
	    {
        	for (i = 0; i < USB_MAXIADS; i++) 
        	{
        	    iad = config->intf_assoc[i];
        		if (iad == NULL)
        			break;


        	    if((iad->bFunctionClass == 0x0E || iad->bFunctionClass == 0xFF) &&
                   iad->bFunctionSubClass == 0x03)
        		{
        		    printk("IAD Video Interface Class\n");
                    return TRUE;
        	    }
        	}
	    }

	}

    return FALSE;
}

int MGC_IsUacDevice(struct usb_device_descriptor *pDescriptor, 
                                              struct usb_device *pDev)
{
    struct usb_host_config *c = NULL;
    int i = 0;
    
    if(!pDescriptor || !pDev)
    {
        return FALSE;
    }
    
    if(pDev->actconfig)
    {
        c = pDev->actconfig;

	if(c)
	{
	    for(; i < c->desc.bNumInterfaces; i ++)
	    {
	        if(c->intf_cache[i]->altsetting->desc.bInterfaceClass == USB_CLASS_AUDIO)
	        {
	            return TRUE;
	        }
	    }
	}
    }
    return FALSE;
}

int MGC_IsMFIDevice(struct usb_device_descriptor *pDescriptor, struct usb_device *pDev)
{
    if(!MGC_IsUacDevice(pDescriptor,pDev))
    {
        return FALSE;
    }
    
    if(0x05AC == pDescriptor->idVendor &&  0x12 == (pDescriptor->idProduct >> 8))
    {
        return TRUE;
    }
    
    return FALSE;   
}

#endif

#ifdef CONFIG_ARCH_MT85XX
/*-------------------------------------------------------------------------*/
int MUC_hcd_power_on(int power_on)
{
	unsigned int u4Reg;

	return 0;
		
	if(power_on){
		//power on port0
		u4Reg = MGC_VIRT_Read32(0x24098);
		MU_MB();
		u4Reg |= 0x400;
		MGC_VIRT_Write32(0x24098, u4Reg);
		MU_MB();
		u4Reg &= 0xFFFFFFFC;
		MGC_VIRT_Write32(0x24098, u4Reg);
		MU_MB();
		u4Reg |= 0x8;
		MGC_VIRT_Write32(0x24098, u4Reg);
		MU_MB();
		u4Reg |= 0x10;
		MGC_VIRT_Write32(0x24098, u4Reg);
		MU_MB();
		u4Reg &= 0xFFFFFFFB;
		MGC_VIRT_Write32(0x24098, u4Reg);
		//power on port2
	}else{
		//power off port0
		u4Reg = MGC_VIRT_Read32(0x24098);
		MU_MB();
		u4Reg &= 0xFFFFFFFB;
		MGC_VIRT_Write32(0x24098, u4Reg);
		MU_MB();
		u4Reg |= 0x10;
		MGC_VIRT_Write32(0x24098, u4Reg);
		MU_MB();
		u4Reg |= 0x8;
		MGC_VIRT_Write32(0x24098, u4Reg);
		MU_MB();
		u4Reg &= 0xFFFFFFFC;
		MGC_VIRT_Write32(0x24098, u4Reg);
		MU_MB();
		u4Reg |= 0x400;
		MGC_VIRT_Write32(0x24098, u4Reg);
		//power off port1
	}

	return 0;
}

int  usb_power_onoff(int bValue)
{	
    if(bValue)
    {
        if(usb_cust_model == 1){
            printk("[usb]bdp7G ax power on\n");
            gpio_configure(6,1,1);//7G ax/bx wifi port
            //gpio_configure(7,1,1);//7G ax  normal port//power on in APP Init
        }
        else if(usb_cust_model == 2){
            printk("[usb]bdp7G bx power on\n");
            gpio_configure(6,1,1);//7G ax/bx wifi port
            //gpio_configure(7,1,1); //7G bx  postpone power of usb
        }
        else if(usb_cust_model == 3){
            printk("[usb]bdv5G ex power on\n");
            gpio_configure(76,1,1);
            //gpio_configure(7,1,1); //7G bx  postpone power of usb
        }
        else if(usb_cust_model == 6){
            printk("[usb]bdv6G nj power on\n");
            gpio_configure(7,1,1);
            gpio_configure(8,1,1);//wifi port
        }
        else if(usb_cust_model == 7){
            printk("[usb]bdv5G nx/bdp7G cx power on\n");
            gpio_configure(7,1,1);
            gpio_configure(13,1,1);
        }
        else if(usb_cust_model == 8){
            printk("[usb]bdp8G ax power on\n");
            gpio_configure(70,1,1);//8G ax port
        }
        else if(usb_cust_model == 9){
            printk("[usb]bdp8G bx power on\n");
            gpio_configure(70,1,1);//8G bx port
        } 
        else if(usb_cust_model == 10){
            printk("[usb]bdv6G ej power on\n");
            gpio_configure(70,1,1);//8G ej front port
            gpio_configure(17,1,1);//8G ej wifi port
        }
		#if (CUSTOMER_MODEL_ID == 1)
		printk("[usb]philips 2k14 power on\n");
		gpio_configure(3,1,1);//rear port
		gpio_configure(5,1,1);//front port
		gpio_configure(7,1,1);//wifi port
		#else
		//do nothine
		#endif
   }else{
	        if(usb_cust_model == 1){
	            printk("[usb]bdp7G ax power on\n");
	            gpio_configure(6,1,0);//7G ax/bx wifi port
	            //gpio_configure(7,1,1);//7G ax  normal port//power on in APP Init
	        }
	        else if(usb_cust_model == 2){
	            printk("[usb]bdp7G bx power on\n");
	            gpio_configure(6,1,0);//7G ax/bx wifi port
	            //gpio_configure(7,1,1); //7G bx  postpone power of usb
	        }
	        else if(usb_cust_model == 3){
	            printk("[usb]bdv5G ex power on\n");
	            gpio_configure(76,1,0);
	            //gpio_configure(7,1,1); //7G bx  postpone power of usb
	        }
	        else if(usb_cust_model == 6){
	            printk("[usb]bdv6G nj power on\n");
	            gpio_configure(7,1,0);
	            gpio_configure(8,1,0);//wifi port
	        }
	        else if(usb_cust_model == 7){
	            printk("[usb]bdv5G nx/bdp7G cx power on\n");
	            gpio_configure(7,1,0);
	            gpio_configure(13,1,0);
	        }
	        else if(usb_cust_model == 8){
	            printk("[usb]bdp8G ax power on\n");
	            gpio_configure(70,1,0);//8G ax port
	        }
	        else if(usb_cust_model == 9){
	            printk("[usb]bdp8G bx power on\n");
	            gpio_configure(70,1,0);//8G bx port
	        } 
	        else if(usb_cust_model == 10){
	            printk("[usb]bdv6G ej power on\n");
	            gpio_configure(70,1,0);//8G ej front port
	            gpio_configure(17,1,0);//8G ej wifi port
        }
    }
	return 0;
}
//-------------------------------------------------------------------------
/** usb_power_reset
 *  usb  power reset control api, hub contro api.
 *  @param   void.
 *  @retval   0        Success.
 *  @retval   others   Fail.
 */
//-------------------------------------------------------------------------

/*just reset usb port, not include wifi ports*/
int usb_power_reset()
{
	if(usb_cust_model == 6){
		printk("[usb]bdv6G nj power on\n");
		gpio_configure(7,1,0);
		mdelay(500);
		gpio_configure(7,1,1);
	}
	else if(usb_cust_model == 7){
		printk("[usb]bdv5G nx/bdp7G cx power on\n");
		gpio_configure(7,1,0);
		mdelay(500);
		gpio_configure(7,1,1);
	}
	else if(usb_cust_model == 8){
		printk("[usb]bdp8G ax power on\n");
		gpio_configure(70,1,0);//8G ax port
		mdelay(500);
		gpio_configure(70,1,1);
	}
	else if(usb_cust_model == 9){
		printk("[usb]bdp8G bx power on\n");
		gpio_configure(70,1,0);//8G bx port
		mdelay(500);
		gpio_configure(70,1,1);
	} 
	else if(usb_cust_model == 10){
		printk("[usb]bdv6G ej power on\n");
		gpio_configure(70,1,0);//8G ej front port
		mdelay(500);
		gpio_configure(70,1,1);
	}

	return 0;
}

#ifdef MGC_HWREWARE_DOWN_SUPPORT
void MGC_hareware_down(void)
{
#if defined(CONFIG_ARCH_MT8580)
   uint32_t u4Reg;
/*Modifier: Jumin.Li
  *Resion : MT8580 only the third port could do hardware down operation, MT8561 only the second
  *Custome-tailor : PANA APP determine which port should be enable or not, so add pana config here
  */
#if CONFIG_PANASONIC_USB_PORT_CTRL
  #if defined(CONFIG_ARCH_MT8580)  
    #if (1 == USB_PORT2_ENABLE)
        uint8_t* pPhyBase = (uint8_t*)(MUSB_BASE2 + MUSB_PWD_PHYBASE + MUSB_PORT0_PHYBASE);
	    uint8_t* pBase = (uint8_t*)MUSB_BASE2;
    #else
        return;
    #endif
  #endif
#else
  #if defined(CONFIG_ARCH_MT8580)
   uint8_t* pPhyBase = (uint8_t*)(MUSB_BASE2 + MUSB_PWD_PHYBASE + MUSB_PORT0_PHYBASE);
   uint8_t* pBase = (uint8_t*)MUSB_BASE2;
  #endif
#endif
   #if ((0 == CONFIG_CUS_PANASONIC_USB) && defined(CONFIG_USB_DETECT_IMPROVE))
   if(MGC_IsDevAttached(pBase))
   {
        printk("[usb]suspend device on pBase 0x%x\n",(uint32_t)pBase);
        u4Reg = MGC_PHY_Read32(pPhyBase, 0x68);
        u4Reg |=  0x00040008; 
        MGC_PHY_Write32(pPhyBase, 0x68, u4Reg);  

		u4Reg = MGC_Read8(pBase, MGC_O_HDRC_POWER);
		u4Reg |= (MGC_M_POWER_SUSPENDM|MGC_M_POWER_ENSUSPEND);
		MGC_Write8(pBase, MGC_O_HDRC_POWER, u4Reg);
   }
   else
   #endif	
   {
        printk("[usb]suspend host hardware on pBase 0x%x\n",(uint32_t)pBase);
        u4Reg = MGC_PHY_Read32(pPhyBase, 0x68);
        u4Reg |=  0x00040000; 
        MGC_PHY_Write32(pPhyBase, 0x68, u4Reg);   
        
        //close otg bit
        u4Reg = MGC_PHY_Read32(pPhyBase,(uint32_t)0x18);
        u4Reg &= ~0x08000000;
        MGC_PHY_Write32(pPhyBase, (uint32_t)0x18, u4Reg);

        //close USB module clk.
        u4Reg = MGC_VIRT_Read32(0x7C);
        u4Reg &= ~0x00010000; 
        MGC_VIRT_Write32(0x7C, u4Reg);		   
   }
   	

#endif
}

#endif

void vBdpDramBusy(unsigned char fgBool)
{   
    static unsigned char *pcDramBusyMemAddr = NULL;
    const unsigned int u4BufLength = 64*1024;
    unsigned int u4Reg = 0;
    
    if(fgBool)
    {
        if(!pcDramBusyMemAddr)
        {
            pcDramBusyMemAddr = (unsigned char*)kmalloc(u4BufLength, GFP_KERNEL);
        }

        if(pcDramBusyMemAddr)
        {
            printk("Start DRAM Test Agent.\n");
            //High priority¡G
            //RISCWrite 0x007104 0x0xxxxxxx
        	u4Reg = IO_READ32(IO_VIRT, 0x7104);
            u4Reg &= 0x0FFFFFFF;
            IO_WRITE32(IO_VIRT, (0x7104), u4Reg);            

            //007210 (PHY Addr)
            IO_WRITE32(IO_VIRT, (0x7210), (unsigned int)pcDramBusyMemAddr);
            
            //007214 (Length, must be greater than 0x1000)
            IO_WRITE32(IO_VIRT, (0x7214), u4BufLength);           
            
            //007218 0x840F110D (once)
            //       0x860F110D (repeat)
            IO_WRITE32(IO_VIRT, (0x7218), 0x860F110D);           
        }
    }
    else
    {
        //Disable
        //007218 0x060F110D (once)
    	u4Reg= IO_READ32(IO_VIRT, 0x7218);
        u4Reg &= 0x0FFFFFFF;
        IO_WRITE32(IO_VIRT, (0x7218), u4Reg);        
    }
}

#ifdef USB_READ_WRITE_TEST

static struct proc_dir_entry *usb_rw_test_entry = NULL;
static struct proc_dir_entry *usb_stress_test_entry = NULL;
static struct proc_dir_entry *usb_log_en_entry = NULL;
static struct proc_dir_entry *usb_vbus_entry = NULL;
static struct proc_dir_entry *usb_root_port_entry = NULL;
static struct proc_dir_entry *usb_setting_entry = NULL;
static struct proc_dir_entry *usb_proc_dir = NULL;
static struct proc_dir_entry *usb_suspend_entry = NULL;
static struct proc_dir_entry *usb_resume_entry = NULL;

static unsigned int usb_test_log_en = 0x5;
static uint32_t MGC_usb_setting = 0;

#define WIFI_USE_QUEUE    (0x00000001)
#define USB_SETTING_CLEAR (0x80000000)
#define USB_DMA_BUFFER_SIZE     (64*1024)

#define USB_TEST_LOG(n, f, x...) if (usb_test_log_en & (n)){ printk(DRIVER_NAME ": <%s> " f, __func__,## x); }


uint32_t Str2Hex(char *inp_buf, unsigned long count)
{
    uint32_t mCh;
    int i;
    
    // if input HEX format 0xXX
    if ((inp_buf[0] == '0') && (inp_buf[1] == 'x'))
    {
        mCh = 0;
        for (i = 2; i < count; i ++)
        {
            if ((inp_buf[i] >= '0') && (inp_buf[i] <= '9'))
            {
                mCh <<= 4;
                mCh += (inp_buf[i] - '0');
            }
            else if ((inp_buf[i] >= 'a') && (inp_buf[i] <= 'f'))
            {
                mCh <<= 4;
                mCh += ((inp_buf[i] - 'a') + 10);
            }
            else if ((inp_buf[i] >= 'A') && (inp_buf[i] <= 'F'))
            {
                mCh <<= 4;
                mCh += ((inp_buf[i] - 'A') + 10);
            }
        }
    }
    else
    {
        mCh = 0;
        for (i = 0; i < count; i ++)
        {
            if ((inp_buf[i] >= '0') && (inp_buf[i] <= '9'))
            {
                mCh *= 10;
                mCh += (inp_buf[i] - '0');
            }
        }
    }

    return mCh;
}

static void print_Buffer(uint32_t u4BufSz, uint8_t *pu1Buf)
{
    uint32_t u4Idx = 0;
    uint8_t  *pu1TmpBuf = pu1Buf;

    for (u4Idx = 0; u4Idx < (u4BufSz); u4Idx ++) 
	{
        if (u4Idx % 16 == 0) 
		{
            printk("[0x%04X]", u4Idx);
        }

        printk("%02X", pu1TmpBuf[0]);

        if (u4Idx % 16 == 3 || u4Idx % 16 == 7 || u4Idx % 16 == 11) 
		{
            printk(" ");
        }
        else if (u4Idx % 16 == 15) 
		{
            printk("\n");
        }
      
        pu1TmpBuf ++;
    }    
}

static bool fgCompareFunc(uint32_t *pu4Buf1, uint32_t *pu4Buf2, uint32_t u4MemLen)
{
    uint32_t i;

    // Compare the result
    for(i=0; i<u4MemLen; i+=4)
    {
        if(pu4Buf1[i/4] != pu4Buf2[i/4])
        {
            printk("Compare Failed : rd_addr : 0x%x != wt_addr : 0x%x, inconsistant at element %d \n", (((uint32_t)pu4Buf2)+i), (((uint32_t)pu4Buf1)+i), i/4);
            MUSB_ASSERT(0);
            break;
        }
    }

    if(i == u4MemLen)
    {
        return TRUE;
    }

    printk("[Debug] Write Buf = 0x%x\n", (uint32_t)pu4Buf1);
    print_Buffer(u4MemLen, (uint8_t *)pu4Buf1);
    printk("[Debug] Read Buf = 0x%x\n", (uint32_t)pu4Buf2);    
    print_Buffer(u4MemLen, (uint8_t *)pu4Buf2);

    return FALSE;
}

static int proc_read_usb_suspend(char *page, char **start,
                             off_t off, int count,
                             int *eof, void *data)
{
#if defined(CC_MT8580)
    uint32_t u4Reg;
    uint8_t bIntrUsbValue;
  #if defined(CC_MT8580)
    uint8_t* pBase = (uint8_t*)MUSB_BASE3;
  #endif
    if(MGC_IsDevAttached(pBase))
    {
        printk("[usb]suspend device on pBase 0x%x\n",(uint32_t)pBase);
        u4Reg = MGC_PHY_Read32(pBase, 0x68);
        u4Reg |=  0x00040008; 
        MGC_PHY_Write32(pBase, 0x68, u4Reg);  
	
        u4Reg = MGC_Read8(pBase, MGC_O_HDRC_POWER);
        u4Reg |= (MGC_M_POWER_SUSPENDM|MGC_M_POWER_ENSUSPEND);
        MGC_Write8(pBase, MGC_O_HDRC_POWER, u4Reg);
	
        printk("[usb]clear interrupt on pBase 0x%x\n",(uint32_t)pBase);
        bIntrUsbValue = MGC_Read8(pBase, MGC_O_HDRC_INTRUSB);
        MGC_Write8(pBase, MGC_O_HDRC_INTRUSB, bIntrUsbValue);
    }
    else
    {
        printk("[usb]suspend host hardware on pBase 0x%x\n",(uint32_t)pBase);
        u4Reg = MGC_PHY_Read32(pBase, 0x68);
        u4Reg |=  0x00040000; 
        MGC_PHY_Write32(pBase, 0x68, u4Reg);	  
    }
#endif
    return 0;
}
static int proc_read_usb_resume(char *page, char **start,
                             off_t off, int count,
                             int *eof, void *data)
{
  #if defined(CC_MT8580)||defined(CC_MT8563)
    uint32_t u4Reg;
    uint8_t* pBase = (uint8_t*)MUSB_BASE2;


    if(MGC_IsDevAttached(pBase))
    {
        printk("[usb]resume device on pBase 0x%x\n",(uint32_t)pBase);
        u4Reg = MGC_PHY_Read32(pBase, 0x68);
        u4Reg &=  ~0x00040008;
        MGC_PHY_Write32(pBase, 0x68, u4Reg);  

		mdelay(10);
		
        u4Reg = MGC_Read8(pBase, MGC_O_HDRC_POWER);
        u4Reg &= ~(MGC_M_POWER_SUSPENDM|MGC_M_POWER_ENSUSPEND);
        u4Reg |= (MGC_M_POWER_RESUME);
        MGC_Write8(pBase, MGC_O_HDRC_POWER, u4Reg);

        mdelay(10);

        u4Reg = MGC_Read8(pBase, MGC_O_HDRC_POWER);
        u4Reg &= ~(MGC_M_POWER_RESUME);
        MGC_Write8(pBase, MGC_O_HDRC_POWER, u4Reg);
    }
    else
    {
        printk("[usb]resume host hardware on pBase 0x%x\n",(uint32_t)pBase);
        u4Reg = MGC_PHY_Read32(pBase, 0x68);
        u4Reg &=  ~0x00040000; 
        MGC_PHY_Write32(pBase, 0x68, u4Reg);	  
    }
#endif
    return 0;
}

static int MGC_ResetRootPort(uint8_t bPortNumb, uint8_t fgIsWantFullSpeed, uint32_t u4mdelay)
{
    MGC_LinuxCd *pThis = NULL;
    uint8_t *pPhyBase = NULL;
    uint32_t u4Reg = 0;
    if(bPortNumb >= MUCUST_GetControllerNum())
    {
        return -ENODEV;
    }

    pThis = MUCUST_GetControllerDev(bPortNumb)->pThis;
    if(!pThis)
    {
        return -ENODEV;
	}

    pPhyBase = (uint8_t *) pThis->pPhyBase;

    printk("[USB]reset root port %d\n", bPortNumb);

	//force disable R45
    u4Reg = MGC_PHY_Read32(pPhyBase, 0x1C);
    MGC_PHY_Write32( pPhyBase, 0x1C, u4Reg | 0x00004000); 
	
    mdelay(u4mdelay);

    u4Reg = MGC_PHY_Read32( pPhyBase, 0x1C);
    MGC_PHY_Write32(pPhyBase, 0x1C, u4Reg | 0x00002000);	
	
    mdelay(u4mdelay);
	
	//enable R45
    u4Reg = MGC_PHY_Read32(pPhyBase, 0x1C);
    MGC_PHY_Write32(pPhyBase, 0x1C, u4Reg & ~0x00006000);	

    //Set Speed
    pThis->fgIsWantFullSpeed = fgIsWantFullSpeed;

    return 0;        
}

static int proc_read_usb_log_en(char *page, char **start,
                             off_t off, int count,
                             int *eof, void *data)
{
    int  len;

    len = sprintf(page, "[USB TEST] Current USB Log Level: 0x%x\n", (uint32_t)usb_test_log_en);

    return len;
}

static int proc_write_usb_log_en(struct file *file,
                             const char *buffer,
                             unsigned long count,
                             void *data)
{
    int i;
    int tmp;
    char *buf;

    buf = kmalloc(count + 1, GFP_KERNEL);

    if(copy_from_user(buf, buffer, count))
            return -EFAULT;

    buf[count] = '\0';

    if ((buf[0] == '0') && (buf[1] == 'x'))
    {
        tmp = 0;
        for (i = 2; i < count; i ++)
        {
            if ((buf[i] >= '0') && (buf[i] <= '9'))
            {
                tmp <<= 4;
                tmp += (buf[i] - '0');
            }
            else if ((buf[i] >= 'a') && (buf[i] <= 'f'))
            {
                tmp <<= 4;
                tmp += ((buf[i] - 'a') + 10);
            }
            else if ((buf[i] >= 'A') && (buf[i] <= 'F'))
            {
                tmp <<= 4;
                tmp += ((buf[i] - 'A') + 10);
            }
        }
        usb_test_log_en = tmp;
        
        printk("[USB TEST] New USB Log Level: 0x%x\n", usb_test_log_en);
    }
    else
    {
        printk("Format : echo 0xZZ > log_en\n");
    }

    kfree(buf);

    return count;
}

static int proc_read_usb_vbus(char *page, char **start,
                             off_t off, int count,
                             int *eof, void *data)
{
    int  len;

    len = sprintf(page, "[usb] read vbus not implement yet\n");

    return len;
}

static int proc_write_usb_vbus(struct file *file,
                             const char *buffer,
                             unsigned long count,
                             void *data)
{ 
    char *inp_buf;
    uint32_t mHex;

    // Retrive magic char from echo 0xXX > /proc/driver/mmc/rw_test    
    inp_buf = kmalloc(count + 1, GFP_KERNEL);

    if (!inp_buf)
    {
        printk("[%s] inp_buf allocate failed !!\n", __func__);
        return 0;
    }
        
    if(copy_from_user(inp_buf, buffer, count))
            return -EFAULT;

    inp_buf[count] = '\0';

    mHex = Str2Hex(inp_buf, count);    
    kfree(inp_buf);

    mHex = mHex ? 1 : 0;

    MUCUST_GetControllerDev(0)->MGC_pfVbusControl(0,(uint8_t)mHex);
	
	return count;
}    


static int proc_read_usb_setting(char *page, char **start,
                             off_t off, int count,
                             int *eof, void *data)
{
    int  len;

    len = sprintf(page, "[USB TEST] Current USB setting: 0x%x\n", (uint32_t)MGC_usb_setting);

    return len;
}

static int proc_write_usb_setting(struct file *file,
                             const char *buffer,
                             unsigned long count,
                             void *data)
{ 
    char *inp_buf;
    uint32_t mHex;

    // Retrive magic char from echo 0xXX > /proc/driver/mmc/rw_test    
    inp_buf = kmalloc(count + 1, GFP_KERNEL);

    if (!inp_buf)
    {
        printk("[%s] inp_buf allocate failed !!\n", __func__);
        return 0;
    }
        
    if(copy_from_user(inp_buf, buffer, count))
            return -EFAULT;

    inp_buf[count] = '\0';

    mHex = Str2Hex(inp_buf, count);    
    kfree(inp_buf);

	if(mHex & USB_SETTING_CLEAR){
	  mHex &= ~USB_SETTING_CLEAR;
      MGC_usb_setting &= ~mHex;
	}
	else{
	  MGC_usb_setting |= mHex;
	}

    printk("[USB] New USB setting: 0x%x\n", MGC_usb_setting);
	return count;
}    


static int proc_read_test_pressed(char *page, char **start,
                             off_t off, int count,
                             int *eof, void *data)
{
    struct file *filp;
    mm_segment_t old_fs;
    uint32_t file_buf_len;
    int  len = 0;    
    
    if (off > 0) 
    {
        *eof = 1;
         return 0;
    }

    old_fs = get_fs();
    set_fs(KERNEL_DS);
    
    file_buf_len = USB_DMA_BUFFER_SIZE;
  
    filp = filp_open("/dev/sda", O_RDWR | O_NONBLOCK | O_SYNC, 0);
        
    if (IS_ERR(filp))
    {
        len = sprintf(page, "[%s]Open /dev/sda failed: %d !!\n", __func__, (int)filp);
    }    
    else
    {
        uint8_t  *file_buf;
        uint32_t rd_length;

        file_buf = (uint8_t *)kmalloc(file_buf_len, GFP_KERNEL | GFP_DMA);

        if (file_buf)
        {
            rd_length = filp->f_op->read(filp, file_buf, file_buf_len, &filp->f_pos);
            
            len = sprintf(page, "[%s] /dev/sda : file_buf = 0x%x, rd_length = %d\n", __func__, (uint32_t)file_buf, rd_length);
            
            kfree(file_buf);
        }
        else
        {
            printk("[%s] Can't allocate buffer size = 0x%x\n", __func__, file_buf_len);
        }
                
        filp_close(filp, NULL);
    }

    set_fs(old_fs);

    return len;
}

static int proc_write_test_pressed(struct file *file,
                             const char *buffer,
                             unsigned long count,
                             void *data)
{
    struct file *filp;
    mm_segment_t old_fs;
    uint32_t file_buf_len;
    
    char *inp_buf;
    uint32_t mHex;

    // Retrive magic char from echo 0xXX > /proc/driver/mmc/rw_test    
    inp_buf = kmalloc(count + 1, GFP_KERNEL);

    if (!inp_buf)
    {
        printk("[%s] inp_buf allocate failed !!\n", __func__);
        return 0;
    }
        
    if(copy_from_user(inp_buf, buffer, count))
            return -EFAULT;

    inp_buf[count] = '\0';

    mHex = Str2Hex(inp_buf, count);    
    kfree(inp_buf);
    
    old_fs = get_fs();
    set_fs(KERNEL_DS);
    
    file_buf_len = USB_DMA_BUFFER_SIZE;
  
    filp = filp_open("/dev/sda", O_RDWR | O_NONBLOCK | O_SYNC, 0);
    
    if (IS_ERR(filp))
    {
        printk("[%s]Open /dev/sda failed: %d !!\n", __func__, (int)filp);
    }
    else
    {
        uint8_t  *file_buf;
        uint32_t wt_length;

        file_buf = (uint8_t *)kmalloc(file_buf_len, GFP_KERNEL | GFP_DMA);

        if (file_buf)
        {
            memset(file_buf, mHex, file_buf_len);
            wt_length = filp->f_op->write(filp, file_buf, file_buf_len, &filp->f_pos);
            printk("[%s] /dev/sda : file_buf = 0x%x, wt_length = %d, Magic Char = 0x%x\n", __func__, (uint32_t)file_buf, wt_length, mHex);
            
            kfree(file_buf);
        }
        else
        {
            printk("[%s] Can't allocate buffer size = 0x%x\n", __func__, file_buf_len);
        }
                
        filp_close(filp, NULL);
    }

    set_fs(old_fs);

    return count;
}

static int proc_stress_test_pressed(struct file *file,
                             const char *buffer,
                             unsigned long count,
                             void *data)
{
    struct file *filp;
    mm_segment_t old_fs;
    uint32_t file_buf_len;
    
    char *inp_buf = NULL;
    char *token = NULL;
    char **stringp = NULL;
    int len = 0;
    uint32_t mTestCount;
    char file_name[9] = {0};
    char disk_no = 'a';

    // Retrive magic char from 
    //echo 1000,a > /proc/driver/usb/stress
    inp_buf = kmalloc(count + 1, GFP_KERNEL);

    if (!inp_buf)
    {
        printk("[%s] inp_buf allocate failed !!\n", __func__);
        return 0;
    }
        
    if(copy_from_user(inp_buf, buffer, count))
            return -EFAULT;

    inp_buf[count] = '\0';

    stringp = &inp_buf;

    token = strsep(stringp, ",;"); //separate the argument with ',' and ';'
    len = strlen(token);

    mTestCount = Str2Hex(token, len);    

    while(token != NULL)
    {
        token = strsep(stringp, ",;"); //separate the argument with ',' and ';   
        if(token)
        {
            int c = (int)*token;
            if(isalpha(c))
            {
                disk_no = *token;
                printk("[USB TEST] test on /dev/sd%c \n", disk_no);
            }
        }
    }

    kfree(inp_buf);
    
    old_fs = get_fs();
    set_fs(KERNEL_DS);
    
    file_buf_len = USB_DMA_BUFFER_SIZE;

    sprintf(file_name, "/dev/sd%c", disk_no);

    printk("[USB TEST] open file: %s\n", file_name);
    
    filp = filp_open(file_name, O_RDWR | O_NONBLOCK | O_SYNC, 0);
    
    if (IS_ERR(filp))
    {
        printk("[%s]Open %s failed: %d !!\n", __func__, file_name, (int)filp);
    }
    else
    {
        uint8_t  *wt_buf = NULL;
        uint8_t  *rd_buf = NULL;
        uint32_t wt_length;
        uint32_t rd_length;
        loff_t seekpos, seekresult, seekendpos;
        int   i, j;

        wt_buf = (uint8_t *)kmalloc(file_buf_len, GFP_KERNEL | GFP_DMA);
        rd_buf = (uint8_t *)kmalloc(file_buf_len, GFP_KERNEL | GFP_DMA);

#if defined(CONFIG_ARCH_MT85XX)
        vBdpDramBusy(TRUE);
#endif

        seekendpos = filp->f_op->llseek(filp, 0, SEEK_END);
          
        if (wt_buf && rd_buf && seekendpos)
        {            	
            unsigned long randCh;

            printk("\n **** Total test Count : %d, file length : %d KB = %d Test Unit (Test Unit : %d bytes) **** \n\n", mTestCount, (uint32_t)seekendpos / 1024, (uint32_t)seekendpos / file_buf_len, file_buf_len);

            // Start stress test for specific test count
            for (i = 0; i < mTestCount; i ++)
            {
                int loopnum = 0;
                
                seekpos = file_buf_len * i;

                // Avoid out of file range
                while (seekpos >= seekendpos-1)
                {
                    seekpos -= seekendpos;
                    loopnum++;
                }

                if (loopnum > 0)
                {
                    printk("\n **** Loop %d **** \n", loopnum);
                }
                
                // Seek to target position (Unit : USB_DMA_BUFFER_SIZE)
                seekresult = filp->f_op->llseek(filp, seekpos, SEEK_SET);
                if ( seekresult != seekpos)
                {                
                    printk("[%s] %s seek failed before write !! TestCount : %d, seekresult : 0x%x, seekpos : 0x%x\n", __func__, file_name, mTestCount, (uint32_t)seekresult, (uint32_t)seekpos);
                    break;
                }
                
                // Fill the write buffer by random number
                get_random_bytes(&randCh, sizeof(randCh));
                
                for (j = 0; j < file_buf_len; j += 4)
                {
                    *((uint32_t*)(((uint32_t)wt_buf)+j)) = (j+randCh);
                }
                // Write the buffer contents to card
                wt_length = filp->f_op->write(filp, wt_buf, file_buf_len, &filp->f_pos);

                // Close the file to avoid cache
                filp_close(filp, NULL);

                // Re-open the file for read back checking
                filp = filp_open(file_name, O_RDWR | O_NONBLOCK | O_SYNC, 0);
                
                if (IS_ERR(filp))
                {
                    printk("[%s]Open %s failed: %d !! TestCount = %d\n\n", __func__, file_name, (int)filp, mTestCount);
                    break;
                }
                
                printk("[%03d] %s : wt_buf : 0x%x, len : %d, pos : 0x%08X, Rand : 0x%08X\n", i, file_name, (uint32_t)wt_buf, wt_length, (uint32_t)seekpos, (uint32_t)randCh);
                
                // Seek to target position (Unit : USB_DMA_BUFFER_SIZE)
                seekresult = filp->f_op->llseek(filp, seekpos, SEEK_SET);
                if ( seekresult != seekpos)
                {                
                    printk("[%s] %s seek failed before read !! TestCount : %d, seekresult : 0x%x, seekpos : 0x%x\n", __func__, file_name, mTestCount, (uint32_t)seekresult, (uint32_t)seekpos);
                    MUSB_ASSERT(0);
                    break;
                }

                // Read back for verify
                rd_length = filp->f_op->read(filp, rd_buf, file_buf_len, &filp->f_pos);
                
                if(wt_length != rd_length) 
                {
                    printk("ERROR: wt_length != rd_length\n");
                    MUSB_ASSERT(0);
                }
                
                printk("[%03d] %s : rd_buf : 0x%x, len : %d, pos : 0x%08X\n", i, file_name, (uint32_t)rd_buf, rd_length, (uint32_t)seekpos);    

                // Compare read/write result
                if (fgCompareFunc((uint32_t*)wt_buf, (uint32_t*)rd_buf, file_buf_len))
                {
                    printk("[%03d] %s : Compare OK.\n", i, file_name);
                }
                else
                {
                    printk("[%03d] %s : Compare NG !!.\n", i, file_name);
                    MUSB_ASSERT(0);
                    break;
                }
            }
            
            printk("\n **** Test End : count : %d **** \n", i);
                    
            kfree(wt_buf);
        }
        else
        {
            if(seekendpos)
            {
                printk("[USB TEST] capacity of %s is 0\n", file_name);
            }
            else
            {
                printk("[%s] Can't allocate buffer size = 0x%x\n", __func__, file_buf_len);
            }
        }
                
        filp_close(filp, NULL);
    }

    set_fs(old_fs);

    return count;
}


static int proc_root_port_ctnl(struct file *file,
                             const char *buffer,
                             unsigned long count,
                             void *data)
{
    char *inp_buf = NULL;
    char *token = NULL;
    char **stringp = NULL;
    int len = 0, ret = 0;
    uint8_t bPortNum = 0;
    uint8_t fgIsFullSpeed = 0;
    uint32_t argv[3] = {0}, i = 0;
    uint32_t mdelay = 0;
    static const uint32_t max_argc = 3;
    

    // Retrive magic char from 
    //echo 0,1,100 > /proc/driver/usb/root_port
    inp_buf = kmalloc(count + 1, GFP_KERNEL);

    if (!inp_buf)
    {
        printk("[%s] inp_buf allocate failed !!\n", __func__);
        return 0;
    }
        
    if(copy_from_user(inp_buf, buffer, count))
            return -EFAULT;

    inp_buf[count] = '\0';
    stringp = &inp_buf;

    i = 0;

    do{
        token = strsep(stringp, ",;"); //separate the argument with ',' and ';   
        if(token)
        {
            len = strlen(token);
            argv[i] = Str2Hex(token, len);    
        }

        i++;
    }while(token != NULL && i < max_argc);

    bPortNum = (uint8_t)(argv[0]&0x000000FF);
    fgIsFullSpeed = (uint8_t)(argv[1]&0x000000FF);
    mdelay = argv[2];

    kfree(inp_buf);

    if(i < 2)
    {
        printk("Usage: echo [port_num],[is_full_speed],[mdelay] > /proc/driver/usb/root_port\n");
    }

    printk("Root Port[%d] reset to %s speed for %d ms\n", 
            bPortNum, fgIsFullSpeed==TRUE?"Full":"High", mdelay);
    
    ret = MGC_ResetRootPort(bPortNum, fgIsFullSpeed, mdelay);

    if(ret) 
    {
        printk("Reset command failed!\n");
    }

    printk("Reset success");    
    return count;
}

int __init usb_init_procfs(void)
{
    usb_proc_dir = proc_mkdir("driver/usb", NULL);
    //usb_proc_dir->owner = THIS_MODULE;
    if(usb_proc_dir == NULL) {
        printk("[USB TEST] Create procfs fail!\n");
        goto proc_init_fail;
    }

#ifdef USB_READ_WRITE_TEST
    usb_rw_test_entry = create_proc_entry("rw_test", 0755, usb_proc_dir);
    if(usb_rw_test_entry == NULL) {
        printk("[USB TEST] Create rw_test fail!\n");
        goto proc_init_fail;
    }
    usb_rw_test_entry->read_proc = proc_read_test_pressed;   // cat /proc/driver/usb/rw_test
    usb_rw_test_entry->write_proc = proc_write_test_pressed; // echo 0x1F > /proc/driver/usb/rw_test
    //usb_rw_test_entry->owner = THIS_MODULE;

    usb_stress_test_entry = create_proc_entry("stress", 0755, usb_proc_dir);
    if(usb_stress_test_entry == NULL) {
        printk("[USB TEST] Create stress fail!\n");
        goto proc_init_fail;
    }
    usb_stress_test_entry->read_proc = NULL;
    usb_stress_test_entry->write_proc = proc_stress_test_pressed; // echo 1000,a > /proc/driver/usb/stress
    //usb_stress_test_entry->owner = THIS_MODULE;    

    usb_root_port_entry = create_proc_entry("root_port", 0755, usb_proc_dir); 
    if(usb_root_port_entry == NULL) {
        printk("[USB TEST] Create root_port fail!\n");
        goto proc_init_fail;
    }
    usb_root_port_entry->read_proc = NULL;
    usb_root_port_entry->write_proc = proc_root_port_ctnl; //echo 0,1 > /proc/driver/usb/root_port
    //usb_root_port_entry->owner = THIS_MODULE;    

    usb_log_en_entry = create_proc_entry("log_en", 0755, usb_proc_dir);
    if(usb_log_en_entry == NULL) {
        goto proc_init_fail;
    }
    usb_log_en_entry->read_proc = proc_read_usb_log_en;   // cat /proc/driver/usb/log_en
    usb_log_en_entry->write_proc = proc_write_usb_log_en; // echo 0x07 > /proc/driver/usb/log_en
    //usb_log_en_entry->owner = THIS_MODULE;

    usb_vbus_entry = create_proc_entry("vbus", 0755, usb_proc_dir);
    if(usb_vbus_entry == NULL) {
        goto proc_init_fail;
    }
    usb_vbus_entry->read_proc = proc_read_usb_vbus;   // cat /proc/driver/usb/log_en
    usb_vbus_entry->write_proc = proc_write_usb_vbus; // echo 0x07 > /proc/driver/usb/log_en


    usb_setting_entry = create_proc_entry("setting", 0755, usb_proc_dir);
    if(usb_setting_entry == NULL) {
        goto proc_init_fail;
    }
    usb_setting_entry->read_proc = proc_read_usb_setting;   // cat /proc/driver/usb/setting
    usb_setting_entry->write_proc = proc_write_usb_setting; // echo 0x07 > /proc/driver/usb/setting	
    usb_suspend_entry =  create_proc_entry("suspend", 0755, usb_proc_dir);
    if(usb_suspend_entry == NULL) {
        goto proc_init_fail;
    }
    usb_suspend_entry->read_proc = proc_read_usb_suspend;   // cat /proc/driver/usb/suspend

	usb_resume_entry =  create_proc_entry("resume", 0755, usb_proc_dir);
    if(usb_resume_entry == NULL) {
        goto proc_init_fail;
    }
    usb_resume_entry->read_proc = proc_read_usb_resume;   // cat /proc/driver/usb/resume
#endif  // #ifdef USB_READ_WRITE_TEST

proc_init_fail:

    return -1;
}

void __exit usb_uninit_procfs(void)
{
    if(usb_rw_test_entry)
    {
        printk("[USB] Remove /proc/driver/usb/rw_test\n");
        remove_proc_entry("rw_test", usb_proc_dir);
        usb_rw_test_entry = NULL;
    }
    
    if(usb_stress_test_entry)
    {
        printk("[USB] Remove /proc/driver/usb/stress\n");
        remove_proc_entry("stress", usb_proc_dir);       
        usb_stress_test_entry = NULL;
    }
    
    if(usb_log_en_entry)
    {
        printk("[USB] Remove /proc/driver/usb/log_en\n");
        remove_proc_entry("log_en", usb_proc_dir);       
        usb_log_en_entry = NULL;
    }

    if(usb_vbus_entry)
    {
        printk("[USB] Remove /proc/driver/usb/vbus\n");
        remove_proc_entry("vbus", usb_proc_dir);       
        usb_vbus_entry = NULL;
    }	
    
    if(usb_setting_entry)
    {
        printk("[USB] Remove /proc/driver/usb/setting\n");
        remove_proc_entry("setting", usb_proc_dir);       
        usb_setting_entry = NULL;
    }
    
    if(usb_root_port_entry)
    {
        printk("[USB] Remove /proc/driver/usb/root_port\n");
        remove_proc_entry("root_port", usb_proc_dir);       
        usb_root_port_entry = NULL;
    }
    if(usb_suspend_entry)
    {
        printk("[USB] Remove /proc/driver/usb/suspend\n");
        remove_proc_entry("suspend", usb_proc_dir);       
        usb_suspend_entry = NULL;
    }

    if(usb_resume_entry)
    {
        printk("[USB] Remove /proc/driver/usb/resume\n");
        remove_proc_entry("resume", usb_proc_dir);       
        usb_resume_entry = NULL;
    }
    if(usb_proc_dir)
    {
        printk("[USB] Remove /proc/driver/usb\n");
        remove_proc_entry("driver/usb", NULL);       
        usb_proc_dir = NULL;
    }
}
#endif // #ifdef USB_READ_WRITE_TEST

#endif

