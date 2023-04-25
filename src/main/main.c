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

void app_main(void)
{
	SSD1306_t dev;
	int center, top, bottom;
	//char lineChar[20];

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

        while (1) {
  		ssd1306_clear_screen(&dev, false);
		ssd1306_contrast(&dev, 0xff);
        	ssd1306_display_text_x3(&dev, 3, "Hello", 5, false);
        	vTaskDelay(3000 / portTICK_PERIOD_MS);

		top = 2;
		center = 3;
		bottom = 8;
		ssd1306_display_text(&dev, 0, "SSD1306 128x64", 14, false);
		ssd1306_display_text(&dev, 1, "ABCDEFGHIJKLMNOP", 16, false);
		ssd1306_display_text(&dev, 2, "abcdefghijklmnop",16, false);
		ssd1306_display_text(&dev, 3, "Hello World!!", 13, false);
		ssd1306_display_text(&dev, 4, "SSD1306 128x64", 14, true);
		ssd1306_display_text(&dev, 5, "ABCDEFGHIJKLMNOP", 16, true);
		ssd1306_display_text(&dev, 6, "abcdefghijklmnop",16, true);
		ssd1306_display_text(&dev, 7, "Hello World!!", 13, true);
		vTaskDelay(3000 / portTICK_PERIOD_MS);

		// Invert
		ssd1306_clear_screen(&dev, true);
		ssd1306_contrast(&dev, 0xff);
		ssd1306_display_text(&dev, center, "  Next turn!", 12, true);
		vTaskDelay(3000 / portTICK_PERIOD_MS);

		// Fade Out
		ssd1306_fadeout(&dev);
		vTaskDelay(1000 / portTICK_PERIOD_MS);
	}

}
