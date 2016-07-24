#ifndef __VMSG_H__
#define __VMSG_H__
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
#include "basic-type-family.h"
#include "vmsg-error-codes.h"

/* A vmsg is an opaque type usable through the functions in this library. */
struct vmsg;

// These are the only valid vmsg types.
enum vmsg_type_family {
	// Not a real type -- just compiler tricks.
	// This ensure that the compiler will use a signed int for our enum.
	// A signed int is necessary so  enum_vmsg_error_code values can
	// "sort of" occupy the same data space as the type family values.
	VTF_INVALID_ERROR = -1,

	VTF_BASIC = 0,   // This is the only implemented type family.
	VTF_UNUSED_1 = 1,
	VTF_UNUSED_2 = 2,
	VTF_UNUSED_3 = 3,

	// Any value >= VTF_MAX is an error.
	VTF_MAX    = 3
} vmsg_type_family_t;


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
 * Before a vmsg can be used, it must be initialized.  Initialization
 * prepares the vmsg for use.  Only a select few operations may be performed
 * on an uninitialized vmsg:
 *   * @c vmsg_set_type_family
 *   * @c vmsg_delete
 *   * TODO: Fill vmsg from buffer
 *
 * An initialized vmsg may be obtained by calling the appropriate
 * @c vmsg_new* or @c vmsg_wrap* function.
 *
 * Operations on uninitialized vmsgs will either return null or, if
 * supported by the operation, return the vmsg_error_code E_VMSG_UNINITIALIZED.
 */

/**
 * Create a vmsg instance capable of holding @p max_size bytes.
 * The vmsg is allocated along with a congituous buffer at least
 * @p max_size bytes.
 *
 * The memory of the buffer is not cleared.
 * The vmsg returned has 0 bytes marked as used.
 * The returned vmsg instance MUST be freed with @c vmsg_delete().
 * NOTE:  The vmsg returned is uninitialized until the type is set.
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
 * NOTE: The vmsg returned is uninitialized.
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
 * NOTE: The vmsg returned is uninitialized.
 *
 * If @p buffer is NULL, no new vmsg will be created.
 *
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
 * NOTE: The vmsg returned IS initialized.
 *
 * @param buffer The buffer to use with the returned vmsg instance.
 * @param buffer_size The size of @p buffer.
 * @param type vmsg type.  See FIXME:  Add type definition location.
 * @param bytes_used The number of bytes in buffer that are already used.
 * @param transfer_ownership 1 to transfer ownership to the vmsg, 0 otherwise.
 * @retval New struct vmsg instance on success.
 * @retval NULL on error.
 */
struct vmsg* vmsg_wrap_full(const char* buffer, const size_t buffer_size,
		const uint32_t type, const size_t bytes_used,
		const uint32_t transfer_ownership);


/**
 * Create a new vmsg instance as a copy of an existing vmsg instance.
 *
 * The new vmsg is created as via @c vmsg_new, with the contents of @p vmsg's
 * buffer copied into it (whether @p vmsg is contiguous or wraps a buffer).
 *
 * The returned vmsg must be deleted using @c vmsg_delete.
 *
 * If @p vmsg is initialized, the returned vmsg will be also.
 *
 * @param vmsg The struct vmsg instance to copy.
 * @retval New struct vmsg instance on success.
 * @retval NULL on error.
 *
 * TODO:  Determine if this is actually necessary.
 * struct vmsg* vmsg_new_copy(const struct vmsg* vmsg);
 */


/**
 * Create a new vmsg instance as a copy of a buffer.
 *
 * The new vmsg is created as via @c vmsg_new, with the contents of
 * @p buffer copied into it.  The maximum size for the returned vmsg is
 * @p buffer_size.
 *
 * NOTE: The new vsmsg is not initialized. 
 *
 * @param buffer The buffer to copy when creating the new vmsg instance.
 * @param buffer_size The size of @p buffer.
 * @retval New vmsg instance on success.
 * @retval NULL on error.
 *
 * TODO: Is this really necessary?
 * struct vmsg* vmsg_new_copy_buffer(const char* buffer,
 * 		const size_t buffer_size);
 */


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
 * Get the type-family of the vmsg.
 *
 * The return value is the type-family, or negative in the event of an error.
 *
 * @param vmsg The vmsg to get the type family from.
 * @retval vmsg_type_family value if @p vmsg is not NULL and is initialized.
 * @retval E_VMSG_NULL_POINTER if @p vmsg is NULL.
 * @retval E_VMSG_UNINITIALIZED if @p vmsg is not initialized.
 */
enum vmsg_type_family vmsg_get_type_family(const struct vmsg* vmsg);


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
int32_t vmsg_set_type_basic(const struct vmsg* vmsg,
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
 * @retval E_VMSG_UNINITIALIZED if @p vmsg does not have its size set.
 */
ssize_t vmsg_get_size(const struct vmsg* vmsg);


/**
 * Set the size of the value in a vmsg.
 *
 * If @p size is greater than the maximum possible size of @p vmsg,
 * or if @p size is greater than the maximum size of the type of @p vmsg
 * an error is returned.
 *
 * @param vmsg The vmsg to set the size of.
 * @param size The size value to set in @p vmsg.
 * @retval 0 on success.
 * @retval E_VMSG_NULL_POINTER if @p vmsg is NULL.
 * @retval E_VMSG_UNINITIALIZED if @p vmsg does not have the type set.
 * @retval E_VMSG_INVALID_SIZE_FOR_TYPE if @p size is too large for @p vmsg type.
 * @retval E_VMSG_SIZE_TOO_LARGE if @p size is too large for @p vmsg max size.
 */
int32_t vmsg_normal_set_size(const struct vmsg* vmsg, size_t size);


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
 * It is an error to call this with a vmsg that is not initiallized.
 *
 * @param vmsg The vmsg to get the available space from.
 * @retval >0 on success
 * @retval E_VMSG_NULL_POINTER if @p vmsg is NULL.
 * @retval E_VMSG_UNINITIALIZED if @p vmsg is not initialized.
 */
ssize_t vmsg_get_space_available(const struct vmsg* vmsg);


// FIXME:  Should there be a function to get the space limit of a vmsg?
// The space limiit is the smaller of the type-family specific max size
// and the @c vmsg_get_max_size(vmsg).



/* ************** vmsg header I/O operations ************** */


/**
 * Set the type and size of a vmsg from a @p buffer.
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
int32_t vmsg_set_header_from_buffer(const struct vmsg* vmsg,
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

