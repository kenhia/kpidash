#ifndef NET_H
#define NET_H

#include "registry.h"

/**
 * Start the UDP listener thread.
 * Listens on 0.0.0.0:port for JSON messages and updates the registry.
 * Returns 0 on success, -1 on failure.
 */
int net_start(registry_t *reg, int port);

#endif /* NET_H */
