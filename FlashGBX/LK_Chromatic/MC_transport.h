//
// Created by fred on 8/27/2026.
//

#ifndef FLASHGBX_NATIVE_MC_IO_H
#define FLASHGBX_NATIVE_MC_IO_H

#include <inttypes.h>
#include <stddef.h>

/***** MUST BE IMPLEMENTED BY TRANSPORT *****/

void lk_recv_from_host(uint8_t* data, uint16_t count);
void lk_send_to_host(const uint8_t* data, uint16_t count);

struct mc_transport_progress {
  size_t completed_this_transaction {};
  size_t completed_cumulative {};

  // backend-specific; only requirement is that '0' is success
  int error {};
};

struct mc_transport_callbacks {
  void* user_data {};
  void (*on_tx_progress)(void* user_data, const mc_transport_progress*) {};
  void (*on_rx_progress)(void* user_data, const mc_transport_progress*) {};
};

void mc_transport_set_callbacks(const mc_transport_callbacks*);
void mc_transport_flush();

/* returns previous cumulative count */
[[nodiscard]]
size_t mc_transport_enqueue_tx(const uint8_t* data, size_t count);
/* returns previous cumulative count */
[[nodiscard]]
size_t mc_transport_enqueue_rx(uint8_t* data, size_t count);

/* message will have a null terminator, but length *does not* include the
 * null terminator */
void mc_on_error(const char* message, size_t length);
void mc_on_debug_message(const char* message, size_t length);

/***** MUST BE CALLED BY TRANSPORT *****/

/* Pass first byte to this; additional bytes will be fetched by a call to
 * `lk_recv_from_host()` */
void mc_exec(uint8_t command);

/* call from your `open()`-like function once opened */
void mc_init();
/* call from your `close()`-like function */
void mc_reset();

/***** MAY BE CALLED BY TRANSPORT *****/

/* Execute a PING outside of an async batch.
 *
 * You might want to do this in your 'open()' function as a test.
 *
 * @returns bitwise negation of input
 */
[[nodiscard]]
uint8_t mc_standalone_ping(uint8_t);

#endif // FLASHGBX_NATIVE_MC_IO_H
