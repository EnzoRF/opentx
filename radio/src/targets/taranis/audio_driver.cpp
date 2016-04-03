/*
 * Copyright (C) OpenTX
 *
 * Based on code named
 *   th9x - http://code.google.com/p/th9x 
 *   er9x - http://code.google.com/p/er9x
 *   gruvin9x - http://code.google.com/p/gruvin9x
 *
 * License GPLv2: http://www.gnu.org/licenses/gpl-2.0.html
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include "../../opentx.h"

#if !defined(SIMU)
bool dacIdle = true;

void setSampleRate(uint32_t frequency)
{
  register uint32_t timer = (PERI1_FREQUENCY * TIMER_MULT_APB1) / frequency - 1 ;         // MCK/8 and 100 000 Hz

  AUDIO_TIMER->CR1 &= ~TIM_CR1_CEN ;
  AUDIO_TIMER->CNT = 0 ;
  AUDIO_TIMER->ARR = limit<uint32_t>(2, timer, 65535) ;
  AUDIO_TIMER->CR1 |= TIM_CR1_CEN ;
}

// Start TIMER6 at 100000Hz, used for DAC trigger
void dacTimerInit()
{
  AUDIO_TIMER->PSC = 0 ;                                                                                                 // Max speed
  AUDIO_TIMER->ARR = (PERI1_FREQUENCY * TIMER_MULT_APB1) / 100000 - 1 ;        // 10 uS, 100 kHz
  AUDIO_TIMER->CR2 = 0 ;
  AUDIO_TIMER->CR2 = 0x20 ;
  AUDIO_TIMER->CR1 = TIM_CR1_CEN ;
}

// Configure DAC0 (or DAC1 for REVA)
// Not sure why PB14 has not be allocated to the DAC, although it is an EXTRA function
// So maybe it is automatically done
void dacInit()
{
  dacTimerInit();

  configure_pins( GPIO_Pin_4, PIN_ANALOG | PIN_PORTA ) ;

  // Chan 7, 16-bit wide, Medium priority, memory increments
  AUDIO_DMA_Stream->CR &= ~DMA_SxCR_EN ;              // Disable DMA
  AUDIO_DMA->HIFCR = DMA_HIFCR_CTCIF5 | DMA_HIFCR_CHTIF5 | DMA_HIFCR_CTEIF5 | DMA_HIFCR_CDMEIF5 | DMA_HIFCR_CFEIF5 ; // Write ones to clear bits
  AUDIO_DMA_Stream->CR = DMA_SxCR_CHSEL_0 | DMA_SxCR_CHSEL_1 | DMA_SxCR_CHSEL_2 | DMA_SxCR_PL_0 |
                     DMA_SxCR_MSIZE_0 | DMA_SxCR_PSIZE_0 | DMA_SxCR_MINC | DMA_SxCR_DIR_0 | DMA_SxCR_CIRC ;
  AUDIO_DMA_Stream->PAR = CONVERT_PTR_UINT(&DAC->DHR12R1);
  // AUDIO_DMA_Stream->M0AR = CONVERT_PTR_UINT(Sine_values);
  AUDIO_DMA_Stream->FCR = 0x05 ; //DMA_SxFCR_DMDIS | DMA_SxFCR_FTH_0 ;
  // AUDIO_DMA_Stream->NDTR = 100 ;

  DAC->DHR12R1 = 2010 ;
  DAC->SR = DAC_SR_DMAUDR1 ;              // Write 1 to clear flag
  DAC->CR = DAC_CR_TEN1 | DAC_CR_EN1 ;                    // Enable DAC
  NVIC_EnableIRQ(AUDIO_TIM_IRQn); // TODO needed?
  NVIC_SetPriority(AUDIO_TIM_IRQn, 7);
  NVIC_EnableIRQ(AUDIO_DMA_Stream_IRQn);
  NVIC_SetPriority(AUDIO_DMA_Stream_IRQn, 7);
}

void audioPushBuffer(AudioBuffer * buffer)
{
  if (dacIdle) {
    dacIdle = false;
    buffer->state = AUDIO_BUFFER_PLAYING;
    AUDIO_DMA_Stream->CR &= ~DMA_SxCR_EN ;                              // Disable DMA channel
    AUDIO_DMA->HIFCR = DMA_HIFCR_CTCIF5 | DMA_HIFCR_CHTIF5 | DMA_HIFCR_CTEIF5 | DMA_HIFCR_CDMEIF5 | DMA_HIFCR_CFEIF5 ; // Write ones to clear bits
    AUDIO_DMA_Stream->M0AR = CONVERT_PTR_UINT(buffer->data);
    AUDIO_DMA_Stream->NDTR = buffer->size;
    AUDIO_DMA_Stream->CR |= DMA_SxCR_EN | DMA_SxCR_TCIE ;               // Enable DMA channel and interrupt
    DAC->SR = DAC_SR_DMAUDR1 ;                      // Write 1 to clear flag
    DAC->CR |= DAC_CR_EN1 | DAC_CR_DMAEN1 ;                 // Enable DAC
  else {
    buffer->state = AUDIO_BUFFER_FILLED;
  }
}

void dacStart()
{
  AUDIO_DMA->HIFCR = DMA_HIFCR_CTCIF5 | DMA_HIFCR_CHTIF5 | DMA_HIFCR_CTEIF5 | DMA_HIFCR_CDMEIF5 | DMA_HIFCR_CFEIF5 ; // Write ones to clear bits
  AUDIO_DMA_Stream->CR |= DMA_SxCR_CIRC | DMA_SxCR_EN ;                               // Enable DMA channel
  DAC->SR = DAC_SR_DMAUDR1 ;                      // Write 1 to clear flag
  DAC->CR |= DAC_CR_EN1 | DAC_CR_DMAEN1 ;                 // Enable DAC
}

void dacStop()
{
  AUDIO_DMA_Stream->CR &= ~DMA_SxCR_CIRC ;
}


// Sound routines
void audioInit()
{
  dacInit();
}

void audioEnd()
{
  DAC->CR = 0 ;
  AUDIO_TIMER->CR1 = 0 ;
  // Also need to turn off any possible interrupts
  NVIC_DisableIRQ(AUDIO_TIM_IRQn) ;
  NVIC_DisableIRQ(AUDIO_DMA_Stream_IRQn) ;
}

extern "C" void AUDIO_TIM_IRQHandler()
{
  DEBUG_INTERRUPT(INT_AUDIO);
  DAC->CR &= ~DAC_CR_DMAEN1 ;     // Stop DMA requests
#if !defined(REV9E)
  DAC->CR &= ~DAC_CR_DMAUDRIE1 ;  // Stop underrun interrupt
#endif
  DAC->SR = DAC_SR_DMAUDR1 ;      // Write 1 to clear flag
}

extern "C" void AUDIO_DMA_Stream_IRQHandler()
{
  AUDIO_DMA_Stream->CR &= ~DMA_SxCR_TCIE ;            // Stop interrupt
  AUDIO_DMA->HIFCR = DMA_HIFCR_CTCIF5 | DMA_HIFCR_CHTIF5 | DMA_HIFCR_CTEIF5 | DMA_HIFCR_CDMEIF5 | DMA_HIFCR_CFEIF5 ; // Write ones to clear flags
  AUDIO_DMA_Stream->CR &= ~DMA_SxCR_EN ;                              // Disable DMA channel

  AudioBuffer * nextBuffer = audioQueue.getNextFilledBuffer();
  if (nextBuffer) {
    AUDIO_DMA_Stream->M0AR = CONVERT_PTR_UINT(nextBuffer->data);
    AUDIO_DMA_Stream->NDTR = nextBuffer->size;
    AUDIO_DMA->HIFCR = DMA_HIFCR_CTCIF5 | DMA_HIFCR_CHTIF5 | DMA_HIFCR_CTEIF5 | DMA_HIFCR_CDMEIF5 | DMA_HIFCR_CFEIF5 ; // Write ones to clear bits
    AUDIO_DMA_Stream->CR |= DMA_SxCR_EN | DMA_SxCR_TCIE ;       // Enable DMA channel
    DAC->SR = DAC_SR_DMAUDR1;                      // Write 1 to clear flag
  }
  else {
    dacIdle = true;
  }
}
#endif  // #if !defined(SIMU)
