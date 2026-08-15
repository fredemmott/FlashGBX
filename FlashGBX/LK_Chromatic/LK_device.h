/*
	LK Firmware version L15 (template)
	for <Device Name>
	Author: Lesserkuma (github.com/lesserkuma)
	Last Modified: 2025-04-18T22:10:43+02:00
*/

#ifndef _LK_DEVICENAME_H_
#define _LK_DEVICENAME_H_

#include "LK.h"

#define HARDWARE_DEVICENAME

#define LK_DEVICE_NAME 						"<Device Name>"
#define LK_PCB_VERSION						1
#define LK_POWER_CONTROL_SUPPORT			true
#define LK_BOOTLOADER_RESET_SUPPORT			false
#define LK_CART_PRESENCE_SWITCH_SUPPORT		false
#define LK_CART_MODE_SWITCH_SUPPORT			false

#define dprint(s, params...)				{}

// Delays including overhead, measured with a DSLogic U3Pro32
#define _delay_100ns()						{} // 160ns
#define _delay_200ns()						{ __NOP(); __NOP(); __NOP(); } // 250ns
#define _delay_300ns()						{ __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); } // 310ns
#define _delay_400ns()						{ __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); } // 400ns
#define _delay_500ns()						{ __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); } // 500ns
#define _delay_us(us)						{ for (u32 delay = 0; delay < us; delay++) { __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); } }
#define _delay_ms(ms)						_delay_us(ms * 1000)
#define _delay_dmg_slow_access()			_delay_us(2)

#define _timeout_init()						time_start = TIMESTAMP_NOW();
#define _timeout_reset()					time_start = 0;
#define _timeout_check()					((time_start > 0) && (TIMESTAMP_NOW() - time_start > 500))

#define PIN_WR								/**/
#define PIN_RD								/**/
#define PIN_CS								/**/
#define PIN_CS2								/**/
#define PIN_AUDIO							/**/
#define PIN_CLK								/**/
#define VOLTAGE_SELECT						/**/
#define PIN_WR_H()							{}
#define PIN_WR_L()							{}
#define PIN_RD_H()							{}
#define PIN_RD_L()							{}
#define PIN_CS_H()							{}
#define PIN_CS_L()							{}
#define PIN_CS2_H()							{}
#define PIN_CS2_L()							{}
#define PIN_AUDIO_H()						{}
#define PIN_AUDIO_L()						{}
#define PIN_CLK_H()							{}
#define PIN_CLK_L()							{}
#define PIN_ADDR_H(pin)						{}
#define PIN_ADDR_L(pin)						{}

#define CART_POWER_ON()						{}
#define CART_POWER_OFF()					{}
#define ACTIVITY_LED_ON()					{}
#define ACTIVITY_LED_OFF()					{}
#define SET_VOLTAGE_3_3V()					{}
#define SET_VOLTAGE_5V()					{}
#define AUTO_POWEROFF_RESUME()				{}
#define AUTO_POWEROFF_SUSPEND()				{}
#define TIMESTAMP_NOW()						{}

#define RAW_PINS_DIR_OUT()					{}
#define RAW_PINS_DIR_IN()					{}

#define PIN_AUDIO_DIR_OUT()					{}
#define PIN_AUDIO_DIR_IN()					{}

#define PULLUPS_ON()						{}

#define PULLUPS_OFF()						{}

#define CONN_RECV(data, count)				{}
#define CONN_SEND_BYTE(data)				{}
#define CONN_SEND(data, count)				{}
#define BOOTLOADER_RESET()					{}

#define DISABLE_INTERRUPTS()				{}
#define ENABLE_INTERRUPTS()					{}

// #define CART_PRESENCE_SWITCH_GET()			() // 0 = off, 1 = on
// #define CART_MODE_SWITCH_GET()				() // 0 = AGB, 1 = DMG

#define CHUNK_MAX_LEN						64

// GB/GBC
#define RAW_DMG_ADDR_SET(addr)				{}
#define RAW_DMG_DATA_SET(data)				{}

#define RAW_DMG_ADDR_DIR_OUT()				{}
#define RAW_DMG_ADDR_DIR_IN()				{}
#define RAW_DMG_DATA_DIR_OUT()				{}
#define RAW_DMG_DATA_DIR_IN()				{}

#define RAW_DMG_DATA_GET()					{}

// GBA
#define RAW_AGB_ADDR_SET(addr)				{}
#define RAW_AGB_DATA_SET(data)				{}

#define RAW_AGB_ADDR_DIR_OUT()				{}
#define RAW_AGB_ADDR_DIR_IN()				{}
#define RAW_AGB_DATA_DIR_OUT()				{}
#define RAW_AGB_DATA_DIR_IN()				{}

#define RAW_AGB_DATA_GET()					{}

// GBA EEPROM
#define PIN_A0_H()							PIN_ADDR_H(0)
#define PIN_A0_L()							PIN_ADDR_L(0)
#define PIN_A0_OUT()						{}
#define PIN_A0_IN()							{}

#endif
