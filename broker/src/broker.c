#include <library/library.h>
#include <signal.h>

/*/
 *
 * GLOBAL VARIABLES
 *
/*/
broker_config CONFIG;
t_list * queues;
int last_message_id;

t_list * clients;
pthread_mutex_t clients_mutex;

t_list * messages_index;
pthread_mutex_t messages_index_mutex;

t_list * partitions;
void * main_memory;
int memory_free_space;
pthread_mutex_t memory_access_mutex;
int time_counter;
pthread_mutex_t time_counter_mutex;

/*/
 *
 * PROTOTYPES
 *
/*/

//SETUP
void setup(int argc, char **argv);

//MEMORY
void init_memory();
char * enum_to_memory_alg(_memory_alg alg);
char * enum_to_memory_replacement_alg(_remplacement_alg alg);
char * enum_to_memory_selection_alg(_seek_alg alg);
int closest_bs_size(int payload_size);
memory_partition * get_available_partition_by_payload_size(int payload_size);
memory_partition * bs_split_partition(memory_partition * partition);
memory_partition * get_bs_available_partition_by_size(int partition_size);
memory_partition * get_partitions_available_by_alg(int partition_size);
memory_partition * get_partitions_available_partition_by_size(int get_partitions_available_partition_by_size);
memory_partition * find_partition_starting_in(int partition_start);
memory_partition * partition_create(int number, int size, void * start, broker_message * message);
void print_partitions_info();
void print_partition_info(int n, memory_partition * partition);
void free_memory_partition(memory_partition * partition);
memory_partition * write_payload_to_memory(int payload_size, void * payload);
void compact_memory(int already_reserved_mutex);
void remove_a_partition();
void * access_partition(memory_partition * partition);
int get_time_counter();

//QUEUES
void init_queue(_message_queue_name);
message_queue * find_queue_by_name(_message_queue_name);
broker_message * add_message_to_queue(queue_message * message, _message_queue_name queue_name);

//SUBSCRIPTIONS
int subscribe_to_broker_queue(int socket, char * ip, int port, _message_queue_name queue_name);
int unsubscribe_from_broker_queue(int socket, char * ip, int port, _message_queue_name queue_name);
void unsubscribe_socket_from_all_queues(int fd, char * ip, int port);

//SERIALIZATION
_aux_serialization * serialize_message_payload(queue_message * message);
void * deserialize_message_payload(void * payload, pokemon_message_type type);

//SERVER
int server_function(int socket);

//MESSAGES
int generate_message_id();
int acknowledge_message(int socket, char * ip, int port, int message_id);
int message_was_sent_to_susbcriber(broker_message * tmessage, client * subscriber);
int destroy_unserialized(void * payload, pokemon_message_type type);

//CLIENTS
int is_same_client(client * c1, client * c2);
client * add_or_get_client(int socket, char * ip, int port);

//SIGNAL
void my_handler(int signum);

int main(int argc, char **argv) {
	setup(argc, argv);

	return EXIT_SUCCESS;
}

/*
 *
 * SETUP *
 *
 * */

void setup(int argc, char **argv) {

	char * cfg_path = string_new();

	string_append(&cfg_path, (argc > 1) ? argv[1] : "broker");
	string_append(&cfg_path, ".cfg");
	_CONFIG = config_create(cfg_path);

	LOGGER = log_create(config_get_string_value(_CONFIG, "LOG_FILE"), (argc > 1) ? argv[1] : "broker", true, LOG_LEVEL_INFO);

	CONFIG.memory_size = config_get_int_value(_CONFIG, "TAMANO_MEMORIA");
	CONFIG.partition_min_size = config_get_int_value(_CONFIG, "TAMANO_MINIMO_PARTICION");
	CONFIG.broker_ip = config_get_string_value(_CONFIG, "IP_BROKER");
	CONFIG.broker_port = config_get_int_value(_CONFIG, "PUERTO_BROKER");
	CONFIG.compacting_freq = config_get_int_value(_CONFIG, "FRECUENCIA_COMPACTACION");
	if(strcmp("PARTICIONES", config_get_string_value(_CONFIG, "ALGORITMO_MEMORIA")) == 0) {
		CONFIG.memory_alg = PARTITIONS;
	} else {
		CONFIG.memory_alg = BUDDY_SYSTEM;
	}
	if(strcmp("FIFO", config_get_string_value(_CONFIG, "ALGORITMO_REEMPLAZO")) == 0) {
		CONFIG.remplacement_alg = FIFO_REPLACEMENT;
	} else {
		CONFIG.remplacement_alg = LRU;
	}
	if(strcmp("FF", config_get_string_value(_CONFIG, "ALGORITMO_PARTICION_LIBRE")) == 0) {
		CONFIG.seek_alg = FIRST_FIT;
	} else {
		CONFIG.seek_alg = BEST_FIT;
	}

	signal(SIGUSR1, my_handler);

	init_memory();

	last_message_id = 0;

	clients = list_create();
	pthread_mutex_init(&clients_mutex, NULL);
	pthread_mutex_init(&messages_index_mutex, NULL);

	messages_index = list_create();

	queues = list_create();

	init_queue(QUEUE_NEW_POKEMON);
	init_queue(QUEUE_APPEARED_POKEMON);
	init_queue(QUEUE_CATCH_POKEMON);
	init_queue(QUEUE_CAUGHT_POKEMON);
	init_queue(QUEUE_GET_POKEMON);
	init_queue(QUEUE_LOCALIZED_POKEMON);

	CONFIG.internal_socket = create_socket();
	bind_socket(CONFIG.internal_socket, CONFIG.broker_port);

	pthread_create(&CONFIG.server_thread, NULL, server_function, CONFIG.internal_socket);
	pthread_join(CONFIG.server_thread, NULL);

	//print_partitions_info();
}

/*
 *
 * MEMORY *
 *
 * */

void init_memory() {
	pthread_mutex_init(&time_counter_mutex, NULL);
	time_counter = 0;
	partitions = list_create();
	main_memory = malloc(CONFIG.memory_size);
	memory_free_space = CONFIG.memory_size;

	pthread_mutex_init(&memory_access_mutex, NULL);

	pthread_mutex_lock(&memory_access_mutex);
		list_add(partitions, partition_create(0, CONFIG.memory_size, main_memory, NULL));
	pthread_mutex_unlock(&memory_access_mutex);
}
char * enum_to_memory_alg(_memory_alg alg) {
	switch(alg) {
		case PARTITIONS:
			return "DYNAMIC_PARTITIONS";
		case BUDDY_SYSTEM:
			return "BUDDY_SYSTEM";
	}
	return "";
}
char * enum_to_memory_replacement_alg(_remplacement_alg alg) {
	switch(alg) {
		case FIFO_REPLACEMENT:
			return "FIFO";
		case LRU:
			return "LRU";
	}
	return "";
}
char * enum_to_memory_selection_alg(_seek_alg alg) {
	switch(alg) {
		case FIRST_FIT:
			return "FIRST_FIT";
		case BEST_FIT:
			return "BEST_FIT";
	}
	return "";
}
int closest_bs_size(int payload_size) {
	int potency = 1, acum_result = 2;
	while(acum_result < payload_size) {
		potency++;
		acum_result *= 2;
	}
	return acum_result;
}
memory_partition * get_available_partition_by_payload_size(int payload_size) {
	int partition_size = payload_size;
	if(partition_size < CONFIG.partition_min_size) {
		partition_size = CONFIG.partition_min_size;
	}

	if(CONFIG.memory_alg == BUDDY_SYSTEM) {
		partition_size = closest_bs_size(payload_size);
	}
	if(partition_size > CONFIG.memory_size) {
		return NULL;
	}
	switch(CONFIG.memory_alg) {
		case PARTITIONS:
			return get_partitions_available_partition_by_size(partition_size);
		case BUDDY_SYSTEM:
			return get_bs_available_partition_by_size(partition_size);
	}
	return NULL;
}
memory_partition * bs_split_partition(memory_partition * partition) {
	int split_size = partition->partition_size / 2;
	memory_partition * p1 = partition_create(partition->number, split_size, partition->partition_start, NULL);
	memory_partition * p2 = partition_create(partition->number + 1, split_size, partition->partition_start + split_size, NULL);

	pthread_mutex_lock(&memory_access_mutex);
	t_list * a_list = list_create();
	int i;
	for(i=0 ; i<partitions->elements_count ; i++) {
		memory_partition * anly = list_get(partitions, i);
		if(partition == anly) {
			list_add(a_list, p1);
			list_add(a_list, p2);
		} else {
			if(anly->number > partition->number) {
				anly->number++;
			}
			list_add(a_list, anly);
		}
	}
	free(partition);
	partitions = a_list;
	pthread_mutex_unlock(&memory_access_mutex);
	return p1;
}
memory_partition * get_bs_available_partition_by_size(int partition_size) {
	int i;
	for(i=0 ; i<partitions->elements_count ; i++) {
		memory_partition * tpartition = list_get(partitions, i);
		if(tpartition->partition_size >= partition_size && tpartition->is_free) {
			while(tpartition->partition_size > partition_size) {
				tpartition = bs_split_partition(tpartition);
			}
			return tpartition;
		}
	}

	compact_memory(false);

	for(i=0 ; i<partitions->elements_count ; i++) {
		memory_partition * tpartition = list_get(partitions, i);
		if(tpartition->partition_size >= partition_size && tpartition->is_free) {
			while(tpartition->partition_size > partition_size) {
				tpartition = bs_split_partition(tpartition);
			}
			return tpartition;
		}
	}

	remove_a_partition();
	return get_bs_available_partition_by_size(partition_size);
}
memory_partition * get_partitions_available_by_alg(int partition_size) {
	int start_parsing = main_memory;
	int i, continue_parsing = false;
	memory_partition * found_memory = NULL;
	int best_fit_fragmentation = CONFIG.memory_size;

	do {
		memory_partition * part = find_partition_starting_in(start_parsing);

		if(part->is_free) {
			if(part->partition_size >= partition_size) {

				if(CONFIG.seek_alg == FIRST_FIT) {
					found_memory = part;
					start_parsing = main_memory + CONFIG.memory_size;
				} else if(CONFIG.seek_alg == BEST_FIT) {
					if( (part->partition_size - partition_size) <= best_fit_fragmentation ) {
						best_fit_fragmentation = part->partition_size - partition_size;
						found_memory = part;
					}
				}

			}
		}

		start_parsing += part->partition_size;
		continue_parsing = start_parsing < (int)(main_memory + CONFIG.memory_size);
	} while(continue_parsing);

	if(found_memory != NULL) {

		if(found_memory->partition_size == partition_size) {
			return found_memory;
		}

		pthread_mutex_lock(&memory_access_mutex);
		t_list * npart = list_create();
		for(i=0 ; i<found_memory->number ; i++) {
			list_add(npart, list_get(partitions, i));
		}
		list_add(npart, found_memory);

		list_add(npart, partition_create(found_memory->number+1, found_memory->partition_size - partition_size,
				found_memory->partition_start + partition_size, NULL));

		found_memory->partition_size = partition_size;
		found_memory->free_size = partition_size;

		for(i=found_memory->number + 1; i<partitions->elements_count ; i++) {
			memory_partition * temp = list_get(partitions, i);
			temp->number++;
			list_add(npart, temp);
		}
		list_destroy(partitions);
		partitions = npart;
		pthread_mutex_unlock(&memory_access_mutex);

		return found_memory;
	}
	return NULL;
}
memory_partition * get_partitions_available_partition_by_size(int partition_size) {
	int failed_searches = 0;
	memory_partition * ret = get_partitions_available_by_alg(partition_size);
	if(ret != NULL) {
		return ret;
	}

	failed_searches++;

	if(CONFIG.compacting_freq == -1) {
		remove_a_partition();
		while(ret == NULL) {
			while(partitions->elements_count > 1) {
				ret = get_partitions_available_by_alg(partition_size);
				if(ret != NULL) {
					return ret;
				}
				remove_a_partition();
			}
			compact_memory(false);
		}
	} else {
		remove_a_partition();
		while(ret == NULL) {
			while(failed_searches < CONFIG.compacting_freq) {
				ret = get_partitions_available_by_alg(partition_size);
				if(ret != NULL) {
					return ret;
				}
				failed_searches++;
				remove_a_partition();
			}
			compact_memory(false);
			failed_searches = 0;
		}
	}
	return NULL;
}
memory_partition * find_partition_starting_in(int partition_start) {
	int i;
	for(i=0 ; i<partitions->elements_count ; i++) {
		memory_partition * tpartition = list_get(partitions, i);
		int start = tpartition->partition_start;
		if(start == partition_start) {
			return tpartition;
		}
	}
	return NULL;
}
memory_partition * partition_create(int number, int size, void * start, broker_message * message) {
	memory_partition * partition = malloc(sizeof(memory_partition));
	partition->number = number;
	partition->partition_start = start;
	partition->is_free = 1;
	partition->partition_size = size;
	partition->free_size = size;
	partition->access_time = get_time_counter();
	partition->entry_time = get_time_counter();
	partition->message = message;
	partition->tipo_cola = "";
	partition->id_message = 0;
	return partition;
}
void print_partitions_info() {
	int i;
	log_info(LOGGER, "-------------------------------------------------------");
	log_info(LOGGER, "::DUMP::");
	for(i=0 ; i<partitions->elements_count ; i++) {
		memory_partition * partition = list_get(partitions, i);
		print_partition_info(i, partition);
	}
	log_info(LOGGER, "-------------------------------------------------------");
}
void print_partition_info(int n, memory_partition * partition) {
	log_info(LOGGER, "Partition %d - \tFrom %d\tTo %d\t[%c]\tSize:%db\tLRU<%d>\tET<%d>\tCOLA:<%s>\tID:<%d>",
			partition->number, partition->partition_start - main_memory, partition->partition_start + partition->partition_size - main_memory,
			partition->is_free ? 'F' : 'X', partition->partition_size, partition->access_time, partition->entry_time,
			partition->tipo_cola, partition->id_message);
}
void free_memory_partition(memory_partition * partition) {
	int i, j;

	message_queue * queue = find_queue_by_name(partition->message->message->header->queue);
	pthread_mutex_lock(&messages_index_mutex);
	pthread_mutex_lock(&(queue->access_mutex));

		_Bool * is_same_message(broker_message * p1) {
			return p1->message->header->message_id == partition->message->message->header->message_id;
		}
		list_remove_by_condition(messages_index, is_same_message);
		list_remove_by_condition(queue->messages, is_same_message);

	pthread_mutex_unlock(&(queue->access_mutex));
	pthread_mutex_unlock(&messages_index_mutex);

	pthread_mutex_lock(&memory_access_mutex);
	partition->free_size = partition->partition_size;
	partition->message = NULL;
	partition->is_free = true;

	memory_free_space += partition->partition_size;

	if(CONFIG.memory_alg == BUDDY_SYSTEM) {
		compact_memory(true);
	} else {
		for(j=0 ; j<partitions->elements_count - 1; j++) {
			memory_partition * tpartition = list_get(partitions, j);
			memory_partition * next_partition = list_get(partitions, j+1);

			if(next_partition != NULL) {
				if(next_partition->is_free && tpartition->is_free) {
					tpartition->partition_size += next_partition->partition_size;
					for(i=0 ; i<partitions->elements_count ; i++) {
						memory_partition * tp = list_get(partitions, i);
						if(tp->number == next_partition->number) {
							list_remove(partitions, i);
							i--;
						} else {
							if(tp->number > tpartition->number) {
								tp->number = tp->number - 1;
							}
						}
					}
				}
			}
		}
	}
	pthread_mutex_unlock(&memory_access_mutex);
	log_info(LOGGER, "REMOVED PARTITION %d (%d)", partition->number, partition->partition_start - main_memory);
}
memory_partition * write_payload_to_memory(int payload_size, void * payload) {
	memory_partition * the_partition = get_available_partition_by_payload_size(payload_size);

	if(the_partition == NULL) {
	} else {
		the_partition->is_free = false;
		the_partition->free_size = the_partition->partition_size - payload_size;
		the_partition->access_time = get_time_counter();
		the_partition->entry_time = the_partition->access_time;
		memcpy(the_partition->partition_start, payload, payload_size);

		memory_free_space -= payload_size;
	}

	return the_partition;
}
void compact_memory(int already_reserved_mutex) {
	if(!already_reserved_mutex){
		pthread_mutex_lock(&memory_access_mutex);
	}

	int i;
	switch(CONFIG.memory_alg) {
		case PARTITIONS:
			{
				log_info(LOGGER, "START COMPACTING");
				int free_s = 0, removed = 0;
				void * last_ptr = main_memory;
				for(i=0 ; i<partitions->elements_count ; i++) {
					memory_partition * tp = list_get(partitions, i);
					if(tp->is_free) {
						free_s += tp->partition_size;
						list_remove(partitions, i);
						removed++;
						i--;
					} else {
						memcpy(last_ptr, tp->partition_start, tp->partition_size);
						tp->partition_start = last_ptr;

						tp->number = tp->number - removed;
						last_ptr += tp->partition_size;
					}
				}
				if(free_s > 0) {
					list_add(partitions, partition_create(partitions->elements_count, free_s, last_ptr, NULL));
				}
				log_info(LOGGER, "DONE COMPACTING");
			}
			break;
		case BUDDY_SYSTEM:;
			{
				log_info(LOGGER, "START BD ASSOCIATION");
				int was_change = false;
				do {
					was_change = false;
					for(i=0 ; i<partitions->elements_count - 1 ; i++) {
						memory_partition * p1 = list_get(partitions, i);
						memory_partition * p2 = list_get(partitions, i + 1);

						if(p1->partition_size == p2->partition_size &&
							p1->is_free && p2->is_free
						) {
							int addr_1 = p1->partition_start - main_memory;
							int buddy_check = addr_1  ^ p2->partition_size;
							if(buddy_check == (p2->partition_start - main_memory)) {
								log_info(LOGGER, "ASOCIATE PARTITIONS %d (%d) AND %d (%d)",
										p1->number, p1->partition_start - main_memory,
										p2->number, p2->partition_start - main_memory);
								p1->partition_size *= 2;
								list_remove(partitions, i+1);
								free(p2);
								for(i++ ; i<partitions->elements_count ; i++) {
									p2 = list_get(partitions, i);
									p2->number--;
								}
								was_change = true;
							}
						}
					}
				} while(was_change);
				log_info(LOGGER, "DONE BD ASSOCIATION");
			}
			break;
	}

	if(!already_reserved_mutex){
		pthread_mutex_unlock(&memory_access_mutex);
	}
}
void remove_a_partition() {
	int i;
	memory_partition * to_remove = NULL;
	switch(CONFIG.remplacement_alg) {
		case FIFO_REPLACEMENT:
			{
				int now_time_counter = get_time_counter();
				for(i=0 ; i<partitions->elements_count ; i++) {
					memory_partition * partition = list_get(partitions, i);
					if(now_time_counter >= partition->entry_time && !partition->is_free) {
						now_time_counter = partition->entry_time;
						to_remove = partition;
					}
				}
			}
			break;
		case LRU:;
			{
				int now_time_counter = get_time_counter();
				for(i=0 ; i<partitions->elements_count ; i++) {
					memory_partition * partition = list_get(partitions, i);
					if(now_time_counter >= partition->access_time && !partition->is_free) {
						now_time_counter = partition->access_time;
						to_remove = partition;
					}
				}
			}
			break;
	}
	if(to_remove == NULL) { }
	else {
		free_memory_partition(to_remove);
	}
}
void * access_partition(memory_partition * partition) {
	partition->access_time = get_time_counter();
	return partition->partition_start;
}
int get_time_counter() {
	pthread_mutex_lock(&time_counter_mutex);
	time_counter++;
	pthread_mutex_unlock(&time_counter_mutex);
	return time_counter;
}

/*
 *
 * QUEUES *
 *
 * */

void init_queue(_message_queue_name name) {
	message_queue * queue = malloc(sizeof(message_queue));
	queue->messages = list_create();
	queue->subscribers = list_create();
	queue->name = name;

	pthread_mutex_init(&(queue->access_mutex), NULL);

	list_add(queues, queue);
}
message_queue * find_queue_by_name(_message_queue_name name) {
	int i;
	for(i=0 ; i<queues->elements_count ; i++) {
		message_queue * queue = list_get(queues, i);
		if(queue->name == name) {
			return queue;
		}
	}
	return NULL;
}
broker_message * add_message_to_queue(queue_message * message, _message_queue_name queue_name) {
	message_queue * queue = find_queue_by_name(queue_name);
	if(queue == NULL) {
		return NULL;
	}

	broker_message * final_message = malloc(sizeof(broker_message));
	final_message->message = message;
	final_message->already_sent = list_create();
	final_message->already_acknowledged = list_create();

	void * original_payload = message->payload;
	_aux_serialization * aux_str = serialize_message_payload(message);
	message->payload = aux_str->payload;
	destroy_unserialized(original_payload, message->header->type);

	memory_partition * partition = write_payload_to_memory(aux_str->size, aux_str->payload);
	if(partition == NULL) {
		list_destroy(final_message->already_sent);
		list_destroy(final_message->already_acknowledged);

		free(aux_str->payload);
		free(aux_str);
		free(final_message);

		return NULL;
	}
	free(aux_str->payload);
	free(aux_str);
	log_info(LOGGER, "WRITEN MID %d (correlative %d) TO PARTITION %d (%d)",
			final_message->message->header->message_id,
			final_message->message->header->correlative_id,
			partition->number,
			partition->partition_start - main_memory);
	partition->message = final_message;

	partition->tipo_cola = enum_to_queue_name(queue_name);
	partition->id_message = message->header->message_id;

	final_message->message->payload = partition;
	final_message->payload_address_copy = partition;

	pthread_mutex_lock(&messages_index_mutex);
	pthread_mutex_lock(&(queue->access_mutex));
	list_add(queue->messages, final_message);
	list_add(messages_index, final_message);
	log_info(LOGGER, "MID %d added to indexes", final_message->message->header->message_id);
	pthread_mutex_unlock(&(queue->access_mutex));
	pthread_mutex_unlock(&messages_index_mutex);
	return final_message;
}
int send_message_to_client(broker_message * message, client * subscriber) {
	memory_partition * partition = message->payload_address_copy;

	void * deserialized_message = deserialize_message_payload(partition->partition_start, message->message->header->type);
	message->message->is_serialized = false;
	message->message->payload = deserialized_message;

	if(!message_was_sent_to_susbcriber(message, subscriber) && subscriber->ready_to_recieve == 1) {
		log_info(LOGGER, "SENDING MID %d, TO FD %d", message->message->header->message_id, subscriber->socket);
			log_info(LOGGER, "  SENT MID %d, TO FD %d", message->message->header->message_id, subscriber->socket);
			send_pokemon_message_with_id(subscriber->socket, message->message, 0,
					message->message->header->message_id,
					message->message->header->correlative_id);
		list_add(message->already_sent, message);

		access_partition(partition);

		destroy_unserialized(deserialized_message, message->message->header->type);
		message->message->is_serialized = true;
		message->message->payload = partition;
		return 1;
	}

	destroy_unserialized(deserialized_message, message->message->header->type);
	message->message->is_serialized = true;
	message->message->payload = partition;
	return 0;
}
void broadcast_message(broker_message * message) {
	log_info(LOGGER, "Broadcasting %d", message->message->header->message_id);
	message_queue * queue = find_queue_by_name(message->message->header->queue);
	log_info(LOGGER, "  Queue has %d subs", queue->subscribers->elements_count);
	for(int i=0 ; i<queue->subscribers->elements_count ; i++) {
		client * subscriber = list_get(queue->subscribers, i);
		pthread_mutex_lock(&(subscriber->access_answering));
		send_message_to_client(message, subscriber);
	}
}
void update_subscriber_with_messages_for_queue(message_queue * queue, client * subscriber) {
	pthread_mutex_lock(&(queue->access_mutex));
	for(int i=0 ; i<queue->messages->elements_count ; i++) {
		broker_message * message = list_get(queue->messages, i);
		send_message_to_client(message, subscriber);
	}
	pthread_mutex_unlock(&(queue->access_mutex));
}
void update_subscriber_with_messages_all_queues(client * subscriber) {
	log_info(LOGGER, "Suscrito a %d colas", subscriber->queues->elements_count);
	for(int l=0 ; l<subscriber->queues->elements_count ; l++) {
		message_queue * queue = list_get(subscriber->queues, l);
		update_subscriber_with_messages_for_queue(queue, subscriber);
	}
}

/*
 *
 * SUBSCRIPTIONS *
 *
 * */

int subscribe_to_broker_queue(int socket, char * ip, int port, _message_queue_name queue_name) {
	client * sub = add_or_get_client(socket, ip, port);

	message_queue * queue = find_queue_by_name(queue_name);
	if(queue != NULL) {
		list_add(queue->subscribers, sub);

		int pres = 0;
		for(int o=0 ; o<sub->queues->elements_count ; o++) {
			message_queue * q = list_get(sub->queues, o);
			if(q->name == queue->name) {
				pres = 1;
			}
		}
		if(pres == 0) {
			list_add(sub->queues, queue);
		}

		log_info(LOGGER, "FD %d SUBSCRIBED TO QUEUE %s", socket, enum_to_queue_name(queue_name));
		send_int(socket, OPT_OK);
		return OPT_OK;
	} else {
		log_info(LOGGER, "FD %d FAILED SUBSCRIBING TO QUEUE %s", socket, enum_to_queue_name(queue_name));
		send_int(socket, OPT_FAILED);
		return OPT_FAILED;
	}
}
int unsubscribe_from_broker_queue(int socket, char * ip, int port, _message_queue_name queue_name) {
	message_queue * queue = find_queue_by_name(queue_name);
	client * tc = add_or_get_client(socket, ip, port);

	if(queue != NULL) {
		int i, r = 0;
		for(i=0 ; i<queue->subscribers->elements_count ; i++) {
			client * s = list_get(queue->subscribers, i);
			if(is_same_client(s, tc)) {
				r = 1;
				list_remove(queue->subscribers, i);
			}
		}
		if(r == 0) {
			send_int(socket, OPT_FAILED);
			return OPT_FAILED;
		} else {
			send_int(socket, OPT_OK);
			return OPT_OK;
		}
	} else {
		send_int(socket, OPT_FAILED);
		return OPT_FAILED;
	}
}
void unsubscribe_socket_from_all_queues(int fd, char * ip, int port) {
	client * tclient = add_or_get_client(fd, ip, port);
	int i, j;
	for(i=0 ; i<queues->elements_count ; i++) {
		message_queue * queue = list_get(queues, i);
		for(j=0 ; j<queue->subscribers->elements_count ; j++) {
			client * sub = list_get(queue->subscribers, j);
			if(is_same_client(sub, tclient)) {
				list_remove(queue->subscribers, j);
				j = queue->subscribers->elements_count + 1;
			}
		}
	}
}

/*
 *
 * SERIALIZATION *
 *
 * */

_aux_serialization * serialize_message_payload(queue_message * message) {
	_aux_serialization * serialized = malloc(sizeof(_aux_serialization));
	serialized->size = 0;

	int offset = 0, i;
	switch(message->header->type) {
		case NEW_POKEMON:;
			new_pokemon_message * npm = message->payload;

			serialized->size =	sizeof(uint32_t) +
								sizeof(char) * npm->name_length +
								sizeof(uint32_t) +
								sizeof(uint32_t) +
								sizeof(uint32_t);

			serialized->payload = malloc(serialized->size);

			memcpy(serialized->payload + offset, &npm->name_length, sizeof(uint32_t));
				offset += sizeof(uint32_t);
			memcpy(serialized->payload + offset, npm->pokemon, sizeof(char) * npm->name_length);
				offset += sizeof(char) * npm->name_length;
			memcpy(serialized->payload + offset, &npm->x, sizeof(uint32_t));
				offset += sizeof(uint32_t);
			memcpy(serialized->payload + offset, &npm->y, sizeof(uint32_t));
				offset += sizeof(uint32_t);
			memcpy(serialized->payload + offset, &npm->count, sizeof(uint32_t));
				offset += sizeof(uint32_t);

			break;
		case APPEARED_POKEMON:;
			appeared_pokemon_message * apm = message->payload;

			serialized->size =	sizeof(uint32_t) +
								sizeof(char) * apm->name_length +
								sizeof(uint32_t) * 2;

			serialized->payload = malloc(serialized->size);

			memcpy(serialized->payload + offset, &apm->name_length, sizeof(uint32_t));
				offset += sizeof(uint32_t);
			memcpy(serialized->payload + offset, apm->pokemon, sizeof(char) * apm->name_length);
				offset += sizeof(char) * apm->name_length;
			memcpy(serialized->payload + offset, &apm->x, sizeof(uint32_t));
				offset += sizeof(uint32_t);
			memcpy(serialized->payload + offset, &apm->y, sizeof(uint32_t));
				offset += sizeof(uint32_t);

			break;
		case CATCH_POKEMON:;
			catch_pokemon_message * chpm = message->payload;

			serialized->size =	sizeof(uint32_t) +
								sizeof(char) * chpm->name_length +
								sizeof(uint32_t) * 2;

			serialized->payload = malloc(serialized->size);

			memcpy(serialized->payload + offset, &chpm->name_length, sizeof(uint32_t));
				offset += sizeof(uint32_t);
			memcpy(serialized->payload + offset, chpm->pokemon, sizeof(char) * chpm->name_length);
				offset += sizeof(char) * chpm->name_length;
			memcpy(serialized->payload + offset, &chpm->x, sizeof(uint32_t));
				offset += sizeof(uint32_t);
			memcpy(serialized->payload + offset, &chpm->y, sizeof(uint32_t));
				offset += sizeof(uint32_t);

			break;
		case CAUGHT_POKEMON:;
			caught_pokemon_message * ctpm = message->payload;

			serialized->size =	sizeof(uint32_t);

			serialized->payload = malloc(serialized->size);

			memcpy(serialized->payload + offset, &ctpm->result, sizeof(uint32_t));
				offset += sizeof(uint32_t);

			break;
		case GET_POKEMON:;
			get_pokemon_message * gpm = message->payload;

			serialized->size =	sizeof(uint32_t) +
								sizeof(char) * gpm->name_length;

			serialized->payload = malloc(serialized->size);

			memcpy(serialized->payload + offset, &gpm->name_length, sizeof(uint32_t));
				offset += sizeof(uint32_t);
			memcpy(serialized->payload + offset, gpm->pokemon, sizeof(char) * gpm->name_length);
				offset += sizeof(char) * gpm->name_length;

			break;
		case LOCALIZED_POKEMON:;
			localized_pokemon_message * lpm = message->payload;

			serialized->size =	sizeof(uint32_t) +
								sizeof(char) * lpm->name_length +
								sizeof(uint32_t) +
								sizeof(uint32_t) * 2 * lpm->locations_counter;

			serialized->payload = malloc(serialized->size);

			memcpy(serialized->payload + offset, &lpm->name_length, sizeof(uint32_t));
				offset += sizeof(uint32_t);
			memcpy(serialized->payload + offset, lpm->pokemon, sizeof(char) * lpm->name_length);
				offset += sizeof(char) * lpm->name_length;
			memcpy(serialized->payload + offset, &lpm->locations_counter, sizeof(uint32_t));
				offset += sizeof(uint32_t);
			for(i=0 ; i<lpm->locations_counter ; i++) {
				location * temp = list_get(lpm->locations, i);
				memcpy(serialized->payload + offset, &temp->x, sizeof(uint32_t));
					offset += sizeof(uint32_t);
				memcpy(serialized->payload + offset, &temp->y, sizeof(uint32_t));
					offset += sizeof(uint32_t);
			}

			break;
	}

	message->is_serialized = true;
	return serialized;
}
void * deserialize_message_payload(void * payload, pokemon_message_type type) {
	void * return_pointer;

	int offset = 0, i;
	uint32_t aux_size;
	switch(type) {
		case NEW_POKEMON:;
			new_pokemon_message * npm = malloc(sizeof(new_pokemon_message));

			memcpy(&aux_size, payload + offset, sizeof(uint32_t));
				offset += sizeof(uint32_t);

			npm->name_length = aux_size;
			npm->pokemon = malloc(sizeof(char) * aux_size);
			memcpy(npm->pokemon, payload + offset, sizeof(char) * aux_size);
				offset += sizeof(char) * aux_size;
			memcpy(&npm->x, payload + offset, sizeof(uint32_t));
				offset += sizeof(uint32_t);
			memcpy(&npm->y, payload + offset, sizeof(uint32_t));
				offset += sizeof(uint32_t);
			memcpy(&npm->count, payload + offset, sizeof(uint32_t));
				offset += sizeof(uint32_t);

			return_pointer = npm;
			break;
		case APPEARED_POKEMON:;
			appeared_pokemon_message * apm = malloc(sizeof(appeared_pokemon_message));

			memcpy(&aux_size, payload + offset, sizeof(uint32_t));
				offset += sizeof(uint32_t);

			apm->name_length = aux_size;
			apm->pokemon = malloc(sizeof(char) * aux_size);
			memcpy(apm->pokemon, payload + offset, sizeof(char) * aux_size);
				offset += sizeof(char) * aux_size;
			memcpy(&apm->x, payload + offset, sizeof(uint32_t));
				offset += sizeof(uint32_t);
			memcpy(&apm->y, payload + offset, sizeof(uint32_t));
				offset += sizeof(uint32_t);

			return_pointer = apm;
			break;
		case CATCH_POKEMON:;
			catch_pokemon_message * chpm = malloc(sizeof(catch_pokemon_message));

			memcpy(&aux_size, payload + offset, sizeof(uint32_t));
				offset += sizeof(uint32_t);

			chpm->name_length = aux_size;
			chpm->pokemon = malloc(sizeof(char) * aux_size);
			memcpy(chpm->pokemon, payload + offset, sizeof(char) * aux_size);
				offset += sizeof(char) * aux_size;
			memcpy(&chpm->x, payload + offset, sizeof(uint32_t));
				offset += sizeof(uint32_t);
			memcpy(&chpm->y, payload + offset, sizeof(uint32_t));
				offset += sizeof(uint32_t);

			return_pointer = chpm;
			break;
		case CAUGHT_POKEMON:;
			caught_pokemon_message * ctpm = malloc(sizeof(caught_pokemon_message));

			memcpy(&ctpm->result, payload + offset, sizeof(uint32_t));
				offset += sizeof(uint32_t);

			return_pointer = ctpm;
			break;
		case GET_POKEMON:;
			get_pokemon_message * gpm = malloc(sizeof(get_pokemon_message));

			memcpy(&aux_size, payload + offset, sizeof(uint32_t));
				offset += sizeof(uint32_t);
			gpm->name_length = aux_size;

			gpm->pokemon = malloc(sizeof(char) * aux_size);
			memcpy(gpm->pokemon, payload + offset, sizeof(char) * aux_size);
				offset += sizeof(char) * aux_size;

			return_pointer = gpm;
			break;
		case LOCALIZED_POKEMON:;
			localized_pokemon_message * lpm = malloc(sizeof(localized_pokemon_message));

			memcpy(&aux_size, payload + offset, sizeof(uint32_t));
				offset += sizeof(uint32_t);
			lpm->name_length = aux_size;

			lpm->pokemon = malloc(sizeof(char) * aux_size);
			memcpy(lpm->pokemon, payload + offset, sizeof(char) * aux_size);
				offset += sizeof(char) * aux_size;

			memcpy(&aux_size, payload + offset, sizeof(uint32_t));
				offset += sizeof(uint32_t);
			lpm->locations_counter = aux_size;

			lpm->locations = list_create();
			for(i=0 ; i<lpm->locations_counter ; i++) {
				uint32_t x, y;
				memcpy(&x, payload + offset, sizeof(uint32_t));
					offset += sizeof(uint32_t);
				memcpy(&y, payload + offset, sizeof(uint32_t));
					offset += sizeof(uint32_t);
				list_add(lpm->locations, location_create(x, y));
			}

			return_pointer = lpm;
			break;
	}

	return return_pointer;
}

typedef struct {
	int fd;
	char * ip;
	int port;
	net_message_header * header;
} handle_incoming_struct;
void * handle_incoming_as_thread(handle_incoming_struct * data) {
	int fd = data->fd;
	char * ip = data->ip;
	int port = data->port;
	net_message_header * header = data->header;

	int time_req = get_time_counter();
	client * tsub = add_or_get_client(fd, ip, port);

	switch(header->type) {
		case READY_TO_RECIEVE:;
			{
				tsub->ready_to_recieve = 1;
				log_info(LOGGER, "FD %d Ready To Recieve Data", fd);
				update_subscriber_with_messages_all_queues(tsub);
			}
			break;
		case PROCESSED:;
			{
				log_info(LOGGER, "  %d Answered Completition", tsub->socket);
				pthread_mutex_unlock(&(tsub->access_answering));
			}
			break;
		case NEW_SUBSCRIPTION:;
			{
				_message_queue_name queue_name = recv_int(fd);
				subscribe_to_broker_queue(fd, ip, port, queue_name);
			}
			break;
		case DOWN_SUBSCRIPTION:;
			{
				_message_queue_name queue_name = recv_int(fd);
				unsubscribe_from_broker_queue(fd, ip, port, queue_name);
			}
			break;
		case NEW_MESSAGE:;
			{
				int message_id = generate_message_id();

				send_int(fd, message_id);
				queue_message * message = receive_pokemon_message(fd);

				if(message->header->message_id != message_id) {
					log_info(LOGGER, "  Pre assigned MID %d", message->header->message_id);
				}

				print_pokemon_message(message);
				broker_message * broker_answer = add_message_to_queue(message, message->header->queue);
				if(broker_answer != NULL) {
					broadcast_message(broker_answer);
				} else { }
			}
			break;
		case MESSAGE_ACK:;
			int message_id = recv_int(fd);
			acknowledge_message(fd, ip, port, message_id);
			break;
	}

	tsub->doing_internal_work = 0;
	pthread_mutex_unlock(&tsub->access_mutex);

	return NULL;
}

/*
 *
 * SERVER *
 *
 * */

int server_function(int socket) {
	int MAX_CONN = 50;

	int addrlen, new_socket ,client_socket_array[MAX_CONN], activity, i, bytesread, sd;
	int max_sd;
	struct sockaddr_in address;
	fd_set readfds;

	net_message_header * incoming;

	for (i = 0; i < MAX_CONN; i++) {
		client_socket_array[i] = 0;
	}
	log_trace(LOGGER, "Init Clients");
	if (listen(socket, MAX_CONN) < 0) {
		log_error(LOGGER, "Socket %d cannot listen", socket);
		return -1;
	}

	addrlen = sizeof(address);

	while(1) {
		FD_ZERO(&readfds);
		FD_SET(socket, &readfds);
		max_sd = socket;
		for (i = 0 ; i < MAX_CONN ; i++) {
			sd = client_socket_array[i];
			if (sd > 0){
				FD_SET( sd , &readfds);
			}
			if (sd > max_sd){
				max_sd = sd;
			}
		}

		log_trace(LOGGER, "Awaiting message");
		activity = select(max_sd + 1, &readfds, NULL, NULL, NULL);
		if (activity < 0) {
			log_error(LOGGER, "Error on listening activity");
		} else {
			if (FD_ISSET(socket, &readfds)) {
				if ((new_socket = accept(socket,
						(struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {
					log_error(LOGGER, "Error accepting new connection");
				} else {
					log_trace(LOGGER, "Accepted new connection");

					char str[INET_ADDRSTRLEN];
					inet_ntop(AF_INET, &(address.sin_addr), str, INET_ADDRSTRLEN);

					{
						//NEW CONNECTION
						//new_connection(new_socket, string_duplicate(str), address.sin_port);
						log_info(LOGGER, "NEW CONNECTION from FD %d", new_socket);
					}

					int registered = 0;
					for (i = 0; i < MAX_CONN; i++) {
						if (client_socket_array[i] == 0) {
							client_socket_array[i] = new_socket;
							log_trace(LOGGER, "New client registered in index %d", i);
							registered = 1;
							break;
						}
					}
					if(registered == 0) {
						log_error(LOGGER, "Cannot register client because of MAX_CONN limit");
					}
				}
			}
			for (i = 0; i < MAX_CONN; i++) {
				sd = client_socket_array[i];
				if (FD_ISSET(sd, &readfds)) {
					int client_socket = sd;
					incoming = malloc(sizeof(net_message_header));

					getpeername(sd , (struct sockaddr*)&address , (socklen_t*)&addrlen);
					char str[INET_ADDRSTRLEN];
					inet_ntop(AF_INET, &(address.sin_addr), str, INET_ADDRSTRLEN);

					client * tsub = add_or_get_client(client_socket, str, address.sin_port);

					int checkrecv = recv(client_socket, incoming, 1, MSG_PEEK);
					if(checkrecv > 0 && tsub->doing_internal_work == 0) {
						tsub->doing_internal_work = 1;

						if ((bytesread = read(client_socket, incoming, sizeof(net_message_header))) <= 0) {
							{
								//LOST CONNECTION
								//lost_connection(client_socket, string_duplicate(str), address.sin_port);
							}
							close(sd);
							client_socket_array[i] = 0;
							log_trace(LOGGER, "Closed and freed connection for index %d, socket %d", i, sd);
						} else {
							{
								//INCOMING MESSAGE
								//incoming_message(client_socket, string_duplicate(str), address.sin_port, incoming);

								handle_incoming_struct * data = malloc(sizeof(handle_incoming_struct));
								data->fd = client_socket;
								data->ip = str;
								data->port = address.sin_port;
								data->header = incoming;

								pthread_t handling_thread;
								pthread_create(&handling_thread, NULL, &handle_incoming_as_thread, data);
							}
						}
					}
				}
			}
		}
	}

	return 0;
}

/*
 *
 * MESSAGES *
 *
 * */

int generate_message_id() {
	return last_message_id++;
}
int acknowledge_message(int socket, char * ip, int port, int message_id) {
	log_info(LOGGER, "FD %d ACK MSG %d", socket, message_id);
	client * from = add_or_get_client(socket, ip, port);

	int i;
	for(i=0 ; i<messages_index->elements_count ; i++) {
		broker_message * message = list_get(messages_index, i);
		if(message->message->header->message_id == message_id) {
			list_add(message->already_acknowledged, from);
			message_queue * queue = find_queue_by_name(message->message->header->queue);
			if(message->already_acknowledged->elements_count ==
					queue->subscribers->elements_count) {
				memory_partition * partition = message->payload_address_copy;
			}
			return OPT_OK;
		}
	}
	return OPT_FAILED;
}
int message_was_sent_to_susbcriber(broker_message * tmessage, client * subscriber) {
	int i;
	for(i=0 ; i<tmessage->already_sent->elements_count ; i++) {
		client * sent = list_get(tmessage->already_sent, i);
		if(is_same_client(sent, subscriber)) {
			return 1;
		}
	}
	return 0;
}
int destroy_unserialized(void * payload, pokemon_message_type type) {
	int i;
	switch(type) {
		case NEW_POKEMON:;
			new_pokemon_message * npm = payload;

			free(npm->pokemon);

			break;
		case APPEARED_POKEMON:;
			appeared_pokemon_message * apm = payload;

			free(apm->pokemon);

			break;
		case CATCH_POKEMON:;
			catch_pokemon_message * chpm = payload;

			free(chpm->pokemon);

			break;
		case CAUGHT_POKEMON:;
			//caught_pokemon_message * ctpm = payload;

			break;
		case GET_POKEMON:;
			get_pokemon_message * gpm = payload;

			free(gpm->pokemon);

			break;
		case LOCALIZED_POKEMON:;
			localized_pokemon_message * lpm = payload;

			free(lpm->pokemon);
			for(i=0 ; i<lpm->locations_counter ; i++) {
				location * l = list_get(lpm->locations, i);
				free(l);
			}
			list_destroy(lpm->locations);

			break;
	}
	free(payload);
	return 1;
}

/*
 *
 * CLIENTS *
 *
 * */

int is_same_client(client * c1, client * c2) {
	return	(/*c1->socket == c2->socket ||*/
			(c1->port == c2->port && strcmp(c2->ip, c1->ip) == 0));
}

client * add_or_get_client_and_lock_doing(int socket, char * ip, int port, int doing) {
	pthread_mutex_lock(&clients_mutex);
	int i;
	for(i=0 ; i<clients->elements_count ; i++) {
		client * oc = list_get(clients, i);
		if(oc->socket == socket ||
				(oc->port == port && strcmp(oc->ip, ip) == 0)) {
			oc->doing_internal_work = doing;
			pthread_mutex_unlock(&clients_mutex);
			return oc;
		}
	}
	client * c = build_client(socket, ip, port);
	list_add(clients, c);
	c->doing_internal_work = doing;
	pthread_mutex_unlock(&clients_mutex);
	return c;
}

client * add_or_get_client(int socket, char * ip, int port) {
	pthread_mutex_lock(&clients_mutex);
	int i;
	for(i=0 ; i<clients->elements_count ; i++) {
		client * oc = list_get(clients, i);
		if(oc->socket == socket ||
				(oc->port == port && strcmp(oc->ip, ip) == 0)) {
			pthread_mutex_unlock(&clients_mutex);
			return oc;
		}
	}
	client * c = build_client(socket, ip, port);
	list_add(clients, c);
	pthread_mutex_unlock(&clients_mutex);
	return c;
}

/*
 *
 * SIGNAL *
 *
 * */
void my_handler(int signum) {
    if (signum == SIGUSR1) {
        print_partitions_info();
    }
}
