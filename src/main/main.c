#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"

#include <u8g2.h>
#include "u8g2_esp32_hal.h"

// SDA - GPIO21
#define PIN_SDA 21
// SCL - GPIO22
#define PIN_SCL 22


#define tag "SBCCtl"

TaskHandle_t taskTestDisplayHandle = NULL;
TaskHandle_t taskDisplayHandle = NULL;
TaskHandle_t taskCounterHandle = NULL;
TaskHandle_t ISR = NULL;
static QueueHandle_t calipers_bit_queue = NULL;

u8g2_t u8g2;  // a structure which will contain all the data for one display
char lineChar[20];
int stripeLength = 123;
uint32_t stripeThickness = 0;
uint32_t stripeThicknessPrev = 0;
uint32_t stripeThicknessBits = 0;
bool syncCalipers = true;
bool unitMM = true;
bool signPlus = true;

#define ESP_INTR_FLAG_DEFAULT 0
#define CALIPERS_CLK_PIN  26
#define CALIPERS_DATA_PIN  27

//#define GPIO_INPUT_PIN_SEL  ((1ULL<<CALIPERS_CLK_PIN) | (1ULL<<CALIPERS_DATA_PIN))
#define GPIO_INPUT_PIN_SEL  (1ULL<<CALIPERS_CLK_PIN)

static void IRAM_ATTR gpio_isr_handler(void* arg)
{
	uint32_t gpio_num = (uint32_t) arg;
	switch (gpio_num) {
		case CALIPERS_CLK_PIN:
			bool data_bit = (bool) gpio_get_level(CALIPERS_DATA_PIN);
			bool clk_level = (bool) gpio_get_level(CALIPERS_CLK_PIN);
			if(!clk_level){
				xQueueSendFromISR(calipers_bit_queue, &data_bit, NULL);
			}
			break;;
		default:
	}
}

static void readCalippersBit(void *arg){
	bool level;
	uint16_t count=0;
	int64_t time=0;
	int64_t now=0;
	bool mSign;
	bool mUnit;
	for(;;) {
		if(xQueueReceive(calipers_bit_queue, &level, portMAX_DELAY)) {
			now = esp_timer_get_time();
			if(now-time > 5000) {
				// Debug print
				//printf("time delta %lld ", now-time);
				//printf("bit: %d, ", level ? 0:1);
				//printf("val:%s%ld %s", signPlus ? "+":"-",stripeThicknessBits, unitMM ? "mm":"in"); 
				//printf("cnt: %d", count);
				//printf("\n");

				if(count==24){
					stripeThickness = stripeThicknessBits;
					unitMM = mUnit;
					signPlus = mSign;
				}
				stripeThicknessBits = 0;
				count=0;
			}
			switch (count) {
				case 23:
					mUnit = level ? true:false;
					stripeThicknessBits = stripeThicknessBits >> 1;
					break;;
				case 22: 
					stripeThicknessBits = stripeThicknessBits >> 1;
					break;;
				case 21: 
					stripeThicknessBits = stripeThicknessBits >> 1;
					break;;
				case 20: 
					mSign = level ? true:false;
					stripeThicknessBits = stripeThicknessBits >> 1;
					break;;
				default:
					stripeThicknessBits = (stripeThicknessBits >> 1) | (!(bool)(level==0?0:1) << 23);
			}
			count++;
			time = now;
		}
	}
}

void RefreshDisplayU8G2(void *arg)
{
  //u8g2_SetFont(&u8g2, u8g2_font_ncenB14_tr);

	while(1){
		if(stripeThickness != stripeThicknessPrev) {
			u8g2_SetFont(&u8g2, u8g2_font_6x10_mf);
			u8g2_DrawStr(&u8g2, 5, 12, "Thickness:");
			if(unitMM) {
				sprintf(&lineChar[0], "%s%.2f mm   ", signPlus?" ":"-", stripeThickness/100.00);
			} else {
				sprintf(&lineChar[0], "%s%.4f in   ", signPlus?" ":"-", stripeThickness*5/10000.00);
			}
			u8g2_SetFont(&u8g2, u8g2_font_chargen_92_mf);
			u8g2_DrawStr(&u8g2, 0, 30, lineChar);
			u8g2_SendBuffer(&u8g2);
			ESP_LOGI(tag, "Thickness: %s", lineChar); // for monitor logging
			//printf("%s%.2f\n", signPlus?"":"-", stripeThickness/100.00); // for serial plotter
		}
		stripeThicknessPrev = stripeThickness;
		vTaskDelay(500/portTICK_PERIOD_MS);
	}
}

void Counter(void *arg)
{
	while(1){
		stripeLength++;
		ESP_LOGI(tag, "Counter incremented to: %d", stripeLength);
		vTaskDelay(5000/portTICK_PERIOD_MS);
	}
}

void task_test_SSD1306i2c(void* ignore) {
	ESP_LOGI(tag, "u8g2_DrawBox");
	u8g2_DrawBox(&u8g2, 0, 26, 80, 6);
	u8g2_DrawFrame(&u8g2, 0, 26, 100, 6);

	ESP_LOGI(tag, "u8g2_SetFont");
	u8g2_SetFont(&u8g2, u8g2_font_ncenB14_tr);
	ESP_LOGI(tag, "u8g2_DrawStr");
	u8g2_DrawStr(&u8g2, 2, 17, "Hi nkolban!");
	ESP_LOGI(tag, "u8g2_SendBuffer");
	u8g2_SendBuffer(&u8g2);

	ESP_LOGI(tag, "All done!");
	vTaskDelete(NULL);
}


void app_main(void)
{
	/* GPIO SETUP */
	//zero-initialize the config structure.
	gpio_config_t io_conf = {};
	//interrupt of rising edge
	io_conf.intr_type = GPIO_INTR_NEGEDGE;
	//bit mask of the pins
	io_conf.pin_bit_mask = (1ULL<<CALIPERS_CLK_PIN);
	//set as input mode
	io_conf.mode = GPIO_MODE_INPUT;
	gpio_config(&io_conf);

	//interrupt disabled
	io_conf.intr_type = GPIO_INTR_DISABLE;
	//bit mask of the pins
	io_conf.pin_bit_mask = (1ULL<<CALIPERS_DATA_PIN);
	//set as input mode
	io_conf.mode = GPIO_MODE_INPUT;
	gpio_config(&io_conf);

	//create a queue to handle gpio event from isr
	calipers_bit_queue = xQueueCreate(25, sizeof(bool));
	xTaskCreate(readCalippersBit, "read_calipers_bits", 2048, NULL, 10, NULL);

	//install gpio isr service
	gpio_install_isr_service(ESP_INTR_FLAG_DEFAULT);
	//hook isr handler for specific gpio pin
	gpio_isr_handler_add(CALIPERS_CLK_PIN, gpio_isr_handler, (void*)CALIPERS_CLK_PIN);

	/* OLED display init */
	u8g2_esp32_hal_t u8g2_esp32_hal = U8G2_ESP32_HAL_DEFAULT;
	u8g2_esp32_hal.bus.i2c.sda = PIN_SDA;
	u8g2_esp32_hal.bus.i2c.scl = PIN_SCL;
	u8g2_esp32_hal_init(u8g2_esp32_hal);

	u8g2_Setup_ssd1306_i2c_128x32_univision_f(
		&u8g2, U8G2_R0,
		// u8x8_byte_sw_i2c,
		u8g2_esp32_i2c_byte_cb,
		u8g2_esp32_gpio_and_delay_cb);  // init u8g2 structure
	u8x8_SetI2CAddress(&u8g2.u8x8, 0x78);

	ESP_LOGI(tag, "u8g2_InitDisplay");
	u8g2_InitDisplay(&u8g2);// send init sequence to the display, display is in
				// sleep mode after this,

	ESP_LOGI(tag, "u8g2_SetPowerSave");
	u8g2_SetPowerSave(&u8g2, 0);  // wake up display
	ESP_LOGI(tag, "u8g2_ClearBuffer");
	u8g2_ClearBuffer(&u8g2);

	/* create tasks */

	//xTaskCreate(task_test_SSD1306i2c, "task_test_SSD1306i2c", 8192, NULL, 10, &taskTestDisplayHandle);
	xTaskCreate(RefreshDisplayU8G2, "RefreshDisplayU8G2", 8192, NULL, 10, &taskDisplayHandle);
	//xTaskCreate(Counter, "Counter", 4096, NULL, 10, &taskCounterHandle);

	printf("Minimum free heap size: %"PRIu32" bytes\n", esp_get_minimum_free_heap_size());
	vTaskDelete(NULL);
}

/*
 * Used URL:
 * https://esp32tutorials.com/esp32-esp-idf-freertos-tutorial-create-tasks/
 * https://esp32tutorials.com/esp32-gpio-interrupts-esp-idf/
 * https://github.com/espressif/esp-idf/blob/a8b6a70620b2dcbcd36e4d26ec6ff34ec515a0b1/examples/peripherals/gpio/generic_gpio/main/gpio_example_main.c
 */
