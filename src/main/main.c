#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "ssd1306.h"
#include "font8x8_basic.h"

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
char lineChar[20];
int stripeLength = 123;

void RefreshDisplay(void *arg)
{
    while(1){
        sprintf(&lineChar[0], "Length: %04d", stripeLength);
        ssd1306_display_text(&dev, 2, lineChar, strlen(lineChar), false);
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

	xTaskCreate(RefreshDisplay, "RefreshDisplay", 4096, NULL, 10, &taskDisplayHandle);
	xTaskCreate(Counter, "Counter", 4096, NULL, 10, &taskCounterHandle);

        while (1) {
		vTaskDelay(1000 / portTICK_PERIOD_MS);
	}
}
