#ifndef LIBRARY_STRUCTURES_H_
#define LIBRARY_STRUCTURES_H_

#include "library.h"

typedef enum {
	success=1,
	failed=-1
} Boolean;

typedef enum {
	NEW_SUBSCRIPTION,
	DOWN_SUBSCRIPTION,
	OPT_OK,
	OPT_FAILED,
	NEW_MESSAGE,
	MESSAGE_ACK
} net_message_type;

typedef struct {
	net_message_type type;
} net_message_header __attribute__((packed));

typedef enum {
	NEW_POKEMON,
	APPEARED_POKEMON,
	CATCH_POKEMON,
	CAUGHT_POKEMON,
	GET_POKEMON,
	LOCALIZED_POKEMON
} pokemon_message_type;

typedef enum {
	QUEUE_NEW_POKEMON,
	QUEUE_APPEARED_POKEMON,
	QUEUE_CATCH_POKEMON,
	QUEUE_CAUGHT_POKEMON,
	QUEUE_GET_POKEMON,
	QUEUE_LOCALIZED_POKEMON
} _message_queue_name;

typedef struct {
	pokemon_message_type type;
	int message_id;
	int correlative_id;
	_message_queue_name queue;
} pokemon_message_header __attribute__((packed));

typedef struct {
	pokemon_message_header * header;
	int is_serialized;
	void * payload;
} queue_message __attribute__((packed));

typedef struct {
	uint32_t name_length;
	char * pokemon;
	uint32_t x;
	uint32_t y;
	uint32_t count;
} new_pokemon_message __attribute__((packed));

typedef struct {
	uint32_t name_length;
	char * pokemon;
	uint32_t x;
	uint32_t y;
} appeared_pokemon_message __attribute__((packed));

typedef struct {
	uint32_t name_length;
	char * pokemon;
	uint32_t x;
	uint32_t y;
} catch_pokemon_message __attribute__((packed));

typedef struct {
	uint32_t result;
} caught_pokemon_message __attribute__((packed));

typedef struct {
	uint32_t name_length;
	char * pokemon;
} get_pokemon_message __attribute__((packed));

typedef struct {
	uint32_t x;
	uint32_t y;
} location __attribute__((packed));
typedef struct {
	uint32_t name_length;
	char * pokemon;
	uint32_t locations_counter;
	t_list * locations;
} localized_pokemon_message __attribute__((packed));

/*
 *
 * BROKER
 *
 * */

typedef enum {
	PARTITIONS,
	BUDDY_SYSTEM
} _memory_alg;
typedef enum {
	FIFO_REPLACEMENT,
	LRU
} _remplacement_alg;
typedef enum {
	FIRST_FIT,
	BEST_FIT
} _seek_alg;
typedef struct {
	int memory_size;
	int partition_min_size;
	_memory_alg memory_alg;
	_remplacement_alg remplacement_alg;
	_seek_alg seek_alg;
	char * broker_ip;
	int broker_port;
	int compating_freq;
	char * log_file;

	int internal_socket;
	pthread_t server_thread;
} broker_config __attribute__((packed));

typedef struct {
	int socket;
	char * ip;
	int port;
	sem_t * mutex;
} client __attribute__((packed));

typedef struct {
	_message_queue_name name;
	t_list * subscribers;
	t_list * messages;
	sem_t * mutex;
	pthread_t thread;
} message_queue __attribute__((packed));

typedef struct {
	queue_message * message;
	t_list * already_sent;
	t_list * already_acknowledged;
} broker_message __attribute__((packed));

typedef struct {
	int size;
	void * payload;
} _aux_serialization;


/*
 *
 * GAMECARD
 *
 * */
typedef struct {
	int retry_time_conn;
	int retry_time_op;
	char * tallgrass_mounting_point;
	char * broker_ip;
	int broker_port;
	int gamecard_port;

	int internal_socket;
	pthread_t server_thread;
	int broker_socket;
	pthread_t broker_thread;
} gamecard_config __attribute__((packed));


/*
 *
 *  TEAM
 *
 * */
typedef struct {
	int x;
	int y;
	t_list * pokemons;
	t_list * targets;
} trainer __attribute__((packed));

typedef enum {
	FIFO_PLANNING,
	RR,
	SJF_CD,
	SJF_SD
} _planning_alg;
typedef struct {
	t_list * trainers_positions;
	t_list * trainers_pokemons;
	t_list * trainers_targets;
	int retry_time_conn;
	int cpu_delay;
	_planning_alg planning_alg;
	int quantum;
	char * broker_ip;
	int broker_port;
	int initial_estimate;
	char * log_file;
	int team_port;

	int broker_socket;
	pthread_t broker_thread;
	int internal_socket;
	pthread_t server_thread;
} team_config __attribute__((packed));

/*
 *
 * GAMEBOY
 *
 * */
typedef struct {
	char * broker_ip;
	char * team_ip;
	char * gamecard_ip;
	int broker_port;
	int team_port;
	int gamecard_port;
} gameboy_config __attribute__((packed));

#endif /* LIBRARY_STRUCTURES_H_ */
