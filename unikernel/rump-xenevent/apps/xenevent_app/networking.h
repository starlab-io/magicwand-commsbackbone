#ifndef xenevent_app_networking_h
#define xenevent_app_networking_h

#include <sys/types.h>
#include "app_common.h"
#include "message_types.h"
#include "threadpool.h"


int
xe_net_create_socket( IN  mt_request_socket_create_t  * Request,
                      OUT mt_response_socket_create_t * Response,
                      OUT thread_item_t               * WorkerThread );


int
xe_net_connect_socket( IN  mt_request_socket_connect_t  * Request,
                       OUT mt_response_socket_connect_t * Response,
                       IN  thread_item_t                * WorkerThread );

int
xe_net_close_socket( IN  mt_request_socket_close_t  * Request,
                     OUT mt_response_socket_close_t * Response,
                     IN thread_item_t               * WorkerThread );

//int
//xe_net_read_socket( IN  mt_request_socket_read_t  * Request,
//                    OUT mt_response_socket_read_t * Response,
//                    IN thread_item_t              * WorkerThread );

int
xe_net_send_socket( IN  mt_request_socket_send_t   * Request,
                     OUT mt_response_socket_send_t  * Response,
                     IN thread_item_t               * WorkerThread );

int
xe_net_bind_socket( IN mt_request_socket_bind_t   * Request,
                    OUT mt_response_socket_bind_t * Response,
                    IN thread_item_t              * WorkerThread );

int
xe_net_listen_socket( IN  mt_request_socket_listen_t	* Request,
                      OUT mt_response_socket_listen_t 	* Response,
                      IN  thread_item_t                 * WorkerThread );

int
xe_net_accept_socket( IN   mt_request_socket_accept_t		* Request,
                      OUT  mt_response_socket_accept_t 		* Response,
                      IN   thread_item_t                        * WorkerThread );


int
xe_net_recv_socket( IN  mt_request_socket_recv_t        * Request,
                    OUT mt_response_socket_recv_t       * Response,
                    IN  thread_item_t                   * WorkerThread );
#endif // xenevent_app_networking_h
