#ifndef mwerrno_h
#define mwerrno_h

/*********************************************************************
 * Defines standardized errno values, which line up with those on
 * Ubuntu 14.04. The INS must convert its errno values to these.
*********************************************************************/

// asm-generic/errno-base.h

#define	MW_EPERM		 1	/* Operation not permitted */
#define	MW_ENOENT		 2	/* No such file or directory */
#define	MW_ESRCH		 3	/* No such process */
#define	MW_EINTR		 4	/* Interrupted system call */
#define	MW_EIO		 5	/* I/O error */
#define	MW_ENXIO		 6	/* No such device or address */
#define	MW_E2BIG		 7	/* Argument list too long */
#define	MW_ENOEXEC		 8	/* Exec format error */
#define	MW_EBADF		 9	/* Bad file number */
#define	MW_ECHILD		10	/* No child processes */
#define	MW_EAGAIN		11	/* Try again */
#define	MW_ENOMEM		12	/* Out of memory */
#define	MW_EACCES		13	/* Permission denied */
#define	MW_EFAULT		14	/* Bad address */
#define	MW_ENOTBLK		15	/* Block device required */
#define	MW_EBUSY		16	/* Device or resource busy */
#define	MW_EEXIST		17	/* File exists */
#define	MW_EXDEV		18	/* Cross-device link */
#define	MW_ENODEV		19	/* No such device */
#define	MW_ENOTDIR		20	/* Not a directory */
#define	MW_EISDIR		21	/* Is a directory */
#define	MW_EINVAL		22	/* Invalid argument */
#define	MW_ENFILE		23	/* File table overflow */
#define	MW_EMFILE		24	/* Too many open files */
#define	MW_ENOTTY		25	/* Not a typewriter */
#define	MW_ETXTBSY		26	/* Text file busy */
#define	MW_EFBIG		27	/* File too large */
#define	MW_ENOSPC		28	/* No space left on device */
#define	MW_ESPIPE		29	/* Illegal seek */
#define	MW_EROFS		30	/* Read-only file system */
#define	MW_EMLINK		31	/* Too many links */
#define	MW_EPIPE		32	/* Broken pipe */
#define	MW_EDOM		33	/* Math argument out of domain of func */
#define	MW_ERANGE		34	/* Math result not representable */



// asm-generic/errno.h

#define	MW_EDEADLK		35	/* Resource deadlock would occur */
#define	MW_ENAMETOOLONG	36	/* File name too long */
#define	MW_ENOLCK		37	/* No record locks available */
#define	MW_ENOSYS		38	/* Function not implemented */
#define	MW_ENOTEMPTY	39	/* Directory not empty */
#define	MW_ELOOP		40	/* Too many symbolic links encountered */
#define	MW_EWOULDBLOCK	EAGAIN	/* Operation would block */
#define	MW_ENOMSG		42	/* No message of desired type */
#define	MW_EIDRM		43	/* Identifier removed */
#define	MW_ECHRNG		44	/* Channel number out of range */
#define	MW_EL2NSYNC	45	/* Level 2 not synchronized */
#define	MW_EL3HLT		46	/* Level 3 halted */
#define	MW_EL3RST		47	/* Level 3 reset */
#define	MW_ELNRNG		48	/* Link number out of range */
#define	MW_EUNATCH		49	/* Protocol driver not attached */
#define	MW_ENOCSI		50	/* No CSI structure available */
#define	MW_EL2HLT		51	/* Level 2 halted */
#define	MW_EBADE		52	/* Invalid exchange */
#define	MW_EBADR		53	/* Invalid request descriptor */
#define	MW_EXFULL		54	/* Exchange full */
#define	MW_ENOANO		55	/* No anode */
#define	MW_EBADRQC		56	/* Invalid request code */
#define	MW_EBADSLT		57	/* Invalid slot */

#define	MW_EDEADLOCK	EDEADLK

#define	MW_EBFONT		59	/* Bad font file format */
#define	MW_ENOSTR		60	/* Device not a stream */
#define	MW_ENODATA		61	/* No data available */
#define	MW_ETIME		62	/* Timer expired */
#define	MW_ENOSR		63	/* Out of streams resources */
#define	MW_ENONET		64	/* Machine is not on the network */
#define	MW_ENOPKG		65	/* Package not installed */
#define	MW_EREMOTE		66	/* Object is remote */
#define	MW_ENOLINK		67	/* Link has been severed */
#define	MW_EADV		68	/* Advertise error */
#define	MW_ESRMNT		69	/* Srmount error */
#define	MW_ECOMM		70	/* Communication error on send */
#define	MW_EPROTO		71	/* Protocol error */
#define	MW_EMULTIHOP	72	/* Multihop attempted */
#define	MW_EDOTDOT		73	/* RFS specific error */
#define	MW_EBADMSG		74	/* Not a data message */
#define	MW_EOVERFLOW	75	/* Value too large for defined data type */
#define	MW_ENOTUNIQ	76	/* Name not unique on network */
#define	MW_EBADFD		77	/* File descriptor in bad state */
#define	MW_EREMCHG		78	/* Remote address changed */
#define	MW_ELIBACC		79	/* Can not access a needed shared library */
#define	MW_ELIBBAD		80	/* Accessing a corrupted shared library */
#define	MW_ELIBSCN		81	/* .lib section in a.out corrupted */
#define	MW_ELIBMAX		82	/* Attempting to link in too many shared libraries */
#define	MW_ELIBEXEC	83	/* Cannot exec a shared library directly */
#define	MW_EILSEQ		84	/* Illegal byte sequence */
#define	MW_ERESTART	85	/* Interrupted system call should be restarted */
#define	MW_ESTRPIPE	86	/* Streams pipe error */
#define	MW_EUSERS		87	/* Too many users */
#define	MW_ENOTSOCK	88	/* Socket operation on non-socket */
#define	MW_EDESTADDRREQ	89	/* Destination address required */
#define	MW_EMSGSIZE	90	/* Message too long */
#define	MW_EPROTOTYPE	91	/* Protocol wrong type for socket */
#define	MW_ENOPROTOOPT	92	/* Protocol not available */
#define	MW_EPROTONOSUPPORT	93	/* Protocol not supported */
#define	MW_ESOCKTNOSUPPORT	94	/* Socket type not supported */
#define	MW_EOPNOTSUPP	95	/* Operation not supported on transport endpoint */
#define	MW_EPFNOSUPPORT	96	/* Protocol family not supported */
#define	MW_EAFNOSUPPORT	97	/* Address family not supported by protocol */
#define	MW_EADDRINUSE	98	/* Address already in use */
#define	MW_EADDRNOTAVAIL	99	/* Cannot assign requested address */
#define	MW_ENETDOWN	100	/* Network is down */
#define	MW_ENETUNREACH	101	/* Network is unreachable */
#define	MW_ENETRESET	102	/* Network dropped connection because of reset */
#define	MW_ECONNABORTED	103	/* Software caused connection abort */
#define	MW_ECONNRESET	104	/* Connection reset by peer */
#define	MW_ENOBUFS		105	/* No buffer space available */
#define	MW_EISCONN		106	/* Transport endpoint is already connected */
#define	MW_ENOTCONN	107	/* Transport endpoint is not connected */
#define	MW_ESHUTDOWN	108	/* Cannot send after transport endpoint shutdown */
#define	MW_ETOOMANYREFS	109	/* Too many references: cannot splice */
#define	MW_ETIMEDOUT	110	/* Connection timed out */
#define	MW_ECONNREFUSED	111	/* Connection refused */
#define	MW_EHOSTDOWN	112	/* Host is down */
#define	MW_EHOSTUNREACH	113	/* No route to host */
#define	MW_EALREADY	114	/* Operation already in progress */
#define	MW_EINPROGRESS	115	/* Operation now in progress */
#define	MW_ESTALE		116	/* Stale file handle */
#define	MW_EUCLEAN		117	/* Structure needs cleaning */
#define	MW_ENOTNAM		118	/* Not a XENIX named type file */
#define	MW_ENAVAIL		119	/* No XENIX semaphores available */
#define	MW_EISNAM		120	/* Is a named type file */
#define	MW_EREMOTEIO	121	/* Remote I/O error */
#define	MW_EDQUOT		122	/* Quota exceeded */

#define	MW_ENOMEDIUM	123	/* No medium found */
#define	MW_EMEDIUMTYPE	124	/* Wrong medium type */
#define	MW_ECANCELED	125	/* Operation Canceled */
#define	MW_ENOKEY		126	/* Required key not available */
#define	MW_EKEYEXPIRED	127	/* Key has expired */
#define	MW_EKEYREVOKED	128	/* Key has been revoked */
#define	MW_EKEYREJECTED	129	/* Key was rejected by service */

/* for robust mutexes */
#define	MW_EOWNERDEAD	130	/* Owner died */
#define	MW_ENOTRECOVERABLE	131	/* State not recoverable */

#define MW_ERFKILL		132	/* Operation not possible due to RF-kill */

#define MW_EHWPOISON	133	/* Memory page has hardware error */



#endif // mwerrno_h


