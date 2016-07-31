#ifndef __VMSG_H__
#define __VMSG_H__
// @MAGICWAND_HEADER@

/**
 * Public header for variable-sized TLV (Type, Length, Value) messages.
 *
 * The Type and Length portion of a message form the notional "header" of
 * the message.
 *
 * The vmsg format reserves the first two bits of the Type field to specify
 * the "type-family" that a vmsg instance belongs to.  The intepretation
 * and validation of the message header is type-family specific.
 *
 * At present, only a single type-family has been defined -- the "basic"
 * type-family.
 *
 * TODO List
 * ___ Are arbitary write ops needed in addtion to append to vmsg?
 *     Do vmsg users need to be able to write at arbitrary locations in
 *     the vmsg?
 *
 * ___ Should there be a function to get the space limit for a vmsg?
 *     The space limit is the smaller of the vmsg's type-family's max size
 *     and the maximum size for the vmsg itself.
 */

#include <stdlib.h>
#include <stdint.h>

#include "mwchan-error-codes.h"
#include "basic-type-family.h"

/* A vmsg is an opaque type usable through the functions in this library. */
struct vmsg;

// These are the only valid vmsg types.
typedef enum vmsg_type_family {
	// Not a real type -- just compiler tricks.
	// This ensure that the compiler will use a signed int for our enum.
	// A signed int is necessary so enum mwchan_error_code values can
	// "sort of" occupy the same data space as the type family values.
	VTF_INVALID_ERROR = -1,

	//! The basic vmsg type-family.
	VTF_BASIC = 0,   // This is the only implemented type family.

	//! Unimplemented vmsg type-family.
	VTF_UNUSED_1 = 1,

	//! Unimplemented vmsg type-family.
	VTF_UNUSED_2 = 2,

	//! Unimplemented vmsg type-family.
	VTF_UNUSED_3 = 3,

	// Any value >= VTF_MAX is an error.
	VTF_MAX
} vmsg_type_family_t;



/* ************** vmsg life-cycle operations **************** */

/* Note about allocation and deletion of vmsg instances.
 * vmsg instances are allocated using vmsg_new* functions.  Certain of
 * these functions allocate the buffer used by the vmsg while others merely
 * wrap a caller-provided buffer.
 * All vmsg instances MUST be freed using vmsg_delete().  DO NOT use free()
 * to delete vmsg instances.
 */


/*
 * vmsg initialization
 * Before a vmsg can be used, it must be initialized.
 * Initialization ensures that the vmsg has its type-family set so that
 * operations that set the message type or size can be validated.
 * Only a select few operations may be performed on an uninitialized vmsg:
 *   * @c vmsg_set_type_family
 *   * @c vmsg_delete
 *   * @c vmsg_wrap_full
 *   * @c vmsg_set_header_from_buffer
 *   * @c vmsg_get_max_size
 *
 * An initialized vmsg may be obtained by calling the appropriate
 * @c vmsg_new* or @c vmsg_wrap* function.
 *
 * Operations on uninitialized vmsgs will either return null or, if
 * supported by the operation, return the mwchan_error_code
 * E_VMSG_UNINITIALIZED.
 */


/**
 * Create a vmsg instance capable of holding @p max_size bytes.
 *
 * The vmsg is allocated contiguously with a buffer of at least
 * @p max_size bytes.
 *
 * The contents of the buffer are not cleared.
 * The vmsg returned has 0 bytes marked as used.
 *
 * The returned vmsg instance MUST be freed with @c vmsg_delete.
 *
 * NOTE:  The vmsg returned is uninitialized until the type is set.
 * 
 * @param max_size The maximum message size this vmsg can hold.
 * @retval New struct vmsg instance on success.
 * @retval NULL if the allocation fails.
 */
struct vmsg* vmsg_new(const size_t max_size);


/**
 * Create a vmsg instance with zero'd out buffer of @p max_size.
 *
 * The vmsg is allocated contiguously with a buffer of at least
 * @p max_size bytes.  The buffer is set to all zeros before the vmsg
 * is returned.
 *
 * NOTE: The vmsg returned is uninitialized.
 *
 * The returned vmsg must be freed with @c vmsg_delete.
 *
 * @param max_size The maximum message size this vmsg can hold.
 * @retval New struct vmsg instance on success.
 * @return NULL if the allocation fails.
 */
struct vmsg* vmsg_new0(const size_t max_size);


/**
 * Create an intialized vmsg instance capable of holding @p max_size bytes.
 *
 * The vmsg is allocated contiguously with a buffer of at least
 * @p max_size bytes.
 *
 * The returned vmsg is fully initialized.
 *
 * If there is an error creating the vmsg, NULL is returned and
 * @p error_code is set to indicate the reason for the error.
 *
 * The returned vmsg instance MUST be freed with @c vmsg_delete.
 *
 * @param max_size The maximum size for the vmsg.
 * @param type_family The type-family for the vmsg.
 * @param type The type (within the type-family @p type_family) for the vmsg.
 * @param bytes_used The number of bytes in buffer that are already used.
 * @param transfer_ownership non-zero to transfer ownership of @p buffer to the vmsg.
 * @param[out] error_code Error code indicating reason for failure.
 * @retval New struct vmsg instance on success.
 * @retval NULL on error.
 * @retval error_code: E_VMSG_WRONG_TYPE_FAMILY if @p type_family is not valid.
 * @retval error_code: E_VMSG_INVALID_SIZE_FOR_TYPE if @p bytes_used is not in the valid range for @p type_family.
 */

struct vmsg* vmsg_new_full(const size_t max_size,
		const enum vmsg_type_family type_family,
		const uint32_t type, const size_t bytes_used,
		enum mwchan_error_code* error_code);


/**
 * Create a vmsg instance by wrapping a caller-provided @p buffer.
 *
 * The maximum size of the vmsg is set to @p buffer_size.
 *
 * The vmsg returned has 0 bytes marked as used.
 *
 * This DOES NOT transfer ownership of the buffer to the vmsg.  The returned
 * vmsg MUST be freed with @c vmsg_delete (the same as any other vmsg), but
 * @c vmsg_delete will not free the buffer -- freeing the buffer remains the
 * caller's responsibility.
 *
 * NOTE: The vmsg returned is uninitialized.
 *
 * If @p buffer is NULL, no new vmsg will be created and NULL will be returned.
 *
 * @param buffer The buffer to use with the returned vmsg instance.
 * @param buffer_size The size of @p buffer.
 * @retval New struct vmsg instance on success.
 * @retval NULL on error.
 */
struct vmsg* vmsg_wrap(const char* buffer, const size_t buffer_size);


/**
 * Create an initialized vmsg instance by wrapping a caller-provided @p buffer.
 *
 * This function allows full control over the resulting configuration of
 * the vmsg.
 *
 * The maximum size of the vmsg is set to @p buffer_size.
 * When appending data to the vmsg, @p bytes_used is used as the offset for
 * the next append -- use 0 to indicate that none of the buffer has been used
 * yet.
 *
 * With @p transfer_ownership set to true, the buffer becomes owned by the
 * vmsg and will be deleted when the vmsg is deleted with @c vmsg_delete.
 * If transfering ownership of the buffer to the vmsg, it is crucial that
 * the buffer is allocated by the same allocator as the vmsg.
 *
 * NOTE: The vmsg returned IS initialized.
 *
 * If there is an error creating the vmsg, NULL is returned and @p error_code
 * is set to indicate the reason for the failure.
 *
 * @param buffer The buffer to use with the returned vmsg instance.
 * @param buffer_size The size of @p buffer.
 * @param type_family The type-family for the vmsg.
 * @param type The type (within the type-family @p type_family) for the vmsg.
 * @param bytes_used The number of bytes in buffer that are already used.
 * @param transfer_ownership non-zero to transfer ownership of @p buffer to the vmsg.
 * @param[out] error_code Error code indicating reason for failure.
 * @retval New struct vmsg instance on success.
 * @retval NULL on error.
 * @retval error_code: E_VMSG_NULL_POINTER if @p buffer is NULL.
 * @retval error_code: E_VMSG_WRONG_TYPE_FAMILY if @p type_family is not valid.
 * @retval error_code: E_VMSG_INVALID_SIZE_FOR_TYPE if @p bytes_used is not in the valid range for @p type_family.
 */
struct vmsg* vmsg_wrap_full(const char* buffer, const size_t buffer_size,
		const enum vmsg_type_family type_family,
		const uint32_t type, const size_t bytes_used,
		const uint32_t transfer_ownership,
		enum mwchan_error_code* error_code);


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


/* ************** vmsg type-family specific functions **************** */
/* Each vmsg type-family needs the following type-family-specific
 * functions:
 *    * vmsg_set_type_<type-family name>
 *    * vmsg_get_type_<type-family name>
 *    * vmsg_is_type_<type-family name>
 *
 * Right now, only the "basic" type-family has been defined.
 */



/**
 * Get the type of a "basic" type-family vmsg.
 *
 * @param vmsg The vmsg instance to get the "basic" type value from.
 * @retval The basic type value on success.
 * @retval E_VMSG_NULL_POINTER if @p vmsg is NULL.
 * @retval E_VMSG_WRONG_TYPE_FAMILY if @p vmsg is not a "basic type vmsg.
 * @retval E_VMSG_UNINITIALIZED if @p vmsg has not had its type-family set.
 */
enum vmsg_basic_type vmsg_get_type_basic(const struct vmsg* vmsg);


/**
 * Set the type of a "basic" type-family vmsg.
 *
 * If the size of @p vmsg has already been set, @p vmsg will be initialized
 * after this call, otherwise, it will remain uninitialized.
 *
 * @param vmsg The vmsg instance to set the type of.
 * @param type The "basic" message type to set @p vmsg to.
 * @retval 0 on success.
 * @retval E_VMSG_NULL_POINTER if @p vmsg is NULL.
 * @retval E_VMSG_INVALID_TYPE if @p type is not a valid "basic" type.
 */
enum mwchan_error_code vmsg_set_type_basic(const struct vmsg* vmsg,
		const enum vmsg_basic_type type);


/**
 * Check whether a vmsg is a "basic" message type.
 *
 * @param vmsg The vmsg to check the type-family of.
 * @retval 1 if the vmsg type-family is "basic".
 * @retval 0 if the vmsg type-family is not "basic".
 * @retval E_VMSG_NULL_POINTER if @p vmsg is NULL.
 * @retval E_VMSG_UNINITIALIZED if @p vmsg is not initialized.
 */
int32_t vmsg_type_is_basic(const struct vmsg* vmsg);


/**
 * Set the type and size of a basic vmsg.
 *
 * If there is an error, a negative value is returned.
 * @p vmsg must not be NULL.
 *
 * The values of @p size and @p type must be valid for the basic type-family.
 *
 * @param vmsg The vmsg to set the header (type and size) of.
 * @param type The type value to specify in @p vmsg.
 * @param size The size value to specify in @p vmsg.
 * @retval 0 on success.
 * @retval E_VMSG_NULL_POINTER if @p vmsg is NULL.
 * @retval E_VMSG_INVALID_TYPE if the @p type value is out of range.
 * @retval E_VMSG_INVALID_SIZE_FOR_TYPE if the @p size is out of range.
 */
enum mwchan_error_code vmsg_set_header_basic(const struct vmsg* vmsg,
		const uint32_t type, const size_t size);


/* ************** vmsg informational operations *************** */

/**
 * Get the type-family of the vmsg.
 *
 * The return value is the type-family, or negative in the event of an error.
 *
 * @param vmsg The vmsg to get the type-family from.
 * @retval vmsg_type_family value if @p vmsg is not NULL and is initialized.
 * @retval E_VMSG_NULL_POINTER if @p vmsg is NULL.
 * @retval E_VMSG_UNINITIALIZED if @p vmsg is not initialized.
 */
enum vmsg_type_family vmsg_get_type_family(const struct vmsg* vmsg);


/**
 * Get the size of the value in a vmsg.
 *
 * If there is an error, a negative value is returned.
 *
 * @param vmsg The vmsg to get the value size from.
 * @retval >= 0  on success -- the size of the value in the vmsg.
 * @retval E_VMSG_NULL_POINTER if @p vmsg is NULL.
 * @retval E_VMSG_WRONG_TYPE_FAMILY if the type family is not supported.
 * @retval E_VMSG_UNINITIALIZED if @p vmsg is not initialized.
 */
ssize_t vmsg_get_size(const struct vmsg* vmsg);


/**
 * Set the size of the value in a vmsg.
 *
 * Returns 0 on success, a negative value on error.
 *
 * The type-family for @p vmsg must already have been set or an error is
 * returned.
 *
 * If @p size is not valid for @p vmsg's type-family, or is too large for
 * @p vmsg to hold, an error is returned.
 *
 * @param vmsg The vmsg to set the size of.
 * @param size The size value to set in @p vmsg.
 * @retval 0 on success.
 * @retval E_VMSG_NULL_POINTER if @p vmsg is NULL.
 * @retval E_VMSG_UNINITIALIZED if @p vmsg does not have its type-family set.
 * @retval E_VMSG_INVALID_SIZE_FOR_TYPE if @p size is out of range for @p vmsg's type-family.
 * @retval E_VMSG_SIZE_TOO_LARGE if @p size is too large for @p vmsg max size.
 */
enum mwchan_error_code vmsg_set_size(const struct vmsg* vmsg,
		const size_t size);


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
 * @retval >= 0 on success -- the number of bytes needed to write @p vmsg.
 * @retval E_VMSG_NULL_POINTER if @p vmsg is NULL.
 * @retval E_VMSG_UNINITIALIZED if @p vmsg is not initialized.
 */
ssize_t vmsg_get_write_size(const struct vmsg* vmsg);


/**
 * Get the maximum size value this @p vmsg can hold.
 *
 * @param vmsg The vmsg to get the maximum size from.
 * @retval >0 The maximum size value for @p vmsg.
 * @retval E_VMSG_NULL_POINTER if @p vmsg is NULL.
 */
ssize_t vmsg_get_max_size(const struct vmsg* vmsg);


/**
 * Get the amount of available space remaining in a vmsg.
 *
 * The amount of space available is @p vmsg's space limit minus
 * the number of bytes already used in @p vmsg.
 * @p vmsg's space limit is the smller of:
 *   * the maximum value size for @p vmsg's type-family
 *   * the maximum size for @p vmsg.
 *
 * It is an error to call this with a vmsg that is not fully initiallized.
 *
 * @param vmsg The vmsg to get the available space from.
 * @retval >0 on success
 * @retval E_VMSG_NULL_POINTER if @p vmsg is NULL.
 * @retval E_VMSG_UNINITIALIZED if @p vmsg is not initialized.
 */
ssize_t vmsg_get_space_available(const struct vmsg* vmsg);



/* ************** vmsg header I/O operations ************** */


/**
 * Set the type and size of a vmsg from @p buffer.
 *
 * @p buffer must contain at least a vmsg header (4 bytes).
 *
 * The type-family specified in the buffer must be a supported type-family.
 * Currently, only the "basic" type-family is supported.
 *
 * The value size in @p buffer must be smaller than the space limit of
 * @p vmsg.  @p vms's space limit is the smaller of:
 *   * the maximum value size for the type-family from @p buffer
 *   * the maximum size for @p vmsg.
 *
 * If there are no errors, this initializes @p vmsg and sets the number of
 * bytes used in @p vmsg to 0.
 *
 * @param vmsg The vmsg to set the header (type and size) in.
 * @param buffer The source of the type and size values.
 * @retval 0 on success.
 * @retval E_VMSG_NULL_POINTER @p vmsg or @p buffer are NULL.
 * @retval E_VMSG_WRONG_TYPE_FAMILY Unsupported type-family in @p buffer.
 * @retval E_VMSG_INVALID_SIZE_FOR_TYPE size from @p buffer is too large for type family.
 * @retval E_VMSG_SIZE_TOO_LARGE size from @p buffer is larger than max size for  @p vmsg.
 */
enum mwchan_error_code vmsg_set_header_from_buffer(const struct vmsg* vmsg,
		const char* buffer);


/**
 * Write the header (type and size) from @p vmsg to @p buffer.
 *
 * @p vmsg must be initialized prior to this call.
 * @p buffer must be at least 4 bytes long.
 *
 * @param vmsg The vmsg whose header to write to @p buffer.
 * @param buffer The buffer to write the header from @p vmsg to.
 * @retval 0 On success.
 * @retval E_VMSG_NULL_POINTER if @p vmsg or @p buffer are NULL.
 * @retval E_VMSG_UNINITIALIZED if @p vmsg is not initialized.
 * @retval E_VMSG_WRONG_TYPE_FAMILY Unsupported type-family.
 */
enum mwchan_error_code vmsg_copy_header_to_buffer(const struct vmsg* vmsg,
	const char* buffer);


/* ************** vmsg I/O operations **************** */

/*
 * The first two operations deal with copying data into the vmsg:
 *  * vmsg_append() - append as much data as will fit.
 *  * vmsg_append_if_room() - append data only if it will all fit.
 *
 * The second two opoerations deal with copying data out of the vmsg:
 *  * vmsg_read() - copy out as much data as will fit in the destination.
 *  * vmsg_read_if_room() - copy out data only if it fits in the destination.
 */


/**
 * Append up to @p num_bytes from @p source to @p vmsg.
 *
 * @p vmsg must be initialized prior to this call.
 *
 * The number of bytes appended to @p vmsg is returned on success.
 * On error, a negative error code is returned.
 *
 * After appending, @p vmsg's bytes used is incremented by @p num_bytes.
 *
 * @param vmsg The vmsg to append data to.
 * @param source The data to append to @p vmsg.
 * @param num_bytes The number of bytes to copy from @p source.
 * @retval >0 The number of bytes copied from @p source to @p vmsg.
 * @retval E_VMSG_NULL_POINTER if @p vmsg or @p source are NULL.
 * @retval E_VMSG_UNINITIALIZED if @p vmsg is not initialized.
 */
ssize_t vmsg_append(const struct vmsg* vmsg, const char* source,
	const size_t num_bytes);


/**
 * Append exactly @p num_bytes from @p source to @p vmsg if it has room.
 *
 * Copy either 0 or @p num_bytes bytes from @p source to @p vmsg.
 * Only copy if @p vmsg has room for at least @p num_bytes.
 *
 * @p vmsg must be intialized prior to this call.
 *
 * After appending, @p vmsg's bytes used is incremented by @p num_bytes.
 *
 * @param vmsg The vmsg to append data to.
 * @param source The data to append to @p vsmsg.
 * @param num_bytes The number of bytes to copy from @source.
 * @retval > 0 The number of bytes copied.
 * @retval E_VMSG_NULL_POINTER if @p vmsg or @p source are NULL.
 * @retval E_VMSG_UNINITIALIZED if @p vmsg is not initialized.
 * @retval E_VMSG_INVALID_SIZE_FOR_TYPE if @p num_bytes would exceed @p vmsg's type-family specific size limit.
 * @retval E_VMSG_SIZE_TOO_LARGE if @p num_bytes would exceed @p vmsg's max size.
 */
ssize_t vmsg_append_if_room(const struct vmsg* vmsg, const char* source,
	const size_t num_bytes);


/**
 * Copy up to @p num_bytes from @p vmsg (starting at @p offset) into @p dest.
 *
 * Copy the smaller of (@p vmsg size - @p offset) and @p num_bytes to the
 * @p dest buffer.  The number of bytes copied is returned.
 *
 * @p vmsg must be initialized prior to this call.
 *
 * @param vmsg The vmsg to copy data from.
 * @param offset The position (in @p vmsg's value) to start copying from.
 * @param dest The destination buffer to copy into.
 * @param num_bytes The maximum number of bytes to copy from @p vmsg.
 * @retval 0 No data copied, either no data left in @p vmsg or @p num_bytes==0.
 * @retval >0 The number of bytes copied.
 * @retval E_VMSG_NULL_POINTER @p vmsg or @p dest are NULL.
 * @retval E_VMSG_UNINITIALIZED @p vmsg is not initialized.
 */
ssize_t vmsg_read(const struct vmsg* vmsg, const size_t offset,
	const char* dest, const size_t num_bytes);


/**
 * Copy data from @p vmsg to @p dest if and only if @p dest has room.
 *
 * Copy the smaller of @p vmsg bytes remaining after @p offset and @p num_bytes
 * to @p dest, but only if @p dest has sufficient room.
 *
 * If @p dest does not have sufficient room, no bytes are copied and an error
 * is returned.  Otherwise, the number of bytes copied from @p vmsg to
 * @p dest is returned.
 *
 * @p vmsg must be initialized prior to this call.
 *
 * @param vmsg The vmsg to copy data from.
 * @param offset The position (in the vmsg's value) to start copying from.
 * @param dest The destination buffer to copy into.
 * @param num_bytes The amount of space available in @p dest.
 * @retval 0 No data copied, either no data left in @p vmsg or @p num_bytes==0.
 * @retval E_VMSG_NULL_POINTER @p vmsg or @p dest ware NULL.
 * @retval E_VMSG_UNINITIALIZED @p vmsg is not initialized.
 * @retval E_VMSG_SIZE_TOO_LARGE data in @p vmsg + @p offset > @p num_bytes.
 */
ssize_t vmsg_read_if_room(const struct vmsg* vmsg, const size_t offset,
	const char* dest, const size_t num_bytes);

#endif // __VMSG_H__

