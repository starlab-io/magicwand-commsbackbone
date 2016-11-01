#ifndef _MINIOS_NETFRONT_H_
#define _MINIOS_NETFRONT_H_

#include <mini-os/wait.h>
struct netfront_dev;
struct netfront_dev *netfront_init(char *nodename, void (*netif_rx)(struct netfront_dev *, unsigned char *data, int len), unsigned char rawmac[6], char **ip, void *priv);
void netfront_xmit(struct netfront_dev *dev, unsigned char* data,int len);
void netfront_shutdown(struct netfront_dev *dev);

void *netfront_get_private(struct netfront_dev *);

extern struct wait_queue_head netfront_queue;

#endif /* _MINIOS_NETFRONT_H_ */
