/**
 ******************************************************************************
  * File Name          : app_ble_lld.c
  * Description        : application utilities.
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; Copyright (c) 2019 STMicroelectronics.
  * All rights reserved.</center></h2>
  *
  * This software component is licensed by ST under Ultimate Liberty license
  * SLA0044, the "License"; You may not use this file except in compliance with
  * the License. You may obtain a copy of the License at:
  *                             www.st.com/SLA0044
  *
  ******************************************************************************
  */

/**
 * This file provides low level utilities for application:
 *  - IPCC for communication with radio MCU
 *  - UART management
 *  - error handling
 */

/* Includes ------------------------------------------------------------------*/
#include <stdbool.h>
#include "app_common.h"
#include "utilities_common.h"
#include "app_entry.h"
#include "dbg_trace.h"
#include "tl.h"
#include "shci.h"
#include "stm_logging.h"
#include "stm32_lpm.h"
#include "stm32_seq.h"
#include "gpio_lld.h"
#include "stm_queue.h"
#include "ble_lld.h"
#include "app_ble_lld.h"

/* Private includes ----------------------------------------------------------*/

/* Private typedef -----------------------------------------------------------*/
/*
 *  List of all errors tracked by the application
 *  running on M4. Some of these errors may be fatal
 *  or just warnings
 */
typedef enum
{
  ERR_BLE_LLD_SET_STATE_CB,
  ERR_BLE_LLD_ERASE_PERSISTENT_INFO,
  ERR_BLE_LLD_CHECK_WIRELESS
} ErrAppBleLldIdEnum_t;

/* Private defines -----------------------------------------------------------*/
#define UART_BUFFER_SIZE        64
#define TX_BUFFER_SIZE          268
#define UART_TX_CHUNK_SIZE      16
#define UART_LINE_END           "\r\n"

/* Private macros ------------------------------------------------------------*/

/* Private function prototypes -----------------------------------------------*/
static void uartTxSendChunk(void);
static void uartRxCpltCallback(void);
static void m0CmdProcess(void);

/* Private variables ---------------------------------------------------------*/
static queue_t uartTxBuf;
static uint8_t uartTxBufData[TX_BUFFER_SIZE];

static bool txBusy = false;

static char uartRxBuf;
static void(*uartRxUserCb)(char);

// IPCC configuration
PLACE_IN_SECTION("MB_MEM1") ALIGN(4) static TL_BLE_LLD_Config_t BleLldConfigBuffer;

// Shared memory used by IPCC to send/receive messages to/from M0
PLACE_IN_SECTION("MB_MEM2") ALIGN(4) static TL_CmdPacket_t BleLldM0CmdPacket;
PLACE_IN_SECTION("MB_MEM2") ALIGN(4) static TL_CmdPacket_t BleLldCmdRspPacket;

/* Shared memory used to send/receive data and parameters to/from M0 because
 IPCC messages have a limited size */
PLACE_IN_SECTION("MB_MEM2") ALIGN(4) static msg_BLE_LLD_t bleparam_BLE_LLD_Packet;

// Shared buffers for packet transmission and reception, separate buffers are needed because radio
PLACE_IN_SECTION("MB_MEM2") static ipBLE_lld_txrxdata_Type txBuffer;
PLACE_IN_SECTION("MB_MEM2") static ipBLE_lld_txrxdata_Type rxBuffer;


/* Functions Definition ------------------------------------------------------*/

void APP_BLE_LLD_Init(void)
{
  uint32_t devId = HAL_GetDEVID();
  uint32_t revId = HAL_GetREVID();
  uint8_t  param[8];
  SHCI_CmdStatus_t LldTestsInitStatus;

  /* Initialize transport layer */
  BleLldConfigBuffer.p_BleLldCmdRspBuffer = (uint8_t*)&BleLldCmdRspPacket;
  BleLldConfigBuffer.p_BleLldM0CmdBuffer = (uint8_t*)&BleLldM0CmdPacket;
  TL_BLE_LLD_Init(&BleLldConfigBuffer);

  APP_BLE_LLD_Init_UART();

  /* Send LLD tests start information to UART */
  uartWrite("");
  uartWrite("================================");
  uartWrite("RF BLE LLD");
  uartWrite("================================");
#if (CFG_DEBUGGER_SUPPORTED == 0U)
  uartWrite("Debugger de-activated");
#endif
#if (( CFG_DEBUG_TRACE_FULL == 0 ) && ( CFG_DEBUG_TRACE_LIGHT == 0 ))
  uartWrite("Trace is de-activated");
#endif


  /* Send start cmd to M0 (with device and revision ID as parameters */
  memcpy(&param[0], &devId, sizeof(devId));
  memcpy(&param[4], &revId, sizeof(revId));
  LldTestsInitStatus = SHCI_C2_BLE_LLD_Init(sizeof(param), param);
  if(LldTestsInitStatus != SHCI_Success){
  }else{
  }

  UTIL_SEQ_RegTask( 1<<CFG_TASK_CMD_FROM_M0_TO_M4, UTIL_SEQ_RFU, m0CmdProcess);

  BLE_LLD_PRX_Init(&bleparam_BLE_LLD_Packet.params,
                   &txBuffer,
                   &rxBuffer,
                   APP_BLE_LLD_SendCmdM0);
}

/**
  * @brief  Warn the user that an error has occurred.In this case,
  *         the LEDs on the Board will start blinking.
  * @param  ErrId :
  * @param  ErrCode
  * @retval None
  */
void APP_BLE_LLD_Error(uint32_t ErrId, uint32_t ErrCode)
{
      
  aPwmLedGsData_TypeDef aPwmLedGsData;
  aPwmLedGsData[PWM_LED_RED]   = PWM_LED_GSDATA_7_0;
  aPwmLedGsData[PWM_LED_GREEN] = PWM_LED_GSDATA_7_0;
  aPwmLedGsData[PWM_LED_BLUE]  = PWM_LED_GSDATA_7_0;
  
  while(true)
  {
    LED_On(aPwmLedGsData);
    HAL_Delay(500U);
    LED_Off();
    HAL_Delay(500U);
  }
}

/**
 * @brief Check if the Coprocessor Wireless Firmware loaded supports Thread
 *        and display associated informations
 * @param  None
 * @retval None
 */
void CheckWirelessFirmwareInfo(void)
{
  WirelessFwInfo_t  wireless_info_instance;
  WirelessFwInfo_t* p_wireless_info = &wireless_info_instance;
  if (SHCI_GetWirelessFwInfo(p_wireless_info) != SHCI_Success)
  {
    APP_BLE_LLD_Error(ERR_BLE_LLD_CHECK_WIRELESS, 0);
  }
  else
  {
    switch(p_wireless_info->StackType)
    {
    case INFO_STACK_TYPE_BLE_PHY_VALID :
      break;

    default :
      APP_BLE_LLD_Error(ERR_BLE_LLD_CHECK_WIRELESS, 0);
      break;
    }
  }
}

/*************************************************************
 *
 * LOCAL FUNCTIONS
 *
 *************************************************************/

/*************************************************************
 *
 * WRAP FUNCTIONS
 *
 *************************************************************/
/**
 * @brief Perform initialization of UART.
 * @param  None
 * @retval None
 */
void APP_BLE_LLD_Init_UART(void)
{
#ifdef CFG_UART
  MX_UART_Init(CFG_UART);
#endif

  CircularQueue_Init(&uartTxBuf,
                     uartTxBufData,
                     sizeof(uartTxBufData),
                     sizeof(char),
                     CIRCULAR_QUEUE_NO_FLAG);
  txBusy = false;
}

/**
 * @brief Perform de-initialization of UART.
 * @param  None
 * @retval None
 */
void APP_BLE_LLD_DeInit_UART(void)
{
#ifdef CFG_UART
  MX_UART_Deinit(CFG_UART);
#endif
}

static void uartRxStart(void)
{
  if (HW_UART_Receive_IT(CFG_UART, (uint8_t *)&uartRxBuf, 1, uartRxCpltCallback) != hw_uart_ok){
  }
}

void APP_BLE_LLD_uartRxStart(void(*callback)(char))
{
  uartRxUserCb = callback;
  uartRxStart();
}

static void uartRxCpltCallback(void)
{
  // No need to buffer uartRxBuf since the callback is called by value
  uartRxUserCb(uartRxBuf);
  // Since UART is in full duplex, receive can be always active without blocking send
  uartRxStart();
}

void uartWrite(const char *format, ...)
{
  char out[UART_BUFFER_SIZE];
  int nbChar;
  va_list argp;
  va_start(argp, format);
  nbChar = vsnprintf(out, sizeof(out), format, argp);
  va_end(argp);
  if (nbChar < 0){
    return;
  }
  if (nbChar > (sizeof(out) - ((strlen(UART_LINE_END) + 1)))){
    strcpy(&(out[sizeof(out) - (strlen(UART_LINE_END) + 1)]), UART_LINE_END);
  }else{
    strcat(out, UART_LINE_END);
  }
  uartWriteRaw(out);
}

void uartWriteRaw(const char *str)
{
  CRITICAL_BEGIN();
  while (*str != '\0'){
    CircularQueue_Add(&uartTxBuf, (uint8_t *)str, 0, 1);
    str++;
  }
  if (! txBusy){
    uartTxSendChunk();
  }
  CRITICAL_END();
}

// Send multiple chars through the UART
// must be called inside critical section
// loop on itself via the UART callback
static void uartTxSendChunk(void){
  static char hwBuf[UART_TX_CHUNK_SIZE];
  char *charPtr;
  uint32_t count = 0;

  while ((charPtr = (char *)CircularQueue_Remove(&uartTxBuf, NULL)) != NULL){
    hwBuf[count] = *charPtr;
    count++;
    if (count >= UART_TX_CHUNK_SIZE){
      break;
    }
  }
  if (count != 0){
    txBusy = true;
    if (HW_UART_Transmit_IT(CFG_UART, (uint8_t *)hwBuf, count, uartTxSendChunk) != hw_uart_ok){
    }
  }else{
    txBusy = false;
  }
}

static void m0CmdProcess(void)
{
  BLE_LLD_PRX_EventProcessTask();
}

/**
 * @brief Processes an event from radio CPU
 *
 * @param   cmdBuffer : a pointer to TL_CmdPacket_t
 * @return  None
 */
void TL_BLE_LLD_ReceiveM0Cmd( TL_CmdPacket_t * cmdBuffer )
{
  BLE_LLD_PRX_EventProcessInter((radioEventType)cmdBuffer->cmdserial.cmd.cmdcode);
  UTIL_SEQ_SetTask(1U << CFG_TASK_CMD_FROM_M0_TO_M4, CFG_SCH_PRIO_0);
  TL_BLE_LLD_SendM0CmdAck();
}

/**
 * @brief Sends a command to radio CPU
 *
 * Waits for reply from radio CPU before returning (synchronous calls).
 *
 * @param[in] command BLE command already packed (by LLD)
 */
uint8_t APP_BLE_LLD_SendCmdM0(BLE_LLD_Code_t bleCmd)
{
  BleLldCmdRspPacket.cmdserial.cmd.cmdcode = bleCmd;
  payload_BLE_LLD_t *payload = (payload_BLE_LLD_t *)&BleLldCmdRspPacket.cmdserial.cmd.payload;
  payload->msg = &bleparam_BLE_LLD_Packet;
  UTIL_SEQ_ClrEvt(1U << CFG_EVT_RECEIVE_RSPACKEVT);
  TL_BLE_LLD_SendCmd();
  UTIL_SEQ_WaitEvt(1U << CFG_EVT_RECEIVE_RSPACKEVT);

  return bleparam_BLE_LLD_Packet.returnValue;
}

/**
 * @brief Processes a reply (to a command) from radio CPU
 *
 * Unlocks task waiting in APP_BLE_LLD_SendCmdM0(), this is used to make LLD
 * API calls synchronous.
 *
 * @param   Notbuffer : a pointer to TL_CmdPacket_t
 * @return  None
 */
void TL_BLE_LLD_ReceiveRsp( TL_CmdPacket_t * Notbuffer )
{
  switch (Notbuffer->cmdserial.cmd.cmdcode){
    case BLE_LLD_RSP_END:
      UTIL_SEQ_SetEvt(1U << CFG_EVT_RECEIVE_RSPACKEVT);
      break;
    default:
      break;
  }

    /* This is just a trace from M0, write to UART */
    //uartWriteRaw(sourceBuf);

  TL_BLE_LLD_SendRspAck();
}

/* USER CODE END FD_WRAP_FUNCTIONS */

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
