/*
	LK Firmware version L15
	Author: Lesserkuma (github.com/Lesserkuma)
	Last Modified: 2026-05-23T17:51:58+02:00
*/

#ifndef _LK_H_
#define _LK_H_

#define __BUILD_TIMESTAMP__	1780508702
//#define DEBUG_VERSION

#include <stdint.h>
#include <stdlib.h>

// Device specifics
#ifdef LK_DEVICE_HEADER
	#include LK_DEVICE_HEADER
#else
	#include "LK_device.h"
#endif

#ifndef NULL
	#define NULL ((void *)0)
#endif
#ifndef bool
	#define bool u8
#endif
#ifndef u8
	#define u8 uint8_t
	#define u16 uint16_t
	#define u32 uint32_t
#endif
#ifndef s8
	#define s8 int8_t
	#define s16 int16_t
	#define s32 int32_t
#endif
#ifndef true
	#define true 1
	#define false 0
#endif

#define LK_FIRMWARE_VERSION			15
#define LK_MODE_DMG					1
#define LK_MODE_AGB					2
#define LK_MODE_ROM_READ			1
#define LK_MODE_ROM_WRITE			2
#define LK_MODE_RAM_READ			3
#define LK_MODE_RAM_WRITE			4
#define LK_SIZE_EEPROM_4K			1
#define LK_SIZE_EEPROM_64K			2
#define LK_TYPE_FLASH_NON_ATMEL		1
#define LK_TYPE_FLASH_ATMEL			2
#define LK_STATUS_OK				1
#define LK_STATUS_ERROR				2
#define LK_STATUS_CHUNK_NEXT		3

#define LK_FLASH_COMMAND_SET_AMD				1
#define LK_FLASH_COMMAND_SET_INTEL				2

#define LK_FLASH_METHOD_UNBUFFERED				1
#define LK_FLASH_METHOD_BUFFERED				2
#define LK_FLASH_METHOD_DMG_MMSA				3
#define LK_FLASH_METHOD_DMG_MBC6				4
#define LK_FLASH_METHOD_AGB_FLASH2ADVANCE		5
#define LK_FLASH_METHOD_AGB_FUJITSU_FASTMODE	6
#define LK_FLASH_METHOD_AGB_SAVE_FLASH			7
#define LK_FLASH_METHOD_AGB_PAGED				8
#define LK_FLASH_METHOD_DMG_DATEL_ORBITV2		9
#define LK_FLASH_METHOD_DMG_E201264				10
#define LK_FLASH_METHOD_AGB_GBAMP				11
#define LK_FLASH_METHOD_DMG_BUNG_16M			12

#define LK_FLASH_WE_PIN_WR						1
#define LK_FLASH_WE_PIN_AUDIO					2
#define LK_FLASH_WE_PIN_WR_RESET				3

#define LK_DMG_READ_METHOD_PULSE_RD				0
#define LK_DMG_READ_METHOD_PULSE_A15			1
#define LK_DMG_READ_METHOD_PULSE_A15_SLOW		2

#define LK_AGB_READ_METHOD_SINGLE				0
#define LK_AGB_READ_METHOD_MEMCPY				1
#define LK_AGB_READ_METHOD_CPU					2

// *** GENERAL COMMANDS *** //
#define LK_CMD_DEBUG						0xA0
#define LK_CMD_QUERY_FW_INFO				0xA1
#define LK_CMD_SET_MODE_AGB					0xA2
#define LK_CMD_SET_MODE_DMG					0xA3
#define LK_CMD_SET_VOLTAGE_3_3V				0xA4
#define LK_CMD_SET_VOLTAGE_5V				0xA5
#define LK_CMD_SET_VARIABLE					0xA6
#define LK_CMD_SET_FLASH_CMD				0xA7
#define LK_CMD_SET_ADDR_AS_INPUTS			0xA8
#define LK_CLK_TOGGLE						0xA9
#define LK_CMD_ENABLE_PULLUPS				0xAB
#define LK_CMD_DISABLE_PULLUPS				0xAC
#define LK_CMD_GET_VARIABLE					0xAD
#define LK_CMD_GET_VAR_STATE				0xAE
#define LK_CMD_SET_VAR_STATE				0xAF
#define LK_CMD_DMG_CART_READ				0xB1
#define LK_CMD_DMG_CART_WRITE				0xB2
#define LK_CMD_DMG_CART_WRITE_SRAM			0xB3
#define LK_CMD_DMG_MBC_RESET				0xB4
#define LK_CMD_DMG_MBC7_READ_EEPROM			0xB5
#define LK_CMD_DMG_MBC7_WRITE_EEPROM		0xB6
#define LK_CMD_DMG_MBC6_MMSA_WRITE_FLASH	0xB7
#define LK_CMD_DMG_SET_BANK_CHANGE_CMD		0xB8
#define LK_CMD_DMG_EEPROM_WRITE				0xB9
#define LK_CMD_AGB_CART_READ				0xC1
#define LK_CMD_AGB_CART_WRITE				0xC2
#define LK_CMD_AGB_CART_READ_SRAM			0xC3
#define LK_CMD_AGB_CART_WRITE_SRAM			0xC4
#define LK_CMD_AGB_CART_READ_EEPROM			0xC5
#define LK_CMD_AGB_CART_WRITE_EEPROM		0xC6
#define LK_CMD_AGB_CART_WRITE_FLASH_DATA	0xC7
#define LK_CMD_AGB_CART_READ_3D_MEMORY		0xC8
#define LK_CMD_AGB_BOOTUP_SEQUENCE			0xC9
#define LK_CMD_AGB_READ_GPIO_RTC			0xCA
#define LK_CMD_DMG_FLASH_WRITE_BYTE			0xD1
#define LK_CMD_AGB_FLASH_WRITE_SHORT		0xD2
#define LK_CMD_FLASH_PROGRAM				0xD3
#define LK_CMD_CART_WRITE_FLASH_CMD			0xD4
#define LK_CMD_CALC_CRC32					0xD5
#define LK_CMD_BOOTLOADER_RESET				0xF1
#define LK_CMD_CART_PWR_ON					0xF2
#define LK_CMD_CART_PWR_OFF					0xF3
#define LK_CMD_QUERY_CART_PWR				0xF4
#define LK_CMD_SET_PIN						0xF5
#define LK_CMD_GET_SWITCH_STATE				0xF6
#define LK_CMD_PING							0xFE

// *** FIRMWARE VARIABLES *** //
#define LK_NUM_OF_VARIABLES_32BIT			2
#define LK_VAR32_ADDRESS					0x00
#define LK_VAR32_AUTO_POWEROFF_TIME			0x01

#define LK_NUM_OF_VARIABLES_16BIT			7
#define LK_VAR16_TRANSFER_SIZE				0x00
#define LK_VAR16_BUFFER_SIZE				0x01
#define LK_VAR16_DMG_ROM_BANK				0x02
#define LK_VAR16_STATUS_REGISTER			0x03
#define LK_VAR16_LAST_BANK_ACCESSED			0x04
#define LK_VAR16_STATUS_REGISTER_MASK		0x05
#define LK_VAR16_STATUS_REGISTER_VALUE		0x06

#define LK_NUM_OF_VARIABLES_8BIT			18
#define LK_VAR8_CART_MODE					0x00
#define LK_VAR8_DMG_ACCESS_MODE				0x01
#define LK_VAR8_FLASH_COMMAND_SET			0x02
#define LK_VAR8_FLASH_METHOD				0x03
#define LK_VAR8_FLASH_WE_PIN				0x04
#define LK_VAR8_FLASH_PULSE_RESET			0x05
#define LK_VAR8_FLASH_COMMANDS_BANK_1		0x06
#define LK_VAR8_FLASH_SHARP_VERIFY_SR		0x07
#define LK_VAR8_DMG_READ_CS_PULSE			0x08
#define LK_VAR8_DMG_WRITE_CS_PULSE			0x09
#define LK_VAR8_FLASH_DOUBLE_DIE			0x0A
#define LK_VAR8_DMG_READ_METHOD				0x0B
#define LK_VAR8_AGB_READ_METHOD				0x0C
#define LK_VAR8_CART_POWERED				0x0D
#define LK_VAR8_PULLUPS_ENABLED				0x0E
#define LK_VAR8_AUTO_POWEROFF_ENABLED		0x0F
#define LK_VAR8_AGB_IRQ_ENABLED				0x10
#define LK_VAR8_DMG_AUDIO_ENABLED			0x11

// *** GLOBAL VARIABLES *** //
extern u32 lk_runtime;
extern u32 _lk_var32[LK_NUM_OF_VARIABLES_32BIT];
extern u16 _lk_var16[LK_NUM_OF_VARIABLES_16BIT];
extern u8 _lk_var8[LK_NUM_OF_VARIABLES_8BIT];
extern u32 _lk_flashcmd_addr[32];
extern u16 _lk_flashcmd_data[32];
extern u8 _lk_bankcmd_num;
extern u32 _lk_bankcmd_addr[3];
extern u8 _lk_bankcmd_mode[3];

extern u8 data_buffer[0x1000];
extern u16 flash_write_cycle[3][2];
extern u32 time_start;
extern bool auto_off_timer_suspended;
extern bool activity_done;
extern u32 activity_last_run;

// *** FUNCTION PROTOTYPES *** //
void lk_loop(u8 command);
void lk_conn_recv(u8* data, u16 count);
void lk_conn_send(u8* data, u16 count);
u8 lk_conn_recv_u8(void);
u16 lk_conn_recv_u16(void);
u32 lk_conn_recv_u32(void);
void lk_conn_send_u8(u8 data);
void lk_conn_send_u16(u16 data);
void lk_conn_send_u32(u32 data);
void lk_set_mode(u8 mode);
void lk_dmg_set_address(u16 address);
void lk_dmg_mbc6_mmsa_write_flash_bytes(void);
void lk_dmg_mbc7_write(u8 value);
void lk_dmg_mbc7_reinit(void);
void lk_dmg_mbc7_set_write_access(u8 enable);
void lk_dmg_mbc7_set_cmd(u16 address, u8 mode);
void lk_dmg_eeprom_write_bytes(u16 transfer_size, u16 buffer_offset, u32 sector_address);
u8 lk_dmg_cart_read_byte(u16 address);
void lk_dmg_cart_write_byte(u32 address, u16 value);
void lk_dmg_cart_read_data(void);
void lk_dmg_mbc7_read_eeprom(void);
void lk_dmg_mbc7_write_eeprom(void);
void lk_dmg_verify_data(u32 addr, u16 comp);
void lk_dmg_verify_status_register(u32 addr);
void lk_dmg_flash_write_byte(u32 address, u16 value);
void lk_dmg_flash_enable_audio(u8 enable);
void lk_dmg_change_bank(u16 index);
void lk_dmg_agb_flash_unbuffered(void);
void lk_dmg_agb_flash_buffered(u8 this_iteration, u8 num_of_iterations, u16 transfer_size, u16 buffer_offset, u32 sector_address);
void lk_dmg_flash_E201264(u16 buffer_offset);
void lk_dmg_flash_bung_16m(u16 buffer_offset);
void lk_dmg_flash_mmsa(u16 buffer_offset);
void lk_dmg_flash_mbc6(u16 buffer_offset);
void lk_dmg_flash_datel_orbitv2(void);
void lk_dmg_mmsa_flash_command(u16 addr, u8 data);
void lk_dmg_mmsa_access_mapper(void);
void lk_dmg_mmsa_access_rom(void);
void lk_dmg_mmsa_access_mbc(bool enable);
void lk_dmg_mmsa_disable_flash_write_protect(void);
void lk_dmg_mmsa_map_full(void);
void lk_dmg_mmsa_map_menu(void);

void lk_agb_save_eeprom_access(u8* buffer, u16 address, u8 eeprom_type, u8 command);
u16 lk_agb_cart_read_short(u32 address);
void lk_agb_cart_read_data(void);
void lk_agb_cart_write_short(u32 address, u16 value);
u8 lk_agb_cart_read_sram_byte(u16 address);
void lk_agb_cart_write_sram_byte(u16 address, u8 value);
void lk_agb_cart_read_sram(void);
void lk_agb_cart_write_sram(void);
void lk_agb_cart_read_eeprom(u8 eeprom_type);
void lk_agb_cart_write_eeprom(u8 eeprom_type);
void lk_agb_cart_write_flash_byte(u32 address, u16 value);
void lk_agb_cart_write_flash_program_sequence(void);
void lk_agb_cart_write_flash(u8 flash_type);
void lk_agb_cart_read_data_3d_memory(void);
void lk_agb_verify_data(u32 addr, u16 comp);
void lk_agb_verify_status_register(u32 addr);
void lk_agb_flash2advance_buffered(u16 transfer_size, u16 buffer_offset, u32 sector_address);
void lk_agb_bootup_sequence(void);
void lk_agb_flash_fujitsu_fastmode(void);
void lk_agb_flash_paged(u16 buffer_offset);
u32 lk_dmg_agb_calc_crc32(u32 length);
void lk_cart_power_on(void);
void lk_cart_power_off(void);
void lk_cart_power_off_proc(void);
void lk_agb_read_gpio_rtc(void);
void lk_agb_flash_gbamp(void);

static const u32 crc32_table[] = {
	0x00000000, 0x77073096, 0xEE0E612C, 0x990951BA, 0x076DC419, 0x706AF48F, 0xE963A535, 0x9E6495A3,
	0x0EDB8832, 0x79DCB8A4, 0xE0D5E91E, 0x97D2D988, 0x09B64C2B, 0x7EB17CBD, 0xE7B82D07, 0x90BF1D91,
	0x1DB71064, 0x6AB020F2, 0xF3B97148, 0x84BE41DE, 0x1ADAD47D, 0x6DDDE4EB, 0xF4D4B551, 0x83D385C7,
	0x136C9856, 0x646BA8C0, 0xFD62F97A, 0x8A65C9EC, 0x14015C4F, 0x63066CD9, 0xFA0F3D63, 0x8D080DF5,
	0x3B6E20C8, 0x4C69105E, 0xD56041E4, 0xA2677172, 0x3C03E4D1, 0x4B04D447, 0xD20D85FD, 0xA50AB56B,
	0x35B5A8FA, 0x42B2986C, 0xDBBBC9D6, 0xACBCF940, 0x32D86CE3, 0x45DF5C75, 0xDCD60DCF, 0xABD13D59,
	0x26D930AC, 0x51DE003A, 0xC8D75180, 0xBFD06116, 0x21B4F4B5, 0x56B3C423, 0xCFBA9599, 0xB8BDA50F,
	0x2802B89E, 0x5F058808, 0xC60CD9B2, 0xB10BE924, 0x2F6F7C87, 0x58684C11, 0xC1611DAB, 0xB6662D3D,
	0x76DC4190, 0x01DB7106, 0x98D220BC, 0xEFD5102A, 0x71B18589, 0x06B6B51F, 0x9FBFE4A5, 0xE8B8D433,
	0x7807C9A2, 0x0F00F934, 0x9609A88E, 0xE10E9818, 0x7F6A0DBB, 0x086D3D2D, 0x91646C97, 0xE6635C01,
	0x6B6B51F4, 0x1C6C6162, 0x856530D8, 0xF262004E, 0x6C0695ED, 0x1B01A57B, 0x8208F4C1, 0xF50FC457,
	0x65B0D9C6, 0x12B7E950, 0x8BBEB8EA, 0xFCB9887C, 0x62DD1DDF, 0x15DA2D49, 0x8CD37CF3, 0xFBD44C65,
	0x4DB26158, 0x3AB551CE, 0xA3BC0074, 0xD4BB30E2, 0x4ADFA541, 0x3DD895D7, 0xA4D1C46D, 0xD3D6F4FB,
	0x4369E96A, 0x346ED9FC, 0xAD678846, 0xDA60B8D0, 0x44042D73, 0x33031DE5, 0xAA0A4C5F, 0xDD0D7CC9,
	0x5005713C, 0x270241AA, 0xBE0B1010, 0xC90C2086, 0x5768B525, 0x206F85B3, 0xB966D409, 0xCE61E49F,
	0x5EDEF90E, 0x29D9C998, 0xB0D09822, 0xC7D7A8B4, 0x59B33D17, 0x2EB40D81, 0xB7BD5C3B, 0xC0BA6CAD,
	0xEDB88320, 0x9ABFB3B6, 0x03B6E20C, 0x74B1D29A, 0xEAD54739, 0x9DD277AF, 0x04DB2615, 0x73DC1683,
	0xE3630B12, 0x94643B84, 0x0D6D6A3E, 0x7A6A5AA8, 0xE40ECF0B, 0x9309FF9D, 0x0A00AE27, 0x7D079EB1,
	0xF00F9344, 0x8708A3D2, 0x1E01F268, 0x6906C2FE, 0xF762575D, 0x806567CB, 0x196C3671, 0x6E6B06E7,
	0xFED41B76, 0x89D32BE0, 0x10DA7A5A, 0x67DD4ACC, 0xF9B9DF6F, 0x8EBEEFF9, 0x17B7BE43, 0x60B08ED5,
	0xD6D6A3E8, 0xA1D1937E, 0x38D8C2C4, 0x4FDFF252, 0xD1BB67F1, 0xA6BC5767, 0x3FB506DD, 0x48B2364B,
	0xD80D2BDA, 0xAF0A1B4C, 0x36034AF6, 0x41047A60, 0xDF60EFC3, 0xA867DF55, 0x316E8EEF, 0x4669BE79,
	0xCB61B38C, 0xBC66831A, 0x256FD2A0, 0x5268E236, 0xCC0C7795, 0xBB0B4703, 0x220216B9, 0x5505262F,
	0xC5BA3BBE, 0xB2BD0B28, 0x2BB45A92, 0x5CB36A04, 0xC2D7FFA7, 0xB5D0CF31, 0x2CD99E8B, 0x5BDEAE1D,
	0x9B64C2B0, 0xEC63F226, 0x756AA39C, 0x026D930A, 0x9C0906A9, 0xEB0E363F, 0x72076785, 0x05005713,
	0x95BF4A82, 0xE2B87A14, 0x7BB12BAE, 0x0CB61B38, 0x92D28E9B, 0xE5D5BE0D, 0x7CDCEFB7, 0x0BDBDF21,
	0x86D3D2D4, 0xF1D4E242, 0x68DDB3F8, 0x1FDA836E, 0x81BE16CD, 0xF6B9265B, 0x6FB077E1, 0x18B74777,
	0x88085AE6, 0xFF0F6A70, 0x66063BCA, 0x11010B5C, 0x8F659EFF, 0xF862AE69, 0x616BFFD3, 0x166CCF45,
	0xA00AE278, 0xD70DD2EE, 0x4E048354, 0x3903B3C2, 0xA7672661, 0xD06016F7, 0x4969474D, 0x3E6E77DB,
	0xAED16A4A, 0xD9D65ADC, 0x40DF0B66, 0x37D83BF0, 0xA9BCAE53, 0xDEBB9EC5, 0x47B2CF7F, 0x30B5FFE9,
	0xBDBDF21C, 0xCABAC28A, 0x53B39330, 0x24B4A3A6, 0xBAD03605, 0xCDD70693, 0x54DE5729, 0x23D967BF,
	0xB3667A2E, 0xC4614AB8, 0x5D681B02, 0x2A6F2B94, 0xB40BBE37, 0xC30C8EA1, 0x5A05DF1B, 0x2D02EF8D
};
static const u16 agb_bootup_table[] = {
	0x479B, 0x7426, 0x11BC, 0x6D4F, 0x11BD, 0x32F1, 0x7FD9, 0x2CE7,
	0x5DA5, 0x11BD, 0x4610, 0x5DA4, 0x4E90, 0x6173, 0x2A84, 0x4E91,
	0x106A, 0x75FE, 0x29C8, 0x7839, 0x420E, 0x5D1B, 0x7838, 0x12A8,
	0x3F7D, 0x67B9, 0x26F3, 0x54EF, 0x7C23, 0x26F2, 0x6BC6, 0x4137,
	0x15AB, 0x730D, 0x6BC7, 0x3B4F, 0x5F24, 0x3DDA, 0x253F, 0x1749,
	0x3DDB, 0x70E6, 0x746C, 0x30F7, 0x531F, 0x6738, 0x531E, 0x1A51,
	0x1971, 0x5B7D, 0x4ED6, 0x1970, 0x3F27, 0x75CB, 0x3D62, 0x128C,
	0x74B8, 0x2FAD, 0x74B9, 0x64FD, 0x6C9A, 0x4F3A, 0x276D, 0x73EF,
	0x38B1, 0x4F3B, 0x571E, 0x7EA3, 0x6249, 0x3587, 0x1B7C, 0x3586,
	0x7AFB, 0x67E4, 0x5C92, 0x67E5, 0x2BCA, 0x438C, 0x2E6F, 0x587F,
	0x14B7, 0x2E6E, 0x4CB9, 0x6FA2, 0x38F0, 0x719E, 0x475A, 0x1F3C,
	0x6AD8, 0x475B, 0x5199, 0x3264, 0x7B41, 0x49EF, 0x5198, 0x1CD7
};

#define AGB_GPIO_REG_DAT 0xC4 >> 1
#define AGB_GPIO_REG_CNT 0xC6 >> 1
#define AGB_GPIO_REG_RE 0xC8 >> 1
#define AGB_GPIO_RTC_RESET 0x60
#define AGB_GPIO_RTC_WRITE_STATUS 0x62
#define AGB_GPIO_RTC_READ_STATUS 0x63
#define AGB_GPIO_RTC_WRITE_DATE 0x64
#define AGB_GPIO_RTC_READ_DATE 0x65
#define AGB_GPIO_RTC_WRITE_TIME 0x66
#define AGB_GPIO_RTC_READ_TIME 0x67
#define AGB_GPIO_RTC_WRITE_ALARM 0x68
#define AGB_GPIO_RTC_READ_ALARM 0x69

#endif
