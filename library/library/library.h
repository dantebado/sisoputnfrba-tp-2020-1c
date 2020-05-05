#ifndef LIBRARY_LIBRARY_H_
#define LIBRARY_LIBRARY_H_

#include <stdio.h>
#include <stdlib.h>

#include <commons/log.h>
#include <commons/bitarray.h>
#include <commons/config.h>
#include <commons/string.h>
#include <commons/collections/list.h>

#include <stdbool.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <semaphore.h>
#include <pthread.h>

#include "structures.h"
#include "functions.h"

t_log * LOGGER;
t_config * _CONFIG;

#endif /* LIBRARY_LIBRARY_H_ */
