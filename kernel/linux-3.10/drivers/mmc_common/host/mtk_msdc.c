#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/timer.h>
#include <linux/ioport.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/blkdev.h>
#include <linux/slab.h>
#include <linux/wakelock.h>
#include <linux/mmc/host.h>
#include <linux/mmc/card.h>
#include <linux/mmc/core.h>
#include <linux/mmc/mmc.h>
#include <linux/mmc/sd.h>
#include <linux/mmc/sdio.h>
#include <linux/dma-mapping.h>
#include <linux/irq.h>
#include <mach/mtk_gpio.h>
#include <mach/mt53xx_linux.h>
#include <mach/dma.h>
#include <mach/irqs.h>

#include <linux/proc_fs.h>
#include "../card/queue.h"
#include "dbg.h"

#ifdef CC_MTD_ENCRYPT_SUPPORT  
#include <crypto/mtk_crypto.h>
#endif



//#define MSDC_POWER_MC1 MSDC_VEMC_3V3 //Don't define this in local code!!!!!!
//#define MSDC_POWER_MC2 MSDC_VMC //Don't define this in local code!!!!!!

#include "mt_sd.h"

static struct workqueue_struct *workqueue;


#define EXT_CSD_BOOT_SIZE_MULT          226 /* R */
#define EXT_CSD_RPMB_SIZE_MULT          168 /* R */
#define EXT_CSD_GP1_SIZE_MULT           143 /* R/W 3 bytes */
#define EXT_CSD_GP2_SIZE_MULT           146 /* R/W 3 bytes */
#define EXT_CSD_GP3_SIZE_MULT           149 /* R/W 3 bytes */
#define EXT_CSD_GP4_SIZE_MULT           152 /* R/W 3 bytes */
#define EXT_CSD_PART_CFG                179 /* R/W/E & R/W/E_P */
#define CAPACITY_2G						(2 * 1024 * 1024 * 1024ULL)

#ifdef CONFIG_MTKEMMC_BOOT
u32 g_emmc_mode_switch = 0;
#endif


/* the global variable related to clcok */

u32 msdcClk[][10] = {{MSDC_CLK_TARGET},
                                      {MSDC_CLK_SRC_VAL},
                                      {MSDC_CLK_MODE_VAL},
                                      {MSDC_CLK_DIV_VAL},
                                      {MSDC_CLK_DRV_VAL},
                                      {MSDC_CMD_DRV_VAL},
                                      {MSDC_DAT_DRV_VAL}};


#define UNSTUFF_BITS(resp,start,size)					\
	({								\
		const int __size = size;				\
		const u32 __mask = (__size < 32 ? 1 << __size : 0) - 1;	\
		const int __off = 3 - ((start) / 32);			\
		const int __shft = (start) & 31;			\
		u32 __res;						\
									\
		__res = resp[__off] >> __shft;				\
		if (__size + __shft > 32)				\
			__res |= resp[__off-1] << ((32 - __shft) % 32);	\
		__res & __mask;						\
	})



struct mmc_blk_data {
	spinlock_t	lock;
	struct gendisk	*disk;
	struct mmc_queue queue;

	unsigned int	usage;
	unsigned int	read_only;
};


#define DRV_NAME            "MTK_MSDC"

#define MSDC_SUPPORT_DISABLE_BKOPS  1

#if MSDC_SUPPORT_DISABLE_BKOPS
struct timer_list bkops_timer;	
bool volatile g_msdc_bkops_enable = false;
bool volatile g_hynix_emmc = false;
#endif

struct timer_list card_detect_timer;	/* card detection timer */	

#define MSDC_GPIO_MAX_NUMBERS 6     
int MSDC_Gpio[MSDC_GPIO_MAX_NUMBERS]= {0} ;
#ifdef CC_SDMMC_SUPPORT

static struct resource mt_resource_msdc1[] = 
{
    {
        .start  = MSDC_0_BASE,
        .end    = MSDC_0_BASE + 0x108,
        .flags  = IORESOURCE_MEM,
    },
    {
        .start  = VECTOR_MSDC,
        .flags  = IORESOURCE_IRQ,
    },
};

struct msdc_hw msdc1_hw = 
{
    .clk_src        = -1, 
    .cmd_edge       = MSDC_SMPL_FALLING,
    .rdata_edge     = MSDC_SMPL_FALLING,
    .wdata_edge     = MSDC_SMPL_FALLING,
    .clk_drv        = 0,
    .cmd_drv        = 0,
    .dat_drv        = 0,
    .clk_drv_sd_18	= 3,
    .cmd_drv_sd_18	= 3,
    .dat_drv_sd_18	= 3,
    .data_pins      = 4,
    .data_offset    = 0,
    .flags          = MSDC_SDCARD_FLAG,
    .dat0rddly		= 0,
    .dat1rddly		= 0,
    .dat2rddly		= 0,
    .dat3rddly		= 0,
    .dat4rddly		= 0,
    .dat5rddly		= 0,
    .dat6rddly		= 0,
    .dat7rddly		= 0,
    .datwrddly		= 0,
    .cmdrrddly		= 0,
    .cmdrddly		= 0,
    .host_function	= MSDC_SD,
    .boot			= 0,
};
#endif

#ifdef CONFIG_MTKEMMC_BOOT
static struct resource mt_resource_msdc0[] = 
{
    {
        .start  = MSDC_1_BASE,
        .end    = MSDC_1_BASE + 0x108,
        .flags  = IORESOURCE_MEM,
    },
    {
        .start  = VECTOR_MSDC2,
        .flags  = IORESOURCE_IRQ,
    },
};

struct msdc_hw msdc0_hw = 
{
	    .clk_src        = -1,
	    .cmd_edge       = MSDC_SMPL_FALLING,
		.rdata_edge 	= MSDC_SMPL_FALLING,
		.wdata_edge 	= MSDC_SMPL_FALLING,
	    .clk_drv        = 0,
	    .cmd_drv        = 0,
	    .dat_drv        = 0,
	    #ifdef CC_EMMC_4BIT
	    .data_pins      = 4,
	    #else
	    .data_pins      = 8,	    
	    #endif
	    .data_offset    = 0,
	    .flags          = MSDC_EMMC_FLAG,
	    .dat0rddly		= 0,
      	.dat1rddly		= 0,
      	.dat2rddly		= 0,
      	.dat3rddly		= 0,
      	.dat4rddly		= 0,
      	.dat5rddly		= 0,
      	.dat6rddly		= 0,
      	.dat7rddly		= 0,
      	.datwrddly		= 0,
      	.cmdrrddly		= 0,
      	.cmdrddly		= 0,
	    .host_function	= MSDC_EMMC,
	    .boot			= MSDC_BOOT_EN,
};
#endif


static u32 emmcidx = 0;

static struct platform_device mt_device_msdc[] =
{

#ifdef CONFIG_MTKEMMC_BOOT
    {
        .name           = DRV_NAME,
        .id             = 0,
        .num_resources  = ARRAY_SIZE(mt_resource_msdc0),
        .resource       = mt_resource_msdc0,
        .dev = {
            .platform_data = &msdc0_hw,
            .coherent_dma_mask = MTK_MSDC_DMA_MASK,
            .dma_mask = &(mt_device_msdc[0].dev.coherent_dma_mask),
        },
    },
#endif
#ifdef CC_SDMMC_SUPPORT
    {
        .name           = DRV_NAME,
        .id             = 1,
        .num_resources  = ARRAY_SIZE(mt_resource_msdc1),
        .resource       = mt_resource_msdc1,
        .dev = {
            .platform_data = &msdc1_hw,
            .coherent_dma_mask = MTK_MSDC_DMA_MASK,
            .dma_mask = &(mt_device_msdc[1].dev.coherent_dma_mask),
        },
    },
#endif
};



#if defined(CC_MT5399) || defined(CONFIG_ARCH_MT5399)|| \
    defined(CC_MT5882) || defined(CONFIG_ARCH_MT5882)
const EMMC_FLASH_DEV_T _arEMMC_DevInfo1[] =
{
  // Device name                  ID1         ID2     DS26Sample  DS26Delay   HS52Sample  HS52Delay   DDR52Sample DDR52Delay  HS200Sample HS200Delay//HS200Delay ,0x:WRDAT_CRCS_TA_CNTR,CKGEN_MSDC_DLY_SEL,PAD_DAT_RD_RXDLY,PAD_DAT_WR_RXDLY
  {"UNKNOWN",                 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x1861001c, 0x01000100, 0x00000102, 0x0000000A, 0x00000102, 0x0000030D},
  {"H26M21001ECR",            0x4A48594E, 0x49582056, 0x00000000, 0x00000000, 0x00000006, 0x00000000, 0x00000102, 0x00000000, 0x00000006, 0x00000000},
  {"H26M31003FPR",            0x4A205849, 0x4E594812, 0x00000000, 0x00000000, 0x00000006, 0x00000000, 0x00000102, 0x00000000, 0x00000006, 0x00000000},
  {"H26M31003GMR",            0x4A483447, 0x31640402, 0x00000000, 0x00000000, 0x00000006, 0x00000000, 0x00000102, 0x00000000, 0x00000002, 0x0000030D},
  {"H26M31001HPR",            0x4A483447, 0x32611101, 0x00000000, 0x00000F0F, 0x1861001c, 0x01000100, 0x00000002, 0x03000d0d, 0x00000002, 0x03060d0f},
  {"H26M52103FMR",            0x4A484147, 0x32650502, 0x00000000, 0x00000000, 0x00000006, 0x00000000, 0x00000002, 0x00040F0F, 0x00000100, 0x04150917},
  {"H26M41103HPR",            0x4A483847, 0x31650502, 0x00000000, 0x00000000, 0x00000006, 0x00000000, 0x00000002, 0x00000F0F, 0x00000000, 0x030a080f},
  {"SDIN5D2-4G",              0x0053454D, 0x30344790, 0x00000000, 0x00000000, 0x00000006, 0x00000000, 0x00000102, 0x00000000, 0x00000002, 0x0000030D},
  {"KLM2G1HE3F-B001",         0x004D3247, 0x31484602, 0x00000000, 0x00000000, 0x00000006, 0x00000000, 0x00000102, 0x00000000, 0x00000006, 0x00000000},
  {"KLM8G1GEAC-B031xxx",      0x004D3847, 0x3147430B, 0x00000000, 0x00000000, 0x00000006, 0x00000000, 0x00000002, 0x00000F0F, 0x00000002, 0x0309090C},
  {"KLMAG2GEAC-B031000",      0x004D4147, 0x3247430B, 0x00000000, 0x00000000, 0x00000006, 0x00000000, 0x00000102, 0x00040F0F, 0x00000002, 0x0309090C},
  {"THGBM3G4D1FBAIG",         0x00303032, 0x47303000, 0x00000000, 0x00000000, 0x00000006, 0x00000000, 0x00000102, 0x00000000, 0x00000006, 0x00000000},
  {"THGBM4G4D1FBAIG",         0x00303032, 0x47343900, 0x00000000, 0x00000000, 0x00000006, 0x00000000, 0x00000102, 0x00000000, 0x00000006, 0x00000000},
  {"THGBM4G5D1HBAIR",         0x00303034, 0x47343900, 0x00000000, 0x00000000, 0x00000006, 0x00000000, 0x00000102, 0x00000000, 0x00000006, 0x00000000},
  {"THGBM4G6D2HBAIR",         0x00303038, 0x47344200, 0x00000000, 0x00000000, 0x00000006, 0x00000000, 0x00000102, 0x00000000, 0x00000006, 0x00000000},
  {"MTFC8GACAAAM-1M WT",      0x4E503158, 0x58585812, 0x00000000, 0x00000F0F, 0x00000006, 0x00000F0F, 0x00000002, 0x03000d0d, 0x00000002, 0x03080808},// JWA61
  {"MTFC8GACAEAM-1M WT",      0x4E52314A, 0x35354110, 0x00000000, 0x00000000, 0x00000006, 0x00000F0F, 0x00000102, 0x0000130F, 0x00000000, 0x030B090C},
  {"MTFC4GACAEAM-1M WT",      0x4E52314A, 0x35344110, 0x00000000, 0x00000000, 0x00000006, 0x00000F0F, 0x00000102, 0x0000130F, 0x00000002, 0x02060808},//JWA80
  {"MTFC4GACAAAM-1M WT",	  0x4E503158, 0x58585812, 0x00000000, 0x00000F0F, 0x00000006, 0x00000F0F, 0x00000002, 0x03000d0d, 0x00000002, 0x03080808},
  {"MTFC4GMVEA-1M WT(JW857)", 0x4E4D4D43, 0x3034473A, 0x00000000, 0x00000000, 0x00000006, 0x00000000, 0x00000102, 0x0000000A, 0x00000006, 0x00000000},
  {"MTFC2GMVEA-0M WT(JW896)", 0x4E4D4D43, 0x3032473A, 0x00000000, 0x00000000, 0x00000006, 0x00000000, 0x00000102, 0x0000000A, 0x00000006, 0x00000000},
  {"MTFC8GLCDM-1M WT(JW962)", 0x4E503158, 0x58585814, 0x00000000, 0x00000000, 0x00000004, 0x00000000, 0x00000102, 0x0000000A, 0x00000102, 0x1800030D},
  {"THGBMAG6A2JBAIR",         0x00303038, 0x47393251, 0x00000000, 0x00000000, 0x00000006, 0x00000000, 0x00000102, 0x0000130F, 0x00000002, 0x0309080C},
  {"THGBM5G6A2JBAIR",         0x00303038, 0x47393200, 0x00000000, 0x00000000, 0x00000006, 0x00000000, 0x00000102, 0x0000000A, 0x00000102, 0x0000030D},
  {"THGBMBG5D1KBAIT",         0x00303034, 0x47453000, 0x00000000, 0x00000000, 0x00000006, 0x00000F0F, 0x00000102, 0x0000130F, 0x00000002, 0x0306090a},
  {"THGBMAG5A1JBAIR",         0x00303034, 0x47393051, 0x00000000, 0x00000000, 0x00000006, 0x00000000, 0x00000102, 0x0000000A, 0x00000102, 0x0000030D},
  {"THGBMAG7A2JBAIR",         0x00303136, 0x47393201, 0x00000000, 0x00000000, 0x00000006, 0x00000000, 0x00000102, 0x0000000A, 0x00000102, 0x0000030D},
  {"THGBMAG6A2JBAIR",         0x00303038, 0x47393251, 0x00000000, 0x00000000, 0x00000006, 0x00000000, 0x00000102, 0x0000130F, 0x00000002, 0x0309080C},
  {"THGBMBG6D1KBAIL-XXX",     0x00303038, 0x47453000, 0x00000000, 0x00000000, 0x00000006, 0x00000F0F, 0x00000102, 0x0000130F, 0x00000002, 0x0309090C},
};
#endif

#if defined(CC_MT5890)|| defined(CONFIG_ARCH_MT5890) 
const EMMC_FLASH_DEV_T _arEMMC_DevInfo1[] =
{
  // Device name                  ID1         ID2     DS26Sample  DS26Delay   HS52Sample  HS52Delay   DDR52Sample DDR52Delay  HS200Sample HS200Delay//HS200Delay ,0x:WRDAT_CRCS_TA_CNTR,CKGEN_MSDC_DLY_SEL,PAD_DAT_RD_RXDLY,PAD_DAT_WR_RXDLY
  {"UNKNOWN",                 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x1861001c, 0x01000100, 0x00000102, 0x0000000A, 0x00000102, 0x0000030D},
  {"H26M21001ECR",            0x4A48594E, 0x49582056, 0x00000000, 0x00000000, 0x00000006, 0x00000000, 0x00000102, 0x00000000, 0x00000006, 0x00000000},
  {"H26M31003FPR",            0x4A205849, 0x4E594812, 0x00000000, 0x00000000, 0x00000006, 0x00000000, 0x00000102, 0x00000000, 0x00000006, 0x00000000},
  {"H26M31003GMR",            0x4A483447, 0x31640402, 0x00000000, 0x00000000, 0x00000006, 0x00000000, 0x00000102, 0x00000000, 0x00000002, 0x0000030D},
  {"H26M31001HPR",            0x4A483447, 0x32611101, 0x00000000, 0x00000000, 0x10801010, 0x00100010, 0x00000002, 0x00000F0F, 0x00000002, 0x02090D08},
  {"H26M52103FMR",            0x4A484147, 0x32650502, 0x00000000, 0x00000000, 0x00000006, 0x00000000, 0x00000002, 0x00040F0F, 0x00000100, 0x04150917},
  {"H26M41103HPR",            0x4A483847, 0x31650502, 0x00000000, 0x00000000, 0x00000006, 0x00000000, 0x00000002, 0x00000F0F, 0x00000000, 0x030a080f},
  {"SDIN5D2-4G",              0x0053454D, 0x30344790, 0x00000000, 0x00000000, 0x00000006, 0x00000000, 0x00000102, 0x00000000, 0x00000002, 0x0000030D},
  {"KLM2G1HE3F-B001",         0x004D3247, 0x31484602, 0x00000000, 0x00000000, 0x00000006, 0x00000000, 0x00000102, 0x00000000, 0x00000006, 0x00000000},
  {"KLM8G1GEND-B031",         0x0038474E, 0x44335201, 0x00000000, 0x00000000, 0x00000006, 0x00000000, 0x00000002, 0x03000d0d, 0x00000002, 0x0309080c},
  {"KLMAG2GEND-B031",         0x0041474E, 0x44335201, 0x00000000, 0x00000000, 0x00000006, 0x00000000, 0x00000002, 0x03000d0d, 0x00000002, 0x0309080c},
  {"KLM8G1GEAC-B031xxx",      0x004D3847, 0x3147430B, 0x00000000, 0x00000000, 0x00000006, 0x00000000, 0x00000002, 0x00000F0F, 0x00000002, 0x0309090C},
  {"KLMAG2GEAC-B031000",      0x004D4147, 0x3247430B, 0x00000000, 0x00000000, 0x00000006, 0x00000000, 0x00000102, 0x00040F0F, 0x00000002, 0x0309090C},
  {"THGBM3G4D1FBAIG",         0x00303032, 0x47303000, 0x00000000, 0x00000000, 0x00000006, 0x00000000, 0x00000102, 0x00000000, 0x00000006, 0x00000000},
  {"THGBM4G4D1FBAIG",         0x00303032, 0x47343900, 0x00000000, 0x00000000, 0x00000006, 0x00000000, 0x00000102, 0x00000000, 0x00000006, 0x00000000},
  {"THGBM4G5D1HBAIR",         0x00303034, 0x47343900, 0x00000000, 0x00000000, 0x00000006, 0x00000000, 0x00000102, 0x00000000, 0x00000006, 0x00000000},
  {"THGBM4G6D2HBAIR",         0x00303038, 0x47344200, 0x00000000, 0x00000000, 0x00000006, 0x00000000, 0x00000102, 0x00000000, 0x00000006, 0x00000000},
  {"THGBMBG5D1KBAIL",         0x00303034, 0x47453000, 0x00000000, 0x00000000, 0x00000006, 0x00000000, 0x00000102, 0x00000000, 0x00000002, 0x04060315},
  {"MTFC8GACAAAM-1M WT",      0x4E503158, 0x58585812, 0x00000000, 0x00000000, 0x00000106, 0x0105150F, 0x00000002, 0x03000d0d, 0x00000100, 0x04080818},// JWA61
  {"MTFC8GACAEAM-1M WT",      0x4E52314A, 0x35354110, 0x00000000, 0x00000000, 0x00000006, 0x00000F0F, 0x00000102, 0x0000130F, 0x00000000, 0x030B090C},
  {"MTFC4GMVEA-1M WT(JW857)", 0x4E4D4D43, 0x3034473A, 0x00000000, 0x00000000, 0x00000006, 0x00000000, 0x00000102, 0x0000000A, 0x00000006, 0x00000000},
  {"MTFC2GMVEA-0M WT(JW896)", 0x4E4D4D43, 0x3032473A, 0x00000000, 0x00000000, 0x00000006, 0x00000000, 0x00000102, 0x0000000A, 0x00000006, 0x00000000},
  {"MTFC8GLCDM-1M WT(JW962)", 0x4E503158, 0x58585814, 0x00000000, 0x00000000, 0x00000004, 0x00000000, 0x00000102, 0x0000000A, 0x00000102, 0x1800030D},
  {"THGBMAG6A2JBAIR",         0x00303038, 0x47393251, 0x00000000, 0x00000000, 0x00000006, 0x00000000, 0x00000102, 0x0000130F, 0x00000002, 0x0309080C},
  {"THGBM5G6A2JBAIR",         0x00303038, 0x47393200, 0x00000000, 0x00000000, 0x00000006, 0x00000000, 0x00000102, 0x0000000A, 0x00000102, 0x0000030D},
  {"THGBMAG5A1JBAIR",         0x00303034, 0x47393051, 0x00000000, 0x00000000, 0x00000006, 0x00000000, 0x00000102, 0x0000000A, 0x00000102, 0x0000030D},
  {"THGBMAG7A2JBAIR",         0x00303136, 0x47393201, 0x00000000, 0x00000000, 0x00000006, 0x00000000, 0x00000102, 0x0000000A, 0x00000102, 0x0000030D},
  {"THGBMAG6A2JBAIR",         0x00303038, 0x47393251, 0x00000000, 0x00000000, 0x00000006, 0x00000000, 0x00000102, 0x0000130F, 0x00000002, 0x0309080C},
  {"THGBMBG6D1KBAIL-XXX",     0x00303038, 0x47453000, 0x00000000, 0x00000000, 0x00000006, 0x00000F0F, 0x00000102, 0x0000130F, 0x00000002, 0x0309090C},
  //new emmc
  {"KLM4G1FEPD-B031",         0x00344650, 0x44335202, 0x00000000, 0x00000000, 0x10801010, 0x00100010, 0x00000000, 0x00000000, 0x00000000, 0x00000000},
  {"THGBMDG5D1LBAIL",         0x00303034, 0x47363002, 0x00000000, 0x00000000, 0x10801010, 0x00100010, 0x00000000, 0x00000000, 0x00000000, 0x00000000},
};
#endif

#if defined(CC_EMMC_HS200)
#define HOST_MAX_MCLK       (200000000)
#else
#define HOST_MAX_MCLK       (52000000)
#endif

#define HOST_MIN_MCLK       (260000)

#define HOST_MAX_BLKSZ      (2048)

#define MSDC_OCR_AVAIL       (MMC_VDD_28_29 | MMC_VDD_29_30 | MMC_VDD_30_31 | MMC_VDD_31_32 | MMC_VDD_32_33)


//#define DEFAULT_DTOC        (3)      /* data timeout counter. 65536x40(75/77) /1048576 * 3(83/85) sclk. */

#define DEFAULT_DTOC        (0xff) //to check. follow dtv set to max

#define CMD_TIMEOUT         (HZ/10 * 5)   /* 100ms x5 */
#define DAT_TIMEOUT         (HZ    * 5)   /* 1000ms x5 */
#define POLLING_BUSY		(HZ	   * 3)
#define MAX_DMA_CNT         (64 * 1024 - 512)   /* a single transaction for WIFI may be 50K*/

#define MAX_HW_SGMTS        (MAX_BD_NUM)
#define MAX_PHY_SGMTS       (MAX_BD_NUM)
#define MAX_SGMT_SZ         (MAX_DMA_CNT)
#define MAX_REQ_SZ          (512*1024)

#define CMD_TUNE_UHS_MAX_TIME 	(2*32*8*8)
#define CMD_TUNE_HS_MAX_TIME 	(2*32)

#define READ_TUNE_UHS_CLKMOD1_MAX_TIME 	(2*32*32*8)
#define READ_TUNE_UHS_MAX_TIME 	(2*32*32)
#define READ_TUNE_HS_MAX_TIME 	(2*32)

#define WRITE_TUNE_HS_MAX_TIME 	(2*32)
#define WRITE_TUNE_UHS_MAX_TIME (2*32*8)


//=================================

#define MSDC_LOWER_FREQ
#define MSDC_MAX_FREQ_DIV   (2)  /* 200 / (4 * 2) */
#define MSDC_MAX_TIMEOUT_RETRY	(1)
#define MSDC_MAX_W_TIMEOUT_TUNE (5)
#define MSDC_MAX_R_TIMEOUT_TUNE	(3)


struct msdc_host *mtk_msdc_host[] = {NULL, NULL};
//static int g_intsts[] = {0, 0, 0, 0};
int g_dma_debug[HOST_MAX_NUM] = {0, 0};
transfer_mode msdc_latest_transfer_mode[HOST_MAX_NUM] = // 0 for PIO; 1 for DMA; 2 for nothing
{
   TRAN_MOD_NUM,
   TRAN_MOD_NUM
};

operation_type msdc_latest_operation_type[HOST_MAX_NUM] = // 0 for read; 1 for write; 2 for nothing
{
   OPER_TYPE_NUM,
   OPER_TYPE_NUM
};

struct dma_addr msdc_latest_dma_address[MAX_BD_PER_GPD];

static int msdc_rsp[] = {
    0,  /* RESP_NONE */
    1,  /* RESP_R1 */
    2,  /* RESP_R2 */
    3,  /* RESP_R3 */
    4,  /* RESP_R4 */
    1,  /* RESP_R5 */
    1,  /* RESP_R6 */
    1,  /* RESP_R7 */
    7,  /* RESP_R1b */
};

/* For Inhanced DMA */
#define msdc_init_gpd_ex(gpd,extlen,cmd,arg,blknum) \
    do { \
        ((gpd_t*)gpd)->extlen = extlen; \
        ((gpd_t*)gpd)->cmd    = cmd; \
        ((gpd_t*)gpd)->arg    = arg; \
        ((gpd_t*)gpd)->blknum = blknum; \
    }while(0)

#define msdc_init_bd(bd, blkpad, dwpad, dptr, dlen) \
    do { \
        BUG_ON(dlen > 0xFFFFUL); \
        ((bd_t*)bd)->blkpad = blkpad; \
        ((bd_t*)bd)->dwpad  = dwpad; \
        ((bd_t*)bd)->ptr    = (void*)dptr; \
        ((bd_t*)bd)->buflen = dlen; \
    }while(0)

#define msdc_txfifocnt()   ((sdr_read32(MSDC_FIFOCS) & MSDC_FIFOCS_TXCNT) >> 16)
#define msdc_rxfifocnt()   ((sdr_read32(MSDC_FIFOCS) & MSDC_FIFOCS_RXCNT) >> 0)
#define msdc_fifo_write32(v)   sdr_write32(MSDC_TXDATA, (v))
#define msdc_fifo_write8(v)    sdr_write8(MSDC_TXDATA, (v))
#define msdc_fifo_read32()   sdr_read32(MSDC_RXDATA)
#define msdc_fifo_read8()    sdr_read8(MSDC_RXDATA)

#define msdc_dma_on()        sdr_clr_bits(MSDC_CFG, MSDC_CFG_PIO)
#define msdc_dma_off()       sdr_set_bits(MSDC_CFG, MSDC_CFG_PIO)

static int msdc_card_exist(void *data);
static int msdc_clk_stable(struct msdc_host *host,u32 mode, u32 div);

#if MSDC_SUPPORT_DISABLE_BKOPS
/*
//can not be write, why ?
static void msdc_bkops_enable(int val)
{
    struct msdc_host *host_ctl = mtk_msdc_host[0];
	char ext_csd[512] = {0};

	printk(KERN_ERR "xhb emmc  , bkops val %d \n");

	
	mmc_send_ext_csd(host_ctl->mmc->card, ext_csd);
	printk("==>BKOPS: en %d, start %d, support %d, status %d ,then do\n",ext_csd[163],ext_csd[164],ext_csd[502],ext_csd[246]);

	
	if(!host_ctl || !host_ctl->mmc || !host_ctl->mmc->card)
	{
		printk("[msdc_bkops_enable] there is no card initialized in host[%d], NULL Pointer!\n",0);
		return ;
	}
	
	int ret = mmc_switch(host_ctl->mmc->card, EXT_CSD_CMD_SET_NORMAL,
			   EXT_CSD_BKOPS_EN, val, 0);

   
	if (ret)
	{
		printk(KERN_ERR "[xhb emmc]BKOPS   Failed  ret %d \n",ret);
	}
	else
	{
		printk(KERN_ERR "[xhb emmc]BKOPS   Success  \n");
	}

	
	mmc_send_ext_csd(host_ctl->mmc->card, ext_csd);
	printk("==>BKOPS: en %d, start %d, support %d, status %d \n",ext_csd[163],ext_csd[164],ext_csd[502],ext_csd[246]);

}
*/
static void msdc_bkops_timer(unsigned long arg)
{
	printk(KERN_ERR "[xhb emmc]BKOPS  Timer enter ,enable BKOPS \n");
	//msdc_bkops_enable(1);
	g_msdc_bkops_enable = true;
}
bool msdc_bkops_is_hynix_specific_emmc(void)
{
	//if ((0x4A483447 == _arEMMC_DevInfo1[emmcidx].u4ID1) && (0x32611101 == _arEMMC_DevInfo1[emmcidx].u4ID2))
	if (true == g_hynix_emmc)
	{
		//STOP BKOPS
		printk(KERN_ERR "[xhb emmc] Hynix emmc, special disable bkops \n");
		g_msdc_bkops_enable = false;

		init_timer(&bkops_timer);	 
		bkops_timer.data = 0;	 
		bkops_timer.function = msdc_bkops_timer;	
		bkops_timer.expires = jiffies + 20 * HZ;	
		add_timer(&bkops_timer);
		
		return true;
	}
	
	return false;
}

#endif

#ifdef CONFIG_MTKEMMC_BOOT
u8 ext_csd[512];
int offset = 0;
char partition_access = 0;

static int msdc_get_data(u8* dst,struct mmc_data *data)
{
	int left;
	u8* ptr;
	struct scatterlist *sg = data->sg;
	int num = data->sg_len;
        while (num) {
			left = sg_dma_len(sg);
   			ptr = (u8*)sg_virt(sg);
			memcpy(dst,ptr,left);
            sg = sg_next(sg);
			dst+=left;
			num--;
        }
	return 0;
}


static u32 msdc_get_other_capacity(void) //to check?
{
    u32 device_other_capacity = 0;
	device_other_capacity = ext_csd[EXT_CSD_BOOT_SIZE_MULT]* 128 * 1024
                + ext_csd[EXT_CSD_BOOT_SIZE_MULT] * 128 * 1024
                + ext_csd[EXT_CSD_RPMB_SIZE_MULT] * 128 * 1024
                + ext_csd[EXT_CSD_GP1_SIZE_MULT + 2] * 256 * 256
                + ext_csd[EXT_CSD_GP1_SIZE_MULT + 1] * 256
                + ext_csd[EXT_CSD_GP1_SIZE_MULT + 0]
                + ext_csd[EXT_CSD_GP2_SIZE_MULT + 2] * 256 * 256
                + ext_csd[EXT_CSD_GP2_SIZE_MULT + 1] * 256
                + ext_csd[EXT_CSD_GP2_SIZE_MULT + 0]
                + ext_csd[EXT_CSD_GP3_SIZE_MULT + 2] * 256 * 256
                + ext_csd[EXT_CSD_GP3_SIZE_MULT + 1] * 256
                + ext_csd[EXT_CSD_GP3_SIZE_MULT + 0]
                + ext_csd[EXT_CSD_GP4_SIZE_MULT + 2] * 256 * 256
                + ext_csd[EXT_CSD_GP4_SIZE_MULT + 1] * 256
                + ext_csd[EXT_CSD_GP4_SIZE_MULT + 0];
	return device_other_capacity;
}

//int msdc_get_offset(void)
//{
//    u32 l_offset;
//
//    l_offset =  MBR_START_ADDRESS_BYTE - msdc_get_other_capacity();
//
//    return (l_offset >> 9);
//}
//EXPORT_SYMBOL(msdc_get_offset);


static bool msdc_cal_offset(struct msdc_host *host)
{
	#if(0)
	u64 device_capacity = 0;
	offset =  MBR_START_ADDRESS_BYTE - msdc_get_other_capacity();//to check MBR_START_ADDRESS_BYTE
	device_capacity = msdc_get_user_capacity(host);
	if(device_capacity > CAPACITY_2G)
			offset /= 512;
    ERR_MSG("Address offset in USER REGION(Capacity %lld MB) is 0x%x",device_capacity/(1024*1024),offset);
    if(offset < 0) {
          ERR_MSG("XXX Address offset error(%d),please check MBR start address!!",(int)offset);
          BUG();
    }
    #endif
	return true;
}
#endif

static void msdc_dump_register(struct msdc_host *host)
{
    u32 base = host->base;
    //u32 off = 0;

    INIT_MSG("Reg[00] MSDC_CFG       = 0x%.8x", sdr_read32(base + 0x00));
    INIT_MSG("Reg[04] MSDC_IOCON     = 0x%.8x", sdr_read32(base + 0x04));
    INIT_MSG("Reg[08] MSDC_PS        = 0x%.8x", sdr_read32(base + 0x08));
    INIT_MSG("Reg[0C] MSDC_INT       = 0x%.8x", sdr_read32(base + 0x0C));
    INIT_MSG("Reg[10] MSDC_INTEN     = 0x%.8x", sdr_read32(base + 0x10));
    INIT_MSG("Reg[14] MSDC_FIFOCS    = 0x%.8x", sdr_read32(base + 0x14));
    INIT_MSG("Reg[18] MSDC_TXDATA    = not read");
    INIT_MSG("Reg[1C] MSDC_RXDATA    = not read");
    INIT_MSG("Reg[30] SDC_CFG        = 0x%.8x", sdr_read32(base + 0x30));
    INIT_MSG("Reg[34] SDC_CMD        = 0x%.8x", sdr_read32(base + 0x34));
    INIT_MSG("Reg[38] SDC_ARG        = 0x%.8x", sdr_read32(base + 0x38));
    INIT_MSG("Reg[3C] SDC_STS        = 0x%.8x", sdr_read32(base + 0x3C));
    INIT_MSG("Reg[40] SDC_RESP0      = 0x%.8x", sdr_read32(base + 0x40));
    INIT_MSG("Reg[44] SDC_RESP1      = 0x%.8x", sdr_read32(base + 0x44));
    INIT_MSG("Reg[48] SDC_RESP2      = 0x%.8x", sdr_read32(base + 0x48));
    INIT_MSG("Reg[4C] SDC_RESP3      = 0x%.8x", sdr_read32(base + 0x4C));
    INIT_MSG("Reg[50] SDC_BLK_NUM    = 0x%.8x", sdr_read32(base + 0x50));
    INIT_MSG("Reg[58] SDC_CSTS       = 0x%.8x", sdr_read32(base + 0x58));
    INIT_MSG("Reg[5C] SDC_CSTS_EN    = 0x%.8x", sdr_read32(base + 0x5C));
    INIT_MSG("Reg[60] SDC_DATCRC_STS = 0x%.8x", sdr_read32(base + 0x60));
    INIT_MSG("Reg[70] EMMC_CFG0      = 0x%.8x", sdr_read32(base + 0x70));
    INIT_MSG("Reg[74] EMMC_CFG1      = 0x%.8x", sdr_read32(base + 0x74));
    INIT_MSG("Reg[78] EMMC_STS       = 0x%.8x", sdr_read32(base + 0x78));
    INIT_MSG("Reg[7C] EMMC_IOCON     = 0x%.8x", sdr_read32(base + 0x7C));
    INIT_MSG("Reg[80] SD_ACMD_RESP   = 0x%.8x", sdr_read32(base + 0x80));
    INIT_MSG("Reg[84] SD_ACMD19_TRG  = 0x%.8x", sdr_read32(base + 0x84));
    INIT_MSG("Reg[88] SD_ACMD19_STS  = 0x%.8x", sdr_read32(base + 0x88));
    INIT_MSG("Reg[90] DMA_SA         = 0x%.8x", sdr_read32(base + 0x90));
    INIT_MSG("Reg[94] DMA_CA         = 0x%.8x", sdr_read32(base + 0x94));
    INIT_MSG("Reg[98] DMA_CTRL       = 0x%.8x", sdr_read32(base + 0x98));
    INIT_MSG("Reg[9C] DMA_CFG        = 0x%.8x", sdr_read32(base + 0x9C));
    INIT_MSG("Reg[A0] SW_DBG_SEL     = 0x%.8x", sdr_read32(base + 0xA0));
    INIT_MSG("Reg[A4] SW_DBG_OUT     = 0x%.8x", sdr_read32(base + 0xA4));
    INIT_MSG("Reg[B0] PATCH_BIT0     = 0x%.8x", sdr_read32(base + 0xB0));
    INIT_MSG("Reg[B4] PATCH_BIT1     = 0x%.8x", sdr_read32(base + 0xB4));
    INIT_MSG("Reg[EC] PAD_TUNE       = 0x%.8x", sdr_read32(base + 0xEC));
    INIT_MSG("Reg[F0] DAT_RD_DLY0    = 0x%.8x", sdr_read32(base + 0xF0));
    INIT_MSG("Reg[F4] DAT_RD_DLY1    = 0x%.8x", sdr_read32(base + 0xF4));
    INIT_MSG("Reg[F8] HW_DBG_SEL     = 0x%.8x", sdr_read32(base + 0xF8));
    INIT_MSG("Rg[100] MAIN_VER       = 0x%.8x", sdr_read32(base + 0x100));
    INIT_MSG("Rg[104] ECO_VER        = 0x%.8x", sdr_read32(base + 0x104));
}

static void msdc_dump_info(u32 id)
{
    struct msdc_host *host = mtk_msdc_host[id];
    u32 base;
    u32 temp;

    //return;

    if(host == NULL) {
        printk("msdc host<%d> null\r\n", id);
        return;
    }
    base = host->base;

    // 1: dump msdc hw register
    msdc_dump_register(host);

    // 2: check msdc clock gate and clock source
    //msdc_dump_clock_sts(host);

    // 3: check the register read_write
    temp = sdr_read32(base + 0xB0);
    INIT_MSG("patch reg[%x] = 0x%.8x", (base + 0xB0), temp);

    temp = (~temp);
    sdr_write32(base + 0xB0, temp);
    temp = sdr_read32(base + 0xB0);
    INIT_MSG("patch reg[%x] = 0x%.8x second time", (base + 0xB0), temp);

    temp = (~temp);
    sdr_write32(base + 0xB0, temp);
    temp = sdr_read32(base + 0xB0);
    INIT_MSG("patch reg[%x] = 0x%.8x Third time", (base + 0xB0), temp);

    // 4: For YD
    //msdc_debug_reg(host);

    //sd_debug_zone[id] = 0x3ff;
}

#ifdef CC_MTD_ENCRYPT_SUPPORT  

extern mtk_partition mtk_msdc_partition;
#define AES_ADDR_ALIGN   0x40
#define AES_LEN_ALIGN    0x20 
#define AES_BUFF_LEN     0x10000

static u8 _pu1MsdcBuf_AES[AES_BUFF_LEN + AES_ADDR_ALIGN], *_pu1MsdcBuf_Aes_Align;

void msdc_aes_init(void)
{  
	u32 tmp;	  
    if(NAND_AES_INIT())
    {  
        printk(KERN_ERR "yifan aes init ok\n");
    }
    else
    {
        printk(KERN_ERR "yifan aes init failed\n");	
    }
    
    //_pu1MsdcBuf_AES = (u8 *)__get_free_pages(GFP_KERNEL | __GFP_ZERO, get_order(AES_BUFF_LEN));
    //BUG_ON(!_pu1MsdcBuf_AES);
    
    tmp = ((u32)(_pu1MsdcBuf_AES)) & (AES_ADDR_ALIGN - 1);
    if(tmp != 0x0)
        _pu1MsdcBuf_Aes_Align = (UINT8 *)(((u32)_pu1MsdcBuf_AES & (~((u32)(AES_ADDR_ALIGN - 1)))) + AES_ADDR_ALIGN);
    else
    	   _pu1MsdcBuf_Aes_Align = _pu1MsdcBuf_AES;
    	   
    printk(KERN_ERR "_pu1MsdcBuf_Aes_Align is 0x%x, _pu1MsdcBuf_AES is 0x%x\n",(u32)_pu1MsdcBuf_Aes_Align, (u32)_pu1MsdcBuf_AES);
         
    return;     	
}

int msdc_partition_encrypted(u32 blkindx, struct msdc_host *host)
{
    int i = 0;
    BUG_ON(!host);    
    if(host->hw->host_function != MSDC_EMMC)
    	{
    		ERR_MSG("decrypt on emmc only!\n");
    		return -1;
    	}
    if(partition_access == 3)
    {
        printk("rpmb skip enc\n");
        return 0;
    }  
    
    for(i = 0;i < mtk_msdc_partition.nparts; i++)
    {
        if((mtk_msdc_partition.parts[i].attr & ATTR_ENCRYPTION) &&
        	 (blkindx >= mtk_msdc_partition.parts[i].start_sect) &&
        	 (blkindx < (mtk_msdc_partition.parts[i].start_sect + mtk_msdc_partition.parts[i].nr_sects)))	
        {
        	  N_MSG(FUC, " the buf(0x%08x) is encrypted!\n", blkindx);
            return 1;	
        }
    }
    
    return 0;	
}


int msdc_aes_encryption_sg(struct mmc_data *data,struct msdc_host *host)
{
    u32 len_used = 0, len_total = 0, len = 0, i = 0, buff = 0;
    struct scatterlist *sg;
    
    BUG_ON(!host);
    
    if(host->hw->host_function != MSDC_EMMC)
    	{
    		ERR_MSG("decrypt on emmc only!\n");
    		return -1;
    	}
    if(!data)
    {
    	    ERR_MSG("decrypt data is invalid\n");
    	    return -1;
    }
    
    BUG_ON(data->sg_len > 1);
    for_each_sg(data->sg, sg, data->sg_len, i)
    {
		     len = sg->length;
		     buff = (u32)sg_virt(sg);
		     
			  //if( (virt_to_phys((void *)buff) & (AES_ADDR_ALIGN - 1)) != 0x0)
			  if(1)
			  {
					  len_total = (u32)len;
					  do
					  {
					  	  len_used = (len_total > AES_BUFF_LEN)?AES_BUFF_LEN:len_total;
					  	  if((len_used & (AES_LEN_ALIGN - 1)) != 0x0)
                			 {
                			      ERR_MSG(" the buffer(0x%08x) to be encrypted is not align to %d bytes!\n", buff, AES_LEN_ALIGN);
                			      return -1;		
                			 }	
					      memcpy((void *)_pu1MsdcBuf_Aes_Align, (void *)buff, len_used);	
					      //sg_copy_to_buffer(sg,1,_pu1MsdcBuf_Aes_Align,len_used);
					      //u32 Aesphy = 0;
					      //Aesphy = dma_map_single(NULL, _pu1MsdcBuf_Aes_Align, len_used, DMA_BIDIRECTIONAL);
					      //if(NAND_AES_Encryption(Aesphy, Aesphy, len_used))
					    if(NAND_AES_Encryption(virt_to_phys((void *)_pu1MsdcBuf_Aes_Align), virt_to_phys((void *)_pu1MsdcBuf_Aes_Align), len_used))
				        {
				        	   //dma_unmap_single(NULL,Aesphy, len_used, DMA_BIDIRECTIONAL);
				           // N_MSG(FUC, "Encryption to buffer(addr:0x%08X size:0x%08X) success(cur:%08x)!\n", (u32)_pu1MsdcBuf_Aes_Align, len_used,
				               //                                                                   *((u32 *)_pu1MsdcBuf_Aes_Align));	
				        }
				        else
				        {
				            //dma_unmap_single(NULL, Aesphy, len_used, DMA_BIDIRECTIONAL);
				            ERR_MSG("Encryption to buffer(addr:0x%08X size:0x%08X) failed!\n", (u32)_pu1MsdcBuf_Aes_Align, len_used);	
				            return -1;    	
				        }	
				        memcpy((void *)buff, (void *)_pu1MsdcBuf_Aes_Align, len_used);
				        //sg_copy_from_buffer(sg,1,_pu1MsdcBuf_Aes_Align,len_used);
				        
				        len_total -= len_used;
				        buff += len_used;
				        
					  }while(len_total > 0);
			  }
			  else
			  {
			      if(NAND_AES_Encryption(virt_to_phys((void *)buff), virt_to_phys((void *)buff), len))
				    {
				        N_MSG(FUC,"Encryption to buffer(addr:0x%08X size:0x%08X) success(cur:%08x)!\n", buff, len, *((u32 *)buff));
				    }
				    else
				    {
				        ERR_MSG("Encryption to buffer(addr:0x%08X size:0x%08X) failed!\n", buff, len);	
				        return -1;    	
				    }		
			  }
	  }

	  return 0;	
}

int msdc_aes_decryption_sg(struct mmc_data *data,struct msdc_host *host)
{
    u32 len_used = 0, len_total = 0, len = 0, i = 0, buff = 0;
    struct scatterlist *sg;
    
    BUG_ON(!host);
    
    if(host->hw->host_function != MSDC_EMMC)
    	{
    		ERR_MSG("decrypt on emmc only!\n");
    		return -1;
    	}
    if(!data)
    {
    	    ERR_MSG("decrypt data is invalid\n");
    	    return -1;
    }
    BUG_ON(data->sg_len > 1);
     for_each_sg(data->sg, sg, data->sg_len, i)
     {
    	          len = sg->length;
		      buff = (u32)sg_virt(sg);
		    //ERR_MSG("sg total len is 0x%x\n", sg->length);
		//    BUG_ON(len > MAX_SGMT_SZ);
		    //BUG_ON(sg->offset);
		    
			  
			  u32 sgdmaddr = sg_dma_address(sg);
			  u32 sgphy= virt_to_phys((void *)buff);
			  if(!sgdmaddr || (sgdmaddr >= 0x20000000))
			  {
			  	printk(KERN_ERR "sgdmaddr is 0x%x, sgphy is 0x%x, sg virt is 0x%x. sg num is %d/%d, sg length is 0x%x\n",sgdmaddr,sgphy,(u32)buff, i,data->sg_len,sg->length);			  	 	
			  }
			  /*NG here: sg address may be high memory, and can't be used directly without kmap*/
		  	  struct page *page;
                unsigned int offset = 0;
                unsigned char *tempbuf;
		  	   page = sg_page(sg);
                offset = sg>offset;
                page = nth_page(page, (offset >> PAGE_SHIFT));
                offset %= PAGE_SIZE;
                if (PageHighMem(page)) 
                {
                    printk(KERN_ERR "sg page is high mem, sg_virt is 0x%x\n",(u32)buff);
                }
			  
			  /* it seems that aes hw do not support such kernel address(dma addr = 0, virt addr =  0xc0000000)*/
			  //if(((virt_to_phys((void *)buff) & (AES_ADDR_ALIGN - 1)) != 0x0) || (!sgdmaddr))
			  if(1)
			  {
					  len_total = len;
					  do
					  {
					  	  len_used = (len_total > AES_BUFF_LEN)?AES_BUFF_LEN:len_total;
					  	  if((len_used & (AES_LEN_ALIGN - 1)) != 0x0)
                			  {
                			      ERR_MSG("the buffer(0x%08x) to be decrypted is not align to %d bytes!\n", buff, AES_LEN_ALIGN);
                			      return -1;		
                			  }	
			  
					       memcpy((void *)_pu1MsdcBuf_Aes_Align, (void *)buff, len_used);	
					      //sg_copy_to_buffer(sg,1,_pu1MsdcBuf_Aes_Align,len_used);
					      //u32 Aesphy = 0;
					      //Aesphy = dma_map_single(NULL, _pu1MsdcBuf_Aes_Align, len_used, DMA_BIDIRECTIONAL);
					       if(NAND_AES_Decryption(virt_to_phys((void *)_pu1MsdcBuf_Aes_Align), virt_to_phys((void *)_pu1MsdcBuf_Aes_Align), len_used))
					      //if(NAND_AES_Decryption(Aesphy, Aesphy, len_used))
      				        {
     				            //ERR_MSG("Decryption to buffer(addr:0x%08X size:0x%08X) success(cur:%08x)!\n", (u32)_pu1MsdcBuf_Aes_Align, len_used,
      				                //                                                                         *((u32 *)_pu1MsdcBuf_Aes_Align));	
      				           //dma_unmap_single(NULL, Aesphy, len_used, DMA_BIDIRECTIONAL);
      				        }
      				        else
      				        {
      				        	   //dma_unmap_single(NULL, Aesphy, len_used, DMA_BIDIRECTIONAL);
      				            ERR_MSG("Decryption to buffer(addr:0x%08X size:0x%08X) failed!\n", (u32)_pu1MsdcBuf_Aes_Align, len_used);	
      				            return -1;    	
      				        }	
      				        memcpy((void *)buff, (void *)_pu1MsdcBuf_Aes_Align, len_used);
      				        //sg_copy_from_buffer(sg,1,_pu1MsdcBuf_Aes_Align,len_used);
      				        
      				        len_total -= len_used;
      				        buff += len_used;
				        
					  }while(len_total > 0);
		       }
		       else
			  {
			      //if(NAND_AES_Decryption(, virt_to_phys((void *)buff), len))
			      if(NAND_AES_Decryption(sg_dma_address(sg), sg_dma_address(sg), sg_dma_len(sg)))
				  {
				       N_MSG(FUC, "Decryption to buffer(addr:0x%08X size:0x%08X) success(cur:%08x)!\n", buff, len,
				                                                                                     *((u32 *)buff));	
				  }
				  else
				  {
				      ERR_MSG("Decryption to buffer(addr:0x%08X size:0x%08X) failed!\n", buff, len);	
				      return -1;    	
				  }		
			  }	
    }

	return 0;
}

#endif

static u32 msdc_power_tuning(struct msdc_host *host)
{
	return 1; //to check
}


/*
 * for AHB read / write debug
 * return DMA status.
 */
int msdc_get_dma_status(int host_id)
{
   int result = -1;

   if(host_id < 0 || host_id >= HOST_MAX_NUM)
   {
      printk(KERN_ERR "[%s] failed to get dma status, bad host_id %d\n", __func__, host_id);
      return result;
   }

   if(msdc_latest_transfer_mode[host_id] == TRAN_MOD_DMA)
   {
      switch(msdc_latest_operation_type[host_id])
      {
         case OPER_TYPE_READ:
             result = 1; // DMA read
             break;
         case OPER_TYPE_WRITE:
             result = 2; // DMA write
             break;
         default:
             break;
      }
   }else if(msdc_latest_transfer_mode[host_id] == TRAN_MOD_PIO){
      result = 0; // PIO mode
   }

   return result;
}
EXPORT_SYMBOL(msdc_get_dma_status);

struct dma_addr* msdc_get_dma_address(int host_id)
{
   bd_t* bd;
   int i;
   int mode = -1;
   struct msdc_host *host;
   u32 base;
   if(host_id < 0 || host_id >= HOST_MAX_NUM)
   {
      printk(KERN_ERR "[%s] failed to get dma status, bad host_id %d\n", __func__, host_id);
      return NULL;
   }

   if(!mtk_msdc_host[host_id])
   {
      printk(KERN_ERR "[%s] failed to get dma status, msdc%d is not exist\n", __func__, host_id);
      return NULL;
   }

   host = mtk_msdc_host[host_id];
   base = host->base;
   //spin_lock(&host->lock);
sdr_get_field(MSDC_DMA_CTRL, MSDC_DMA_CTRL_MODE, mode);
if(mode == 1){
	printk(KERN_CRIT "Desc.DMA\n");
   bd = host->dma.bd;
   i = 0;
   while(i < MAX_BD_PER_GPD){
       msdc_latest_dma_address[i].start_address = (u32)bd[i].ptr;
       msdc_latest_dma_address[i].size = bd[i].buflen;
       msdc_latest_dma_address[i].end = bd[i].eol;
       if(i>0)
          msdc_latest_dma_address[i-1].next = &msdc_latest_dma_address[i];

       if(bd[i].eol){
          break;
       }
       i++;
   }
}
else if(mode == 0){
	printk(KERN_CRIT "Basic DMA\n");
	msdc_latest_dma_address[i].start_address = sdr_read32(MSDC_DMA_SA);
#if defined(CC_MT5399) || defined(CONFIG_ARCH_MT5399)|| defined(CC_MT5890)|| defined(CONFIG_ARCH_MT5890)|| \
    defined(CC_MT5882) || defined(CONFIG_ARCH_MT5882)
    /* a change happens for dma xfer size:
          * a new 32-bit register(0xA8) is used for xfer size configuration instead of 16-bit register(0x98 DMA_CTRL)
          */
    msdc_latest_dma_address[i].size = sdr_read32(MSDC_DMA_LEN);
#else
	sdr_get_field(MSDC_DMA_CTRL, MSDC_DMA_CTRL_XFERSZ, msdc_latest_dma_address[i].size);
#endif
	msdc_latest_dma_address[i].end = 1;
}

   //spin_unlock(&host->lock);

   return msdc_latest_dma_address;

}
EXPORT_SYMBOL(msdc_get_dma_address);



#define msdc_retry(expr, retry, cnt,id) \
    do { \
        int backup = cnt; \
        while (retry) { \
            if (!(expr)) break; \
            if (cnt-- == 0) { \
                retry--; mdelay(1); cnt = backup; \
            } \
        } \
        if (retry == 0) { \
        } \
        WARN_ON(retry == 0); \
    } while(0)

#define msdc_reset(id) \
    do { \
        int retry = 3, cnt = 1000; \
        sdr_set_bits(MSDC_CFG, MSDC_CFG_RST); \
        mb(); \
        msdc_retry(sdr_read32(MSDC_CFG) & MSDC_CFG_RST, retry, cnt,id); \
    } while(0)

#define msdc_clr_int() \
    do { \
        volatile u32 val = sdr_read32(MSDC_INT); \
        sdr_write32(MSDC_INT, val); \
    } while(0)

#define msdc_clr_fifo(id) \
    do { \
        int retry = 3, cnt = 1000; \
        sdr_set_bits(MSDC_FIFOCS, MSDC_FIFOCS_CLR); \
        msdc_retry(sdr_read32(MSDC_FIFOCS) & MSDC_FIFOCS_CLR, retry, cnt,id); \
    } while(0)

#define msdc_reset_hw(id) \
		msdc_reset(id); \
		msdc_clr_fifo(id); \
		msdc_clr_int();


#define msdc_irq_save(val) \
    do { \
        val = sdr_read32(MSDC_INTEN); \
        sdr_clr_bits(MSDC_INTEN, val); \
    } while(0)

#define msdc_irq_restore(val) \
    do { \
        sdr_set_bits(MSDC_INTEN, val); \
    } while(0)


/* VMCH is for T-card main power.
 * VMC for T-card when no emmc, for eMMC when has emmc.
 * VGP for T-card when has emmc.
 */
u32 g_msdc0_io  = 0;
u32 g_msdc0_flash = 0;
u32 g_msdc1_io  = 0;
u32 g_msdc1_flash  = 0;
u32 g_msdc2_io = 0;
u32 g_msdc2_flash  = 0;
u32 g_msdc3_io  = 0;
u32 g_msdc3_flash  = 0;
u32 g_msdc4_io  = 0;
u32 g_msdc4_flash  = 0;



void msdc_set_driving(struct msdc_host *host,struct msdc_hw *hw,bool sd_18)
{
	u32 base = host->base; 	
    // PAD CTRL
    /* The value is determined by the experience
      * So, a best proper value should be investigate further.
      *   - when I test TF card, which is connected to main board by a SDIO daughter board,
      *      it will happen crc error in 48MHz, so I enhance pad drving strenght again.
      */
#if defined(CC_MT5399) || defined(CONFIG_ARCH_MT5399)|| \
    defined(CC_MT5882) || defined(CONFIG_ARCH_MT5882)
    sdr_clr_bits(MSDC_PAD_CTL0, (0x7<<15) |(0x7<<4) |(0x7<<0));
    sdr_clr_bits(MSDC_PAD_CTL1, (0x7<<15) |(0x7<<4) |(0x7<<0));
    sdr_clr_bits(MSDC_PAD_CTL2, (0x7<<15) |(0x7<<4) |(0x7<<0));	

    sdr_set_bits(MSDC_PAD_CTL0, ((0x4)<<15) |(((hw->clk_drv>>3)&0x7)<<4) | ((hw->clk_drv&0x7)<<0));
    sdr_set_bits(MSDC_PAD_CTL1, ((0x4)<<15) |(((hw->cmd_drv>>3)&0x7)<<4) | ((hw->cmd_drv&0x7)<<0));
    sdr_set_bits(MSDC_PAD_CTL2, ((0x4)<<15) |(((hw->dat_drv>>3)&0x7)<<4) | ((hw->dat_drv&0x7)<<0));
#endif

#if defined(CC_MT5890)|| defined(CONFIG_ARCH_MT5890)

    sdr_clr_bits(MSDC_PAD_CTL0, (0x7<<15) | (0x7<<0));
    sdr_clr_bits(MSDC_PAD_CTL1, (0x7<<15) | (0x7<<0));
    sdr_clr_bits(MSDC_PAD_CTL2, (0x7<<15) | (0x7<<0));	
    
    if(IS_IC_MT5861_ES1())
    {
    sdr_set_bits(MSDC_PAD_CTL0, ((0x4)<<15) | ((2&0x7)<<0));
    sdr_set_bits(MSDC_PAD_CTL1, ((0x4)<<15) | ((2&0x7)<<0));
    sdr_set_bits(MSDC_PAD_CTL2, ((0x4)<<15) | ((2&0x7)<<0));
    }
    else
    {
    sdr_set_bits(MSDC_PAD_CTL0, ((0x4)<<15) | ((hw->clk_drv&0x7)<<0));
    sdr_set_bits(MSDC_PAD_CTL1, ((0x4)<<15) | ((hw->cmd_drv&0x7)<<0));
    sdr_set_bits(MSDC_PAD_CTL2, ((0x4)<<15) | ((hw->dat_drv&0x7)<<0));
    }
#endif
}

void msdc_pinmux(struct msdc_host *host)
{
#if defined(CC_MT5399) || defined(CONFIG_ARCH_MT5399)|| \
    defined(CC_MT5890) || defined(CONFIG_ARCH_MT5890)
    if (IS_IC_MT5861_ES1())
    {
			if(host->hw->host_function == MSDC_EMMC)
			{
			//pinmux register d600[5:4], function 2 - CMD/DAT0~DAT3 	  
			//pinmux register d600[7:6], function 2 - DAT4~DAT7 	  
			//pinmux register d610[7], function 1 - CLK 	 
			sdr_clr_bits(0xF000D600, (0xF<<4)); 	
			sdr_set_bits(0xF000D600, (0xA<<4)); 	   
			sdr_clr_bits(0xF000D610, 0x1<<7);		
			sdr_set_bits(0xF000D610, 0x1<<7);				 
		
			//Local Arbitor open
			sdr_clr_bits(0xF0012200, (0x1<<5)); 
			sdr_set_bits(0xF0012200, (0x1<<5));
			}
			else
			if(host->hw->host_function == MSDC_SD)
			{
		
			// GPIO Setting  
			//pinmux register d604[27:26], function 1 - CMD/CLK/DAT0~DAT3		 
			sdr_clr_bits(0xF000D604, 0x3<<26);	   
			sdr_set_bits(0xF000D604, 0x1<<26);				 
		
			//Local Arbitor open	
			sdr_clr_bits(0xF0012200, (0x1));	 
			sdr_set_bits(0xF0012200, (0x1));	
			}
			else
			{
				N_MSG(WRN, "unsupport function %d\n",
					host->hw->host_function);		
			}

   	}
    else
   	{

    if(host->hw->host_function == MSDC_EMMC)
    {
    //pinmux register d600[5:4], function 2 - CMD/DAT0~DAT3 	  
    //pinmux register d600[7:6], function 2 - DAT4~DAT7		  
    //pinmux register d610[7], function 1 - CLK		 
    sdr_clr_bits(0xF000D600, (0x03<<4) | (0x03<<6));		
    sdr_set_bits(0xF000D600, (0x02<<4) | (0x02<<6)); 	   
    sdr_clr_bits(0xF000D610, 0x01<<7);		
    sdr_set_bits(0xF000D610, 0x01<<7);				 

    //Local Arbitor open
    sdr_clr_bits(0xF0012200, (0x03<<16) | (0x1F<<5));	
    sdr_set_bits(0xF0012200, (0x01<<16) | (0x1F<<5));
    }
    else
    if(host->hw->host_function == MSDC_SD)
    {

    // GPIO Setting
    // Switch Pinmux to GPIO Function
    // Set gpio input mode
    sdr_set_bits(0xF00280B4, 0x1u<<31);    
    sdr_clr_bits(0xF0028080, 0x1<<4);   

     //pinmux register d604[27:26], function 1 - CMD/CLK/DAT0~DAT3		 
    sdr_clr_bits(0xF000D604, 0x03<<26); 	   
    sdr_set_bits(0xF000D604, 0x01<<26);				 

    //Local Arbitor open	
    sdr_clr_bits(0xF0012200, (0x03<<16) | (0x1F<<0));	 
    sdr_set_bits(0xF0012200, (0x01<<16) | (0x1F<<0));	
    }
    else
    {
        N_MSG(WRN, "unsupport function %d\n",
            host->hw->host_function);    	
    }

   	}
	
#elif defined(CC_MT5882) || defined(CONFIG_ARCH_MT5882)
    if(host->hw->host_function == MSDC_EMMC)
    {
    //pinmux register d600[5:4], function 2 - CMD/DAT0~DAT3 	  
    //pinmux register d600[7:6], function 2 - DAT4~DAT7		  
    //pinmux register d610[7], function 1 - CLK		 
    sdr_clr_bits(0xF000D600, (0xF<<4) | (0x1<<2));		
    sdr_set_bits(0xF000D600, (0xA<<4) | (0x1<<2)); 	   			 
    //Local Arbitor open
    sdr_clr_bits(0xF0012200, (0x1<<5));	
    sdr_set_bits(0xF0012200, (0x1<<5));
    }
    else
    if(host->hw->host_function == MSDC_SD)
    {

    // GPIO Setting
    //pinmux register d604[27:26], function 1 - CMD/CLK/DAT0~DAT3		 
    sdr_clr_bits(0xF000D604, 0x7<<29); 	   
    sdr_set_bits(0xF000D604, 0x1<<29);				 

    //Local Arbitor open	
    sdr_clr_bits(0xF0012200, (0x1));	 
    sdr_set_bits(0xF0012200, (0x1));	
    }
    else
    {
        N_MSG(WRN, "unsupport function %d\n",
            host->hw->host_function);    	
    }
#endif
}


static void msdc_emmc_power(struct msdc_host *host,u32 on)
{
  return;
}

static void msdc_sd_power(struct msdc_host *host,u32 on)
{
  return;
}


#define sdc_is_busy()          (sdr_read32(SDC_STS) & SDC_STS_SDCBUSY)
#define sdc_is_cmd_busy()      (sdr_read32(SDC_STS) & SDC_STS_CMDBUSY)

#define sdc_send_cmd(cmd,arg) \
    do { \
        sdr_write32(SDC_ARG, (arg)); \
        mb(); \
        sdr_write32(SDC_CMD, (cmd)); \
    } while(0)

// can modify to read h/w register.
//#define is_card_present(h)   ((sdr_read32(MSDC_PS) & MSDC_PS_CDSTS) ? 0 : 1);
#define is_card_present(h)     (((struct msdc_host*)(h))->card_inserted)
//#define is_card_sdio(h)        (((struct msdc_host*)(h))->hw->register_pm) //wcp used one specific host for sdio only
bool is_card_sdio(struct msdc_host *h) 
{ 
	if(h) 
		if(((struct msdc_host*)(h))->mmc) 
			if(((struct msdc_host*)(h))->mmc->card) 
			   if((mmc_card_sdio(((struct msdc_host*)(h))->mmc->card))) 
			   {
			   	  printk(KERN_ERR "SDIO card?\n");
			      return 1;
			   }
	return 0;	   
}

static unsigned int msdc_do_command(struct msdc_host   *host,
                                      struct mmc_command *cmd,
                                      int                 tune,
                                      unsigned long       timeout);

static int msdc_tune_cmdrsp(struct msdc_host *host);
static int msdc_get_card_status(struct mmc_host *mmc, struct msdc_host *host, u32 *status);
static void msdc_clksrc_onoff(struct msdc_host *host, u32 on);




static void msdc_set_timeout(struct msdc_host *host, u32 ns, u32 clks)
{
    u32 base = host->base;
    u32 timeout, clk_ns;

    host->timeout_ns   = ns;
    host->timeout_clks = clks;

    //clk_ns  = 1000000000UL / host->sclk;
    clk_ns  = 1000000000UL / host->mclk;
    timeout = ns / clk_ns + clks;
    timeout = timeout >> 20; /* in 1048576 sclk cycle unit (83/85)*/
    timeout = timeout > 1 ? timeout - 1 : 0;
    timeout = timeout > 255 ? 255 : timeout;

    sdr_set_field(SDC_CFG, SDC_CFG_DTOC, timeout);

    N_MSG(OPS, "Set read data timeout: %dns %dclks -> %d x 1048576  cycles",
        ns, clks, timeout + 1);
}

static int  MsdcSDPowerSwitch(unsigned int i4V)
{
    if(0 != mtk_gpio_direction_output(MSDC_Gpio[MSDC0VoltageSwitchGpio], i4V))
    {
		return -1;
	}
	return 0;
}

/* detect cd interrupt */

static void msdc_tasklet_card(unsigned long arg)
{
    struct msdc_host *host = (struct msdc_host *)arg;
    struct msdc_hw *hw = host->hw;

    u32 inserted;
   
    inserted = msdc_card_exist(host);
    	
    if (!host->suspend)
    {
        if(inserted && !host->card_inserted)
        {
        	host->block_bad_card = 0;
        	INIT_MSG("card found<%s>", inserted ? "inserted" : "removed");
        	host->card_inserted = inserted;
        	MsdcSDPowerSwitch(0);//set the host ic gpio to 3.3v
        	mmc_detect_change(host->mmc, msecs_to_jiffies(1500));
        }
        else
        if(!inserted && host->card_inserted)
        {
        	INIT_MSG("card found<%s>", inserted ? "inserted" : "removed");
            host->block_bad_card = 0;
            host->card_inserted = inserted;
            MsdcSDPowerSwitch(0);//set the host ic gpio to 3.3v
        	mmc_detect_change(host->mmc, msecs_to_jiffies(10));
        }
        else
        if(host->block_bad_card && !host->card_inserted)
        {
        	host->block_bad_card = 0;
        	INIT_MSG("card found removed");
        	MsdcSDPowerSwitch(0);//set the host ic gpio to 3.3v
        	mmc_detect_change(host->mmc, msecs_to_jiffies(10));
        }
    }

    

	mod_timer(&card_detect_timer, jiffies + HZ/4);
}


// wait for extend if power pin is controlled by GPIO
void msdc0_card_power_up(void)
{	
    if(MSDC_Gpio[MSDC0PoweronoffDetectGpio] == -1)
    {
        //R_MSG("return 0 \n");
        
        return 0;	
    }
    else
    {
        mtk_gpio_direction_output(MSDC_Gpio[MSDC0PoweronoffDetectGpio],1);
		//ERR_MSG("msdc0_card_power_up MSDC_Gpio[MSDC0PoweronoffGpio] :%d \n",MSDC_Gpio[MSDC0PoweronoffDetectGpio]);
    }

}
// wait for extend if power pin is controlled by GPIO
void msdc0_card_power_down(void)
{	

    if(MSDC_Gpio[MSDC0PoweronoffDetectGpio] == -1)
    {
        //R_MSG("return 0 \n");
        return 0;	
    }
    else
    {
        mtk_gpio_direction_output(MSDC_Gpio[MSDC0PoweronoffDetectGpio],0);
		//ERR_MSG("msdc0_card_power_down MSDC_Gpio[MSDC0PoweronoffGpio] :%d \n",MSDC_Gpio[MSDC0PoweronoffDetectGpio]);
    }

}

#if 1
static int msdc_card_exist(void *data)
{
    unsigned int val;	
    struct msdc_host *host = (struct msdc_host *)data;
	if(!host)
    {
    	return 0;
    }
    
    //ERR_MSG("MSDC_Gpio[MSDC0DetectGpio] :%d \n",MSDC_Gpio[MSDC0DetectGpio]);

    if(host->hw->host_function == MSDC_EMMC)
    {
    	return 1;
    }
    else
    if(host->hw->host_function == MSDC_SD)
    {

	    //SD CARD DETECT GPIO VALUE
	    if(MSDC_Gpio[MSDC0DetectGpio] == -1)
	    {
	        //ERR_MSG("return 0 \n");
	        return 0;	
	    }
	    else
	    {
	        mtk_gpio_direction_input(MSDC_Gpio[MSDC0DetectGpio]);
	        val = mtk_gpio_get_value(MSDC_Gpio[MSDC0DetectGpio]);
	        //ERR_MSG("tyler return val: %d \n",val);
	    }
	    
	    if(val)	
	    {
	        msdc0_card_power_down();   
	        //ERR_MSG("[0]Card not Insert(%d)!\n", val);	
	        return 0;	
	    }	

	    msdc0_card_power_up();
		return 1;
    }
    //ERR_MSG("[0]Card Insert(%d)!\n", val);   


}

#else
static int msdc_card_exist(void *data)
{
    unsigned int val;	
    struct msdc_host *host = (struct msdc_host *)data;
    if(!host)
    {
    	return 0;
    }
    
    if(host->hw->host_function == MSDC_EMMC)
    {
    	return 1;
    }
    else
    if(host->hw->host_function == MSDC_SD)
    {
    	    //SD CARD DETECT GPIO VALUE
        val = SD_CARD_DETECT_GPIO;
    	
        if(val)	
        {
            return 0;	
        }	
        
        return 1;
    }
    else
    {
    	return 0; //to do
    }    

}
#endif

static void msdc_select_clksrc(struct msdc_host* host, int clksrc)
{   
    
    if(clksrc != host->hw->clk_src) 
    {
    	ERR_MSG("host%d select clk source %d",host->id,clksrc);
      
        // Clr msdc_src_clk selection
        if(host->hw->host_function == MSDC_EMMC)
        {
        	msdc_clksrc_onoff(host, 0);
            sdr_clr_bits(MSDC_CLK_S_REG1, MSDC_CLK_SEL_MASK);  
            #if defined(CC_EMMC_DDR50)
            sdr_set_bits(MSDC_CLK_S_REG1, 0x8<<0);  
            #endif
            sdr_set_bits(MSDC_CLK_S_REG1, clksrc<<0);  
            msdc_clksrc_onoff(host, 1);
        }
        else
        if(host->hw->host_function == MSDC_SD)  
        {
        	msdc_clksrc_onoff(host, 0);
        	sdr_clr_bits(MSDC_CLK_S_REG0, MSDC_CLK_SEL_MASK);  
            sdr_set_bits(MSDC_CLK_S_REG0, clksrc<<0); 
            msdc_clksrc_onoff(host, 1); 
        }
        
        host->hw->clk_src = clksrc;
    }
}


static void msdc_set_mclk(struct msdc_host *host, int ddr, u32 hz)
{
    //struct msdc_hw *hw = host->hw;
    u32 base = host->base;
    u32 mode = 1; /* no divisor */
    u32 div  = 0;
    u32 flags;
    //u8  clksrc = hw->clk_src;

    if (!hz) { // set mmc system clock to 0
        ERR_MSG("msdc%d -> set mclk to 0",host->id);  // fix me: need to set to 0
        host->mclk = 0;
        msdc_reset_hw(host->id);
        return;
    }
    else
    {
    	ERR_MSG("msdc%d -> set mclk to %d",host->id,hz);
    }

    msdc_irq_save(flags);

    u32 sdClkSel = 0, idx = 0;

    do
    {
	    if((hz < msdcClk[IDX_CLK_TARGET][idx]) ||
	       (ddr && (msdcClk[IDX_CLK_MODE_VAL][idx] != 2)))
		    continue;
	    else
		    break;
	
    }while(++idx < (sizeof(msdcClk[0]) / sizeof(msdcClk[0][0])));
    
    idx = (idx >= (sizeof(msdcClk[0]) / sizeof(msdcClk[0][0])))?((sizeof(msdcClk[0]) / sizeof(msdcClk[0][0]))):idx;
    	
    sdClkSel = msdcClk[IDX_CLK_SRC_VAL][idx];
    div = msdcClk[IDX_CLK_DIV_VAL][idx];
    mode = msdcClk[IDX_CLK_MODE_VAL][idx];
    
    msdc_select_clksrc(host,sdClkSel);
    msdc_clk_stable(host,mode, div);
    
    host->hw->clk_drv = msdcClk[IDX_CLK_DRV_VAL][idx];
    host->hw->cmd_drv = msdcClk[IDX_CMD_DRV_VAL][idx];
    host->hw->dat_drv = msdcClk[IDX_DAT_DRV_VAL][idx];
    msdc_set_driving(host,host->hw,0);


    host->sclk = msdcClk[IDX_CLK_TARGET][idx];
    host->mclk = hz;
	host->ddr = ddr;


    msdc_set_timeout(host, host->timeout_ns, host->timeout_clks); // need because clk changed.

    if(hz >= 25000000)
    	printk(KERN_ERR "msdc%d -> !!! Set<%dKHz> -> sclk<%dKHz> DDR<%d> mode<%d> div<%d>" ,
		                host->id, hz/1000,host->mclk/1000, ddr, mode, div);
	else
		printk(KERN_WARNING "msdc%d -> !!! Set<%dKHz>  -> sclk<%dKHz> DDR<%d> mode<%d> div<%d>" ,
		                host->id, hz/1000, host->mclk/1000, ddr, mode, div);


    msdc_irq_restore(flags);
}



void msdc_send_stop(struct msdc_host *host)
{
    struct mmc_command stop = {0};
    struct mmc_request mrq = {0};
    u32 err = -1;

    stop.opcode = MMC_STOP_TRANSMISSION;
    stop.arg = 0;
    stop.flags = MMC_RSP_R1B | MMC_CMD_AC;

    mrq.cmd = &stop; stop.mrq = &mrq;
    stop.data = NULL;

    err = msdc_do_command(host, &stop, 0, CMD_TIMEOUT);
}

typedef enum{
	cmd_counter = 0,
	read_counter,
	write_counter,
	all_counter,
}TUNE_COUNTER;

static void msdc_reset_tune_counter(struct msdc_host *host,TUNE_COUNTER index)
{
	if(index >= 0 && index <= all_counter){
	switch (index){
		case cmd_counter   : if(host->t_counter.time_cmd != 0)
									ERR_MSG("TUNE CMD Times(%d)", host->t_counter.time_cmd);
							 host->t_counter.time_cmd = 0;break;
		case read_counter  : if(host->t_counter.time_read != 0)
									ERR_MSG("TUNE READ Times(%d)", host->t_counter.time_read);
							 host->t_counter.time_read = 0;break;
		case write_counter : if(host->t_counter.time_write != 0)
									ERR_MSG("TUNE WRITE Times(%d)", host->t_counter.time_write);
							 host->t_counter.time_write = 0;break;
		case all_counter   : if(host->t_counter.time_cmd != 0)
									ERR_MSG("TUNE CMD Times(%d)", host->t_counter.time_cmd);
							 if(host->t_counter.time_read != 0)
									ERR_MSG("TUNE READ Times(%d)", host->t_counter.time_read);
							 if(host->t_counter.time_write != 0)
									ERR_MSG("TUNE WRITE Times(%d)", host->t_counter.time_write);
							 host->t_counter.time_cmd = 0;
						     host->t_counter.time_read = 0;
						     host->t_counter.time_write = 0;
						     host->read_time_tune = 0;
                             host->write_time_tune = 0;
	                         host->rwcmd_time_tune = 0;
						     break;
		default : break;
		}
	}
	else{
		 ERR_MSG("msdc%d ==> reset counter index(%d) error!\n",host->id,index);
		}
}


extern void mmc_remove_card(struct mmc_card *card);
extern void mmc_detach_bus(struct mmc_host *host);
extern void mmc_power_off(struct mmc_host *host);


//#endif
/* Fix me. when need to abort */
static u32 msdc_abort_data(struct msdc_host *host)
{
    struct mmc_host *mmc = host->mmc;
    u32 base = host->base;
    u32 status = 0;
    u32 state = 0;
    u32 err = 0;
    unsigned long tmo = jiffies + POLLING_BUSY;

    while (state != 4) { // until status to "tran"
        msdc_reset_hw(host->id);
        while ((err = msdc_get_card_status(mmc, host, &status))) {
			ERR_MSG("CMD13 ERR<%d>",err);
            if (err != (unsigned int)-EIO) {
				return msdc_power_tuning(host);
            } else if (msdc_tune_cmdrsp(host)) {
                ERR_MSG("update cmd para failed");
                return 1;
            }
        }

        state = R1_CURRENT_STATE(status);
        ERR_MSG("check card state<%d>", state);
        if (state == 5 || state == 6) {
            ERR_MSG("state<%d> need cmd12 to stop", state);
            msdc_send_stop(host); // don't tuning
        } else if (state == 7) {  // busy in programing
            ERR_MSG("state<%d> card is busy", state);
            spin_unlock(&host->lock);
            msleep(100);
            spin_lock(&host->lock);
        } else if (state != 4) {
            ERR_MSG("state<%d> ??? ", state);
            return msdc_power_tuning(host);
        }

        if (time_after(jiffies, tmo)) {
            ERR_MSG("abort timeout. Do power cycle");
			if(host->hw->host_function == MSDC_SD && (host->sclk >= 100000000 || host->ddr))
				host->sd_30_busy++;
			return msdc_power_tuning(host);
        }
    }

    msdc_reset_hw(host->id);
    return 0;
}
static u32 msdc_polling_idle(struct msdc_host *host)
{
    struct mmc_host *mmc = host->mmc;
    u32 status = 0;
    u32 state = 0;
    u32 err = 0;
    unsigned long tmo = jiffies + POLLING_BUSY;

    while (state != 4) { // until status to "tran"
        while ((err = msdc_get_card_status(mmc, host, &status))) {
			ERR_MSG("CMD13 ERR<%d>",err);
            if (err != (unsigned int)-EIO) {
				return msdc_power_tuning(host);
            } else if (msdc_tune_cmdrsp(host)) {
                ERR_MSG("update cmd para failed");
                return 1;
            }
        }

        state = R1_CURRENT_STATE(status);
        //ERR_MSG("check card state<%d>", state);
        if (state == 5 || state == 6) {
            ERR_MSG("state<%d> need cmd12 to stop", state);
            msdc_send_stop(host); // don't tuning
        } else if (state == 7) {  // busy in programing
            ERR_MSG("state<%d> card is busy", state);
            spin_unlock(&host->lock);
            msleep(100);
            spin_lock(&host->lock);
        } else if (state != 4) {
            ERR_MSG("state<%d> ??? ", state);
            return msdc_power_tuning(host);
        }

        if (time_after(jiffies, tmo)) {
            ERR_MSG("abort timeout. Do power cycle");
			return msdc_power_tuning(host);
        }
    }
    return 0;
}




extern int mmc_card_sleepawake(struct mmc_host *host, int sleep);
//extern int mmc_send_status(struct mmc_card *card, u32 *status);
extern int mmc_go_idle(struct mmc_host *host);
extern int mmc_send_op_cond(struct mmc_host *host, u32 ocr, u32 *rocr);
extern int mmc_all_send_cid(struct mmc_host *host, u32 *cid);
//extern int mmc_attach_mmc(struct mmc_host *host, u32 ocr);

typedef enum MMC_STATE_TAG
{
    MMC_IDLE_STATE,
    MMC_READ_STATE,
    MMC_IDENT_STATE,
    MMC_STBY_STATE,
    MMC_TRAN_STATE,
    MMC_DATA_STATE,
    MMC_RCV_STATE,
    MMC_PRG_STATE,
    MMC_DIS_STATE,
    MMC_BTST_STATE,
    MMC_SLP_STATE,
    MMC_RESERVED1_STATE,
    MMC_RESERVED2_STATE,
    MMC_RESERVED3_STATE,
    MMC_RESERVED4_STATE,
    MMC_RESERVED5_STATE,
}MMC_STATE_T;

typedef enum EMMC_CHIP_TAG{
    SAMSUNG_EMMC_CHIP = 0x15,
    SANDISK_EMMC_CHIP = 0x45,
    HYNIX_EMMC_CHIP   = 0x90,
} EMMC_VENDOR_T;


static void msdc_clksrc_onoff(struct msdc_host *host, u32 on)
{
    u32 base = host->base; 	
    if (on) 
    {
    	if(host->hw->host_function == MSDC_EMMC)
             sdr_clr_bits(MSDC_CLK_S_REG1, MSDC_CLK_GATE_BIT);	
             
         if(host->hw->host_function == MSDC_SD)
             sdr_clr_bits(MSDC_CLK_S_REG0, MSDC_CLK_GATE_BIT);	
          	
    } 
    else 
    {                
         if(host->hw->host_function == MSDC_EMMC)
             sdr_set_bits(MSDC_CLK_S_REG1, MSDC_CLK_GATE_BIT);	
             
         if(host->hw->host_function == MSDC_SD)
             sdr_set_bits(MSDC_CLK_S_REG0, MSDC_CLK_GATE_BIT);	      	
    }      	
}

static int msdc_clk_stable(struct msdc_host *host,u32 mode, u32 div){
	u32 base = host->base;
    int retry = 0;
	int cnt = 1000;
	int retry_cnt = 1;
	do{
		retry = 3;
		msdc_clksrc_onoff(host, 0);
      	sdr_set_field(MSDC_CFG, MSDC_CFG_CKMOD|MSDC_CFG_CKDIV,(mode << 8)|((div + retry_cnt) % 0xff));
      	msdc_clksrc_onoff(host, 1);
      	//sdr_set_field(MSDC_CFG, MSDC_CFG_CKMOD, mode);
      	msdc_retry(!(sdr_read32(MSDC_CFG) & MSDC_CFG_CKSTB), retry, cnt,host->id);
		if(retry == 0){
				ERR_MSG("msdc%d on clock failed ===> retry twice\n",host->id);
                  msdc_clksrc_onoff(host,0);
                  msdc_clksrc_onoff(host,1);
			}
		retry = 3;
		msdc_clksrc_onoff(host, 0);
      	sdr_set_field(MSDC_CFG, MSDC_CFG_CKDIV, div);//to check while set ckdiv again here
      	msdc_clksrc_onoff(host, 1);
      	msdc_retry(!(sdr_read32(MSDC_CFG) & MSDC_CFG_CKSTB), retry, cnt,host->id);
      	if(retry_cnt == 0)
      	{
      		ERR_MSG("msdc%d on clock failed ===> retry twice\n",host->id);
      	}
      	msdc_reset_hw(host->id); //to check"why reset hw here"
		if(retry_cnt == 2)
			break;
		retry_cnt += 1;
	}while(!retry);
	return 0;
}


/*
   register as callback function of WIFI(combo_sdio_register_pm) .
   can called by msdc_drv_suspend/resume too.
*/
static void msdc_save_emmc_setting(struct msdc_host *host)
{
	u32 base = host->base;
	host->saved_para.ddr = host->ddr;
	host->saved_para.hz = host->mclk;
	host->saved_para.sdc_cfg = sdr_read32(SDC_CFG);
	sdr_get_field(MSDC_IOCON, MSDC_IOCON_RSPL, host->hw->cmd_edge); // save the para
	sdr_get_field(MSDC_IOCON, MSDC_IOCON_DSPL, host->hw->rdata_edge);
	sdr_get_field(MSDC_IOCON, MSDC_IOCON_W_DSPL, host->hw->wdata_edge);
	host->saved_para.pad_tune = sdr_read32(MSDC_PAD_TUNE);
	host->saved_para.ddly0 = sdr_read32(MSDC_DAT_RDDLY0);
	host->saved_para.ddly1 = sdr_read32(MSDC_DAT_RDDLY1);
	sdr_get_field(MSDC_PATCH_BIT1, MSDC_PATCH_BIT1_CMD_RSP,    host->saved_para.cmd_resp_ta_cntr);
	sdr_get_field(MSDC_PATCH_BIT1, MSDC_PATCH_BIT1_WRDAT_CRCS, host->saved_para.wrdat_crc_ta_cntr);
}
static void msdc_restore_emmc_setting(struct msdc_host *host)
{
	u32 base = host->base;
	msdc_set_mclk(host,host->ddr,host->mclk);
	sdr_write32(SDC_CFG,host->saved_para.sdc_cfg);
	sdr_set_field(MSDC_IOCON, MSDC_IOCON_RSPL, host->hw->cmd_edge);
    sdr_set_field(MSDC_IOCON, MSDC_IOCON_DSPL, host->hw->rdata_edge);
    sdr_set_field(MSDC_IOCON, MSDC_IOCON_W_DSPL, host->hw->wdata_edge);
	sdr_write32(MSDC_PAD_TUNE,host->saved_para.pad_tune);
	sdr_write32(MSDC_DAT_RDDLY0,host->saved_para.ddly0);
	sdr_write32(MSDC_DAT_RDDLY1,host->saved_para.ddly1);
	sdr_set_field(MSDC_PATCH_BIT1, MSDC_PATCH_BIT1_WRDAT_CRCS, host->saved_para.wrdat_crc_ta_cntr);
    sdr_set_field(MSDC_PATCH_BIT1, MSDC_PATCH_BIT1_CMD_RSP,    host->saved_para.cmd_resp_ta_cntr);
}


static u64 msdc_get_user_capacity(struct msdc_host *host)
{
	u64 device_capacity = 0;
    u32 device_legacy_capacity = 0;
	struct mmc_host* mmc = NULL;
	BUG_ON(!host);
    BUG_ON(!host->mmc);
    BUG_ON(!host->mmc->card);
	mmc = host->mmc;
	if(mmc_card_mmc(mmc->card)){
		if(mmc->card->csd.read_blkbits)
				device_legacy_capacity = mmc->card->csd.capacity * (2 << (mmc->card->csd.read_blkbits - 1));
		else{
				device_legacy_capacity = mmc->card->csd.capacity;
				ERR_MSG("XXX read_blkbits = 0 XXX");
			}
		device_capacity = (u64)(mmc->card->ext_csd.sectors)* 512 > device_legacy_capacity ? (u64)(mmc->card->ext_csd.sectors)* 512 : device_legacy_capacity;
	}
	else if(mmc_card_sd(mmc->card)){
		device_capacity = (u64)(mmc->card->csd.capacity) << (mmc->card->csd.read_blkbits);
	}
	return device_capacity;
}



u32 erase_start = 0;
u32 erase_end = 0;

u32 msdc_get_capacity(int get_emmc_total)
{
   u64 user_size = 0;
   u32 other_size = 0;
   u64 total_size = 0;
   int index = 0;
	for(index = 0;index < HOST_MAX_NUM;++index){
		if((mtk_msdc_host[index] != NULL) && (mtk_msdc_host[index]->hw->boot)){
		  	user_size = msdc_get_user_capacity(mtk_msdc_host[index]);
			#ifdef CONFIG_MTKEMMC_BOOT
			if(get_emmc_total){
		  		if(mmc_card_mmc(mtk_msdc_host[index]->mmc->card))
			  		other_size = msdc_get_other_capacity();
			}
			#endif
		  	break;
		}
	}
   total_size = user_size + other_size;
   return total_size/512;
}
EXPORT_SYMBOL(msdc_get_capacity);
/*--------------------------------------------------------------------------*/
/* mmc_host_ops members                                                      */
/*--------------------------------------------------------------------------*/
static u32 wints_cmd = MSDC_INT_CMDRDY  | MSDC_INT_RSPCRCERR  | MSDC_INT_CMDTMO  |
                       MSDC_INT_ACMDRDY | MSDC_INT_ACMDCRCERR | MSDC_INT_ACMDTMO;
static unsigned int msdc_command_start(struct msdc_host   *host,
                                      struct mmc_command *cmd,
                                      int                 tune,   /* not used */
                                      unsigned long       timeout)
{
    u32 base = host->base;
    u32 opcode = cmd->opcode;
    u32 rawcmd = 0;
	u32 rawarg = 0;
    u32 resp = 0;
    unsigned long tmo = 0;

    /* Protocol layer does not provide response type, but our hardware needs
     * to know exact type, not just size!
     */
    if (opcode == MMC_SEND_OP_COND || opcode == SD_APP_OP_COND)
        resp = RESP_R3;
    else if (opcode == MMC_SET_RELATIVE_ADDR || opcode == SD_SEND_RELATIVE_ADDR)
        resp = (mmc_cmd_type(cmd) == MMC_CMD_BCR) ? RESP_R6 : RESP_R1;
    else if (opcode == MMC_FAST_IO)
        resp = RESP_R4;
    else if (opcode == MMC_GO_IRQ_STATE)
        resp = RESP_R5;
    else if (opcode == MMC_SELECT_CARD) {
        resp = (cmd->arg != 0) ? RESP_R1B : RESP_NONE;
        host->app_cmd_arg = cmd->arg;
        printk(KERN_WARNING "msdc%d select card<0x%.8x>", host->id,cmd->arg);  // select and de-select
    } else if (opcode == SD_IO_RW_DIRECT || opcode == SD_IO_RW_EXTENDED)
        resp = RESP_R1; /* SDIO workaround. */
    else if (opcode == SD_SEND_IF_COND && (mmc_cmd_type(cmd) == MMC_CMD_BCR))
        resp = RESP_R1;
    else {
        switch (mmc_resp_type(cmd)) {
        case MMC_RSP_R1:
            resp = RESP_R1;
            break;
        case MMC_RSP_R1B:
            resp = RESP_R1B;
            break;
        case MMC_RSP_R2:
            resp = RESP_R2;
            break;
        case MMC_RSP_R3:
            resp = RESP_R3;
            break;
        case MMC_RSP_NONE:
        default:
            resp = RESP_NONE;
            break;
        }
    }

    cmd->error = 0;
    /* rawcmd :
     * vol_swt << 30 | auto_cmd << 28 | blklen << 16 | go_irq << 15 |
     * stop << 14 | rw << 13 | dtype << 11 | rsptyp << 7 | brk << 6 | opcode
     */
    rawcmd = opcode | msdc_rsp[resp] << 7 | host->blksz << 16;

    if (opcode == MMC_READ_MULTIPLE_BLOCK) {
        rawcmd |= (2 << 11);
		if (host->autocmd & MSDC_AUTOCMD12)
            rawcmd |= (1 << 28);
    } else if (opcode == MMC_READ_SINGLE_BLOCK) {
        rawcmd |= (1 << 11);
    } else if (opcode == MMC_WRITE_MULTIPLE_BLOCK) {
    	     if(cmd->data->blocks > 1)
         rawcmd |= ((2 << 11) | (1 << 13));
         else
        	rawcmd |= ((1 << 11) | (1 << 13));
		if (host->autocmd & MSDC_AUTOCMD12)
            rawcmd |= (1 << 28);
    } else if (opcode == MMC_WRITE_BLOCK) {
        rawcmd |= ((1 << 11) | (1 << 13));
    } else if (opcode == SD_IO_RW_EXTENDED) {
        if (cmd->data->flags & MMC_DATA_WRITE)
            rawcmd |= (1 << 13);
        if (cmd->data->blocks > 1)
            rawcmd |= (2 << 11);
        else
            rawcmd |= (1 << 11);
    } else if (opcode == SD_IO_RW_DIRECT && cmd->flags == (unsigned int)-1) {
        rawcmd |= (1 << 14);
    } else if (opcode == SD_SWITCH_VOLTAGE) {
        rawcmd |= (1 << 30);
    } else if ((opcode == SD_APP_SEND_SCR) ||
        (opcode == SD_APP_SEND_NUM_WR_BLKS) ||
        (opcode == SD_SWITCH && (mmc_cmd_type(cmd) == MMC_CMD_ADTC)) ||
        (opcode == SD_APP_SD_STATUS && (mmc_cmd_type(cmd) == MMC_CMD_ADTC)) ||
        (opcode == MMC_SEND_EXT_CSD && (mmc_cmd_type(cmd) == MMC_CMD_ADTC))) {
        rawcmd |= (1 << 11);
    } else if (opcode == MMC_STOP_TRANSMISSION) {
        rawcmd |= (1 << 14);
        rawcmd &= ~(0x0FFF << 16);
    }
    	    

    N_MSG(CMD, "CMD<%d><0x%.8x> Arg<0x%.8x>", opcode , rawcmd, cmd->arg);

    tmo = jiffies + timeout;

    if (opcode == MMC_SEND_STATUS) {
        for (;;) {
            if (!sdc_is_cmd_busy())
                break;

            if (time_after(jiffies, tmo)) {
                ERR_MSG("XXX cmd_busy timeout: before CMD<%d>", opcode);
                cmd->error = (unsigned int)-ETIMEDOUT;
                msdc_reset_hw(host->id);
                return cmd->error;  /* Fix me: error handling */
            }
        }
    }else {
        for (;;) {
            if (!sdc_is_busy())
                break;
            if (time_after(jiffies, tmo)) {
                ERR_MSG("XXX sdc_busy timeout: before CMD<%d>", opcode);
                cmd->error = (unsigned int)-ETIMEDOUT;
                msdc_reset_hw(host->id);
                return cmd->error;
            }
        }
    }

    //BUG_ON(in_interrupt());
    host->cmd     = cmd;
    host->cmd_rsp = resp;

    if (cmd->opcode == MMC_SEND_STATUS){
        init_completion(&host->cmd_done);

        sdr_set_bits(MSDC_INTEN, wints_cmd);
    } else {
        /* use polling way */
        sdr_clr_bits(MSDC_INTEN, wints_cmd);
    }
	rawarg = cmd->arg;
#ifdef MTK_EMMC_SUPPORT
        if(host->hw->host_function == MSDC_EMMC &&
			host->hw->boot == MSDC_BOOT_EN &&
            (cmd->opcode == MMC_READ_SINGLE_BLOCK 
            || cmd->opcode == MMC_READ_MULTIPLE_BLOCK 
            || cmd->opcode == MMC_WRITE_BLOCK 
            || cmd->opcode == MMC_WRITE_MULTIPLE_BLOCK
            || cmd->opcode == MMC_ERASE_GROUP_START             
            || cmd->opcode == MMC_ERASE_GROUP_END) 
            && (partition_access == 0)) {
            if(cmd->arg == 0){
					msdc_cal_offset(host);	
				}
            rawarg  += offset;
            if(offset)
            	ERR_MSG("retune start off from 0 to 0x%x\n",rawarg);
			if(cmd->opcode == MMC_ERASE_GROUP_START)
				erase_start = rawarg;
			if(cmd->opcode == MMC_ERASE_GROUP_END)
				erase_end = rawarg;
        }	
		if(cmd->opcode == MMC_ERASE
			&& (cmd->arg == MMC_SECURE_ERASE_ARG || cmd->arg == MMC_ERASE_ARG)
			&& host->mmc->card 
			&& host->hw->host_function == MSDC_EMMC 
			&& host->hw->boot == MSDC_BOOT_EN
			&& (!mmc_erase_group_aligned(host->mmc->card,erase_start,erase_end))){
				if(cmd->arg == MMC_SECURE_ERASE_ARG && mmc_can_secure_erase_trim(host->mmc->card))
			  		rawarg = MMC_SECURE_TRIM1_ARG;
				else if(cmd->arg == MMC_ERASE_ARG 
				   ||(cmd->arg == MMC_SECURE_ERASE_ARG && !mmc_can_secure_erase_trim(host->mmc->card)))
					rawarg = MMC_TRIM_ARG;
			}
#endif

    sdc_send_cmd(rawcmd, rawarg);

//end:
    return 0;  // irq too fast, then cmd->error has value, and don't call msdc_command_resp, don't tune.
}

void msdc_find_dev(u32 *pCID) //emmc only
{
    u32 i, devNum;

	/*
      * why we need to define the id mask of emmc
      * Some vendors' emmc has the same brand & type but different product revision.
      * That means the firmware in eMMC has different revision
      * We should treat these emmcs as same type and timing
      * So id mask can ignore this case
      */

    if (0x90 == ((pCID[0] & 0xff000000)>> 24))
   {
        g_hynix_emmc = true;
	printk(KERN_ERR "It's Hynix emmc \n");
   }

    u32 idMask = 0xFFFFFF00;

    devNum = sizeof(_arEMMC_DevInfo1)/sizeof(EMMC_FLASH_DEV_T);
    printk(KERN_ERR "%08X:%08X:%08X:%08X\n", pCID[0], pCID[1], pCID[2], pCID[3]);
    printk(KERN_ERR "id1:%08X id2:%08X\n", ID1(pCID), ID2(pCID));

    

    for(i = 0; i<devNum; i++)
    {
        if((_arEMMC_DevInfo1[i].u4ID1 == ID1(pCID)) &&
           ((_arEMMC_DevInfo1[i].u4ID2 & idMask) == (ID2(pCID) & idMask)))
        {
            break;
        }
    }

    emmcidx= (i == devNum)?0:i;
    printk(KERN_ERR "[yifan] eMMC Name: %s\n", _arEMMC_DevInfo1[emmcidx].acName);	

	
		
}

void msdc_smaple_delay(struct mmc_host* mmc,u32 flag)
{
#if defined(CC_MT5399) || defined(CONFIG_ARCH_MT5399)|| defined(CC_MT5890)|| defined(CONFIG_ARCH_MT5890)|| \
    defined(CC_MT5882) || defined(CONFIG_ARCH_MT5882)
    struct msdc_host *host = mmc_priv(mmc);
    struct msdc_hw *hw=host->hw;
    u32 base = host->base;

    // Sample Edge init
    sdr_clr_bits(MSDC_PAD_TUNE, 0xFFFFFFFF);

    // Sample Edge init
    if(mmc->ios.timing == MMC_TIMING_LEGACY)
    {
        sdr_set_bits(MSDC_PAD_TUNE, _arEMMC_DevInfo1[emmcidx].DS26Delay);
    }
    else if(mmc->ios.timing == MMC_TIMING_MMC_HS)
    {
        //sdr_set_bits(MSDC_PAD_TUNE, _arEMMC_DevInfo1[emmcidx].HS52Delay);
		
#if defined(CC_MT5890) || defined(CONFIG_ARCH_MT5890)
	    if(IS_IC_MT5890())
		{
			
			printk(KERN_ERR "Kernel using 50MHZ, Param is 0x%x , 0x%x \n",_arEMMC_DevInfo1[emmcidx].HS52Sample,_arEMMC_DevInfo1[emmcidx].HS52Sample);
		/*++++++++++++++++++++++clock part+++++++++++++++++++*/
		sdr_clr_bits(MSDC_PATCH_BIT0, INT_DAT_LATCH_CK_SEL);
		sdr_set_bits(MSDC_PATCH_BIT0, 0x0<< INT_DAT_LATCH_CK_SEL_SHIFT);
		
		sdr_clr_bits(MSDC_PATCH_BIT0, CKGEN_MSDC_DLY_SEL);//CKGEN_MSDC_DLY_SEL
		sdr_set_bits(MSDC_PATCH_BIT0, ((_arEMMC_DevInfo1[emmcidx].HS52Sample&0x00f00000)>>20)<< CKGEN_MSDC_DLY_SEL_SHIFT);
		  
		/*++++++++++++++++++++++cmd part+++++++++++++++++++*/
		sdr_clr_bits(MSDC_PATCH_BIT1, CMD_RSP_TA_CNTR);//cmd turn around fix 1
		sdr_set_bits(MSDC_PATCH_BIT1, 0x1<<CMD_RSP_TA_CNTR_SHIFT);

		sdr_clr_bits(MSDC_PAD_TUNE, MSDC_PAD_TUNE_CMDRRDLY);//cmd resp rx delay
		sdr_set_bits(MSDC_PAD_TUNE, ((_arEMMC_DevInfo1[emmcidx].HS52Sample&0xff000000)>>24)<<MSDC_PAD_TUNE_CMDRRDLY_SHIFT); //clock delay 0xc 
		
		sdr_clr_bits(MSDC_IOCON, MSDC_IOCON_R_SMPL);//cmd resp rx delay
		sdr_set_bits(MSDC_IOCON,((_arEMMC_DevInfo1[emmcidx].HS52Sample&0x000f0000)>>16)<<MSDC_IOCON_R_SMPL_SHIFT); // 

		sdr_clr_bits(MSDC_PAD_TUNE, MSDC_PAD_TUNE_CMDRDLY);//cmd rx dly
		sdr_set_bits(MSDC_PAD_TUNE, ((_arEMMC_DevInfo1[emmcidx].HS52Sample&0x0000ff00)>>8)<<MSDC_PAD_TUNE_CMDRDLY_SHIFT); //clock delay 0xc 
	
		/*++++++++++++++++++++++write part+++++++++++++++++++*/   
		sdr_clr_bits(MSDC_PATCH_BIT1, WRDAT_CRCS_TA_CNTR);//WRDAT_CRCS_TA_CNTR 
		sdr_set_bits(MSDC_PATCH_BIT1, 0x1<<WRDAT_CRCS_TA_CNTR_SHIFT);//write crc status turn aroud fix 1
		
		sdr_clr_bits(MSDC_PAD_TUNE, MSDC_PAD_TUNE_DATWRDLY);
		sdr_set_bits(MSDC_PAD_TUNE, ((_arEMMC_DevInfo1[emmcidx].HS52Sample&0x000000ff))<<MSDC_PAD_TUNE_DATWRDLY_SHIFT); //

		sdr_clr_bits(MSDC_IOCON, MSDC_IOCON_W_D_SMPL_SEL);
		sdr_set_bits(MSDC_IOCON, ((_arEMMC_DevInfo1[emmcidx].HS52Delay&0xff000000)>>24)<<MSDC_IOCON_W_D_SMPL_SHIFT); //

		sdr_clr_bits(MSDC_DAT_RDDLY0, MSDC_DAT_RDDLY0_D0);
		sdr_set_bits(MSDC_DAT_RDDLY0, ((_arEMMC_DevInfo1[emmcidx].HS52Delay&0x00ff0000)>>16)<<MSDC_DAT_RDDLY0_D0_SHIFT); //
	
		/*++++++++++++++++++++++read part+++++++++++++++++++*/	 
		sdr_clr_bits(MSDC_IOCON, MSDC_IOCON_D_SMPL);//read sample
		sdr_set_bits(MSDC_IOCON,((_arEMMC_DevInfo1[emmcidx].HS52Delay&0x00ff00)>>8)<<MSDC_IOCON_D_SMPL_SHIFT); // 
		
		sdr_clr_bits(MSDC_PAD_TUNE, MSDC_PAD_TUNE_DATRRDLY);//read rx delay
		sdr_set_bits(MSDC_PAD_TUNE, ((_arEMMC_DevInfo1[emmcidx].HS52Delay&0x000000ff))<<MSDC_PAD_TUNE_DATRRDLY_SHIFT); //clock delay 0xc	
		}	 
#else
				sdr_set_bits(MSDC_PAD_TUNE, _arEMMC_DevInfo1[emmcidx].HS52Delay);
				sdr_clr_bits(MSDC_PATCH_BIT0, CKGEN_MSDC_DLY_SEL);
				sdr_set_bits(MSDC_PATCH_BIT0, 0x1<< CKGEN_MSDC_DLY_SEL_SHIFT);
#endif	  		
    }
    else if(mmc->ios.timing == MMC_TIMING_UHS_DDR50)
    {
        sdr_set_bits(MSDC_PAD_TUNE, _arEMMC_DevInfo1[emmcidx].DDR52Delay&0x0000ffff);//PAD_DAT_RD_RXDLY,PAD_DAT_WR_RXDLY
        sdr_clr_bits(MSDC_PATCH_BIT0, MSDC_CKGEN_MSDC_DLY_SEL);//CKGEN_MSDC_DLY_SEL
        sdr_set_bits(MSDC_PATCH_BIT0, ((_arEMMC_DevInfo1[emmcidx].DDR52Delay&0x00ff0000)>>16)<<10);
    }
    else if(mmc->ios.timing == MMC_TIMING_MMC_HS200)
    {
        sdr_set_bits(MSDC_PAD_TUNE, _arEMMC_DevInfo1[emmcidx].HS200Delay);

        sdr_clr_bits(MSDC_PATCH_BIT0, MSDC_CKGEN_MSDC_DLY_SEL);
        sdr_set_bits(MSDC_PATCH_BIT0, 0x3 << 10);
        
#if defined(CC_MT5890) || defined(CONFIG_ARCH_MT5890)|| \
    defined(CC_MT5882) || defined(CONFIG_ARCH_MT5882)
    	sdr_clr_bits(MSDC_PAD_TUNE, 0xFFFFFFFF);
        sdr_set_bits(MSDC_PAD_TUNE, _arEMMC_DevInfo1[emmcidx].HS200Delay&0x0000ffff);//PAD_DAT_RD_RXDLY,PAD_DAT_WR_RXDLY
        
        sdr_clr_bits(MSDC_PAD_TUNE, MSDC_PAD_TUNE_CLKTXDLY);
   	    sdr_set_bits(MSDC_PAD_TUNE, 0xc<<MSDC_PAD_TUNE_CLK_XDLY_SHIFT);

        sdr_clr_bits(MSDC_PATCH_BIT1, 0x7);//WRDAT_CRCS_TA_CNTR
        sdr_set_bits(MSDC_PATCH_BIT1, (_arEMMC_DevInfo1[emmcidx].HS200Delay&0xff000000)>>24);

        sdr_clr_bits(MSDC_PATCH_BIT0, MSDC_CKGEN_MSDC_DLY_SEL);//CKGEN_MSDC_DLY_SEL
        sdr_set_bits(MSDC_PATCH_BIT0, ((_arEMMC_DevInfo1[emmcidx].HS200Delay&0x00ff0000)>>16)<<10);
#endif

    }
#endif
}

void msdc_sample_edge(struct mmc_host*mmc, u32 flag)
{
    struct msdc_host *host = mmc_priv(mmc);
    struct msdc_hw *hw=host->hw;
    u32 base = host->base;
#if defined(CC_MT5399) || defined(CONFIG_ARCH_MT5399)|| defined(CC_MT5890)|| defined(CONFIG_ARCH_MT5890)|| \
    defined(CC_MT5882) || defined(CONFIG_ARCH_MT5882)
    // Sample Edge init
    sdr_clr_bits(MSDC_IOCON, 0xFFFFFF);

    // Sample Edge init
    if(mmc->ios.timing == MMC_TIMING_LEGACY)
    {
        sdr_set_bits(MSDC_IOCON, _arEMMC_DevInfo1[emmcidx].DS26Sample & 0xFFFFFF);
    }
    else if(mmc->ios.timing == MMC_TIMING_MMC_HS)
    {
	#if defined(CC_MT5890) || defined(CONFIG_ARCH_MT5890)
	/*do set IOCON in new ett flow*/
	#else
        sdr_set_bits(MSDC_IOCON, _arEMMC_DevInfo1[emmcidx].HS52Sample & 0xFFFFFF);
	#endif
    }
    else if(mmc->ios.timing == MMC_TIMING_UHS_DDR50)
    {
        sdr_set_bits(MSDC_IOCON, _arEMMC_DevInfo1[emmcidx].DDR52Sample & 0xFFFFFF);
        sdr_write32(MSDC_CLK_H_REG1, 0x2);
    }
    else if(mmc->ios.timing == MMC_TIMING_MMC_HS200)
    {
        sdr_set_bits(MSDC_IOCON, _arEMMC_DevInfo1[emmcidx].HS200Sample & 0xFFFFFF);
        sdr_write32(MSDC_CLK_H_REG1, 0x2);
    }
#endif
}

extern int __cmd_tuning_test;
static unsigned int msdc_command_resp_polling(struct msdc_host   *host,
                                      struct mmc_command *cmd,
                                      int                 tune,
                                      unsigned long       timeout)
{
    u32 base = host->base;
    u32 intsts;
    u32 resp;
    //u32 status;
    unsigned long tmo;
    //struct mmc_data   *data = host->data;

    u32 cmdsts = MSDC_INT_CMDRDY  | MSDC_INT_RSPCRCERR  | MSDC_INT_CMDTMO;

    resp = host->cmd_rsp;

    /*polling*/
    tmo = jiffies + timeout;
	while (1){
		if (((intsts = sdr_read32(MSDC_INT)) & cmdsts) != 0){
            /* clear all int flag */
			intsts &= cmdsts;
            sdr_write32(MSDC_INT, intsts);
            break;
        }

		 if (time_after(jiffies, tmo)) {
                ERR_MSG("XXX CMD<%d> polling_for_completion timeout ARG<0x%.8x>", cmd->opcode, cmd->arg);
                cmd->error = (unsigned int)-ETIMEDOUT;
				host->sw_timeout++;
		        msdc_dump_info(host->id);
                msdc_reset_hw(host->id);
                goto out;
         }
    }

    /* command interrupts */
    if (intsts & cmdsts) {
        if ((intsts & MSDC_INT_CMDRDY) || (intsts & MSDC_INT_ACMDRDY) ||
            (intsts & MSDC_INT_ACMD19_DONE)) {
            u32 *rsp = NULL;
			rsp = &cmd->resp[0];
		   if((-1 == __cmd_tuning_test) && (cmd->opcode == MMC_READ_SINGLE_BLOCK || cmd->opcode == MMC_READ_MULTIPLE_BLOCK || cmd->opcode == MMC_WRITE_MULTIPLE_BLOCK))
		   {
		   	   cmd->error = (unsigned int)-EIO;
                IRQ_MSG("yf test:fake XXX CMD<%d> MSDC_INT_RSPCRCERR Arg<0x%.8x>",cmd->opcode, cmd->arg);
                msdc_reset_hw(host->id);
                __cmd_tuning_test = 0;
                goto out;
		   }
            switch (host->cmd_rsp) {
            case RESP_NONE:
                break;
            case RESP_R2:
                *rsp++ = sdr_read32(SDC_RESP3); *rsp++ = sdr_read32(SDC_RESP2);
                *rsp++ = sdr_read32(SDC_RESP1); *rsp++ = sdr_read32(SDC_RESP0);
                break;
            default: /* Response types 1, 3, 4, 5, 6, 7(1b) */
                *rsp = sdr_read32(SDC_RESP0);
                break;
            }
            if(cmd->opcode == MMC_ALL_SEND_CID && host->hw->host_function == MSDC_EMMC) //dtv remained 
        	{
                msdc_find_dev(cmd->resp);
        	}
        } else if (intsts & MSDC_INT_RSPCRCERR) {
            cmd->error = (unsigned int)-EIO;
            IRQ_MSG("XXX CMD<%d> MSDC_INT_RSPCRCERR Arg<0x%.8x>",cmd->opcode, cmd->arg);
            msdc_reset_hw(host->id);
        } else if (intsts & MSDC_INT_CMDTMO) {
            cmd->error = (unsigned int)-ETIMEDOUT;
            IRQ_MSG("XXX CMD<%d> MSDC_INT_CMDTMO Arg<0x%.8x>",cmd->opcode, cmd->arg);
            msdc_reset_hw(host->id);
        }
    }
out:
    host->cmd = NULL;

    return cmd->error;
}

static unsigned int msdc_command_resp(struct msdc_host   *host,
                                      struct mmc_command *cmd,
                                      int                 tune,
                                      unsigned long       timeout)
{
    u32 base = host->base;
    u32 opcode = cmd->opcode;
    //u32 resp = host->cmd_rsp;
    //u32 tmo;
    //u32 intsts;

    spin_unlock(&host->lock);
    if(!wait_for_completion_timeout(&host->cmd_done, 10*timeout)){
        ERR_MSG("XXX CMD<%d> wait_for_completion timeout ARG<0x%.8x>", opcode, cmd->arg);
		host->sw_timeout++;
        msdc_dump_info(host->id);
        cmd->error = (unsigned int)-ETIMEDOUT;
        msdc_reset_hw(host->id);
    }
    spin_lock(&host->lock);

    sdr_clr_bits(MSDC_INTEN, wints_cmd);
    host->cmd = NULL;
    /* if (resp == RESP_R1B) {
        while ((sdr_read32(MSDC_PS) & 0x10000) != 0x10000);
    } */

    return cmd->error;
}

static unsigned int msdc_do_command(struct msdc_host   *host,
                                      struct mmc_command *cmd,
                                      int                 tune,
                                      unsigned long       timeout)
{
	if((cmd->opcode == MMC_GO_IDLE_STATE) && (host->hw->host_function == MSDC_SD)){
		mdelay(10);
	}

    if (msdc_command_start(host, cmd, tune, timeout))
        goto end;

    if (cmd->opcode == MMC_SEND_STATUS){
        if (msdc_command_resp(host, cmd, tune, timeout))
            goto end;
    } else {
        if (msdc_command_resp_polling(host, cmd, tune, timeout))
            goto end;
    }
end:

    N_MSG(CMD, "        return<%d> resp<0x%.8x>", cmd->error, cmd->resp[0]);
    return cmd->error;
}

/* The abort condition when PIO read/write
   tmo:
*/
static int msdc_pio_abort(struct msdc_host *host, struct mmc_data *data, unsigned long tmo)
{
    int  ret = 0;
    u32  base = host->base;

    if (atomic_read(&host->abort)) {
        ret = 1;
    }

    if (time_after(jiffies, tmo)) {
        data->error = (unsigned int)-ETIMEDOUT;
        ERR_MSG("XXX PIO Data Timeout: CMD<%d>", host->mrq->cmd->opcode);
        msdc_dump_info(host->id);
        ret = 1;
    }

    if(ret) {
        msdc_reset_hw(host->id);
        ERR_MSG("msdc pio find abort");
    }
    return ret;
}

/*
   Need to add a timeout, or WDT timeout, system reboot.
*/
// pio mode data read/write
static int msdc_pio_read(struct msdc_host *host, struct mmc_data *data)
{
    struct scatterlist *sg = data->sg;
    u32  base = host->base;
    u32  num = data->sg_len;
    u32 *ptr;
    u8  *u8ptr;
    u8 *test;
    u32  left = 0;
    u32  count, size = 0;
    u32  wints = MSDC_INTEN_DATTMO | MSDC_INTEN_DATCRCERR | MSDC_INTEN_XFER_COMPL;
	u32  ints = 0;
	bool get_xfer_done = 0;
    unsigned long tmo = jiffies + DAT_TIMEOUT;  
          
    //sdr_clr_bits(MSDC_INTEN, wints);
    while (1) {
		if(!get_xfer_done){
			ints = sdr_read32(MSDC_INT);
			ints &= wints;
			sdr_write32(MSDC_INT,ints);
		}
		if(ints & MSDC_INT_DATTMO){
			data->error = (unsigned int)-ETIMEDOUT;
			msdc_reset_hw(host->id); 
			break;
			}
		else if(ints & MSDC_INT_DATCRCERR){
			data->error = (unsigned int)-EIO;
			msdc_reset_hw(host->id); 
			break;
			}
		else if(ints & MSDC_INT_XFER_COMPL){
			get_xfer_done = 1;
		}
		if((num == 0) && (left == 0) && get_xfer_done)
			break;  
		
		if(left == 0 && sg){
			left = sg_dma_len(sg);
			ptr = sg_virt(sg);
			test = sg_virt(sg);
		}
        if ((left >=  MSDC_FIFO_THD) && (msdc_rxfifocnt() >= MSDC_FIFO_THD)) {
             count = MSDC_FIFO_THD >> 2;
             do {
                *ptr++ = msdc_fifo_read32();
             } while (--count);
             left -= MSDC_FIFO_THD;
        } else if ((left < MSDC_FIFO_THD) && msdc_rxfifocnt() >= left) {
             while (left > 3) {
                *ptr++ = msdc_fifo_read32();
                 left -= 4;
             }
			 u8ptr = (u8 *)ptr; 
             while(left) {
                 * u8ptr++ = msdc_fifo_read8();
                 left--; 	  
             }
        }
    	
        if (msdc_pio_abort(host, data, tmo)) {
             goto end; 	
        }
		if(left == 0 && sg){
        	size += sg_dma_len(sg);
        	sg = sg_next(sg); 
			num--;
		}
    }

end:
    data->bytes_xfered += size;

    ERR_MSG("PIO Read<%d>bytes", size); //to check log output
    

    //sdr_clr_bits(MSDC_INTEN, wints);    
    if(data->error) ERR_MSG("read pio data->error<%d> left<%d> size<%d>", data->error, left, size);
    return data->error;
}

/* please make sure won't using PIO when size >= 512
   which means, memory card block read/write won't using pio
   then don't need to handle the CMD12 when data error.
*/
static int msdc_pio_write(struct msdc_host* host, struct mmc_data *data)
{
    u32  base = host->base;
    struct scatterlist *sg = data->sg;
    u32  num = data->sg_len;
    u32 *ptr;
    u8  *u8ptr;
    u32  left = 0;
    u32  count, size = 0;
    u32  wints = MSDC_INTEN_DATTMO | MSDC_INTEN_DATCRCERR | MSDC_INTEN_XFER_COMPL; 
	bool get_xfer_done = 0;
    unsigned long tmo = jiffies + DAT_TIMEOUT;  
    u32 ints = 0;
    //sdr_clr_bits(MSDC_INTEN, wints);
    while (1) {
		if(!get_xfer_done){
			ints = sdr_read32(MSDC_INT);
			ints &= wints;
			sdr_write32(MSDC_INT,ints);
		}
		if(ints & MSDC_INT_DATTMO){
			data->error = (unsigned int)-ETIMEDOUT;
			msdc_reset_hw(host->id); 
			break;
			}
		else if(ints & MSDC_INT_DATCRCERR){
			data->error = (unsigned int)-EIO;
			msdc_reset_hw(host->id); 
			break;
			}
		else if(ints & MSDC_INT_XFER_COMPL){
			get_xfer_done = 1;
		}
		if((num == 0) && (left == 0) && get_xfer_done)	
			break;
		
        if(left == 0 && sg){
			left = sg_dma_len(sg);
			ptr = sg_virt(sg);
		}
		
        if (left >= MSDC_FIFO_SZ && msdc_txfifocnt() == 0) {
            count = MSDC_FIFO_SZ >> 2;
            do {
                 msdc_fifo_write32(*ptr); ptr++;
            } while (--count);
             left -= MSDC_FIFO_SZ;
        } else if (left < MSDC_FIFO_SZ && msdc_txfifocnt() == 0) {
            while (left > 3) {
              msdc_fifo_write32(*ptr); ptr++;
              left -= 4;
            } 
            u8ptr = (u8*)ptr; 
             while(left){
                msdc_fifo_write8(*u8ptr);	u8ptr++;
                left--;
              }
           }
        if (msdc_pio_abort(host, data, tmo)) {
               goto end; 	
        }
		           
        if(left == 0 && sg){
        	size += sg_dma_len(sg);
        	sg = sg_next(sg); 
			num--;
        }
    }
end:    
    data->bytes_xfered += size;
    N_MSG(FIO, "        PIO Write<%d>bytes", size);
    if(data->error) ERR_MSG("write pio data->error<%d>", data->error);
    	
    //sdr_clr_bits(MSDC_INTEN, wints);  
    return data->error;	
}


static void msdc_dma_start(struct msdc_host *host)
{
    u32 base = host->base;
    u32 wints = MSDC_INTEN_XFER_COMPL | MSDC_INTEN_DATTMO | MSDC_INTEN_DATCRCERR ; 
    if(host->autocmd == MSDC_AUTOCMD12)
		wints |= MSDC_INT_ACMDCRCERR | MSDC_INT_ACMDTMO | MSDC_INT_ACMDRDY; 
    sdr_set_bits(MSDC_INTEN, wints);
    mb();
    sdr_set_field(MSDC_DMA_CTRL, MSDC_DMA_CTRL_START, 1);

    N_MSG(DMA, "DMA start");
}

static void msdc_dma_stop(struct msdc_host *host)
{
    u32 base = host->base;
	int retry = 30;
	int count = 1000;
    //u32 retries=500;
    u32 wints = MSDC_INTEN_XFER_COMPL | MSDC_INTEN_DATTMO | MSDC_INTEN_DATCRCERR ; 
    if(host->autocmd == MSDC_AUTOCMD12)
		wints |= MSDC_INT_ACMDCRCERR | MSDC_INT_ACMDTMO | MSDC_INT_ACMDRDY; 
    N_MSG(DMA, "DMA status: 0x%.8x",sdr_read32(MSDC_DMA_CFG));
    //while (sdr_read32(MSDC_DMA_CFG) & MSDC_DMA_CFG_STS);

    sdr_set_field(MSDC_DMA_CTRL, MSDC_DMA_CTRL_STOP, 1);
    //while (sdr_read32(MSDC_DMA_CFG) & MSDC_DMA_CFG_STS);
    msdc_retry((sdr_read32(MSDC_DMA_CFG) & MSDC_DMA_CFG_STS),retry,count,host->id);
	if(retry == 0){
		ERR_MSG("!!ASSERT!!");
		BUG();
	}
    mb();
    sdr_clr_bits(MSDC_INTEN, wints); /* Not just xfer_comp */

    N_MSG(DMA, "DMA stop");
}


/* calc checksum */
static u8 msdc_dma_calcs(u8 *buf, u32 len)
{
    u32 i, sum = 0;
    for (i = 0; i < len; i++) {
        sum += buf[i];
    }
    return 0xFF - (u8)sum;
}

/* gpd bd setup + dma registers */
static int msdc_dma_config(struct msdc_host *host, struct msdc_dma *dma)
{
    u32 base = host->base;
    u32 sglen = dma->sglen;
    //u32 i, j, num, bdlen, arg, xfersz;
    u32 j, num, bdlen;
    u32 dma_address, dma_len;
    u8  blkpad, dwpad, chksum;
    struct scatterlist *sg = dma->sg;
    gpd_t *gpd;
    bd_t *bd;

    switch (dma->mode) {
    case MSDC_MODE_DMA_BASIC:
        //BUG_ON(dma->xfersz > 65535); //to check, no such limitation here since 5399
        BUG_ON(dma->sglen > 1);
        dma_address = sg_dma_address(sg);
        
        dma_len = sg_dma_len(sg);
        BUG_ON(dma_len >  512 * 1024);
        
        if(dma_len > 64 * 1024)
        {
          //printk(KERN_ERR "yf sg len 0x%x\n", dma_len);        	
        }
        sdr_write32(MSDC_DMA_SA, dma_address);

        sdr_set_field(MSDC_DMA_CTRL, MSDC_DMA_CTRL_LASTBUF, 1);
#if defined(CC_MT5399) || defined(CONFIG_ARCH_MT5399)|| defined(CC_MT5890)|| defined(CONFIG_ARCH_MT5890)|| \
    defined(CC_MT5882) || defined(CONFIG_ARCH_MT5882)
        /* a change happens for dma xfer size:
              * a new 32-bit register(0xA8) is used for xfer size configuration instead of 16-bit register(0x98 DMA_CTRL)
              */
        sdr_write32(MSDC_DMA_LEN, dma_len);
    #else
        sdr_set_field(MSDC_DMA_CTRL, MSDC_DMA_CTRL_XFERSZ, dma_len);
    #endif
        sdr_set_field(MSDC_DMA_CTRL, MSDC_DMA_CTRL_BRUSTSZ, dma->burstsz);
        sdr_set_field(MSDC_DMA_CTRL, MSDC_DMA_CTRL_MODE, 0);
        break;
    case MSDC_MODE_DMA_DESC:
        blkpad = (dma->flags & DMA_FLAG_PAD_BLOCK) ? 1 : 0;
        dwpad  = (dma->flags & DMA_FLAG_PAD_DWORD) ? 1 : 0;
        chksum = (dma->flags & DMA_FLAG_EN_CHKSUM) ? 1 : 0;

        /* calculate the required number of gpd */
        num = (sglen + MAX_BD_PER_GPD - 1) / MAX_BD_PER_GPD;
        BUG_ON(num > 1 );

        gpd = dma->gpd;
        bd  = dma->bd;
        bdlen = sglen;

        /* modify gpd*/
        //gpd->intr = 0;
        gpd->hwo = 1;  /* hw will clear it */
        gpd->bdp = 1;
        gpd->chksum = 0;  /* need to clear first. */
        gpd->chksum = (chksum ? msdc_dma_calcs((u8 *)gpd, 16) : 0);

        /* modify bd*/
        for (j = 0; j < bdlen; j++) {
#ifdef MSDC_DMA_VIOLATION_DEBUG
            if(g_dma_debug[host->id] && (msdc_latest_operation_type[host->id] == OPER_TYPE_READ)){
               printk("[%s] msdc%d do write 0x10000\n", __func__, host->id);
               dma_address = 0x10000;
            }else{
               dma_address = sg_dma_address(sg);
            }
#else
            dma_address = sg_dma_address(sg);
#endif
            dma_len = sg_dma_len(sg);
            msdc_init_bd(&bd[j], blkpad, dwpad, dma_address, dma_len);

            if(j == bdlen - 1) {
            bd[j].eol = 1;     	/* the last bd */
            } else {
                bd[j].eol = 0;
            }
            bd[j].chksum = 0; /* checksume need to clear first */
            bd[j].chksum = (chksum ? msdc_dma_calcs((u8 *)(&bd[j]), 16) : 0);
            sg++;
        }
#ifdef MSDC_DMA_VIOLATION_DEBUG
        if(g_dma_debug[host->id] && (msdc_latest_operation_type[host->id] == OPER_TYPE_READ))
             g_dma_debug[host->id] = 0;
#endif

        dma->used_gpd += 2;
        dma->used_bd += bdlen;

        sdr_set_field(MSDC_DMA_CFG, MSDC_DMA_CFG_DECSEN, chksum);
        sdr_set_field(MSDC_DMA_CTRL, MSDC_DMA_CTRL_BRUSTSZ, dma->burstsz);
        sdr_set_field(MSDC_DMA_CTRL, MSDC_DMA_CTRL_MODE, 1);

        sdr_write32(MSDC_DMA_SA, (u32)dma->gpd_addr);
        break;

    default:
        break;
    }

    N_MSG(DMA, "DMA_CTRL = 0x%x", sdr_read32(MSDC_DMA_CTRL));
    N_MSG(DMA, "DMA_CFG  = 0x%x", sdr_read32(MSDC_DMA_CFG));
    N_MSG(DMA, "DMA_SA   = 0x%x", sdr_read32(MSDC_DMA_SA));

    return 0;
}

static void msdc_dma_setup(struct msdc_host *host, struct msdc_dma *dma,
    struct scatterlist *sg, unsigned int sglen)
{
    BUG_ON(sglen > MAX_BD_NUM); /* not support currently */

    dma->sg = sg;
    dma->flags = DMA_FLAG_EN_CHKSUM;
    //dma->flags = DMA_FLAG_NONE; /* CHECKME */
    dma->sglen = sglen;
    dma->xfersz = host->xfer_size;
    dma->burstsz = MSDC_BRUST_64B;

    if (sglen == 1 && sg_dma_len(sg) <= MAX_DMA_CNT)
        dma->mode = MSDC_MODE_DMA_BASIC;
    else
    	dma->mode = MSDC_MODE_DMA_DESC;
    	
    	#ifdef CC_MTD_ENCRYPT_SUPPORT  
    	BUG_ON(sglen > 1);
    	dma->mode = MSDC_MODE_DMA_BASIC;
    	#endif

    N_MSG(DMA, "DMA mode<%d> sglen<%d> xfersz<%d>", dma->mode, dma->sglen, dma->xfersz);

    msdc_dma_config(host, dma);
}

/* set block number before send command */
static void msdc_set_blknum(struct msdc_host *host, u32 blknum)
{
    u32 base = host->base;

    sdr_write32(SDC_BLK_NUM, blknum);
}


#define REQ_CMD_EIO  (0x1 << 0)
#define REQ_CMD_TMO  (0x1 << 1)
#define REQ_DAT_ERR  (0x1 << 2)
#define REQ_STOP_EIO (0x1 << 3)
#define REQ_STOP_TMO (0x1 << 4)

static void msdc_restore_info(struct msdc_host *host){
    u32 base = host->base;
    int retry = 3;
    mb();
    msdc_reset_hw(host->id);
    host->saved_para.msdc_cfg = host->saved_para.msdc_cfg & 0xFFFFFFDF; //force bit5(BV18SDT) to 0
    sdr_write32(MSDC_CFG,host->saved_para.msdc_cfg);
    
    while(retry--){
        msdc_set_mclk(host, host->saved_para.ddr, host->saved_para.hz);
        if(sdr_read32(MSDC_CFG) != host->saved_para.msdc_cfg){
            ERR_MSG("msdc3 set_mclk is unstable (cur_cfg=%x, save_cfg=%x, cur_hz=%d, save_hz=%d).", sdr_read32(MSDC_CFG), host->saved_para.msdc_cfg, host->mclk, host->saved_para.hz);
        } else 
            break;
    } 

    sdr_set_field(MSDC_PATCH_BIT0, MSDC_INT_DAT_LATCH_CK_SEL,    host->saved_para.int_dat_latch_ck_sel); //for SDIO 3.0
    sdr_set_field(MSDC_PATCH_BIT0, MSDC_CKGEN_MSDC_DLY_SEL, host->saved_para.ckgen_msdc_dly_sel); //for SDIO 3.0
    sdr_set_field(MSDC_PATCH_BIT1, MSDC_PATCH_BIT1_CMD_RSP,    host->saved_para.cmd_resp_ta_cntr);//for SDIO 3.0
    sdr_set_field(MSDC_PATCH_BIT1, MSDC_PATCH_BIT1_WRDAT_CRCS, host->saved_para.wrdat_crc_ta_cntr); //for SDIO 3.0
    sdr_write32(MSDC_DAT_RDDLY0,host->saved_para.ddly0);
    sdr_write32(MSDC_PAD_TUNE,host->saved_para.pad_tune);
    sdr_write32(SDC_CFG,host->saved_para.sdc_cfg);
    sdr_write32(MSDC_IOCON,host->saved_para.iocon);
}
static int msdc_do_request(struct mmc_host*mmc, struct mmc_request*mrq)
{
    struct msdc_host *host = mmc_priv(mmc);
    struct mmc_command *cmd;
    struct mmc_data *data;

    u32 base = host->base;
    //u32 intsts = 0;
    unsigned int left=0;
    int dma = 0, read = 1, dir = DMA_FROM_DEVICE, send_type=0;
    u32 map_sg = 0;  /* Fix the bug of dma_map_sg and dma_unmap_sg not match issue */
	unsigned long pio_tmo;
    #define SND_DAT 0
    #define SND_CMD 1

    if (is_card_sdio(host)) {
        mb();
        if (host->saved_para.hz){
            if (host->saved_para.suspend_flag){
                ERR_MSG("msdc3 resume[s] cur_cfg=%x, save_cfg=%x, cur_hz=%d, save_hz=%d", sdr_read32(MSDC_CFG), host->saved_para.msdc_cfg, host->mclk, host->saved_para.hz);
                host->saved_para.suspend_flag = 0;
                msdc_restore_info(host);
            }else if ((host->saved_para.msdc_cfg !=0) && (sdr_read32(MSDC_CFG)!= host->saved_para.msdc_cfg)){
                // Notice it turns off clock, it possible needs to restore the register
                ERR_MSG("msdc3 resume[ns] cur_cfg=%x, save_cfg=%x, cur_hz=%d, save_hz=%d", sdr_read32(MSDC_CFG), host->saved_para.msdc_cfg, host->mclk, host->saved_para.hz);
               
                msdc_restore_info(host);        
            }
        }
    }

    BUG_ON(mmc == NULL);
    BUG_ON(mrq == NULL);

    host->error = 0;
    atomic_set(&host->abort, 0);

    cmd  = mrq->cmd;
    data = mrq->cmd->data;

    /* check msdc is work ok. rule is RX/TX fifocnt must be zero after last request
     * if find abnormal, try to reset msdc first
     */
    if (msdc_txfifocnt() || msdc_rxfifocnt()) {
        printk(KERN_ERR "[SD%d] register abnormal,please check!\n",host->id);
        msdc_reset_hw(host->id);
    }

    if (!data) {
        send_type=SND_CMD;

        if (msdc_do_command(host, cmd, 0, CMD_TIMEOUT) != 0) {
            goto done;
        }

#ifdef CONFIG_MTKEMMC_BOOT
        if(host->hw->host_function == MSDC_EMMC &&
		  host->hw->boot == MSDC_BOOT_EN &&
			cmd->opcode == MMC_SWITCH &&
			(((cmd->arg >> 16) & 0xFF) == EXT_CSD_PART_CFG)){
            partition_access = (char)((cmd->arg >> 8) & 0x07);
            printk(KERN_ERR "partition_access is 0x%x\n",partition_access);
        }
#endif

    } else {
        BUG_ON(data->blksz > HOST_MAX_BLKSZ);
        send_type=SND_DAT;

        data->error = 0;
        read = data->flags & MMC_DATA_READ ? 1 : 0;
        msdc_latest_operation_type[host->id] = read ? OPER_TYPE_READ : OPER_TYPE_WRITE;
        host->data = data;
        host->xfer_size = data->blocks * data->blksz;
        host->blksz = data->blksz;

        /* deside the transfer mode */
        if (drv_mode[host->id] == MODE_PIO) {
            host->dma_xfer = dma = 0;
            msdc_latest_transfer_mode[host->id] = TRAN_MOD_PIO;
        } else if (drv_mode[host->id] == MODE_DMA) {
            host->dma_xfer = dma = 1;
            msdc_latest_transfer_mode[host->id] = TRAN_MOD_DMA;
        } else if (drv_mode[host->id] == MODE_SIZE_DEP) {
            host->dma_xfer = dma = ((host->xfer_size >= dma_size[host->id]) ? 1 : 0);
            msdc_latest_transfer_mode[host->id] = dma ? TRAN_MOD_DMA: TRAN_MOD_PIO;
        }

        if (read) {
            if ((host->timeout_ns != data->timeout_ns) ||
                (host->timeout_clks != data->timeout_clks)) {
                msdc_set_timeout(host, data->timeout_ns, data->timeout_clks);
            }
        }

        msdc_set_blknum(host, data->blocks);
        //msdc_clr_fifo();  /* no need */

        if (dma) {
            msdc_dma_on();  /* enable DMA mode first!! */
            init_completion(&host->xfer_done);

            /* start the command first*/
			if(host->hw->host_function != MSDC_SDIO && !is_card_sdio(host)){
					host->autocmd = MSDC_AUTOCMD12;
			}
			if(3 == partition_access)
			{
				host->autocmd = 0;
			}
            if (msdc_command_start(host, cmd, 0, CMD_TIMEOUT) != 0)
                goto done;

        	#ifdef CC_MTD_ENCRYPT_SUPPORT  

            if(((cmd->opcode == MMC_WRITE_BLOCK) ||
            	   (cmd->opcode == MMC_WRITE_MULTIPLE_BLOCK)) && (host->hw->host_function == MSDC_EMMC))
            {
            	  u32 addr_blk = mmc_card_blockaddr(host->mmc->card)?(cmd->arg):(cmd->arg>>9);
            	  	
                if(msdc_partition_encrypted(addr_blk, host))
                {  
                	spin_unlock(&host->lock);
                	if (msdc_aes_encryption_sg(host->data,host))
                	{
                	    printk(KERN_ERR "[1]encryption before write process failed!\n");	
                	    BUG_ON(1);
                	} 
                	spin_lock(&host->lock);
                	//printk(KERN_ERR "[1]encryption before write process success!\n");	
                } 	
            }  
        #endif
            dir = read ? DMA_FROM_DEVICE : DMA_TO_DEVICE;
            u32 sgcurnum = dma_map_sg(mmc_dev(mmc), data->sg, data->sg_len, dir);
            if(data->sg_len != sgcurnum)
            {
                printk(KERN_ERR "orig sg num is %d, cur sg num is %d\n", data->sg_len, sgcurnum);    
                data->sg_len = sgcurnum;        	
            }
            map_sg = 1;

            /* then wait command done */
            if (msdc_command_resp_polling(host, cmd, 0, CMD_TIMEOUT) != 0){ //not tuning
                goto stop;
            }

            /* for read, the data coming too fast, then CRC error
               start DMA no business with CRC. */
            //init_completion(&host->xfer_done);
            msdc_dma_setup(host, &host->dma, data->sg, data->sg_len);

	
            msdc_dma_start(host);

            spin_unlock(&host->lock);
            if(!wait_for_completion_timeout(&host->xfer_done, DAT_TIMEOUT)){
                ERR_MSG("XXX CMD<%d> ARG<0x%x> wait xfer_done<%d> timeout!!", cmd->opcode, cmd->arg,data->blocks * data->blksz);
				host->sw_timeout++;

                msdc_dump_info(host->id);
                data->error = (unsigned int)-ETIMEDOUT;

                msdc_reset_hw(host->id);
            }
            spin_lock(&host->lock);
            msdc_dma_stop(host);
			if ((mrq->data && mrq->data->error)||(host->autocmd == MSDC_AUTOCMD12 && mrq->stop && mrq->stop->error)){
				msdc_clr_fifo(host->id);
				msdc_clr_int();
			}

        } else {
            /* Firstly: send command */
		   host->autocmd = 0;
		   host->dma_xfer = 0;
            if (msdc_do_command(host, cmd, 0, CMD_TIMEOUT) != 0) {
                goto stop;
            }

            /* Secondly: pio data phase */
            if (read) {
                if (msdc_pio_read(host, data)){
                    goto stop; 	 // need cmd12.
                }
            } else {
                if (msdc_pio_write(host, data)) {
                    goto stop;
                }
            }

            /* For write case: make sure contents in fifo flushed to device */
            if (!read) {
				pio_tmo = jiffies + DAT_TIMEOUT;
                while (1) {
                    left=msdc_txfifocnt();
                    if (left == 0) {
                        break;
                    }
                    if (msdc_pio_abort(host, data, pio_tmo)) {
                        break;
                        /* Fix me: what about if data error, when stop ? how to? */
                    }
                }
            } else {
                /* Fix me: read case: need to check CRC error */
            }

            /* For write case: SDCBUSY and Xfer_Comp will assert when DAT0 not busy.
               For read case : SDCBUSY and Xfer_Comp will assert when last byte read out from FIFO.
            */

            /* try not to wait xfer_comp interrupt.
               the next command will check SDC_BUSY.
               SDC_BUSY means xfer_comp assert
            */

        } // PIO mode

stop:
        /* Last: stop transfer */
        if (data->stop){
			if(!((cmd->error == 0) && (data->error == 0) && (host->autocmd == MSDC_AUTOCMD12) && (cmd->opcode == MMC_READ_MULTIPLE_BLOCK || cmd->opcode == MMC_WRITE_MULTIPLE_BLOCK))){
            	if (msdc_do_command(host, data->stop, 0, CMD_TIMEOUT) != 0) {
                	goto done;
            	}
			}
        }
    }

done:

    if (data != NULL) {
        host->data = NULL;
        host->dma_xfer = 0;

        if (dma != 0) {
            msdc_dma_off();
            host->dma.used_bd  = 0;
            host->dma.used_gpd = 0;
            if (map_sg == 1) {
				/*if(data->error == 0){
                int retry = 3;
                int count = 1000;
				msdc_retry(host->dma.gpd->hwo,retry,count,host->id);
				}*/
                dma_unmap_sg(mmc_dev(mmc), data->sg, data->sg_len, dir);
                #ifdef CC_MTD_ENCRYPT_SUPPORT  
                 if(((cmd->opcode == MMC_READ_SINGLE_BLOCK) ||
                 	    (cmd->opcode == MMC_READ_MULTIPLE_BLOCK)) && (host->hw->host_function == MSDC_EMMC))
                 {
                 	  u32 addr_blk = mmc_card_blockaddr(host->mmc->card)?(cmd->arg):(cmd->arg>>9);
                 	  	
                     if(msdc_partition_encrypted(addr_blk, host))
                     {  
                      spin_unlock(&host->lock);
                     	if (msdc_aes_decryption_sg(data,host))
                     	{
                     	    printk(KERN_ERR "[1]decryption after read process failed!\n");	
                     	    BUG_ON(1);
                     	}
                     	spin_lock(&host->lock);
                     	
//                     	printk(KERN_ERR "[1]decryption after read process success!\n");	 
                     } 	
                 }  
                 #endif
            }
        }
#ifdef CONFIG_MTKEMMC_BOOT
        if(cmd->opcode == MMC_SEND_EXT_CSD){
            msdc_get_data(ext_csd,data);
        }
#endif
        host->blksz = 0;


        N_MSG(OPS, "CMD<%d> data<%s %s> blksz<%d> block<%d> error<%d>",cmd->opcode, (dma? "dma":"pio"),
                (read ? "read ":"write") ,data->blksz, data->blocks, data->error);

        if (!is_card_sdio(host)) {
            if ((cmd->opcode != 17)&&(cmd->opcode != 18)&&(cmd->opcode != 24)&&(cmd->opcode != 25)) {
                N_MSG(NRW, "CMD<%3d> arg<0x%8x> Resp<0x%8x> data<%s> size<%d>", cmd->opcode, cmd->arg, cmd->resp[0],
                    (read ? "read ":"write") ,data->blksz * data->blocks);
            } else {
                N_MSG(RW,  "CMD<%3d> arg<0x%8x> Resp<0x%8x> block<%d>", cmd->opcode,
                    cmd->arg, cmd->resp[0], data->blocks);
            }
        }
    } else {
        if (!is_card_sdio(host)) {
            if (cmd->opcode != 13) { // by pass CMD13
                N_MSG(NRW, "CMD<%3d> arg<0x%8x> resp<%8x %8x %8x %8x>", cmd->opcode, cmd->arg,
                    cmd->resp[0],cmd->resp[1], cmd->resp[2], cmd->resp[3]);
            }
        }
    }

    if (mrq->cmd->error == (unsigned int)-EIO) {
        host->error |= REQ_CMD_EIO;
        sdio_tune_flag |= 0x1;

        if( mrq->cmd->opcode == SD_IO_RW_EXTENDED )
            sdio_tune_flag |= 0x1;
    }
	if (mrq->cmd->error == (unsigned int)-ETIMEDOUT) 
		host->error |= REQ_CMD_TMO;
     if (mrq->data && mrq->data->error) {
        host->error |= REQ_DAT_ERR;

        sdio_tune_flag |= 0x10;

	    if (mrq->data->flags & MMC_DATA_READ)
		   sdio_tune_flag |= 0x80;
         else
		   sdio_tune_flag |= 0x40;
    }
     if (mrq->stop && (mrq->stop->error == (unsigned int)-EIO)) host->error |= REQ_STOP_EIO;
	if (mrq->stop && (mrq->stop->error == (unsigned int)-ETIMEDOUT)) host->error |= REQ_STOP_TMO;
    //if (host->error) ERR_MSG("host->error<%d>", host->error);

    return host->error;
}
static int msdc_tune_rw_request(struct mmc_host*mmc, struct mmc_request*mrq)
{
    struct msdc_host *host = mmc_priv(mmc);
    struct mmc_command *cmd;
    struct mmc_data *data;

    u32 base = host->base;
    //u32 intsts = 0;
	  //unsigned int left=0;
    int read = 1,dma = 1;//dir = DMA_FROM_DEVICE, send_type=0,
    //u32 map_sg = 0;  /* Fix the bug of dma_map_sg and dma_unmap_sg not match issue */
    //u32 bus_mode = 0;
    #define SND_DAT 0
    #define SND_CMD 1

    BUG_ON(mmc == NULL);
    BUG_ON(mrq == NULL);

    //host->error = 0;
    atomic_set(&host->abort, 0);

    cmd  = mrq->cmd;
    data = mrq->cmd->data;

    /* check msdc is work ok. rule is RX/TX fifocnt must be zero after last request
     * if find abnormal, try to reset msdc first
     */
    if (msdc_txfifocnt() || msdc_rxfifocnt()) {
        printk("[SD%d] register abnormal,please check!\n",host->id);
        msdc_reset_hw(host->id);
    }


      BUG_ON(data->blksz > HOST_MAX_BLKSZ);
      //send_type=SND_DAT;

      data->error = 0;
      read = data->flags & MMC_DATA_READ ? 1 : 0;
      msdc_latest_operation_type[host->id] = read ? OPER_TYPE_READ : OPER_TYPE_WRITE;
      host->data = data;
      host->xfer_size = data->blocks * data->blksz;
      host->blksz = data->blksz;
	  host->dma_xfer = 1;

        /* deside the transfer mode */
		/*
        if (drv_mode[host->id] == MODE_PIO) {
            host->dma_xfer = dma = 0;
            msdc_latest_transfer_mode[host->id] = TRAN_MOD_PIO;
        } else if (drv_mode[host->id] == MODE_DMA) {
            host->dma_xfer = dma = 1;
            msdc_latest_transfer_mode[host->id] = TRAN_MOD_DMA;
        } else if (drv_mode[host->id] == MODE_SIZE_DEP) {
            host->dma_xfer = dma = ((host->xfer_size >= dma_size[host->id]) ? 1 : 0);
            msdc_latest_transfer_mode[host->id] = dma ? TRAN_MOD_DMA: TRAN_MOD_PIO;
        }
		*/
        if (read) {
            if ((host->timeout_ns != data->timeout_ns) ||
                (host->timeout_clks != data->timeout_clks)) {
                msdc_set_timeout(host, data->timeout_ns, data->timeout_clks);
            }
        }

        msdc_set_blknum(host, data->blocks);
        //msdc_clr_fifo();  /* no need */
        msdc_dma_on();  /* enable DMA mode first!! */
        init_completion(&host->xfer_done);

          /* start the command first*/
		if(host->hw->host_function != MSDC_SDIO && !is_card_sdio(host)){
				host->autocmd = MSDC_AUTOCMD12;
		}
        if (msdc_command_start(host, cmd, 0, CMD_TIMEOUT) != 0)
            goto done;



        /* then wait command done */
        if (msdc_command_resp_polling(host, cmd, 0, CMD_TIMEOUT) != 0){ //not tuning
            ERR_MSG("XXX CMD<%d> ARG<0x%x> polling cmd resp <%d> timeout!!", cmd->opcode, cmd->arg,data->blocks * data->blksz);
            goto stop;
        }

        /* for read, the data coming too fast, then CRC error
           start DMA no business with CRC. */
        msdc_dma_setup(host, &host->dma, data->sg, data->sg_len);
        msdc_dma_start(host);

        spin_unlock(&host->lock);
        if(!wait_for_completion_timeout(&host->xfer_done, DAT_TIMEOUT)){
            ERR_MSG("XXX CMD<%d> ARG<0x%x> wait xfer_done<%d> timeout!!", cmd->opcode, cmd->arg,data->blocks * data->blksz);
		    host->sw_timeout++;

            data->error = (unsigned int)-ETIMEDOUT;

            msdc_reset_hw(host->id);
        }
        spin_lock(&host->lock);

        msdc_dma_stop(host);
		if ((mrq->data && mrq->data->error)||(host->autocmd == MSDC_AUTOCMD12 && mrq->stop && mrq->stop->error)){
			msdc_clr_fifo(host->id);
			msdc_clr_int();
		}



stop:
        /* Last: stop transfer */

        if (data->stop){
			if(!((cmd->error == 0) && (data->error == 0) && (host->autocmd == MSDC_AUTOCMD12) && (cmd->opcode == MMC_READ_MULTIPLE_BLOCK || cmd->opcode == MMC_WRITE_MULTIPLE_BLOCK))){
            	if (msdc_do_command(host, data->stop, 0, CMD_TIMEOUT) != 0) {
                	goto done;
            	}
			}
        }

done:
        host->data = NULL;
        host->dma_xfer = 0;
		msdc_dma_off();
        host->dma.used_bd  = 0;
        host->dma.used_gpd = 0;
        host->blksz = 0;


        N_MSG(OPS, "CMD<%d> data<%s %s> blksz<%d> block<%d> error<%d>",cmd->opcode, (dma? "dma":"pio"),
                (read ? "read ":"write") ,data->blksz, data->blocks, data->error);

        if (!is_card_sdio(host)) {
            if ((cmd->opcode != 17)&&(cmd->opcode != 18)&&(cmd->opcode != 24)&&(cmd->opcode != 25)) {
                N_MSG(NRW, "CMD<%3d> arg<0x%8x> Resp<0x%8x> data<%s> size<%d>", cmd->opcode, cmd->arg, cmd->resp[0],
                    (read ? "read ":"write") ,data->blksz * data->blocks);
            } else {
                N_MSG(RW,  "CMD<%3d> arg<0x%8x> Resp<0x%8x> block<%d>", cmd->opcode,
                    cmd->arg, cmd->resp[0], data->blocks);
            }
        }
    	else {
         if (!is_card_sdio(host)) {
            if (cmd->opcode != 13) { // by pass CMD13
                N_MSG(NRW, "CMD<%3d> arg<0x%8x> resp<%8x %8x %8x %8x>", cmd->opcode, cmd->arg,
                    cmd->resp[0],cmd->resp[1], cmd->resp[2], cmd->resp[3]);
            }
        }
    }
	host->error = 0;
    if (mrq->cmd->error == (unsigned int)-EIO) host->error |= REQ_CMD_EIO;
	if (mrq->cmd->error == (unsigned int)-ETIMEDOUT) host->error |= REQ_CMD_TMO;
    if (mrq->data && (mrq->data->error)) host->error |= REQ_DAT_ERR;
    if (mrq->stop && (mrq->stop->error == (unsigned int)-EIO)) host->error |= REQ_STOP_EIO;
	if (mrq->stop && (mrq->stop->error == (unsigned int)-ETIMEDOUT)) host->error |= REQ_STOP_TMO;
    return host->error;
}

static void msdc_pre_req(struct mmc_host *mmc, struct mmc_request *mrq,
			       bool is_first_req)
{
	struct msdc_host *host = mmc_priv(mmc);
    struct mmc_data *data;
	struct mmc_command *cmd = mrq->cmd;
	int read = 1, dir = DMA_FROM_DEVICE;
	BUG_ON(!cmd);
	data = mrq->data;
	if(data)
		data->host_cookie = 0;
	if(data && (cmd->opcode == MMC_READ_SINGLE_BLOCK || cmd->opcode == MMC_READ_MULTIPLE_BLOCK || cmd->opcode == MMC_WRITE_BLOCK || cmd->opcode == MMC_WRITE_MULTIPLE_BLOCK)){
		host->xfer_size = data->blocks * data->blksz;
		read = data->flags & MMC_DATA_READ ? 1 : 0;
		if (drv_mode[host->id] == MODE_PIO) {
			data->host_cookie = 0;
			msdc_latest_transfer_mode[host->id] = TRAN_MOD_PIO;
		} else if (drv_mode[host->id] == MODE_DMA) {
			data->host_cookie = 1;
			msdc_latest_transfer_mode[host->id] = TRAN_MOD_DMA;
		} else if (drv_mode[host->id] == MODE_SIZE_DEP) {
			data->host_cookie = ((host->xfer_size >= dma_size[host->id]) ? 1 : 0);
			msdc_latest_transfer_mode[host->id] = data->host_cookie ? TRAN_MOD_DMA: TRAN_MOD_PIO;
		}
		 if (data->host_cookie) {		 	
	 #ifdef CC_MTD_ENCRYPT_SUPPORT  
            if(((cmd->opcode == MMC_WRITE_BLOCK) ||
            	   (cmd->opcode == MMC_WRITE_MULTIPLE_BLOCK)) && (host->hw->host_function == MSDC_EMMC))
            {
            	  u32 addr_blk = mmc_card_blockaddr(host->mmc->card)?(cmd->arg):(cmd->arg>>9);
            	  	
                if(msdc_partition_encrypted(addr_blk, host))
                {  
                	if (msdc_aes_encryption_sg(data,host))
                	{
                	    printk(KERN_ERR "[1]encryption before write process failed!\n");	
                	    BUG_ON(1);
                	} 
                	//printk(KERN_ERR "[1]encryption before write process success!\n");	
                } 	
            }  
        #endif
			dir = read ? DMA_FROM_DEVICE : DMA_TO_DEVICE;
    		     u32 sgcurnum = dma_map_sg(mmc_dev(mmc), data->sg, data->sg_len, dir);
              if(data->sg_len != sgcurnum)
              {
                  printk(KERN_ERR "orig sg num is %d, cur sg num is %d\n", data->sg_len, sgcurnum);          
                  data->sg_len = sgcurnum;  	
              }
	 	}
	 	N_MSG(OPS, "CMD<%d> ARG<0x%x>data<%s %s> blksz<%d> block<%d> error<%d>",mrq->cmd->opcode,mrq->cmd->arg, (data->host_cookie ? "dma":"pio"),
                (read ? "read ":"write") ,data->blksz, data->blocks, data->error);
	}
	 return;
}
static void msdc_dma_clear(struct msdc_host *host)
{
		u32 base = host->base;
		host->data = NULL;
		N_MSG(DMA, "dma clear\n");
		host->mrq = NULL;
        host->dma_xfer = 0;
        msdc_dma_off();
        host->dma.used_bd  = 0;
        host->dma.used_gpd = 0;
        host->blksz = 0;
}
static void msdc_post_req(struct mmc_host *mmc, struct mmc_request *mrq, int err)
{
	struct msdc_host *host = mmc_priv(mmc);
	struct mmc_data *data;
	//struct mmc_command *cmd = mrq->cmd;
	int  read = 1, dir = DMA_FROM_DEVICE;
	data = mrq->data;
	if(data && data->host_cookie){
		host->xfer_size = data->blocks * data->blksz;
		read = data->flags & MMC_DATA_READ ? 1 : 0;
		dir = read ? DMA_FROM_DEVICE : DMA_TO_DEVICE;
		dma_unmap_sg(mmc_dev(mmc), data->sg, data->sg_len, dir);
		#ifdef CC_MTD_ENCRYPT_SUPPORT  
          if(((mrq->cmd->opcode == MMC_READ_SINGLE_BLOCK) ||
          	 (mrq->cmd->opcode == MMC_READ_MULTIPLE_BLOCK)) && (host->hw->host_function == MSDC_EMMC))
          {
          	  u32 addr_blk = mmc_card_blockaddr(host->mmc->card)?(mrq->cmd->arg):(mrq->cmd->arg>>9);
          	  	
              if(msdc_partition_encrypted(addr_blk, host))
              {  
              	if (msdc_aes_decryption_sg(data,host))
              	{
              	    printk(KERN_ERR "[1]decryption after read process failed!\n");	
              	    BUG_ON(1);
              	}           	
              } 	
          }  
         #endif
		data->host_cookie = 0;
		N_MSG(OPS, "CMD<%d> ARG<0x%x> blksz<%d> block<%d> error<%d>",mrq->cmd->opcode,mrq->cmd->arg,
                data->blksz, data->blocks, data->error);
	}
	return;

}
static int msdc_do_request_async(struct mmc_host*mmc, struct mmc_request*mrq)
{
    struct msdc_host *host = mmc_priv(mmc);
    struct mmc_command *cmd;
    struct mmc_data *data;
    u32 base = host->base;
    //u32 intsts = 0;
	  //unsigned int left=0;
    int dma = 0, read = 1;//, dir = DMA_FROM_DEVICE;
    //u32 map_sg = 0;  /* Fix the bug of dma_map_sg and dma_unmap_sg not match issue */
    //u32 bus_mode = 0;


    BUG_ON(mmc == NULL);
    BUG_ON(mrq == NULL);
	if (!is_card_present(host) || host->power_mode == MMC_POWER_OFF) 
	{
        ERR_MSG("cmd<%d> arg<0x%x> card<%d> power<%d>", mrq->cmd->opcode,mrq->cmd->arg,is_card_present(host), host->power_mode);
        mrq->cmd->error = (unsigned int)-ENOMEDIUM;
		if(mrq->done)
			mrq->done(mrq);         // call done directly.

		printk(KERN_ERR "card power off %d\n",__LINE__);
        return 0;
    }

	host->tune = 0;

    host->error = 0;
    atomic_set(&host->abort, 0);
    spin_lock(&host->lock);
    cmd  = mrq->cmd;
    data = mrq->cmd->data;
	host->mrq = mrq;
    /* check msdc is work ok. rule is RX/TX fifocnt must be zero after last request
     * if find abnormal, try to reset msdc first
     */
    if (msdc_txfifocnt() || msdc_rxfifocnt()) {
        printk("[SD%d] register abnormal,please check!\n",host->id);
        msdc_reset_hw(host->id);
    }

    BUG_ON(data->blksz > HOST_MAX_BLKSZ);
    //send_type=SND_DAT;

    data->error = 0;
    read = data->flags & MMC_DATA_READ ? 1 : 0;
    msdc_latest_operation_type[host->id] = read ? OPER_TYPE_READ : OPER_TYPE_WRITE;
    host->data = data;
    host->xfer_size = data->blocks * data->blksz;
    host->blksz = data->blksz;
    host->dma_xfer = 1;
    /* deside the transfer mode */


    if (read) {
        if ((host->timeout_ns != data->timeout_ns) ||
            (host->timeout_clks != data->timeout_clks)) {
            msdc_set_timeout(host, data->timeout_ns, data->timeout_clks);
        }
    }

    msdc_set_blknum(host, data->blocks);
    //msdc_clr_fifo();  /* no need */
    msdc_dma_on();  /* enable DMA mode first!! */
      //init_completion(&host->xfer_done);

      /* start the command first*/
    if(host->hw->host_function != MSDC_SDIO && !is_card_sdio(host)){
		host->autocmd = MSDC_AUTOCMD12;
    }
    
    if(3 == partition_access)
	{
		host->autocmd = 0;
	}

	

    if (msdc_command_start(host, cmd, 0, CMD_TIMEOUT) != 0)
    {
        goto done;
    }

    /* then wait command done */
    if (msdc_command_resp_polling(host, cmd, 0, CMD_TIMEOUT) != 0) 
	{ // not tuning.
        goto stop;
    }

    /* for read, the data coming too fast, then CRC error
       start DMA no business with CRC. */
    //init_completion(&host->xfer_done);
	
    msdc_dma_setup(host, &host->dma, data->sg, data->sg_len);

    msdc_dma_start(host);

    spin_unlock(&host->lock);
return 0;

stop:
    /* Last: stop transfer */
    if (data && data->stop){
	if(!((cmd->error == 0) && (data->error == 0) && (host->autocmd == MSDC_AUTOCMD12) && (cmd->opcode == MMC_READ_MULTIPLE_BLOCK || cmd->opcode == MMC_WRITE_MULTIPLE_BLOCK))){
        	if (msdc_do_command(host, data->stop, 0, CMD_TIMEOUT) != 0) {
            	goto done;
        	}
	}
    }


done:

	msdc_dma_clear(host);

    N_MSG(OPS, "CMD<%d> data<%s %s> blksz<%d> block<%d> error<%d>",cmd->opcode, (dma? "dma":"pio"),
            (read ? "read ":"write") ,data->blksz, data->blocks, data->error);

    if (!is_card_sdio(host)) {
        if ((cmd->opcode != 17)&&(cmd->opcode != 18)&&(cmd->opcode != 24)&&(cmd->opcode != 25)) {
            N_MSG(NRW, "CMD<%3d> arg<0x%8x> Resp<0x%8x> data<%s> size<%d>", cmd->opcode, cmd->arg, cmd->resp[0],
                (read ? "read ":"write") ,data->blksz * data->blocks);
        } else {
            N_MSG(RW,  "CMD<%3d> arg<0x%8x> Resp<0x%8x> block<%d>", cmd->opcode,
                cmd->arg, cmd->resp[0], data->blocks);
        }
    }

    if (mrq->cmd->error == (unsigned int)-EIO) host->error |= REQ_CMD_EIO;
	if (mrq->cmd->error == (unsigned int)-ETIMEDOUT) host->error |= REQ_CMD_TMO;
    if (mrq->stop && (mrq->stop->error == (unsigned int)-EIO)) host->error |= REQ_STOP_EIO;
	if (mrq->stop && (mrq->stop->error == (unsigned int)-ETIMEDOUT)) host->error |= REQ_STOP_TMO;
	if(mrq->done)
		mrq->done(mrq);

	spin_unlock(&host->lock);
    return host->error;
}

#ifdef CONFIG_MTKEMMC_BOOT
//need debug the interface for kernel panic
static unsigned int msdc_command_start_simple(struct msdc_host   *host,
                                      struct mmc_command *cmd,
                                      int                 tune,   /* not used */
                                      unsigned long       timeout)
{
    u32 base = host->base;
    u32 opcode = cmd->opcode;
    u32 rawcmd;

    u32 resp;
    unsigned long tmo;
    volatile u32 intsts;

    /* Protocol layer does not provide response type, but our hardware needs
     * to know exact type, not just size!
     */
    if (opcode == MMC_SEND_OP_COND || opcode == SD_APP_OP_COND)
        resp = RESP_R3;
    else if (opcode == MMC_SET_RELATIVE_ADDR || opcode == SD_SEND_RELATIVE_ADDR)
        resp = (mmc_cmd_type(cmd) == MMC_CMD_BCR) ? RESP_R6 : RESP_R1;
    else if (opcode == MMC_FAST_IO)
        resp = RESP_R4;
    else if (opcode == MMC_GO_IRQ_STATE)
        resp = RESP_R5;
    else if (opcode == MMC_SELECT_CARD)
        resp = (cmd->arg != 0) ? RESP_R1B : RESP_NONE;
    else if (opcode == SD_IO_RW_DIRECT || opcode == SD_IO_RW_EXTENDED)
        resp = RESP_R1; /* SDIO workaround. */
    else if (opcode == SD_SEND_IF_COND && (mmc_cmd_type(cmd) == MMC_CMD_BCR))
        resp = RESP_R1;
    else {
        switch (mmc_resp_type(cmd)) {
        case MMC_RSP_R1:
            resp = RESP_R1;
            break;
        case MMC_RSP_R1B:
            resp = RESP_R1B;
            break;
        case MMC_RSP_R2:
            resp = RESP_R2;
            break;
        case MMC_RSP_R3:
            resp = RESP_R3;
            break;
        case MMC_RSP_NONE:
        default:
            resp = RESP_NONE;
            break;
        }
    }

    cmd->error = 0;
    /* rawcmd :
     * vol_swt << 30 | auto_cmd << 28 | blklen << 16 | go_irq << 15 |
     * stop << 14 | rw << 13 | dtype << 11 | rsptyp << 7 | brk << 6 | opcode
     */
    rawcmd = opcode | msdc_rsp[resp] << 7 | host->blksz << 16;

    if (opcode == MMC_READ_MULTIPLE_BLOCK) {
        rawcmd |= (2 << 11);
    } else if (opcode == MMC_READ_SINGLE_BLOCK) {
        rawcmd |= (1 << 11);
    } else if (opcode == MMC_WRITE_MULTIPLE_BLOCK) {
        rawcmd |= ((2 << 11) | (1 << 13));
    } else if (opcode == MMC_WRITE_BLOCK) {
        rawcmd |= ((1 << 11) | (1 << 13));
    } else if (opcode == SD_IO_RW_EXTENDED) {
        if (cmd->data->flags & MMC_DATA_WRITE)
            rawcmd |= (1 << 13);
        if (cmd->data->blocks > 1)
            rawcmd |= (2 << 11);
        else
            rawcmd |= (1 << 11);
    } else if (opcode == SD_IO_RW_DIRECT && cmd->flags == (unsigned int)-1) {
        rawcmd |= (1 << 14);
    } else if (opcode == 11) { /* bug */
        rawcmd |= (1 << 30);
    } else if ((opcode == SD_APP_SEND_SCR) ||
        (opcode == SD_APP_SEND_NUM_WR_BLKS) ||
        (opcode == SD_SWITCH && (mmc_cmd_type(cmd) == MMC_CMD_ADTC)) ||
        (opcode == SD_APP_SD_STATUS && (mmc_cmd_type(cmd) == MMC_CMD_ADTC)) ||
        (opcode == MMC_SEND_EXT_CSD && (mmc_cmd_type(cmd) == MMC_CMD_ADTC))) {
        rawcmd |= (1 << 11);
    } else if (opcode == MMC_STOP_TRANSMISSION) {
        rawcmd |= (1 << 14);
        rawcmd &= ~(0x0FFF << 16);
    }

    //printk("CMD<%d><0x%.8x> Arg<0x%.8x>\n", opcode , rawcmd, cmd->arg);

    tmo = jiffies + timeout;
    if (opcode == MMC_SEND_STATUS) {
        for (;;) {
            if (!sdc_is_cmd_busy())
                break;

            if (time_after(jiffies, tmo)) {
                ERR_MSG("XXX cmd_busy timeout: before CMD<%d>", opcode);
                cmd->error = (unsigned int)-ETIMEDOUT;
                msdc_reset_hw(host->id);
                return cmd->error;
            }
        }
    }else {
        for (;;) {
            if (!sdc_is_busy() && !sdc_is_cmd_busy())
                break;
            if (time_after(jiffies, tmo)) {
                ERR_MSG("XXX sdc_busy timeout: before CMD<%d>", opcode);
                cmd->error = (unsigned int)-ETIMEDOUT;
                msdc_reset_hw(host->id);
                return cmd->error;
            }
        }
    }

    //BUG_ON(in_interrupt());
    host->cmd     = cmd;
    host->cmd_rsp = resp;

    intsts = 0x1f7fb;
    sdr_write32(MSDC_INT, intsts);  /* clear interrupts */

    sdc_send_cmd(rawcmd, cmd->arg);

    return 0;  // irq too fast, then cmd->error has value, and don't call msdc_command_resp, don't tune.
}

u32 msdc_intr_wait(struct msdc_host *host,  struct mmc_command *cmd, u32 intrs,unsigned long timeout)
{
    u32 base = host->base;
    u32 sts;
	unsigned long tmo;

    tmo = jiffies + timeout;
	while (1){
		if (((sts = sdr_read32(MSDC_INT)) & intrs) != 0){
            sdr_write32(MSDC_INT, (sts & intrs));
            break;
        }

		 if (time_after(jiffies, tmo)) {
                ERR_MSG("XXX wait cmd response timeout: before CMD<%d>", cmd->opcode);
                cmd->error = (unsigned int)-ETIMEDOUT;
                msdc_reset_hw(host->id);
                return cmd->error;
            }
		}

    /* command interrupts */
    if ((sts & MSDC_INT_CMDRDY) || (sts & MSDC_INT_ACMDRDY) ||
            (sts & MSDC_INT_ACMD19_DONE)) {
        u32 *rsp = &cmd->resp[0];

        switch (host->cmd_rsp) {
            case RESP_NONE:
                break;
            case RESP_R2:
                *rsp++ = sdr_read32(SDC_RESP3); *rsp++ = sdr_read32(SDC_RESP2);
                *rsp++ = sdr_read32(SDC_RESP1); *rsp++ = sdr_read32(SDC_RESP0);
                break;
            default: /* Response types 1, 3, 4, 5, 6, 7(1b) */
                if ((sts & MSDC_INT_ACMDRDY) || (sts & MSDC_INT_ACMD19_DONE)) {
                    *rsp = sdr_read32(SDC_ACMD_RESP);
                } else {
                    *rsp = sdr_read32(SDC_RESP0);
                }

                //msdc_dump_card_status(host, *rsp);
                break;
        }
    } else if ((sts & MSDC_INT_RSPCRCERR) || (sts & MSDC_INT_ACMDCRCERR)) {
        if(sts & MSDC_INT_ACMDCRCERR){
            printk("XXX CMD<%d> MSDC_INT_ACMDCRCERR",cmd->opcode);
        }
        else {
            printk("XXX CMD<%d> MSDC_INT_RSPCRCERR Arg<0x%x>",cmd->opcode, cmd->arg);
        }
        cmd->error = (unsigned int)-EIO;
        msdc_reset_hw(host->id);
    } else if ((sts & MSDC_INT_CMDTMO) || (sts & MSDC_INT_ACMDTMO)) {
        if(sts & MSDC_INT_ACMDTMO){
            printk("XXX CMD<%d> MSDC_INT_ACMDTMO",cmd->opcode);
        }
        else {
            printk("XXX CMD<%d> MSDC_INT_CMDTMO Arg<0x%x>",cmd->opcode, cmd->arg);
        }
        cmd->error = (unsigned int)-ETIMEDOUT;
        msdc_reset_hw(host->id);
    }

    N_MSG(INT, "[SD%d] INT(0x%x)\n", host->id, sts);
    if (~intrs & sts) {
        N_MSG(WRN, "[SD%d]<CHECKME> Unexpected INT(0x%x)\n",
            host->id, ~intrs & sts);
    }
    return sts;
}

static unsigned int msdc_command_resp_simple(struct msdc_host   *host,
                                      struct mmc_command *cmd,
                                      int                 tune,
                                      unsigned long       timeout)
{
    //u32 base = host->base;
    u32 resp;
    u32 status;
    u32 wints = MSDC_INT_CMDRDY  | MSDC_INT_RSPCRCERR  | MSDC_INT_CMDTMO  |
                MSDC_INT_ACMDRDY | MSDC_INT_ACMDCRCERR | MSDC_INT_ACMDTMO |
                MSDC_INT_ACMD19_DONE;

    resp = host->cmd_rsp;

    /*polling*/
    status = msdc_intr_wait(host, cmd, wints, timeout);

    host->cmd = NULL;

    return cmd->error;
}

static unsigned int msdc_do_command_simple(struct msdc_host   *host,
                                      struct mmc_command *cmd,
                                      int                 tune,
                                      unsigned long       timeout)
{
    if (msdc_command_start_simple(host, cmd, tune, timeout))
        goto end;

    if (msdc_command_resp_simple(host, cmd, tune, timeout))
        goto end;

end:

    N_MSG(CMD, "        return<%d> resp<0x%.8x>", cmd->error, cmd->resp[0]);
    return cmd->error;
}

int simple_sd_request(struct mmc_host*mmc, struct mmc_request* mrq)
{
    struct msdc_host *host = mmc_priv(mmc);
    struct mmc_command *cmd;
    struct mmc_data *data;
    u32 base = host->base;
    unsigned int left=0;
    int dma = 0, read = 1, send_type=0;
    volatile u32 intsts;
    volatile u32 l_sts;


    #define SND_DAT 0
    #define SND_CMD 1

    BUG_ON(mmc == NULL);
    BUG_ON(mrq == NULL);

    host->error = 0;

    cmd  = mrq->cmd;
    data = mrq->cmd->data;


    if (!data) {
        host->blksz = 0;
        send_type=SND_CMD;
        if (msdc_do_command_simple(host, cmd, 1, CMD_TIMEOUT) != 0) {
            if(cmd->opcode == MMC_STOP_TRANSMISSION){
                msdc_dma_stop(host);
                msdc_reset_hw(host->id);
                msdc_clr_fifo(host->id);
                l_sts = sdr_read32(MSDC_CFG);
                if ((l_sts & 0x8)==0){
                    l_sts |= 0x8;
                    sdr_write32(MSDC_CFG, l_sts);
                }
                //msdc_dump_register(host);
            }
            goto done;
        }

        if(cmd->opcode == MMC_STOP_TRANSMISSION){
            msdc_dma_stop(host);
            msdc_reset_hw(host->id);
            msdc_clr_fifo(host->id);
            l_sts = sdr_read32(MSDC_CFG);
            if ((l_sts & 0x8)==0){
                l_sts |= 0x8;
                sdr_write32(MSDC_CFG, l_sts);
            }

            //msdc_dump_register(host);
        }
    } else {
        BUG_ON(data->blksz > HOST_MAX_BLKSZ);
        send_type=SND_DAT;

        data->error = 0;
        read = data->flags & MMC_DATA_READ ? 1 : 0;
        msdc_latest_operation_type[host->id] = read ? OPER_TYPE_READ : OPER_TYPE_WRITE;
        host->data = data;
        host->xfer_size = data->blocks * data->blksz;
        host->blksz = data->blksz;

        host->dma_xfer = dma = 0;
        msdc_latest_transfer_mode[host->id] = TRAN_MOD_PIO;

        if (read) {
            if ((host->timeout_ns != data->timeout_ns) ||
                (host->timeout_clks != data->timeout_clks)) {
                msdc_set_timeout(host, data->timeout_ns, data->timeout_clks);
            }
        }

        msdc_set_blknum(host, data->blocks);
        if(host->hw->host_function == MSDC_EMMC &&
		   host->hw->boot == MSDC_BOOT_EN &&
        (cmd->opcode == MMC_READ_SINGLE_BLOCK || cmd->opcode == MMC_READ_MULTIPLE_BLOCK || cmd->opcode == MMC_WRITE_BLOCK || cmd->opcode == MMC_WRITE_MULTIPLE_BLOCK))
        {
            cmd->arg  += offset;
        }
        /* Firstly: send command */
        if (msdc_do_command_simple(host, cmd, 1, CMD_TIMEOUT) != 0) {
            goto done;
        }

        /* Secondly: pio data phase */
        l_sts = sdr_read32(MSDC_CFG);
        l_sts = 0;
        sdr_clr_bits(MSDC_INTEN, MSDC_INT_XFER_COMPL);
        if (msdc_pio_write(host, data)) {
            goto done;
        }

        /* For write case: make sure contents in fifo flushed to device */
        if (!read) {
            while (1) {
                left=msdc_txfifocnt();
                if (left == 0) {
                    break;
                }
                if (msdc_pio_abort(host, data, jiffies + DAT_TIMEOUT)) {
                    break;
                    /* Fix me: what about if data error, when stop ? how to? */
                }
            }
        } else {
            /* Fix me: read case: need to check CRC error */
        }

        /* polling for write */
        intsts = 0;
        //msdc_dump_register(host);
        while ((intsts & MSDC_INT_XFER_COMPL) == 0 ){
            intsts = sdr_read32(MSDC_INT);
        }
        sdr_set_bits(MSDC_INT, MSDC_INT_XFER_COMPL);
    }

done:
#ifdef CONFIG_MTKEMMC_BOOT
    if(host->hw->host_function == MSDC_EMMC &&
		host->hw->boot == MSDC_BOOT_EN &&
       (cmd->opcode == MMC_READ_SINGLE_BLOCK || cmd->opcode == MMC_READ_MULTIPLE_BLOCK || cmd->opcode == MMC_WRITE_BLOCK || cmd->opcode == MMC_WRITE_MULTIPLE_BLOCK))
    {
        cmd->arg  -= offset;
    }
#endif
    if (data != NULL) {
        host->data = NULL;
        host->dma_xfer = 0;
        host->blksz = 0;

        N_MSG(OPS, "CMD<%d> data<%s %s> blksz<%d> block<%d> error<%d>",cmd->opcode, (dma? "dma":"pio"),
                (read ? "read ":"write") ,data->blksz, data->blocks, data->error);

    }

    if (mrq->cmd->error) host->error = 0x001;
    if (mrq->data && mrq->data->error) host->error |= 0x010;
    if (mrq->stop && mrq->stop->error) host->error |= 0x100;


    return host->error;
}
#endif

static int msdc_app_cmd(struct mmc_host *mmc, struct msdc_host *host)
{
    struct mmc_command cmd = {0};
    struct mmc_request mrq = {0};
    u32 err = -1;

    cmd.opcode = MMC_APP_CMD;
    cmd.arg = host->app_cmd_arg;  /* meet mmc->card is null when ACMD6 */
    cmd.flags = MMC_RSP_SPI_R1 | MMC_RSP_R1 | MMC_CMD_AC;

    mrq.cmd = &cmd; cmd.mrq = &mrq;
    cmd.data = NULL;

    err = msdc_do_command(host, &cmd, 0, CMD_TIMEOUT);
    return err;
}

static int msdc_lower_freq(struct msdc_host *host) //to check
{
    u32 div, mode;
    u32 base = host->base;

    ERR_MSG("need to lower freq");
	msdc_reset_tune_counter(host,all_counter);
    sdr_get_field(MSDC_CFG, MSDC_CFG_CKMOD, mode);
    sdr_get_field(MSDC_CFG, MSDC_CFG_CKDIV, div);

    if (div >= MSDC_MAX_FREQ_DIV) {
        ERR_MSG("but, div<%d> power tuning", div); // to check why
        return msdc_power_tuning(host);
    } else if(mode == 1){
        mode = 0;
        msdc_clk_stable(host,mode, div);
        host->sclk = (div == 0) ? msdcClk[IDX_CLK_TARGET][host->hw->clk_src] /2 : msdcClk[IDX_CLK_TARGET][host->hw->clk_src] /(4*div);

        ERR_MSG("new div<%d>, mode<%d> new freq.<%dKHz>", div, mode,host->sclk/1000);
        return 0;
    }
    else{
        msdc_clk_stable(host,mode, div + 1);
        host->sclk = (mode == 2) ? msdcClk[IDX_CLK_TARGET][host->hw->clk_src]/(2*4*(div+1)) : msdcClk[IDX_CLK_TARGET][host->hw->clk_src]/(4*(div+1));
        ERR_MSG("new div<%d>, mode<%d> new freq.<%dKHz>", div + 1, mode,host->sclk/1000);
        return 0;
    }
}

static int msdc_tune_cmdrsp(struct msdc_host *host)
{
    int result = 0;
    u32 base = host->base;
    u32 sel = 0;
    u32 cur_rsmpl = 0, orig_rsmpl;
    u32 cur_rrdly = 0, orig_rrdly;
    u32 cur_cntr  = 0,orig_cmdrtc;
    u32 cur_dl_cksel = 0, orig_dl_cksel;

    sdr_get_field(MSDC_IOCON, MSDC_IOCON_RSPL, orig_rsmpl);
    sdr_get_field(MSDC_PAD_TUNE, MSDC_PAD_TUNE_CMDRDLY, orig_rrdly);
    sdr_get_field(MSDC_PATCH_BIT1, MSDC_PATCH_BIT1_CMD_RSP, orig_cmdrtc);
    sdr_get_field(MSDC_PATCH_BIT0, MSDC_INT_DAT_LATCH_CK_SEL, orig_dl_cksel);

#if 1
    if (host->mclk >= 100000000){
        sel = 1;
        //sdr_set_field(MSDC_PATCH_BIT0, MSDC_CKGEN_RX_SDCLKO_SEL,0);
    }
    else {
        sdr_set_field(MSDC_PATCH_BIT1,MSDC_PATCH_BIT1_CMD_RSP,1);//to check cmd resp turn around
        //sdr_set_field(MSDC_PATCH_BIT0, MSDC_CKGEN_RX_SDCLKO_SEL,1);
        sdr_set_field(MSDC_PATCH_BIT0, MSDC_INT_DAT_LATCH_CK_SEL,0);
    }

    cur_rsmpl = (orig_rsmpl + 1);
    sdr_set_field(MSDC_IOCON, MSDC_IOCON_RSPL, cur_rsmpl % 2);
	if (host->mclk <= 400000){//In sd/emmc init flow, fix rising edge for latching cmd response
			sdr_set_field(MSDC_IOCON, MSDC_IOCON_RSPL, 0);
			cur_rsmpl = 2;
		}
    if(cur_rsmpl >= 2){
        cur_rrdly = (orig_rrdly + 1);
        sdr_set_field(MSDC_PAD_TUNE, MSDC_PAD_TUNE_CMDRDLY, cur_rrdly % 32);
    }
    if(cur_rrdly >= 32){
        if(sel){
            cur_cntr = (orig_cmdrtc + 1) ;
            sdr_set_field(MSDC_PATCH_BIT1, MSDC_PATCH_BIT1_CMD_RSP, cur_cntr % 8);
        }
    }
    if(cur_cntr >= 8){
        if(sel){
            cur_dl_cksel = (orig_dl_cksel +1);
            sdr_set_field(MSDC_PATCH_BIT0, MSDC_INT_DAT_LATCH_CK_SEL, cur_dl_cksel % 8);
        }
    }
    ++(host->t_counter.time_cmd);
    if((sel && host->t_counter.time_cmd == CMD_TUNE_UHS_MAX_TIME)||(sel == 0 && host->t_counter.time_cmd == CMD_TUNE_HS_MAX_TIME)){
#ifdef MSDC_LOWER_FREQ
        result = msdc_lower_freq(host);
#else
        result = 1;
#endif
        host->t_counter.time_cmd = 0;
    }
#else
    if (orig_rsmpl == 0) {
        cur_rsmpl = 1;
        sdr_set_field(MSDC_IOCON, MSDC_IOCON_RSPL, cur_rsmpl);
    } else {
        cur_rsmpl = 0;
        sdr_set_field(MSDC_IOCON, MSDC_IOCON_RSPL, cur_rsmpl);  // need second layer
        cur_rrdly = (orig_rrdly + 1);
        if (cur_rrdly >= 32) {
            ERR_MSG("failed to update rrdly<%d>", cur_rrdly);
            sdr_set_field(MSDC_PAD_TUNE, MSDC_PAD_TUNE_CMDRDLY, 0);
        #ifdef MSDC_LOWER_FREQ
            return (msdc_lower_freq(host));
        #else
            return 1;
        #endif
        }
        sdr_set_field(MSDC_PAD_TUNE, MSDC_PAD_TUNE_CMDRDLY, cur_rrdly);
    }

#endif
    sdr_get_field(MSDC_IOCON, MSDC_IOCON_RSPL, orig_rsmpl);
    sdr_get_field(MSDC_PAD_TUNE, MSDC_PAD_TUNE_CMDRDLY, orig_rrdly);
    sdr_get_field(MSDC_PATCH_BIT1, MSDC_PATCH_BIT1_CMD_RSP, orig_cmdrtc);
    sdr_get_field(MSDC_PATCH_BIT0, MSDC_INT_DAT_LATCH_CK_SEL, orig_dl_cksel);
    INIT_MSG("TUNE_CMD: rsmpl<%d> rrdly<%d> cmdrtc<%d> dl_cksel<%d> sfreq.<%d>", orig_rsmpl, orig_rrdly,orig_cmdrtc,orig_dl_cksel,host->sclk);
    host->rwcmd_time_tune++; 
    return result;
}

static int msdc_tune_read(struct msdc_host *host)
{
    u32 base = host->base;
    u32 sel = 0;
    u32 ddr = 0;
    u32 dcrc;
    u32 clkmode = 0;
    u32 cur_rxdly0, cur_rxdly1;
    u32 cur_dsmpl = 0, orig_dsmpl;
    u32 cur_dsel = 0,orig_dsel;
    u32 cur_dl_cksel = 0,orig_dl_cksel;
    u32 cur_dat0 = 0, cur_dat1 = 0, cur_dat2 = 0, cur_dat3 = 0,
    cur_dat4 = 0, cur_dat5 = 0, cur_dat6 = 0, cur_dat7 = 0;
    u32 orig_dat0, orig_dat1, orig_dat2, orig_dat3, orig_dat4, orig_dat5, orig_dat6, orig_dat7;
    int result = 0;
    
    	if(3 == partition_access)
	{
		return 1;
	}

	#if 1
    if (host->mclk >= 100000000){
        sel = 1;}
    else{
        sdr_set_field(MSDC_PATCH_BIT0, MSDC_CKGEN_MSDC_DLY_SEL, 0);
    }
    sdr_get_field(MSDC_CFG,MSDC_CFG_CKMOD,clkmode);
    ddr = (clkmode == 2) ? 1 : 0;

    sdr_get_field(MSDC_PATCH_BIT0, MSDC_CKGEN_MSDC_DLY_SEL, orig_dsel);
    sdr_get_field(MSDC_PATCH_BIT0, MSDC_INT_DAT_LATCH_CK_SEL, orig_dl_cksel);
    sdr_get_field(MSDC_IOCON, MSDC_IOCON_DSPL, orig_dsmpl);

    sdr_set_field(MSDC_IOCON, MSDC_IOCON_DDLSEL, 1);
    cur_dsmpl = (orig_dsmpl + 1) ;
    sdr_set_field(MSDC_IOCON, MSDC_IOCON_DSPL, cur_dsmpl % 2);

    if(cur_dsmpl >= 2){
        sdr_get_field(SDC_DCRC_STS, SDC_DCRC_STS_POS | SDC_DCRC_STS_NEG, dcrc);
        if (!ddr) dcrc &= ~SDC_DCRC_STS_NEG;
        cur_rxdly0 = sdr_read32(MSDC_DAT_RDDLY0);
        cur_rxdly1 = sdr_read32(MSDC_DAT_RDDLY1);

            orig_dat0 = (cur_rxdly0 >> 24) & 0x1F;
            orig_dat1 = (cur_rxdly0 >> 16) & 0x1F;
            orig_dat2 = (cur_rxdly0 >>  8) & 0x1F;
            orig_dat3 = (cur_rxdly0 >>  0) & 0x1F;
            orig_dat4 = (cur_rxdly1 >> 24) & 0x1F;
            orig_dat5 = (cur_rxdly1 >> 16) & 0x1F;
            orig_dat6 = (cur_rxdly1 >>  8) & 0x1F;
            orig_dat7 = (cur_rxdly1 >>  0) & 0x1F;

        if (ddr) {
            cur_dat0 = (dcrc & (1 << 0) || dcrc & (1 <<  8)) ? (orig_dat0 + 1) : orig_dat0;
            cur_dat1 = (dcrc & (1 << 1) || dcrc & (1 <<  9)) ? (orig_dat1 + 1) : orig_dat1;
            cur_dat2 = (dcrc & (1 << 2) || dcrc & (1 << 10)) ? (orig_dat2 + 1) : orig_dat2;
            cur_dat3 = (dcrc & (1 << 3) || dcrc & (1 << 11)) ? (orig_dat3 + 1) : orig_dat3;
			cur_dat4 = (dcrc & (1 << 4) || dcrc & (1 << 12)) ? (orig_dat4 + 1) : orig_dat4;
            cur_dat5 = (dcrc & (1 << 5) || dcrc & (1 << 13)) ? (orig_dat5 + 1) : orig_dat5;
            cur_dat6 = (dcrc & (1 << 6) || dcrc & (1 << 14)) ? (orig_dat6 + 1) : orig_dat6;
            cur_dat7 = (dcrc & (1 << 7) || dcrc & (1 << 15)) ? (orig_dat7 + 1) : orig_dat7;
        } else {
            cur_dat0 = (dcrc & (1 << 0)) ? (orig_dat0 + 1) : orig_dat0;
            cur_dat1 = (dcrc & (1 << 1)) ? (orig_dat1 + 1) : orig_dat1;
            cur_dat2 = (dcrc & (1 << 2)) ? (orig_dat2 + 1) : orig_dat2;
            cur_dat3 = (dcrc & (1 << 3)) ? (orig_dat3 + 1) : orig_dat3;
			cur_dat4 = (dcrc & (1 << 4)) ? (orig_dat4 + 1) : orig_dat4;
        	cur_dat5 = (dcrc & (1 << 5)) ? (orig_dat5 + 1) : orig_dat5;
        	cur_dat6 = (dcrc & (1 << 6)) ? (orig_dat6 + 1) : orig_dat6;
        	cur_dat7 = (dcrc & (1 << 7)) ? (orig_dat7 + 1) : orig_dat7;
        }


            cur_rxdly0 = ((cur_dat0 & 0x1F) << 24) | ((cur_dat1 & 0x1F) << 16) |
            	         ((cur_dat2 & 0x1F) << 8)  | ((cur_dat3 & 0x1F) << 0);
            cur_rxdly1 = ((cur_dat4 & 0x1F) << 24) | ((cur_dat5 & 0x1F) << 16) |
            	         ((cur_dat6 & 0x1F) << 8)  | ((cur_dat7 & 0x1F) << 0);


        sdr_write32(MSDC_DAT_RDDLY0, cur_rxdly0);
        sdr_write32(MSDC_DAT_RDDLY1, cur_rxdly1);

		}
    if((cur_dat0 >= 32) || (cur_dat1 >= 32) || (cur_dat2 >= 32) || (cur_dat3 >= 32)||
        (cur_dat4 >= 32) || (cur_dat5 >= 32) || (cur_dat6 >= 32) || (cur_dat7 >= 32)){
        if(sel){
			sdr_write32(MSDC_DAT_RDDLY0, 0);
	        sdr_write32(MSDC_DAT_RDDLY1, 0); 
            cur_dsel = (orig_dsel + 1) ;
            sdr_set_field(MSDC_PATCH_BIT0, MSDC_CKGEN_MSDC_DLY_SEL, cur_dsel % 32);
        }
    }
    if(cur_dsel >= 32 ){
        if(clkmode == 1 && sel){
            cur_dl_cksel = (orig_dl_cksel + 1);
            sdr_set_field(MSDC_PATCH_BIT0, MSDC_INT_DAT_LATCH_CK_SEL, cur_dl_cksel % 8);
        }
    }
    ++(host->t_counter.time_read);
    if((sel == 1 && clkmode == 1 && host->t_counter.time_read == READ_TUNE_UHS_CLKMOD1_MAX_TIME)||
        (sel == 1 && (clkmode == 0 ||clkmode == 2) && host->t_counter.time_read == READ_TUNE_UHS_MAX_TIME)||
        (sel == 0 && (clkmode == 0 ||clkmode == 2) && host->t_counter.time_read == READ_TUNE_HS_MAX_TIME)){
#ifdef MSDC_LOWER_FREQ
        result = msdc_lower_freq(host);
#else
        result = 1;
#endif
        host->t_counter.time_read = 0;
    }
#else
    sdr_set_field(MSDC_IOCON, MSDC_IOCON_DDLSEL, 1);

    cur_rxdly0 = sdr_read32(MSDC_DAT_RDDLY0);
    cur_rxdly1 = sdr_read32(MSDC_DAT_RDDLY1);
    sdr_get_field(MSDC_IOCON, MSDC_IOCON_DSPL, orig_dsmpl);
    if (orig_dsmpl == 0) {
        cur_dsmpl = 1;
        sdr_set_field(MSDC_IOCON, MSDC_IOCON_DSPL, cur_dsmpl);
    } else {
        cur_dsmpl = 0;
        sdr_set_field(MSDC_IOCON, MSDC_IOCON_DSPL, cur_dsmpl); // need second layer

        sdr_get_field(SDC_DCRC_STS, SDC_DCRC_STS_POS | SDC_DCRC_STS_NEG, dcrc);
        if (!ddr) dcrc &= ~SDC_DCRC_STS_NEG;

        if (sdr_read32(MSDC_ECO_VER) >= 4) {
            orig_dat0 = (cur_rxdly0 >> 24) & 0x1F;
            orig_dat1 = (cur_rxdly0 >> 16) & 0x1F;
            orig_dat2 = (cur_rxdly0 >>  8) & 0x1F;
            orig_dat3 = (cur_rxdly0 >>  0) & 0x1F;
            orig_dat4 = (cur_rxdly1 >> 24) & 0x1F;
            orig_dat5 = (cur_rxdly1 >> 16) & 0x1F;
            orig_dat6 = (cur_rxdly1 >>  8) & 0x1F;
            orig_dat7 = (cur_rxdly1 >>  0) & 0x1F;
        } else {
            orig_dat0 = (cur_rxdly0 >>  0) & 0x1F;
            orig_dat1 = (cur_rxdly0 >>  8) & 0x1F;
            orig_dat2 = (cur_rxdly0 >> 16) & 0x1F;
            orig_dat3 = (cur_rxdly0 >> 24) & 0x1F;
            orig_dat4 = (cur_rxdly1 >>  0) & 0x1F;
            orig_dat5 = (cur_rxdly1 >>  8) & 0x1F;
            orig_dat6 = (cur_rxdly1 >> 16) & 0x1F;
            orig_dat7 = (cur_rxdly1 >> 24) & 0x1F;
        }

        if (ddr) {
            cur_dat0 = (dcrc & (1 << 0) || dcrc & (1 << 8))  ? (orig_dat0 + 1) : orig_dat0;
            cur_dat1 = (dcrc & (1 << 1) || dcrc & (1 << 9))  ? (orig_dat1 + 1) : orig_dat1;
            cur_dat2 = (dcrc & (1 << 2) || dcrc & (1 << 10)) ? (orig_dat2 + 1) : orig_dat2;
            cur_dat3 = (dcrc & (1 << 3) || dcrc & (1 << 11)) ? (orig_dat3 + 1) : orig_dat3;
        } else {
            cur_dat0 = (dcrc & (1 << 0)) ? (orig_dat0 + 1) : orig_dat0;
            cur_dat1 = (dcrc & (1 << 1)) ? (orig_dat1 + 1) : orig_dat1;
            cur_dat2 = (dcrc & (1 << 2)) ? (orig_dat2 + 1) : orig_dat2;
            cur_dat3 = (dcrc & (1 << 3)) ? (orig_dat3 + 1) : orig_dat3;
        }
        cur_dat4 = (dcrc & (1 << 4)) ? (orig_dat4 + 1) : orig_dat4;
        cur_dat5 = (dcrc & (1 << 5)) ? (orig_dat5 + 1) : orig_dat5;
        cur_dat6 = (dcrc & (1 << 6)) ? (orig_dat6 + 1) : orig_dat6;
        cur_dat7 = (dcrc & (1 << 7)) ? (orig_dat7 + 1) : orig_dat7;

        if (cur_dat0 >= 32 || cur_dat1 >= 32 || cur_dat2 >= 32 || cur_dat3 >= 32) {
            ERR_MSG("failed to update <%xh><%xh><%xh><%xh>", cur_dat0, cur_dat1, cur_dat2, cur_dat3);
            sdr_write32(MSDC_DAT_RDDLY0, 0);
            sdr_write32(MSDC_DAT_RDDLY1, 0);

#ifdef MSDC_LOWER_FREQ
            return (msdc_lower_freq(host));
#else
            return 1;
#endif
        }

        if (cur_dat4 >= 32 || cur_dat5 >= 32 || cur_dat6 >= 32 || cur_dat7 >= 32) {
            ERR_MSG("failed to update <%xh><%xh><%xh><%xh>", cur_dat4, cur_dat5, cur_dat6, cur_dat7);
            sdr_write32(MSDC_DAT_RDDLY0, 0);
            sdr_write32(MSDC_DAT_RDDLY1, 0);

#ifdef MSDC_LOWER_FREQ
            return (msdc_lower_freq(host));
#else
            return 1;
#endif
        }

        cur_rxdly0 = (cur_dat0 << 24) | (cur_dat1 << 16) | (cur_dat2 << 8) | (cur_dat3 << 0);
        cur_rxdly1 = (cur_dat4 << 24) | (cur_dat5 << 16) | (cur_dat6 << 8) | (cur_dat7 << 0);

        sdr_write32(MSDC_DAT_RDDLY0, cur_rxdly0);
        sdr_write32(MSDC_DAT_RDDLY1, cur_rxdly1);
    }

#endif
    sdr_get_field(MSDC_PATCH_BIT0, MSDC_CKGEN_MSDC_DLY_SEL, orig_dsel);
    sdr_get_field(MSDC_PATCH_BIT0, MSDC_INT_DAT_LATCH_CK_SEL, orig_dl_cksel);
    sdr_get_field(MSDC_IOCON, MSDC_IOCON_DSPL, orig_dsmpl);
    cur_rxdly0 = sdr_read32(MSDC_DAT_RDDLY0);
    cur_rxdly1 = sdr_read32(MSDC_DAT_RDDLY1);
    host->read_time_tune++; 
    INIT_MSG("TUNE_READ: dsmpl<%d> rxdly0<0x%x> rxdly1<0x%x> dsel<%d> dl_cksel<%d> sfreq.<%d>", orig_dsmpl, cur_rxdly0, cur_rxdly1,orig_dsel,orig_dl_cksel,host->sclk);

    return result;
}

static int msdc_tune_write(struct msdc_host *host)
{
    u32 base = host->base;

    //u32 cur_wrrdly = 0, orig_wrrdly;
    u32 cur_dsmpl = 0,  orig_dsmpl;
    u32 cur_rxdly0 = 0;
    u32 orig_dat0, orig_dat1, orig_dat2, orig_dat3;
    u32 cur_dat0 = 0,cur_dat1 = 0,cur_dat2 = 0,cur_dat3 = 0;
    u32 cur_d_cntr = 0 , orig_d_cntr;
    int result = 0;
    if(3 == partition_access)
	{
		return 1;
	}

    int sel = 0;
    int clkmode = 0;
    // MSDC_IOCON_DDR50CKD need to check. [Fix me]
#if 1
    if (host->mclk >= 100000000)
        sel = 1;
    else {
        sdr_set_field(MSDC_PATCH_BIT1,MSDC_PATCH_BIT1_WRDAT_CRCS,1);
    }
    sdr_get_field(MSDC_CFG,MSDC_CFG_CKMOD,clkmode);

    //sdr_get_field(MSDC_PAD_TUNE, MSDC_PAD_TUNE_DATWRDLY, orig_wrrdly);
    sdr_get_field(MSDC_IOCON, MSDC_IOCON_W_DSPL, orig_dsmpl);
    sdr_get_field(MSDC_PATCH_BIT1, MSDC_PATCH_BIT1_WRDAT_CRCS, orig_d_cntr);

    sdr_set_field(MSDC_IOCON, MSDC_IOCON_DDLSEL, 1);
    cur_dsmpl = (orig_dsmpl + 1);
    sdr_set_field(MSDC_IOCON, MSDC_IOCON_W_DSPL, cur_dsmpl % 2);
	#if 0
    if(cur_dsmpl >= 2){
        cur_wrrdly = (orig_wrrdly+1);
        sdr_set_field(MSDC_PAD_TUNE, MSDC_PAD_TUNE_DATWRDLY, cur_wrrdly % 32);
    }
	#endif
    if(cur_dsmpl >= 2){
        cur_rxdly0 = sdr_read32(MSDC_DAT_RDDLY0);

        	orig_dat0 = (cur_rxdly0 >> 24) & 0x1F;
       	 	orig_dat1 = (cur_rxdly0 >> 16) & 0x1F;
       	 	orig_dat2 = (cur_rxdly0 >>  8) & 0x1F;
        	orig_dat3 = (cur_rxdly0 >>  0) & 0x1F;

        cur_dat0 = (orig_dat0 + 1); /* only adjust bit-1 for crc */
        cur_dat1 = orig_dat1;
        cur_dat2 = orig_dat2;
        cur_dat3 = orig_dat3;

            cur_rxdly0 = ((cur_dat0 & 0x1F) << 24) | ((cur_dat1 & 0x1F) << 16) |
               	       ((cur_dat2 & 0x1F) << 8) | ((cur_dat3 & 0x1F) << 0);


        sdr_write32(MSDC_DAT_RDDLY0, cur_rxdly0);
    }
    if(cur_dat0 >= 32){
        if(sel){
            cur_d_cntr= (orig_d_cntr + 1 );
            sdr_set_field(MSDC_PATCH_BIT1, MSDC_PATCH_BIT1_WRDAT_CRCS, cur_d_cntr % 8);
        }
    }
    ++(host->t_counter.time_write);
    if((sel == 0 && host->t_counter.time_write == WRITE_TUNE_HS_MAX_TIME) || (sel && host->t_counter.time_write == WRITE_TUNE_UHS_MAX_TIME)){
#ifdef MSDC_LOWER_FREQ
        result = msdc_lower_freq(host);
#else
        result = 1;
#endif
        host->t_counter.time_write = 0;
    }

#else

    /* Tune Method 2. just DAT0 */
    sdr_set_field(MSDC_IOCON, MSDC_IOCON_DDLSEL, 1);
    cur_rxdly0 = sdr_read32(MSDC_DAT_RDDLY0);
    if (sdr_read32(MSDC_ECO_VER) >= 4) {
        orig_dat0 = (cur_rxdly0 >> 24) & 0x1F;
        orig_dat1 = (cur_rxdly0 >> 16) & 0x1F;
        orig_dat2 = (cur_rxdly0 >>  8) & 0x1F;
        orig_dat3 = (cur_rxdly0 >>  0) & 0x1F;
    } else {
        orig_dat0 = (cur_rxdly0 >>  0) & 0x1F;
        orig_dat1 = (cur_rxdly0 >>  8) & 0x1F;
        orig_dat2 = (cur_rxdly0 >> 16) & 0x1F;
        orig_dat3 = (cur_rxdly0 >> 24) & 0x1F;
    }

    sdr_get_field(MSDC_PAD_TUNE, MSDC_PAD_TUNE_DATWRDLY, orig_wrrdly);
    cur_wrrdly = orig_wrrdly;
    sdr_get_field(MSDC_IOCON,    MSDC_IOCON_W_DSPL,        orig_dsmpl );
    if (orig_dsmpl == 0) {
        cur_dsmpl = 1;
        sdr_set_field(MSDC_IOCON, MSDC_IOCON_W_DSPL, cur_dsmpl);
    } else {
        cur_dsmpl = 0;
        sdr_set_field(MSDC_IOCON, MSDC_IOCON_W_DSPL, cur_dsmpl);  // need the second layer

        cur_wrrdly = (orig_wrrdly + 1);
        if (cur_wrrdly < 32) {
            sdr_set_field(MSDC_PAD_TUNE, MSDC_PAD_TUNE_DATWRDLY, cur_wrrdly);
        } else {
            cur_wrrdly = 0;
            sdr_set_field(MSDC_PAD_TUNE, MSDC_PAD_TUNE_DATWRDLY, cur_wrrdly);  // need third

            cur_dat0 = orig_dat0 + 1; /* only adjust bit-1 for crc */
            cur_dat1 = orig_dat1;
            cur_dat2 = orig_dat2;
            cur_dat3 = orig_dat3;

            if (cur_dat0 >= 32) {
                ERR_MSG("update failed <%xh>", cur_dat0);
                sdr_write32(MSDC_DAT_RDDLY0, 0);

                #ifdef MSDC_LOWER_FREQ
                    return (msdc_lower_freq(host));
                #else
                    return 1;
                #endif
            }

            cur_rxdly0 = (cur_dat0 << 24) | (cur_dat1 << 16) | (cur_dat2 << 8) | (cur_dat3 << 0);
            sdr_write32(MSDC_DAT_RDDLY0, cur_rxdly0);
        }

    }

#endif
    //sdr_get_field(MSDC_PAD_TUNE, MSDC_PAD_TUNE_DATWRDLY, orig_wrrdly);
    sdr_get_field(MSDC_IOCON, MSDC_IOCON_W_DSPL, orig_dsmpl);
    sdr_get_field(MSDC_PATCH_BIT1, MSDC_PATCH_BIT1_WRDAT_CRCS, orig_d_cntr);
    cur_rxdly0 = sdr_read32(MSDC_DAT_RDDLY0);
    host->write_time_tune++; 
    INIT_MSG("TUNE_WRITE: dsmpl<%d> rxdly0<0x%x> d_cntr<%d> sfreq.<%d>", orig_dsmpl,cur_rxdly0,orig_d_cntr,host->sclk);

    return result;
}

static int msdc_get_card_status(struct mmc_host *mmc, struct msdc_host *host, u32 *status)
{
    struct mmc_command cmd;
    struct mmc_request mrq;
    u32 err;

    memset(&cmd, 0, sizeof(struct mmc_command));
    cmd.opcode = MMC_SEND_STATUS;    // CMD13
    cmd.arg = host->app_cmd_arg;
    cmd.flags = MMC_RSP_SPI_R2 | MMC_RSP_R1 | MMC_CMD_AC;

    memset(&mrq, 0, sizeof(struct mmc_request));
    mrq.cmd = &cmd; cmd.mrq = &mrq;
    cmd.data = NULL;

    err = msdc_do_command(host, &cmd, 0, CMD_TIMEOUT);  // tune until CMD13 pass.

    if (status) {
        *status = cmd.resp[0];
    }

    return err;
}

//#define TUNE_FLOW_TEST
#ifdef TUNE_FLOW_TEST
static void msdc_reset_para(struct msdc_host *host)
{
    u32 base = host->base;
    u32 dsmpl, rsmpl;

    // because we have a card, which must work at dsmpl<0> and rsmpl<0>

    sdr_get_field(MSDC_IOCON, MSDC_IOCON_DSPL, dsmpl);
    sdr_get_field(MSDC_IOCON, MSDC_IOCON_RSPL, rsmpl);

    if (dsmpl == 0) {
        sdr_set_field(MSDC_IOCON, MSDC_IOCON_DSPL, 1);
        ERR_MSG("set dspl<0>");
        sdr_set_field(MSDC_PAD_TUNE, MSDC_PAD_TUNE_CMDRDLY, 0);
    }

    if (rsmpl == 0) {
        sdr_set_field(MSDC_IOCON, MSDC_IOCON_RSPL, 1);
        ERR_MSG("set rspl<0>");
        sdr_write32(MSDC_DAT_RDDLY0, 0);
        sdr_set_field(MSDC_PAD_TUNE, MSDC_PAD_TUNE_DATWRDLY, 0);
    }
}
#endif

static void msdc_dump_trans_error(struct msdc_host   *host,
                                  struct mmc_command *cmd,
                                  struct mmc_data    *data,
                                  struct mmc_command *stop)
{
    if ((cmd->opcode == 52) && (cmd->arg == 0xc00)) return;
    if ((cmd->opcode == 52) && (cmd->arg == 0x80000c08)) return;

    if (!is_card_sdio(host)) { // by pass the SDIO CMD TO for SD/eMMC
        if ((host->hw->host_function == MSDC_SD) && (cmd->opcode == 5)) return;             	
    }	else {
        if (cmd->opcode == 8) return;
    }

    ERR_MSG("XXX CMD<%d><0x%x> Error<%d> Resp<0x%x>", cmd->opcode, cmd->arg, cmd->error, cmd->resp[0]);

    if (data) {
        ERR_MSG("XXX DAT block<%d> Error<%d>", data->blocks, data->error);
    }

    if (stop) {
        ERR_MSG("XXX STOP<%d><0x%x> Error<%d> Resp<0x%x>", stop->opcode, stop->arg, stop->error, stop->resp[0]);
    }
	if((host->hw->host_function == MSDC_SD) && 
	   (host->sclk > 100000000) && 
	   (data) &&
	   (data->error != (unsigned int)-ETIMEDOUT)){
	   if((data->flags & MMC_DATA_WRITE) && (host->write_timeout_uhs104))
			host->write_timeout_uhs104 = 0;
	   if((data->flags & MMC_DATA_READ) && (host->read_timeout_uhs104))
	   		host->read_timeout_uhs104 = 0;
	   }
}

/* ops.request */
static void msdc_ops_request_legacy(struct mmc_host *mmc, struct mmc_request *mrq)
{
    struct msdc_host *host = mmc_priv(mmc);
    struct mmc_command *cmd;
    struct mmc_data *data;
    struct mmc_command *stop = NULL;
	int data_abort = 0;
	int got_polarity = 0;
	unsigned long flags;
    //=== for sdio profile ===
    u32 old_H32 = 0, old_L32 = 0, new_H32 = 0, new_L32 = 0;
    u32 ticks = 0, opcode = 0, sizes = 0, bRx = 0;
    msdc_reset_tune_counter(host,all_counter);
    if(host->mrq){
        ERR_MSG("XXX host->mrq<0x%.8x> cmd<%d>arg<0x%x>", (int)host->mrq,host->mrq->cmd->opcode,host->mrq->cmd->arg);
        //BUG();
        WARN_ON(1);
    }

    if (!is_card_present(host) || host->power_mode == MMC_POWER_OFF) {
        ERR_MSG("cmd<%d> arg<0x%x> card<%d> power<%d>", mrq->cmd->opcode,mrq->cmd->arg,is_card_present(host), host->power_mode);
        mrq->cmd->error = (unsigned int)-ENOMEDIUM;

#if 1
	if(mrq->done)
        mrq->done(mrq);         // call done directly.
#else
        mrq->cmd->retries = 0;  // please don't retry.
        mmc_request_done(mmc, mrq);
#endif

        return;
    }

    /* start to process */
    spin_lock(&host->lock);

    cmd = mrq->cmd;
    data = mrq->cmd->data;
    if (data) stop = data->stop;

    host->mrq = mrq;

    while (msdc_do_request(mmc,mrq)) { // there is some error
        // becasue ISR executing time will be monitor, try to dump the info here.
        msdc_dump_trans_error(host, cmd, data, stop);
    	data_abort = 0;
        if (is_card_sdio(host)) {
            goto out;  // sdio not tuning
        }

        if ((cmd->error == (unsigned int)-EIO) || (stop && (stop->error == (unsigned int)-EIO))) {
            if (msdc_tune_cmdrsp(host)){
                ERR_MSG("failed to updata cmd para");
                goto out;
            }
        }

        if (data && (data->error == (unsigned int)-EIO)) {
            if (data->flags & MMC_DATA_READ) {  // read
                if (msdc_tune_read(host)) {
                    ERR_MSG("failed to updata read para");
                    goto out;
                }
            } else {
                if (msdc_tune_write(host)) {
                    ERR_MSG("failed to updata write para");
                    goto out;
                }
            }
        }

        // bring the card to "tran" state
        if (data) {
            if (msdc_abort_data(host)) {
                ERR_MSG("abort failed");
				data_abort = 1;
				if(host->hw->host_function == MSDC_SD){
					host->card_inserted = 0;
					if(host->mmc->card){
						spin_lock_irqsave(&host->remove_bad_card,flags);
						got_polarity = host->sd_cd_polarity;
						host->block_bad_card = 1;
						mmc_card_set_removed(host->mmc->card);
						spin_unlock_irqrestore(&host->remove_bad_card,flags);
					}
                	goto out;
				}
            }
        }

        // CMD TO -> not tuning
        if (cmd->error == (unsigned int)-ETIMEDOUT) {
			if(cmd->opcode == MMC_READ_SINGLE_BLOCK || cmd->opcode == MMC_READ_MULTIPLE_BLOCK || cmd->opcode == MMC_WRITE_BLOCK || cmd->opcode == MMC_WRITE_MULTIPLE_BLOCK ){
				if(data_abort){
            		if(msdc_power_tuning(host))
            			goto out;
				}
			}
			else
				goto out;
        }

        // [ALPS114710] Patch for data timeout issue.
        if (data && (data->error == (unsigned int)-ETIMEDOUT)) {
            if (data->flags & MMC_DATA_READ) {
				if( !(host->sw_timeout) && 
					(host->hw->host_function == MSDC_SD) && 
					(host->sclk > 100000000) && 
					(host->read_timeout_uhs104 < MSDC_MAX_R_TIMEOUT_TUNE)){
					if(host->t_counter.time_read)
						host->t_counter.time_read--;
					host->read_timeout_uhs104++;
					msdc_tune_read(host);
				}
				else if((host->sw_timeout) || (host->read_timeout_uhs104 >= MSDC_MAX_R_TIMEOUT_TUNE) || (++(host->read_time_tune) > MSDC_MAX_TIMEOUT_RETRY)){
					ERR_MSG("msdc%d exceed max read timeout retry times(%d) or SW timeout(%d) or read timeout tuning times(%d),Power cycle\n",host->id,host->read_time_tune,host->sw_timeout,host->read_timeout_uhs104);
					 if(msdc_power_tuning(host))
                		goto out;
					}
            }
			else if(data->flags & MMC_DATA_WRITE){
				if( (!(host->sw_timeout)) &&
					(host->hw->host_function == MSDC_SD) && 
					(host->sclk > 100000000) && 
					(host->write_timeout_uhs104 < MSDC_MAX_W_TIMEOUT_TUNE)){
					if(host->t_counter.time_write)
						host->t_counter.time_write--;
					host->write_timeout_uhs104++;
					msdc_tune_write(host);
				}
				else if((host->sw_timeout) || (host->write_timeout_uhs104 >= MSDC_MAX_W_TIMEOUT_TUNE) || (++(host->write_time_tune) > MSDC_MAX_TIMEOUT_RETRY)){
					ERR_MSG("msdc%d exceed max write timeout retry times(%d) or SW timeout(%d) or write timeout tuning time(%d),Power cycle\n",host->id,host->write_time_tune,host->sw_timeout,host->write_timeout_uhs104);
					if(msdc_power_tuning(host))
                		goto out;
					}
				}
        }

        // clear the error condition.
        cmd->error = 0;
        if (data) data->error = 0;
        if (stop) stop->error = 0;

        // check if an app commmand.
        if (host->app_cmd) {
            while (msdc_app_cmd(host->mmc, host)) {
                if (msdc_tune_cmdrsp(host)){
                    ERR_MSG("failed to updata cmd para for app");
                    goto out;
                }
            }
        }

        if (!is_card_present(host)) {
            goto out;
        }
    }


	if((host->read_time_tune)&&(cmd->opcode == MMC_READ_SINGLE_BLOCK || cmd->opcode == MMC_READ_MULTIPLE_BLOCK) ){
		host->read_time_tune = 0;
		ERR_MSG("sync Read recover");
		msdc_dump_trans_error(host, cmd, data, stop);
	}
	if((host->write_time_tune) && (cmd->opcode == MMC_WRITE_BLOCK || cmd->opcode == MMC_WRITE_MULTIPLE_BLOCK)){
		host->write_time_tune = 0;
		ERR_MSG("sync Write recover");
		msdc_dump_trans_error(host, cmd, data, stop);
	}
	host->sw_timeout = 0;
out:
    msdc_reset_tune_counter(host,all_counter);

#ifdef TUNE_FLOW_TEST
    if (!is_card_sdio(host)) {
        msdc_reset_para(host);
    }
#endif

    /* ==== when request done, check if app_cmd ==== */
    if (mrq->cmd->opcode == MMC_APP_CMD) {
        host->app_cmd = 1;
        host->app_cmd_arg = mrq->cmd->arg;  /* save the RCA */
    } else {
        host->app_cmd = 0;
        //host->app_cmd_arg = 0;
    }

    host->mrq = NULL;

    //=== for sdio profile ===
    if (sdio_pro_enable) {
        if (mrq->cmd->opcode == 52 || mrq->cmd->opcode == 53) {
            //GPT_GetCounter64(&new_L32, &new_H32);
            ticks = msdc_time_calc(old_L32, old_H32, new_L32, new_H32);

            opcode = mrq->cmd->opcode;
            if (mrq->cmd->data) {
                sizes = mrq->cmd->data->blocks * mrq->cmd->data->blksz;
                bRx = mrq->cmd->data->flags & MMC_DATA_READ ? 1 : 0 ;
            } else {
                bRx = mrq->cmd->arg	& 0x80000000 ? 1 : 0;
            }

            if (!mrq->cmd->error) {
                msdc_performance(opcode, sizes, bRx, ticks);
            }
        }
    }

    spin_unlock(&host->lock);
sdio_error_out:
    mmc_request_done(mmc, mrq);

   return;
}

static void msdc_tune_async_request(struct mmc_host *mmc, struct mmc_request *mrq)
{
    struct msdc_host *host = mmc_priv(mmc);
    struct mmc_command *cmd;
    struct mmc_data *data;
    struct mmc_command *stop = NULL;
	int data_abort = 0;
	int got_polarity = 0;
	unsigned long flags;
    //msdc_reset_tune_counter(host,all_counter);
    if(host->mrq){
        ERR_MSG("XXX host->mrq<0x%.8x> cmd<%d>arg<0x%x>", (int)host->mrq,host->mrq->cmd->opcode,host->mrq->cmd->arg);
		BUG();
    }

    if (!is_card_present(host) || host->power_mode == MMC_POWER_OFF) {
        ERR_MSG("cmd<%d> arg<0x%x> card<%d> power<%d>", mrq->cmd->opcode,mrq->cmd->arg, is_card_present(host), host->power_mode);
        mrq->cmd->error = (unsigned int)-ENOMEDIUM;
		//mrq->done(mrq);         // call done directly.
        return;
    }

    cmd = mrq->cmd;
    data = mrq->cmd->data;
    if (data) stop = data->stop;
	if((cmd->error == 0) && (data && data->error == 0) && (!stop || stop->error == 0)){
		if(cmd->opcode == MMC_READ_SINGLE_BLOCK || cmd->opcode == MMC_READ_MULTIPLE_BLOCK)
			host->read_time_tune = 0;
		if(cmd->opcode == MMC_WRITE_BLOCK || cmd->opcode == MMC_WRITE_MULTIPLE_BLOCK)
			host->write_time_tune = 0;
		host->rwcmd_time_tune = 0;

		return;
	}
    /* start to process */
    spin_lock(&host->lock);


    host->tune = 1;
    host->mrq = mrq;
	do{
		msdc_dump_trans_error(host, cmd, data, stop);         // becasue ISR executing time will be monitor, try to dump the info here.
		/*if((host->t_counter.time_cmd % 16 == 15)
		|| (host->t_counter.time_read % 16 == 15)
		|| (host->t_counter.time_write % 16 == 15)){
			spin_unlock(&host->lock);
			msleep(150);
			ERR_MSG("sleep 150ms here!");   //sleep in tuning flow, to avoid printk watchdong timeout
			spin_lock(&host->lock);
			goto out;
		}*/
        if ((cmd->error == (unsigned int)-EIO) || (stop && (stop->error == (unsigned int)-EIO))) {
            if (msdc_tune_cmdrsp(host)){
                ERR_MSG("failed to updata cmd para");
                goto out;
            }
        }

        if (data && (data->error == (unsigned int)-EIO)) {
            if (data->flags & MMC_DATA_READ) {  // read
                if (msdc_tune_read(host)) {
                    ERR_MSG("failed to updata read para");
                    goto out;
                }
            } else {
                if (msdc_tune_write(host)) {
                    ERR_MSG("failed to updata write para");
                    goto out;
                }
            }
        }

        // bring the card to "tran" state
        if (data) {
            if (msdc_abort_data(host)) {
                ERR_MSG("abort failed");
				data_abort = 1;
				if(host->hw->host_function == MSDC_SD){
					host->card_inserted = 0;
					if(host->mmc->card){
						spin_lock_irqsave(&host->remove_bad_card,flags);
						got_polarity = host->sd_cd_polarity;
						host->block_bad_card = 1;
						mmc_card_set_removed(host->mmc->card);
						spin_unlock_irqrestore(&host->remove_bad_card,flags);						
					}
                	goto out;
				}
            }
        }

        // CMD TO -> not tuning
        if (cmd->error == (unsigned int)-ETIMEDOUT) {
			if(cmd->opcode == MMC_READ_SINGLE_BLOCK || cmd->opcode == MMC_READ_MULTIPLE_BLOCK || cmd->opcode == MMC_WRITE_BLOCK || cmd->opcode == MMC_WRITE_MULTIPLE_BLOCK ){
				if((host->sw_timeout) || (++(host->rwcmd_time_tune) > MSDC_MAX_TIMEOUT_RETRY)){
					ERR_MSG("msdc%d exceed max r/w cmd timeout tune times(%d) or SW timeout(%d),Power cycle\n",host->id,host->rwcmd_time_tune,host->sw_timeout);
					 if(!(host->sd_30_busy) && msdc_power_tuning(host))
                		goto out;
					}
			}
			else
				goto out;
        }

        // [ALPS114710] Patch for data timeout issue.
        if (data && (data->error == (unsigned int)-ETIMEDOUT)) {
            if (data->flags & MMC_DATA_READ) {
				if( !(host->sw_timeout) && 
					(host->hw->host_function == MSDC_SD) && 
					(host->sclk > 100000000) && 
					(host->read_timeout_uhs104 < MSDC_MAX_R_TIMEOUT_TUNE)){
					if(host->t_counter.time_read)
						host->t_counter.time_read--;
					host->read_timeout_uhs104++;
					msdc_tune_read(host);
				}
				else if((host->sw_timeout) || (host->read_timeout_uhs104 >= MSDC_MAX_R_TIMEOUT_TUNE) || (++(host->read_time_tune) > MSDC_MAX_TIMEOUT_RETRY)){
					ERR_MSG("msdc%d exceed max read timeout retry times(%d) or SW timeout(%d) or read timeout tuning times(%d),Power cycle\n",host->id,host->read_time_tune,host->sw_timeout,host->read_timeout_uhs104);
					 if(!(host->sd_30_busy) && msdc_power_tuning(host))
                		goto out;
					}
            }
			else if(data->flags & MMC_DATA_WRITE){
				if( !(host->sw_timeout) && 
					(host->hw->host_function == MSDC_SD) && 
					(host->sclk > 100000000) && 
					(host->write_timeout_uhs104 < MSDC_MAX_W_TIMEOUT_TUNE)){
					if(host->t_counter.time_write)
						host->t_counter.time_write--;
					host->write_timeout_uhs104++;
					msdc_tune_write(host);
				}
				else if((host->sw_timeout) || (host->write_timeout_uhs104 >= MSDC_MAX_W_TIMEOUT_TUNE) || (++(host->write_time_tune) > MSDC_MAX_TIMEOUT_RETRY)){
					ERR_MSG("msdc%d exceed max write timeout retry times(%d) or SW timeout(%d) or write timeout tuning time(%d),Power cycle\n",host->id,host->write_time_tune,host->sw_timeout,host->write_timeout_uhs104);
					if(!(host->sd_30_busy) && msdc_power_tuning(host))
                		goto out;
					}
				}
        }

        // clear the error condition.
        cmd->error = 0;
        if (data) data->error = 0;
        if (stop) stop->error = 0;
		host->sw_timeout = 0;
        if (!is_card_present(host)) {
            goto out;
        }
    }while(msdc_tune_rw_request(mmc,mrq));
    
	if((host->rwcmd_time_tune)&&(cmd->opcode == MMC_READ_SINGLE_BLOCK || cmd->opcode == MMC_READ_MULTIPLE_BLOCK || cmd->opcode == MMC_WRITE_BLOCK || cmd->opcode == MMC_WRITE_MULTIPLE_BLOCK)){
		host->rwcmd_time_tune = 0;
		ERR_MSG("RW cmd recover");
		msdc_dump_trans_error(host, cmd, data, stop);
	}
	if((host->read_time_tune)&&(cmd->opcode == MMC_READ_SINGLE_BLOCK || cmd->opcode == MMC_READ_MULTIPLE_BLOCK) ){
		host->read_time_tune = 0;
		ERR_MSG("Read recover");
		msdc_dump_trans_error(host, cmd, data, stop);
	}
	if((host->write_time_tune) && (cmd->opcode == MMC_WRITE_BLOCK || cmd->opcode == MMC_WRITE_MULTIPLE_BLOCK)){
		host->write_time_tune = 0;
		ERR_MSG("Write recover");
		msdc_dump_trans_error(host, cmd, data, stop);
	}

	host->sw_timeout = 0;

out:
	if(host->sclk <= 50000000 && (!host->ddr))
		host->sd_30_busy = 0;  
    msdc_reset_tune_counter(host,all_counter);
    host->mrq = NULL;

    host->tune = 0;
    spin_unlock(&host->lock);

    //mmc_request_done(mmc, mrq);
   	return;
}
static void msdc_ops_request(struct mmc_host *mmc, struct mmc_request *mrq)
{
	 struct mmc_data *data;
	 int async = 0;
	 BUG_ON(mmc == NULL);
     BUG_ON(mrq == NULL);
	 data = mrq->data;
	 if(data){
	 		async = data->host_cookie;
		}
	 if(async){
	 		msdc_do_request_async(mmc,mrq);
	 	}
	 else
	 	msdc_ops_request_legacy(mmc,mrq);
	 return;
}


/* called by ops.set_ios */
static void msdc_set_buswidth(struct msdc_host *host, u32 width)
{
    u32 base = host->base;
    u32 val = sdr_read32(SDC_CFG);

    val &= ~SDC_CFG_BUSWIDTH;

    switch (width) {
    default:
    case MMC_BUS_WIDTH_1:
        width = 1;
        val |= (MSDC_BUS_1BITS << 16);
        break;
    case MMC_BUS_WIDTH_4:
        val |= (MSDC_BUS_4BITS << 16);
        break;
    case MMC_BUS_WIDTH_8:
        val |= (MSDC_BUS_8BITS << 16);
        break;
    }

    sdr_write32(SDC_CFG, val);

    N_MSG(CFG, "Bus Width = %d", width);
}



/* ops.set_ios */

static void msdc_ops_set_ios(struct mmc_host *mmc, struct mmc_ios *ios)
{
    struct msdc_host *host = mmc_priv(mmc);
    struct msdc_hw *hw=host->hw;
    u32 base = host->base;
    u32 ddr = 0;
    //unsigned long flags;
    u32 cur_rxdly0, cur_rxdly1;
    
    //ERR_MSG("host%d set ios, power_mode %d,bus_width is %d,clock is %d",host->id,ios->power_mode,ios->bus_width,ios->clock);

    spin_lock(&host->lock);
    if(ios->timing == MMC_TIMING_UHS_DDR50)
        ddr = 1;

    msdc_set_buswidth(host, ios->bus_width);

    /* Power control ??? */
    switch (ios->power_mode) {
    case MMC_POWER_OFF:
    case MMC_POWER_UP:

		//msdc_set_driving(host,hw,0); //to check? need set driving when power up,such as resume
		
		spin_unlock(&host->lock);
        //msdc_set_power_mode(host, ios->power_mode);
		spin_lock(&host->lock);
        break;
    case MMC_POWER_ON:
        host->power_mode = MMC_POWER_ON;
        break;
    default:
        break;
    }
    if(msdc_host_mode[host->id] != mmc->caps)
	{
  		mmc->caps = msdc_host_mode[host->id];
  		sdr_write32(MSDC_PAD_TUNE,   0x00000000);
		sdr_set_field(MSDC_PAD_TUNE, MSDC_PAD_TUNE_DATWRDLY, host->hw->datwrddly);
  		sdr_set_field(MSDC_PAD_TUNE, MSDC_PAD_TUNE_CMDRRDLY, host->hw->cmdrrddly);
  		sdr_set_field(MSDC_PAD_TUNE, MSDC_PAD_TUNE_CMDRDLY, host->hw->cmdrddly);
  		sdr_write32(MSDC_IOCON,      0x00000000);
#if 1
		sdr_set_field(MSDC_IOCON, MSDC_IOCON_DDLSEL, 1);
  			cur_rxdly0 = ((host->hw->dat0rddly & 0x1F) << 24) | ((host->hw->dat1rddly & 0x1F) << 16) |
         	    		     ((host->hw->dat2rddly & 0x1F) << 8)  | ((host->hw->dat3rddly & 0x1F) << 0);
     			cur_rxdly1 = ((host->hw->dat4rddly & 0x1F) << 24) | ((host->hw->dat5rddly & 0x1F) << 16) |
	          	         ((host->hw->dat6rddly & 0x1F) << 8)  | ((host->hw->dat7rddly & 0x1F) << 0);
	    sdr_write32(MSDC_DAT_RDDLY0, cur_rxdly0);
	    sdr_write32(MSDC_DAT_RDDLY1, cur_rxdly1);
#else
  		sdr_write32(MSDC_DAT_RDDLY0, 0x00000000);
  		sdr_write32(MSDC_DAT_RDDLY1, 0x00000000);
#endif
    	//sdr_write32(MSDC_PATCH_BIT0, 0x403C004F); /* bit0 modified: Rx Data Clock Source: 1 -> 2.0*/

    	if(host->hw->host_function == MSDC_SD) //to check reserved bit7,6?
			sdr_write32(MSDC_PATCH_BIT1, 0xFFFF00C9);
		else
		    sdr_write32(MSDC_PATCH_BIT1, 0xFFFF0009);

          		//sdr_set_field(MSDC_PATCH_BIT1, MSDC_PATCH_BIT1_WRDAT_CRCS, 1);
          		//sdr_set_field(MSDC_PATCH_BIT1, MSDC_PATCH_BIT1_CMD_RSP,    1);


		if (!is_card_sdio(host)) {  /* internal clock: latch read data, not apply to sdio */
          	//sdr_set_bits(MSDC_PATCH_BIT0, MSDC_PATCH_BIT_CKGEN_CK);
          	host->hw->cmd_edge  = 0; // tuning from 0
          	host->hw->rdata_edge = 0;
          	host->hw->wdata_edge = 0;
      	}else if (hw->flags & MSDC_INTERNAL_CLK) {
          	//sdr_set_bits(MSDC_PATCH_BIT0, MSDC_PATCH_BIT_CKGEN_CK);
      	}
	}
	
    msdc_select_clksrc(host, hw->clk_src);
   

    if (host->mclk != ios->clock || host->ddr != ddr) { /* not change when clock Freq. not changed ddr need set clock*/
        if(ios->clock >= 25000000) {
			if(ios->clock > 100000000){
				hw->clk_drv_sd_18 += 1;
				msdc_set_driving(host,hw,1); //to check here
				hw->clk_drv_sd_18 -= 1;
			}
            //INIT_MSG("SD latch rdata<%d> wdatea<%d> cmd<%d>", hw->rdata_edge,hw->wdata_edge, hw->cmd_edge);
            sdr_set_field(MSDC_IOCON, MSDC_IOCON_RSPL, hw->cmd_edge);
            sdr_set_field(MSDC_IOCON, MSDC_IOCON_DSPL, hw->rdata_edge);
            sdr_set_field(MSDC_IOCON, MSDC_IOCON_W_DSPL, hw->wdata_edge);
			sdr_write32(MSDC_PAD_TUNE,host->saved_para.pad_tune);
			sdr_write32(MSDC_DAT_RDDLY0,host->saved_para.ddly0);
			sdr_write32(MSDC_DAT_RDDLY1,host->saved_para.ddly1);
			sdr_set_field(MSDC_PATCH_BIT1, MSDC_PATCH_BIT1_WRDAT_CRCS, host->saved_para.wrdat_crc_ta_cntr);
            sdr_set_field(MSDC_PATCH_BIT1, MSDC_PATCH_BIT1_CMD_RSP,    host->saved_para.cmd_resp_ta_cntr);
			if(host->id == 1){
				sdr_set_field(MSDC_PATCH_BIT1, (1 << 6),1);
				sdr_set_field(MSDC_PATCH_BIT1, (1 << 7),1);
			}

        }

        if (ios->clock == 0) {
			if(ios->power_mode == MMC_POWER_OFF){
            	sdr_get_field(MSDC_IOCON, MSDC_IOCON_RSPL, hw->cmd_edge);   // save the para
            	sdr_get_field(MSDC_IOCON, MSDC_IOCON_DSPL, hw->rdata_edge);
				sdr_get_field(MSDC_IOCON, MSDC_IOCON_W_DSPL, hw->wdata_edge);
				host->saved_para.pad_tune = sdr_read32(MSDC_PAD_TUNE);
				host->saved_para.ddly0 = sdr_read32(MSDC_DAT_RDDLY0);
				host->saved_para.ddly1 = sdr_read32(MSDC_DAT_RDDLY1);
				sdr_get_field(MSDC_PATCH_BIT1, MSDC_PATCH_BIT1_CMD_RSP,    host->saved_para.cmd_resp_ta_cntr);
				sdr_get_field(MSDC_PATCH_BIT1, MSDC_PATCH_BIT1_WRDAT_CRCS, host->saved_para.wrdat_crc_ta_cntr);
            	//INIT_MSG("save latch rdata<%d> wdata<%d> cmd<%d>", hw->rdata_edge,hw->wdata_edge,hw->cmd_edge);
			}
            /* reset to default value */
           	sdr_write32(MSDC_IOCON,      0x00000000);           
           	sdr_write32(MSDC_DAT_RDDLY0, 0x00000000);
           	sdr_write32(MSDC_DAT_RDDLY1, 0x00000000);            
           	sdr_write32(MSDC_PAD_TUNE,   0x00000000);
			sdr_set_field(MSDC_PATCH_BIT1, MSDC_PATCH_BIT1_CMD_RSP,1);
			sdr_set_field(MSDC_PATCH_BIT1, MSDC_PATCH_BIT1_WRDAT_CRCS,1);
			if(host->id == 1){
				sdr_set_field(MSDC_PATCH_BIT1, (1 << 6),1);
				sdr_set_field(MSDC_PATCH_BIT1, (1 << 7),1);
			}
        }
        msdc_set_mclk(host, ddr, ios->clock);
        msdc_sample_edge(mmc,0);
        msdc_smaple_delay(mmc,0); //dtv remained
        
        if(mmc->ios.timing == MMC_TIMING_UHS_DDR50 || mmc->ios.timing == MMC_TIMING_MMC_HS200) 
        {
            sdr_write32(MSDC_CLK_H_REG1, 0x2);
        }
    }
    spin_unlock(&host->lock);
}

/* ops.get_ro */
static int msdc_ops_get_ro(struct mmc_host *mmc)
{
    struct msdc_host *host = mmc_priv(mmc);
    u32 base = host->base;
    unsigned long flags;
    int ro = 0;

    spin_lock_irqsave(&host->lock, flags);
    if (host->hw->flags & MSDC_WP_PIN_EN) { /* set for card */
        ro = (sdr_read32(MSDC_PS) >> 31);
    }

    spin_unlock_irqrestore(&host->lock, flags);
    return ro;
}

/* ops.get_cd */
static int msdc_ops_get_cd(struct mmc_host *mmc)
{
    struct msdc_host *host = mmc_priv(mmc);
    u32 base;
    unsigned long flags;
    //int present = 1;

    base = host->base;
    spin_lock_irqsave(&host->lock, flags);

    /* for emmc, MSDC_REMOVABLE not set, always return 1 */
    if (!(host->hw->flags & MSDC_REMOVABLE)) {
        host->card_inserted = 1;
        goto end;
    }

    INIT_MSG("Card insert<%d> Block bad card<%d>", host->card_inserted,host->block_bad_card);
    if(host->hw->host_function == MSDC_SD && host->block_bad_card)
		host->card_inserted = 0;
end:
    spin_unlock_irqrestore(&host->lock, flags);
    return host->card_inserted;
}

static inline void mmc_delay(unsigned int ms)
{
	if (ms < 1000 / HZ) {
		cond_resched();
		mdelay(ms);
	} else {
		msleep(ms);
	}
}

static int msdc0_do_start_signal_voltage_switch(struct mmc_host *mmc,
	struct mmc_ios *ios)
{  
	struct msdc_host *host = mmc_priv(mmc);
	u32 base = host->base;	
    struct msdc_hw *hw = host->hw;

	// wait SDC_STA.SDCBUSY = 0
    while(sdc_is_busy());

	// Disable pull-up resistors
	sdr_clr_bits(MSDC_PAD_CTL1, (0x1<<17));
	sdr_clr_bits(MSDC_PAD_CTL2, (0x1<<17));


    // make sure CMD/DATA lines are pulled to 0
	while((sdr_read32(MSDC_PS)&(0x1FF<<16)));

	// Program the PMU of host from 3.3V to 1.8V
	if(0 != MsdcSDPowerSwitch(1))
    {
		return -1;
	}

	// Delay at least 5ms
	mmc_delay(6);
    

	// Enable pull-up resistors
	sdr_clr_bits(MSDC_PAD_CTL1, (0x1<<17));
	sdr_clr_bits(MSDC_PAD_CTL2, (0x1<<17));
    

	// start 1.8V voltage detecting
	sdr_set_bits(MSDC_CFG, MSDC_CFG_BV18SDT);

	// Delay at most 1ms
    mmc_delay(1);
    
	// check MSDC.VOL_18V_START_DET is 0
	while(sdr_read32(MSDC_CFG)&MSDC_CFG_BV18SDT);
  

	// check MSDC_CFG.VOL_18V_PASS
	if(sdr_read32(MSDC_CFG)&MSDC_CFG_BV18PSS)
    {  
		printk(KERN_ERR "switch 1.8v voltage success!\n");
      	return 0;
	}
	else
	{ 
		printk(KERN_ERR "switch 1.8v voltage failed!\n");
		return -1;
	}

}

int msdc_ops_switch_volt(struct mmc_host *mmc, struct mmc_ios *ios) //for sdr 104 3.3->1.8
{
	struct msdc_host *host = mmc_priv(mmc);
	int err;
	if(host->hw->host_function == MSDC_EMMC)
		return 0;

	if (mmc->ios.signal_voltage < 1)
		return 0; 
	err = msdc0_do_start_signal_voltage_switch(mmc, ios);

	return err;

}

static void msdc_ops_stop(struct mmc_host *mmc,struct mmc_request *mrq)
{
	//struct mmc_command stop = {0};
    //struct mmc_request mrq_stop = {0};
	struct msdc_host *host = mmc_priv(mmc);
    u32 err = -1;
	//u32 bus_mode = 0;
//	u32 base = host->base;
	if(host->hw->host_function != MSDC_SDIO && !is_card_sdio(host)){
		if(!mrq->stop)
			return;

		//sdr_get_field(SDC_CFG ,SDC_CFG_BUSWIDTH ,bus_mode);
  		if((host->autocmd == MSDC_AUTOCMD12) && (!(host->error & REQ_DAT_ERR)))
			return;
		N_MSG(OPS, "MSDC Stop for non-autocmd12 host->error(%d)host->autocmd(%d)",host->error,host->autocmd);
    	err = msdc_do_command(host, mrq->stop, 0, CMD_TIMEOUT);
		if(err){
			if (mrq->stop->error == (unsigned int)-EIO) host->error |= REQ_STOP_EIO;
			if (mrq->stop->error == (unsigned int)-ETIMEDOUT) host->error |= REQ_STOP_TMO;
		}
	}
}

extern u32 __mmc_sd_num_wr_blocks(struct mmc_card* card);
static bool msdc_check_written_data(struct mmc_host *mmc,struct mmc_request *mrq)
{
	u32 result = 0;
	struct msdc_host *host = mmc_priv(mmc);
	struct mmc_card *card;
	if (!is_card_present(host) || host->power_mode == MMC_POWER_OFF) {
        ERR_MSG("cmd<%d> arg<0x%x> card<%d> power<%d>", mrq->cmd->opcode,mrq->cmd->arg,is_card_present(host), host->power_mode);
        mrq->cmd->error = (unsigned int)-ENOMEDIUM;
		return 0;
	}
	if(mmc->card)
		card = mmc->card;
	else
		return 0;
	if((host->hw->host_function == MSDC_SD)
		&& (host->sclk > 100000000)
		&& mmc_card_sd(card)
		&& (mrq->data) 
		&& (mrq->data->flags & MMC_DATA_WRITE)
		&& (host->error == 0)){
		spin_lock(&host->lock);
		if(msdc_polling_idle(host)){
			spin_unlock(&host->lock);
			return 0;
		}
		spin_unlock(&host->lock);
		result = __mmc_sd_num_wr_blocks(card);
		if((result != mrq->data->blocks) && (is_card_present(host)) && (host->power_mode == MMC_POWER_ON)){
			mrq->data->error = (unsigned int)-EIO;
			host->error |= REQ_DAT_ERR;
			ERR_MSG("written data<%d> blocks isn't equal to request data blocks<%d>",result,mrq->data->blocks);
			return 1;
		}
	}
	return 0;		
}
static void msdc_dma_error_reset(struct mmc_host *mmc)
{
	struct msdc_host *host = mmc_priv(mmc);
	u32 base = host->base;
	struct mmc_data *data = host->data;
	if(data && host->dma_xfer && (data->host_cookie) && (host->tune == 0)){
		host->sw_timeout++;
		host->error |= REQ_DAT_ERR;
		msdc_reset_hw(host->id);
		msdc_dma_stop(host);
		msdc_clr_fifo(host->id);
		msdc_clr_int();
		msdc_dma_clear(host);
	}
}

static struct mmc_host_ops mt_msdc_ops = {
	.post_req 		 = msdc_post_req,
	.pre_req 		 = msdc_pre_req,
    .request         = msdc_ops_request,
    .tuning			 = msdc_tune_async_request,
    .set_ios         = msdc_ops_set_ios,
    .get_ro          = msdc_ops_get_ro,
    .get_cd          = msdc_ops_get_cd,
    .start_signal_voltage_switch = msdc_ops_switch_volt,
    .send_stop		 = msdc_ops_stop,
    .dma_error_reset = msdc_dma_error_reset,
    .check_written_data = msdc_check_written_data,
};

/*--------------------------------------------------------------------------*/
/* interrupt handler                                                    */
/*--------------------------------------------------------------------------*/
//static __tcmfunc irqreturn_t msdc_irq(int irq, void *dev_id)

static irqreturn_t msdc_irq(int irq, void *dev_id)
{
    struct msdc_host  *host = (struct msdc_host *)dev_id;
    struct mmc_data   *data = host->data;
    struct mmc_command *cmd = host->cmd;
    struct mmc_command *stop = NULL;
    struct mmc_request *mrq = NULL;
    u32 base = host->base;

    u32 cmdsts = MSDC_INT_RSPCRCERR  | MSDC_INT_CMDTMO  | MSDC_INT_CMDRDY  |
                 MSDC_INT_ACMDCRCERR | MSDC_INT_ACMDTMO | MSDC_INT_ACMDRDY |
                 MSDC_INT_ACMD19_DONE;
    u32 datsts = MSDC_INT_DATCRCERR  |MSDC_INT_DATTMO;
    u32 intsts, inten;

    intsts = sdr_read32(MSDC_INT);
    
    inten  = sdr_read32(MSDC_INTEN); inten &= intsts;

    sdr_write32(MSDC_INT, intsts);  /* clear interrupts */
    /* MSG will cause fatal error */

    /* sdio interrupt */
    if (intsts & MSDC_INT_SDIOIRQ){
        IRQ_MSG("XXX MSDC_INT_SDIOIRQ");  /* seems not sdio irq */
        //mmc_signal_sdio_irq(host->mmc);
    }
    


    /* transfer complete interrupt */
    if (data != NULL) {
		stop = data->stop;
        if (inten & MSDC_INT_XFER_COMPL) {
            data->bytes_xfered = host->dma.xfersz;
			if((data->host_cookie) && (host->tune == 0)){
					msdc_dma_stop(host);
					mrq = host->mrq;
					msdc_dma_clear(host);
					mb();
					if(mrq->done)
				 		mrq->done(mrq);

					host->error &= ~REQ_DAT_ERR;
				 }
			else
            complete(&host->xfer_done);
        }

        if (intsts & datsts) {
            /* do basic reset, or stop command will sdc_busy */
            msdc_reset_hw(host->id);
            atomic_set(&host->abort, 1);  /* For PIO mode exit */

            if (intsts & MSDC_INT_DATTMO){
               	data->error = (unsigned int)-ETIMEDOUT;
               	IRQ_MSG("XXX CMD<%d> Arg<0x%.8x> MSDC_INT_DATTMO", host->mrq->cmd->opcode, host->mrq->cmd->arg);
            }
            else if (intsts & MSDC_INT_DATCRCERR){
                data->error = (unsigned int)-EIO;
                IRQ_MSG("XXX CMD<%d> Arg<0x%.8x> MSDC_INT_DATCRCERR, SDC_DCRC_STS<0x%x>",
                        host->mrq->cmd->opcode, host->mrq->cmd->arg, sdr_read32(SDC_DCRC_STS));
            }

            //if(sdr_read32(MSDC_INTEN) & MSDC_INT_XFER_COMPL) {
            if (host->dma_xfer) {
				if((data->host_cookie) && (host->tune == 0)){
						msdc_dma_stop(host);
						msdc_clr_fifo(host->id);
						msdc_clr_int();
						mrq = host->mrq;
						msdc_dma_clear(host);
						mb();
						if(mrq->done)
				 			mrq->done(mrq);

						host->error |= REQ_DAT_ERR;
					}
				else
                complete(&host->xfer_done); /* Read CRC come fast, XFER_COMPL not enabled */
            } /* PIO mode can't do complete, because not init */
        }
		if ((stop != NULL) && (host->autocmd == MSDC_AUTOCMD12) && (intsts & cmdsts)) {
        	if (intsts & MSDC_INT_ACMDRDY) {
            	u32 *arsp = &stop->resp[0];
            	*arsp = sdr_read32(SDC_ACMD_RESP);
            }
        	else if (intsts & MSDC_INT_ACMDCRCERR) {
					stop->error =(unsigned int)-EIO;
					host->error |= REQ_STOP_EIO;
        		    msdc_reset_hw(host->id);
            }
        	 else if (intsts & MSDC_INT_ACMDTMO) {
					stop->error =(unsigned int)-ETIMEDOUT;
					host->error |= REQ_STOP_TMO;
					msdc_reset_hw(host->id);
        	}
			if((intsts & MSDC_INT_ACMDCRCERR) || (intsts & MSDC_INT_ACMDTMO)){
				if (host->dma_xfer){
					if((data->host_cookie) && (host->tune == 0)){
						msdc_dma_stop(host);
						msdc_clr_fifo(host->id);
						msdc_clr_int();
						mrq = host->mrq;
						msdc_dma_clear(host);
						mb();
						if(mrq->done)
				 			mrq->done(mrq);
					}
					else
						complete(&host->xfer_done); //Autocmd12 issued but error occur, the data transfer done INT will not issue,so cmplete is need here
				}/* PIO mode can't do complete, because not init */
			}
    	}
    }
    /* command interrupts */
    if ((cmd != NULL) && (intsts & cmdsts)) {
        if (intsts & MSDC_INT_CMDRDY) {
            u32 *rsp = NULL;
			rsp = &cmd->resp[0];

            switch (host->cmd_rsp) {
            case RESP_NONE:
                break;
            case RESP_R2:
                *rsp++ = sdr_read32(SDC_RESP3); *rsp++ = sdr_read32(SDC_RESP2);
                *rsp++ = sdr_read32(SDC_RESP1); *rsp++ = sdr_read32(SDC_RESP0);
                break;
            default: /* Response types 1, 3, 4, 5, 6, 7(1b) */
                *rsp = sdr_read32(SDC_RESP0);
                break;
            }
        } else if (intsts & MSDC_INT_RSPCRCERR) {
        			cmd->error = (unsigned int)-EIO;
					IRQ_MSG("XXX CMD<%d> MSDC_INT_RSPCRCERR Arg<0x%.8x>",cmd->opcode, cmd->arg);
	            	msdc_reset_hw(host->id);
        	}
         else if (intsts & MSDC_INT_CMDTMO) {
            		cmd->error = (unsigned int)-ETIMEDOUT;
					IRQ_MSG("XXX CMD<%d> MSDC_INT_CMDTMO Arg<0x%.8x>",cmd->opcode, cmd->arg);
    		        msdc_reset_hw(host->id);
        }
		if(intsts & (MSDC_INT_CMDRDY | MSDC_INT_RSPCRCERR | MSDC_INT_CMDTMO))
        	complete(&host->cmd_done);
    }

    /* mmc irq interrupts */
    if (intsts & MSDC_INT_MMCIRQ) {
        //printk(KERN_INFO "msdc[%d] MMCIRQ: SDC_CSTS=0x%.8x\r\n", host->id, sdr_read32(SDC_CSTS));
    }

    return IRQ_HANDLED;
}

/*--------------------------------------------------------------------------*/
/* platform_driver members                                                      */
/*--------------------------------------------------------------------------*/
/* called by msdc_drv_probe/remove */
static void msdc_enable_cd_irq(struct msdc_host *host, int enable)
{
	return;
}

/* called by msdc_drv_probe */

static void msdc_init_hw(struct msdc_host *host)
{
    u32 base = host->base;
    struct msdc_hw *hw = host->hw;
    u32 cur_rxdly0, cur_rxdly1;

    msdc_pinmux(host);

    /* Bug Fix: If clock is disabed, Version Register Can't be read. */
    msdc_select_clksrc(host, 0);

    /* Configure to MMC/SD mode */
    sdr_set_field(MSDC_CFG, MSDC_CFG_MODE, MSDC_SDMMC);

    /* Reset */
    msdc_reset_hw(host->id);

    /* Disable card detection */
    sdr_clr_bits(MSDC_PS, MSDC_PS_CDEN);

    /* Disable and clear all interrupts */
    sdr_clr_bits(MSDC_INTEN, sdr_read32(MSDC_INTEN));
    sdr_write32(MSDC_INT, sdr_read32(MSDC_INT));

	/* reset tuning parameter */
    //sdr_write32(MSDC_PAD_CTL0,   0x00098000);
    //sdr_write32(MSDC_PAD_CTL1,   0x000A0000);
    //sdr_write32(MSDC_PAD_CTL2,   0x000A0000);
    sdr_write32(MSDC_PAD_TUNE,   0x00000000);
	
	sdr_write32(MSDC_IOCON, 	 0x00000000);

    sdr_set_field(MSDC_IOCON, MSDC_IOCON_DDLSEL, 1);
    cur_rxdly0 = ((hw->dat0rddly & 0x1F) << 24) | ((hw->dat1rddly & 0x1F) << 16) |
           	         ((hw->dat2rddly & 0x1F) << 8)  | ((hw->dat3rddly & 0x1F) << 0);
    cur_rxdly1 = ((hw->dat4rddly & 0x1F) << 24) | ((hw->dat5rddly & 0x1F) << 16) |
          	         ((hw->dat6rddly & 0x1F) << 8)  | ((hw->dat7rddly & 0x1F) << 0);

    sdr_write32(MSDC_DAT_RDDLY0, 0x00000000);
    sdr_write32(MSDC_DAT_RDDLY1, 0x00000000);	

    //sdr_write32(MSDC_PATCH_BIT0, 0x003C004F); 
   if(host->hw->host_function == MSDC_SD)
		sdr_write32(MSDC_PATCH_BIT1, 0xFFFF00C9); //to check C9??
	else
    	sdr_write32(MSDC_PATCH_BIT1, 0xFFFF0009);
    //sdr_set_field(MSDC_PATCH_BIT1, MSDC_PATCH_BIT1_WRDAT_CRCS, 1); 
    //sdr_set_field(MSDC_PATCH_BIT1, MSDC_PATCH_BIT1_CMD_RSP,    1);
    //data delay settings should be set after enter high speed mode(now is at ios function >25MHz), detail information, please refer to P4 description
    host->saved_para.pad_tune = (((hw->cmdrrddly & 0x1F) << 22) | ((hw->cmdrddly & 0x1F) << 16) | ((hw->datwrddly & 0x1F) << 0));//sdr_read32(MSDC_PAD_TUNE);
	host->saved_para.ddly0 = cur_rxdly0; //sdr_read32(MSDC_DAT_RDDLY0);
	host->saved_para.ddly1 = cur_rxdly1; //sdr_read32(MSDC_DAT_RDDLY1);
	sdr_get_field(MSDC_PATCH_BIT1, MSDC_PATCH_BIT1_CMD_RSP,    host->saved_para.cmd_resp_ta_cntr);
	sdr_get_field(MSDC_PATCH_BIT1, MSDC_PATCH_BIT1_WRDAT_CRCS, host->saved_para.wrdat_crc_ta_cntr);

    if (!is_card_sdio(host)) {  /* internal clock: latch read data, not apply to sdio */
            //sdr_set_bits(MSDC_PATCH_BIT0, MSDC_PATCH_BIT_CKGEN_CK);//No feedback clock only internal clock in MT6589/MT6585
            host->hw->cmd_edge  = 0; // tuning from 0
            host->hw->rdata_edge = 0;
            host->hw->wdata_edge = 0;
        }else if (hw->flags & MSDC_INTERNAL_CLK) {
            //sdr_set_bits(MSDC_PATCH_BIT0, MSDC_PATCH_BIT_CKGEN_CK);
        }
		//sdr_set_field(MSDC_IOCON, MSDC_IOCON_DDLSEL, 1);


    /* for safety, should clear SDC_CFG.SDIO_INT_DET_EN & set SDC_CFG.SDIO in
       pre-loader,uboot,kernel drivers. and SDC_CFG.SDIO_INT_DET_EN will be only
       set when kernel driver wants to use SDIO bus interrupt */
    /* Configure to enable SDIO mode. it's must otherwise sdio cmd5 failed */
    sdr_clr_bits(SDC_CFG, SDC_CFG_SDIO);

    /* disable detect SDIO device interupt function */
    sdr_clr_bits(SDC_CFG, SDC_CFG_SDIOIDE);

    /* set clk, cmd, dat pad driving */
    hw->clk_drv = msdcClk[IDX_CLK_DRV_VAL][sizeof(msdcClk) / sizeof(msdcClk[0]) - 1];
    hw->cmd_drv = msdcClk[IDX_CMD_DRV_VAL][sizeof(msdcClk) / sizeof(msdcClk[0]) - 1];
    hw->dat_drv = msdcClk[IDX_DAT_DRV_VAL][sizeof(msdcClk) / sizeof(msdcClk[0]) - 1];
	//msdc_set_driving(host,hw,0); //to check


    INIT_MSG("msdc drving<clk %d,cmd %d,dat %d>",hw->clk_drv,hw->cmd_drv,hw->dat_drv);

    /* write crc timeout detection */
    sdr_set_field(MSDC_PATCH_BIT0, 1 << 30, 1);

    /* Configure to default data timeout */
    sdr_set_field(SDC_CFG, SDC_CFG_DTOC, DEFAULT_DTOC);

    msdc_set_buswidth(host, MMC_BUS_WIDTH_1);

    N_MSG(FUC, "init hardware done!");
}

/* called by msdc_drv_remove */
static void msdc_deinit_hw(struct msdc_host *host)
{
    u32 base = host->base;

    /* Disable and clear all interrupts */
    sdr_clr_bits(MSDC_INTEN, sdr_read32(MSDC_INTEN));
    sdr_write32(MSDC_INT, sdr_read32(MSDC_INT));

//    msdc_set_power_mode(host, MMC_POWER_OFF);   /* make sure power down */
}

/* init gpd and bd list in msdc_drv_probe */
static void msdc_init_gpd_bd(struct msdc_host *host, struct msdc_dma *dma)
{
    gpd_t *gpd = dma->gpd;
    bd_t  *bd  = dma->bd;
    bd_t  *ptr, *prev;

    /* we just support one gpd */
    int bdlen = MAX_BD_PER_GPD;

    /* init the 2 gpd */
    memset(gpd, 0, sizeof(gpd_t) * 2);
    //gpd->next = (void *)virt_to_phys(gpd + 1); /* pointer to a null gpd, bug! kmalloc <-> virt_to_phys */
    //gpd->next = (dma->gpd_addr + 1);    /* bug */
    gpd->next = (void *)((u32)dma->gpd_addr + sizeof(gpd_t));

    //gpd->intr = 0;
    gpd->bdp  = 1;   /* hwo, cs, bd pointer */
    //gpd->ptr  = (void*)virt_to_phys(bd);
    gpd->ptr = (void *)dma->bd_addr; /* physical address */

    memset(bd, 0, sizeof(bd_t) * bdlen);
    ptr = bd + bdlen - 1;
    //ptr->eol  = 1;  /* 0 or 1 [Fix me]*/
    //ptr->next = 0;

    while (ptr != bd) {
        prev = ptr - 1;
        prev->next = (void *)(dma->bd_addr + sizeof(bd_t) *(ptr - bd));
        ptr = prev;
    }
}

static void msdc_init_dma_latest_address(void)
{
    struct dma_addr *ptr, *prev;
    int bdlen = MAX_BD_PER_GPD;

    memset(msdc_latest_dma_address, 0, sizeof(struct dma_addr) * bdlen);
    ptr = msdc_latest_dma_address + bdlen - 1;
    while(ptr != msdc_latest_dma_address){
       prev = ptr - 1;
       prev->next = (void *)(msdc_latest_dma_address + sizeof(struct dma_addr) * (ptr - msdc_latest_dma_address));
       ptr = prev;
    }

}
struct msdc_host *msdc_get_host(int host_function,bool boot,bool secondary)
{
	int host_index = 0;
	struct msdc_host *host = NULL;
	for(;host_index < HOST_MAX_NUM;++host_index){
		if(!mtk_msdc_host[host_index])
			continue;
		if((host_function == mtk_msdc_host[host_index]->hw->host_function)
			&& (boot == mtk_msdc_host[host_index]->hw->boot)){
			host = mtk_msdc_host[host_index];
			break;
		}
	}
	if(secondary && (host_function == MSDC_SD))
		host = mtk_msdc_host[2];
	if(host == NULL){
		printk(KERN_ERR "[MSDC] This host(<host_function:%d> <boot:%d><secondary:%d>) isn't in MSDC host config list",host_function,boot,secondary);
		//BUG();
	}
	return host;
}
EXPORT_SYMBOL(msdc_get_host);

struct gendisk	* mmc_get_disk(struct mmc_card *card)
{
	struct mmc_blk_data *md;
	//struct gendisk *disk;

	BUG_ON(!card);
	md = mmc_get_drvdata(card);
	BUG_ON(!md);
	BUG_ON(!md->disk);

	return md->disk;
}

#if defined(CONFIG_MTKEMMC_BOOT) && defined(CONFIG_PROC_FS)
static struct proc_dir_entry *proc_emmc;

static inline int emmc_proc_info(char *buf, struct hd_struct *this) //to check
{

}

static int emmc_read_proc (char *page, char **start, off_t off, int count,
			  int *eof, void *data_unused)//to check
{

}


#endif /* CONFIG_MTKEMMC_BOOT && CONFIG_PROC_FS */


static u32 first_probe = 0;
static int msdc_drv_probe(struct platform_device *pdev)
{
    struct mmc_host *mmc;
    struct resource *mem;
    struct msdc_host *host;
    struct msdc_hw *hw;
    unsigned long base;
    int ret, irq;
    struct irq_data l_irq_data;

    /* Allocate MMC host for this device */
    mmc = mmc_alloc_host(sizeof(struct msdc_host), &pdev->dev);
    if (!mmc) return -ENOMEM;

    hw   = (struct msdc_hw*)pdev->dev.platform_data;
    mem  = platform_get_resource(pdev, IORESOURCE_MEM, 0);
    irq  = platform_get_irq(pdev, 0);
    base = mem->start;

    l_irq_data.irq = irq;

    BUG_ON((!hw) || (!mem) || (irq < 0));
    
    printk(KERN_ERR "yifan host%d drv probe\n",pdev->id);

    if (hw->host_function == MSDC_EMMC)
    {
	g_hynix_emmc = false;
    }

    mem = request_mem_region(mem->start, mem->end - mem->start + 1, DRV_NAME);
    if (mem == NULL) {
        mmc_free_host(mmc);
        return -EBUSY;
    }

    /* Set host parameters to mmc */
    mmc->ops        = &mt_msdc_ops;
    mmc->f_min      = HOST_MIN_MCLK;
    mmc->f_max      = HOST_MAX_MCLK;
    mmc->ocr_avail  = MSDC_OCR_AVAIL;

    /* For sd card: MSDC_SYS_SUSPEND | MSDC_WP_PIN_EN | MSDC_CD_PIN_EN | MSDC_REMOVABLE | MSDC_HIGHSPEED,
       For sdio   : MSDC_EXT_SDIO_IRQ | MSDC_HIGHSPEED */
    if (hw->flags & MSDC_HIGHSPEED) {
        mmc->caps   = MMC_CAP_MMC_HIGHSPEED | MMC_CAP_SD_HIGHSPEED;
    }
    if (hw->data_pins == 4) {
        mmc->caps  |= MMC_CAP_4_BIT_DATA;
    } else if (hw->data_pins == 8) {
        mmc->caps  |= MMC_CAP_8_BIT_DATA | MMC_CAP_4_BIT_DATA;
    }
    if ((hw->flags & MSDC_SDIO_IRQ) || (hw->flags & MSDC_EXT_SDIO_IRQ))
        mmc->caps |= MMC_CAP_SDIO_IRQ;  /* yes for sdio */
	if(hw->flags & MSDC_UHS1){
		mmc->caps |= MMC_CAP_UHS_SDR12 | MMC_CAP_UHS_SDR25 |
				MMC_CAP_UHS_SDR50 | MMC_CAP_UHS_SDR104;
		mmc->caps2 |= MMC_CAP2_HS200_1_8V_SDR;
	}
	if(hw->flags & MSDC_DDR)
				mmc->caps |= MMC_CAP_UHS_DDR50|MMC_CAP_1_8V_DDR;
	if(!(hw->flags & MSDC_REMOVABLE))
		mmc->caps |= MMC_CAP_NONREMOVABLE;

	mmc->caps |= MMC_CAP_ERASE;
	
	if(hw->host_function == MSDC_EMMC)
	mmc->caps |= MMC_CAP_CMD23;
	#ifdef CC_MTD_ENCRYPT_SUPPORT
	mmc->max_segs   = 1;
    //mmc->max_phys_segs = MAX_PHY_SGMTS;
    mmc->max_seg_size  = 512 * 1024;
    mmc->max_blk_size  = HOST_MAX_BLKSZ;
    mmc->max_req_size  = 512 * 1024;
    mmc->max_blk_count = mmc->max_req_size;
	#else
    /* MMC core transfer sizes tunable parameters */
    //mmc->max_hw_segs   = MAX_HW_SGMTS;
    mmc->max_segs   = MAX_HW_SGMTS;
    //mmc->max_phys_segs = MAX_PHY_SGMTS;
    mmc->max_seg_size  = MAX_SGMT_SZ;
    mmc->max_blk_size  = HOST_MAX_BLKSZ;
    mmc->max_req_size  = MAX_REQ_SZ;
    mmc->max_blk_count = mmc->max_req_size;
    #endif

    host = mmc_priv(mmc);
    host->hw        = hw;
    host->mmc       = mmc;
    host->id        = pdev->id;
    host->error     = 0;
    host->irq       = irq;
    host->base      = base;
    host->mclk      = 0;                   /* mclk: the request clock of mmc sub-system */
    host->sclk      = 0;                   /* sclk: the really clock after divition */
    host->pm_state  = PMSG_RESUME;
    host->suspend   = 0;
    host->card_clkon = 0;
    host->core_power = 0;
    host->power_mode = MMC_POWER_OFF;
	host->power_control = NULL;
	
	#ifdef CC_SDMMC_SUPPORT 
	MsdcSDPowerSwitch(0);//set the host ic gpio to 3.3v
	#endif
	#ifdef CC_MTD_ENCRYPT_SUPPORT
	if(host->hw->host_function == MSDC_EMMC)
	{
		msdc_aes_init();
	}
	#endif
	
    if(host->hw->host_function == MSDC_EMMC)
	{
		mmc->special_arg = MSDC_TRY_MMC;
	}

    if(host->hw->host_function == MSDC_SD)
	{
		mmc->special_arg = MSDC_TRY_SD;
	}
	
	if(host->hw->host_function == MSDC_SDIO)
	{
		mmc->special_arg = MSDC_TRY_SDIO;
	}
	


	if(host->hw->host_function == MSDC_EMMC && !(host->hw->flags & MSDC_UHS1))
		host->mmc->f_max = 50000000;
    //host->card_inserted = hw->flags & MSDC_REMOVABLE ? 0 : 1;
    host->timeout_ns = 0;
    host->timeout_clks = DEFAULT_DTOC * 1048576;
  	if(host->hw->host_function != MSDC_SDIO && !is_card_sdio(host))
		host->autocmd = MSDC_AUTOCMD12;
	else
		host->autocmd = 0;
    host->mrq = NULL;
    //init_MUTEX(&host->sem); /* we don't need to support multiple threads access */

    host->dma.used_gpd = 0;
    host->dma.used_bd = 0;

    /* using dma_alloc_coherent*/  /* todo: using 1, for all 4 slots */
    host->dma.gpd = dma_alloc_coherent(NULL, MAX_GPD_NUM * sizeof(gpd_t), &host->dma.gpd_addr, GFP_KERNEL);
    host->dma.bd =  dma_alloc_coherent(NULL, MAX_BD_NUM  * sizeof(bd_t),  &host->dma.bd_addr,  GFP_KERNEL);
    BUG_ON((!host->dma.gpd) || (!host->dma.bd));
    msdc_init_gpd_bd(host, &host->dma);
    msdc_clock_src[host->id] = hw->clk_src;
    msdc_host_mode[host->id] = mmc->caps;
    /*for emmc*/
    mtk_msdc_host[pdev->id] = host;
	host->read_time_tune = 0;
    host->write_time_tune = 0;
	host->rwcmd_time_tune = 0;
	host->write_timeout_uhs104 = 0;
	host->read_timeout_uhs104 = 0;

	host->sw_timeout = 0;
	host->tune = 0;
	host->ddr = 0;
	host->block_bad_card = 0;
	host->sd_30_busy = 0;
	host->saved_para.suspend_flag = 0;
    spin_lock_init(&host->lock);
    spin_lock_init(&host->remove_bad_card);

    msdc_init_hw(host);

#ifdef CONFIG_USE_OF
    ret = request_irq((unsigned int)irq, msdc_irq, IRQF_TRIGGER_NONE, DRV_NAME, host);
#else
    ret = request_irq((unsigned int)irq, msdc_irq, IRQF_TRIGGER_LOW, DRV_NAME, host);
#endif
    if (ret) goto release;



    platform_set_drvdata(pdev, mmc);

    ret = mmc_add_host(mmc); //will scan cards here
    if (ret) goto free_irq;
  
    first_probe++;
    if(2 == first_probe) //to check:need one timer or two timer? sd only
    {
        init_timer(&card_detect_timer);    
        card_detect_timer.data = (unsigned long)host;    
        card_detect_timer.function = msdc_tasklet_card;    
        card_detect_timer.expires = jiffies + 2 * HZ;    
        add_timer(&card_detect_timer);
    }

    return 0;

free_irq:
    free_irq(irq, host);
release:
	del_timer(&card_detect_timer);
    platform_set_drvdata(pdev, NULL);
    msdc_deinit_hw(host);

    if (mem)
        release_mem_region(mem->start, mem->end - mem->start + 1);

    mmc_free_host(mmc);

    return ret;
}


static int __init MSDCGPIO_CommonParamParsing(char *str, int *pi4Dist)
{
    char tmp[8]={0};
    char *p,*s;
    int len,i,j;
    if(strlen(str) != 0)
	{      
        printk(KERN_ERR "Parsing String = %s \n",str);
    }
    else
	{
        printk(KERN_ERR "Parse Error!!!!! string = NULL\n");
        return 0;
    }

    for (i=0; i<MSDC_GPIO_MAX_NUMBERS; i++)
	{
        s=str;
        if (i != (MSDC_GPIO_MAX_NUMBERS-1) )
		{
            if(!(p=strchr(str, ',')))
			{
                printk(KERN_ERR "Parse Error!!string format error 1\n");
                break;
            }
            if (!((len=p-s) >= 1))
            {
                printk(KERN_ERR "Parse Error!! string format error 2\n");
                break;
            }
        }
		else 
        {
            len = strlen(s);
        }
        
        for(j=0;j<len;j++)
        {
            tmp[j]=*s;
            s++;
        }
        tmp[j]=0;

        pi4Dist[i] = (int)simple_strtol(&tmp[0], NULL, 10);
        printk(KERN_ERR"Parse Done: msdc [%d] = %d \n", i, pi4Dist[i]);
        
        str += (len+1);
    }
    
    return 1;

}


static int __init MSDC_GpioParseSetup(char *str)
{
    return MSDCGPIO_CommonParamParsing(str,&MSDC_Gpio[0]);
}

__setup("msdcgpio=", MSDC_GpioParseSetup);



/* 4 device share one driver, using "drvdata" to show difference */
static int msdc_drv_remove(struct platform_device *pdev)
{
    struct mmc_host *mmc;
    struct msdc_host *host;
    struct resource *mem;

    mmc  = platform_get_drvdata(pdev);
    BUG_ON(!mmc);

    host = mmc_priv(mmc);
    BUG_ON(!host);

    ERR_MSG("removed !!!");

    platform_set_drvdata(pdev, NULL);
    mmc_remove_host(host->mmc);
    msdc_deinit_hw(host);

    free_irq(host->irq, host);

    dma_free_coherent(NULL, MAX_GPD_NUM * sizeof(gpd_t), host->dma.gpd, host->dma.gpd_addr);
    dma_free_coherent(NULL, MAX_BD_NUM  * sizeof(bd_t),  host->dma.bd,  host->dma.bd_addr);

    mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);

    if (mem)
        release_mem_region(mem->start, mem->end - mem->start + 1);

    mmc_free_host(host->mmc);

    return 0;
}

/* Fix me: Power Flow */
#ifdef CONFIG_PM
static int msdc_drv_suspend(struct platform_device *pdev, pm_message_t state)
{
    int ret = 0;
    struct mmc_host *mmc = platform_get_drvdata(pdev);
    struct msdc_host *host = mmc_priv(mmc);
    u32  base = host->base;

    if (mmc && state.event == PM_EVENT_SUSPEND && (host->hw->flags & MSDC_SYS_SUSPEND)) { /* will set for card */
        if (mmc)
        ret = mmc_suspend_host(mmc);
        msdc_clksrc_onoff(host,0);
    }
      
    return ret;
}

static int msdc_drv_resume(struct platform_device *pdev)
{
    int ret = 0;
    int retry = 3;
    int cnt = 1000;
    struct mmc_host *mmc = platform_get_drvdata(pdev);
    struct msdc_host *host = mmc_priv(mmc);
    u32 base = host->base;
    
    msdc_clksrc_onoff(host,1);
    msdc_init_hw(host);

    if (mmc && (host->hw->flags & MSDC_SYS_SUSPEND)) {/* will set for card */
        
        msdc_retry(!(sdr_read32(MSDC_CFG) & MSDC_CFG_CKSTB), retry, cnt,host->id);
		if(retry == 0)
		{
				ERR_MSG("msdc%d on clock failed ===> retry twice\n",host->id);
                  msdc_clksrc_onoff(host,0);
                  msdc_clksrc_onoff(host,1);
	    }
        ret = mmc_resume_host(mmc);
    }
		
    return ret;
}
#endif

static struct platform_driver mt_msdc_driver = {
    .probe   = msdc_drv_probe,
    .remove  = msdc_drv_remove,
#ifdef CONFIG_PM
    .suspend = msdc_drv_suspend,
    .resume  = msdc_drv_resume,
#endif
    .driver  = {
        .name  = DRV_NAME,
        .owner = THIS_MODULE,
    },
};


/*--------------------------------------------------------------------------*/
/* module init/exit                                                      */
/*--------------------------------------------------------------------------*/
static int __init mt_msdc_init(void)
{
    int ret;
    int i;

    ret = platform_driver_register(&mt_msdc_driver);
    if (ret) {
        printk(KERN_ERR DRV_NAME ": Can't register driver");
        return ret;
    }
    printk(KERN_INFO DRV_NAME ": MediaTek MSDC Driver\n");

    for (i = 0; i < ARRAY_SIZE(mt_device_msdc); i++){
        ret = platform_device_register(&mt_device_msdc[i]);
			if (ret != 0){
				return ret;
			}
		}
    msdc_init_dma_latest_address();
    msdc_debug_proc_init();
    return 0;
}

static void __exit mt_msdc_exit(void)
{
	int i,retval;
    del_timer(&card_detect_timer);
    for (i = 0; i < ARRAY_SIZE(mt_device_msdc); i++){
       platform_device_unregister(&mt_device_msdc[i]);
    }

    platform_driver_unregister(&mt_msdc_driver);
}

module_init(mt_msdc_init);
module_exit(mt_msdc_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("MediaTek SD/MMC Card Driver");
#ifdef CONFIG_MTKEMMC_BOOT
EXPORT_SYMBOL(ext_csd);
EXPORT_SYMBOL(g_emmc_mode_switch);
#endif
EXPORT_SYMBOL(mtk_msdc_host);
EXPORT_SYMBOL(msdc_bkops_is_hynix_specific_emmc);
EXPORT_SYMBOL(g_msdc_bkops_enable);
