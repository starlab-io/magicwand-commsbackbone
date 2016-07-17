// @MAGICWAND_HEADER@

/**
 * Public header for variable-sized messages.
 * All messaging operations can be implemented in terms of variable-size
 * messages.
 *
 * TODO:
 *   ___ Add read_prep or write_prep calls for IO directly to/from the vmsg.
 */

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#include "vmsg-limits.h"

/* A vmsg is an opaque type usable through the functions in this library. */
struct vmsg;


/* ########### TODO #############
 * Define how an error due to a NULL pointer will be returned to the caller.
 * Define how an error of invalid flags will be returned to the caller.
 * Define how an error of invalid size will be returned to the caller.
 * Determine whether or not there is such a thing as an uninitialized vmsg.
 *
 * Standardize the error-indicating negative return values.
 */


/* ************** vmsg life-cycle operations **************** */

/* Note about allocation and deletion of vmsg instances.
 * vmsg instances are allocated using vmsg_new* functions.  Certain of
 * these functions allocate the buffer used by the vmsg while others merely
 * wrap a caller-provided buffer.
 * All vmsg instances MUST be freed using vmsg_delete().  DO NOT use free()
 * to delete vmsg instances.
 *
 * If you prefer to provide your own malloc and free functions, replace
 * the global vmsg_malloc and vmsg_free functions prior to calling any other
 * vmsg functions.
 */

/*
 * vmsg initialization
 * Before the header or value of a vmsg can be read, it must be initialized.
 * At a minimum, initialization sets the type family of the vmsg.
 * Initialization can be done directy by calling @c vmsg_set_type_family,
 * or by writing the vmsg header from a buffer.
 * Read or write operations on an uninitialized vmsg will fail with the
 * error code E_VMSG_UNINITIALIZED.
 */

/**
 * Create a vmsg instance capable of holding @p max_size bytes.
 * The vmsg is allocated along with a congituous buffer at least
 * @p max_size bytes.
 *
 * The memory of the buffer is not cleared.
 * The vmsg returned has 0 bytes marked as used.
 * The returned vmsg instance MUST be freed with @c vmsg_delete().
 * 
 * @param max_size The maximum message size this vmsg can hold.
 * @retval New struct vmsg instance on success.
 * @retval NULL if the allocation fails.
 * 
 */
struct vmsg* vmsg_new(const size_t max_size);


/**
 * Create a vmsg instance with zero'd out buffer of @p max_size.
 * The vmsg is allocated the same as with @c vmsg_new, but the buffer is
 * set to all zeros before the vmsg is returned.
 *
 * @param max_size The maximum message size this vmsg can hold.
 * @retval New struct vmsg instance on success.
 * @return NULL if the allocation fails.
 * @retval
 */
struct vmsg* vmsg_new0(const size_t max_size);


/**
 * Create a vmsg instance by wrapping a caller-provided @p buffer.
 * The maximum size of the vmsg is set to the caller provided @p buffer_size.
 *
 * The vmsg returned has 0 bytes marked as used.
 *
 * This DOES NOT transfer ownership of the buffer to the vmsg.  The returned
 * vmsg MUST be freed with @c vmsg_delete (the same as any other vmsg), but
 * @c vmsg_delete will not free the buffer -- freeing the buffer remains the
 * caller's responsibility.
 *
 * If @p buffer is NULL, no new vmsg will be created.
 *
 * @param buffer The buffer to use with the returned vmsg instance.
 * @param buffer_size The size of @p buffer.
 * @retval New struct vmsg instance on success.
 * @retval NULL on error.
 */
struct vmsg* vmsg_wrap(const char* buffer, const size_t buffer_size);


/**
 * Create a vmsg instance by wrapping a caller-provided @p buffer.
 * The maximum size of the vmsg is set to the caller provided @p buffer_size.
 *
 * This function allows full control over the resulting configuration of
 * the vmsg.
 * When appending data to the vmsg, @p bytes_used is used as the offset for
 * the next append -- use 0 to indicate that none of the buffer has been used
 * yet.
 *
 * With @p transfer_ownership set to true, the buffer becomes owned by the
 * vmsg and will be deleted when the vmsg is deleted with @c vmsg_delete.
 * If transfering ownership of the buffer to the vmsg, it is crucial that
 * the buffer is allocated by the same allocator as the vmsg.
 *
 * @param buffer The buffer to use with the returned vmsg instance.
 * @param buffer_size The size of @p buffer.
 * @param flags vmsg header flags.  See FIXME:  Add flag definition location.
 * @param bytes_used The number of bytes in buffer that are already used.
 * @param transfer_ownership 1 to transfer ownership to the vmsg, 0 otherwise.
 * @retval New struct vmsg instance on success.
 * @retval NULL on error.
 */
struct vmsg* vmsg_wrap_full(const char* buffer, const size_t buffer_size,
		const uint32_t flags, const size_t bytes_used,
		const uint32_t transfer_ownership);


/**
 * Create a new vmsg instance as a copy of an existing vmsg instance.
 *
 * The new vmsg is created as via @c vmsg_new, with the contents of @p vmsg's
 * buffer copied into it (whether @p vmsg is contiguous or wraps a buffer).
 *
 * The returned vmsg must be deleted using @c vmsg_delete.
 *
 * @param vmsg The struct vmsg instance to copy.
 * @retval New struct vmsg instance on success.
 * @retval NULL on error.
 */
struct vmsg* vmsg_new_copy(const struct vmsg* vmsg);


/**
 * Create a new vmsg instance as a copy of a buffer.
 *
 * The new vmsg is created as via @c vmsg_new, with the contents of
 * @p buffer copied into it.  The maximum size of the returned vmsg is set
 * to @p buffer_size.
 *
 * The new vmsg's header is left uninitialized.
 *
 * @param buffer The buffer to copy when creating the new vmsg instance.
 * @param buffer_size The size of @p buffer.
 * @retval New vmsg instance on success.
 * @retval NULL on error.
 */
struct vmsg* vmsg_new_copy_buffer(const char* buffer, const size_t buffer_size);


/**
 * Delete a vmsg instance.
 *
 * If the vmsg was created with @c vmsg_new, @c vmsg_new0, or had a buffer
 * transfered to it with @c vms_wrap_full, then the underlying buffer will
 * be deleted as well.
 *
 * @param vmsg The struct vmsg instance to delete.
 */
void vmsg_delete(const struct vmsg* vmsg);



/* ************* vmsg structural operations *************** */

/**
 * Swap the underlying buffer used by the a vmsg instance that wraps a
 * pre-existing buffer.
 *
 * The vmsg max size is set to @p buffer_size and the number of bytes
 * used is set to 0.
 *
 * The previous buffer is returned.
 *
 * It is an error to call this function if the vmsg owns the buffer (the
 * @p vmsg was created with @c vmsg_new, @vmsg_new0, or the buffer ownership
 * transfered to it via * @c vmsg_wrap_full).
 * 
 * @param vmsg The vmsg to perform this operation on.
 * @param new_buffer The new buffer to wrap with @p vmsg.
 * @param buffer_size The size of @p buffer.
 * @retval The pointer to the previous buffer on success.
 * @retval NULL on error.
 */
char* vmsg_swap_wrapped_buffer(const struct vmsg* vmsg, const char* new_buffer,
		const size_t buffer_size);



/* ************* vmsg operations that are type-family specific. ************ */

/**
 * Check if the vmsg type family in the header is set to VMSG_TYPE_FAMILY_BASIC.
 * There are 4 possible type families (see vmsg-limits.h), of which, only
 * the basic type is specified and implemented currently.
 *
 * Return a negative value if there is an error.
 *
 * @param vmsg The vmsg to check the type family of.
 * @retval 1 if @p vmsg's type family is VMSG_TYPE_FAMILY_BASIC.
 * @retval 0 if @p vmsg's type family is NOT VMSG_TYPE_FAMILY_BASIC.
 * @retval E_VMSG_NULL_POINTER if @p vmsg is NULL.
 * @retval E_VMSG_UNINITIALIZED if @p vmsg is not initialized.
 */
int32_t vmsg_type_family_is_basic(const struct vmsg* vmsg);


/**
 * Get the type value of a basic vmsg.
 *
 * Returns @p vmsg type or a negative value if there is an error.
 *
 * @param vmsg The basic vmsg to get the type from.
 * @retval Type field of @p vmsg header if @p vmsg is basic.
 * @retval E_VMSG_NULL_POINTER if @p vmsg is NULL.
 * @retval E_VMSG_UNINITIALIZED if @p vmsg is uninitialized.
 * @retval E_VMSG_WRONG_TYPE_FAMILY if @p vmsg's type family is not basic.
 */
int32_t vmsg_basic_get_type(const struct vmsg* vmsg);


/**
 * Set the @p type value of a basic vmsg.
 *
 * This sets the type vamily of the vmsg to basic also.
 * The @p type value must be be between 0 and 1023.
 *
 * @param vmsg The basic vmsg to set the type of.
 * @param type The new type value for the vmsg.
 * @retval 0 on success.
 * @retval E_VMSG_NULL_POINTER of @p vmsg is NULL.
 * @retval E_VMSG_INVALID_TYPE if the @p type value is out of range.
 */
int32_t vmsg_basic_set_type(const struct vmsg* vmsg, const uint32_t type);


/**
 * Set the type and size of a basic vmsg.
 *
 * If there is an error, a negative value is returned.
 * @p vmsg must not be NULL.
 * The value of @p size must be valid for the basic type family.
 *
 * @param vmsg The vmsg to set the header (type and size) of.
 * @param type The type value to specify in @p vmsg.
 * @param size The size value to specify in @p vmsg.
 * @return 0 on sucess; < 0 on error.
 * @retval 0 on success.
 * @retval E_VMSG_NULL_POINTER if @p vmsg is NULL.
 * @retval E_VMSG_INVALID_TYPE if the @p type value is out of range.
 * @retval E_VMSG_INVALID_SIZE if the @p size is out of range.
 */
int32_t vmsg_set_header_full(const struct vmsg* vmsg, const uint32_t type,
		const size_t size);



/* ************** vmsg informational operations *************** */


/**
 * Get the size of the value in a vmsg.
 *
 * If there is an error, a negative value is returned.
 *
 * @param vmsg The vmsg to get the value size from.
 * @retval >= 0  on success -- the size of the value in the vmsg.
 * @retval E_VMSG_NULL_POINTER if @p vmsg is NULL.
 * @retval E_VMSG_WRONG_TYPE_FAMILY if the type family is not supported.
 */
ssize_t vmsg_get_size(const struct vmsg* vmsg);


/**
 * Get the number of bytes this vmsg will take "on the wire".
 *
 * A destination must have this many bytes available for @p vmsg to be written
 * to it.
 *
 * It is an error to call this function with a vmsg that is not fully
 * initialized.
 *
 * @param vmsg The vmsg instance to get the "write size" of.
 * @retval >=
 * @return The number of bytes needed to write @p vmsg; < 0 on an error.
 */
ssize_t vmsg_get_write_size(const struct vmsg* vmsg);


/**
 * Set the size of the value in a vmsg.
 *
 * If the vmsg's coarse type is not supported, -1 is returned.
 * If @p size is greater than the maximum possible size of @p vmsg (by
 * being larger than the size of the buffer in \p vmsg) then -2 is returned
 * and the size is not set.
 *
 * @param vmsg The vmsg to set the size of.
 * @param size The size value to set in @p vmsg.
 * @return 0 on success, < 0 on error.
 */
int32_t vmsg_normal_set_size(const struct vmsg* vmsg, size_t size);


/**
 * Get the maximum size value this @p vmsg can hold.
 *
 * @param vmsg The vmsg to get the maximum size from.
 * @return The maximum size value this @p vmsg can hold.
 */
size_t vmsg_get_max_size(const struct vmsg* vmsg);


/**
 * Get the amount of available space remaining in a vmsg.
 *
 * If the vmsg's coarse type is not normal, -1 is returned.
 * When the vmsg is empty, this will return the same value as
 * @c vmsg_get_max_size.
 *
 * @param vmsg The vmsg to get the available space from.
 * @return The amount of space available in this vmsg, < 0 on error.
 */
size_t vmsg_get_space_available(const struct vmsg* vmsg);



/* ************** vmsg header I/O operations ************** */


/**
 * Set the type and size of a vmsg from a @p buffer.
 *
 * @p buffer MUST be at least 4 bytes long.
 *
 * The coarse type specified in the buffer must be a supported type or -1 is
 * returned.
 *
 * The size specified in @p buffer must be within the max size of @p vmsg.
 * If @p vmsg's max size is too small for the size from @p buffer, -2 is
 * returned.
 *
 * @param vmsg The vmsg to set the header (type and size) in.
 * @param buffer The source of the type and size values.a
 * @param 0 on success; < 0 on error.
 */
int32_t vmsg_set_header_from_buffer(const struct vmsg* vmsg,
		const char* buffer);



/**
 * Write the header (type and size) from @p vmsg to @p buffer.
 *
 * @p vmsg must have type and size set prior to this call, if they are unset -1
 * will be returned.
 *
 * @p buffer MUST be at least 4 bytes long.
 *
 * @param vmsg The vmsg whose header to write to @p buffer.
 * @param buffer The buffer to write the header from @p vmsg to.
 * @return bytes written (> 0) on success; < 0 on error.
 */
int32_t vmsg_copy_header_to_buffer(const struct vmsg* vmsg,
	const char* buffer);




/* ************** vmsg I/O operations **************** */

/*
 * The first two operations deal with copying data into the vmsg:
 *  * vmsg_append() - append as much data as will fit.
 *  * vmsg_append_if_room() - append data only if it will all fit.
 *
 * The second two opoerations deal with copying data out of the vmsg:
 *  * vmsg_read() - copy out as much data as will fit in the destination.
 *  * vmsg_read_if_room() - copy out data only if it will fit.
 */


/**
 * Append up to @p num_bytes from @p source to @p vmsg.
 *
 * As many bytes from @p source will be copied to @p vmsg as it has room
 * for.  The number of bytes copied from @p source will be returned.
 *
 * @param vmsg The vmsg to append data to.
 * @param source The data to append to @p vmsg.
 * @param num_bytes The number of bytes to copy from @p source.
 * @return The number of bytes copied to @p vmsg.
 */
size_t vmsg_append(const struct vmsg* vmsg, const char* source,
	const size_t num_bytes);


/**
 * Append exactly @p num_bytes from @p source to @p vmsg.
 *
 * Ensure that @p vmsg has room for @p num_bytes and then copy exactly that
 * many bytes from @source to the vmsg.
 *
 * If the vmsg does not have room for @p num_bytes, then no bytes are copied
 * to the vsmg and -1 is returned.  On success, the number of bytes copied
 * is returned.
 *
 * After appending, the vmsg's sizie is incremented by @p num_bytes.
 *
 * @param vmsg The vmsg to append data to.
 * @param source The data to append to @p vsmsg.
 * @param num_bytes The number of bytes to copy from @source.
 * @return On success # bytes copied; -1 if @p vmsg lacks sufficient room.
 */
ssize_t vmsg_append_if_room(const struct vmsg* vmsg, const char* source,
	const size_t num_bytes);


/**
 * Copy up to @p num_bytes from @p vmsg (starting at @p offset) into @p dest.
 *
 * As many bytes as will fit (according to @p num_bytes) from the vmsg
 * (starting at @p offset) are copied into the * @p dest buffer.  The
 * number of bytes copied is returned.
 *
 * @param vmsg The vmsg to copy data from.
 * @param offset The position (in the vmsg's buffer) to start copying from.
 * @param dest The destination buffer to copy into.
 * @param num_bytes The maximum number of bytes to copy from the vmsg.
 * @return The number of bytes actually copied into @p dest.
 */
size_t vmsg_read(const struct vmsg* vmsg, const size_t offset,
	const char* dest, const size_t num_bytes);


/**
 * Copy exactly @p num_bytes from @p vmsg (starting at @p offset) into @p dest.
 *
 * If the vmsg does not have sufficient data, then no bytes are copied into
 * @p dest and -1 is returned.
 *
 * @param vmsg The vmsg to copy data from.
 * @param offset The position (in the vmsg's buffer) to start copying from.
 * @param dest The destination buffer to copy into.
 * @param num_bytes The exact number of bytes to copy from @p vmsg.
 * @return On success # of bytes copied; -1 if @p vmsg lacks sufficient data.
 */
ssize_t vmsg_read_if_room(const struct vmsg* vmsg, const size_t offset,
	const char* dest, const size_t num_bytes);



