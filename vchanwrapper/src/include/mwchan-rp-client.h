// @MAGICWAND_HEADER@

/**
 * Client side of the MAGICWAND comm channel's Rendezvous Protocol.
 *
 * The protocol is described in detail here:
 *   https://github.com/invincealabs/magicwand/wiki/Comms-Channel---Rendezvous-Protocol
 *
 * Definitions:
 *   * 'server' -  a component that is contacted by another components
 *   * 'client' - a component that initiates a connection
 *   * 'rendezvous point' a well-known path in XenStore assigned to a server
 *   * 'request_token' - A random value unique for each request
 *   * 'wmchan' The struct wmchan instance 
 */

#include <stdint.h>

// The error codes returned come from here.
#include "mwchan-error-codes.h"

extern struct wchan;

/**
 * Request a connection to the service specified by a Rendezvous Point.
 *
 * Attempt to connect to the service listening at @p rendezvous_point.
 * Request that the wmchan be crated with ring-buffers at least as large
 * as the specified buffer sizes.
 *
 * If the connection request fails, @p error_code will be set to the
 * corresponding error code value.
 *
 * @param rendezvous_point The path in XenXtore for the specified service.
 * @param to_server_buffer_size Minimum size of the to-server ring-buffer.
 * @param from_server_buffer_size Minimum size of the from-server ring-buffer.
 * @param[out] error_code Set to the corresponding error code on failure.
 * @return wmchan The new wmchan instance for the client, or NULL on failure.
 * @retval error_code: E_MWRP_NULL_POINTER @p rendezvous_point is NULL.
 * @retval error_code: E_MWRP_BAD_RENDEZVOUS_POINT @p rendezvous_point invalid.
 * @retval error_code: E_MWRP_XENSTORE_CONNECT_FAILED unable to connect to XenStore.
 * @retval error_code: E_MWRP_XENSTORE_ACCESS_DENIED unable to read connection info from XenStore.
 * @retval error_code: E_MWRP_XENSTORE_READ_FAILED unable to read from XenStore.
 * @retval error_code: E_MWRP_VCHAN_CREATE_FAILED unable to create underlying vchan.
 * @retval error_code: E_MWRP_ALLOCATION_FAILED unable to allocate memory.
 * @retval error_code: E_MWRP_BUFFER_TOO_LARGE a requested buffer size is too large.
507 0909
 */
struct wmchan* wmchan_rp_client_connect_full(const char* rendezvous_point,
		const size_t to_server_buffer_size,
		const size_t from_server_buffer_size,
		enum mwchan_error_code* error_code);

