//
// Created by fred on 8/27/2026.
//

#ifndef FLASHGBX_NATIVE_MC_IO_H
#define FLASHGBX_NATIVE_MC_IO_H

#ifdef __cplusplus
#include <cinttypes>
#include <cstddef>
#else
#include <inttypes.h>
#include <stddef.h>
#endif

/***** MUST BE IMPLEMENTED BY TRANSPORT *****/

void lk_recv_from_host(uint8_t* data, uint16_t count);
void lk_send_to_host(const uint8_t* data, uint16_t count);

/* tx all, then rx all */
void mc_exec_batch(
  const uint8_t* txData,
  size_t txCount,
  uint8_t* rxData,
  size_t rxSize);

/* message will have a null terminator, but length *does not* include the
 * null terminator */
void mc_on_error(const char* message, std::size_t length);
void mc_on_debug_message(const char* message, std::size_t length);

/***** MUST BE CALLED BY TRANSPORT *****/

/* You should begin an async batch before calling `lk_loop()`, and end it
 * immediately after */
void mc_begin_async_batch();
void mc_end_async_batch();

/* Pass first byte to this; additional bytes will be fetched by a call to
 * `lk_recv_from_host()` */
void lk_loop(uint8_t command);

#endif // FLASHGBX_NATIVE_MC_IO_H
