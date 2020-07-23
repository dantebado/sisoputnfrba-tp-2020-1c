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
	MESSAGE_ACK,
	READY_TO_RECIEVE,
	PROCESSED
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
	int compacting_freq;
	char * log_file;

	int internal_socket;
	pthread_t server_thread;
} broker_config __attribute__((packed));

typedef struct {
	int socket;
	char * ip;
	int port;
	int ready_to_recieve;
	int doing_internal_work;
	int just_created;
	t_list * queues;
	pthread_mutex_t access_mutex;
	pthread_mutex_t access_answering;
} client __attribute__((packed));

typedef struct {
	_message_queue_name name;
	t_list * subscribers;
	t_list * messages;
	pthread_t thread;
	pthread_mutex_t access_mutex;
} message_queue __attribute__((packed));

typedef struct {
	queue_message * message;
	t_list * already_sent;
	t_list * already_acknowledged;
	void * payload_address_copy;
} broker_message __attribute__((packed));

typedef struct {
	int size;
	void * payload;
} _aux_serialization;

typedef struct {
	int number;
	void * partition_start;
	int partition_size;
	int free_size;
	int is_free;
	int access_time;
	int entry_time;
	broker_message * message;
	//_message_queue_name tipo_cola; // = QUEUE_NEW_POKEMON;
	char * tipo_cola;
	int id_message;
} memory_partition;

/*
 *
 * GAMECARD
 *
 * */

typedef struct {
	queue_message * message;
	int from_broker;
} gamecard_thread_payload;

typedef struct {
	int length;
	char * content;
} pokemon_file_serialized;

typedef struct {
	location * position;
	int quantity;
} pokemon_file_line;

typedef struct {
	t_list * locations;
} pokemon_file;

typedef struct {
	int block_size;
	int blocks;
	char * magic_number;

	int blocks_in_bytes;
	int total_bytes;
	int free_bytes;

	t_bitarray * bitmap;
} tall_grass_fs;

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

/*
 * IMPORTANTE
 *
 * Al principio entran por config los pokemones que el entrenador tiene y los pokemones objetivos.
 * De acuerdo a la respuesta de una pregunta surgida, los pokemones que un entrenador tiene y que a la vez
 * son un objetivo, deben eliminarse de la lista de objetivos y pokemones.
 *
 * ESTRATEGIA INTERNA: Eliminamos los pokemones de la lista de objetivos a medida que se van atrapando.
 * Si un entrenador captura un pokemon que no necesita, es agregado a la lista de pokemones del entrenador.
 * De esta forma, se puede tener control sobre los pokemones que un entrenador tiene y puede repartirle a
 * otros entrenadores en caso de deadlock o llegar a finalizar el objetivo del team.
 */

typedef struct {
	char * name;
	int total_count;
	int total_caught;
} pokemon_requirement;

typedef enum{
	NEW_ACTION,
	READY_ACTION,
	EXEC_ACTION,
	BLOCKED_ACTION,
	EXIT_ACTION
} trainer_action_status;

typedef enum {
	CAPTURING,
	AWAITING_CAPTURE_RESULT,
	TRADING
} trainer_activity_type;

typedef struct {
	trainer_activity_type type;
	int correlative_id_awaiting; //El id del mensaje que va a estar esperando del gamecard
	void * data; //Aca va el entrenador con el que estas haciendo trade
} trainer_activity ;

typedef struct{
	int quantum_counter;
	int last_job_counter;
	int summation_quantum_counter;

	float estimation;
	float last_estimation;

	int max_catch;

	pthread_t * thread;
	pthread_mutex_t mutex;

	trainer_action_status status;

	trainer_activity * current_activity;
} trainer_action;

typedef struct {
	int id;
	int x;
	int y;
	t_list * pokemons;
	t_list * targets;

	trainer_action * stats;
} trainer __attribute__((packed));

typedef struct{
	int global_cpu_counter;
	int context_switch_counter;
	int solved_deadlocks;
} team_statistics;

typedef enum {
	FIFO_PLANNING,
	RR,
} _planning_alg;

typedef struct {
	t_list * trainers_positions;
	t_list * trainers_pokemons;
	t_list * trainers_targets;
	int retry_time_conn;
	int cpu_delay;
	_planning_alg planning_alg;
	int quantum;
	int alpha;
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
