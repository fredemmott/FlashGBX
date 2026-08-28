/*
	LK Firmware version L15 (template)
	for <Device Name>
	Author: Lesserkuma (github.com/lesserkuma)
	Last Modified: 2025-04-18T22:10:43+02:00
*/

#ifndef _LK_MICROCODE_H_
#define _LK_MICROCODE_H_

#include "LK.h"

#ifdef __cplusplus
#include <cstdarg>
#else
#include <stdarg.h>
#endif

#define HARDWARE_MICROCODE
#define HARDWARE_CHROMATIC

#define LK_DEVICE_NAME 						"Chromatic"
#define LK_PCB_VERSION						1
#define LK_POWER_CONTROL_SUPPORT			true
#define LK_BOOTLOADER_RESET_SUPPORT			false
#define LK_CART_PRESENCE_SWITCH_SUPPORT		false
#define LK_CART_MODE_SWITCH_SUPPORT			false

#define LK_ASYNC					true
#if (!LK_ASYNC)
#define LK_ASYNC_FLUSH(DATA, LEN) {}
#endif

uint8_t LK2MC_ping(uint8_t);
void LK2MC_verify_data(uint8_t expected);
void LK2MC_set_variable(uint8_t size, uint32_t key, uint32_t value);
void LK2MC_dprint(const char*, va_list);
void LK2MC_flush(uint8_t* data, uint16_t len);

/* This is a smell as it breaks the abstraction, but given different flash
 * methods submit different numbers of status register checks, we need to track
 * how many status register responses we're expecting.
 *
 * We could return the count from the various flash method implementation
 * functions, but that would be brittle, hard to test, and require changing the
 * signature of all of them, making rebasing/merging future changes more
 * difficult. */
uint32_t LK2MC_get_pending_verify_status_register_count();
void LK2MC_verify_status_register();
// returns LK_STATUS_OK | LK_STATUS_ERROR
uint8_t LK2MC_verify_status_register_flush(uint8_t* buffer, uint32_t count);

#define LK_DEVICE_PING(COOKIE) LK2MC_ping(COOKIE)
#define LK_DEVICE_ON_SET_VARIABLE(SIZE, KEY, VALUE) LK2MC_set_variable(SIZE, KEY, VALUE)

#define LK_ASYNC_FLUSH(DATA, LEN) LK2MC_flush(DATA, LEN)
#define LK_ASYNC_VERIFY_DATA(COMP) LK2MC_verify_data(COMP)
#define LK_ASYNC_VERIFY_STATUS_REGISTER() LK2MC_verify_status_register()
#define LK_ASYNC_VERIFY_STATUS_REGISTER_FLUSH(DATA, LEN) LK2MC_verify_status_register_flush(DATA, LEN)
#define LK_ASYNC_PENDING_VERIFY_STATUS_REGISTER_COUNT() LK2MC_get_pending_verify_status_register_count()

#ifndef LK_DEVICE_NO_DPRINT
inline void dprint(const char* const fmt, ...) {
	va_list args;
	va_start(args, fmt);
	LK2MC_dprint(fmt, args);
	va_end(args);
}
#endif

void LK2MC_DELAY_100NS(uint8_t);
void LK2MC_DELAY_MICROS(uint32_t);
// 60mhz = 16.667ns ticks, and 3 ticks per instruction; while a NOP takes 3 ticks,
// given:
//
//     FOO, NOP, BAR
//
// ... there are at least 6 ticks between FOO executing and BAR executing
//
// So, we can ensure 100ns between instructions by queueing a single nop
#define _delay_100ns()						LK2MC_DELAY_100NS(1)
#define _delay_200ns()						LK2MC_DELAY_100NS(2)
#define _delay_300ns()						LK2MC_DELAY_100NS(3)
#define _delay_400ns()						LK2MC_DELAY_100NS(4)
#define _delay_500ns()						LK2MC_DELAY_100NS(5)
#define _delay_us(us)						LK2MC_DELAY_MICROS(us)
#define _delay_ms(ms)						_delay_us(ms * 1000)
#define _delay_dmg_slow_access()			_delay_us(2)

#define _timeout_init()						time_start = TIMESTAMP_NOW();
#define _timeout_reset()					time_start = 0;
#define _timeout_check()					((time_start > 0) && (TIMESTAMP_NOW() - time_start > 500))

/* Values match lk_types.sv in the Chromatic verilog */
#define LK2MC_SET_PINS_A_MASK       (1 << 7)
#define LK2MC_SET_PINS_B_MASK       (1 << 6)
#define LK2MC_SET_PINS_COMMAND_MASK (LK2MC_SET_PINS_A_MASK | LK2MC_SET_PINS_B_MASK)

#define PIN_CLK								(0 | LK2MC_SET_PINS_A_MASK)
#define PIN_WR								(1 | LK2MC_SET_PINS_A_MASK)
#define PIN_RD								(2 | LK2MC_SET_PINS_A_MASK)
#define PIN_CS								(3 | LK2MC_SET_PINS_A_MASK)

#define LK2MC_PIN_A15                                                   (0 | LK2MC_SET_PINS_B_MASK)
#define PIN_CS2								(1 | LK2MC_SET_PINS_B_MASK) // "CS2" on AGB, "RST" on DMG
#define PIN_AUDIO							(2 | LK2MC_SET_PINS_B_MASK)
#define VOLTAGE_SELECT						/**/
void LK2MC_SET_PIN(uint8_t pin, uint8_t high);
#define PIN_WR_H()							LK2MC_SET_PIN(PIN_WR, 1)
#define PIN_WR_L()							LK2MC_SET_PIN(PIN_WR, 0)
#define PIN_RD_H()							LK2MC_SET_PIN(PIN_RD, 1)
#define PIN_RD_L()							LK2MC_SET_PIN(PIN_RD, 0)
#define PIN_CS_H()							LK2MC_SET_PIN(PIN_CS, 1)
#define PIN_CS_L()							LK2MC_SET_PIN(PIN_CS, 0)
#define PIN_CS2_H()							LK2MC_SET_PIN(PIN_CS2, 1)
#define PIN_CS2_L()							LK2MC_SET_PIN(PIN_CS2, 0)
#define PIN_AUDIO_H()							LK2MC_SET_PIN(PIN_AUDIO, 1)
#define PIN_AUDIO_L()							LK2MC_SET_PIN(PIN_AUDIO, 0)
#define PIN_CLK_H()							LK2MC_SET_PIN(PIN_CLK, 1)
#define PIN_CLK_L()							LK2MC_SET_PIN(PIN_CLK, 0)
void LK2MC_SET_ADDR_PIN(uint8_t pin, uint8_t high);
#define PIN_ADDR_H(pin)						LK2MC_SET_ADDR_PIN(pin, 1)
#define PIN_ADDR_L(pin)						LK2MC_SET_ADDR_PIN(pin, 0)

#define CART_POWER_ON()						{}
#define CART_POWER_OFF()					{}
#define ACTIVITY_LED_ON()					{}
#define ACTIVITY_LED_OFF()					{}
#define SET_VOLTAGE_3_3V()					{}
#define SET_VOLTAGE_5V()					{}
#define AUTO_POWEROFF_RESUME()				{}
#define AUTO_POWEROFF_SUSPEND()				{}

uint32_t LK2MC_TIMESTAMP_NOW();
#define TIMESTAMP_NOW()						LK2MC_TIMESTAMP_NOW()

#define RAW_PINS_DIR_OUT()					{}
#define RAW_PINS_DIR_IN()					{}

// Match lk_types.sv in the FPGA
#define TRISTATE_AUDIO 0
#define TRISTATE_DATA 1
#define TRISTATE_ADDRESS 2
void LK2MC_OUTPUT_ENABLE(uint8_t tristate_pin, uint8_t oe);


#define PIN_AUDIO_DIR_OUT()					LK2MC_OUTPUT_ENABLE(TRISTATE_AUDIO, 1)
#define PIN_AUDIO_DIR_IN()					LK2MC_OUTPUT_ENABLE(TRISTATE_AUDIO, 0)

#define PULLUPS_ON()						{}

#define PULLUPS_OFF()						{}

void lk_recv_from_host(uint8_t* data, uint16_t count);
void lk_send_to_host(const uint8_t* data, uint16_t count);
inline void lk_send_byte_to_host(const uint8_t data) {
    lk_send_to_host(&data, 1);
}
#define CONN_RECV(data, count)				lk_recv_from_host(data, count)
#define CONN_SEND_BYTE(data)				lk_send_byte_to_host(data)
#define CONN_SEND(data, count)				lk_send_to_host(data, count)
#define BOOTLOADER_RESET()					{}

#define DISABLE_INTERRUPTS()				{}
#define ENABLE_INTERRUPTS()					{}

// #define CART_PRESENCE_SWITCH_GET()			() // 0 = off, 1 = on
// #define CART_MODE_SWITCH_GET()				() // 0 = AGB, 1 = DMG

// Moved to LK.h so it can affect the size of data_buffer
//#define CHUNK_MAX_LEN						4096


// GB/GBC
void LK2MC_DMG_ADDR_SET(uint16_t);
void LK2MC_DMG_DATA_SET(uint8_t);
#define RAW_DMG_ADDR_SET(addr)				LK2MC_DMG_ADDR_SET(addr)
#define RAW_DMG_DATA_SET(data)				LK2MC_DMG_DATA_SET(data)

#define RAW_DMG_ADDR_DIR_OUT()				LK2MC_OUTPUT_ENABLE(TRISTATE_ADDRESS, 1)
#define RAW_DMG_ADDR_DIR_IN()				LK2MC_OUTPUT_ENABLE(TRISTATE_ADDRESS, 0)
#define RAW_DMG_DATA_DIR_OUT()				LK2MC_OUTPUT_ENABLE(TRISTATE_DATA, 1)
#define RAW_DMG_DATA_DIR_IN()				LK2MC_OUTPUT_ENABLE(TRISTATE_DATA, 0)

uint8_t LK2MC_DMG_DATA_GET();
#define RAW_DMG_DATA_GET()					LK2MC_DMG_DATA_GET()

// GBA
#define RAW_AGB_ADDR_SET(addr)				{}
#define RAW_AGB_DATA_SET(data)				{}

#define RAW_AGB_ADDR_DIR_OUT()				{}
#define RAW_AGB_ADDR_DIR_IN()				{}
#define RAW_AGB_DATA_DIR_OUT()				{}
#define RAW_AGB_DATA_DIR_IN()				{}

#define RAW_AGB_DATA_GET()					(0xFF) /* unsupported */

// GBA EEPROM
#define PIN_A0_H()							PIN_ADDR_H(0)
#define PIN_A0_L()							PIN_ADDR_L(0)
#define PIN_A0_OUT()						{}
#define PIN_A0_IN()							{}

#endif
