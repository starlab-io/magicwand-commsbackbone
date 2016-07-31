// @MAGICWAND_HEADER@

/**
 * Public header for the MAGICWAND comm channel's Rendezvous Protocol.
 * Different platforms will require different implementations of this
 * protocol.  It would probably be prudent to implement client and server
 * functionality separately on each platform.  We might not need both on
 * every platform.
 */

struct wchan;

struct mwrp;

// This is the default amount of time to wait for a connection to succeed.
const uint32_t WMCHAN_CONNECT_TIMEOUT_MS = 5000;


/* Rendezvous Protocol Overview
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


/* **************** Server operations ***************** */

/**
 * Create a new Rendevous Point at the given path. 
 *
 * Creates a new Rendezvous Point at the given path.
 * If the Rendezvous Point creation succeeds, then the new mwrp instance
 * is returned.  Otherwise, @p error_code is set and @p error_message is
 * pointed to an explantory error message.  If @p error_message is not
 * NULL, the caller must free it.
 *
 * @param rendezvous_point_path Path in XenStore for the Rendezvous Point.
 * @param[out] error_code The error code associated with a failure.
 * @param[out] error_message Explanatatory detail about a failure.
 * @return The new mwrp instance on success; NULL on failure.
 */
struct mwrp* mwrp_server_create_rp(const char* rendevous_point_path,
		int32_t* error_code, char* error_message);

/**
 * Start monitoring a Rendezvous Point for requests.
 *
 * NOTE: This calls @c xs_watch on the underlying xenstore connection.
 *
 * @param mwrp The mwrp instance to monitor for requests.
 * @return 0 on success, < 0 on error.
 */
int32_t mwrp_server_start_rp(const struct mwrp* mwrp);


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
 * @return >= 0 on success, < 0 on error.
 */
int mwrp_server_get_fileno(const struct mwrp* mwrp);


/**
 * Block until a request is received.
 *
 * Return the request token string for the request, or NULL on an error.
 *
 * @param mwrp The mwrp to wait for a request on.
 * @return The request token string, or NULL on error.
 */
char* mwrp_server_wait_for_request(const struct mwrp* mwrp);


/**
 * Nonblocking check to see if a request has been received.
 *
 * Return indicates whether or not a request has been received:
 *   > 0   -- Request has been received and is available.
 *   == 0  -- No request has been received.
 *   < 0   -- An error occured.
 *
 * When this function returns a positive value, a call to
 * @c mwrp_server_wait_for_request will not block.
 *
 * @param mwrp The mwrp instance to check for a request.
 * @return >0 if a request was received, 0 if not, or < 0 if an error occurred.
 */
int32_t mwrp_server_get_request_if_available(const struct mwrp* mwrp);


/**
 * Create the mwchan instance from a request.
 *
 * This handles the creation of the mwchan and the notification of the
 * client (via the Rendezvous Protocol) of the new mchan instance's
 * particulars.
 *
 * @param mwrp The mwrp instance that received the request.
 * @param request_token The string uniquely identifying the request.
 * @return The new mwchan instance.
 */
struct mwchan* mwrp_server_handle_request(const struct mwrp* mwrp,
		const char* request_token);


/* ************* Client operations to request a wmchan ************* */


/**
 * Request a connection to the service specified by a rendezvous point.
 *
 * Attempt to connect to the service listening at @p rendezvous_point.
 * Request that the wmchan be crated with ring-buffers at least as large
 * as the specified buffer sizes.
 *
 * Block for up to @p timeout_ms miliseconds to connect before failing if the
 * wmchan is not established:
 *   > 0   -- Wait for up to this many miliseconds
 *   == 0  -- Wait for up to @c WMCHAN_CONNECT_TIMEOUT_MS miliseconds
 *   < 0   -- Wait indefinitely
 * FIXME:  Should we support indefinite wait on connect?
 *
 * If the connection request fails, @p error_code will be set to the
 * corresponding error code value and an error message will be allocated.
 * The caller is responsible for freeing @p error_message if it is not NULL.
 *
 * @param rendezvous_point The path in XenXtore for the specified service.
 * @param to_server_buffer_size Minimum size of the to-server ring-buffer.
 * @param from_server_buffer_size Minimum size of the from-server ring-buffer.
 * @param timeout_ms The number of miliseconds to block waiting to connect.
 * @param[out] error_code Set to the corresponding error code on failure.
 * @param[out] error_message The error message with details about the failure.
 * @reutrn wmchan The new wmchan instance for the client, or NULL on failure.
 */
struct wmchan* wmchan_rp_client_connect_full(const char* rendezvous_point,
		const size_t to_server_buffer_size,
		const size_t from_server_buffer_size,
		const size_t timeout_ms,
		int32_t* error_code,
		char* error_message);

