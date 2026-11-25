/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "adc.h"
#include "tim.h"
#include "usart.h"
#include "gpio.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

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

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/*
 * STRUCUTRE:
 * define MACROS
 * setup peripherals
 * read sensors values
 * PD control
 * handle error or finish
 */
extern UART_HandleTypeDef huart1;

#define NUM_SENSORS 8
#define NUM_SAMPLES 6
#define LINE_THRESHOLD 500
#define MIDDLE_POS 4500
#define Kp 0.01
#define Kd 0.2
#define SPEED 0
#define MAX_OUT_TIME_MS 10
#define OUT_OUTTER_SPEED 255
#define OUT_INNER_SPEED -255
#define FINISH_LINE_COUNT (NUM_SENSORS - 2)

uint16_t sensor_values[NUM_SENSORS];
const uint32_t adc_channels[NUM_SENSORS] = { ADC_CHANNEL_0, ADC_CHANNEL_1, ADC_CHANNEL_2, ADC_CHANNEL_3, ADC_CHANNEL_4, ADC_CHANNEL_5, ADC_CHANNEL_6, ADC_CHANNEL_7 };
uint16_t arr_max;
bool is_running = false;
uint32_t start_time;
uint32_t current_time;
uint32_t out_start_time;
uint32_t race_elapsed_time;
bool is_on_line = false;
int16_t error;
int16_t previous_error;
uint8_t last_detected_side;

// FUNCS

int __io_putchar(int ch);
uint16_t ADC_Read_Channel(uint32_t channel);
void get_raw_adc_values(void);
void read_adc_sensors(void);
uint16_t read_line(void);
void set_motors(int pwma_value, int pwmb_value);
bool at_finish_line(void);

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

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
  MX_GPIO_Init();
  MX_ADC1_Init();
  MX_USART1_UART_Init();
  MX_TIM3_Init();
  /* USER CODE BEGIN 2 */

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  HAL_ADCEx_Calibration_Start(&hadc1);
  HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_3);
  HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_4);
  arr_max = __HAL_TIM_GET_AUTORELOAD(&htim3); // e.g. 999

  // setup
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET); // power led show mcu is on
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_3, GPIO_PIN_RESET);

  // start up sequence
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_4, GPIO_PIN_SET);
  HAL_Delay(1500);
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_4, GPIO_PIN_RESET);

  while(HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_15)) {};

  HAL_Delay(250);

  // calibrate sensors
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_4, GPIO_PIN_SET);
  while(HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_15)) {
	  HAL_GPIO_TogglePin(GPIOB, GPIO_PIN_4);
	  HAL_Delay(50);
	  //calibrate_sensors();
  };

  HAL_Delay(250);

  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_3, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_4, GPIO_PIN_RESET);

  while(HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_15)) {};

  is_running = true;
  start_time = HAL_GetTick();
  current_time = start_time;
  out_start_time = start_time;

  while (1)
  {

	  // run stright until reach starting point
	  uint32_t race_start_time = HAL_GetTick();
	  while(is_running){
		  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_4, GPIO_PIN_SET);

		  current_time = HAL_GetTick();
		  race_elapsed_time = current_time - race_start_time;
		  uint16_t pos = read_line();

		  error = MIDDLE_POS - pos;
		  int16_t delta_error = error - previous_error;
		  previous_error = error;

		  if(is_on_line){
			  if(sensor_values[0] < LINE_THRESHOLD || sensor_values[1] < LINE_THRESHOLD){
				  last_detected_side = 0; // right side
			  } else if (sensor_values[NUM_SENSORS - 1] < LINE_THRESHOLD || sensor_values[NUM_SENSORS - 2] < LINE_THRESHOLD){
				  last_detected_side = 1;
			  }
			  if(at_finish_line() && race_elapsed_time > 1000){
			  	  is_running = false;
			  }
			  printf("time: %d\n\r",race_elapsed_time);

			  int P = error*Kp;
			  int D = delta_error*Kd;

			  int control_signal = P + D;

			  int pwma_value = SPEED + control_signal;
			  int pwmb_value = SPEED - control_signal;

			  /*
			  if (pwma_value > 255) pwma_value = 255;
			  if (pwma_value < -255) pwma_value = -255;

			  if (pwmb_value > 255) pwmb_value = 255;
			  if (pwmb_value < -255) pwmb_value = -255;
			  */
			  set_motors(pwma_value, pwmb_value);
			  out_start_time = current_time;
		  } else { // out of line method
			  int out_elapsed_time = current_time - out_start_time;

			  if(out_elapsed_time >= MAX_OUT_TIME_MS){
				  is_running = false;
			  }
			  //  set motors in function of last detected side before out
			  if(last_detected_side == 0){ // TURN LEFT
				  set_motors(255, 0);
			  }else{ // TURN RIGHT
				  set_motors(0, 255);
			  }
		  }


	  }
	  set_motors(-255,-255);
	  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_4, GPIO_PIN_RESET);
	  HAL_Delay(100);
	  set_motors(0,0);

	  HAL_Delay(1000);
	  while(1){
		  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_3, GPIO_PIN_SET);
		  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_4, GPIO_PIN_SET);
		  HAL_Delay(250);
		  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_3, GPIO_PIN_RESET);
		  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_4, GPIO_PIN_RESET);
		  HAL_Delay(1000);
	  }


    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
  PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_ADC;
  PeriphClkInit.AdcClockSelection = RCC_ADCPCLK2_DIV6;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */
int __io_putchar(int ch) {
	HAL_UART_Transmit(&huart1, (uint8_t *)&ch, 1U, 100U);
	return ch;
}
uint16_t ADC_Read_Channel(uint32_t channel)
{
    ADC_ChannelConfTypeDef sConfig = {0};

    sConfig.Channel = channel;
    sConfig.Rank = ADC_REGULAR_RANK_1;
    sConfig.SamplingTime = ADC_SAMPLETIME_55CYCLES_5;

    if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK) {
        Error_Handler();
    }

    HAL_ADC_Start(&hadc1);
    HAL_ADC_PollForConversion(&hadc1, 10);
    uint16_t adc_val = HAL_ADC_GetValue(&hadc1);
    HAL_ADC_Stop(&hadc1);

    return adc_val;
}
void get_raw_adc_values(void){
	for(uint8_t i = 0; i < NUM_SENSORS; i++) {
		sensor_values[i] = ADC_Read_Channel(adc_channels[i]);
	}
}
void read_adc_sensors(void){
	uint32_t sum_sensor_values[NUM_SENSORS] = {0};
	for (uint8_t s = 0; s < NUM_SAMPLES; s++) {
		get_raw_adc_values();
		for (uint8_t i = 0; i < NUM_SENSORS; i++) {
			sum_sensor_values[i] += sensor_values[i];
		}
	}
	for (uint8_t i = 0; i < NUM_SENSORS; i++) {
		sensor_values[i] = (uint16_t)((sum_sensor_values[i] + (NUM_SAMPLES / 2U)) / NUM_SAMPLES);
	}
}
uint16_t read_line(void){
	uint16_t pos = 0;
	uint8_t active = 0;
	is_on_line = false;

	read_adc_sensors();

	for(uint8_t i = 0; i < NUM_SENSORS; i++){
		if(sensor_values[i] < LINE_THRESHOLD){
			is_on_line = true;
			active++;
			pos += (i + 1) * 1000;
		}
	}
	if(active == 0){return 4500;}
	return pos/active;
}
void set_motors(int pwma_value, int pwmb_value){
	  __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_3, pwma_value); // PWMA
	  __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_4, pwmb_value); // PWMB
	  // A RIGHT
	  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_12, GPIO_PIN_SET);
	  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_13, GPIO_PIN_RESET);
	  // B LEFT
	  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_14, GPIO_PIN_SET);
	  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_15, GPIO_PIN_RESET);
}
bool at_finish_line(void){
	uint8_t line_count = 0;
	for(uint8_t i = 0; i < FINISH_LINE_COUNT; i++){
		if(sensor_values[i + 1] < LINE_THRESHOLD){
			line_count++;
		}
	}
	return line_count == FINISH_LINE_COUNT;
}

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
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
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
