/*
 *  linux/arch/arm/mach-realview/hotplug.c
 *
 *  Copyright (C) 2002 ARM Ltd.
 *  All Rights Reserved
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/smp.h>
#include <linux/completion.h>

#include <mach/hardware.h>
#include <asm/delay.h>
#include <asm/smp_scu.h>
#include <asm/cp15.h>
#include <asm/cacheflush.h>
#include <asm/io.h>
#include <asm/smp_plat.h>

#include <mach/mt53xx_linux.h>

static inline u32 __bim_readl(u32 regaddr32)
{
    return __raw_readl(BIM_VIRT + regaddr32);
}

static inline void __bim_writel(u32 regval32, u32 regaddr32)
{
    __raw_writel(regval32, BIM_VIRT + regaddr32);
}

void BIM_CpuCA17Down(unsigned int cpu)
{
    unsigned int regval32;

#if defined(CONFIG_ARCH_MT5890) || defined(CONFIG_ARCH_MT5891)
#if 1
    if (mt53xx_get_ic_version() >= IC_VER_MT5890_AC) {
        do
        {
            regval32 = (__bim_readl(0x450) & 0x40);
            //printk(KERN_ERR "Waiting for cpu%d to WFI", cpu);
        } while(regval32 != 0x40);
    }
#endif

#if 1
    regval32 = __bim_readl(REG_CPU0_PWR_CTL + 4 * cpu);
    if (mt53xx_get_ic_version() < IC_VER_MT5890_AC) {
        regval32 |= CKISO;
    } else {
        regval32 |= ES3_CKISO;
    }
    __bim_writel(regval32, REG_CPU0_PWR_CTL + 4 * cpu);

    udelay(20);

    #ifndef CONFIG_MT53_FPGA
    regval32 = __bim_readl(REG_L1_PD_CTL);
    regval32 |= (1 << cpu);
    __bim_writel(regval32, REG_L1_PD_CTL);

    if (mt53xx_get_ic_version() < IC_VER_MT5890_AC) {
        do {
            regval32 = __bim_readl(REG_L1_PD_CTL);
        } while(!(((regval32 & L1_SRAM_ACK) >> 4) & (1 << cpu)));
    }
    else
    {
        do {
            regval32 = __bim_readl(REG_L1_PD_CTL);
        } while(!(((regval32 & ES3_L1_SRAM_ACK) >> 2) & (1 << cpu)));
    }
    #endif
#endif

    udelay(20);

    // Set reset enable bit to "1"
    regval32 = __bim_readl(REG_CPU0_PWR_CTL + 4 * cpu);
    regval32 |= PWR_RST_EN;
    __bim_writel(regval32, REG_CPU0_PWR_CTL + 4 * cpu);

    udelay(20);

#if 1
    regval32 = __bim_readl(REG_CA15ISOLATECPU);
    regval32 &= ~(1 << cpu);
    __bim_writel(regval32, REG_CA15ISOLATECPU);

    udelay(20);

    regval32 = __bim_readl(REG_CPU0_PWR_CTL + 4 * cpu);
    if (mt53xx_get_ic_version() < IC_VER_MT5890_AC) {
        regval32 &= ~PWR_ON;
    } else {
        regval32 &= ~ES3_PWR_ON;
    }
    __bim_writel(regval32, REG_CPU0_PWR_CTL + 4 * cpu);

    if (mt53xx_get_ic_version() < IC_VER_MT5890_AC) {
        do {
            regval32 = __bim_readl(REG_CPU0_PWR_CTL + 4 * cpu);
        } while( regval32 & PWR_ON_ACK);
    } else {
        do {
            regval32 = __bim_readl(REG_CPU0_PWR_CTL + 4 * cpu);
        } while( regval32 & ES3_PWR_ON_ACK);
    }

    udelay(20);

    regval32 = __bim_readl(REG_CPU0_PWR_CTL + 4 * cpu);
    if (mt53xx_get_ic_version() < IC_VER_MT5890_AC) {
        regval32 &= ~PWR_ON_2ND;
    } else {
        regval32 &= ~ES3_PWR_ON_2ND;
    }
    __bim_writel(regval32, REG_CPU0_PWR_CTL + 4 * cpu);

    if (mt53xx_get_ic_version() < IC_VER_MT5890_AC) {
        do {
            regval32 = __bim_readl(REG_CPU0_PWR_CTL + 4 * cpu);
        } while( regval32 & PWR_ON_2ND_ACK);
    } else {
        do {
            regval32 = __bim_readl(REG_CPU0_PWR_CTL + 4 * cpu);
        } while( regval32 & ES3_PWR_ON_2ND_ACK);
    }
    udelay(20);
#endif
#else
    cpu=cpu-1;
    //set pd to "1" //ok
    regval32 = __bim_readl(REG_CPU1_PWR_CTL + 4 * cpu);
    regval32 |= MEM_PD;
    __bim_writel(regval32,REG_CPU1_PWR_CTL + 4 * cpu);
    do {
        regval32 = __bim_readl(REG_CPU1_PWR_CTL + 4 * cpu);
    }while(!(regval32 & MEM_PD_ACK));

     // Set clamp bit to "1" //ng
    regval32 = __bim_readl(REG_CPU1_PWR_CTL + 4 * cpu);
    regval32 |= CLAMP;
    __bim_writel(regval32,REG_CPU1_PWR_CTL + 4 * cpu);
    udelay(20);

    // Set pwr clk_dis bit to "1"
    regval32 = __bim_readl(REG_CPU1_PWR_CTL + 4 * cpu);
    regval32 |= PWR_CLK_DIS;
    __bim_writel(regval32,REG_CPU1_PWR_CTL + 4 * cpu);
    udelay(20);

    // Set reset enable bit to "1"
    regval32 = __bim_readl(REG_CPU1_PWR_CTL + 4 * cpu);
    regval32 |= PWR_RST_EN;
    __bim_writel(regval32,REG_CPU1_PWR_CTL + 4 * cpu);
    udelay(20);

    //set pwr on to "0" //ok
    regval32 = __bim_readl(REG_CPU1_PWR_CTL + 4 * cpu);
    regval32 &= ~PWR_ON;
    __bim_writel(regval32,REG_CPU1_PWR_CTL + 4 * cpu);

    do {
        regval32 = __bim_readl(REG_CPU1_PWR_CTL + 4 * cpu);
    }while( regval32 & PWR_ON_ACK);
    udelay(20);

    //set pwr on 2nd to "0" //ng
    regval32 = __bim_readl(REG_CPU1_PWR_CTL + 4 * cpu);
    regval32 &= ~PWR_ON_2ND;
    __bim_writel(regval32,REG_CPU1_PWR_CTL + 4 * cpu);

    do {
        regval32 = __bim_readl(REG_CPU1_PWR_CTL + 4 * cpu);
    }while( regval32 & PWR_ON_2ND_ACK);
    udelay(20);

    cpu =cpu+1;
#endif

}


void BIM_CpuCA17Up(unsigned int cpu)
{
	unsigned int regval32;

#if defined(CONFIG_ARCH_MT5890) || defined(CONFIG_ARCH_MT5891)
#if 1
	// check if CPU is in reset mode
   	regval32 = __bim_readl(REG_CPU0_PWR_CTL + 4 * cpu);
	if(!(regval32 & PWR_RST_EN))
	{
	    // cpu is already up
	    return;
	}

#if 0
	regval32 = __bim_readl(REG_CPU0_PWR_CTL + 4 * cpu);
    if (mt53xx_get_ic_version() < IC_VER_MT5890_AC) {
	    regval32 &= ~CKISO;
    } else {
	    regval32 &= ~ES3_CKISO;
    }
	__bim_writel(regval32, REG_CPU0_PWR_CTL + 4 * cpu);

    udelay(20);
#endif

	regval32 = __bim_readl(REG_CPU0_PWR_CTL + 4 * cpu);
    if (mt53xx_get_ic_version() < IC_VER_MT5890_AC) {
	    regval32 |= PWR_ON;
    } else {
	    regval32 |= ES3_PWR_ON;
    }
	__bim_writel(regval32, REG_CPU0_PWR_CTL + 4 * cpu);

	#ifndef CONFIG_MT53_FPGA
    if (mt53xx_get_ic_version() < IC_VER_MT5890_AC) {
    	do {
    		regval32 = __bim_readl(REG_CPU0_PWR_CTL + 4 * cpu);
    	} while(!(regval32 & PWR_ON_ACK));
    } else {
    	do {
    		regval32 = __bim_readl(REG_CPU0_PWR_CTL + 4 * cpu);
    	} while(!(regval32 & ES3_PWR_ON_ACK));
    }
	#endif

    udelay(20);

	regval32 = __bim_readl(REG_CPU0_PWR_CTL + 4 * cpu);
    if (mt53xx_get_ic_version() < IC_VER_MT5890_AC) {
	    regval32 |= PWR_ON_2ND;
    } else {
	    regval32 |= ES3_PWR_ON_2ND;
    }
	__bim_writel(regval32, REG_CPU0_PWR_CTL + 4 * cpu);

	#ifndef CONFIG_MT53_FPGA
    if (mt53xx_get_ic_version() < IC_VER_MT5890_AC) {
    	do {
    		regval32 = __bim_readl(REG_CPU0_PWR_CTL + 4 * cpu);
    	} while(!(regval32 & PWR_ON_2ND_ACK));
    } else {
    	do {
    		regval32 = __bim_readl(REG_CPU0_PWR_CTL + 4 * cpu);
    	} while(!(regval32 & ES3_PWR_ON_2ND_ACK));
    }
    #endif

    udelay(20);

	regval32 = __bim_readl(REG_L1_PD_CTL);
	regval32 &= ~(1 << cpu);
	__bim_writel(regval32, REG_L1_PD_CTL);

	#ifndef CONFIG_MT53_FPGA
    if (mt53xx_get_ic_version() < IC_VER_MT5890_AC) {
    	do {
    		regval32 = __bim_readl(REG_L1_PD_CTL);
    	} while((((regval32 & L1_SRAM_ACK) >> 4) & (1 << cpu)));
    } else {
    	do {
    		regval32 = __bim_readl(REG_L1_PD_CTL);
    	} while((((regval32 & ES3_L1_SRAM_ACK) >> 2) & (1 << cpu)));
    }
	#endif

    udelay(20);

#if 1
	regval32 = __bim_readl(REG_CPU0_PWR_CTL + 4 * cpu);
    if (mt53xx_get_ic_version() < IC_VER_MT5890_AC) {
	    regval32 &= ~CKISO;
    } else {
	    regval32 &= ~ES3_CKISO;
    }
	__bim_writel(regval32, REG_CPU0_PWR_CTL + 4 * cpu);

    udelay(20);
#endif

	regval32 = __bim_readl(REG_CA15ISOLATECPU);
	regval32 |= (1 << cpu);
	__bim_writel(regval32, REG_CA15ISOLATECPU);

    udelay(20);
#endif

    // Set reset enable bit to "0"
   	regval32 = __bim_readl(REG_CPU0_PWR_CTL + 4 * cpu);
	regval32 &= ~PWR_RST_EN;
	__bim_writel(regval32, REG_CPU0_PWR_CTL + 4 * cpu);

    udelay(20);
#elif defined(CONFIG_ARCH_MT5882)||defined(CONFIG_ARCH_MT5883)
    cpu=cpu-1;
    //set pwr on to "1"
    regval32 = __bim_readl(REG_CPU1_PWR_CTL + 4* cpu);
    regval32 |= PWR_ON;
    __bim_writel(regval32, REG_CPU1_PWR_CTL + 4 * cpu);
    do {
        regval32 = __bim_readl(REG_CPU1_PWR_CTL + 4 * cpu);
    }while(!(regval32 & PWR_ON_ACK));

    //set pwr on 2nd to "1"
    regval32 = __bim_readl(REG_CPU1_PWR_CTL + 4 * cpu);
    regval32 |= PWR_ON_2ND;
    __bim_writel(regval32, REG_CPU1_PWR_CTL + 4 * cpu);

    #ifndef CONFIG_MT53_FPGA
    do {
        regval32 = __bim_readl(REG_CPU1_PWR_CTL + 4 * cpu);
    }while(!(regval32 & PWR_ON_2ND_ACK));
    #endif
    udelay(20);

    // set pwr clk dis to "0"
    regval32 = __bim_readl(REG_CPU1_PWR_CTL + 4* cpu);
    regval32 &= ~PWR_CLK_DIS;
    __bim_writel(regval32, REG_CPU1_PWR_CTL + 4 * cpu);
    udelay(20);

    //set pd to "0"
    regval32 = __bim_readl(REG_CPU1_PWR_CTL + 4 * cpu);
    regval32 &= ~MEM_PD;
    __bim_writel(regval32, REG_CPU1_PWR_CTL + 4 * cpu);
    do {
        regval32 = __bim_readl(REG_CPU1_PWR_CTL + 4 * cpu);
    }while((regval32 & MEM_PD_ACK));
    udelay(20);

    //set clamp to "0"
    regval32 = __bim_readl(REG_CPU1_PWR_CTL + 4 * cpu);
    regval32 &= ~CLAMP;
    __bim_writel(regval32, REG_CPU1_PWR_CTL + 4 * cpu);
    udelay(20);

    // set pwr reset en to"0"
    regval32 = __bim_readl(REG_CPU1_PWR_CTL + 4* cpu);
    regval32 &= ~PWR_RST_EN;
    __bim_writel(regval32, REG_CPU1_PWR_CTL + 4 * cpu);
    udelay(20);

   cpu=cpu+1;
#endif
}

void BIM_CpuCA7Down(unsigned int cpu)
{
    #ifdef CONFIG_ARCH_MT5890

    unsigned int regval32;

    if (mt53xx_get_ic_version() >= IC_VER_MT5890_AC) {
        do
        {
            regval32 = (__bim_readl(0x450) & (1 << cpu));
            //printk(KERN_ERR "Waiting for cpu%d to WFI", cpu);
        } while(regval32 != (1 << cpu));
    }
#if 1
    regval32 = __bim_readl(REG_CA7_CORE0_PWR + 4 * cpu);
    regval32 |= CA7_CPU_CLAMP;
    __bim_writel(regval32, REG_CA7_CORE0_PWR + 4 * cpu);

    regval32 = __bim_readl(REG_CA7_CORE0_PD + 4 * cpu);
    regval32 &= ~CA7_CPU_ISOINTB;
    __bim_writel(regval32, REG_CA7_CORE0_PD + 4 * cpu);

    regval32 = __bim_readl(REG_CA7_CORE0_PD + 4 * cpu);
    regval32 |= CA7_CPU_CKISO;
    __bim_writel(regval32, REG_CA7_CORE0_PD + 4 * cpu);

    regval32 = __bim_readl(REG_CA7_CORE0_PD + 4 * cpu);
    regval32 |= CA7_CPU_PD;
    __bim_writel(regval32, REG_CA7_CORE0_PD + 4 * cpu);

    do {
        regval32 = __bim_readl(REG_CA7_CORE0_PD + 4 * cpu);
    } while(!(regval32 & CA7_CPU_PD_ACK));
#endif


    regval32 = __bim_readl(REG_CA7_CORE0_PWR + 4 * cpu);
    regval32 |= CA7_CPU_RST_EN;
    __bim_writel(regval32, REG_CA7_CORE0_PWR + 4 * cpu);

#if 1
    regval32 = __bim_readl(REG_CA7_CORE0_PWR + 4 * cpu);
    regval32 |= CA7_CPU_CLK_DIS;
    __bim_writel(regval32, REG_CA7_CORE0_PWR + 4 * cpu);


    regval32 = __bim_readl(REG_CA7_CORE0_PWR + 4 * cpu);
    regval32 &= ~CA7_CPU_PWR_ON;
    __bim_writel(regval32, REG_CA7_CORE0_PWR + 4 * cpu);

    do {
        regval32 = __bim_readl(REG_CA7_CORE0_PWR + 4 * cpu);
    } while(regval32 & CA7_CPU_PWR_ACK);

    regval32 = __bim_readl(REG_CA7_CORE0_PWR + 4 * cpu);
    regval32 &= ~CA7_CPU_PWR_2ND_ON;
    __bim_writel(regval32, REG_CA7_CORE0_PWR + 4 * cpu);

    do {
        regval32 = __bim_readl(REG_CA7_CORE0_PWR + 4 * cpu);
    } while(regval32 & CA7_CPU_PWR_ACK_2ND);

#endif
    #endif // CONFIG_ARCH_MT5890
}


void BIM_CpuCA7Up(unsigned int cpu)
{
    #ifdef CONFIG_ARCH_MT5890
	unsigned int regval32;

#if 1
	// check if CPU is in reset mode
	regval32 = __bim_readl(REG_CA7_CORE0_PWR + 4 * cpu);
    if(!(regval32 & CA7_CPU_RST_EN))
    {
        // cpu is already up
        return;
    }


    regval32 = __bim_readl(REG_CA7_CORE0_PWR + 4 * cpu);
    regval32 |= CA7_CPU_PWR_ON;
    __bim_writel(regval32, REG_CA7_CORE0_PWR + 4 * cpu);

    do {
        regval32 = __bim_readl(REG_CA7_CORE0_PWR + 4 * cpu);
    } while(!(regval32 & CA7_CPU_PWR_ACK));

    regval32 = __bim_readl(REG_CA7_CORE0_PWR + 4 * cpu);
    regval32 |= CA7_CPU_PWR_2ND_ON;
    __bim_writel(regval32, REG_CA7_CORE0_PWR + 4 * cpu);

    do {
        regval32 = __bim_readl(REG_CA7_CORE0_PWR + 4 * cpu);
    } while(!(regval32 & CA7_CPU_PWR_ACK_2ND));


    regval32 = __bim_readl(REG_CA7_CORE0_PD + 4 * cpu);
    regval32 &= ~CA7_CPU_PD;
    __bim_writel(regval32, REG_CA7_CORE0_PD + 4 * cpu);

    do {
        regval32 = __bim_readl(REG_CA7_CORE0_PD + 4 * cpu);
    } while(regval32 & CA7_CPU_PD_ACK);

    regval32 = __bim_readl(REG_CA7_CORE0_PD + 4 * cpu);
    regval32 |= CA7_CPU_ISOINTB;
    __bim_writel(regval32, REG_CA7_CORE0_PD + 4 * cpu);

    regval32 = __bim_readl(REG_CA7_CORE0_PD + 4 * cpu);
    regval32 &= ~CA7_CPU_CKISO;
    __bim_writel(regval32, REG_CA7_CORE0_PD + 4 * cpu);

    regval32 = __bim_readl(REG_CA7_CORE0_PWR + 4 * cpu);
    regval32 &= ~CA7_CPU_CLAMP;
    __bim_writel(regval32, REG_CA7_CORE0_PWR + 4 * cpu);

    regval32 = __bim_readl(REG_CA7_CORE0_PWR + 4 * cpu);
    regval32 &= ~CA7_CPU_CLK_DIS;
    __bim_writel(regval32, REG_CA7_CORE0_PWR + 4 * cpu);
#endif

    regval32 = __bim_readl(REG_CA7_CORE0_PWR + 4 * cpu);
    regval32 &= ~CA7_CPU_RST_EN;
    __bim_writel(regval32, REG_CA7_CORE0_PWR + 4 * cpu);
    #endif // CONFIG_ARCH_MT5890
}

void BIM_ClusterEnable(unsigned int u4Cluster)
{
	unsigned int regval32;

	if(u4Cluster != 1)
	{
	    //Printf("we only turn on cluster1, but not %u\n", u4Cluster);
		return;
	}

    // enable ca7 cci snoop
    regval32 = *(unsigned int *)0xf007811c;
    regval32 &= ~(1 << 4);
    *(unsigned int *)0xf007811c = regval32;

    // enable ca7 cci snoop
    regval32 = *(unsigned int *)0xf2095000;
    regval32 |= 0x3;
    *(unsigned int *)0xf2095000 = regval32;
}

void BIM_ClusterDisable(unsigned int u4Cluster)
{
	unsigned int regval32;

	if(u4Cluster != 1)
	{
	    //Printf("we only turn on cluster1, but not %u\n", u4Cluster);
		return;
	}

    // disable ca7 cci snoop
    regval32 = *(unsigned int *)0xf2095000;
    regval32 &= ~0x3;
    *(unsigned int *)0xf2095000 = regval32;

    // disable ca7 cci snoop - acinactm
    regval32 = *(unsigned int *)0xf007811c;
    regval32 |= (1 << 4);
    *(unsigned int *)0xf007811c = regval32;
}

void BIM_ClusterDown(unsigned int u4Cluster)
{
    #ifdef CONFIG_ARCH_MT5890
	unsigned int regval32;

	if(u4Cluster != 1)
	{
	    //Printf("we only turn on cluster1, but not %u\n", u4Cluster);
		return;
	}

    //
    // dbg power down
    //
    regval32 = __bim_readl(REG_CA7_DBG_PWR);
    regval32 |= CA7_DBG_CLAMP;
    __bim_writel(regval32, REG_CA7_DBG_PWR);

    regval32 = __bim_readl(REG_CA7_DBG_PWR);
    regval32 |= CA7_DBG_RST_EN;
    __bim_writel(regval32, REG_CA7_DBG_PWR);

    regval32 = __bim_readl(REG_CA7_DBG_PWR);
    regval32 |= CA7_CPUSYS_CLK_DIS;
    __bim_writel(regval32, REG_CA7_DBG_PWR);


    regval32 = __bim_readl(REG_CA7_DBG_PWR);
    regval32 &= ~CA7_DBG_PWR_ON;
    __bim_writel(regval32, REG_CA7_DBG_PWR);

    do {
        regval32 = __bim_readl(REG_CA7_DBG_PWR);
    } while(regval32 & CA7_DBG_PWR_ACK);

    regval32 = __bim_readl(REG_CA7_DBG_PWR);
    regval32 &= ~CA7_DBG_PWR_2ND_ON;
    __bim_writel(regval32, REG_CA7_DBG_PWR);

    do {
        regval32 = __bim_readl(REG_CA7_DBG_PWR);
    } while(regval32 & CA7_DBG_PWR_ACK_2ND);


    //
    // CPUSYS power down
    //
    regval32 = __bim_readl(REG_CA7_CACTIVE);
    regval32 &= ~CA7_PWRDNREQN;
    __bim_writel(regval32, REG_CA7_CACTIVE);

    do {
        regval32 = __bim_readl(REG_CA7_CACTIVE);
    } while(regval32 & CA7_PWRDNACKN);

    regval32 = __bim_readl(REG_CA7_CPUSYS_TOP_PWR);
    regval32 |= CA7_CPUSYS_TOP_CLAMP;
    __bim_writel(regval32, REG_CA7_CPUSYS_TOP_PWR);

    regval32 = __bim_readl(REG_CA7_CPUSYS_PD);
    regval32 &= ~CA7_CPUSYS_ISOINTB;
    __bim_writel(regval32, REG_CA7_CPUSYS_PD);

    regval32 = __bim_readl(REG_CA7_CPUSYS_PD);
    regval32 |= CA7_CPUSYS_CKISO;
    __bim_writel(regval32, REG_CA7_CPUSYS_PD);

    regval32 = __bim_readl(REG_CA7_CPUSYS_PD);
    regval32 |= CA7_CPUSYS_PD;
    __bim_writel(regval32, REG_CA7_CPUSYS_PD);

    do {
        regval32 = __bim_readl(REG_CA7_CPUSYS_PD);
    } while(!(regval32 & CA7_CPUSYS_PD_ACK));

    regval32 = __bim_readl(REG_CA7_CPUSYS_TOP_PWR);
    regval32 |= CA7_CPUSYS_RST_EN;
    __bim_writel(regval32, REG_CA7_CPUSYS_TOP_PWR);

    regval32 = __bim_readl(REG_CA7_CPUSYS_TOP_PWR);
    regval32 |= CA7_CPUSYS_CLK_DIS;
    __bim_writel(regval32, REG_CA7_CPUSYS_TOP_PWR);


    regval32 = __bim_readl(REG_CA7_CPUSYS_TOP_PWR);
    regval32 &= ~CA7_CPUSYS_PWR_ON;
    __bim_writel(regval32, REG_CA7_CPUSYS_TOP_PWR);

    #ifndef CONFIG_MT53_FPGA
    do {
        regval32 = __bim_readl(REG_CA7_CPUSYS_TOP_PWR);
    } while(regval32 & CA7_CPUSYS_PWR_ACK);
    #endif

    regval32 = __bim_readl(REG_CA7_CPUSYS_TOP_PWR);
    regval32 &= ~CA7_CPUSYS_PWR_2ND_ON;
    __bim_writel(regval32, REG_CA7_CPUSYS_TOP_PWR);

    do {
        regval32 = __bim_readl(REG_CA7_CPUSYS_TOP_PWR);
    } while(regval32 & CA7_CPUSYS_PWR_ACK_2ND);
    #endif // CONFIG_ARCH_MT5890
}


void BIM_ClusterUp(unsigned int u4Cluster)
{
    #ifdef CONFIG_ARCH_MT5890
	unsigned int regval32;

	if(u4Cluster != 1)
	{
	    //Printf("we only turn on cluster1, but not %u\n", u4Cluster);
		return;
	}

    //
    // CPUSYS power on
    //
	regval32 = __bim_readl(REG_CA7_CPUSYS_TOP_PWR);
	regval32 |= CA7_CPUSYS_PWR_ON;
	__bim_writel(regval32, REG_CA7_CPUSYS_TOP_PWR);

    #ifndef CONFIG_MT53_FPGA
	do {
		regval32 = __bim_readl(REG_CA7_CPUSYS_TOP_PWR);
	} while(!(regval32 & CA7_CPUSYS_PWR_ACK));
    #endif

	regval32 = __bim_readl(REG_CA7_CPUSYS_TOP_PWR);
	regval32 |= CA7_CPUSYS_PWR_2ND_ON;
	__bim_writel(regval32, REG_CA7_CPUSYS_TOP_PWR);

	do {
		regval32 = __bim_readl(REG_CA7_CPUSYS_TOP_PWR);
	} while(!(regval32 & CA7_CPUSYS_PWR_ACK_2ND));

    regval32 = __bim_readl(REG_CA7_CPUSYS_PD);
    regval32 &= ~CA7_CPUSYS_PD;
    __bim_writel(regval32, REG_CA7_CPUSYS_PD);

    do {
        regval32 = __bim_readl(REG_CA7_CPUSYS_PD);
    } while(regval32 & CA7_CPUSYS_PD_ACK);

    regval32 = __bim_readl(REG_CA7_CPUSYS_PD);
    regval32 |= CA7_CPUSYS_ISOINTB;
    __bim_writel(regval32, REG_CA7_CPUSYS_PD);

    regval32 = __bim_readl(REG_CA7_CPUSYS_PD);
    regval32 &= ~CA7_CPUSYS_CKISO;
    __bim_writel(regval32, REG_CA7_CPUSYS_PD);

    regval32 = __bim_readl(REG_CA7_CPUSYS_TOP_PWR);
    regval32 &= ~CA7_CPUSYS_TOP_CLAMP;
    __bim_writel(regval32, REG_CA7_CPUSYS_TOP_PWR);

    regval32 = __bim_readl(REG_CA7_CPUSYS_TOP_PWR);
    regval32 &= ~CA7_CPUSYS_CLK_DIS;
    __bim_writel(regval32, REG_CA7_CPUSYS_TOP_PWR);

    regval32 = __bim_readl(REG_CA7_CPUSYS_TOP_PWR);
    regval32 &= ~CA7_CPUSYS_RST_EN;
    __bim_writel(regval32, REG_CA7_CPUSYS_TOP_PWR);

    // check CPU WFI/WFE status

    regval32 = __bim_readl(REG_CA7_CACTIVE);
    regval32 |= CA7_PWRDNREQN;
    __bim_writel(regval32, REG_CA7_CACTIVE);

	do {
		regval32 = __bim_readl(REG_CA7_CACTIVE);
	} while(!(regval32 & CA7_PWRDNACKN));


    //
    // dbg power on
    //
	regval32 = __bim_readl(REG_CA7_DBG_PWR);
	regval32 |= CA7_DBG_PWR_ON;
	__bim_writel(regval32, REG_CA7_DBG_PWR);

	do {
		regval32 = __bim_readl(REG_CA7_DBG_PWR);
	} while(!(regval32 & CA7_DBG_PWR_ACK));

	regval32 = __bim_readl(REG_CA7_DBG_PWR);
	regval32 |= CA7_DBG_PWR_2ND_ON;
	__bim_writel(regval32, REG_CA7_DBG_PWR);

	do {
		regval32 = __bim_readl(REG_CA7_DBG_PWR);
	} while(!(regval32 & CA7_DBG_PWR_ACK_2ND));

    regval32 = __bim_readl(REG_CA7_DBG_PWR);
    regval32 &= ~CA7_DBG_CLAMP;
    __bim_writel(regval32, REG_CA7_DBG_PWR);

    regval32 = __bim_readl(REG_CA7_DBG_PWR);
    regval32 &= ~CA7_CPUSYS_CLK_DIS;
    __bim_writel(regval32, REG_CA7_DBG_PWR);

    regval32 = __bim_readl(REG_CA7_DBG_PWR);
    regval32 &= ~CA7_DBG_RST_EN;
    __bim_writel(regval32, REG_CA7_DBG_PWR);
    #endif // CONFIG_ARCH_MT5890
}


