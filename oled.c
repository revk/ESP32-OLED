// Simple OLED display and text logic
// Copyright © 2019 Adrian Kennard Andrews & Arnold Ltd
const char TAG[] = "OLED";

#include <unistd.h>
#include <driver/i2c.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifdef	CONFIG_OLED_FONT0
#include "font0.h"
#endif
#ifdef	CONFIG_OLED_FONT1
#include "font1.h"
#endif
#ifdef	CONFIG_OLED_FONT2
#include "font2.h"
#endif
#ifdef	CONFIG_OLED_FONT3
#include "font3.h"
#endif
#ifdef	CONFIG_OLED_FONT4
#include "font4.h"
#endif
#ifdef	CONFIG_OLED_FONT5
#include "font5.h"
#endif

static uint8_t const *fonts[] = {
#ifdef	CONFIG_OLED_FONT0
   font0,
#else
   NULL,
#endif
#ifdef	CONFIG_OLED_FONT1
   font1,
#else
   NULL,
#endif
#ifdef	CONFIG_OLED_FONT2
   font2,
#else
   NULL,
#endif
#ifdef	CONFIG_OLED_FONT3
   font3,
#else
   NULL,
#endif
#ifdef	CONFIG_OLED_FONT4
   font4,
#else
   NULL,
#endif
#ifdef	CONFIG_OLED_FONT5
   font5,
#else
   NULL,
#endif
};

static uint8_t oled[CONFIG_OLED_WIDTH * CONFIG_OLED_HEIGHT * 8 / CONFIG_OLED_BPP];

static TaskHandle_t oled_task_id=NULL;
static SemaphoreHandle_t oled_mutex = NULL;
static int8_t oled_port = 0;
static int8_t oled_address = 0;
static int8_t oled_flip=0;
static volatile uint8_t oled_changed = 1;
static volatile uint8_t oled_update = 0;
static uint8_t oled_contrast = 127;

static void
oled_task (void *p)
{
   int try = 10;
   esp_err_t e;
   while (try--)
   {
      xSemaphoreTake (oled_mutex, portMAX_DELAY);
      oled_changed = 0;
      i2c_cmd_handle_t t = i2c_cmd_link_create ();
      i2c_master_start (t);
      i2c_master_write_byte (t, (oled_address << 1) | I2C_MASTER_WRITE, true);
      i2c_master_write_byte (t, 0x00, true);    // Cmds
      i2c_master_write_byte (t, 0xA5, true);    // White
      i2c_master_write_byte (t, 0xAF, true);    // On
      i2c_master_write_byte (t, 0xA0, true);    // Remap
      i2c_master_write_byte (t, oled_flip ? 0x52 : 0x41, true);  // Match display
      i2c_master_stop (t);
      e = i2c_master_cmd_begin (oled_port, t, 10 / portTICK_PERIOD_MS);
      i2c_cmd_link_delete (t);
      xSemaphoreGive (oled_mutex);
      if (!e)
         break;
      sleep (1);
   }
   if (e)
   {
      ESP_LOGE (TAG, "Configuration failed %s", esp_err_to_name (e));
      vTaskDelete (NULL);
      return;
   }

   while (1)
   {                            // Update
      if (!oled_changed)
      {
         usleep (100000);
         continue;
      }
      xSemaphoreTake (oled_mutex, portMAX_DELAY);
      oled_changed = 0;
      i2c_cmd_handle_t t;
      e = 0;
      if (oled_update < 2)
      {                         // Set up
         t = i2c_cmd_link_create ();
         i2c_master_start (t);
         i2c_master_write_byte (t, (oled_address << 1) | I2C_MASTER_WRITE, true);
         i2c_master_write_byte (t, 0x00, true); // Cmds
         if (oled_update)
            i2c_master_write_byte (t, 0xA4, true);      // Normal mode
         i2c_master_write_byte (t, 0x81, true); // Contrast
         i2c_master_write_byte (t, oled_contrast, true);        // Contrast
         i2c_master_write_byte (t, 0x15, true); // Col
         i2c_master_write_byte (t, 0x00, true); // 0
         i2c_master_write_byte (t, 0x7F, true); // 127
         i2c_master_write_byte (t, 0x75, true); // Row
         i2c_master_write_byte (t, 0x00, true); // 0
         i2c_master_write_byte (t, 0x7F, true); // 127
         i2c_master_stop (t);
         e = i2c_master_cmd_begin (oled_port, t, 100 / portTICK_PERIOD_MS);
         i2c_cmd_link_delete (t);
      }

      if (!e)
      {                         // data
         t = i2c_cmd_link_create ();
         i2c_master_start (t);
         i2c_master_write_byte (t, (oled_address << 1) | I2C_MASTER_WRITE, true);
         i2c_master_write_byte (t, 0x40, true); // Data
         i2c_master_write (t, oled, sizeof (oled), true);       // Buffer
         i2c_master_stop (t);
         e = i2c_master_cmd_begin (oled_port, t, 100 / portTICK_PERIOD_MS);
         i2c_cmd_link_delete (t);
      }
      if (e)
         ESP_LOGE (TAG, "Data failed %s", esp_err_to_name (e));
      if (!oled_update || e)
      {
         oled_update = 1;       // Resend data
         oled_changed = 1;
      } else
         oled_update = 2;       // All OK
      xSemaphoreGive (oled_mutex);
   }
}

void
oled_start (int8_t port, uint8_t address, int8_t scl, int8_t sda)
{                               // Start OLED task and display
   if (scl < 0 || sda < 0 || port < 0)
      return;
   oled_mutex = xSemaphoreCreateMutex ();       // Shared text access
   oled_port = port;
   oled_address = address;
   if (i2c_driver_install (oled_port, I2C_MODE_MASTER, 0, 0, 0))
   {
      ESP_LOGE (TAG, "I2C config fail");
      oled_port = -1;
   } else
   {
      i2c_config_t config = {
         .mode = I2C_MODE_MASTER,
         .sda_io_num = sda,
         .scl_io_num = scl,
         .sda_pullup_en = true,
         .scl_pullup_en = true,
         .master.clk_speed = 100000,
      };
      if (i2c_param_config (oled_port, &config))
      {
         i2c_driver_delete (oled_port);
         ESP_LOGE (TAG, "I2C config fail");
         oled_port = -1;
      } else
         i2c_set_timeout (oled_port, 160000);   // 2ms? allow for clock stretching
   }
   xTaskCreate (oled_task, "OLED", 8 * 1024, NULL, 2, &oled_task_id);
}

void
oled_lock (void)
{                               // Lock display task
   xSemaphoreTake (oled_mutex, portMAX_DELAY);
}

void oled_unlock (void)
{                               // Unlock display task
   xSemaphoreGive (oled_mutex);
}
