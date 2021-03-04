/* USER CODE BEGIN Header */
/**
 ***************************************************************************************
  * File Name          : stm32_lpm_if.c
  * Description        : Low layer function to enter/exit low power modes (stop, sleep).
 ***************************************************************************************
  * @attention
  *
  * <h2><center>&copy; Copyright (c) 2019 STMicroelectronics.
  * All rights reserved.</center></h2>
  *
  * This software component is licensed by ST under BSD 3-Clause license,
  * the "License"; You may not use this file except in compliance with the
  * License. You may obtain a copy of the License at:
  *                        opensource.org/licenses/BSD-3-Clause
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "stm32_lpm_if.h"
#include "stm32_lpm.h"
#include "app_conf.h"
/* USER CODE BEGIN include */

/* USER CODE END include */

/* Exported variables --------------------------------------------------------*/
const struct UTIL_LPM_Driver_s UTIL_PowerDriver =
{
  PWR_EnterSleepMode,
  PWR_ExitSleepMode,

  PWR_EnterStopMode,
  PWR_ExitStopMode,

  PWR_EnterOffMode,
  PWR_ExitOffMode,
};

extern uint32_t boot_after_standby;
extern RTC_HandleTypeDef hrtc;

#define CSTACK_PREAMBLE_NUMBER 16
uint32_t cStackPreamble[CSTACK_PREAMBLE_NUMBER];

typedef void( *intfunc )( void );
typedef union { intfunc __fun; void * __ptr; } intvec_elem;
extern const intvec_elem __vector_table[];

void CPUcontextSave(void); /* this function is implemented in startup assembly file */
void standby_hw_save(void);
void standby_hw_restore(void);

/* Private function prototypes -----------------------------------------------*/
static void Switch_On_HSI( void );
/* USER CODE BEGIN Private_Function_Prototypes */

/* USER CODE END Private_Function_Prototypes */
/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN Private_Typedef */

/* USER CODE END Private_Typedef */
/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN Private_Define */

/* USER CODE END Private_Define */
/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN Private_Macro */

/* USER CODE END Private_Macro */
/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Private_Variables */

/* USER CODE END Private_Variables */

/* Functions Definition ------------------------------------------------------*/
/**
  * @brief Enters Low Power Off Mode
  * @param none
  * @retval none
  */
void PWR_EnterOffMode( void )
{
/* USER CODE BEGIN PWR_EnterOffMode */
#if ( CFG_LPM_STANDBY_SUPPORTED != 0 )
  /**
   * The systick should be disabled for the same reason than when the device enters stop mode because
   * at this time, the device may enter either OffMode or StopMode.
   */
  HAL_SuspendTick();

  /**
   * This function is called from CRITICAL SECTION
   */
  while( LL_HSEM_1StepLock( HSEM, CFG_HW_RCC_SEMID ) );

  if ( ! LL_HSEM_1StepLock( HSEM, CFG_HW_ENTRY_STOP_MODE_SEMID ) )
  {
    if( LL_PWR_IsActiveFlag_C2DS( ) )
    {
      /* Release ENTRY_STOP_MODE semaphore */
      LL_HSEM_ReleaseLock( HSEM, CFG_HW_ENTRY_STOP_MODE_SEMID, 0 );

      /**
       * The switch on HSI before entering Stop Mode is required 
       */
      Switch_On_HSI( );
    }
  }
  else
  {
    /**
     * The switch on HSI before entering Stop Mode is required 
     */
    Switch_On_HSI( );
  }

  /* Release RCC semaphore */
  LL_HSEM_ReleaseLock( HSEM, CFG_HW_RCC_SEMID, 0 );

  /************************************************************************************
   * ENTER OFF MODE
   ***********************************************************************************/
  /*
   * There is no risk to clear all the WUF here because in the current implementation, this API is called
   * in critical section. If an interrupt occurs while in that critical section before that point,
   * the flag is set and will be cleared here but the system will not enter Off Mode
   * because an interrupt is pending in the NVIC. The ISR will be executed when moving out
   * of this critical section
   */
  LL_PWR_ClearFlag_WU( );

  LL_PWR_SetPowerMode( LL_PWR_MODE_STANDBY );

  LL_LPM_EnableDeepSleep( ); /**< Set SLEEPDEEP bit of Cortex System Control Register */

  /**
   * This option is used to ensure that store operations are completed
   */
#if defined ( __CC_ARM)
  __force_stores( );
#endif

  /* This part of code must not put in a function as it deals with C stack calls.
   * A function call will push data in C stack and impact algorithm. 
   */
  /* local variable are here for better view */
  uint8_t i = 0;
  uint32_t* ptr;
  /* Save part of the stack that will be restored at wakeup */
  ptr = __vector_table[0].__ptr ;
  ptr -= CSTACK_PREAMBLE_NUMBER;
  do {
    cStackPreamble[i] = *ptr;
    i++;
    ptr++;
  } while (i < CSTACK_PREAMBLE_NUMBER); 
     
  LL_EXTI_EnableEvent_32_63( LL_EXTI_LINE_40 );
  LL_EXTI_EnableRisingTrig_32_63( LL_EXTI_LINE_40 );
  
  standby_hw_save();

  CPUcontextSave();/* this function will call WFI instruction */
#endif  
  return;  
/* USER CODE END PWR_EnterOffMode */
}

/**
  * @brief Exits Low Power Off Mode
  * @param none
  * @retval none
  */
void PWR_ExitOffMode( void )
{
/* USER CODE BEGIN PWR_ExitOffMode */
#if ( CFG_LPM_STANDBY_SUPPORTED != 0 )
  /* This part of code must not put in a function as it deals with C stack calls.
   * A function call will push data in C stack and impact algorithm. 
   */  
  if(boot_after_standby != 0)
  {
    boot_after_standby = 0;    
    /* local variable are here for better view */
    uint8_t i = 0;
    uint32_t* ptr;
    /* Restore the part of stack that has been saved before the sleep */
    ptr = __vector_table[0].__ptr ;
    ptr -= CSTACK_PREAMBLE_NUMBER;
    do {
      *ptr = cStackPreamble[i];
      i++;
      ptr++;
    } while (i < CSTACK_PREAMBLE_NUMBER);     
        
    standby_hw_restore();
  }
 
  /* Release ENTRY_STOP_MODE semaphore */
  LL_HSEM_ReleaseLock( HSEM, CFG_HW_ENTRY_STOP_MODE_SEMID, 0 );

  while( LL_HSEM_1StepLock( HSEM, CFG_HW_RCC_SEMID ) );

  if(LL_RCC_GetSysClkSource( ) == LL_RCC_SYS_CLKSOURCE_STATUS_HSI)
  {
    LL_RCC_HSE_Enable( );
    while(!LL_RCC_HSE_IsReady( ));
    LL_RCC_SetSysClkSource(LL_RCC_SYS_CLKSOURCE_HSE);
    while (LL_RCC_GetSysClkSource( ) != LL_RCC_SYS_CLKSOURCE_STATUS_HSE);
  }
  else
  {
    /**
     * As long as the current application is fine with HSE as system clock source,
     * there is nothing to do here
     */
  }

  /* Release RCC semaphore */
  LL_HSEM_ReleaseLock( HSEM, CFG_HW_RCC_SEMID, 0 );

  HAL_ResumeTick();
#endif 
/* USER CODE END PWR_ExitOffMode */
}

/**
  * @brief Enters Low Power Stop Mode
  * @note ARM exists the function when waking up
  * @param none
  * @retval none
  */
void PWR_EnterStopMode( void )
{
/* USER CODE BEGIN PWR_EnterStopMode */
  /**
   * When HAL_DBGMCU_EnableDBGStopMode() is called to keep the debugger active in Stop Mode,
   * the systick shall be disabled otherwise the cpu may crash when moving out from stop mode
   *
   * When in production, the HAL_DBGMCU_EnableDBGStopMode() is not called so that the device can reach best power consumption
   * However, the systick should be disabled anyway to avoid the case when it is about to expire at the same time the device enters
   * stop mode ( this will abort the Stop Mode entry ).
   */
  HAL_SuspendTick();

  /**
   * This function is called from CRITICAL SECTION
   */
  while( LL_HSEM_1StepLock( HSEM, CFG_HW_RCC_SEMID ) );

  if ( ! LL_HSEM_1StepLock( HSEM, CFG_HW_ENTRY_STOP_MODE_SEMID ) )
  {
    if( LL_PWR_IsActiveFlag_C2DS( ) )
    {
      /* Release ENTRY_STOP_MODE semaphore */
      LL_HSEM_ReleaseLock( HSEM, CFG_HW_ENTRY_STOP_MODE_SEMID, 0 );

      /**
       * The switch on HSI before entering Stop Mode is required 
       */
      Switch_On_HSI( );
    }
  }
  else
  {
    /**
     * The switch on HSI before entering Stop Mode is required 
     */
    Switch_On_HSI( );
  }

  /* Release RCC semaphore */
  LL_HSEM_ReleaseLock( HSEM, CFG_HW_RCC_SEMID, 0 );

  /************************************************************************************
   * ENTER STOP MODE
   ***********************************************************************************/
#if(STM32WB15xx != 0)
  LL_PWR_SetPowerMode( LL_PWR_MODE_STOP1 );
#else
  LL_PWR_SetPowerMode( LL_PWR_MODE_STOP2 );
#endif

  LL_LPM_EnableDeepSleep( ); /**< Set SLEEPDEEP bit of Cortex System Control Register */

  /**
   * This option is used to ensure that store operations are completed
   */
#if defined ( __CC_ARM)
  __force_stores( );
#endif

  __WFI();
/* USER CODE END PWR_EnterStopMode */
}

/**
  * @brief Exits Low Power Stop Mode
  * @note Enable the pll at 32MHz
  * @param none
  * @retval none
  */
void PWR_ExitStopMode( void )
{
/* USER CODE BEGIN PWR_ExitStopMode */
  /**
   * This function is called from CRITICAL SECTION
   */
  /* Release ENTRY_STOP_MODE semaphore */
  LL_HSEM_ReleaseLock( HSEM, CFG_HW_ENTRY_STOP_MODE_SEMID, 0 );

  while( LL_HSEM_1StepLock( HSEM, CFG_HW_RCC_SEMID ) );

  if(LL_RCC_GetSysClkSource( ) == LL_RCC_SYS_CLKSOURCE_STATUS_HSI)
  {
    LL_RCC_HSE_Enable( );
    while(!LL_RCC_HSE_IsReady( ));
    LL_RCC_SetSysClkSource(LL_RCC_SYS_CLKSOURCE_HSE);
    while (LL_RCC_GetSysClkSource( ) != LL_RCC_SYS_CLKSOURCE_STATUS_HSE);
  }
  else
  {
    /**
     * As long as the current application is fine with HSE as system clock source,
     * there is nothing to do here
     */
  }

  /* Release RCC semaphore */
  LL_HSEM_ReleaseLock( HSEM, CFG_HW_RCC_SEMID, 0 );

  HAL_ResumeTick();

/* USER CODE END PWR_ExitStopMode */
}

/**
  * @brief Enters Low Power Sleep Mode
  * @note ARM exits the function when waking up
  * @param none
  * @retval none
  */
void PWR_EnterSleepMode( void )
{
/* USER CODE BEGIN PWR_EnterSleepMode */

  HAL_SuspendTick();

  /************************************************************************************
   * ENTER SLEEP MODE
   ***********************************************************************************/
  LL_LPM_EnableSleep( ); /**< Clear SLEEPDEEP bit of Cortex System Control Register */

  /**
   * This option is used to ensure that store operations are completed
   */
#if defined ( __CC_ARM)
  __force_stores();
#endif

  __WFI( );
/* USER CODE END PWR_EnterSleepMode */
}

/**
  * @brief Exits Low Power Sleep Mode
  * @note ARM exits the function when waking up
  * @param none
  * @retval none
  */
void PWR_ExitSleepMode( void )
{
/* USER CODE BEGIN PWR_ExitSleepMode */

  HAL_ResumeTick();

/* USER CODE END PWR_ExitSleepMode */
}

/*************************************************************
 *
 * LOCAL FUNCTIONS
 *
 *************************************************************/
/**
  * @brief Switch the system clock on HSI
  * @param none
  * @retval none
  */
static void Switch_On_HSI( void )
{
  LL_RCC_HSI_Enable( );
  while(!LL_RCC_HSI_IsReady( ));
  LL_RCC_SetSysClkSource( LL_RCC_SYS_CLKSOURCE_HSI );
  LL_RCC_SetSMPSClockSource(LL_RCC_SMPS_CLKSOURCE_HSI);
  while (LL_RCC_GetSysClkSource( ) != LL_RCC_SYS_CLKSOURCE_STATUS_HSI);
}

/* USER CODE BEGIN Private_Functions */

/* USER CODE END Private_Functions */

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/

