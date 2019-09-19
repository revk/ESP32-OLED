// Simple OLED display and text logic
// Copyright © 2019 Adrian Kennard Andrews & Arnold Ltd

void oled_start(uint8_t i2cport,uint8_t i2caddress);
void oled_lock(void);
void oled_unlock(void);
void oled_set_contrast(uint8_t contrast);
void oled_clear(void);
int oled_text (int8_t size, int x, int y, char *t);
int oled_icon (int x, int y, const void *p, int w, int h);

