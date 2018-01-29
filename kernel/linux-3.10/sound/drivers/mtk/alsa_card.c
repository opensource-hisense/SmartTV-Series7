/*
* linux/sound/drivers/mtk/alsa_card.c
*
* MTK Sound Card Driver
*
* Copyright (c) 2010-2012 MediaTek Inc.
* $Author: dexi.tang $
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
*/

//#define MT85XX_DEFAULT_CODE

#ifdef MT85XX_DEFAULT_CODE
#include "x_module.h"
#include "x_rm.h"
#include "x_printf.h"
#endif

#include <linux/init.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/jiffies.h>
#include <linux/slab.h>
#include <linux/time.h>
#include <linux/wait.h>
#include <linux/module.h>
#include <linux/switch.h>
#include <linux/workqueue.h>

#include <sound/core.h>
#include <sound/control.h>
#include <sound/tlv.h>
#include <sound/pcm.h>
#include <sound/rawmidi.h>
#include <sound/initval.h>

#include "alsa_pcm.h"

#ifdef MT85XX_DEFAULT_CODE
#define MAX_PCM_DEVICES     4
#define MAX_PCM_SUBSTREAMS  8
#else
  #ifndef MTK_AUDIO_SUPPORT_MULTI_STREAMOUT 
#define MAX_PCM_DEVICES     1
#define MAX_PCM_SUBSTREAMS  2
  #else
#define MAX_PCM_DEVICES     1
#define MAX_PCM_SUBSTREAMS  2
  #endif
#endif

static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;  /* Index 0-MAX */
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;   /* ID for this card */

//static int index[SNDRV_CARDS] = {0,1,2,3,4,5,6,7};  /* Index 0-MAX */
//static char *id[SNDRV_CARDS] = {"MT5890Dexi",NULL};   /* ID for this card */

static int enable[SNDRV_CARDS] = {1, [1 ... (SNDRV_CARDS - 1)] = 0};

#ifndef MTK_AUDIO_SUPPORT_MULTI_STREAMOUT
static int pcm_devs[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS - 1)] = 4};
#else
static int pcm_devs[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS - 1)] = 4};
#endif

static struct platform_device *devices[SNDRV_CARDS];
//#define HP_DETECT_ANDROID
#ifdef HP_DETECT_ANDROID
static struct timer_list hp_detect_timer;
static struct switch_dev hp_switch;
static int hp_state = 0;
static struct work_struct hp_detect_work;
static struct workqueue_struct *hp_detect_workqueue;
extern char isHpPlugIn(void);

static void hp_detect_work_callback(struct work_struct *work) {
	printk("[HP_DETECT]hp_detect_work_callback receive work new send uevent\n");
	switch_set_state((struct switch_dev *)&hp_switch, hp_state);
}
static void do_detect(unsigned long para) {
	int ret = 0;
	//int tmp_state = isHpPlugIn()?2:0;
	int tmp_state;
	char tmp = isHpPlugIn();
	if(tmp == (char)1)
	{
	    tmp_state = 2;
	}else{
        tmp_state = 0;
	}
	//printk(KERN_DEBUG "[HP_DETECT]do_detect tmp_state:%d\n",tmp_state);
	if (tmp_state != hp_state) {
		printk("[HP_DETECT]headphone state changed from %d to %d\n", hp_state, tmp_state);
		hp_state = tmp_state;
		ret = queue_work(hp_detect_workqueue, &hp_detect_work);
		if (!ret) {
			printk("[HP_DETECT]queue_work return:%d\n",ret);
		}
	}
	del_timer(&hp_detect_timer);
	hp_detect_timer.data = (unsigned long) 1;
	hp_detect_timer.function = do_detect;
	hp_detect_timer.expires = jiffies + HZ/10;
	add_timer(&hp_detect_timer);
}

static void hp_timer_init(void) {
	int ret = 0;
	printk("[HP_DETECT]hp_timer_init\n");

	INIT_WORK(&hp_detect_work, hp_detect_work_callback);
	hp_detect_workqueue = create_singlethread_workqueue("hp_detect");
	hp_switch.name = "h2w";
	hp_switch.index = 0;
	hp_switch.state = 0;

	ret = switch_dev_register(&hp_switch);
	if (ret != 0) {
		printk("[HP_DETECT]register switch dev error ret:%d \n",ret);
	}
	
	init_timer(&hp_detect_timer);
	hp_detect_timer.data = (unsigned long) 1;
	hp_detect_timer.function = do_detect;
	hp_detect_timer.expires = jiffies + HZ/10;
	add_timer(&hp_detect_timer);
}
#endif

static int __devinit snd_mt85xx_probe(struct platform_device *devptr)
{
    struct snd_card *card;
    struct snd_mt85xx *mt85xx;
    int idx, err;
    int dev = devptr->id;

    printk("[ALSA] probe: devptr->id = %d\n", devptr->id);

  /*  err = snd_card_create(index[dev], id[dev], THIS_MODULE,
                  sizeof(struct snd_mt85xx), &card);*/
    err = snd_card_create(index[dev], id[dev], THIS_MODULE,
                  sizeof(struct snd_mt85xx), &card);
    if (err < 0)
        return err;

    mt85xx = card->private_data;
    mt85xx->card = card;

#ifdef MTK_AUDIO_SUPPORT_MULTI_STREAMOUT
    printk("[ALSA] pcm_devs[dev] = %d\n", pcm_devs[dev]);
#endif
    for (idx = 0; idx < MAX_PCM_DEVICES && idx < pcm_devs[dev]; idx++)
    {
        if ((err = snd_card_mt85xx_pcm(mt85xx, idx, MAX_PCM_SUBSTREAMS)) < 0)
            goto __nodev;
    }

    //if ((err = snd_card_mt85xx_new_mixer(dummy)) < 0)
    //    goto __nodev;

#ifdef MT85XX_DEFAULT_CODE
    strcpy(card->driver, "mt85xx");
    strcpy(card->shortname, "mt85xx");
    sprintf(card->longname, "mt85xx %i", dev + 1);
#else
    strcpy(card->driver, "mtk-hisense");
    strcpy(card->shortname, "mtk");
    sprintf(card->longname, "mtk %i", dev + 1);
#endif

    snd_card_set_dev(card, &devptr->dev);

    if ((err = snd_card_register(card)) == 0) {
        platform_set_drvdata(devptr, card);
        return 0;
    }

__nodev:
    snd_card_free(card);
    return err;
}

static int __devexit snd_mt85xx_remove(struct platform_device *devptr)
{
    snd_card_free(platform_get_drvdata(devptr));
    platform_set_drvdata(devptr, NULL);
    return 0;
}

#define SND_MT85XX_DRIVER    "snd_mt85xx"

static struct platform_driver snd_mt85xx_driver = {
    .probe      = snd_mt85xx_probe,
    .remove     = __devexit_p(snd_mt85xx_remove),

    .driver     = {
        .name   = SND_MT85XX_DRIVER
    },
};

static void snd_mt85xx_unregister_all(void)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(devices); ++i)
        platform_device_unregister(devices[i]);
    platform_driver_unregister(&snd_mt85xx_driver);
}

static int __init alsa_card_mt85xx_init(void)
{
    int i, cards, err;

    if ((err = platform_driver_register(&snd_mt85xx_driver)) < 0)
        return err;

    cards = 0;
    for (i = 0; i < SNDRV_CARDS; i++) {
        struct platform_device *device;
        if (! enable[i])
            continue;
        device = platform_device_register_simple(SND_MT85XX_DRIVER,
                             i, NULL, 0);
        if (IS_ERR(device))
            continue;
        if (!platform_get_drvdata(device)) {
            platform_device_unregister(device);
            continue;
        }
        devices[i] = device;
        cards++;
    }

    if (!cards) {
    #ifdef MODULE
        printk(KERN_ERR "mt85xx soundcard not found or device busy\n");
    #endif
        snd_mt85xx_unregister_all();
        return -ENODEV;
    }

    return 0;
}

static void __exit alsa_card_mt85xx_exit(void)
{
    snd_mt85xx_unregister_all();
}

static int __init alsa_mt85xx_init(void)
{
    printk(KERN_ERR "[ALSA] alsa_mt85xx_init().\n");

    alsa_card_mt85xx_init();
	
#ifdef HP_DETECT_ANDROID
	hp_timer_init();
#endif
    return 0;
}

static void __exit alsa_mt85xx_exit(void)
{
    printk(KERN_ERR "[ALSA] alsa_mt85xx_exit().\n");

    alsa_card_mt85xx_exit();
#ifdef HP_DETECT_ANDROID
		switch_dev_unregister(&hp_switch);
#endif
}

#ifdef MT85XX_DEFAULT_CODE
DECLARE_MODULE(alsa_mt85xx);
#endif

MODULE_DESCRIPTION("mt85xx soundcard");
MODULE_LICENSE("GPL");

#ifndef MT85XX_DEFAULT_CODE
module_init(alsa_mt85xx_init)
module_exit(alsa_mt85xx_exit)
#endif

