#pragma once
#include <stddef.h>
#include <stdint.h>
/* Serial EEPROM on the Wily Wars cartridge, see mmww_eeprom.c. */
void   mmww_eeprom_install(void);
void   mmww_eeprom_sram_loaded(const char *srm_path, const uint8_t *data, size_t size, int loaded);
size_t mmww_eeprom_rb_save(void *dst, size_t cap);
int    mmww_eeprom_rb_load(const void *src, size_t len);
void   mmww_eeprom_frame(void);
