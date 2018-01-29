/*
* linux/sound/drivers/mtk/alsa_pcm.c
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
 
/*
 *  mt85xx sound card based on streaming button sound
 */

//#define MT85XX_DEFAULT_CODE

#include <linux/init.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/jiffies.h>
#include <linux/slab.h>
#include <linux/time.h>
#include <linux/wait.h>
#include <linux/module.h>
#include <sound/core.h>
#include <sound/control.h>
#include <sound/tlv.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>

#include <sound/rawmidi.h>
#include <sound/initval.h>
#include <linux/dma-mapping.h>
#include <asm/dma.h>


#ifdef MT85XX_DEFAULT_CODE
#include "x_typedef.h"
#include "x_os.h"
#include "x_printf.h"
#include "x_assert.h"

#include "x_aud_dec.h"
#include "DspInc.h"
#include "drv_aud.h"
#else
//COMMON
typedef unsigned int UINT32;
typedef unsigned short UINT16;
typedef unsigned char UINT8;
typedef signed int INT32;
typedef bool BOOL;
//PLAYBACK
extern void AUD_InitALSAPlayback_MixSnd(UINT8 u1StreamId,UINT32 dwBuffer,UINT32 dwPAddr);
extern void AUD_DeInitALSAPlayback_MixSnd(UINT8 u1StreamId);
extern UINT32 AUD_GetMixSndFIFOStart(UINT8 u1StreamId);
extern UINT32 AUD_GetMixSndFIFOEnd(UINT8 u1StreamId);
extern UINT32 AUD_GetMixSndReadPtr(UINT8 u1StreamId);
extern void AUD_SetMixSndWritePtr(UINT8 u1StreamId, UINT32 u4WritePtr);
extern void AUD_PlayMixSndRingFifo(UINT8 u1StreamId, UINT32 u4SampleRate, UINT8 u1StereoOnOff, UINT8 u1BitDepth, UINT32 u4BufferSize);
extern UINT32 AUD_GetMixSndFIFOStartPhy(UINT8 u1StreamId);
extern UINT32 AUD_GetMixSndFIFOEndPhy(UINT8 u1StreamId);

#endif

#include "alsa_pcm.h"

#define SHOW_ALSA_LOG
#ifdef Printf
    #undef Printf
#endif    
#ifdef SHOW_ALSA_LOG
    #define Printf(fmt...)	printk(fmt)
#else
    #define Printf(fmt...)
#endif    

#ifdef MT85XX_DEFAULT_CODE
#define MAX_BUFFER_SIZE     (128 * 1024)  // streaming button sound buffer size = 128KB
#define MAX_PERIOD_SIZE     MAX_BUFFER_SIZE
#else
#define MAX_BUFFER_SIZE     (16 * 1024)
#define MAX_PERIOD_SIZE     (MAX_BUFFER_SIZE/4)
#define MIN_PERIOD_SIZE     (MAX_BUFFER_SIZE/4)
#endif

#ifdef MT85XX_DEFAULT_CODE
#define USE_FORMATS         (SNDRV_PCM_FMTBIT_S16_BE)  // button sound only support 16bit, big-endian
#define USE_RATE            SNDRV_PCM_RATE_48000
#define USE_RATE_MIN        48000
#else
#define USE_FORMATS         (SNDRV_PCM_FMTBIT_S16_LE)  // DTV mixsnd engine supports 16bit, little-endian
#define USE_RATE            (SNDRV_PCM_RATE_CONTINUOUS | SNDRV_PCM_RATE_8000_48000) //SNDRV_PCM_RATE_48000
#define USE_RATE_MIN		8000
#endif
#define USE_RATE_MAX        48000

#ifdef MT85XX_DEFAULT_CODE
#define USE_CHANNELS_MIN    1
#else
#define USE_CHANNELS_MIN    1
#endif
#define USE_CHANNELS_MAX    1

#ifdef MT85XX_DEFAULT_CODE
#define USE_PERIODS_MIN     1
#define USE_PERIODS_MAX     1024
#else
#define USE_PERIODS_MIN     4
#define USE_PERIODS_MAX     4
#endif

#define stream0  1
#define stream1  1
#define stream2  2
#define stream3  3

static int snd_card_mt85xx_pcm_playback_prepare_stream0(struct snd_pcm_substream *substream)
{
    struct snd_pcm_runtime *runtime = substream->runtime;
    struct snd_mt85xx_pcm *pcm = runtime->private_data;
    Printf("[ALSA] operator: prepare0\n");

    pcm->pcm_buffer_size = snd_pcm_lib_buffer_bytes(substream);
    pcm->pcm_period_size = snd_pcm_lib_period_bytes(substream);
    pcm->pcm_rptr = 0;
    pcm->bytes_elapsed = 0;

    // Step 1: get button sound buffer parameters
    // the parameters are determined by audio driver
    //runtime->dma_area  = (unsigned char *)AUD_GetMixSndFIFOStart(stream0);

   // runtime->dma_addr  = 0;
    //runtime->dma_addr  = AUD_GetMixSndFIFOStartPhy(stream0);
    runtime->dma_bytes = pcm->pcm_buffer_size;
    runtime->delay = 0x0;
    substream->dma_buffer.dev.type = SNDRV_DMA_TYPE_DEV;

    Printf("[ALSA] pcm->pcm_buffer_size = %d (bytes)\n", pcm->pcm_buffer_size);
    Printf("[ALSA] pcm->pcm_period_size = %d (bytes)\n", pcm->pcm_period_size);

    Printf("[ALSA] runtime->dma_area  = 0x%X\n", (unsigned int)runtime->dma_area);
    Printf("[ALSA] runtime->dma_addr  = 0x%X\n", (unsigned int)runtime->dma_addr);
    Printf("[ALSA] runtime->dma_bytes = 0x%X\n", runtime->dma_bytes);
    Printf("[ALSA] runtime->rate      = %d\n", runtime->rate);
    Printf("[ALSA] runtime->format    = %d (bitwidth = %d)\n", runtime->format, snd_pcm_format_width(runtime->format));
    Printf("[ALSA] runtime->channels  = %d\n", runtime->channels);
    Printf("[ALSA] runtime->delay     = %d (frames)\n", (int)runtime->delay);
    Printf("[ALSA] runtime->start_threshold     = %d (frames)\n", (int)runtime->start_threshold);
    //if (pcm->pcm_buffer_size > (AUD_GetMixSndFIFOEnd(stream0) - AUD_GetMixSndFIFOStart(stream0)))
    //{
    //    return -EINVAL;
    //}

    // init to silence
    snd_pcm_format_set_silence(runtime->format,
                               runtime->dma_area,
                               bytes_to_samples(runtime, runtime->dma_bytes));
    // setup button sound parameters
	AUD_PlayMixSndRingFifo(stream0, runtime->rate, (1 == runtime->channels) ? 0 : 1, runtime->sample_bits, pcm->pcm_buffer_size);

    return 0;
}


static int snd_card_mt85xx_pcm_playback_prepare_stream1(struct snd_pcm_substream *substream)
{
    struct snd_pcm_runtime *runtime = substream->runtime;
    struct snd_mt85xx_pcm *pcm = runtime->private_data;

#ifdef MT85XX_DEFAULT_CODE
    AUD_DEC_STRM_BUF_INFO_T rStrmBufInfo;
    AUD_DEC_CHANNEL_TYPE_T eChType = AUD_DEC_CHANNEL_TYPE_MONO;
#endif    

    Printf("[ALSA] operator: prepare stream1\n");

    pcm->pcm_buffer_size = snd_pcm_lib_buffer_bytes(substream);
    pcm->pcm_period_size = snd_pcm_lib_period_bytes(substream);
    pcm->pcm_rptr = 0;
    pcm->bytes_elapsed = 0;

    // Step 1: get button sound buffer parameters
    // the parameters are determined by audio driver
#ifdef MT85XX_DEFAULT_CODE
    i4AudGetBtnSndStrmBufInfo(pcm->instance, &rStrmBufInfo, FALSE);
#endif

#ifdef MT85XX_DEFAULT_CODE
    runtime->dma_area  = (unsigned char *)rStrmBufInfo.u4EffsndStrmBufSA;
#else
  #ifndef MTK_AUDIO_SUPPORT_MULTI_STREAMOUT
    runtime->dma_area  = (unsigned char *)AUD_GetMixSndFIFOStart(stream1);
  #else
    runtime->dma_area  = (unsigned char *)AUD_GetMixSndFIFOStart(substream->pcm->device);
  #endif
#endif
   // runtime->dma_addr  = 0;
    runtime->dma_addr  = AUD_GetMixSndFIFOStartPhy(stream1);
    runtime->dma_bytes = pcm->pcm_buffer_size;

    substream->dma_buffer.dev.type = SNDRV_DMA_TYPE_DEV;

    Printf("[ALSA] pcm->pcm_buffer_size = %d (bytes)\n", pcm->pcm_buffer_size);
    Printf("[ALSA] pcm->pcm_period_size = %d (bytes)\n", pcm->pcm_period_size);

    Printf("[ALSA] runtime->dma_area  = 0x%X\n", (unsigned int)runtime->dma_area);
    Printf("[ALSA] runtime->dma_addr  = 0x%X\n", (unsigned int)runtime->dma_addr);
    Printf("[ALSA] runtime->dma_bytes = 0x%X\n", runtime->dma_bytes);
    Printf("[ALSA] runtime->rate      = %d\n", runtime->rate);
    Printf("[ALSA] runtime->format    = %d (bitwidth = %d)\n", runtime->format, snd_pcm_format_width(runtime->format));
    Printf("[ALSA] runtime->channels  = %d\n", runtime->channels);
    Printf("[ALSA] runtime->delay     = %d (frames)\n", (int)runtime->delay);
    Printf("[ALSA] runtime->start_threshold     = %d (frames)\n", (int)runtime->start_threshold);

#ifdef MT85XX_DEFAULT_CODE
    if (pcm->pcm_buffer_size > rStrmBufInfo.u4EffsndStrmBufSize)
#else
  #ifndef MTK_AUDIO_SUPPORT_MULTI_STREAMOUT 
    if (pcm->pcm_buffer_size > (AUD_GetMixSndFIFOEnd(stream1) - AUD_GetMixSndFIFOStart(stream1)))
  #else
    if (pcm->pcm_buffer_size > (AUD_GetMixSndFIFOEnd(substream->pcm->device) - AUD_GetMixSndFIFOStart(substream->pcm->device)))
  #endif
#endif        
    {
        // buffer size must match
#ifdef MT85XX_DEFAULT_CODE        
        ASSERT(0);
#endif
        return -EINVAL;
    }

    // init to silence
    snd_pcm_format_set_silence(runtime->format,
                               runtime->dma_area,
                               bytes_to_samples(runtime, runtime->dma_bytes));

    // setup button sound parameters

#ifdef MT85XX_DEFAULT_CODE    
    // Step 2: setup button sound buffer DRAM
    i4AudSetBtnSndAFifo(pcm->instance,
                        (UINT32*)runtime->dma_area,
                        runtime->dma_bytes,
                        FALSE);

    // Step 3: setup sample info DRAM
    switch (runtime->channels)
    {
    case 1:
        eChType = AUD_DEC_CHANNEL_TYPE_MONO;
        break;
    case 2:
        eChType = AUD_DEC_CHANNEL_TYPE_STEREO;
        break;
    default:
        ASSERT(0);
        break;
    }

    i4AudSetBtnSndDecInfo(pcm->instance,
                          runtime->dma_bytes,
                          (UINT8)eChType,
                          runtime->rate,
                          snd_pcm_format_width(runtime->format) | 0x80); // must raise bit[7] for variable length button sound

    i4AudSetBtnSndMixGain(pcm->instance);
#else
   #ifndef MTK_AUDIO_SUPPORT_MULTI_STREAMOUT
	AUD_PlayMixSndRingFifo(stream1, runtime->rate, (1 == runtime->channels) ? 0 : 1, runtime->sample_bits, pcm->pcm_buffer_size);
   #else	
	AUD_PlayMixSndRingFifo(substream->pcm->device, runtime->rate, (1 == runtime->channels) ? 0 : 1, runtime->sample_bits, pcm->pcm_buffer_size);
   #endif	
#endif

    return 0;
}


static int snd_card_mt85xx_pcm_playback_prepare_stream2(struct snd_pcm_substream *substream)
{
    struct snd_pcm_runtime *runtime = substream->runtime;
    struct snd_mt85xx_pcm *pcm = runtime->private_data;

#ifdef MT85XX_DEFAULT_CODE
    AUD_DEC_STRM_BUF_INFO_T rStrmBufInfo;
    AUD_DEC_CHANNEL_TYPE_T eChType = AUD_DEC_CHANNEL_TYPE_MONO;
#endif    

//    Printf("[ALSA] operator: prepare2\n");

    pcm->pcm_buffer_size = snd_pcm_lib_buffer_bytes(substream);
    pcm->pcm_period_size = snd_pcm_lib_period_bytes(substream);
    pcm->pcm_rptr = 0;
    pcm->bytes_elapsed = 0;

    // Step 1: get button sound buffer parameters
    // the parameters are determined by audio driver
#ifdef MT85XX_DEFAULT_CODE
    i4AudGetBtnSndStrmBufInfo(pcm->instance, &rStrmBufInfo, FALSE);
#endif

#ifdef MT85XX_DEFAULT_CODE
    runtime->dma_area  = (unsigned char *)rStrmBufInfo.u4EffsndStrmBufSA;
#else
  #ifndef MTK_AUDIO_SUPPORT_MULTI_STREAMOUT
    runtime->dma_area  = (unsigned char *)AUD_GetMixSndFIFOStart(stream2);
  #else
    runtime->dma_area  = (unsigned char *)AUD_GetMixSndFIFOStart(substream->pcm->device);
  #endif
#endif
   // runtime->dma_addr  = 0;
    runtime->dma_addr  = AUD_GetMixSndFIFOStartPhy(stream2);
    runtime->dma_bytes = pcm->pcm_buffer_size;

    substream->dma_buffer.dev.type = SNDRV_DMA_TYPE_DEV;

    Printf("[ALSA] pcm->pcm_buffer_size = %d (bytes)\n", pcm->pcm_buffer_size);
    Printf("[ALSA] pcm->pcm_period_size = %d (bytes)\n", pcm->pcm_period_size);

    Printf("[ALSA] runtime->dma_area  = 0x%X\n", (unsigned int)runtime->dma_area);
    Printf("[ALSA] runtime->dma_addr  = 0x%X\n", (unsigned int)runtime->dma_addr);
    Printf("[ALSA] runtime->dma_bytes = 0x%X\n", runtime->dma_bytes);
    Printf("[ALSA] runtime->rate      = %d\n", runtime->rate);
    Printf("[ALSA] runtime->format    = %d (bitwidth = %d)\n", runtime->format, snd_pcm_format_width(runtime->format));
    Printf("[ALSA] runtime->channels  = %d\n", runtime->channels);
    Printf("[ALSA] runtime->delay     = %d (frames)\n", (int)runtime->delay);
    Printf("[ALSA] runtime->start_threshold     = %d (frames)\n", (int)runtime->start_threshold);

#ifdef MT85XX_DEFAULT_CODE
    if (pcm->pcm_buffer_size > rStrmBufInfo.u4EffsndStrmBufSize)
#else
  #ifndef MTK_AUDIO_SUPPORT_MULTI_STREAMOUT 
    if (pcm->pcm_buffer_size > (AUD_GetMixSndFIFOEnd(stream2) - AUD_GetMixSndFIFOStart(stream2)))
  #else
    if (pcm->pcm_buffer_size > (AUD_GetMixSndFIFOEnd(substream->pcm->device) - AUD_GetMixSndFIFOStart(substream->pcm->device)))
  #endif
#endif        
    {
        // buffer size must match
#ifdef MT85XX_DEFAULT_CODE        
        ASSERT(0);
#endif
        return -EINVAL;
    }

    // init to silence
    snd_pcm_format_set_silence(runtime->format,
                               runtime->dma_area,
                               bytes_to_samples(runtime, runtime->dma_bytes));

    // setup button sound parameters

#ifdef MT85XX_DEFAULT_CODE    
    // Step 2: setup button sound buffer DRAM
    i4AudSetBtnSndAFifo(pcm->instance,
                        (UINT32*)runtime->dma_area,
                        runtime->dma_bytes,
                        FALSE);

    // Step 3: setup sample info DRAM
    switch (runtime->channels)
    {
    case 1:
        eChType = AUD_DEC_CHANNEL_TYPE_MONO;
        break;
    case 2:
        eChType = AUD_DEC_CHANNEL_TYPE_STEREO;
        break;
    default:
        ASSERT(0);
        break;
    }

    i4AudSetBtnSndDecInfo(pcm->instance,
                          runtime->dma_bytes,
                          (UINT8)eChType,
                          runtime->rate,
                          snd_pcm_format_width(runtime->format) | 0x80); // must raise bit[7] for variable length button sound

    i4AudSetBtnSndMixGain(pcm->instance);
#else
   #ifndef MTK_AUDIO_SUPPORT_MULTI_STREAMOUT
	AUD_PlayMixSndRingFifo(stream2, runtime->rate, (1 == runtime->channels) ? 0 : 1, runtime->sample_bits, pcm->pcm_buffer_size);
   #else	
	AUD_PlayMixSndRingFifo(substream->pcm->device, runtime->rate, (1 == runtime->channels) ? 0 : 1, runtime->sample_bits, pcm->pcm_buffer_size);
   #endif	
#endif

    return 0;
}


static int snd_card_mt85xx_pcm_playback_prepare_stream3(struct snd_pcm_substream *substream)
{
    struct snd_pcm_runtime *runtime = substream->runtime;
    struct snd_mt85xx_pcm *pcm = runtime->private_data;

#ifdef MT85XX_DEFAULT_CODE
    AUD_DEC_STRM_BUF_INFO_T rStrmBufInfo;
    AUD_DEC_CHANNEL_TYPE_T eChType = AUD_DEC_CHANNEL_TYPE_MONO;
#endif    

    Printf("[ALSA] operator: prepare3\n");

    pcm->pcm_buffer_size = snd_pcm_lib_buffer_bytes(substream);
    pcm->pcm_period_size = snd_pcm_lib_period_bytes(substream);
    pcm->pcm_rptr = 0;
    pcm->bytes_elapsed = 0;

    // Step 1: get button sound buffer parameters
    // the parameters are determined by audio driver
#ifdef MT85XX_DEFAULT_CODE
    i4AudGetBtnSndStrmBufInfo(pcm->instance, &rStrmBufInfo, FALSE);
#endif

#ifdef MT85XX_DEFAULT_CODE
    runtime->dma_area  = (unsigned char *)rStrmBufInfo.u4EffsndStrmBufSA;
#else
  #ifndef MTK_AUDIO_SUPPORT_MULTI_STREAMOUT
    runtime->dma_area  = (unsigned char *)AUD_GetMixSndFIFOStart(stream3);
  #else
    runtime->dma_area  = (unsigned char *)AUD_GetMixSndFIFOStart(substream->pcm->device);
  #endif
#endif
   // runtime->dma_addr  = 0;
    runtime->dma_addr  = AUD_GetMixSndFIFOStartPhy(stream3);
    runtime->dma_bytes = pcm->pcm_buffer_size;

    substream->dma_buffer.dev.type = SNDRV_DMA_TYPE_DEV;

    Printf("[ALSA] pcm->pcm_buffer_size = %d (bytes)\n", pcm->pcm_buffer_size);
    Printf("[ALSA] pcm->pcm_period_size = %d (bytes)\n", pcm->pcm_period_size);

    Printf("[ALSA] runtime->dma_area  = 0x%X\n", (unsigned int)runtime->dma_area);
    Printf("[ALSA] runtime->dma_addr  = 0x%X\n", (unsigned int)runtime->dma_addr);
    Printf("[ALSA] runtime->dma_bytes = 0x%X\n", runtime->dma_bytes);
    Printf("[ALSA] runtime->rate      = %d\n", runtime->rate);
    Printf("[ALSA] runtime->format    = %d (bitwidth = %d)\n", runtime->format, snd_pcm_format_width(runtime->format));
    Printf("[ALSA] runtime->channels  = %d\n", runtime->channels);
    Printf("[ALSA] runtime->delay     = %d (frames)\n", (int)runtime->delay);
    Printf("[ALSA] runtime->start_threshold     = %d (frames)\n", (int)runtime->start_threshold);

#ifdef MT85XX_DEFAULT_CODE
    if (pcm->pcm_buffer_size > rStrmBufInfo.u4EffsndStrmBufSize)
#else
  #ifndef MTK_AUDIO_SUPPORT_MULTI_STREAMOUT 
    if (pcm->pcm_buffer_size > (AUD_GetMixSndFIFOEnd(stream3) - AUD_GetMixSndFIFOStart(stream3)))
  #else
    if (pcm->pcm_buffer_size > (AUD_GetMixSndFIFOEnd(substream->pcm->device) - AUD_GetMixSndFIFOStart(substream->pcm->device)))
  #endif
#endif        
    {
        // buffer size must match
#ifdef MT85XX_DEFAULT_CODE        
        ASSERT(0);
#endif
        return -EINVAL;
    }

    // init to silence
    snd_pcm_format_set_silence(runtime->format,
                               runtime->dma_area,
                               bytes_to_samples(runtime, runtime->dma_bytes));

    // setup button sound parameters

#ifdef MT85XX_DEFAULT_CODE    
    // Step 2: setup button sound buffer DRAM
    i4AudSetBtnSndAFifo(pcm->instance,
                        (UINT32*)runtime->dma_area,
                        runtime->dma_bytes,
                        FALSE);

    // Step 3: setup sample info DRAM
    switch (runtime->channels)
    {
    case 1:
        eChType = AUD_DEC_CHANNEL_TYPE_MONO;
        break;
    case 2:
        eChType = AUD_DEC_CHANNEL_TYPE_STEREO;
        break;
    default:
        ASSERT(0);
        break;
    }

    i4AudSetBtnSndDecInfo(pcm->instance,
                          runtime->dma_bytes,
                          (UINT8)eChType,
                          runtime->rate,
                          snd_pcm_format_width(runtime->format) | 0x80); // must raise bit[7] for variable length button sound

    i4AudSetBtnSndMixGain(pcm->instance);
#else
   #ifndef MTK_AUDIO_SUPPORT_MULTI_STREAMOUT
	AUD_PlayMixSndRingFifo(stream3, runtime->rate, (1 == runtime->channels) ? 0 : 1, runtime->sample_bits, pcm->pcm_buffer_size);
   #else	
	AUD_PlayMixSndRingFifo(substream->pcm->device, runtime->rate, (1 == runtime->channels) ? 0 : 1, runtime->sample_bits, pcm->pcm_buffer_size);
   #endif	
#endif

    return 0;
}

static void snd_card_mt85xx_pcm_playback_timer_start(struct snd_mt85xx_pcm *pcm)
{
 //  Printf("[ALSA] operator: playback_timer_start0\n");
   //2012/12/7 added by daniel
    if (pcm->timer_started)
    {
        del_timer_sync(&pcm->timer);
        pcm->timer_started = 0;
    }

    pcm->timer.expires = jiffies + pcm->pcm_period_size * HZ / (pcm->substream->runtime->rate * pcm->substream->runtime->channels * 2);

    add_timer(&pcm->timer);
    //2012/12/7 added by daniel
    pcm->timer_started = 1;

}

static void snd_card_mt85xx_pcm_playback_timer_stop(struct snd_mt85xx_pcm *pcm)
{
 //   Printf("[ALSA] operator: playback_timer_stop0\n");
    //2012/12/7 added by daniel
    del_timer(&pcm->timer);

    if (del_timer(&pcm->timer)==1)
    {
        pcm->timer_started = 0;
    }
}

static void snd_card_mt85xx_pcm_playback_update_write_pointer_stream0(struct snd_mt85xx_pcm *pcm)
{
    struct snd_pcm_runtime *runtime = pcm->substream->runtime;
    unsigned int pcm_wptr = frames_to_bytes(runtime, runtime->control->appl_ptr % runtime->buffer_size);
 //   Printf("[ALSA] operator: write pointer0\n");
 //   Printf("[ALSA] pcm_wptr= %d,pcm->pcm_rptr=%x,runtime->control->appl_ptr=%d,runtime->buffer_size=%d\n",pcm_wptr,pcm->pcm_rptr,(int)runtime->control->appl_ptr,(int)(runtime->buffer_size));
    if (pcm_wptr == pcm->pcm_rptr)
    {
        // check if buffer full
        if (0 == snd_pcm_playback_avail(runtime)) 
        {
     //       Printf("[ALSA] operator: write pointer0 AUD_SetMixSndWritePtr\n");
            AUD_SetMixSndWritePtr(stream0, AUD_GetMixSndFIFOStartPhy(stream0) + ((pcm->pcm_rptr + pcm->pcm_buffer_size - 1) % pcm->pcm_buffer_size));
        } 
        else 
        {
          // no need process now, will be updated at next timer
     //       Printf("[ALSA] timer: buffer empty\n");
        }
    }
    else
    {
    //    Printf("[ALSA] operator: write pointer0 AUD_SetMixSndWritePtrsd\n");
        AUD_SetMixSndWritePtr(stream0, AUD_GetMixSndFIFOStartPhy(stream0) + pcm_wptr);
    }
}

static void snd_card_mt85xx_pcm_playback_update_write_pointer_stream1(struct snd_mt85xx_pcm *pcm)
{
    struct snd_pcm_runtime *runtime = pcm->substream->runtime;
    unsigned int pcm_wptr = frames_to_bytes(runtime, runtime->control->appl_ptr % runtime->buffer_size);
 //   Printf("[ALSA] operator: pointer1\n");
 #if 0 //added by ling for test , be carefule, it cause system busy -> noise !!!
    printk("[ALSA] @@@@@@@@@ ML-----snd_card_mt85xx_pcm_playback_update_write_pointer () @@@@@@@@ \n");
 #endif


    if (pcm_wptr == pcm->pcm_rptr)
    {
        // check if buffer full
        if (0 == snd_pcm_playback_avail(runtime)) {
#ifdef MT85XX_DEFAULT_CODE
            i4AudSetBtnSndWp(pcm->instance, (pcm->pcm_rptr + pcm->pcm_buffer_size - 1) % pcm->pcm_buffer_size);
#else
        #ifndef MTK_AUDIO_SUPPORT_MULTI_STREAMOUT 
            AUD_SetMixSndWritePtr(stream1, AUD_GetMixSndFIFOStart(stream1) + ((pcm->pcm_rptr + pcm->pcm_buffer_size - 1) % pcm->pcm_buffer_size));
        #else
            AUD_SetMixSndWritePtr(pcm->substream->pcm->device, AUD_GetMixSndFIFOStart(pcm->substream->pcm->device) + ((pcm->pcm_rptr + pcm->pcm_buffer_size - 1) % pcm->pcm_buffer_size));
        #endif
#endif
        } else {
          // no need process now, will be updated at next timer
          // Printf("[ALSA] timer: buffer empty\n");
        }
    }
    else
    {
#ifdef MT85XX_DEFAULT_CODE    
        i4AudSetBtnSndWp(pcm->instance, pcm_wptr);
#else
   #ifndef MTK_AUDIO_SUPPORT_MULTI_STREAMOUT
        AUD_SetMixSndWritePtr(stream1, AUD_GetMixSndFIFOStart(stream1) + pcm_wptr);
   #else
        AUD_SetMixSndWritePtr(pcm->substream->pcm->device, AUD_GetMixSndFIFOStart(pcm->substream->pcm->device) + pcm_wptr);
   #endif
#endif
    }
}


static void snd_card_mt85xx_pcm_playback_update_write_pointer_stream2(struct snd_mt85xx_pcm *pcm)
{
    struct snd_pcm_runtime *runtime = pcm->substream->runtime;
    unsigned int pcm_wptr = frames_to_bytes(runtime, runtime->control->appl_ptr % runtime->buffer_size);
 //   Printf("[ALSA] operator: pointer2\n");
 #if 0 //added by ling for test , be carefule, it cause system busy -> noise !!!
    printk("[ALSA] @@@@@@@@@ ML-----snd_card_mt85xx_pcm_playback_update_write_pointer () @@@@@@@@ \n");
 #endif


    if (pcm_wptr == pcm->pcm_rptr)
    {
        // check if buffer full
        if (0 == snd_pcm_playback_avail(runtime)) {
#ifdef MT85XX_DEFAULT_CODE
            i4AudSetBtnSndWp(pcm->instance, (pcm->pcm_rptr + pcm->pcm_buffer_size - 1) % pcm->pcm_buffer_size);
#else
        #ifndef MTK_AUDIO_SUPPORT_MULTI_STREAMOUT 
            AUD_SetMixSndWritePtr(stream2, AUD_GetMixSndFIFOStart(stream2) + ((pcm->pcm_rptr + pcm->pcm_buffer_size - 1) % pcm->pcm_buffer_size));
        #else
            AUD_SetMixSndWritePtr(pcm->substream->pcm->device, AUD_GetMixSndFIFOStart(pcm->substream->pcm->device) + ((pcm->pcm_rptr + pcm->pcm_buffer_size - 1) % pcm->pcm_buffer_size));
        #endif
#endif
        } else {
          // no need process now, will be updated at next timer
          // Printf("[ALSA] timer: buffer empty\n");
        }
    }
    else
    {
#ifdef MT85XX_DEFAULT_CODE    
        i4AudSetBtnSndWp(pcm->instance, pcm_wptr);
#else
   #ifndef MTK_AUDIO_SUPPORT_MULTI_STREAMOUT
        AUD_SetMixSndWritePtr(stream2, AUD_GetMixSndFIFOStart(stream2) + pcm_wptr);
   #else
        AUD_SetMixSndWritePtr(pcm->substream->pcm->device, AUD_GetMixSndFIFOStart(pcm->substream->pcm->device) + pcm_wptr);
   #endif
#endif
    }
}


static void snd_card_mt85xx_pcm_playback_update_write_pointer_stream3(struct snd_mt85xx_pcm *pcm)
{
    struct snd_pcm_runtime *runtime = pcm->substream->runtime;
    unsigned int pcm_wptr = frames_to_bytes(runtime, runtime->control->appl_ptr % runtime->buffer_size);
//    Printf("[ALSA] operator: pointer3\n");
 #if 0 //added by ling for test , be carefule, it cause system busy -> noise !!!
    printk("[ALSA] @@@@@@@@@ ML-----snd_card_mt85xx_pcm_playback_update_write_pointer () @@@@@@@@ \n");
 #endif


    if (pcm_wptr == pcm->pcm_rptr)
    {
        // check if buffer full
        if (0 == snd_pcm_playback_avail(runtime)) {
#ifdef MT85XX_DEFAULT_CODE
            i4AudSetBtnSndWp(pcm->instance, (pcm->pcm_rptr + pcm->pcm_buffer_size - 1) % pcm->pcm_buffer_size);
#else
        #ifndef MTK_AUDIO_SUPPORT_MULTI_STREAMOUT 
            AUD_SetMixSndWritePtr(stream3, AUD_GetMixSndFIFOStart(stream3) + ((pcm->pcm_rptr + pcm->pcm_buffer_size - 1) % pcm->pcm_buffer_size));
        #else
            AUD_SetMixSndWritePtr(pcm->substream->pcm->device, AUD_GetMixSndFIFOStart(pcm->substream->pcm->device) + ((pcm->pcm_rptr + pcm->pcm_buffer_size - 1) % pcm->pcm_buffer_size));
        #endif
#endif
        } else {
          // no need process now, will be updated at next timer
          // Printf("[ALSA] timer: buffer empty\n");
        }
    }
    else
    {
#ifdef MT85XX_DEFAULT_CODE    
        i4AudSetBtnSndWp(pcm->instance, pcm_wptr);
#else
   #ifndef MTK_AUDIO_SUPPORT_MULTI_STREAMOUT
        AUD_SetMixSndWritePtr(stream3, AUD_GetMixSndFIFOStart(stream3) + pcm_wptr);
   #else
        AUD_SetMixSndWritePtr(pcm->substream->pcm->device, AUD_GetMixSndFIFOStart(pcm->substream->pcm->device) + pcm_wptr);
   #endif
#endif
    }
}

static int snd_card_mt85xx_pcm_playback_trigger_stream0(struct snd_pcm_substream *substream, int cmd)
{
    struct snd_pcm_runtime *runtime = substream->runtime;
    struct snd_mt85xx_pcm *pcm = runtime->private_data;
    int err = 0;

//    Printf("[ALSA] operator: trigger0, cmd = %d\n", cmd);

    spin_lock(&pcm->lock);
    switch (cmd) 
    {
        case SNDRV_PCM_TRIGGER_START:
            snd_card_mt85xx_pcm_playback_update_write_pointer_stream0(pcm);

            snd_card_mt85xx_pcm_playback_timer_start(pcm);
            break;
        case SNDRV_PCM_TRIGGER_STOP:
            snd_card_mt85xx_pcm_playback_timer_stop(pcm);
            break;
        case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
            break;
        case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
            break;
        default:
            err = -EINVAL;
            break;
    }
    spin_unlock(&pcm->lock);
    return 0;
}

static int snd_card_mt85xx_pcm_playback_trigger_stream1(struct snd_pcm_substream *substream, int cmd)
{
    struct snd_pcm_runtime *runtime = substream->runtime;
    struct snd_mt85xx_pcm *pcm = runtime->private_data;
    int err = 0;

//    Printf("[ALSA] operator: trigger1, cmd = %d\n", cmd);

    spin_lock(&pcm->lock);
    switch (cmd) {
    case SNDRV_PCM_TRIGGER_START:
        snd_card_mt85xx_pcm_playback_update_write_pointer_stream1(pcm);
#ifdef MT85XX_DEFAULT_CODE            
        AUD_DSPCmdEffSndPlay(pcm->instance);
#endif
        snd_card_mt85xx_pcm_playback_timer_start(pcm);
        break;
    case SNDRV_PCM_TRIGGER_STOP:
        snd_card_mt85xx_pcm_playback_timer_stop(pcm);
#ifdef MT85XX_DEFAULT_CODE
        AUD_DSPCmdEffSndStop(pcm->instance);
#endif
        break;
    case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
        break;
    case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
        break;
    default:
        err = -EINVAL;
        break;
    }
    spin_unlock(&pcm->lock);
    return 0;
}


static int snd_card_mt85xx_pcm_playback_trigger_stream2(struct snd_pcm_substream *substream, int cmd)
{
    struct snd_pcm_runtime *runtime = substream->runtime;
    struct snd_mt85xx_pcm *pcm = runtime->private_data;
    int err = 0;

//    Printf("[ALSA] operator: trigger2, cmd = %d\n", cmd);

    spin_lock(&pcm->lock);
    switch (cmd) {
    case SNDRV_PCM_TRIGGER_START:
        snd_card_mt85xx_pcm_playback_update_write_pointer_stream2(pcm);
#ifdef MT85XX_DEFAULT_CODE            
        AUD_DSPCmdEffSndPlay(pcm->instance);
#endif
        snd_card_mt85xx_pcm_playback_timer_start(pcm);
        break;
    case SNDRV_PCM_TRIGGER_STOP:
        snd_card_mt85xx_pcm_playback_timer_stop(pcm);
#ifdef MT85XX_DEFAULT_CODE
        AUD_DSPCmdEffSndStop(pcm->instance);
#endif
        break;
    case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
        break;
    case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
        break;
    default:
        err = -EINVAL;
        break;
    }
    spin_unlock(&pcm->lock);
    return 0;
}


static int snd_card_mt85xx_pcm_playback_trigger_stream3(struct snd_pcm_substream *substream, int cmd)
{
    struct snd_pcm_runtime *runtime = substream->runtime;
    struct snd_mt85xx_pcm *pcm = runtime->private_data;
    int err = 0;

//    Printf("[ALSA] operator: trigger3, cmd = %d\n", cmd);

    spin_lock(&pcm->lock);
    switch (cmd) {
    case SNDRV_PCM_TRIGGER_START:
        snd_card_mt85xx_pcm_playback_update_write_pointer_stream3(pcm);
#ifdef MT85XX_DEFAULT_CODE            
        AUD_DSPCmdEffSndPlay(pcm->instance);
#endif
        snd_card_mt85xx_pcm_playback_timer_start(pcm);
        break;
    case SNDRV_PCM_TRIGGER_STOP:
        snd_card_mt85xx_pcm_playback_timer_stop(pcm);
#ifdef MT85XX_DEFAULT_CODE
        AUD_DSPCmdEffSndStop(pcm->instance);
#endif
        break;
    case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
        break;
    case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
        break;
    default:
        err = -EINVAL;
        break;
    }
    spin_unlock(&pcm->lock);
    return 0;
}


static void snd_card_mt85xx_pcm_playback_timer_function_stream0(unsigned long data)
{
    struct snd_mt85xx_pcm *pcm = (struct snd_mt85xx_pcm *)data;
    unsigned long flags;
    unsigned int pcm_old_rptr = pcm->pcm_rptr;
    unsigned int bytes_elapsed;

    spin_lock_irqsave(&pcm->lock, flags);

    // setup next timer
    pcm->timer.expires = jiffies + pcm->pcm_period_size * HZ / (pcm->substream->runtime->rate * pcm->substream->runtime->channels * 2);        
    add_timer(&pcm->timer);

    // STEP 1: refresh read pointer
    //pcm->pcm_rptr = (AUD_GetMixSndReadPtr(stream0)- AUD_GetMixSndFIFOStartPhy(stream0))- AUD_GetMixSndFIFOStart(stream0);
    pcm->pcm_rptr = AUD_GetMixSndReadPtr(stream0)- AUD_GetMixSndFIFOStartPhy(stream0);

    // STEP 2: update write pointer
    snd_card_mt85xx_pcm_playback_update_write_pointer_stream0(pcm);

    // STEP 3: check period
    bytes_elapsed = pcm->pcm_rptr - pcm_old_rptr + pcm->pcm_buffer_size;
    bytes_elapsed %= pcm->pcm_buffer_size;
    pcm->bytes_elapsed += bytes_elapsed;

//    Printf("[ALSA] timer: pcm->bytes_elapsed = %d\n", pcm->bytes_elapsed);

    if (pcm->bytes_elapsed >= pcm->pcm_period_size)
    {
   //     Printf("[ALSA] timer: pcm->bytes_elapsed = %d,pcm->pcm_period_size=%d\n", pcm->bytes_elapsed,pcm->pcm_period_size);
        pcm->bytes_elapsed %= pcm->pcm_period_size;
        spin_unlock_irqrestore(&pcm->lock, flags);

        snd_pcm_period_elapsed(pcm->substream);
    }
    else
    {
        spin_unlock_irqrestore(&pcm->lock, flags);
    }
}



static void snd_card_mt85xx_pcm_playback_timer_function_stream1(unsigned long data)
{
    struct snd_mt85xx_pcm *pcm = (struct snd_mt85xx_pcm *)data;
    unsigned long flags;
    unsigned int pcm_old_rptr = pcm->pcm_rptr;
    unsigned int bytes_elapsed;

    spin_lock_irqsave(&pcm->lock, flags);

    // setup next timer
#ifdef MT85XX_DEFAULT_CODE    
    pcm->timer.expires = 10 + jiffies; // 10ms later
#else
    pcm->timer.expires = jiffies + pcm->pcm_period_size * HZ / (pcm->substream->runtime->rate * pcm->substream->runtime->channels * 2);        
#endif
    add_timer(&pcm->timer);

    // STEP 1: refresh read pointer
#ifdef MT85XX_DEFAULT_CODE    
    pcm->pcm_rptr = i4AudGetBtnSndReadOffset(pcm->instance);
#else
  #ifndef MTK_AUDIO_SUPPORT_MULTI_STREAMOUT 
    pcm->pcm_rptr = AUD_GetMixSndReadPtr(stream1) - AUD_GetMixSndFIFOStart(stream1);
  #else
    pcm->pcm_rptr = AUD_GetMixSndReadPtr(pcm->substream->pcm->device) - AUD_GetMixSndFIFOStart(pcm->substream->pcm->device);
  #endif
#endif

    // STEP 2: update write pointer
    snd_card_mt85xx_pcm_playback_update_write_pointer_stream1(pcm);

    // STEP 3: check period
    bytes_elapsed = pcm->pcm_rptr - pcm_old_rptr + pcm->pcm_buffer_size;
    bytes_elapsed %= pcm->pcm_buffer_size;
    pcm->bytes_elapsed += bytes_elapsed;

//     Printf("[ALSA] timer: pcm->bytes_elapsed = %d\n", pcm->bytes_elapsed);

    if (pcm->bytes_elapsed >= pcm->pcm_period_size)
    {
        pcm->bytes_elapsed %= pcm->pcm_period_size;
        spin_unlock_irqrestore(&pcm->lock, flags);

        snd_pcm_period_elapsed(pcm->substream);
    }
    else
    {
        spin_unlock_irqrestore(&pcm->lock, flags);
    }
}



static void snd_card_mt85xx_pcm_playback_timer_function_stream2(unsigned long data)
{
    struct snd_mt85xx_pcm *pcm = (struct snd_mt85xx_pcm *)data;
    unsigned long flags;
    unsigned int pcm_old_rptr = pcm->pcm_rptr;
    unsigned int bytes_elapsed;

    spin_lock_irqsave(&pcm->lock, flags);

    // setup next timer
#ifdef MT85XX_DEFAULT_CODE    
    pcm->timer.expires = 10 + jiffies; // 10ms later
#else
    pcm->timer.expires = jiffies + pcm->pcm_period_size * HZ / (pcm->substream->runtime->rate * pcm->substream->runtime->channels * 2);        
#endif
    add_timer(&pcm->timer);

    // STEP 1: refresh read pointer
#ifdef MT85XX_DEFAULT_CODE    
    pcm->pcm_rptr = i4AudGetBtnSndReadOffset(pcm->instance);
#else
  #ifndef MTK_AUDIO_SUPPORT_MULTI_STREAMOUT 
    pcm->pcm_rptr = AUD_GetMixSndReadPtr(stream2) - AUD_GetMixSndFIFOStart(stream2);
  #else
    pcm->pcm_rptr = AUD_GetMixSndReadPtr(pcm->substream->pcm->device) - AUD_GetMixSndFIFOStart(pcm->substream->pcm->device);
  #endif
#endif

    // STEP 2: update write pointer
    snd_card_mt85xx_pcm_playback_update_write_pointer_stream2(pcm);

    // STEP 3: check period
    bytes_elapsed = pcm->pcm_rptr - pcm_old_rptr + pcm->pcm_buffer_size;
    bytes_elapsed %= pcm->pcm_buffer_size;
    pcm->bytes_elapsed += bytes_elapsed;

//     Printf("[ALSA] timer: pcm->bytes_elapsed = %d\n", pcm->bytes_elapsed);

    if (pcm->bytes_elapsed >= pcm->pcm_period_size)
    {
        pcm->bytes_elapsed %= pcm->pcm_period_size;
        spin_unlock_irqrestore(&pcm->lock, flags);

        snd_pcm_period_elapsed(pcm->substream);
    }
    else
    {
        spin_unlock_irqrestore(&pcm->lock, flags);
    }
}



static void snd_card_mt85xx_pcm_playback_timer_function_stream3(unsigned long data)
{
    struct snd_mt85xx_pcm *pcm = (struct snd_mt85xx_pcm *)data;
    unsigned long flags;
    unsigned int pcm_old_rptr = pcm->pcm_rptr;
    unsigned int bytes_elapsed;

    spin_lock_irqsave(&pcm->lock, flags);

    // setup next timer
#ifdef MT85XX_DEFAULT_CODE    
    pcm->timer.expires = 10 + jiffies; // 10ms later
#else
    pcm->timer.expires = jiffies + pcm->pcm_period_size * HZ / (pcm->substream->runtime->rate * pcm->substream->runtime->channels * 2);        
#endif
    add_timer(&pcm->timer);

    // STEP 1: refresh read pointer
#ifdef MT85XX_DEFAULT_CODE    
    pcm->pcm_rptr = i4AudGetBtnSndReadOffset(pcm->instance);
#else
  #ifndef MTK_AUDIO_SUPPORT_MULTI_STREAMOUT 
    pcm->pcm_rptr = AUD_GetMixSndReadPtr(stream3) - AUD_GetMixSndFIFOStart(stream3);
  #else
    pcm->pcm_rptr = AUD_GetMixSndReadPtr(pcm->substream->pcm->device) - AUD_GetMixSndFIFOStart(pcm->substream->pcm->device);
  #endif
#endif

    // STEP 2: update write pointer
    snd_card_mt85xx_pcm_playback_update_write_pointer_stream3(pcm);

    // STEP 3: check period
    bytes_elapsed = pcm->pcm_rptr - pcm_old_rptr + pcm->pcm_buffer_size;
    bytes_elapsed %= pcm->pcm_buffer_size;
    pcm->bytes_elapsed += bytes_elapsed;

//     Printf("[ALSA] timer: pcm->bytes_elapsed = %d\n", pcm->bytes_elapsed);

    if (pcm->bytes_elapsed >= pcm->pcm_period_size)
    {
        pcm->bytes_elapsed %= pcm->pcm_period_size;
        spin_unlock_irqrestore(&pcm->lock, flags);

        snd_pcm_period_elapsed(pcm->substream);
    }
    else
    {
        spin_unlock_irqrestore(&pcm->lock, flags);
    }
}

static snd_pcm_uframes_t snd_card_mt85xx_pcm_playback_pointer_stream0(struct snd_pcm_substream *substream)
{
    struct snd_pcm_runtime *runtime = substream->runtime;
    struct snd_mt85xx_pcm *pcm = runtime->private_data;

//     Printf("[ALSA] operator: pointer0,pcm->pcm_rptr=%d,runtime->frame_bits=%d\n",pcm->pcm_rptr,runtime->frame_bits);

    return bytes_to_frames(runtime, pcm->pcm_rptr);
}
static snd_pcm_uframes_t snd_card_mt85xx_pcm_playback_pointer_stream1(struct snd_pcm_substream *substream)
{
    struct snd_pcm_runtime *runtime = substream->runtime;
    struct snd_mt85xx_pcm *pcm = runtime->private_data;

     Printf("[ALSA] operator: pointer1\n");

    return bytes_to_frames(runtime, pcm->pcm_rptr);
}
static snd_pcm_uframes_t snd_card_mt85xx_pcm_playback_pointer_stream2(struct snd_pcm_substream *substream)
{
    struct snd_pcm_runtime *runtime = substream->runtime;
    struct snd_mt85xx_pcm *pcm = runtime->private_data;

    Printf("[ALSA] operator: pointer2\n");

    return bytes_to_frames(runtime, pcm->pcm_rptr);
}
static snd_pcm_uframes_t snd_card_mt85xx_pcm_playback_pointer_stream3(struct snd_pcm_substream *substream)
{
    struct snd_pcm_runtime *runtime = substream->runtime;
    struct snd_mt85xx_pcm *pcm = runtime->private_data;

    Printf("[ALSA] operator: pointer3\n");

    return bytes_to_frames(runtime, pcm->pcm_rptr);
}





static struct snd_pcm_hardware snd_card_mt85xx_pcm_playback_hw =
{
    .info =            (SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED |
                        SNDRV_PCM_INFO_PAUSE | SNDRV_PCM_INFO_MMAP_VALID | SNDRV_PCM_INFO_BLOCK_TRANSFER),
    .formats =          USE_FORMATS,
    .rates =            USE_RATE,
    .rate_min =         USE_RATE_MIN,
    .rate_max =         USE_RATE_MAX,
    .channels_min =     USE_CHANNELS_MIN,
    .channels_max =     USE_CHANNELS_MAX,
    .buffer_bytes_max = MAX_BUFFER_SIZE,
#ifdef MT85XX_DEFAULT_CODE    
    .period_bytes_min = 64,
#else
    .period_bytes_min = MIN_PERIOD_SIZE,
#endif
    .period_bytes_max = MAX_PERIOD_SIZE,
    .periods_min =      USE_PERIODS_MIN,
    .periods_max =      USE_PERIODS_MAX,
    .fifo_size =        0,
};

static void snd_card_mt85xx_runtime_free(struct snd_pcm_runtime *runtime)
{
    kfree(runtime->private_data);
}

static int snd_card_mt85xx_pcm_playback_hw_params_stream0(struct snd_pcm_substream *substream,
                    struct snd_pcm_hw_params *hw_params)
{
        struct snd_pcm_runtime *runtime = substream->runtime;
        struct snd_mt85xx_pcm *pcm = runtime->private_data; 
        int ret = 0;
        unsigned int channels, rate, format, period_bytes, buffer_bytes;
        size_t dwSize = MAX_BUFFER_SIZE;
        //AUD_DeInitALSAPlayback_MixSnd(stream0);
        format = params_format(hw_params);
        rate = params_rate(hw_params);
        channels = params_channels(hw_params);
        period_bytes = params_period_bytes(hw_params);
        buffer_bytes = params_buffer_bytes(hw_params);
        Printf("[ALSA] operator: hw_params stream0\n");
        printk("format %d, rate %d, channels %d, period_bytes %d, buffer_bytes %d\n",
                format, rate, channels, period_bytes, buffer_bytes);
        pcm->pcm_buffer_size = snd_pcm_lib_buffer_bytes(substream);
        pcm->pcm_period_size = snd_pcm_lib_period_bytes(substream);
        pcm->pcm_rptr = 0;
        pcm->bytes_elapsed = 0;
        substream->dma_buffer.dev.type = SNDRV_DMA_TYPE_DEV;
       // runtime->dma_area  = (unsigned char *)AUD_GetMixSndFIFOStart(stream0);
       // runtime->dma_addr  = AUD_GetMixSndFIFOStartPhy(stream0);
        ret = snd_pcm_lib_malloc_pages(substream,dwSize);
        if(ret < 0)
        {
            Printf("[ALSA] snd_pcm_lib_malloc_pages error=%x\n",ret);
            return ret;
        }
        AUD_InitALSAPlayback_MixSnd(stream0,(UINT32)(runtime->dma_area),(UINT32)(runtime->dma_addr));
    return 0;
}
static int snd_card_mt85xx_pcm_playback_hw_params_stream1(struct snd_pcm_substream *substream,
                    struct snd_pcm_hw_params *hw_params)
{
        struct snd_pcm_runtime *runtime = substream->runtime;
        struct snd_mt85xx_pcm *pcm = runtime->private_data; 
    
       Printf("[ALSA] operator: hw_params stream1\n");
    
        pcm->pcm_buffer_size = snd_pcm_lib_buffer_bytes(substream);
        pcm->pcm_period_size = snd_pcm_lib_period_bytes(substream);
        pcm->pcm_rptr = 0;
        pcm->bytes_elapsed = 0;
    
#ifdef MT85XX_DEFAULT_CODE
        runtime->dma_area  = (unsigned char *)rStrmBufInfo.u4EffsndStrmBufSA;
#else
  #ifndef MTK_AUDIO_SUPPORT_MULTI_STREAMOUT
        runtime->dma_area  = (unsigned char *)AUD_GetMixSndFIFOStart(stream1);
  #else
        runtime->dma_area  = (unsigned char *)AUD_GetMixSndFIFOStart(substream->pcm->device);
  #endif
#endif

        runtime->dma_addr  = AUD_GetMixSndFIFOStartPhy(stream1);
        substream->dma_buffer.dev.type = SNDRV_DMA_TYPE_DEV;

    return 0;
}
static int snd_card_mt85xx_pcm_playback_hw_params_stream2(struct snd_pcm_substream *substream,
                    struct snd_pcm_hw_params *hw_params)
{
        struct snd_pcm_runtime *runtime = substream->runtime;
        struct snd_mt85xx_pcm *pcm = runtime->private_data; 
    
       Printf("[ALSA] operator: hw_params stream1\n");
    
        pcm->pcm_buffer_size = snd_pcm_lib_buffer_bytes(substream);
        pcm->pcm_period_size = snd_pcm_lib_period_bytes(substream);
        pcm->pcm_rptr = 0;
        pcm->bytes_elapsed = 0;
    
#ifdef MT85XX_DEFAULT_CODE
        runtime->dma_area  = (unsigned char *)rStrmBufInfo.u4EffsndStrmBufSA;
#else
  #ifndef MTK_AUDIO_SUPPORT_MULTI_STREAMOUT
        runtime->dma_area  = (unsigned char *)AUD_GetMixSndFIFOStart(stream2);
  #else
        runtime->dma_area  = (unsigned char *)AUD_GetMixSndFIFOStart(substream->pcm->device);
  #endif
#endif

        runtime->dma_addr  = AUD_GetMixSndFIFOStartPhy(stream2);
        substream->dma_buffer.dev.type = SNDRV_DMA_TYPE_DEV;
    return 0;
}
static int snd_card_mt85xx_pcm_playback_hw_params_stream3(struct snd_pcm_substream *substream,
                    struct snd_pcm_hw_params *hw_params)
{
        struct snd_pcm_runtime *runtime = substream->runtime;
        struct snd_mt85xx_pcm *pcm = runtime->private_data; 

       Printf("[ALSA] operator: hw_params stream3\n");
    
        pcm->pcm_buffer_size = snd_pcm_lib_buffer_bytes(substream);
        pcm->pcm_period_size = snd_pcm_lib_period_bytes(substream);
        pcm->pcm_rptr = 0;
        pcm->bytes_elapsed = 0;
    
#ifdef MT85XX_DEFAULT_CODE
        runtime->dma_area  = (unsigned char *)rStrmBufInfo.u4EffsndStrmBufSA;
#else
  #ifndef MTK_AUDIO_SUPPORT_MULTI_STREAMOUT
        runtime->dma_area  = (unsigned char *)AUD_GetMixSndFIFOStart(stream3);
  #else
        runtime->dma_area  = (unsigned char *)AUD_GetMixSndFIFOStart(substream->pcm->device);
  #endif
#endif

        runtime->dma_addr  = AUD_GetMixSndFIFOStartPhy(stream3);
        substream->dma_buffer.dev.type = SNDRV_DMA_TYPE_DEV;
    return 0;
}




static int snd_card_mt85xx_pcm_playback_buffer_bytes_rule(struct snd_pcm_hw_params *params,
					  struct snd_pcm_hw_rule *rule)
{
#if 1
    struct snd_interval *buffer_bytes;
   
    // fix buffer size to 128KB
    buffer_bytes = hw_param_interval(params, SNDRV_PCM_HW_PARAM_BUFFER_BYTES);
    // patch for android audio
    // allow alsa/oss client to shrink buffer size
    buffer_bytes->min = 512;
    buffer_bytes->max = MAX_BUFFER_SIZE;
    Printf("[ALSA] buffer bytes rule\n");
#endif
    return 0;
}

static int snd_card_mt85xx_pcm_playback_hw_free_stream0(struct snd_pcm_substream *substream)
{
    Printf("[ALSA] operator: hw_free\n");
    snd_pcm_lib_free_pages(substream);
    return 0;
}

static int snd_card_mt85xx_pcm_playback_hw_free_stream1(struct snd_pcm_substream *substream)
{
    Printf("[ALSA] operator: hw_free\n");

    return 0;
}
static int snd_card_mt85xx_pcm_playback_hw_free_stream2(struct snd_pcm_substream *substream)
{
    Printf("[ALSA] operator: hw_free\n");

    return 0;
}
static int snd_card_mt85xx_pcm_playback_hw_free_stream3(struct snd_pcm_substream *substream)
{
    Printf("[ALSA] operator: hw_free\n");

    return 0;
}

static struct snd_mt85xx_pcm *new_pcm_playback_stream_stream0(struct snd_pcm_substream *substream)
{
    struct snd_mt85xx_pcm *pcm;

    pcm = kzalloc(sizeof(*pcm), GFP_KERNEL);
    if (! pcm)
        return pcm;

    init_timer(&pcm->timer);
    pcm->timer.data = (unsigned long) pcm;
    pcm->timer.function = snd_card_mt85xx_pcm_playback_timer_function_stream0;

    spin_lock_init(&pcm->lock);

    pcm->substream = substream;
#ifdef MT85XX_DEFAULT_CODE
    pcm->instance = MIXSND0 + stream0;
#else
    pcm->instance = stream0;
#endif

    return pcm;
}
static struct snd_mt85xx_pcm *new_pcm_playback_stream_stream1(struct snd_pcm_substream *substream)
{
    struct snd_mt85xx_pcm *pcm;

    pcm = kzalloc(sizeof(*pcm), GFP_KERNEL);
    if (! pcm)
        return pcm;

    init_timer(&pcm->timer);
    pcm->timer.data = (unsigned long) pcm;
    pcm->timer.function = snd_card_mt85xx_pcm_playback_timer_function_stream1;

    spin_lock_init(&pcm->lock);

    pcm->substream = substream;
#ifdef MT85XX_DEFAULT_CODE
    pcm->instance = MIXSND0 + stream1;
#else
    pcm->instance = stream1;
#endif

    return pcm;
}
static struct snd_mt85xx_pcm *new_pcm_playback_stream_stream2(struct snd_pcm_substream *substream)
{
    struct snd_mt85xx_pcm *pcm;

    pcm = kzalloc(sizeof(*pcm), GFP_KERNEL);
    if (! pcm)
        return pcm;

    init_timer(&pcm->timer);
    pcm->timer.data = (unsigned long) pcm;
    pcm->timer.function = snd_card_mt85xx_pcm_playback_timer_function_stream2;

    spin_lock_init(&pcm->lock);

    pcm->substream = substream;
#ifdef MT85XX_DEFAULT_CODE
    pcm->instance = MIXSND0 + stream2;
#else
    pcm->instance = stream2;
#endif

    return pcm;
}
static struct snd_mt85xx_pcm *new_pcm_playback_stream_stream3(struct snd_pcm_substream *substream)
{
    struct snd_mt85xx_pcm *pcm;

    pcm = kzalloc(sizeof(*pcm), GFP_KERNEL);
    if (! pcm)
        return pcm;

    init_timer(&pcm->timer);
    pcm->timer.data = (unsigned long) pcm;
    pcm->timer.function = snd_card_mt85xx_pcm_playback_timer_function_stream3;

    spin_lock_init(&pcm->lock);

    pcm->substream = substream;
#ifdef MT85XX_DEFAULT_CODE
    pcm->instance = MIXSND0 + stream3;
#else
    pcm->instance = stream3;
#endif

    return pcm;
}


static int snd_card_mt85xx_pcm_playback_open_stream0(struct snd_pcm_substream *substream)
{
    struct snd_pcm_runtime *runtime = substream->runtime;
    struct snd_mt85xx_pcm *pcm;
    int err;
    //size_t dwSize = MAX_BUFFER_SIZE;
    
    if ((pcm = new_pcm_playback_stream_stream0(substream)) == NULL)
        return -ENOMEM;
    
    runtime->private_data = pcm;
    runtime->private_free = snd_card_mt85xx_runtime_free;
    runtime->hw = snd_card_mt85xx_pcm_playback_hw;

    Printf("[ALSA] operator: open, substream = 0x%X\n", (unsigned int) substream);
    Printf("[ALSA] substream->pcm->device = 0x%X\n", substream->pcm->device);
    Printf("[ALSA] substream->number = %d\n", stream0);
    //substream->dma_buffer.dev.type = SNDRV_DMA_TYPE_DEV;
    AUD_DeInitALSAPlayback_MixSnd(stream0);
    if (substream->pcm->device & 1) 
    {
        runtime->hw.info &= ~SNDRV_PCM_INFO_INTERLEAVED;
        runtime->hw.info |= SNDRV_PCM_INFO_NONINTERLEAVED;
    }
    if (substream->pcm->device & 2)
        runtime->hw.info &= ~(SNDRV_PCM_INFO_MMAP|SNDRV_PCM_INFO_MMAP_VALID);

    // add constraint rules
    err = snd_pcm_hw_rule_add(runtime, 0, SNDRV_PCM_HW_PARAM_BUFFER_BYTES,
                              snd_card_mt85xx_pcm_playback_buffer_bytes_rule, NULL,
                              SNDRV_PCM_HW_PARAM_CHANNELS, -1);
    //AUD_InitALSAPlayback_MixSnd(stream0,(UINT32)(runtime->dma_area));
    
    return 0;
}


static int snd_card_mt85xx_pcm_playback_open_stream1(struct snd_pcm_substream *substream)
{
    struct snd_pcm_runtime *runtime = substream->runtime;
    struct snd_mt85xx_pcm *pcm;
    int err;

    if ((pcm = new_pcm_playback_stream_stream1(substream)) == NULL)
        return -ENOMEM;

    runtime->private_data = pcm;
    runtime->private_free = snd_card_mt85xx_runtime_free;
    runtime->hw = snd_card_mt85xx_pcm_playback_hw;

    Printf("[ALSA] operator: open, substream = 0x%X\n", (unsigned int) substream);
    Printf("[ALSA] substream->pcm->device = 0x%X\n", substream->pcm->device);
    Printf("[ALSA] substream->number = %d\n", stream1);

#ifndef MTK_AUDIO_SUPPORT_MULTI_STREAMOUT
    if (substream->pcm->device & 1) {
        runtime->hw.info &= ~SNDRV_PCM_INFO_INTERLEAVED;
        runtime->hw.info |= SNDRV_PCM_INFO_NONINTERLEAVED;
    }
    if (substream->pcm->device & 2)
        runtime->hw.info &= ~(SNDRV_PCM_INFO_MMAP|SNDRV_PCM_INFO_MMAP_VALID);
#endif

    // add constraint rules
    err = snd_pcm_hw_rule_add(runtime, 0, SNDRV_PCM_HW_PARAM_BUFFER_BYTES,
                              snd_card_mt85xx_pcm_playback_buffer_bytes_rule, NULL,
                              SNDRV_PCM_HW_PARAM_CHANNELS, -1);

#ifndef MT85XX_DEFAULT_CODE
 #ifndef MTK_AUDIO_SUPPORT_MULTI_STREAMOUT
    //AUD_InitALSAPlayback_MixSnd(stream1,(UINT32)(runtime->dma_area));
 #else
    AUD_InitALSAPlayback_MixSnd(substream->pcm->device);
 #endif
#endif
    return 0;
}

static int snd_card_mt85xx_pcm_playback_open_stream2(struct snd_pcm_substream *substream)
{
    struct snd_pcm_runtime *runtime = substream->runtime;
    struct snd_mt85xx_pcm *pcm;
    int err;

    if ((pcm = new_pcm_playback_stream_stream2(substream)) == NULL)
        return -ENOMEM;

    runtime->private_data = pcm;
    runtime->private_free = snd_card_mt85xx_runtime_free;
    runtime->hw = snd_card_mt85xx_pcm_playback_hw;

    Printf("[ALSA] operator: open, substream = 0x%X\n", (unsigned int) substream);
    Printf("[ALSA] substream->pcm->device = 0x%X\n", substream->pcm->device);
    Printf("[ALSA] substream->number = %d\n", stream2);

#ifndef MTK_AUDIO_SUPPORT_MULTI_STREAMOUT
    if (substream->pcm->device & 1) {
        runtime->hw.info &= ~SNDRV_PCM_INFO_INTERLEAVED;
        runtime->hw.info |= SNDRV_PCM_INFO_NONINTERLEAVED;
    }
    if (substream->pcm->device & 2)
        runtime->hw.info &= ~(SNDRV_PCM_INFO_MMAP|SNDRV_PCM_INFO_MMAP_VALID);
#endif

    // add constraint rules
    err = snd_pcm_hw_rule_add(runtime, 0, SNDRV_PCM_HW_PARAM_BUFFER_BYTES,
                              snd_card_mt85xx_pcm_playback_buffer_bytes_rule, NULL,
                              SNDRV_PCM_HW_PARAM_CHANNELS, -1);

#ifndef MT85XX_DEFAULT_CODE
 #ifndef MTK_AUDIO_SUPPORT_MULTI_STREAMOUT
    //AUD_InitALSAPlayback_MixSnd(stream2,(UINT32)(runtime->dma_area));
 #else
    AUD_InitALSAPlayback_MixSnd(substream->pcm->device);
 #endif
#endif
    return 0;
}



static int snd_card_mt85xx_pcm_playback_open_stream3(struct snd_pcm_substream *substream)
{
    struct snd_pcm_runtime *runtime = substream->runtime;
    struct snd_mt85xx_pcm *pcm;
    int err;

    if ((pcm = new_pcm_playback_stream_stream3(substream)) == NULL)
        return -ENOMEM;

    runtime->private_data = pcm;
    runtime->private_free = snd_card_mt85xx_runtime_free;
    runtime->hw = snd_card_mt85xx_pcm_playback_hw;

    Printf("[ALSA] operator: open, substream = 0x%X\n", (unsigned int) substream);
    Printf("[ALSA] substream->pcm->device = 0x%X\n", substream->pcm->device);
    Printf("[ALSA] substream->number = %d\n", stream3);

#ifndef MTK_AUDIO_SUPPORT_MULTI_STREAMOUT
    if (substream->pcm->device & 1) {
        runtime->hw.info &= ~SNDRV_PCM_INFO_INTERLEAVED;
        runtime->hw.info |= SNDRV_PCM_INFO_NONINTERLEAVED;
    }
    if (substream->pcm->device & 2)
        runtime->hw.info &= ~(SNDRV_PCM_INFO_MMAP|SNDRV_PCM_INFO_MMAP_VALID);
#endif

    // add constraint rules
    err = snd_pcm_hw_rule_add(runtime, 0, SNDRV_PCM_HW_PARAM_BUFFER_BYTES,
                              snd_card_mt85xx_pcm_playback_buffer_bytes_rule, NULL,
                              SNDRV_PCM_HW_PARAM_CHANNELS, -1);

#ifndef MT85XX_DEFAULT_CODE
 #ifndef MTK_AUDIO_SUPPORT_MULTI_STREAMOUT
    //AUD_InitALSAPlayback_MixSnd(stream3,(UINT32)(runtime->dma_area));
 #else
    AUD_InitALSAPlayback_MixSnd(substream->pcm->device);
 #endif
#endif
    return 0;
}

static int snd_card_mt85xx_pcm_playback_close_stream0(struct snd_pcm_substream *substream)
{
#ifndef MT85XX_DEFAULT_CODE //2012/12/7 added by daniel
    struct snd_pcm_runtime *runtime = substream->runtime;
    struct snd_mt85xx_pcm *pcm = runtime->private_data;
    if (pcm->timer_started)
    {
        del_timer_sync(&pcm->timer);
        pcm->timer_started = 0;
    }
#endif

    Printf("[ALSA] operator: close, substream = 0x%X\n", (unsigned int) substream);

#ifndef MT85XX_DEFAULT_CODE
  #ifndef MTK_AUDIO_SUPPORT_MULTI_STREAMOUT
     //AUD_DeInitALSAPlayback_MixSnd(stream0);
  #else
     AUD_DeInitALSAPlayback_MixSnd(substream->pcm->device);
  #endif
#endif
    return 0;
}

static int snd_card_mt85xx_pcm_playback_close_stream1(struct snd_pcm_substream *substream)
{
#ifndef MT85XX_DEFAULT_CODE //2012/12/7 added by daniel
    struct snd_pcm_runtime *runtime = substream->runtime;
    struct snd_mt85xx_pcm *pcm = runtime->private_data;
    if (pcm->timer_started)
    {
        del_timer_sync(&pcm->timer);
        pcm->timer_started = 0;
    }
#endif

    Printf("[ALSA] operator: close, substream = 0x%X\n", (unsigned int) substream);

#ifndef MT85XX_DEFAULT_CODE
  #ifndef MTK_AUDIO_SUPPORT_MULTI_STREAMOUT
    AUD_DeInitALSAPlayback_MixSnd(stream1);
  #else
     AUD_DeInitALSAPlayback_MixSnd(substream->pcm->device);
  #endif
#endif
    return 0;
}

static int snd_card_mt85xx_pcm_playback_close_stream2(struct snd_pcm_substream *substream)
{
#ifndef MT85XX_DEFAULT_CODE //2012/12/7 added by daniel
    struct snd_pcm_runtime *runtime = substream->runtime;
    struct snd_mt85xx_pcm *pcm = runtime->private_data;
    if (pcm->timer_started)
    {
        del_timer_sync(&pcm->timer);
        pcm->timer_started = 0;
    }
#endif

    Printf("[ALSA] operator: close, substream = 0x%X\n", (unsigned int) substream);

#ifndef MT85XX_DEFAULT_CODE
  #ifndef MTK_AUDIO_SUPPORT_MULTI_STREAMOUT
    AUD_DeInitALSAPlayback_MixSnd(stream2);
  #else
     AUD_DeInitALSAPlayback_MixSnd(substream->pcm->device);
  #endif
#endif
    return 0;
}

static int snd_card_mt85xx_pcm_playback_close_stream3(struct snd_pcm_substream *substream)
{
#ifndef MT85XX_DEFAULT_CODE //2012/12/7 added by daniel
    struct snd_pcm_runtime *runtime = substream->runtime;
    struct snd_mt85xx_pcm *pcm = runtime->private_data;
    if (pcm->timer_started)
    {
        del_timer_sync(&pcm->timer);
        pcm->timer_started = 0;
    }
#endif

    Printf("[ALSA] operator: close, substream = 0x%X\n", (unsigned int) substream);

#ifndef MT85XX_DEFAULT_CODE
  #ifndef MTK_AUDIO_SUPPORT_MULTI_STREAMOUT
    AUD_DeInitALSAPlayback_MixSnd(stream3);
  #else
     AUD_DeInitALSAPlayback_MixSnd(substream->pcm->device);
  #endif
#endif
    return 0;
}

static int snd_card_mt85xx_pcm_playback_mmap_stream0(struct snd_pcm_substream *substream, struct vm_area_struct *vma)
{
	unsigned long off, start;
	u32 len;
	off = vma->vm_pgoff << PAGE_SHIFT;
	//start = (unsigned long)AUD_GetMixSndFIFOStartPhy(stream0); //info->fix.smem_start;
    start = substream->runtime->dma_addr;
    //info->fix.smem_len;
	len = PAGE_ALIGN(start & ~PAGE_MASK) + 0x4000;
	start &= PAGE_MASK;
	if ((vma->vm_end - vma->vm_start + off) > len)
	{
		return -EINVAL;
	}
	off += start;
	vma->vm_pgoff = off >> PAGE_SHIFT;

	// This is an IO map - tell maydump to skip this VMA 
	vma->vm_flags |= VM_IO;

	vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);
    Printf("[ALSA] operator: mmap_stream0 vma->vm_page_prot=%x\n",vma->vm_page_prot);
	/*
	 * Don't alter the page protection flags; we want to keep the area
	 * cached for better performance.  This does mean that we may miss
	 * some updates to the screen occasionally, but process switches
	 * should cause the caches and buffers to be flushed often enough.
	 */
	if (io_remap_pfn_range(vma, vma->vm_start, (off >> PAGE_SHIFT),
				vma->vm_end - vma->vm_start,
				vma->vm_page_prot))  
	{
		return -EAGAIN;
	}

	return 0;
}


static struct snd_pcm_ops snd_card_mt85xx_playback_ops[] = {
    {
        .open      = snd_card_mt85xx_pcm_playback_open_stream0,
        .close     = snd_card_mt85xx_pcm_playback_close_stream0,
        .ioctl     = snd_pcm_lib_ioctl,
        .hw_params = snd_card_mt85xx_pcm_playback_hw_params_stream0,
        .hw_free   = snd_card_mt85xx_pcm_playback_hw_free_stream0,
        .prepare   = snd_card_mt85xx_pcm_playback_prepare_stream0,
        .trigger   = snd_card_mt85xx_pcm_playback_trigger_stream0,
        .pointer   = snd_card_mt85xx_pcm_playback_pointer_stream0,
        .mmap      = snd_card_mt85xx_pcm_playback_mmap_stream0,
    },
    {
        .open      = snd_card_mt85xx_pcm_playback_open_stream1,
        .close     = snd_card_mt85xx_pcm_playback_close_stream1,
        .ioctl     = snd_pcm_lib_ioctl,
        .hw_params = snd_card_mt85xx_pcm_playback_hw_params_stream1,
        .hw_free   = snd_card_mt85xx_pcm_playback_hw_free_stream1,
        .prepare   = snd_card_mt85xx_pcm_playback_prepare_stream1,
        .trigger   = snd_card_mt85xx_pcm_playback_trigger_stream1,
        .pointer   = snd_card_mt85xx_pcm_playback_pointer_stream1,
    },
    {
        .open      = snd_card_mt85xx_pcm_playback_open_stream2,
        .close     = snd_card_mt85xx_pcm_playback_close_stream2,
        .ioctl     = snd_pcm_lib_ioctl,
        .hw_params = snd_card_mt85xx_pcm_playback_hw_params_stream2,
        .hw_free   = snd_card_mt85xx_pcm_playback_hw_free_stream2,
        .prepare   = snd_card_mt85xx_pcm_playback_prepare_stream2,
        .trigger   = snd_card_mt85xx_pcm_playback_trigger_stream2,
        .pointer   = snd_card_mt85xx_pcm_playback_pointer_stream2,
    },
    {
        .open      = snd_card_mt85xx_pcm_playback_open_stream3,
        .close     = snd_card_mt85xx_pcm_playback_close_stream3,
        .ioctl     = snd_pcm_lib_ioctl,
        .hw_params = snd_card_mt85xx_pcm_playback_hw_params_stream3,
        .hw_free   = snd_card_mt85xx_pcm_playback_hw_free_stream3,
        .prepare   = snd_card_mt85xx_pcm_playback_prepare_stream3,
        .trigger   = snd_card_mt85xx_pcm_playback_trigger_stream3,
        .pointer   = snd_card_mt85xx_pcm_playback_pointer_stream3,
    }
    
};

//Dan Zhou add on 20111125
static int snd_card_mt85xx_pcm_capture_open(struct snd_pcm_substream *substream)
{
    Printf("[ALSA]snd_card_mt85xx_pcm_capture_open----> operator: open, substream = 0x%X\n", (unsigned int) substream);
	
    return EPERM;
}

static int snd_card_mt85xx_pcm_capture_close(struct snd_pcm_substream *substream)
{
    Printf("[ALSA]snd_card_mt85xx_pcm_capture_close----> operator: close, substream = 0x%X\n", (unsigned int) substream);

    return 0;
}

static struct snd_pcm_ops snd_card_mt85xx_capture_ops = {
    .open      = snd_card_mt85xx_pcm_capture_open,
    .close     = snd_card_mt85xx_pcm_capture_close,
    .ioctl     = snd_pcm_lib_ioctl,
};

int __devinit snd_card_mt85xx_pcm(struct snd_mt85xx *mt85xx, int device, int substreams)
{
    struct snd_pcm *pcm;
    int err;
    printk("[ALSA]snd_card_mt85xx_pcm device is %d\n",device);
    if ((err = snd_pcm_new(mt85xx->card, "mt85xx PCM", device,
                   substreams, substreams, &pcm)) < 0)
        return err;

    mt85xx->pcm = pcm;

    //snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &snd_card_mt85xx_playback_ops);
    snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &(snd_card_mt85xx_playback_ops[device]));
    //Dan Zhou add on 20111125
    snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &snd_card_mt85xx_capture_ops);

    pcm->private_data = mt85xx;
    pcm->info_flags = 0;
#ifdef MT85XX_DEFAULT_CODE    
    strcpy(pcm->name, "mt85xx PCM");
#else
    strcpy(pcm->name, "mtk PCM");
#endif

    return 0;
}


