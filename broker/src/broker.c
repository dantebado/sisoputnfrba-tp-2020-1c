#include <library/library.h>

/*/
 *
 * GLOBAL VARIABLES
 *
/*/
broker_config CONFIG;
t_list * queues;
int last_message_id;

t_list * clients;
sem_t * clients_mutex;

t_list * messages_index;
sem_t * messages_index_mutex;

void * main_memory;

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

//QUEUES
void init_queue(_message_queue_name);
message_queue * find_queue_by_name(_message_queue_name);
int add_message_to_queue(queue_message * message, _message_queue_name queue_name);
void queue_thread_function(message_queue * queue);

//SUBSCRIPTIONS
int subscribe_to_broker_queue(int socket, char * ip, int port, _message_queue_name queue_name);
int unsubscribe_from_broker_queue(int socket, char * ip, int port, _message_queue_name queue_name);
void unsubscribe_socket_from_all_queues(int fd);

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
	CONFIG.compating_freq = config_get_int_value(_CONFIG, "FRECUENCIA_COMPACTACION");
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

	init_memory();
	return;

	last_message_id = 0;

	log_info(LOGGER, "Configuration loaded");

	clients = list_create();
	clients_mutex = malloc(sizeof(sem_t));
	sem_init(clients_mutex, 0, 1);

	messages_index_mutex = malloc(sizeof(sem_t));
	sem_init(messages_index_mutex, 0, 1);

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
}

/*
 *
 * MEMORY *
 *
 * */
void init_memory() {
	log_info(LOGGER, "Initializing Memory of %d bytes", CONFIG.memory_size);
	log_info(LOGGER, "Min partition size %d bytes", CONFIG.partition_min_size);
	log_info(LOGGER, "%s, with %s replacement and %s seeking", enum_to_memory_alg(CONFIG.memory_alg),
			enum_to_memory_replacement_alg(CONFIG.remplacement_alg), enum_to_memory_selection_alg(CONFIG.seek_alg));

	main_memory = malloc(CONFIG.memory_size);
}

char * enum_to_memory_alg(_memory_alg alg) {
	switch(alg) {
		case PARTITIONS:
			return "PARTICIONES";
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
	queue->mutex = malloc(sizeof(sem_t));
	sem_init(queue->mutex, 0, 1);
	list_add(queues, queue);

	pthread_create(&queue->thread, NULL, queue_thread_function, queue);

	log_info(LOGGER, "Created List %s & Server Started", enum_to_queue_name(name));
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

int add_message_to_queue(queue_message * message, _message_queue_name queue_name) {
	message_queue * queue = find_queue_by_name(queue_name);
	if(queue == NULL) {
		return OPT_FAILED;
	}

	broker_message * final_message = malloc(sizeof(broker_message));
	final_message->message = message;
	final_message->already_sent = list_create();
	final_message->already_acknowledged = list_create();

	void * original_payload = message->payload;
	message->payload = serialize_message_payload(message)->payload;
	destroy_unserialized(original_payload, message->header->type);

	log_info(LOGGER, "Adding message %d to queue %s", message->header->message_id, enum_to_queue_name(queue_name));
	sem_wait(queue->mutex);
	sem_wait(messages_index_mutex);
	list_add(queue->messages, final_message);
	list_add(messages_index, final_message);
	sem_post(messages_index_mutex);
	sem_post(queue->mutex);
	log_info(LOGGER, "\t\tAdded");
	return OPT_OK;
}

void queue_thread_function(message_queue * queue) {
	int i, j, k;
	while(1) {
		sem_wait(queue->mutex);
		for(i=0 ; i<queue->messages->elements_count ; i++) {
			broker_message * tmessage = list_get(queue->messages, i);

			void * serialized_message = tmessage->message->payload;
			void * deserialized_message = deserialize_message_payload(tmessage->message->payload, tmessage->message->header->type);
			tmessage->message->is_serialized = false;
			tmessage->message->payload = deserialized_message;

			for(j=0 ; j<queue->subscribers->elements_count ; j++) {
				client * tsubscriber = list_get(queue->subscribers, j);

				if(!message_was_sent_to_susbcriber(tmessage, tsubscriber)) {

					log_info(LOGGER, "Sending MID %d, to socket %d",
							tmessage->message->header->message_id,
							tsubscriber->socket);

					sem_wait(tsubscriber->mutex);
					send_pokemon_message_with_id(tsubscriber->socket, tmessage->message, 0,
							tmessage->message->header->message_id,
							tmessage->message->header->correlative_id);
					sem_post(tsubscriber->mutex);

					list_add(tmessage->already_sent, tsubscriber);
				}

			}

			destroy_unserialized(deserialized_message, tmessage->message->header->type);
			tmessage->message->is_serialized = true;
			tmessage->message->payload = serialized_message;
		}
		sem_post(queue->mutex);
	}
}

/*
 *
 * SUBSCRIPTIONS *
 *
 * */

int subscribe_to_broker_queue(int socket, char * ip, int port, _message_queue_name queue_name) {
	log_info(LOGGER, "Socket %d subscribing to queue %d", socket, queue_name);

	client * sub = add_or_get_client(socket, ip, port);

	message_queue * queue = find_queue_by_name(queue_name);
	if(queue != NULL) {
		sem_wait(queue->mutex);
		list_add(queue->subscribers, sub);
		sem_post(queue->mutex);

		log_info(LOGGER, "Subscription successful");
		sem_wait(sub->mutex);
		send_int(socket, OPT_OK);
		sem_post(sub->mutex);
		return OPT_OK;
	} else {
		log_info(LOGGER, "Cannot find queue %d", queue_name);
		sem_wait(sub->mutex);
		send_int(socket, OPT_FAILED);
		sem_post(sub->mutex);
		return OPT_FAILED;
	}
}

int unsubscribe_from_broker_queue(int socket, char * ip, int port, _message_queue_name queue_name) {
	log_info(LOGGER, "Socket %d unsubscribing from queue %d", socket, queue_name);

	message_queue * queue = find_queue_by_name(queue_name);
	client * tc = add_or_get_client(socket, ip, port);

	if(queue != NULL) {
		int i, r = 0;
		sem_wait(queue->mutex);
		for(i=0 ; i<queue->subscribers->elements_count ; i++) {
			client * s = list_get(queue->subscribers, i);
			if(is_same_client(s, tc)) {
				r = 1;
				list_remove(queue->subscribers, i);
			}
		}
		sem_post(queue->mutex);
		if(r == 0) {
			log_info(LOGGER, "Unsubscription failed");
			sem_wait(tc->mutex);
			send_int(socket, OPT_FAILED);
			sem_post(tc->mutex);
			return OPT_FAILED;
		} else {
			log_info(LOGGER, "Unsubscription successful");
			sem_wait(tc->mutex);
			send_int(socket, OPT_OK);
			sem_post(tc->mutex);
			return OPT_OK;
		}
	} else {
		log_info(LOGGER, "Cannot find queue %d", queue_name);
		sem_wait(tc->mutex);
		send_int(socket, OPT_FAILED);
		sem_post(tc->mutex);
		return OPT_FAILED;
	}
}

void unsubscribe_socket_from_all_queues(int fd) {
	int i, j, k;
	for(i=0 ; i<queues->elements_count ; i++) {
		message_queue * queue = list_get(queues, i);
		sem_wait(queue->mutex);
		for(j=0 ; j<queue->subscribers->elements_count ; j++) {
			client * sub = list_get(queue->subscribers, j);
			if(sub->socket == fd) {
				list_remove(queue->subscribers, j);
				j = queue->subscribers->elements_count + 1;
			}
		}
		sem_post(queue->mutex);
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

/*
 *
 * SERVER *
 *
 * */

int server_function(int socket) {
	log_info(LOGGER, "Server Started");
	void new(int fd, char * ip, int port) {
	}

	void lost(int fd, char * ip, int port) {
		unsubscribe_socket_from_all_queues(fd);
	}

	void incoming(int fd, char * ip, int port, net_message_header * header) {
		switch(header->type) {
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

					client * from = add_or_get_client(fd, ip, port);

					sem_wait(from->mutex);
					send_int(fd, message_id);

					log_info(LOGGER, "Incoming message from socket %d", fd);

					queue_message * message = receive_pokemon_message(fd);

					if(message->header->message_id == message_id) {
						log_info(LOGGER, "Assigned MID %d", message->header->message_id);
					} else {
						log_info(LOGGER, "With preassigned MID %d", message->header->message_id);
					}

					print_pokemon_message(message);
					if(add_message_to_queue(message, message->header->queue) == OPT_OK) {

					} else {
						log_error(LOGGER, "Error adding message %d to queue", message->header->message_id);
					}
					sem_post(from->mutex);
				}
				break;
			case MESSAGE_ACK:;
				int message_id = recv_int(fd);
				acknowledge_message(fd, ip, port, message_id);
				break;
		}
	}

	start_server(socket, &new, &lost, &incoming);
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
	log_info(LOGGER, "Socket %d acknowledged message %d", socket, message_id);
	client * from = add_or_get_client(socket, ip, port);

	int i;
	for(i=0 ; i<messages_index->elements_count ; i++) {
		broker_message * message = list_get(messages_index, i);
		if(message->message->header->message_id == message_id) {
			list_add(message->already_acknowledged, from);
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
			caught_pokemon_message * ctpm = payload;

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
	return	(c1->socket == c2->socket ||
			(c1->port == c2->port && strcmp(c2->ip, c1->ip) == 0));
}

client * add_or_get_client(int socket, char * ip, int port) {
	int i;
	for(i=0 ; i<clients->elements_count ; i++) {
		client * oc = list_get(clients, i);
		if(oc->socket == socket ||
				(oc->port == port && strcmp(oc->ip, ip) == 0)) {
			return oc;
		}
	}
	client * c = build_client(socket, ip, port);
	sem_wait(clients_mutex);
	list_add(clients, c);
	sem_post(clients_mutex);
	return c;
}
