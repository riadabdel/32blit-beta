/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; Copyright (c) 2020 STMicroelectronics.
  * All rights reserved.</center></h2>
  *
  * This software component is licensed by ST under Ultimate Liberty license
  * SLA0044, the "License"; You may not use this file except in compliance with
  * the License. You may obtain a copy of the License at:
  *                             www.st.com/SLA0044
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "dma.h"
#include "fatfs.h"
#include "hrtim.h"
#include "i2c.h"
#include "jpeg.h"
#include "quadspi.h"
#include "rng.h"
#include "spi.h"
#include "tim.h"
#include "usb_device.h"

#include "adc.hpp"
#include "gpio.hpp"
#include "display.hpp"
#include "sound.hpp"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "32blit.h"
#include "32blit.hpp"
#include "graphics/color.hpp"
#include "CDCResetHandler.h"
#include "CDCInfoHandler.h"
#include "CDCCommandStream.h"
#include "USBManager.h"

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
extern USBManager g_usbManager;
extern CDCCommandStream g_commandStream;
CDCResetHandler g_resetHandler;
CDCInfoHandler g_infoHandler;
bool is_beta_unit = false;


uint8_t charge_led_r = 0;
uint8_t charge_led_g = 0;
uint8_t charge_led_b = 0;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* Enable I-Cache---------------------------------------------------------*/
  SCB_EnableICache();

  /* Enable D-Cache---------------------------------------------------------*/
  SCB_EnableDCache();

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  gpio::init();
  sound::init();

  is_beta_unit = HAL_GPIO_ReadPin(VERSION_GPIO_Port, VERSION_Pin);

  //MX_GPIO_Init();

  MX_DMA_Init();
  MX_TIM2_Init();
  MX_TIM4_Init();
  MX_TIM3_Init();
  //MX_DAC1_Init();
  MX_HRTIM_Init();
  MX_I2C4_Init();
#if (INITIALISE_QSPI==1)
  MX_QUADSPI_Init();
#endif
  adc::init();

  //MX_USB_OTG_HS_USB_Init();
  MX_SPI1_Init();
  MX_SPI4_Init();
  //MX_TIM6_Init();
  MX_TIM15_Init();
  //MX_TIM16_Init();
  MX_FATFS_Init();
  MX_RNG_Init();
  MX_USB_DEVICE_Init();
  MX_JPEG_Init();
  /* USER CODE BEGIN 2 */

  // USB in sleep
  RCC->AHB1LPENR &= ~RCC_AHB1LPENR_USB1OTGHSULPILPEN;
  RCC->AHB1LPENR |= RCC_AHB1LPENR_USB1OTGHSLPEN;

  //NVIC_SetPriority(SysTick_IRQn, 0x0);

#if (INITIALISE_QSPI==1)
  qspi_init();
#endif

  blit_init();

  // add CDC handler to reset device on receiving "_RST" and "SWIT"
	g_commandStream.AddCommandHandler(CDCCommandHandler::CDCFourCCMake<'_', 'R', 'S', 'T'>::value, &g_resetHandler);
	g_commandStream.AddCommandHandler(CDCCommandHandler::CDCFourCCMake<'S', 'W', 'I', 'T'>::value, &g_resetHandler);

  // add CDC handler to log info device on receiving "INFO"
	g_commandStream.AddCommandHandler(CDCCommandHandler::CDCFourCCMake<'I', 'N', 'F', 'O'>::value, &g_infoHandler);

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    blit_tick();
    //HAL_Delay(1000);

    // USB
    g_usbManager.Update();

    if(USB_GetMode(USB_OTG_HS))
      MX_USB_HOST_Process();

    // handle CDC input
    g_commandStream.Stream();

    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

// HAL_RCCEx_PeriphCLKConfig is a HUGE (~2k bytes, near 1.5k lines) function, this is all we actually need from it...
HAL_StatusTypeDef PeriphCLKConfig(RCC_PeriphCLKInitTypeDef *PeriphClkInit)
{
  // QSPI
  __HAL_RCC_QSPI_CONFIG(RCC_QSPICLKSOURCE_D1HCLK);

  // SPI 1/2/3
  __HAL_RCC_PLLCLKOUT_ENABLE(RCC_PLL1_DIVQ);
  __HAL_RCC_SPI123_CONFIG(RCC_SPI123CLKSOURCE_PLL);

  // SPI 4/5
  __HAL_RCC_SPI45_CONFIG(RCC_SPI45CLKSOURCE_HSI);

  // I2C 4
  __HAL_RCC_I2C4_CONFIG(RCC_I2C4CLKSOURCE_D3PCLK1);

  // ADC
  if(RCCEx_PLL2_Config(&(PeriphClkInit->PLL2), /*DIVIDER_P_UPDATE*/ 0) != HAL_OK)
    return HAL_ERROR;

  __HAL_RCC_ADC_CONFIG(RCC_ADCCLKSOURCE_PLL2);

  // USB
  __HAL_RCC_USB_CONFIG(RCC_USBCLKSOURCE_HSI48);

  // LTDC
  if(RCCEx_PLL3_Config(&(PeriphClkInit->PLL3), /*DIVIDER_R_UPDATE*/ 2) != HAL_OK)
    return HAL_ERROR;

  // RNG
  __HAL_RCC_RNG_CONFIG(RCC_RNGCLKSOURCE_HSI48);

  // HRTIM1 is ifdef'd out?

  return HAL_OK;
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};
  RCC_CRSInitTypeDef RCC_CRSInitStruct = {0};

  /** Supply configuration update enable
  */
  HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY);
  /** Configure the main internal regulator output voltage
  */
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE0);

  while(!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {}
  /** Macro to configure the PLL clock source
  */
  __HAL_RCC_PLL_PLLSOURCE_CONFIG(RCC_PLLSOURCE_HSE);
  /** Initializes the CPU, AHB and APB busses clocks
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI48|RCC_OSCILLATORTYPE_HSI
                              | RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSIState = RCC_HSI_DIV1;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.HSI48State = RCC_HSI48_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 1;
  RCC_OscInitStruct.PLL.PLLN = 80;
  RCC_OscInitStruct.PLL.PLLP = 2;
  RCC_OscInitStruct.PLL.PLLQ = 2;
  RCC_OscInitStruct.PLL.PLLR = 2;
  RCC_OscInitStruct.PLL.PLLRGE = RCC_PLL1VCIRANGE_3;
  RCC_OscInitStruct.PLL.PLLVCOSEL = RCC_PLL1VCOWIDE;
  RCC_OscInitStruct.PLL.PLLFRACN = 0;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }
  /** Initializes the CPU, AHB and APB busses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2
                              |RCC_CLOCKTYPE_D3PCLK1|RCC_CLOCKTYPE_D1PCLK1;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.SYSCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB3CLKDivider = RCC_APB3_DIV2;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_APB1_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_APB2_DIV2;
  RCC_ClkInitStruct.APB4CLKDivider = RCC_APB4_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
  {
    Error_Handler();
  }
  /*PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_LTDC|RCC_PERIPHCLK_HRTIM1
                              |RCC_PERIPHCLK_RNG|RCC_PERIPHCLK_SPI4
                              |RCC_PERIPHCLK_SPI1|RCC_PERIPHCLK_ADC
                              |RCC_PERIPHCLK_I2C4|RCC_PERIPHCLK_USB
                              |RCC_PERIPHCLK_QSPI;*/
  PeriphClkInitStruct.PLL2.PLL2M = 1;
  PeriphClkInitStruct.PLL2.PLL2N = 12;
  PeriphClkInitStruct.PLL2.PLL2P = 1;
  PeriphClkInitStruct.PLL2.PLL2Q = 2;
  PeriphClkInitStruct.PLL2.PLL2R = 2;
  PeriphClkInitStruct.PLL2.PLL2RGE = RCC_PLL2VCIRANGE_3;
  PeriphClkInitStruct.PLL2.PLL2VCOSEL = RCC_PLL2VCOMEDIUM;
  PeriphClkInitStruct.PLL2.PLL2FRACN = 4096;
  PeriphClkInitStruct.PLL3.PLL3M = 6;
  PeriphClkInitStruct.PLL3.PLL3N = 129;
  PeriphClkInitStruct.PLL3.PLL3P = 2;
  PeriphClkInitStruct.PLL3.PLL3Q = 2;
  PeriphClkInitStruct.PLL3.PLL3R = 53;
  PeriphClkInitStruct.PLL3.PLL3RGE = RCC_PLL3VCIRANGE_1;
  PeriphClkInitStruct.PLL3.PLL3VCOSEL = RCC_PLL3VCOWIDE;
  PeriphClkInitStruct.PLL3.PLL3FRACN = 0;
  /*PeriphClkInitStruct.QspiClockSelection = RCC_QSPICLKSOURCE_D1HCLK;
  PeriphClkInitStruct.Spi123ClockSelection = RCC_SPI123CLKSOURCE_PLL;
  PeriphClkInitStruct.Spi45ClockSelection = RCC_SPI45CLKSOURCE_HSI;
  PeriphClkInitStruct.RngClockSelection = RCC_RNGCLKSOURCE_HSI48;
  PeriphClkInitStruct.UsbClockSelection = RCC_USBCLKSOURCE_HSI48;
  PeriphClkInitStruct.I2c4ClockSelection = RCC_I2C4CLKSOURCE_D3PCLK1;
  PeriphClkInitStruct.AdcClockSelection = RCC_ADCCLKSOURCE_PLL2;
  PeriphClkInitStruct.Hrtim1ClockSelection = RCC_HRTIM1CLK_CPUCLK;*/
  if (PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK)
  {
    Error_Handler();
  }
  /** Enable the SYSCFG APB clock
  */
  __HAL_RCC_CRS_CLK_ENABLE();
  /** Configures CRS
  */
  RCC_CRSInitStruct.Prescaler = RCC_CRS_SYNC_DIV1;
  RCC_CRSInitStruct.Source = RCC_CRS_SYNC_SOURCE_USB1;
  RCC_CRSInitStruct.Polarity = RCC_CRS_SYNC_POLARITY_RISING;
  RCC_CRSInitStruct.ReloadValue = __HAL_RCC_CRS_RELOADVALUE_CALCULATE(48000000,1000);
  RCC_CRSInitStruct.ErrorLimitValue = 34;
  RCC_CRSInitStruct.HSI48CalibrationValue = 32;

  HAL_RCCEx_CRSConfig(&RCC_CRSInitStruct);

  __HAL_RCC_DMA2D_CLK_ENABLE();

  /** Enable USB Voltage detector
  */
  HAL_PWREx_EnableUSBVoltageDetector();
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */

  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     tex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
