/*
	LK Firmware version L15
	Author: Lesserkuma (github.com/Lesserkuma)
	Last Modified: 2026-05-25T01:55:45+02:00
*/

#include "LK.h"

// *** GLOBAL VARIABLES *** //
u32 lk_runtime = 0;
u32 _lk_var32[LK_NUM_OF_VARIABLES_32BIT];
u16 _lk_var16[LK_NUM_OF_VARIABLES_16BIT];
u8 _lk_var8[LK_NUM_OF_VARIABLES_8BIT];
u32 _lk_flashcmd_addr[32];
u16 _lk_flashcmd_data[32];
u8 _lk_bankcmd_num = 0;
u32 _lk_bankcmd_addr[3];
u8 _lk_bankcmd_mode[3];
u8 data_buffer[0x1000];
u16 flash_write_cycle[3][2];
u32 time_start = 0;
bool auto_off_timer_suspended = false;
bool activity_done = false;
u32 activity_last_run = 0;

// This function should be called in the main loop when data is received.
void lk_loop(u8 command) {
	AUTO_POWEROFF_SUSPEND();
	_timeout_reset();
	activity_done = false;

	switch (command) {
		case LK_CMD_QUERY_FW_INFO:
			{
				dprint("Welcome to the LK Firmware for FlashGBX by Lesserkuma!\r\n");
				lk_conn_send_u8(8);
				lk_conn_send_u8('L');
				lk_conn_send_u16(LK_FIRMWARE_VERSION);
				lk_conn_send_u8(LK_PCB_VERSION);
				lk_conn_send_u32(__BUILD_TIMESTAMP__);
				lk_conn_send_u8(sizeof(LK_DEVICE_NAME));
				lk_conn_send((u8*)LK_DEVICE_NAME, sizeof(LK_DEVICE_NAME));
				lk_conn_send_u8(LK_POWER_CONTROL_SUPPORT | (LK_CART_PRESENCE_SWITCH_SUPPORT << 1) | (LK_CART_MODE_SWITCH_SUPPORT << 2));
				lk_conn_send_u8(LK_BOOTLOADER_RESET_SUPPORT);
			}
			break;

		case LK_CMD_PING:
			{
				u8 value = lk_conn_recv_u8();
				// lk_conn_send_u8(~value);

				// As LK 'firmware' is running on the host,
				// explicitly ping the device
				lk_conn_send_u8(LK_Chromatic_ping(value));
			}
			break;

		case LK_CMD_DEBUG:
			{
				//lk_conn_recv(data_buffer, 0x800);
				//lk_conn_send(data_buffer, 0x800);
				
				// test no delay
				for (u32 i = 0; i < 20; i++) {
					PIN_CLK_H();
					PIN_CLK_L();
					PIN_CLK_H();
					PIN_CLK_L();
				}
				_delay_ms(10);

				// test nanoseconds
				for (u32 i = 0; i < 20; i++) {
					PIN_CLK_H();
					_delay_100ns();
					PIN_CLK_L();
					_delay_100ns();
					PIN_CLK_H();
					_delay_100ns();
					PIN_CLK_L();
					_delay_100ns();
				}
				_delay_ms(10);

				for (u32 i = 0; i < 20; i++) {
					PIN_CLK_H();
					_delay_200ns();
					PIN_CLK_L();
					_delay_200ns();
					PIN_CLK_H();
					_delay_200ns();
					PIN_CLK_L();
					_delay_200ns();
				}
				_delay_ms(10);

				for (u32 i = 0; i < 20; i++) {
					PIN_CLK_H();
					_delay_300ns();
					PIN_CLK_L();
					_delay_300ns();
					PIN_CLK_H();
					_delay_300ns();
					PIN_CLK_L();
					_delay_300ns();
				}
				_delay_ms(10);

				for (u32 i = 0; i < 20; i++) {
					PIN_CLK_H();
					_delay_400ns();
					PIN_CLK_L();
					_delay_400ns();
					PIN_CLK_H();
					_delay_400ns();
					PIN_CLK_L();
					_delay_400ns();
				}
				_delay_ms(10);

				for (u32 i = 0; i < 20; i++) {
					PIN_CLK_H();
					_delay_500ns();
					PIN_CLK_L();
					_delay_500ns();
					PIN_CLK_H();
					_delay_500ns();
					PIN_CLK_L();
					_delay_500ns();
				}
				_delay_ms(10);

				// test microseconds
				for (u32 i = 0; i < 20; i++) {
					PIN_CLK_H();
					_delay_us(20);
					PIN_CLK_L();
					_delay_us(20);
					PIN_CLK_H();
					_delay_us(20);
					PIN_CLK_L();
					_delay_us(20);
				}
				_delay_ms(10);

				// test milliseconds
				for (u32 i = 0; i < 20; i++) {
					PIN_CLK_H();
					_delay_ms(2);
					PIN_CLK_L();
					_delay_ms(2);
					PIN_CLK_H();
					_delay_ms(2);
					PIN_CLK_L();
					_delay_ms(2);
				}

				lk_conn_send_u8(LK_STATUS_OK);
			}
			break;
		
		case LK_CMD_SET_MODE_DMG:
			{
				lk_set_mode(LK_MODE_DMG);

				// Unlock Sachen carts
				for (u16 i=0; i<0x30; i++) {
					PIN_ADDR_L(15);
					_delay_200ns();
					PIN_ADDR_H(15);
					_delay_200ns();
				}

				lk_conn_send_u8(LK_STATUS_OK);
			}
			break;
		
		case LK_CMD_SET_MODE_AGB:
			{
				lk_set_mode(LK_MODE_AGB);
				lk_conn_send_u8(LK_STATUS_OK);
			}
			break;
		
		case LK_CMD_SET_VOLTAGE_3_3V:
			{
				_lk_var8[LK_VAR8_CART_MODE] = LK_MODE_AGB;
				SET_VOLTAGE_3_3V();
				lk_conn_send_u8(LK_STATUS_OK);
			}
			break;
		
		case LK_CMD_SET_VOLTAGE_5V:
			{
				_lk_var8[LK_VAR8_CART_MODE] = LK_MODE_DMG;
				SET_VOLTAGE_5V();
				lk_conn_send_u8(LK_STATUS_OK);
			}
			break;

		case LK_CMD_CART_PWR_ON:
			{
				lk_cart_power_on();
				lk_conn_send_u8(LK_STATUS_OK);
			}
			break;
		
		case LK_CMD_CART_PWR_OFF:
			{
				lk_cart_power_off();
				lk_conn_send_u8(LK_STATUS_OK);
			}
			break;

		case LK_CMD_QUERY_CART_PWR:
			{
				lk_conn_send_u8(_lk_var8[LK_VAR8_CART_POWERED]);
			}
			break;

		case LK_CMD_SET_VARIABLE:
			// args: variable's size (1 byte), variable's key (4 byte), variable's value (4 byte)
			{
				u8 size = lk_conn_recv_u8();
				u32 key = lk_conn_recv_u32();
				u32 value = lk_conn_recv_u32();
				
				if (size == 1) {
					_lk_var8[key] = value;

					if (key == LK_VAR8_FLASH_WE_PIN) { // thanks simonK
						if (value == LK_FLASH_WE_PIN_AUDIO) {
							PIN_AUDIO_DIR_OUT();
						} else {
							PIN_AUDIO_DIR_IN();
						}
					}
					if ((_lk_var8[LK_VAR8_CART_MODE] == LK_MODE_AGB) && (key == LK_VAR8_AGB_IRQ_ENABLED)) {
						if (value == 1) {
							lk_dmg_flash_enable_audio(true);
						} else {
							lk_dmg_flash_enable_audio(false);
						}
					}
					if ((_lk_var8[LK_VAR8_CART_MODE] == LK_MODE_DMG) && (key == LK_VAR8_DMG_AUDIO_ENABLED)) {
						if (value == 1) {
							lk_dmg_flash_enable_audio(true);
						} else {
							lk_dmg_flash_enable_audio(false);
						}
					}

				} else if (size == 2) {
					_lk_var16[key] = value;
					if (key == LK_VAR16_DMG_ROM_BANK) {
						_lk_var16[LK_VAR16_LAST_BANK_ACCESSED] = value & 0xFF;
					}
					
				} else if (size == 4) {
					_lk_var32[key] = value;
					if (key == LK_VAR32_AUTO_POWEROFF_TIME) {
						ACTIVITY_LED_ON();
					}
				}

				lk_conn_send_u8(LK_STATUS_OK);
			}
			break;

		case LK_CMD_SET_FLASH_CMD:
			// args: flash command set, method, we_pin and 6 sets of address and value
			{
				_lk_var8[LK_VAR8_FLASH_COMMAND_SET] = lk_conn_recv_u8();
				_lk_var8[LK_VAR8_FLASH_METHOD] = lk_conn_recv_u8(); // FLASH_METHOD_BUFFERED or FLASH_METHOD_UNBUFFERED
				_lk_var8[LK_VAR8_FLASH_WE_PIN] = lk_conn_recv_u8(); // WR/AUDIO/WR+RESET
				for (u8 x = 0; x < 6; x++) {
					_lk_flashcmd_addr[x] = lk_conn_recv_u32();
					_lk_flashcmd_data[x] = lk_conn_recv_u16();
				}
				lk_dmg_flash_enable_audio((_lk_var8[LK_VAR8_FLASH_WE_PIN] == LK_FLASH_WE_PIN_AUDIO) ? true : false);
				for (u8 x = 0; x < 3; x++) {
					flash_write_cycle[x][0] = _lk_flashcmd_addr[x];
					flash_write_cycle[x][1] = _lk_flashcmd_data[x];
				}
				lk_conn_send_u8(LK_STATUS_OK);
			}
			break;

		case LK_CMD_SET_ADDR_AS_INPUTS:
			{
				RAW_DMG_ADDR_DIR_IN();
				lk_conn_send_u8(LK_STATUS_OK);
			}
			break;

		case LK_CLK_TOGGLE:
			// args: number of toggles
			{
				u32 num = lk_conn_recv_u32();
				ACTIVITY_LED_ON();
				while (num > 0) {
					PIN_CLK_L();
					_delay_500ns();
					PIN_CLK_H();
					_delay_500ns();
					num--;
				}
				lk_conn_send_u8(LK_STATUS_OK);
			}
			break;

		case LK_CMD_ENABLE_PULLUPS:
			{
				PULLUPS_ON();
				_delay_ms(5);
				_lk_var8[LK_VAR8_PULLUPS_ENABLED] = true;
				lk_conn_send_u8(LK_STATUS_OK);
			}
			break;

		case LK_CMD_DISABLE_PULLUPS:
			{
				PULLUPS_OFF();
				_delay_ms(5);
				_lk_var8[LK_VAR8_PULLUPS_ENABLED] = false;
				lk_conn_send_u8(LK_STATUS_OK);
			}
			break;
			
		case LK_CMD_DMG_MBC_RESET:
			{
				PIN_CS2_L();
				_delay_ms(5);
				PIN_CS2_H();
				lk_conn_send_u8(LK_STATUS_OK);
			}
			break;

		case LK_CMD_DMG_CART_READ:
			// note: client must set address and transfer size variable beforehand
			{
				ACTIVITY_LED_ON();
				lk_dmg_cart_read_data();
			}
			break;
		
		case LK_CMD_DMG_CART_WRITE:
			// args: address (4 byte), value (1 byte)
			{
				u32 addr = lk_conn_recv_u32();
				u8 data = lk_conn_recv_u8();
				ACTIVITY_LED_ON();
				lk_dmg_cart_write_byte(addr, data);
				lk_conn_send_u8(LK_STATUS_OK);
			}
			break;

		case LK_CMD_DMG_CART_WRITE_SRAM:
			// note: client must set address and transfer size variable beforehand
			{
				lk_conn_recv(data_buffer, _lk_var16[LK_VAR16_TRANSFER_SIZE]);
				ACTIVITY_LED_ON();
				for (u32 x = 0; x < _lk_var16[LK_VAR16_TRANSFER_SIZE]; x++) {
					lk_dmg_cart_write_byte(_lk_var32[LK_VAR32_ADDRESS]++, data_buffer[x]);
				}
				lk_conn_send_u8(LK_STATUS_OK);
			}
			break;
		
		case LK_CMD_DMG_MBC6_MMSA_WRITE_FLASH:
			// note: client must set address and transfer size variable beforehand
			{
				lk_conn_recv(data_buffer, _lk_var16[LK_VAR16_TRANSFER_SIZE]);
				ACTIVITY_LED_ON();
				lk_dmg_mbc6_mmsa_write_flash_bytes();
				lk_conn_send_u8(LK_STATUS_OK);
			}
			break;

		case LK_CMD_DMG_EEPROM_WRITE:
			// note: client must set address and transfer size variable beforehand
			{
				lk_conn_recv(data_buffer, _lk_var16[LK_VAR16_TRANSFER_SIZE]);
				ACTIVITY_LED_ON();
				for (u16 x = 0; x < (u16)(_lk_var16[LK_VAR16_TRANSFER_SIZE] / _lk_var16[LK_VAR16_BUFFER_SIZE]); x++) {
					if (_timeout_check()) break;
					lk_dmg_eeprom_write_bytes(_lk_var16[LK_VAR16_BUFFER_SIZE], x * _lk_var16[LK_VAR16_BUFFER_SIZE], _lk_var32[LK_VAR32_ADDRESS]);
				}
				lk_conn_send_u8(LK_STATUS_OK);
			}
			break;

		case LK_CMD_DMG_MBC7_READ_EEPROM:
			// note: client must set address and transfer size variable beforehand
			{
				ACTIVITY_LED_ON();
				lk_dmg_mbc7_read_eeprom();
			}
			break;

		case LK_CMD_DMG_MBC7_WRITE_EEPROM:
			// note: client must set address and transfer size variable beforehand
			{
				lk_conn_recv(data_buffer, _lk_var16[LK_VAR16_TRANSFER_SIZE]);
				ACTIVITY_LED_ON();
				lk_dmg_mbc7_write_eeprom();
				lk_conn_send_u8(LK_STATUS_OK);
			}
			break;

		case LK_CMD_AGB_CART_READ:
			// note: client must set address variable beforehand
			{
				ACTIVITY_LED_ON();
				lk_agb_cart_read_data();
			}
			break;

		case LK_CMD_AGB_CART_WRITE:
			// args: address (4 byte), value (2 byte)
			{
				u32 addr = lk_conn_recv_u32();
				u16 data = lk_conn_recv_u16();
				ACTIVITY_LED_ON();
				lk_agb_cart_write_short(addr, data);
				lk_conn_send_u8(LK_STATUS_OK);
			}
			break;

		case LK_CMD_AGB_CART_READ_SRAM:
			// note: client must set address and transfer size variable beforehand
			{
				ACTIVITY_LED_ON();
				lk_agb_cart_read_sram();
			}
			break;

		case LK_CMD_AGB_CART_WRITE_SRAM:
			// note: client must set address and transfer size variable beforehand
			{
				lk_conn_recv(data_buffer, _lk_var16[LK_VAR16_TRANSFER_SIZE]);
				ACTIVITY_LED_ON();
				lk_agb_cart_write_sram();
				lk_conn_send_u8(LK_STATUS_OK);
			}
			break;

		case LK_CMD_AGB_CART_READ_EEPROM:
			// args: eeprom_type (1 byte)
			// note: client must set address and transfer size variable beforehand
			{
				u8 eeprom_type = lk_conn_recv_u8();
				ACTIVITY_LED_ON();
				lk_agb_cart_read_eeprom(eeprom_type);
			}
			break;

		case LK_CMD_AGB_CART_WRITE_EEPROM:
			// args: eeprom_type (1 byte)
			// note: client must set address and transfer size variable beforehand
			{
				u8 eeprom_type = lk_conn_recv_u8();
				lk_conn_recv(data_buffer, _lk_var16[LK_VAR16_TRANSFER_SIZE]);
				ACTIVITY_LED_ON();
				lk_agb_cart_write_eeprom(eeprom_type);
				lk_conn_send_u8(LK_STATUS_OK);
			}
			break;

		case LK_CMD_AGB_CART_WRITE_FLASH_DATA:
			// args: flash_type (atmel or other) (1 byte)
			// note: client must set address and transfer size variable beforehand
			{
				u8 flash_type = lk_conn_recv_u8();
				lk_conn_recv(data_buffer, _lk_var16[LK_VAR16_TRANSFER_SIZE]);
				ACTIVITY_LED_ON();
				lk_agb_cart_write_flash(flash_type);
				lk_conn_send_u8(LK_STATUS_OK);
			}
			break;

		case LK_CMD_AGB_CART_READ_3D_MEMORY:
			// note: client must set address transfer size variable beforehand
			// note: client must set buffer size variable beforehand (1 or 8)
			{
				ACTIVITY_LED_ON();
				lk_agb_cart_read_data_3d_memory();
			}
			break;

		case LK_CMD_AGB_BOOTUP_SEQUENCE:
			{
				ACTIVITY_LED_ON();
				lk_agb_bootup_sequence();
				lk_conn_send_u8(LK_STATUS_OK);
			}
			break;

		case LK_CMD_AGB_READ_GPIO_RTC:
			{
				ACTIVITY_LED_ON();
				lk_agb_read_gpio_rtc();
				lk_conn_send(data_buffer, 8);
			}
			break;

		case LK_CMD_DMG_SET_BANK_CHANGE_CMD:
			// args: number of bank switch commands (1 byte), addresses (4 byte) and values (1 byte)
			{
				_lk_bankcmd_num = lk_conn_recv_u8();
				for (u8 x = 0; x < _lk_bankcmd_num; x++) {
					_lk_bankcmd_addr[x] = lk_conn_recv_u32();
					_lk_bankcmd_mode[x] = lk_conn_recv_u8();
				}
				lk_conn_send_u8(LK_STATUS_OK);
			}
			break;

		// *** FLASH CARTRIDGES *** //
		case LK_CMD_CART_WRITE_FLASH_CMD:
			// args: num of commands (1 byte), max(32, num) * (address (4 byte), value (2 byte))
			// note: because there may be time critical commands,
			//       this function executes them all at once quickly
			{
				u8 mode = _lk_var8[LK_VAR8_CART_MODE];
				u8 flash = lk_conn_recv_u8();
				void (*p_cart_write_flash_byte)(u32, u16) = NULL;
				if (mode == LK_MODE_DMG) {
					if (flash == 1) {
						p_cart_write_flash_byte = &lk_dmg_flash_write_byte;
					} else {
						p_cart_write_flash_byte = &lk_dmg_cart_write_byte;
					}
					RAW_DMG_DATA_DIR_OUT();
				} else if (mode == LK_MODE_AGB) {
					if (flash == 1) {
						p_cart_write_flash_byte = &lk_agb_cart_write_short;
					} else {
						lk_set_mode(LK_MODE_DMG);
						p_cart_write_flash_byte = &lk_agb_cart_write_flash_byte;
					}
				}
			
				u8 num = lk_conn_recv_u8();
				for (u8 x = 0; x < num; x++) {
					_lk_flashcmd_addr[x+16] = lk_conn_recv_u32();
					_lk_flashcmd_data[x+16] = lk_conn_recv_u16();
				}
				
				ACTIVITY_LED_ON();
				for (u8 x = 0; x < num; x++) {
					p_cart_write_flash_byte(_lk_flashcmd_addr[x+16], _lk_flashcmd_data[x+16]);
				}

				if (mode == LK_MODE_DMG) {
					RAW_DMG_DATA_SET(0);
					RAW_DMG_DATA_DIR_IN();
				} else {
					if (flash == 0) {
						_delay_ms(10); // Atmel AT29LV512 flash save needs this
					}
					lk_set_mode(LK_MODE_AGB);
				}

				lk_conn_send_u8(LK_STATUS_OK);
			}
			break;

		case LK_CMD_DMG_FLASH_WRITE_BYTE:
			// args: address (4 byte), value (1 byte)
			{
				u32 addr = lk_conn_recv_u32();
				u8 data = lk_conn_recv_u8();
				ACTIVITY_LED_ON();
				RAW_DMG_DATA_DIR_OUT();
				lk_dmg_flash_write_byte(addr, data);
				RAW_DMG_DATA_SET(0);
				RAW_DMG_DATA_DIR_IN();
				lk_conn_send_u8(LK_STATUS_OK);
			}
			break;

		case LK_CMD_AGB_FLASH_WRITE_SHORT:
			// args: address (4 byte), value (2 byte)
			{
				u32 addr = lk_conn_recv_u32();
				u16 data = lk_conn_recv_u16();
				ACTIVITY_LED_ON();
				lk_agb_cart_write_short(addr, data);
				lk_conn_send_u8(LK_STATUS_OK);
			}
			break;

		case LK_CMD_FLASH_PROGRAM:
			// note: client must set various variables beforehand
			{
				if (_lk_var8[LK_VAR8_FLASH_METHOD] == LK_FLASH_METHOD_UNBUFFERED) {
					lk_conn_recv(data_buffer, _lk_var16[LK_VAR16_TRANSFER_SIZE]);
					ACTIVITY_LED_ON();
					lk_dmg_agb_flash_unbuffered();
				} else if (_lk_var8[LK_VAR8_FLASH_METHOD] == LK_FLASH_METHOD_AGB_FUJITSU_FASTMODE) {
					lk_conn_recv(data_buffer, _lk_var16[LK_VAR16_TRANSFER_SIZE]);
					ACTIVITY_LED_ON();
					lk_agb_flash_fujitsu_fastmode();
				} else if (_lk_var8[LK_VAR8_FLASH_METHOD] == LK_FLASH_METHOD_AGB_PAGED) {
					lk_conn_recv(data_buffer, _lk_var16[LK_VAR16_TRANSFER_SIZE]);
					ACTIVITY_LED_ON();
					for (u16 x = 0; x < (u16)(_lk_var16[LK_VAR16_TRANSFER_SIZE] / _lk_var16[LK_VAR16_BUFFER_SIZE]); x++) {
						if (_timeout_check()) break;
						lk_agb_flash_paged(x * _lk_var16[LK_VAR16_BUFFER_SIZE]);
					}
					// Back to read array mode
					lk_agb_cart_write_short(_lk_var32[LK_VAR32_ADDRESS]-1, 0xFF);
				} else if (_lk_var8[LK_VAR8_FLASH_METHOD] == LK_FLASH_METHOD_DMG_DATEL_ORBITV2) {
					lk_conn_recv(data_buffer, _lk_var16[LK_VAR16_TRANSFER_SIZE]);
					ACTIVITY_LED_ON();
					lk_dmg_flash_datel_orbitv2();
				} else if (_lk_var8[LK_VAR8_FLASH_METHOD] == LK_FLASH_METHOD_DMG_E201264) {
					lk_conn_recv(data_buffer, _lk_var16[LK_VAR16_TRANSFER_SIZE]);
					ACTIVITY_LED_ON();
					for (u16 x = 0; x < (u16)(_lk_var16[LK_VAR16_TRANSFER_SIZE] / 128); x++) {
						if (_timeout_check()) break;
						lk_dmg_flash_E201264(x * 128);
					}
				} else if (_lk_var8[LK_VAR8_FLASH_METHOD] == LK_FLASH_METHOD_DMG_BUNG_16M) {
					lk_conn_recv(data_buffer, _lk_var16[LK_VAR16_TRANSFER_SIZE]);
					ACTIVITY_LED_ON();
					for (u16 x = 0; x < (u16)(_lk_var16[LK_VAR16_TRANSFER_SIZE] / 128); x++) {
						if (_timeout_check()) break;
						lk_dmg_flash_bung_16m(x * 128);
					}
				} else if (_lk_var8[LK_VAR8_FLASH_METHOD] == LK_FLASH_METHOD_DMG_MMSA) {
					lk_conn_recv(data_buffer, _lk_var16[LK_VAR16_TRANSFER_SIZE]);
					ACTIVITY_LED_ON();
					for (u16 x = 0; x < (u16)(_lk_var16[LK_VAR16_TRANSFER_SIZE] / 128); x++) {
						if (_timeout_check()) break;
						lk_dmg_flash_mmsa(x * 128);
					}
					// Back to read array mode
					lk_dmg_cart_write_byte(_lk_var32[LK_VAR32_ADDRESS]-1, 0xF0);
				} else if (_lk_var8[LK_VAR8_FLASH_METHOD] == LK_FLASH_METHOD_DMG_MBC6) {
					lk_conn_recv(data_buffer, _lk_var16[LK_VAR16_TRANSFER_SIZE]);
					ACTIVITY_LED_ON();
					for (u16 x = 0; x < (u16)(_lk_var16[LK_VAR16_TRANSFER_SIZE] / 128); x++) {
						if (_timeout_check()) break;
						lk_dmg_flash_mbc6(x * 128);
					}
					// Back to read array mode
					lk_dmg_cart_write_byte(_lk_var32[LK_VAR32_ADDRESS]-1, 0xF0);
				} else if (_lk_var8[LK_VAR8_FLASH_METHOD] == LK_FLASH_METHOD_AGB_GBAMP) {
					lk_conn_recv(data_buffer, _lk_var16[LK_VAR16_TRANSFER_SIZE]);
					ACTIVITY_LED_ON();
					lk_agb_flash_gbamp();
				} else if (_lk_var8[LK_VAR8_FLASH_METHOD] == LK_FLASH_METHOD_AGB_FLASH2ADVANCE) {
					lk_conn_recv(data_buffer, _lk_var16[LK_VAR16_TRANSFER_SIZE]);
					ACTIVITY_LED_ON();
					for (u16 x = 0; x < (u16)(_lk_var16[LK_VAR16_TRANSFER_SIZE] / _lk_var16[LK_VAR16_BUFFER_SIZE]); x++) {
						if (_timeout_check()) break;
						lk_agb_flash2advance_buffered(_lk_var16[LK_VAR16_BUFFER_SIZE], x * _lk_var16[LK_VAR16_BUFFER_SIZE], _lk_var32[LK_VAR32_ADDRESS]);
					}
				}
				else {
					if (_lk_var16[LK_VAR16_TRANSFER_SIZE] < _lk_var16[LK_VAR16_BUFFER_SIZE]) {
						u32 sector_address = _lk_var32[LK_VAR32_ADDRESS];
						u16 num_of_iterations = _lk_var16[LK_VAR16_BUFFER_SIZE] / _lk_var16[LK_VAR16_TRANSFER_SIZE];
						for (u16 this_iteration = 0; this_iteration < num_of_iterations; this_iteration++) {
							if (_timeout_check()) break;
							if (this_iteration > 0) {
								lk_conn_send_u8(LK_STATUS_CHUNK_NEXT);
							}
							lk_conn_recv(data_buffer, _lk_var16[LK_VAR16_TRANSFER_SIZE]);
							ACTIVITY_LED_ON();
							lk_dmg_agb_flash_buffered(this_iteration, num_of_iterations, _lk_var16[LK_VAR16_TRANSFER_SIZE], 0, sector_address);
						}
					} else {
						lk_conn_recv(data_buffer, _lk_var16[LK_VAR16_TRANSFER_SIZE]);
						ACTIVITY_LED_ON();
						for (u16 x = 0; x < (u16)(_lk_var16[LK_VAR16_TRANSFER_SIZE] / _lk_var16[LK_VAR16_BUFFER_SIZE]); x++) {
							if (_timeout_check()) break;
							lk_dmg_agb_flash_buffered(0, 1, _lk_var16[LK_VAR16_BUFFER_SIZE], x * _lk_var16[LK_VAR16_BUFFER_SIZE], _lk_var32[LK_VAR32_ADDRESS]);
						}
					}
				}

				if (_timeout_check()) {
					lk_conn_send_u8(LK_STATUS_ERROR);
				} else {
					lk_conn_send_u8(LK_STATUS_OK);
				}
			}
			break;

		case LK_CMD_CALC_CRC32:
			// args: length (4 byte)
			{
				u32 length = lk_conn_recv_u32();
				ACTIVITY_LED_ON();
				lk_conn_send_u32(lk_dmg_agb_calc_crc32(length));
			}
			break;

		case LK_CMD_SET_PIN:
			// args: pin (1 byte), high/low (1 byte)
			{
				// TODO: change to u32 (1 bit per pin)
				u32 pins = lk_conn_recv_u32();
				u8 set = lk_conn_recv_u8(); // 0=set low, 1=set high
				if ((pins >> 0) & 1) {
					if (set) { CART_POWER_ON(); } else { CART_POWER_OFF(); }
				}
				if ((pins >> 1) & 1) {
					if (set) { PIN_CLK_H(); } else { PIN_CLK_L(); }
				}
				if ((pins >> 2) & 1) {
					if (set) { PIN_WR_H(); } else { PIN_WR_L(); }
				}
				if ((pins >> 3) & 1) {
					if (set) { PIN_RD_H(); } else { PIN_RD_L(); }
				}
				if ((pins >> 4) & 1) {
					if (set) { PIN_CS_H(); } else { PIN_CS_L(); }
				}
				if (pins & 0x1FFFFFE0) { RAW_AGB_ADDR_DIR_OUT(); }
				for (u8 i = 5; i <= 28; i++) {
					if ((pins >> i) & 1) {
						if (set) { PIN_ADDR_H(i - 5); } else { PIN_ADDR_L(i - 5); }
					}
				}
				if ((pins >> 29) & 1) {
					if (set) { PIN_CS2_H(); } else { PIN_CS2_L(); }
				}
				if ((pins >> 30) & 1) {
					if (set) { PIN_AUDIO_H(); } else { PIN_AUDIO_L(); }
				}
				lk_conn_send_u8(LK_STATUS_OK);
			}
			break;

		case LK_CMD_GET_VARIABLE:
			// args: variable size (1 byte), variable key (4 byte)
			{
				u8 size = lk_conn_recv_u8();
				u32 key = lk_conn_recv_u32();
				if (key == 0xFF) {
					lk_conn_send_u32(lk_runtime);
				} else {
					if (size == 1) {
						lk_conn_send_u32(_lk_var8[key]);
					} else if (size == 2) {
						lk_conn_send_u32(_lk_var16[key]);
					} else if (size == 4) {
						lk_conn_send_u32(_lk_var32[key]);
					}
				}
			}
			break;

		case LK_CMD_GET_VAR_STATE:
			{
				for (u8 key = 0; key < LK_NUM_OF_VARIABLES_32BIT; key++) {
					lk_conn_send_u32(_lk_var32[key]);
				}
				for (u8 key = 0; key < LK_NUM_OF_VARIABLES_16BIT; key++) {
					lk_conn_send_u16(_lk_var16[key]);
				}
				for (u8 key = 0; key < LK_NUM_OF_VARIABLES_8BIT; key++) {
					lk_conn_send_u8(_lk_var8[key]);
				}
				for (u8 i = 0; i < 32; i++) {
					lk_conn_send_u32(_lk_flashcmd_addr[i]);
					lk_conn_send_u16(_lk_flashcmd_data[i]);
				}
				lk_conn_send_u8(_lk_bankcmd_num);
				for (u8 i = 0; i < 3; i++) {
					lk_conn_send_u32(_lk_bankcmd_addr[i]);
					lk_conn_send_u8(_lk_bankcmd_mode[i]);
				}
				for (u8 i = 0; i < 3; i++) {
					for (u8 j = 0; j < 2; j++) {
						lk_conn_send_u16(flash_write_cycle[i][j]);
					}
				}
			}
			break;

		case LK_CMD_SET_VAR_STATE:
			{
				for (u8 key = 0; key < LK_NUM_OF_VARIABLES_32BIT; key++) {
					_lk_var32[key] = lk_conn_recv_u32();
				}
				for (u8 key = 0; key < LK_NUM_OF_VARIABLES_16BIT; key++) {
					_lk_var16[key] = lk_conn_recv_u16();
				}
				for (u8 key = 0; key < LK_NUM_OF_VARIABLES_8BIT; key++) {
					_lk_var8[key] = lk_conn_recv_u8();
				}
				for (u8 i = 0; i < 32; i++) {
					_lk_flashcmd_addr[i] = lk_conn_recv_u32();
					_lk_flashcmd_data[i] = lk_conn_recv_u16();
				}
				_lk_bankcmd_num = lk_conn_recv_u8();
				for (u8 i = 0; i < 3; i++) {
					_lk_bankcmd_addr[i] = lk_conn_recv_u32();
					_lk_bankcmd_mode[i] = lk_conn_recv_u8();
				}
				for (u8 i = 0; i < 3; i++) {
					for (u8 j = 0; j < 2; j++) {
						flash_write_cycle[i][j] = lk_conn_recv_u16();
					}
				}
				lk_conn_send_u8(LK_STATUS_OK);
			}
			break;

		case LK_CMD_GET_SWITCH_STATE:
			{
				u8 state = 0;
				#ifdef CART_PRESENCE_SWITCH_GET
					if (CART_PRESENCE_SWITCH_GET() != -1) {
						state |= (CART_PRESENCE_SWITCH_GET() & 1);
					}
				#endif
				#ifdef CART_MODE_SWITCH_GET
					if (CART_MODE_SWITCH_GET() != -1) {
						state |= (CART_MODE_SWITCH_GET() & 1) << 1;
					}
				#endif
				lk_conn_send_u8(state);
			}
			break;

		// Reset device
		case LK_CMD_BOOTLOADER_RESET:
			{
				if (LK_BOOTLOADER_RESET_SUPPORT) {
					lk_cart_power_off();
					lk_conn_send_u8(LK_STATUS_OK);
					lk_conn_recv(&command, 1);
					if (command == LK_STATUS_OK) {
						BOOTLOADER_RESET();
					}
				} else {
					lk_conn_send_u8(LK_STATUS_ERROR);
				}
			}
			break;
		
		default:
			lk_conn_send_u8(LK_STATUS_ERROR);
			break;
	}

	activity_last_run = lk_runtime;
	activity_done = true;
	ACTIVITY_LED_OFF();
	AUTO_POWEROFF_RESUME();
}

/****************************************************/

void lk_cart_power_on(void) {
	ACTIVITY_LED_ON();
	if (_lk_var8[LK_VAR8_CART_POWERED] == true) return;

	RAW_PINS_DIR_OUT();
	if (_lk_var8[LK_VAR8_CART_MODE] == LK_MODE_DMG) {
		#ifdef CART_POWER_ON_FIX_1
			CART_POWER_ON_FIX_1(); // GBxCart RW is known to have additional issues, so special workarounds are inserted here
		#endif
		PIN_WR_L();
		PIN_RD_L();
		PIN_CS_L();
		PIN_WR_H();
		_delay_us(5);
		CART_POWER_ON();
		PIN_CS2_H();
		PIN_CS_H();
		_delay_us(150);
		PIN_RD_H();
		PIN_AUDIO_H();
		PIN_CLK_L();
		#ifdef CART_POWER_ON_FIX_2
			CART_POWER_ON_FIX_2(); // GBxCart RW is known to have additional issues, so special workarounds are inserted here
		#endif

	} else if (_lk_var8[LK_VAR8_CART_MODE] == LK_MODE_AGB) {
		PIN_WR_L();
		PIN_RD_L();
		PIN_CS_L();
		PIN_CS2_H();
		_delay_500ns(); // GBAMP seems to need a delay of 900 ns to work
		_delay_400ns();
		CART_POWER_ON();
		if (_lk_var8[LK_VAR8_AGB_IRQ_ENABLED] == true) {
			PIN_AUDIO_H();
		} else {
			PIN_AUDIO_L();
		}
		_delay_us(50);
		PIN_CS_H();
		_delay_us(150); // Some bootlegs corrupt their save data if this is too short
		PIN_RD_H();
		PIN_WR_H();
	}

	// Atmel AT29LV512 flash save needs at least 5 ms, FunnyPlaying MidnightTrace at least 40 ms
	// insideGadgets 1M FLASH needs at least 100 ms, AGB-R1M-02V4 needs at least 140 ms
	_delay_ms(150);

	_lk_var8[LK_VAR8_CART_POWERED] = true;
	dprint("Cartridge power has been turned ON.\r\n");
}
void lk_cart_power_off(void) {
	ACTIVITY_LED_OFF();
	if (_lk_var8[LK_VAR8_CART_POWERED] == false) return;
	
	PIN_AUDIO_L();
	_delay_ms(2);
	PIN_CLK_L();
	_delay_ms(2);
	CART_POWER_OFF();
	_delay_ms(100); // Unlicensed 256M multi game cartridges need at least 5 ms, GBAMP needs more

	_lk_var8[LK_VAR8_CART_POWERED] = false;
	dprint("Cartridge power has been turned OFF.\r\n");
}

/****************************************************/

// *** UART FUNCTIONS *** //
void lk_conn_recv(u8* data, u16 count) {
	CONN_RECV(data, count);
}
u8 lk_conn_recv_u8(void) {
	u8 temp;
	lk_conn_recv(&temp, 1);
	return temp;
}
u16 lk_conn_recv_u16(void) {
	u8 temp[2];
	lk_conn_recv(temp, 2);
	return ((u16)temp[0] << 8) | (u16)temp[1];
}
u32 lk_conn_recv_u32(void) {
	u8 temp[4];
	lk_conn_recv(temp, 4);
	return ((u32)temp[0] << 24) | ((u32)temp[1] << 16) | ((u32)temp[2] << 8) | (u32)temp[3];
}

void lk_conn_send(u8* data, u16 count) {
	CONN_SEND(data, count);
}
void lk_conn_send_u8(u8 data) {
	CONN_SEND_BYTE(data);
}
void lk_conn_send_u16(u16 data) {
	CONN_SEND_BYTE((data >> 8) & 0xFF);
	CONN_SEND_BYTE(data & 0xFF);
}
void lk_conn_send_u32(u32 data) {
	CONN_SEND_BYTE((data >> 24) & 0xFF);
	CONN_SEND_BYTE((data >> 16) & 0xFF);
	CONN_SEND_BYTE((data >> 8) & 0xFF);
	CONN_SEND_BYTE(data & 0xFF);
}

void lk_set_mode(u8 mode) {
	if (mode == LK_MODE_DMG) {
		RAW_DMG_DATA_SET(0);
		RAW_DMG_DATA_DIR_IN();
		RAW_DMG_ADDR_DIR_OUT();
	} else {
		RAW_AGB_ADDR_SET(0);
		RAW_AGB_ADDR_DIR_OUT();
	}
	_lk_var8[LK_VAR8_CART_MODE] = mode;
}

// *** GAME BOY FUNCTIONS *** //
void lk_dmg_set_address(u16 address) {
	RAW_DMG_ADDR_DIR_OUT();
	RAW_DMG_ADDR_SET(address);
}

void lk_dmg_mbc6_mmsa_write_flash_bytes(void) {
	RAW_DMG_DATA_DIR_OUT();
	for (u32 x = 0; x < _lk_var16[LK_VAR16_TRANSFER_SIZE]; x++) {
		lk_dmg_cart_write_byte(_lk_var32[LK_VAR32_ADDRESS]++, data_buffer[x]);
	}
	RAW_DMG_DATA_SET(0);
	RAW_DMG_DATA_DIR_IN();
}
void lk_dmg_mbc7_write(u8 value) {
	lk_dmg_cart_write_byte(0xA080, 0x80 + (value << 1));
	lk_dmg_cart_write_byte(0xA080, 0xC0 + (value << 1));
}
void lk_dmg_mbc7_reinit(void) {
	lk_dmg_cart_write_byte(0xA080, 0x00);
	lk_dmg_cart_write_byte(0xA080, 0x80);
	_delay_ms(15); // seems to work from at least 6 ms, so 15 ms should be safe
}
void lk_dmg_mbc7_set_write_access(u8 enable) {
	lk_dmg_mbc7_reinit();
	lk_dmg_mbc7_write(0);
	lk_dmg_mbc7_write(1);
	lk_dmg_mbc7_write(0);
	lk_dmg_mbc7_write(0);
	if (enable == true) {
		lk_dmg_mbc7_write(1);
		lk_dmg_mbc7_write(1);
	} else {
		lk_dmg_mbc7_write(0);
		lk_dmg_mbc7_write(0);
	}
	for (u8 x=0; x < 6; x++) {
		lk_dmg_mbc7_write(0);
	}
}
void lk_dmg_mbc7_set_cmd(u16 address, u8 mode) {
	// Re-init and commit previous action(?)
	lk_dmg_mbc7_reinit();
	
	// Command sequence
	lk_dmg_mbc7_write(0);
	lk_dmg_mbc7_write(1);
	if (mode == LK_MODE_RAM_READ) {
		lk_dmg_mbc7_write(1);
		lk_dmg_mbc7_write(0);
	} else { /*if (mode == LK_MODE_RAM_WRITE) {*/
		lk_dmg_mbc7_write(0);
		lk_dmg_mbc7_write(1);
	}
	
	// Set address; bitwise, 80/C0=0, 82/C2=1
	for (u8 x = 0; x < 8; x++) {
		if (((address << x) >> 7) & 1) {
			lk_dmg_mbc7_write(1);
		} else {
			lk_dmg_mbc7_write(0);
		}
	}
}
void lk_dmg_eeprom_write_bytes(u16 transfer_size, u16 buffer_offset, u32 sector_address) {
	RAW_DMG_DATA_DIR_OUT();
	if (_lk_var8[LK_VAR8_FLASH_COMMANDS_BANK_1] == 1) {
		lk_dmg_change_bank(1);
	}
	lk_dmg_flash_write_byte(_lk_flashcmd_addr[0], _lk_flashcmd_data[0]);			// AAA=AA
	lk_dmg_flash_write_byte(_lk_flashcmd_addr[1], _lk_flashcmd_data[1]);			// 555=55
	lk_dmg_flash_write_byte(_lk_flashcmd_addr[2], _lk_flashcmd_data[2]);			// AAA=A0
	if (_lk_var8[LK_VAR8_FLASH_COMMANDS_BANK_1] == 1) {
		lk_dmg_change_bank(_lk_var16[LK_VAR16_LAST_BANK_ACCESSED]);
	}
	
	for (u32 x = 0; x < transfer_size; x++) {
		lk_dmg_flash_write_byte(sector_address+x, data_buffer[buffer_offset+x]);
	}
	_delay_ms(4);

	_lk_var32[LK_VAR32_ADDRESS] += transfer_size;
	RAW_DMG_DATA_SET(0);
	RAW_DMG_DATA_DIR_IN();
}

u8 lk_dmg_cart_read_byte(u16 address) {
	RAW_DMG_ADDR_DIR_OUT();
	RAW_DMG_ADDR_SET(address);
	PIN_RD_L();
	_delay_400ns();
	RAW_DMG_DATA_DIR_IN();
	u8 data = RAW_DMG_DATA_GET();
	PIN_RD_H();
	return data;
}
u8 lk_dmg_cart_read_sram(u16 address) {
	PIN_CLK_H(); // CLK signal needed by FRAM mods
	RAW_DMG_ADDR_DIR_OUT();
	RAW_DMG_ADDR_SET(address);
	PIN_CS_L();
	PIN_RD_L();
	PIN_CLK_L();
	_delay_400ns();
	RAW_DMG_DATA_DIR_IN();
	u8 data = RAW_DMG_DATA_GET();
	PIN_CLK_H();
	PIN_RD_H();
	PIN_CS_H();
	return data;
}
void lk_dmg_cart_write_byte(u32 address, u16 value) {
	// DMG-MMSA-JPN is very timing-sensitive and slower devices may cause too much delay, so
	// the LK_VAR8_DMG_WRITE_CS_PULSE check is around everything and the entire code is duplicated.
	if (_lk_var8[LK_VAR8_DMG_WRITE_CS_PULSE] == true) {
		PIN_CLK_H();
		_delay_500ns();
		RAW_DMG_ADDR_SET(0);
		RAW_DMG_ADDR_DIR_OUT();
		RAW_DMG_ADDR_SET(address);
		RAW_DMG_DATA_DIR_OUT();
		RAW_DMG_DATA_SET(value);
		_delay_200ns();
		PIN_CS_L();
		PIN_WR_L();
		PIN_CLK_L();
		_delay_500ns();
		PIN_CLK_H();
		PIN_WR_H();
		PIN_CS_H();
		RAW_DMG_DATA_SET(0);
		RAW_DMG_DATA_DIR_IN();
	} else {
		PIN_CLK_H();
		_delay_500ns();
		RAW_DMG_ADDR_SET(0);
		RAW_DMG_ADDR_DIR_OUT();
		RAW_DMG_ADDR_SET(address);
		RAW_DMG_DATA_DIR_OUT();
		RAW_DMG_DATA_SET(value);
		_delay_200ns();
		PIN_WR_L();
		PIN_CLK_L();
		_delay_500ns();
		PIN_CLK_H();
		PIN_WR_H();
		RAW_DMG_DATA_SET(0);
		RAW_DMG_DATA_DIR_IN();
	}
}
void lk_dmg_cart_read_data(void) {
	PIN_RD_L();
	PIN_CLK_L(); // Pocket Camera needs this
	RAW_DMG_ADDR_DIR_OUT();
	RAW_DMG_DATA_DIR_IN();
	u16 left = _lk_var16[LK_VAR16_TRANSFER_SIZE];
	while (left > 0) {
		u16 chunk_len = left > CHUNK_MAX_LEN ? CHUNK_MAX_LEN : left;

		if (_lk_var8[LK_VAR8_DMG_READ_CS_PULSE] == true) { // SRAM
			for (u32 x = 0; x < chunk_len; x++) {
				data_buffer[x] = lk_dmg_cart_read_sram(_lk_var32[LK_VAR32_ADDRESS]++);
			}
		} else if (_lk_var8[LK_VAR8_DMG_READ_METHOD] == LK_DMG_READ_METHOD_PULSE_A15) { // read ROM with A15 pulsing
			for (u32 x = 0; x < chunk_len; x++) {
				RAW_DMG_ADDR_SET(_lk_var32[LK_VAR32_ADDRESS]);
				_delay_400ns();
				data_buffer[x] = RAW_DMG_DATA_GET();
				PIN_ADDR_H(15);
				_lk_var32[LK_VAR32_ADDRESS]++;
			}
		} else if (_lk_var8[LK_VAR8_DMG_READ_METHOD] == LK_DMG_READ_METHOD_PULSE_A15_SLOW) { // read ROM with A15 pulsing, but slower
			for (u32 x = 0; x < chunk_len; x++) {
				RAW_DMG_ADDR_SET(_lk_var32[LK_VAR32_ADDRESS]);
				_delay_dmg_slow_access();
				data_buffer[x] = RAW_DMG_DATA_GET();
				PIN_ADDR_H(15);
				_lk_var32[LK_VAR32_ADDRESS]++;
			}
		} else { // read ROM with RD pulsing
			for (u32 x = 0; x < chunk_len; x++) {
				RAW_DMG_ADDR_SET(_lk_var32[LK_VAR32_ADDRESS]++);
				PIN_RD_L();
				_delay_400ns();
				data_buffer[x] = RAW_DMG_DATA_GET();
				PIN_RD_H();
			}
		}
		LK_Chromatic_async_flush(data_buffer, chunk_len);

		lk_conn_send(data_buffer, chunk_len);
		left -= chunk_len;
	}
	PIN_RD_H();
}
void lk_dmg_mbc7_read_eeprom(void) {
	for (u32 x = 0; x < _lk_var16[LK_VAR16_TRANSFER_SIZE]; x += 2) {
		lk_dmg_mbc7_set_cmd(_lk_var32[LK_VAR32_ADDRESS]++, LK_MODE_RAM_READ);
		u16 data = 0;
		for (u8 y = 0; y < 16; y++) {
			lk_dmg_mbc7_write(0);
			data = (data << 1) | (lk_dmg_cart_read_byte(0xA080) & 1);
		}
		lk_conn_send_u16(data);
	}
}
void lk_dmg_mbc7_write_eeprom(void) {
	lk_dmg_mbc7_set_write_access(true);
	for (u32 x = 0; x < _lk_var16[LK_VAR16_TRANSFER_SIZE]; x += 2) {
		lk_dmg_mbc7_set_cmd(_lk_var32[LK_VAR32_ADDRESS]++, LK_MODE_RAM_WRITE);
		for (u8 y = 0; y < 16; y++) {
			if (((((data_buffer[x] << 8) + (data_buffer[x+1])) << y) >> 15) & 1) {
				lk_dmg_mbc7_write(1);
			} else {
				lk_dmg_mbc7_write(0);
			}
		}
	}
	lk_dmg_mbc7_set_write_access(false);
}

void lk_dmg_verify_data(u32 addr, u16 comp) {
	_timeout_init();
	u8 data;
	PIN_RD_L();
	PIN_CLK_L(); // Pocket Camera needs this
	RAW_DMG_DATA_SET(0);
	RAW_DMG_DATA_DIR_IN();
	RAW_DMG_ADDR_DIR_OUT();
	RAW_DMG_ADDR_SET(addr & 0xFFFF);
	while (1) {
		PIN_RD_L();
		_delay_400ns();
		data = RAW_DMG_DATA_GET();
		PIN_RD_H();

		if (data == comp) {
			_timeout_reset();
			break;
		}
		if (_timeout_check()) {
			dprint("lk_dmg_verify_data(addr=%x, comp=%x): Timed out with %x!\r\n", addr, comp, data);
			_lk_var16[LK_VAR16_STATUS_REGISTER] = data;
			break;
		}
	}
}
void lk_dmg_verify_status_register(u32 addr) {
	_timeout_init();
	RAW_DMG_DATA_SET(0);
	RAW_DMG_DATA_DIR_IN();
	u8 data;
	RAW_DMG_ADDR_DIR_OUT();
	RAW_DMG_ADDR_SET(addr & 0xFFFF);
	while (1) {
		PIN_RD_L();
		_delay_400ns();
		data = RAW_DMG_DATA_GET();
		PIN_RD_H();
		if ((data & _lk_var16[LK_VAR16_STATUS_REGISTER_MASK]) == _lk_var16[LK_VAR16_STATUS_REGISTER_VALUE]) {
			_timeout_reset();
			break;
		}
		if (_timeout_check()) {
			dprint("lk_dmg_verify_status_register(addr=%x): Timed out with %x!\r\n", addr, data);
			_lk_var16[LK_VAR16_STATUS_REGISTER] = data;
			break;
		}
	}
	RAW_DMG_DATA_DIR_OUT();
}
void lk_dmg_flash_write_byte(u32 address, u16 value) {
	PIN_WR_H();
	PIN_CLK_L(); // Pocket Camera needs this

	RAW_DMG_ADDR_DIR_OUT();
	RAW_DMG_ADDR_SET(address);
	RAW_DMG_DATA_DIR_OUT();
	RAW_DMG_DATA_SET(value);

	_delay_200ns(); // thanks simonK

	if (_lk_var8[LK_VAR8_FLASH_WE_PIN] == LK_FLASH_WE_PIN_WR) {
		PIN_WR_L();
		_delay_400ns();
		PIN_WR_H();
	} else if (_lk_var8[LK_VAR8_FLASH_WE_PIN] == LK_FLASH_WE_PIN_AUDIO) {
		PIN_AUDIO_L();
		_delay_400ns();
		PIN_AUDIO_H();
	} else if (_lk_var8[LK_VAR8_FLASH_WE_PIN] == LK_FLASH_WE_PIN_WR_RESET) {
		PIN_CS2_L();
		PIN_WR_L();
		_delay_400ns();
		PIN_WR_H();
		PIN_CS2_H();
	}
}

void lk_dmg_flash_enable_audio(u8 enable) {
	if (enable == true) {
		PIN_AUDIO_DIR_OUT();
		PIN_AUDIO_H();
	} else {
		PIN_AUDIO_L();
		PIN_AUDIO_DIR_IN();
	}
}
void lk_dmg_change_bank(u16 index) {
	RAW_DMG_DATA_DIR_OUT();
	if (_lk_bankcmd_num == 0) {
		lk_dmg_set_address(0x2000);
		RAW_DMG_DATA_SET(index);
		PIN_WR_L();
		_delay_400ns();
		PIN_WR_H();
		PIN_CLK_H();
	} else {
		for (u8 b = 0; b < _lk_bankcmd_num; b++) {
			if (_lk_bankcmd_mode[b] == 0) {
				lk_dmg_set_address(_lk_bankcmd_addr[b]);
				RAW_DMG_DATA_SET(index);
			} else if (_lk_bankcmd_mode[b] == 1) {
				lk_dmg_set_address(index);
				RAW_DMG_DATA_SET(_lk_bankcmd_addr[b] & 0xFF);
			}
			PIN_WR_L();
			_delay_400ns();
			PIN_WR_H();
			PIN_CLK_H();
		}
	}
}

void lk_dmg_flash_mbc6(u16 buffer_offset) {
	RAW_DMG_DATA_DIR_OUT();
	
	// Set Sub-Bank A (0x4000-0x5FFF) to flash bank 1
	// Set Sub-Bank B (0x6000-0x7FFF) to flash bank 2
	lk_dmg_cart_write_byte(0x2800, 0x08);
	lk_dmg_cart_write_byte(0x3800, 0x08);
	lk_dmg_cart_write_byte(0x2000, 0x01);
	lk_dmg_cart_write_byte(0x3000, 0x02);
	
	// Flash write command sequence
	lk_dmg_cart_write_byte(0x7555, 0xAA);
	lk_dmg_cart_write_byte(0x4AAA, 0x55);
	lk_dmg_cart_write_byte(0x7555, 0xA0);
	
	// Restore bank
	lk_dmg_cart_write_byte(0x2800, 0x08);
	lk_dmg_cart_write_byte(0x3800, 0x08);
	lk_dmg_cart_write_byte(0x2000, _lk_var16[LK_VAR16_DMG_ROM_BANK]);
	lk_dmg_cart_write_byte(0x3000, _lk_var16[LK_VAR16_DMG_ROM_BANK]);
	
	// Write data
	for (u16 x = 0; x < 128; x++) {
		lk_dmg_cart_write_byte(_lk_var32[LK_VAR32_ADDRESS]++, data_buffer[buffer_offset+x]);
	}
	lk_dmg_cart_write_byte(_lk_var32[LK_VAR32_ADDRESS]-1, 0x00);
	
	// Status register check
	lk_dmg_verify_status_register(_lk_var32[LK_VAR32_ADDRESS]-1);
}

void lk_dmg_flash_E201264(u16 buffer_offset) {
	lk_dmg_cart_write_byte(0x4000, 0xFF);
	lk_dmg_cart_write_byte(_lk_var32[LK_VAR32_ADDRESS], 0xE0);
	lk_dmg_cart_write_byte(_lk_var32[LK_VAR32_ADDRESS], 128 - 1);
	lk_dmg_cart_write_byte(_lk_var32[LK_VAR32_ADDRESS], 0x00);
	for (u16 x = 0; x < 128; x++) {
		lk_dmg_cart_write_byte(_lk_var32[LK_VAR32_ADDRESS]+x, data_buffer[buffer_offset+x]);
	}
	lk_dmg_cart_write_byte(_lk_var32[LK_VAR32_ADDRESS], 0x0C);
	lk_dmg_cart_write_byte(_lk_var32[LK_VAR32_ADDRESS], 128 - 1);
	lk_dmg_cart_write_byte(_lk_var32[LK_VAR32_ADDRESS], 0x00);
	_lk_var32[LK_VAR32_ADDRESS] += 128;
	lk_dmg_cart_write_byte(0x4000, 0x70);
	lk_dmg_verify_status_register(_lk_var32[LK_VAR32_ADDRESS] - 1);
	lk_dmg_cart_write_byte(0x4000, 0xFF);
}

void lk_dmg_flash_bung_16m(u16 buffer_offset) {
	lk_dmg_change_bank(2);
	lk_dmg_flash_write_byte(0x6AAA, 0xAA);
	lk_dmg_change_bank(1);
	lk_dmg_flash_write_byte(0x5554, 0x55);
	lk_dmg_change_bank(2);
	lk_dmg_flash_write_byte(0x6AAA, 0xA0);
	lk_dmg_change_bank(_lk_var16[LK_VAR16_LAST_BANK_ACCESSED]);
	for (u16 x = 0; x < 128; x++) {
		lk_dmg_flash_write_byte(_lk_var32[LK_VAR32_ADDRESS]++, data_buffer[buffer_offset+x]);
	}
	_delay_us(100);
	lk_dmg_verify_status_register(_lk_var32[LK_VAR32_ADDRESS] - 1);
}

void lk_dmg_flash_datel_orbitv2() {
	for (u16 x = 0; x < _lk_var16[LK_VAR16_TRANSFER_SIZE]; x++) {
		lk_dmg_cart_write_byte(0x7FE1, 0x02);
		lk_dmg_cart_write_byte(0x5555, 0xAA);
		lk_dmg_cart_write_byte(0x2AAA, 0x55);
		lk_dmg_cart_write_byte(0x5555, 0xA0);
		lk_dmg_cart_write_byte(0x7FE1, _lk_var32[LK_VAR32_ADDRESS] >> 24);
		lk_dmg_cart_write_byte((_lk_var32[LK_VAR32_ADDRESS]++) & 0xFFFF, data_buffer[x]);
		_delay_us(20);
	}
}

void lk_dmg_flash_mmsa(u16 buffer_offset) {
	// Thanks to nocash et. al. for the initial research
	#ifdef DISABLE_INTERRUPTS
		DISABLE_INTERRUPTS();
	#endif
	lk_dmg_mmsa_access_mbc(true);
	lk_dmg_cart_write_byte(0x2100, 1);
	lk_dmg_mmsa_access_mbc(false);
	lk_dmg_mmsa_access_mapper();
	lk_dmg_mmsa_disable_flash_write_protect();
	lk_dmg_mmsa_map_full();
	lk_dmg_mmsa_flash_command(0x5555, 0xAA);
	lk_dmg_mmsa_flash_command(0x2AAA, 0x55);
	lk_dmg_mmsa_flash_command(0x5555, 0xA0);
	lk_dmg_mmsa_access_mbc(true);
	lk_dmg_cart_write_byte(0x2100, _lk_var16[LK_VAR16_DMG_ROM_BANK]);
	lk_dmg_mmsa_access_mbc(false);
	lk_dmg_mmsa_access_rom();
	for (u8 x = 0; x < 128; x++) {
		lk_dmg_cart_write_byte(_lk_var32[LK_VAR32_ADDRESS]++, data_buffer[buffer_offset+x]);
	}
	lk_dmg_cart_write_byte(_lk_var32[LK_VAR32_ADDRESS] - 1, 0xFF);
	_delay_ms(4);
	lk_dmg_verify_status_register(_lk_var32[LK_VAR32_ADDRESS] - 1);
	#ifdef ENABLE_INTERRUPTS
		ENABLE_INTERRUPTS();
	#endif
}
void lk_dmg_mmsa_flash_command(u16 addr, u8 data) {
	lk_dmg_cart_write_byte(0x120, 0x0F);
	lk_dmg_cart_write_byte(0x125, addr >> 8);
	lk_dmg_cart_write_byte(0x126, addr & 0xFF);
	lk_dmg_cart_write_byte(0x127, data);
	lk_dmg_cart_write_byte(0x13F, 0xA5);
	_delay_us(10);
}
void lk_dmg_mmsa_access_mapper(void) {
	lk_dmg_cart_write_byte(0x120, 0x09);
	lk_dmg_cart_write_byte(0x121, 0xAA);
	lk_dmg_cart_write_byte(0x122, 0x55);
	lk_dmg_cart_write_byte(0x13F, 0xA5);
	_delay_us(10);
}
void lk_dmg_mmsa_access_rom(void) {
	lk_dmg_cart_write_byte(0x120, 0x08);
	lk_dmg_cart_write_byte(0x13F, 0xA5);
	_delay_us(10);
}
void lk_dmg_mmsa_access_mbc(bool enable) {
	if (enable) {
		lk_dmg_cart_write_byte(0x120, 0x11);
		lk_dmg_cart_write_byte(0x13F, 0xA5);
	} else {
		lk_dmg_cart_write_byte(0x120, 0x10);
		lk_dmg_cart_write_byte(0x13F, 0xA5);
	}
	_delay_us(10);
}
void lk_dmg_mmsa_disable_flash_write_protect(void) {
	// stat.bit0
	lk_dmg_cart_write_byte(0x120, 0x0A);
	lk_dmg_cart_write_byte(0x125, 0x62);
	lk_dmg_cart_write_byte(0x126, 0x04);
	lk_dmg_cart_write_byte(0x13F, 0xA5);
	_delay_us(10);
	// stat.bit1
	lk_dmg_cart_write_byte(0x120, 0x02);
	lk_dmg_cart_write_byte(0x13F, 0xA5);
	_delay_us(10);
}
void lk_dmg_mmsa_map_full(void) {
	lk_dmg_cart_write_byte(0x120, 0x04);
	lk_dmg_cart_write_byte(0x13F, 0xA5);
	_delay_us(10);
}
void lk_dmg_mmsa_map_menu(void) {
	lk_dmg_cart_write_byte(0x120, 0x05);
	lk_dmg_cart_write_byte(0x13F, 0xA5);
	_delay_us(10);
}

// *** GAME BOY ADVANCE FUNCTIONS *** //
void lk_agb_save_eeprom_access(u8* buffer, u16 address, u8 eeprom_type, u8 mode) {
	u8 high_bits = (mode == LK_MODE_RAM_READ) ? 0xC0 : 0x80;
	s8 bit_shift = (eeprom_type == LK_SIZE_EEPROM_64K) ? 15 : 7;
	address |= high_bits << (bit_shift - 7);

	PIN_CS_L();

	// CLK toggle required by Korokoro Puzzle Happy Panecchu
	PIN_CLK_H();
	PIN_CLK_L();

	PIN_A0_OUT();

	for (u8 i = 0; i <= bit_shift; i++) {
		if (address & (1 << (bit_shift - i))) {
			PIN_A0_H();
		} else {
			PIN_A0_L();
		}
		PIN_WR_L();
		_delay_100ns();
		PIN_WR_H();
		_delay_100ns();
	}
	
	if (mode == LK_MODE_RAM_READ) {
		PIN_A0_L();
		_delay_100ns();
		PIN_WR_L();
		_delay_100ns();
		PIN_WR_H();
		PIN_CS_H();
		
		PIN_A0_IN();
		PIN_CS_L();
		
		for (u8 i = 0; i < 4; i++) {
			PIN_RD_L();
			_delay_100ns();
			PIN_RD_H();
			_delay_100ns();
		}
		
		for (u8 i = 0; i < 64; i++) {
			if (i % 8 == 0) buffer[i / 8] = 0;
			PIN_RD_L();
			_delay_100ns();
			PIN_RD_H();

			buffer[i / 8] |= (RAW_AGB_DATA_GET() & 1) << (7 - (i % 8));
		}

		PIN_CS_H();
		PIN_A0_OUT();

	} else if (mode == LK_MODE_RAM_WRITE) {
		for (u8 i = 0; i < 64; i++) {
			if (buffer[i >> 3] & (1 << (7 - (i % 8)))) {
				PIN_A0_H();
			} else {
				PIN_A0_L();
			}
			PIN_WR_L();
			_delay_100ns();
			PIN_WR_H();
			_delay_100ns();
		}

		PIN_A0_L();
		PIN_WR_L();
		_delay_100ns();
		PIN_WR_H();
		_delay_100ns();
	}

	PIN_CS_H();

	// CLK toggle required by Korokoro Puzzle Happy Panecchu
	PIN_CLK_H();
	PIN_CLK_L();
}

u16 lk_agb_cart_read_short(u32 address) {
	RAW_AGB_ADDR_SET(0);
	RAW_AGB_ADDR_DIR_OUT();
	RAW_AGB_ADDR_SET(address);
	PIN_CS_L();
	RAW_AGB_DATA_SET(0); // Set floating lines to 0 instead of mirroring the address
	RAW_AGB_DATA_DIR_IN();
	PIN_RD_L();
	_delay_100ns();
	u16 data = RAW_AGB_DATA_GET();
	PIN_RD_H();
	PIN_CS_H();
	return data;
}
void lk_agb_cart_read_data(void) {
	u16 left = _lk_var16[LK_VAR16_TRANSFER_SIZE];
	
	while (left > 0) {
		u16 chunk_len = left > CHUNK_MAX_LEN ? CHUNK_MAX_LEN : left;
		if (_lk_var8[LK_VAR8_AGB_READ_METHOD] == LK_AGB_READ_METHOD_MEMCPY) { // dump like memcpy() on real hardware
			for (u32 x = 0; x < chunk_len >> 1; x += 2) {
				RAW_AGB_ADDR_DIR_OUT();
				RAW_AGB_ADDR_SET(_lk_var32[LK_VAR32_ADDRESS]);
				PIN_CS_L();
				RAW_AGB_DATA_SET(0); // set floating lines to 0 instead of mirroring the address
				RAW_AGB_DATA_DIR_IN();
				PIN_RD_L();
				_delay_300ns();
				((u16*)data_buffer)[x] = RAW_AGB_DATA_GET();
				PIN_RD_H();
				PIN_RD_L();
				_delay_300ns();
				((u16*)data_buffer)[x+1] = RAW_AGB_DATA_GET();
				PIN_RD_H();
				PIN_CS_H();
				_lk_var32[LK_VAR32_ADDRESS] += 2;
			}
		} else if (_lk_var8[LK_VAR8_AGB_READ_METHOD] == LK_AGB_READ_METHOD_CPU) { // dump like CPU access on real hardware
			RAW_AGB_ADDR_DIR_OUT();
			RAW_AGB_ADDR_SET(_lk_var32[LK_VAR32_ADDRESS]);
			PIN_CS_L();
			RAW_AGB_DATA_SET(0); // set floating areas to 0 instead of mirroring the address
			RAW_AGB_DATA_DIR_IN();
			for (u32 x = 0; x < chunk_len >> 1; x++) {
				PIN_RD_L();
				_delay_300ns();
				((u16*)data_buffer)[x] = RAW_AGB_DATA_GET();
				PIN_RD_H();
			}
			PIN_CS_H();
			_lk_var32[LK_VAR32_ADDRESS] += chunk_len >> 1;
		} else { // dump by accessing each address separately
			for (u32 x = 0; x < chunk_len >> 1; x++) {
				((u16*)data_buffer)[x] = lk_agb_cart_read_short(_lk_var32[LK_VAR32_ADDRESS]++);
			}
		}
		
		lk_conn_send(data_buffer, chunk_len);
		left -= chunk_len;
	}
}
void lk_agb_cart_write_short(u32 address, u16 value) {
	RAW_AGB_ADDR_SET(0);
	RAW_AGB_ADDR_DIR_OUT();
	RAW_AGB_ADDR_SET(address);
	PIN_CS_L();
	RAW_AGB_DATA_SET(value);
	PIN_WR_L();
	_delay_500ns();
	PIN_WR_H();
	PIN_CS_H();
}

u8 lk_agb_cart_read_sram_byte(u16 address) {
	PIN_RD_H(); // needed for iG 1M flash save type detection
	RAW_DMG_ADDR_DIR_OUT();
	RAW_DMG_ADDR_SET(address);
	PIN_CS2_L();
	PIN_RD_L();
	_delay_400ns(); // FRAM needs this
	RAW_DMG_DATA_DIR_IN();
	u8 data = RAW_DMG_DATA_GET();
	PIN_RD_H();
	PIN_CS2_H();
	return data;
}
void lk_agb_cart_write_sram_byte(u16 address, u8 value) {
	RAW_DMG_DATA_DIR_OUT();
	RAW_DMG_ADDR_SET(address);
	RAW_DMG_DATA_SET(value);
	PIN_CS2_L();
	PIN_WR_L();
	_delay_400ns(); // FRAM needs this
	PIN_WR_H();
	PIN_CS2_H();
}

void lk_agb_cart_read_sram(void) {
	lk_set_mode(LK_MODE_DMG);
	u16 left = _lk_var16[LK_VAR16_TRANSFER_SIZE];
	PIN_RD_L();
	while (left > 0) {
		u16 chunk_len = left > CHUNK_MAX_LEN ? CHUNK_MAX_LEN : left;
		for (u32 x = 0; x < chunk_len; x++) {
			data_buffer[x] = lk_agb_cart_read_sram_byte(_lk_var32[LK_VAR32_ADDRESS]++);
		}
		lk_conn_send(data_buffer, chunk_len);
		left -= chunk_len;
	}
	lk_set_mode(LK_MODE_AGB);
}
void lk_agb_cart_write_sram(void) {
	lk_set_mode(LK_MODE_DMG);
	for (u32 x = 0; x < _lk_var16[LK_VAR16_TRANSFER_SIZE]; x++) {
		lk_agb_cart_write_sram_byte(_lk_var32[LK_VAR32_ADDRESS]++, data_buffer[x]);
	}
	lk_set_mode(LK_MODE_AGB);
}

void lk_agb_cart_read_eeprom(u8 eeprom_type) {
	RAW_AGB_ADDR_DIR_OUT();
	RAW_AGB_ADDR_SET(0xFFFF80);
	for (u32 x = 0; x < _lk_var16[LK_VAR16_TRANSFER_SIZE]; x += 8) {
		lk_agb_save_eeprom_access(data_buffer, _lk_var32[LK_VAR32_ADDRESS]++, eeprom_type, LK_MODE_RAM_READ);
		lk_conn_send(data_buffer, 8);
	}
	RAW_AGB_ADDR_SET(0);
	RAW_AGB_ADDR_DIR_OUT();
}
void lk_agb_cart_write_eeprom(u8 eeprom_type) {
	RAW_AGB_ADDR_DIR_OUT();
	RAW_AGB_ADDR_SET(0xFFFF80);
	for (u32 x = 0; x < _lk_var16[LK_VAR16_TRANSFER_SIZE]; x += 8) {
		lk_agb_save_eeprom_access(data_buffer+x, _lk_var32[LK_VAR32_ADDRESS]++, eeprom_type, LK_MODE_RAM_WRITE);
		_delay_us(8500);
	}
	RAW_AGB_ADDR_SET(0);
	RAW_AGB_ADDR_DIR_OUT();
}

void lk_agb_cart_write_flash_byte(u32 address, u16 value) {
	RAW_DMG_ADDR_DIR_OUT();
	RAW_DMG_ADDR_SET(address);
	RAW_DMG_DATA_DIR_OUT();
	RAW_DMG_DATA_SET(value);
	PIN_CS2_L();
	PIN_WR_L();
	_delay_500ns();
	PIN_CS2_H();
	PIN_WR_H();
}
void lk_agb_cart_write_flash_program_sequence(void) {
	lk_agb_cart_write_flash_byte(0x5555, 0xAA);
	lk_agb_cart_write_flash_byte(0x2AAA, 0x55);
	lk_agb_cart_write_flash_byte(0x5555, 0xA0);
}
void lk_agb_cart_write_flash(u8 flash_type) {
	lk_set_mode(LK_MODE_DMG);
	if (flash_type == LK_TYPE_FLASH_NON_ATMEL) {
		for (u32 x = 0; x < _lk_var16[LK_VAR16_TRANSFER_SIZE]; x++) {
			lk_agb_cart_write_flash_program_sequence();
			lk_agb_cart_write_flash_byte(_lk_var32[LK_VAR32_ADDRESS]++, data_buffer[x]);
			_delay_us(20);
		}
	} else {
		for (u32 x = 0; x < _lk_var16[LK_VAR16_TRANSFER_SIZE]; x += 128) {
			lk_agb_cart_write_flash_program_sequence();
			for (u8 y = 0; y < 128; y++) {
				lk_agb_cart_write_flash_byte((u16)(_lk_var32[LK_VAR32_ADDRESS] << 7) | (u16)y, data_buffer[x + y]);
			}
			_lk_var32[LK_VAR32_ADDRESS]++;
			_delay_ms(10);
		}
	}
	lk_set_mode(LK_MODE_AGB);
}

void lk_agb_cart_read_data_3d_memory(void) {
	// Thanks to endrift for the initial research
	lk_agb_cart_write_short(0x800184 >> 1, (_lk_var32[LK_VAR32_ADDRESS] * 2) & 0xFFFF); // actual ROM address
	lk_agb_cart_write_short(0x800186 >> 1, (_lk_var32[LK_VAR32_ADDRESS] * 2) >> 16);
	lk_agb_cart_write_short(0x800188 >> 1, 0x08001000 & 0xFFFF); // virtual ROM address to use
	lk_agb_cart_write_short(0x80018A >> 1, 0x08001000 >> 16);
	lk_agb_cart_write_short(0x80018C >> 1, _lk_var16[LK_VAR16_BUFFER_SIZE] / 0x200); // size (1 or 8) x 0x200 = actual size
	lk_agb_cart_write_short(0x80018E >> 1, 0);
	lk_agb_cart_write_short(0x800180 >> 1, 0x11); // command
	lk_agb_cart_write_short(0x800182 >> 1, 0);
	_delay_us(400); // mapper is usually ready after ~140 µs

	RAW_AGB_ADDR_DIR_OUT();
	RAW_AGB_ADDR_SET(0x1000 >> 1);
	PIN_CS_L();
	RAW_AGB_ADDR_SET(0);
	RAW_AGB_DATA_DIR_IN();

	u8 client_ack = LK_CMD_AGB_CART_READ_3D_MEMORY;

	for (u16 y = 0; y < (u16)(_lk_var16[LK_VAR16_BUFFER_SIZE] / _lk_var16[LK_VAR16_TRANSFER_SIZE]); y++) {
		for (u32 z = 0; z < (_lk_var16[LK_VAR16_TRANSFER_SIZE] >> 1); z++) {
			PIN_RD_L();
			_delay_500ns();
			((u16*)data_buffer)[z] = RAW_AGB_DATA_GET();
			PIN_RD_H();
		}
		lk_conn_send(data_buffer, _lk_var16[LK_VAR16_TRANSFER_SIZE]);
		client_ack = lk_conn_recv_u8();
		if (client_ack != LK_CMD_AGB_CART_READ_3D_MEMORY) break;
	}

	_lk_var32[LK_VAR32_ADDRESS] += (_lk_var16[LK_VAR16_BUFFER_SIZE] >> 1);
}

void lk_agb_verify_data(u32 addr, u16 comp) {
	_timeout_init();
	u16 data;
	while (1) {
		data = lk_agb_cart_read_short(addr);
		if (data == comp) {
			_timeout_reset();
			break;
		}
		if (_timeout_check()) {
			dprint("lk_agb_verify_data(addr=%x, comp=%x): Timed out with %x!\r\n", addr, comp, data);
			_lk_var16[LK_VAR16_STATUS_REGISTER] = data;
			break;
		}
	}
}
void lk_agb_verify_status_register(u32 addr) {
	_timeout_init();
	u16 data;
	while (1) {
		if (_lk_var8[LK_VAR8_FLASH_SHARP_VERIFY_SR] == true) {
			lk_agb_cart_write_short(addr, 0x70);
			_delay_us(2);
		}
		data = lk_agb_cart_read_short(addr);
		if ((data & _lk_var16[LK_VAR16_STATUS_REGISTER_MASK]) == _lk_var16[LK_VAR16_STATUS_REGISTER_VALUE]) {
			_timeout_reset();
			break;
		}
		if (_timeout_check()) {
			dprint("lk_agb_verify_status_register(addr=%x): Timed out with %x!\r\n", addr, data);
			_lk_var16[LK_VAR16_STATUS_REGISTER] = data;
			break;
		}
	}
	RAW_AGB_ADDR_DIR_OUT();
}

void lk_dmg_agb_flash_unbuffered(void) {
	u16 data;
	if (_lk_var8[LK_VAR8_CART_MODE] == LK_MODE_DMG) {
		RAW_DMG_DATA_DIR_OUT();
	} else {
		RAW_AGB_DATA_DIR_OUT();
	}

	if ((_lk_var8[LK_VAR8_FLASH_COMMAND_SET] == LK_FLASH_COMMAND_SET_AMD)) {
		if (_lk_var8[LK_VAR8_CART_MODE] == LK_MODE_DMG) {
			for (u16 x = 0; x < _lk_var16[LK_VAR16_TRANSFER_SIZE]; x++) {
				if (data_buffer[x] != 0xFF) {
					if (_lk_var8[LK_VAR8_FLASH_COMMANDS_BANK_1] == 1) {
						lk_dmg_change_bank(1);
					}
					lk_dmg_flash_write_byte(_lk_flashcmd_addr[0], _lk_flashcmd_data[0]);	// AAA=AA
					lk_dmg_flash_write_byte(_lk_flashcmd_addr[1], _lk_flashcmd_data[1]);	// 555=55
					lk_dmg_flash_write_byte(_lk_flashcmd_addr[2], _lk_flashcmd_data[2]);	// AAA=A0
					if (_lk_var8[LK_VAR8_FLASH_COMMANDS_BANK_1] == 1) {
						lk_dmg_change_bank(_lk_var16[LK_VAR16_LAST_BANK_ACCESSED]);
					}
					lk_dmg_flash_write_byte(_lk_var32[LK_VAR32_ADDRESS], data_buffer[x]);	// PA=PD
					if (_lk_var8[LK_VAR8_FLASH_PULSE_RESET] == 1) {
						_timeout_reset();
						_delay_us(10);
						PIN_CS2_L();
						_delay_us(3);
						PIN_CS2_H();
						if (_lk_var16[LK_VAR16_DMG_ROM_BANK] > 0) {
							lk_dmg_change_bank(_lk_var16[LK_VAR16_LAST_BANK_ACCESSED]);
						}
					}
					lk_dmg_verify_data(_lk_var32[LK_VAR32_ADDRESS], data_buffer[x]);
					if (_timeout_check()) break;
				}
				_lk_var32[LK_VAR32_ADDRESS]++;
			}
		} else { // AGB
			for (u16 x = 0; x < _lk_var16[LK_VAR16_TRANSFER_SIZE]; x += 2) {
				data = data_buffer[x + 1] << 8 | data_buffer[x];
				if (data_buffer[x] != 0xFFFF) {
					lk_agb_cart_write_short(_lk_flashcmd_addr[0], _lk_flashcmd_data[0]);	// AAA=AA
					lk_agb_cart_write_short(_lk_flashcmd_addr[1], _lk_flashcmd_data[1]);	// 555=55
					lk_agb_cart_write_short(_lk_flashcmd_addr[2], _lk_flashcmd_data[2]);	// AAA=A0
					lk_agb_cart_write_short(_lk_var32[LK_VAR32_ADDRESS], data);			// PA=PD
					lk_agb_verify_data(_lk_var32[LK_VAR32_ADDRESS], data);
					if (_timeout_check()) break;
				}
				_lk_var32[LK_VAR32_ADDRESS]++;
			}
		}

	} else { /*if ((_lk_var8[LK_VAR8_FLASH_COMMAND_SET] == FLASH_COMMAND_SET_INTEL)) {*/
		if (_lk_var8[LK_VAR8_CART_MODE] == LK_MODE_DMG) {
			for (u16 x = 0; x < _lk_var16[LK_VAR16_TRANSFER_SIZE]; x++) {
				if (data_buffer[x] != 0xFF) {
					lk_dmg_flash_write_byte(_lk_var32[LK_VAR32_ADDRESS], _lk_flashcmd_data[0]);		// 0=70
					lk_dmg_verify_status_register(_lk_var32[LK_VAR32_ADDRESS]);
					lk_dmg_flash_write_byte(_lk_var32[LK_VAR32_ADDRESS], _lk_flashcmd_data[1]);		// 0=10
					lk_dmg_flash_write_byte(_lk_var32[LK_VAR32_ADDRESS], data_buffer[x]);			// PA=PD
					lk_dmg_verify_status_register(_lk_var32[LK_VAR32_ADDRESS]);
					if (_timeout_check()) return;
				}
				_lk_var32[LK_VAR32_ADDRESS]++;
			}
		} else { // AGB
			for (u16 x = 0; x < _lk_var16[LK_VAR16_TRANSFER_SIZE]; x += 2) {
				data = data_buffer[x + 1] << 8 | data_buffer[x];
				if (data != 0xFFFF) {
					lk_agb_cart_write_short(_lk_var32[LK_VAR32_ADDRESS], _lk_flashcmd_data[0]);	// 0=70
					lk_agb_verify_status_register(_lk_var32[LK_VAR32_ADDRESS]);
					lk_agb_cart_write_short(_lk_var32[LK_VAR32_ADDRESS], _lk_flashcmd_data[1]);	// 0=10
					lk_agb_cart_write_short(_lk_var32[LK_VAR32_ADDRESS], data);					// PA=PD
					lk_agb_verify_status_register(_lk_var32[LK_VAR32_ADDRESS]);
					if (_timeout_check()) return;
				}
				_lk_var32[LK_VAR32_ADDRESS]++;
			}
		}
	}
}

void lk_dmg_agb_flash_buffered(u8 this_iteration, u8 num_of_iterations, u16 transfer_size, u16 buffer_offset, u32 sector_address) {
	u16 buffer_size;
	void (*p_flash_write_data)(u32, u16);
	void (*p_verify_status_register)(u32);
	void (*p_verify_data)(u32, u16);
	if (_lk_var8[LK_VAR8_CART_MODE] == LK_MODE_DMG) { // DMG mode
		p_flash_write_data = &lk_dmg_flash_write_byte;
		p_verify_status_register = &lk_dmg_verify_status_register;
		p_verify_data = &lk_dmg_verify_data;
		buffer_size = _lk_var16[LK_VAR16_BUFFER_SIZE];
	} else { // AGB mode
		p_flash_write_data = &lk_agb_cart_write_short;
		p_verify_status_register = &lk_agb_verify_status_register;
		p_verify_data = &lk_agb_verify_data;
		buffer_size = (_lk_var16[LK_VAR16_BUFFER_SIZE] >> 1);
	}
	
	if ((_lk_var8[LK_VAR8_FLASH_COMMAND_SET] == LK_FLASH_COMMAND_SET_AMD)) {
		if (this_iteration == 0) {
			if (_lk_var8[LK_VAR8_CART_MODE] == LK_MODE_DMG) {
				RAW_DMG_DATA_DIR_OUT();
			} else { // AGB
				RAW_AGB_DATA_DIR_OUT();
			}
			p_flash_write_data(_lk_flashcmd_addr[0], _lk_flashcmd_data[0]);	// AAA=AA
			p_flash_write_data(_lk_flashcmd_addr[1], _lk_flashcmd_data[1]);	// 555=55
			p_flash_write_data(sector_address, _lk_flashcmd_data[2]);		// SA=25
			if (_lk_var8[LK_VAR8_FLASH_DOUBLE_DIE] == 1) {
				p_flash_write_data(sector_address, ((buffer_size - 1) << 8) | (buffer_size - 1));	// SA=BS
			} else {
				p_flash_write_data(sector_address, buffer_size - 1);		// SA=BS
			}
		}

		// Write data
		if (_lk_var8[LK_VAR8_CART_MODE] == LK_MODE_DMG) {
			for (u16 x = 0; x < transfer_size; x ++) {						// PA=PD
				RAW_DMG_ADDR_SET(_lk_var32[LK_VAR32_ADDRESS]);
				RAW_DMG_DATA_SET(data_buffer[buffer_offset+x]);
				
				if (_lk_var8[LK_VAR8_FLASH_WE_PIN] == LK_FLASH_WE_PIN_WR) {
					PIN_WR_L();
					_delay_400ns();
					PIN_WR_H();
				} else if (_lk_var8[LK_VAR8_FLASH_WE_PIN] == LK_FLASH_WE_PIN_AUDIO) {
					PIN_AUDIO_L();
					_delay_400ns();
					PIN_AUDIO_H();
				} else if (_lk_var8[LK_VAR8_FLASH_WE_PIN] == LK_FLASH_WE_PIN_WR_RESET) {
					PIN_CS2_L();
					PIN_WR_L();
					_delay_400ns();
					PIN_WR_H();
					PIN_CS2_H();
				}
				
				_lk_var32[LK_VAR32_ADDRESS]++;
			}
		} else { // AGB
			for (u16 x = 0; x < transfer_size; x += 2) {					// PA=PD
				RAW_AGB_ADDR_SET(_lk_var32[LK_VAR32_ADDRESS]);
				PIN_CS_L();
				_delay_300ns();
				RAW_AGB_DATA_SET(data_buffer[buffer_offset+x+1] << 8 | data_buffer[buffer_offset+x]);
				PIN_WR_L();
				_delay_500ns();
				PIN_WR_H();
				
				PIN_CS_H();
				_lk_var32[LK_VAR32_ADDRESS]++;
			}
		}

		if (this_iteration == num_of_iterations - 1) {
			// Write buffer to flash
			p_flash_write_data(sector_address, _lk_flashcmd_data[5]);		// SA=29
			if (_lk_var8[LK_VAR8_CART_MODE] == LK_MODE_DMG) {
				p_verify_data(_lk_var32[LK_VAR32_ADDRESS]-1, data_buffer[buffer_offset+transfer_size-1]);
			} else { // AGB
				p_verify_data(_lk_var32[LK_VAR32_ADDRESS]-1, data_buffer[buffer_offset+transfer_size-1] << 8 | data_buffer[buffer_offset+transfer_size-2]);
			}
			if (_timeout_check()) return;
		}

	} else { /*if ((_lk_var8[LK_VAR8_FLASH_COMMAND_SET] == FLASH_COMMAND_SET_INTEL)) {*/
		if (this_iteration == 0) {
			p_flash_write_data(sector_address, _lk_flashcmd_data[0]);		// SA=E8
			if (_lk_var8[LK_VAR8_FLASH_SHARP_VERIFY_SR] == true) {
				_delay_us(10);
			} else {
				p_verify_status_register(sector_address);
				if (_timeout_check()) return;
			}
			p_flash_write_data(sector_address, buffer_size - 1);			// SA=BS
		}

		// Write data
		if (_lk_var8[LK_VAR8_CART_MODE] == LK_MODE_DMG) {
			RAW_DMG_ADDR_DIR_OUT();
			RAW_DMG_DATA_DIR_OUT();
			for (u16 x = 0; x < transfer_size; x++) {						// PA=PD
				RAW_DMG_ADDR_SET(_lk_var32[LK_VAR32_ADDRESS]);
				RAW_DMG_DATA_SET(data_buffer[buffer_offset+x]);
				
				if (_lk_var8[LK_VAR8_FLASH_WE_PIN] == LK_FLASH_WE_PIN_WR) {
					PIN_WR_L();
					_delay_400ns();
					PIN_WR_H();
				} else if (_lk_var8[LK_VAR8_FLASH_WE_PIN] == LK_FLASH_WE_PIN_AUDIO) {
					PIN_AUDIO_L();
					_delay_400ns();
					PIN_AUDIO_H();
				} else if (_lk_var8[LK_VAR8_FLASH_WE_PIN] == LK_FLASH_WE_PIN_WR_RESET) {
					PIN_CS2_L();
					PIN_WR_L();
					_delay_400ns();
					PIN_WR_H();
					PIN_CS2_H();
				}
				
				_lk_var32[LK_VAR32_ADDRESS]++;
			}
		} else { // AGB
			for (u16 x = 0; x < transfer_size; x += 2) {					// PA=PD
				RAW_AGB_ADDR_SET(_lk_var32[LK_VAR32_ADDRESS]);
				PIN_CS_L();
				RAW_AGB_DATA_SET(data_buffer[buffer_offset+x+1] << 8 | data_buffer[buffer_offset+x]);
				PIN_WR_L();
				_delay_400ns();
				PIN_WR_H();
				PIN_CS_H();
				_lk_var32[LK_VAR32_ADDRESS]++;
			}
		}

		if (this_iteration == num_of_iterations - 1) {
			// Write buffer to flash
			p_flash_write_data(sector_address, _lk_flashcmd_data[3]);		// SA=D0
			p_verify_status_register(sector_address);
			p_flash_write_data(sector_address, _lk_flashcmd_data[4]);		// SA=FF
		}
		
		if (_timeout_check()) return;
	}
}

void lk_agb_flash2advance_buffered(u16 transfer_size, u16 buffer_offset, u32 sector_address) {
	u16 buffer_size = (_lk_var16[LK_VAR16_BUFFER_SIZE] >> 1);
	RAW_AGB_ADDR_DIR_OUT();
	
	lk_agb_cart_write_short(sector_address, _lk_flashcmd_data[0]);			// SA=E8
	lk_agb_cart_write_short(sector_address + 1, _lk_flashcmd_data[0]);		// SA+1=E8
	lk_agb_verify_status_register(sector_address);
	lk_agb_verify_status_register(sector_address + 1);
	lk_agb_cart_write_short(sector_address, (buffer_size >> 1) - 1);		// SA=BS/2
	lk_agb_cart_write_short(sector_address + 1, (buffer_size >> 1) - 1);	// SA+1=BS/2
	
	for (u16 x = 0; x < transfer_size; x += 2) {							// PA=PD
		RAW_AGB_ADDR_SET(_lk_var32[LK_VAR32_ADDRESS]);
		PIN_CS_L();
		RAW_AGB_DATA_SET(data_buffer[buffer_offset+x+1] << 8 | data_buffer[buffer_offset+x]);
		PIN_WR_L();
		_delay_200ns();
		PIN_WR_H();
		PIN_CS_H();
		
		_lk_var32[LK_VAR32_ADDRESS]++;
	}

	// Write buffer to flash
	lk_agb_cart_write_short(sector_address, _lk_flashcmd_data[3]);			// SA=D0
	lk_agb_cart_write_short(sector_address + 1, _lk_flashcmd_data[3]);		// SA+1=D0
	lk_agb_verify_status_register(sector_address);
	lk_agb_verify_status_register(sector_address + 1);
	lk_agb_cart_write_short(sector_address, _lk_flashcmd_data[4]);			// SA=FF
	lk_agb_cart_write_short(sector_address + 1, _lk_flashcmd_data[4]);		// SA+1=FF
}

// This function unlocks some cartridges like the DACS one, and is
// also needed for Sintax bootlegs to respond with the ROM header.
void lk_agb_bootup_sequence(void) { // Thanks TachY3n
	u16 x, y, z, value = 0;
	u32 b, hash = 0;

	// 3D Memory GBA Video cartridges need this
	PIN_WR_L();
	PIN_RD_L();
	_delay_us(1);
	PIN_RD_H();
	PIN_WR_H();

	lk_agb_cart_read_short(0x5A);
	lk_agb_cart_read_short(0xFFFFF0);
	lk_agb_cart_read_short(0xFFFFF1);
	lk_agb_cart_read_short(0xFFFFF2);
	lk_agb_cart_read_short(0xFFFFF3);
	lk_agb_cart_read_short(0xFFFFF4);
	lk_agb_cart_read_short(0xFFFFF5);
	lk_agb_cart_read_short(0xFFFFF6);
	lk_agb_cart_read_short(0xFFFFF7);
	lk_agb_cart_read_short(0xFFFFF8);
	lk_agb_cart_read_short(0xFFFFF9);
	
	for (x = 0x9C; x < 0xB8; x += 2) {
		value = lk_agb_cart_read_short(x >> 1);
		for (u8 y = 0; y < 2; y++) {
			if ((x == 0x9C) && (y == 0)) continue;
			b = (value >> (y * 8)) & 0xFF;
			hash = (hash >> 3) | (hash << 29);
			hash = hash ^ b;
			b = b << 8;
			hash = hash ^ b;
			b = b << 8;
			hash = hash ^ b;
			b = b << 8;
			hash = hash ^ b;
		}
	}
	hash <<= 27;
	hash >>= 30;
	
	u32 tindex = 6 * hash + ((lk_agb_cart_read_short(0x9E >> 1) & 0xFF) & 3) * 0x18;

	for (x = 2; x < 42; ++x) {
		lk_agb_cart_read_short(x);
	}

	for (y = 0; y < 6; ++y) {
		u32 address = agb_bootup_table[tindex++];
		lk_agb_cart_read_short(address);
		for (z = 10 * y; z < 10 * (y + 1); ++z) {
			lk_agb_cart_read_short(x + z);
		}
	}
	for (z = 10 * y; z < 10 * (y + 1); ++z) {
		lk_agb_cart_read_short(x + z);
	}

	lk_agb_cart_read_short(0);
	lk_agb_cart_read_short(1);
}

void lk_agb_flash_fujitsu_fastmode(void) {
	u16 data;
	lk_agb_cart_write_short(_lk_flashcmd_addr[0], _lk_flashcmd_data[0]);	// AAA=AAA9
	lk_agb_cart_write_short(_lk_flashcmd_addr[1], _lk_flashcmd_data[1]);	// 555=5556
	lk_agb_cart_write_short(_lk_flashcmd_addr[2], _lk_flashcmd_data[2]);	// AAA=2020
	for (u16 x = 0; x < _lk_var16[LK_VAR16_TRANSFER_SIZE]; x += 2) {
		data = data_buffer[x + 1] << 8 | data_buffer[x];
		if (data_buffer[x] != 0xFFFF) {
			lk_agb_cart_write_short(_lk_var32[LK_VAR32_ADDRESS], _lk_flashcmd_data[3]);	// AAA=A0A0
			lk_agb_cart_write_short(_lk_var32[LK_VAR32_ADDRESS], data);					// PA=PD
			lk_agb_verify_data(_lk_var32[LK_VAR32_ADDRESS], data);
			if (_timeout_check()) break;
		}
		_lk_var32[LK_VAR32_ADDRESS]++;
	}
	lk_agb_cart_write_short(_lk_flashcmd_addr[4], _lk_flashcmd_data[4]);	// 0=F0F0
}

void lk_agb_flash_paged(u16 buffer_offset) {
	u16 data;
	u32 page_address = _lk_var32[LK_VAR32_ADDRESS];
	lk_agb_cart_write_short(_lk_flashcmd_addr[0], _lk_flashcmd_data[0]);			// 0=70
	lk_agb_verify_status_register(page_address);
	if (_timeout_check()) return;
	lk_agb_cart_write_short(_lk_flashcmd_addr[1], _lk_flashcmd_data[1]);			// 0=41
	for (u16 x = 0; x < _lk_var16[LK_VAR16_BUFFER_SIZE]; x += 2) {
		data = data_buffer[buffer_offset + x + 1] << 8 | data_buffer[buffer_offset + x];
		lk_agb_cart_write_short(_lk_var32[LK_VAR32_ADDRESS], data);				// PA=PD
		_lk_var32[LK_VAR32_ADDRESS]++;
	}
	lk_agb_verify_status_register(page_address);
}

void lk_agb_flash_gbamp() {
	u32 addr;
	u16 data;
	for (u16 x = 0; x < _lk_var16[LK_VAR16_TRANSFER_SIZE]; x += 2) {
		lk_agb_cart_write_short(0x213C2, 0xAA);
		lk_agb_cart_write_short(0x10C3D, 0x55);
		lk_agb_cart_write_short(0x213C2, 0xA0);
		addr = _lk_var32[LK_VAR32_ADDRESS]++;
		addr = (addr >> 13 << 16) | (addr & 0x1FFF);
		data = data_buffer[x + 1] << 8 | data_buffer[x];
		lk_agb_cart_write_short(addr, data);
		lk_agb_verify_data(addr, data);
		if (_timeout_check()) break;
	}
}

u32 lk_dmg_agb_calc_crc32(u32 length) {
	u32 checksum = 0;
	checksum = ~checksum;
	if (_lk_var8[LK_VAR8_CART_MODE] == LK_MODE_DMG) {
		PIN_RD_L();
		PIN_CLK_L(); // Pocket Camera needs this
		RAW_DMG_ADDR_DIR_OUT();
		for (u32 chunk_begin = 0; chunk_begin < length; chunk_begin += CHUNK_MAX_LEN) {
			u32 chunk_end = chunk_begin + CHUNK_MAX_LEN;
			if (chunk_end > length) chunk_end = length;
			u32 chunk_length = chunk_end - chunk_begin;

			for (u32 x = 0; x < chunk_length; ++x) {
				RAW_DMG_ADDR_SET(_lk_var32[LK_VAR32_ADDRESS]);
				_delay_400ns();
				data_buffer[x] = RAW_DMG_DATA_GET();
				PIN_ADDR_H(15);
				_lk_var32[LK_VAR32_ADDRESS]++;
			}
			LK_Chromatic_async_flush(data_buffer, chunk_length);
			for (u32 x = 0; x < chunk_length; x++) {
				checksum = crc32_table[(checksum ^ data_buffer[x]) & 0xFF] ^ (checksum >> 8);
			}
		}
		PIN_RD_H();
	} else { // AGB mode
		RAW_AGB_ADDR_DIR_OUT();
		RAW_AGB_ADDR_SET(_lk_var32[LK_VAR32_ADDRESS]);
		PIN_CS_L();
		RAW_AGB_DATA_DIR_IN();
		for (u32 x = 0; x < length >> 1; x++) {
			PIN_RD_L();
			_delay_100ns();
			u16 temp = RAW_AGB_DATA_GET();
			checksum = crc32_table[(checksum ^ (temp & 0xFF)) & 0xFF] ^ (checksum >> 8);
			checksum = crc32_table[(checksum ^ (temp >> 8)) & 0xFF] ^ (checksum >> 8);
			PIN_RD_H();
		}
		PIN_CS_H();
		_lk_var32[LK_VAR32_ADDRESS] += length >> 1;
	}
	return ~checksum;
}

void lk_agb_gpio_rtc_command(u8 command) {
	u8 bit = 0;
	for (u8 i=0; i<8; i++) {
		bit = (command >> (7 - i)) & 0x01;
		lk_agb_cart_write_short(AGB_GPIO_REG_DAT, 4 | (bit << 1));
		lk_agb_cart_write_short(AGB_GPIO_REG_DAT, 4 | (bit << 1));
		lk_agb_cart_write_short(AGB_GPIO_REG_DAT, 4 | (bit << 1));
		lk_agb_cart_write_short(AGB_GPIO_REG_DAT, 5 | (bit << 1));
	}
}
u8 lk_agb_gpio_rtc_read_data(void) {
	u8 bit = 0;
	u8 data = 0;
	for (u8 i=0; i<8; i++) {
		lk_agb_cart_write_short(AGB_GPIO_REG_DAT, 4);
		lk_agb_cart_write_short(AGB_GPIO_REG_DAT, 4);
		lk_agb_cart_write_short(AGB_GPIO_REG_DAT, 4);
		lk_agb_cart_write_short(AGB_GPIO_REG_DAT, 4);
		lk_agb_cart_write_short(AGB_GPIO_REG_DAT, 4);
		lk_agb_cart_write_short(AGB_GPIO_REG_DAT, 5);
		u16 temp = lk_agb_cart_read_short(AGB_GPIO_REG_DAT);
		bit = (temp & 2) >> 1;
		data = (data >> 1) | (bit << 7);
	}
	return data;
}
void lk_agb_read_gpio_rtc(void) {
	// Read Status
	lk_agb_cart_write_short(AGB_GPIO_REG_RE, 1); // Enable RTC mapping
	lk_agb_cart_write_short(AGB_GPIO_REG_DAT, 1);
	lk_agb_cart_write_short(AGB_GPIO_REG_DAT, 5);
	lk_agb_cart_write_short(AGB_GPIO_REG_CNT, 7); // Write enable
	lk_agb_gpio_rtc_command(AGB_GPIO_RTC_READ_STATUS);
	lk_agb_cart_write_short(AGB_GPIO_REG_CNT, 5); // Read enable
	data_buffer[0] = lk_agb_gpio_rtc_read_data();
	lk_agb_cart_write_short(AGB_GPIO_REG_DAT, 1);
	lk_agb_cart_write_short(AGB_GPIO_REG_DAT, 1);
	lk_agb_cart_write_short(AGB_GPIO_REG_RE, 0); // Disable RTC mapping
	
	// Read Data
	lk_agb_cart_write_short(AGB_GPIO_REG_RE, 1); // Enable RTC mapping
	lk_agb_cart_write_short(AGB_GPIO_REG_DAT, 1);
	lk_agb_cart_write_short(AGB_GPIO_REG_DAT, 5);
	lk_agb_cart_write_short(AGB_GPIO_REG_CNT, 7); // Write enable
	lk_agb_gpio_rtc_command(AGB_GPIO_RTC_READ_DATE);
	lk_agb_cart_write_short(AGB_GPIO_REG_CNT, 5); // Read enable
	for (u8 i=0; i<7; i++) {
		data_buffer[1+i] = lk_agb_gpio_rtc_read_data();
	}
	lk_agb_cart_write_short(AGB_GPIO_REG_DAT, 1);
	lk_agb_cart_write_short(AGB_GPIO_REG_DAT, 1);
	lk_agb_cart_write_short(AGB_GPIO_REG_RE, 0); // Disable RTC mapping
}

/****************************************************/

// This function should be called periodically in the main loop after calling lk_loop(), or via a timer.
void lk_cart_power_off_proc() {
	if (auto_off_timer_suspended) return;
	if (_lk_var8[LK_VAR8_CART_POWERED] != true) return;
	if (activity_done && (lk_runtime > activity_last_run + 10)) {
		if (_lk_var8[LK_VAR8_CART_POWERED] == true) {
			ACTIVITY_LED_ON();
		}
		activity_done = false; // Reset activity flag to prevent continuous calling
	} else if (lk_runtime > activity_last_run + _lk_var32[LK_VAR32_AUTO_POWEROFF_TIME]) {
		lk_cart_power_off();
	}
}
