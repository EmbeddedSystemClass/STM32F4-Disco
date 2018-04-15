/**
  ******************************************************************************
  * @file    STM324x7i_eval_fsmc_sram.h
  * @author  MCD Application Team
  * @version V1.0.0
  * @date    11-January-2013
  * @brief   This file contains all the functions prototypes for the 
  *          STM324x7i_eval_fsmc_sram.c driver.
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; COPYRIGHT 2013 STMicroelectronics</center></h2>
  *
  * Licensed under MCD-ST Liberty SW License Agreement V2, (the "License");
  * You may not use this file except in compliance with the License.
  * You may obtain a copy of the License at:
  *
  *        http://www.st.com/software_license_agreement_liberty_v2
  *
  * Unless required by applicable law or agreed to in writing, software 
  * distributed under the License is distributed on an "AS IS" BASIS, 
  * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  * See the License for the specific language governing permissions and
  * limitations under the License.
  *
  ******************************************************************************
  */ 

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __STM324x7I_EVAL_FSMC_SRAM_H
#define __STM324x7I_EVAL_FSMC_SRAM_H

#ifdef __cplusplus
 extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
//#include "STM324x7i_eval.h"
#include <stm32f4xx.h>
#include <system_stm32f4xx.h>
#include <core_cm4.h>
#include "main.h"
#include <stm32f4xx_fsmc.h>
#include <stm32f4xx_gpio.h>
#include <stm32f4xx_rcc.h>
#include <stm32f4xx_dma.h>

#define Bank1_SRAM3_ADDR  ((uint32_t)0x68000000)

/** @addtogroup Utilities
  * @{
  */
  
/** @addtogroup STM32_EVAL
  * @{
  */ 

/** @addtogroup STM324x7I_EVAL
  * @{
  */
  
/** @addtogroup STM324x7I_EVAL_FSMC_SRAM
  * @{
  */  

/** @defgroup STM324x7I_EVAL_FSMC_SRAM_Exported_Types
  * @{
  */
/**
  * @}
  */
  
/** @defgroup STM324x7I_EVAL_FSMC_SRAM_Exported_Constants
  * @{
  */ 
/**
  * @}
  */ 
  
/** @defgroup STM324x7I_EVAL_FSMC_SRAM_Exported_Macros
  * @{
  */ 
/**
  * @}
  */ 

/** @defgroup STM324x7I_EVAL_FSMC_SRAM_Exported_Functions
  * @{
  */      

void SRAM_Init(void);
void SRAM_WriteBuffer(uint16_t* pBuffer, uint32_t WriteAddr, uint32_t NumHalfwordToWrite);
void SRAM_WriteBuffer1(uint8_t* pBuffer, uint32_t WriteAddr, uint32_t NumHalfwordToWrite);
void SRAM_ReadBuffer(uint16_t* pBuffer, uint32_t ReadAddr, uint32_t NumHalfwordToRead);
void SRAM_ReadBuffer1(uint8_t* pBuffer, uint32_t ReadAddr, uint32_t NumHalfwordToRead);

#ifdef __cplusplus
}
#endif

#endif /* __STM324x7I_EVAL_FSMC_SRAM_H */
/**
  * @}
  */

/**
  * @}
  */

/**
  * @}
  */

/**
  * @}
  */ 

/**
  * @}
  */ 

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/