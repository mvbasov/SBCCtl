#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"

#include "ssd1306.h"

/*
 You have to set this config value with menuconfig

 for i2c
 CONFIG_MODEL
 CONFIG_SDA_GPIO
 CONFIG_SCL_GPIO
 CONFIG_RESET_GPIO
*/

#define tag "SBCCtl"

SSD1306_t dev;
TaskHandle_t taskDisplayHandle = NULL;
TaskHandle_t taskCounterHandle = NULL;
TaskHandle_t ISR = NULL;
static QueueHandle_t calipers_bit_queue = NULL;

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

void RefreshDisplay(void *arg)
{
	while(1){
		if(stripeThickness != stripeThicknessPrev) {
			ssd1306_display_text(&dev, 1, "Thickness:", 10, false);
			if(unitMM) {
				sprintf(&lineChar[0], "    %s%.2f mm   ", signPlus?" ":"-", stripeThickness/100.00);
			} else {
				sprintf(&lineChar[0], "    %s%.4f in   ", signPlus?" ":"-", stripeThickness*5/10000.00);
			}
			ssd1306_display_text(&dev, 3, lineChar, strlen(lineChar), false);
			ESP_LOGI(tag, "Thickness: %s", lineChar);
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

void app_main(void)
{
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

	ESP_LOGI(tag, "INTERFACE is i2c");
	ESP_LOGI(tag, "CONFIG_SDA_GPIO=%d",CONFIG_SDA_GPIO);
	ESP_LOGI(tag, "CONFIG_SCL_GPIO=%d",CONFIG_SCL_GPIO);
	ESP_LOGI(tag, "CONFIG_RESET_GPIO=%d",CONFIG_RESET_GPIO);
	i2c_master_init(&dev, CONFIG_SDA_GPIO, CONFIG_SCL_GPIO, CONFIG_RESET_GPIO);


#if CONFIG_FLIP
	dev._flip = true;
	ESP_LOGW(tag, "Flip upside down");
#endif

	ESP_LOGI(tag, "Panel is 128x64");
	ssd1306_init(&dev, 128, 64);
	ssd1306_clear_screen(&dev, false);
	ssd1306_contrast(&dev, 0xff);

	xTaskCreate(RefreshDisplay, "RefreshDisplay", 8192, NULL, 10, &taskDisplayHandle);
	//xTaskCreate(Counter, "Counter", 4096, NULL, 10, &taskCounterHandle);

	printf("Minimum free heap size: %"PRIu32" bytes\n", esp_get_minimum_free_heap_size());
}

/*
 * Used URL:
 * https://esp32tutorials.com/esp32-esp-idf-freertos-tutorial-create-tasks/
 * https://esp32tutorials.com/esp32-gpio-interrupts-esp-idf/
 * https://github.com/espressif/esp-idf/blob/a8b6a70620b2dcbcd36e4d26ec6ff34ec515a0b1/examples/peripherals/gpio/generic_gpio/main/gpio_example_main.c
 */
