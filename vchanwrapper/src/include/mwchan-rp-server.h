// @MAGICWAND_HEADER@

/**
 * Server side of the MAGICWAND comm channel's Rendezvous Protocol.
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
 * 
 * In this implementation of the Rendezvous Protocol, the server acts
 * as the Rendezvous Point Creator as well as the channel creator
 */

#include <stdint.h>

// The error_codes returned come from here.
#include "mwchan-error-codes.h"

/**
 * A handle to a Rendezvous Point.
 */
struct mwrp;

struct wchan;


/**
 * Create a new Rendevous Point at the given path. 

 * If the Rendezvous Point creation succeeds, then the new mwrp instance
 * is returned.  Otherwise, @p error_code is set and @p error_message is
 * pointed to an explantory error message.  If @p error_message is not
 * NULL, the caller must free it.
 *
 * This call will open a connection to the XenStore database.
 *
 * If @p error_code or @p error_message are NULL, no error information will
 * be available to the caller.
 *
 * @param rendezvous_point_path Path in XenStore for the Rendezvous Point.
 * @param[out] error_code The error code associated with a failure.
 * @param[out] error_message Explanatatory detail about a failure.
 * @retval The new mwrp instance on success.
 * @retval NULL on failure.
 * @retval error_code: E_MWRP_NULL_POINTER if @p rendezvous_point_path is NULL.
 * @retval error_code: E_MWRP_XENSTORE_ACCESS_DENIED Access denied to XenStore.
 * @retval error_code: E_MWRP_XENSTORE_CONNECT_FAILED Unable to connect to XenStore.
 * @retval error_code: E_MWRP_XENSTORE_WRITE_FAILED Write to XenStore failed.
 * @retval error_code: E_MWRP_ALLOCATION_FAILED Memory allocation failure.
 */
struct mwrp* mwrp_server_create_rp(const char* rendevous_point_path,
		enum mwchan_error_code* error_code, char* error_message);

/**
 * Start monitoring a Rendezvous Point for requests.
 *
 * NOTE: This calls @c xs_watch on the underlying xenstore connection.
 *
 * @param mwrp The mwrp instance to monitor for requests.
 * @retval 0 on success: E_SUCCESS.
 * @retval E_MWRP_NULL_POINTER @p mwrp is NULL.
 * @retval E_MWRP_XENSTORE_WATCH_FAILED Adding a WATCH to the path in XenStore failed.
 * @retval E_MWRP_XENSTORE_ACCESS_DENIED Access denied to path in XenStore.
 * @retval E_MWRP_XENSTORE_CLOSED XenStore connection closed.
 * @return 0 on success, < 0 on error.
 */
enum mwchan_error_code mwrp_server_start_rp(const struct mwrp* mwrp);


/* The mwrp struct contains an xs_handle struct which contains an open
 * xenstore daemon connection.  Within that handle is a file descriptor
 * that can be checked for read availability (via select/poll after
 * first calling xs_watch() on the Rendezvous Point path.  We have to
 * be able to let the caller have access to that file descriptor so that
 * the caller can incpororate us into their event loop.
 *
 */

/**
 * Get the file descriptor from the mwrp instance.
 *
 * The caller can add the mwrp instance's file descriptor to its own
 * event loop.  In such a scenario, when the file descripter is readable,
 * the caller should call @c mwrp_server_wait_for_request to get the
 * request token associated with the request.
 *
 * @param mwrp The mwrp instance to get the file descriptor from.
 * @retval >= 0 file descriptor on success.
 * @retval E_MWRP_NULL_POINTER @p mwrp is NULL.
 * @retval E_MWRP_XENSTORE_CLOSED XenStore connection closed.
 */
uint32_t mwrp_server_get_fileno(const struct mwrp* mwrp);


/**
 * Block until a request is received.
 *
 * Return the request token string for the request, or NULL on an error.
 * If there is an error, and @p error_code is not NULL, @p error_code will
 * be set to indicate the reason for the error.
 *
 * The Rendezvous Point must have been "started" prior to this call by
 * calling @c mwrp_server_start_rp.
 *
 * @param mwrp The mwrp to wait for a request on.
 * @param[out] error_code Set to the error reason code on error.
 * @return The request token string on success; NULL on error.
 * @retval error_code: E_MWRP_NULL_POINTER if @p rendezvous_point_path is NULL.
 * @retval error_code: E_MWRP_NOT_STARTED if @p mwrp not started yet.
 */
char* mwrp_server_wait_for_request(const struct mwrp* mwrp,
		enum mwchan_error_code* error_code);


/**
 * Non-blocking get of a request if one has been received.
 * Return the request token string for the request, or NULL if there was
 * an error or if no request has been received.
 *
 * The Rendezvous Point must have been "started" prior to this call by
 * calling @c mwrp_server_start_rp.
 *
 * @param mwrp The mwrp instance to get a request from if one is available.
 * @param[out] error_code Set to the error reason code on error.
 * @return The request token string on success; NULL on error or no request.
 * @retval error_code: E_MWRP_NULL_POINTER if @p rendezvous_point_path is NULL.
 * @retval error_code: E_MWRP_NOT_STARTED if @p mwrp not started yet.
 */
char* mwrp_server_get_request(const struct mwrp* mwrp,
		enum mwchan_error_code* error_code);


/**
 * Create the server-side mwchan instance from a request.
 *
 * This handles the creation of the mwchan and the notification of the
 * client (via the Rendezvous Protocol) of the new mchan instance's
 * particulars.
 *
 * @param mwrp The mwrp instance that received the request.
 * @param request_token The string uniquely identifying the request.
 * @param[out] error_code Set to the error reason code on error.
 * @return The new mwchan instance, or NULL on error.
 * @retval error_code: E_MWRP_NULL_POINTER if @p mwrp or @p request_token are NULL.
 * @retval error_code: E_MWRP_XENSTORE_WRITE_FAILED if unable to store info for client in XenStore.
 * @retval error_code: E_MWRP_VCHAN_CREATE_FAILED if unable to create the underlying vchan.
 */
struct mwchan* mwrp_server_handle_request(const struct mwrp* mwrp,
		const char* request_token, enum mwchan_error_code* error_code);



