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
#define fillArea 24053
#define minStripeWidth 200
#define maxStripeWidth 1600


TaskHandle_t taskTestDisplayHandle = NULL;
TaskHandle_t taskDisplayHandle = NULL;
TaskHandle_t taskCounterHandle = NULL;
TaskHandle_t ISR = NULL;
static QueueHandle_t calipers_bit_queue = NULL;
static QueueHandle_t length_enc_bits_queue = NULL;

u8g2_t u8g2;  // a structure which will contain all the data for one display
char lineChar[20];

uint32_t stripeThickness = 10; // in mm/100 
uint32_t stripeThicknessPrev = -10;
uint32_t stripeThicknessPrevC = -10;
uint32_t stripeThicknessBits = 0;
bool unitMM = true;
bool unitMMPrev = false;
bool signPlus = true;

//int32_t stripeLength = -100*1000*10; // length value in 0.1 mm
int32_t stripeLength = 0; // length value in 0.1 mm
int32_t stripeLengthPrev = -25;

uint32_t stripeWidth = 1000; // in mm/100
uint32_t stripeWidthPrev = 1055;

uint32_t percetInfill = 100;
uint32_t percetInfillPrev = 101;

#define ESP_INTR_FLAG_DEFAULT 0
#define CALIPERS_CLK_PIN  26
#define CALIPERS_DATA_PIN  27
#define LENGTH_ENC_PINA  33
#define LENGTH_ENC_PINB  32

#define GPIO_INPUT_PIN_SEL  (1ULL<<CALIPERS_DATA_PIN)
#define GPIO_INPUT_PIN_INT_NEG_SEL  (1ULL<<CALIPERS_CLK_PIN)
#define GPIO_INPUT_PIN_INT_ANY_SEL  ((1ULL<<LENGTH_ENC_PINA) | (1ULL<<LENGTH_ENC_PINB))

uint32_t absSimple(int32_t value) {
	return value > 0 ? value : (-1 * value);
}

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
			break;
		/*
		 * Length encoder queue data structure:
		 * 	0bSLO
		 * 	S - The source of interrupt (0 - A channel, 1 - B channel)
		 * 	L - Interrupt generated channel level
		 * 	O - Other channel level
		 */
		case LENGTH_ENC_PINA:
			char aa = 0<<2  | (gpio_get_level(LENGTH_ENC_PINA)<<1) | gpio_get_level(LENGTH_ENC_PINB);
			xQueueSendFromISR(length_enc_bits_queue, &aa, NULL);
			break;
		case LENGTH_ENC_PINB:
			char bb = 1<<2 | (gpio_get_level(LENGTH_ENC_PINB)<<1) | gpio_get_level(LENGTH_ENC_PINA);
			xQueueSendFromISR(length_enc_bits_queue, &bb, NULL);
			break;
		default:
	}
}

static void readCalippersBit(void *arg){
	bool level;
	uint16_t bitCount=0;
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
				//printf("cnt: %d", bitCount);
				//printf("\n");

				if(bitCount==24){
					stripeThickness = stripeThicknessBits;
					unitMM = mUnit;
					signPlus = mSign;
				}
				stripeThicknessBits = 0;
				bitCount=0;
			}
			switch (bitCount) {
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
			bitCount++;
			time = now;
		}
	}
}

void readLengthEncoderBits(void *ignore)
{
	for(;;) {
		char cc;
		if(xQueueReceive(length_enc_bits_queue, &cc, portMAX_DELAY)) {
			switch(cc) {
				case 0b000:
				case 0b011:
				case 0b101:
				case 0b110:
					stripeLength += 25;
					break;
				case 0b001:
				case 0b010:
				case 0b100:
				case 0b111:
					stripeLength -= 25;
					break;
				default:
			}
		}
	}

}
void setStripeWidth(void *ignore)
{
	while(1){
		if(absSimple(stripeThickness - stripeThicknessPrevC) > 2 && stripeThickness != 0) {
			ESP_LOGI(tag, "Diff: %ld, Prev: %ld, Cur: %ld", absSimple(stripeThickness - stripeThicknessPrevC), stripeThicknessPrevC, stripeThickness);
			stripeWidth = (percetInfill * fillArea) / (stripeThickness * 100);
			if(stripeWidth > maxStripeWidth) stripeWidth = maxStripeWidth;
			if(stripeWidth < minStripeWidth) stripeWidth = minStripeWidth;
			stripeThicknessPrevC = stripeThickness;
		}
		vTaskDelay(50/portTICK_PERIOD_MS);
	}
}

void RefreshDisplayU8G2(void *arg)
{
	while(1){
		if((stripeThickness != stripeThicknessPrev) || (unitMM != unitMMPrev)) {
			if(unitMM) {
				sprintf(&lineChar[0], "%s%.2f mm  ", signPlus?" ":"-", stripeThickness/100.00);
				u8g2_SetDrawColor(&u8g2, 0);
				u8g2_DrawBox(&u8g2, 0, 0, 64, 16);
				u8g2_SetDrawColor(&u8g2, 1);
				u8g2_DrawFrame(&u8g2, 0, 0, 65, 17);
			} else {
				sprintf(&lineChar[0], "%s%.4fin ", signPlus?" ":"-", stripeThickness*5/10000.00);
				u8g2_DrawBox(&u8g2, 0, 0, 64, 16);
				u8g2_SetDrawColor(&u8g2, 0);
			}
			u8g2_SetFont(&u8g2, u8g2_font_6x10_mf);
			u8g2_DrawStr(&u8g2, 4, 12, lineChar);
			u8g2_SetDrawColor(&u8g2, 1);

			u8g2_SendBuffer(&u8g2);

			ESP_LOGI(tag, "Thickness: %s", lineChar); // for monitor logging
			//printf("%s%.2f\n", signPlus?"":"-", stripeThickness/100.00); // for serial plotter
			stripeThicknessPrev = stripeThickness;
		}
		if(stripeLength != stripeLengthPrev) {
			u8g2_SetFont(&u8g2, u8g2_font_6x10_mf);
			sprintf(&lineChar[0], "%.2f m ", stripeLength/10000.0);
			u8g2_DrawStr(&u8g2, 71, 12, lineChar);
			u8g2_SendBuffer(&u8g2);
			ESP_LOGI(tag, "Length: %.2f m", stripeLength/10000.0);
			stripeLengthPrev = stripeLength;
		}
		if(stripeWidth != stripeWidthPrev) {
			u8g2_SetFont(&u8g2, u8g2_font_6x10_mf);
			sprintf(&lineChar[0], " %.2f mm ", stripeWidth/100.0);
			u8g2_DrawStr(&u8g2, 3, 28, lineChar);
			u8g2_SendBuffer(&u8g2);
			ESP_LOGI(tag, "Width: %.2f mm", stripeWidth/100.0);
			stripeWidthPrev = stripeWidth;
		}

		//percetInfill = ((stripeWidth * stripeThickness) * 100) / fillArea;
		if(percetInfill != percetInfillPrev) {
			u8g2_SetFont(&u8g2, u8g2_font_6x10_mf);
			sprintf(&lineChar[0], "%2ld %%", percetInfill);
			u8g2_DrawStr(&u8g2, 71, 28, lineChar);
			u8g2_SendBuffer(&u8g2);
			ESP_LOGI(tag, "Infill: %2ld %%", percetInfill);
			percetInfillPrev = percetInfill;
		}

		unitMMPrev = unitMM;
		// Reset length counter if thickness equal 0 and units is inch
		if (!unitMM && (stripeThickness == 0)) stripeLength = 0;

		vTaskDelay(50/portTICK_PERIOD_MS);
	}
}

void Counter(void *arg)
{
	while(1){
		stripeLength++;
		ESP_LOGI(tag, "Counter incremented to: %ld", stripeLength);
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
	//create a queue to handle gpio event from isr
	calipers_bit_queue = xQueueCreate(25, sizeof(bool));
	length_enc_bits_queue = xQueueCreate(10, sizeof(char));

	/* GPIO SETUP */
	//zero-initialize the config structure.
	gpio_config_t io_conf = {};
	//interrupt of rising edge
	io_conf.intr_type = GPIO_INTR_NEGEDGE;
	//bit mask of the pins
	io_conf.pin_bit_mask = GPIO_INPUT_PIN_INT_NEG_SEL;
	//set as input mode
	io_conf.mode = GPIO_MODE_INPUT;
	gpio_config(&io_conf);

	//interrupt of both edge
	io_conf.intr_type = GPIO_INTR_ANYEDGE;
	//bit mask of the pins
	io_conf.pin_bit_mask = GPIO_INPUT_PIN_INT_ANY_SEL;
	//set as input mode
	io_conf.mode = GPIO_MODE_INPUT;
	gpio_config(&io_conf);

	//interrupt disabled
	io_conf.intr_type = GPIO_INTR_DISABLE;
	//bit mask of the pins
	io_conf.pin_bit_mask = GPIO_INPUT_PIN_SEL;
	//set as input mode
	io_conf.mode = GPIO_MODE_INPUT;
	gpio_config(&io_conf);

	//install gpio isr service
	gpio_install_isr_service(ESP_INTR_FLAG_DEFAULT);
	//hook isr handler for specific gpio pin
	gpio_isr_handler_add(CALIPERS_CLK_PIN, gpio_isr_handler, (void*)CALIPERS_CLK_PIN);
	gpio_isr_handler_add(LENGTH_ENC_PINA, gpio_isr_handler, (void*)LENGTH_ENC_PINA);
	gpio_isr_handler_add(LENGTH_ENC_PINB, gpio_isr_handler, (void*)LENGTH_ENC_PINB);

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

	//u8g2_DrawRFrame(&u8g2, 0, 0, 128, 32, 3);
	u8g2_DrawFrame(&u8g2, 0, 0, 128, 32);
	u8g2_DrawHLine(&u8g2, 0, 16, 128);
	u8g2_DrawVLine(&u8g2, 64, 0, 32);
	u8g2_SendBuffer(&u8g2);

	/* create tasks */

	//ESP_LOGI(tag, "Start task: test SSD1306 display");
	//xTaskCreate(task_test_SSD1306i2c, "task_test_SSD1306i2c", 8192, NULL, 10, &taskTestDisplayHandle);

	//ESP_LOGI(tag, "Start task: test counter");
	//xTaskCreate(Counter, "Counter", 4096, NULL, 10, &taskCounterHandle);

	ESP_LOGI(tag, "Start task: refresh display");
	xTaskCreate(RefreshDisplayU8G2, "RefreshDisplayU8G2", 8192, NULL, 10, &taskDisplayHandle);

	ESP_LOGI(tag, "Start task: read calipers bits");
	xTaskCreate(readCalippersBit, "read_calipers_bits", 2048, NULL, 10, NULL);

	ESP_LOGI(tag, "Start task: read length encoder bits");
	xTaskCreate(readLengthEncoderBits, "read_length_encoder_bits", 2048, NULL, 10, NULL);

	ESP_LOGI(tag, "Start task: set stripe width");
	xTaskCreate(setStripeWidth, "read_length_encoder_bits", 2048, NULL, 10, NULL);

	/* finish app_main task */
	printf("Minimum free heap size: %"PRIu32" bytes\n", esp_get_minimum_free_heap_size());
	vTaskDelete(NULL);
}

/*
 * Used URL:
 * https://esp32tutorials.com/esp32-esp-idf-freertos-tutorial-create-tasks/
 * https://esp32tutorials.com/esp32-gpio-interrupts-esp-idf/
 * https://github.com/espressif/esp-idf/blob/a8b6a70620b2dcbcd36e4d26ec6ff34ec515a0b1/examples/peripherals/gpio/generic_gpio/main/gpio_example_main.c
 */
