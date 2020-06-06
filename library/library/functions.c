#include "library.h"

#define MAX_CONN 50

int create_socket() {
	int fd;
	if ((fd=socket(AF_INET, SOCK_STREAM, 0)) == -1) {
		log_error(LOGGER, "Error creating socket");
		return failed;
	} else {
		int option = 1;
		setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option));
		log_trace(LOGGER, "Created socket %d", fd);
		return fd;
	}
}

int bind_socket(int socket, int port) {
	struct sockaddr_in server;
	int bindres;

	server.sin_family = AF_INET;
	server.sin_port = htons(port);
	server.sin_addr.s_addr = INADDR_ANY;
	bzero(&(server.sin_zero), 8);

	bindres = bind(socket, (struct sockaddr*)&server,
		sizeof(struct sockaddr));
	if(bindres == -1) {
		log_error(LOGGER, "Error binding socket %d to port %d", socket, port);
		return failed;
	} else {
		log_trace(LOGGER, "Socket %d binded to port %d", socket, port);
	}
	return bindres;
}

int connect_socket(int socket, char * addr, int port) {
	struct hostent * he;
	struct sockaddr_in server;

	if ((he=gethostbyname(addr)) == NULL) {
		log_error(LOGGER, "Error getting host %s", addr);
		return failed;
	}

	server.sin_family = AF_INET;
	server.sin_port = htons(port);

	server.sin_addr = *((struct in_addr *)he->h_addr);

	bzero(&(server.sin_zero), 8);

	if (connect(socket, (struct sockaddr *)&server, sizeof(struct sockaddr)) == -1){
		log_error(LOGGER, "Error connecting socket %d to %s:%d", socket, addr, port);
		return failed;
	} else {
		log_trace(LOGGER, "Connected socket %d to %s:%d", socket, addr, port);
	}
	return success;
}

int close_socket(int socket) {
	close(socket);
	log_trace(LOGGER, "Closed socket %d", socket);
	return success;
}



/*GENERIC*/

int _send_stream(int socket, void * stream, int size) {
	return send(socket, stream, size, 0);
}

void * _recv_stream(int socket, int size) {
	void * mem = malloc(size);
	if(recv(socket, mem, size, 0) == size) {
		return mem;
	} else {
		log_error(LOGGER, "Error receiving %d bytes from socket %d", size, socket);
		return NULL;
	}
}

char * _recv_stream_add_string_end(int socket, int size) {
	char * mem = malloc(size + sizeof(char));
	if(recv(socket, mem, size, 0) == size) {
		mem[size/sizeof(char)] = '\0';
		return mem;
	} else {
		log_error(LOGGER, "Error receiving %d bytes from socket %d", size, socket);
		return NULL;
	}
}

/*PRIMITIVES*/

int send_char(int socket, char value) {
	if(_send_stream(socket, &value, sizeof(value)) == sizeof(value)) {
		log_trace(LOGGER, "Successfully sent %c to socket %d", value, socket);
	} else {
		log_error(LOGGER, "Error sending %c to socket %d", value, socket);
		return failed;
	}
	return sizeof(value);
}
int send_int(int socket, int value) {
	if(_send_stream(socket, &value, sizeof(value)) == sizeof(value)) {
		log_trace(LOGGER, "Successfully sent %d to socket %d", value, socket);
	} else {
		log_error(LOGGER, "Error sending %d to socket %d", value, socket);
		return failed;
	}
	return sizeof(value);
}
int send_long(int socket, long value) {
	if(_send_stream(socket, &value, sizeof(value)) == sizeof(value)) {
		log_trace(LOGGER, "Successfully sent %l to socket %d", value, socket);
	} else {
		log_error(LOGGER, "Error sending %l to socket %d", value, socket);
		return failed;
	}
	return sizeof(value);
}
int send_float(int socket, float value) {
	if(_send_stream(socket, &value, sizeof(value)) == sizeof(value)) {
		log_trace(LOGGER, "Successfully sent %f to socket %d", value, socket);
	} else {
		log_error(LOGGER, "Error sending %f to socket %d", value, socket);
		return failed;
	}
	return sizeof(value);
}
int send_double(int socket, double value) {
	if(_send_stream(socket, &value, sizeof(value)) == sizeof(value)) {
		log_trace(LOGGER, "Successfully sent %d to socket %d", value, socket);
	} else {
		log_error(LOGGER, "Error sending %d to socket %d", value, socket);
		return failed;
	}
	return sizeof(value);
}
int send_uint32(int socket, uint32_t value) {
	if(_send_stream(socket, &value, sizeof(uint32_t)) == sizeof(uint32_t)) {
		log_trace(LOGGER, "Successfully sent %lu to socket %d", value, socket);
	} else {
		log_error(LOGGER, "Error sending to %lu socket %d", value, socket);
		return failed;
	}
	return sizeof(value);
}

char recv_char(int socket) {
	char * v = _recv_stream(socket, sizeof(char));
	return *v;
}
int recv_int(int socket) {
	int * v = _recv_stream(socket, sizeof(int));
	return *v;
}
long recv_long(int socket) {
	long * v = _recv_stream(socket, sizeof(long));
	return *v;
}
float recv_float(int socket) {
	float * v = _recv_stream(socket, sizeof(float));
	return *v;
}
double recv_double(int socket) {
	double * v = _recv_stream(socket, sizeof(double));
	return *v;
}
uint32_t recv_uint32(int socket) {
	uint32_t * v = _recv_stream(socket, sizeof(uint32_t));
	return *v;
}

/*STRING & STRUCTS*/

int send_string(int socket, char * string) {
	int size = (strlen(string) + 1) / sizeof(char);
	send_int(socket, size);
	if(_send_stream(socket, string, size) == size) {
		log_trace(LOGGER, "Successfully sent %s to socket %d", string, socket);
	} else {
		log_error(LOGGER, "Error sending %s to socket %d", string, socket);
		return failed;
	}
	return size;
}
char * recv_string(int socket) {
	int size = recv_int(socket);
	char * string = _recv_stream(socket, size * sizeof(char));
	return string;
}

int send_header(int socket, net_message_header * header) {
	return _send_stream(socket, header, sizeof(net_message_header));
}

int start_server(int socket,
		void (*new_connection)(int fd, char * ip, int port),
		void (*lost_connection)(int fd, char * ip, int port),
		void (*incoming_message)(int fd, char * ip, int port, net_message_header * header)) {

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

					new_connection(new_socket, string_duplicate(str), address.sin_port);
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

					if ((bytesread = read(client_socket, incoming, sizeof(net_message_header))) <= 0) {
						getpeername(sd , (struct sockaddr*)&address , (socklen_t*)&addrlen);

						char str[INET_ADDRSTRLEN];
						inet_ntop(AF_INET, &(address.sin_addr), str, INET_ADDRSTRLEN);

						lost_connection(client_socket, string_duplicate(str), address.sin_port);

						close(sd);
						client_socket_array[i] = 0;
						log_trace(LOGGER, "Closed and freed connection for index %d, socket %d", i, sd);
					} else {

						char str[INET_ADDRSTRLEN];
						inet_ntop(AF_INET, &(address.sin_addr), str, INET_ADDRSTRLEN);

						incoming_message(client_socket, string_duplicate(str), address.sin_port, incoming);
					}
					free(incoming);
				}
			}
		}
	}
}


/*BROKER*/
int subscribe_to_queue(int broker_socket, _message_queue_name queue) {
	net_message_header header;
	header.type = NEW_SUBSCRIPTION;
	send_header(broker_socket, &header);

	send_int(broker_socket, queue);
	int result = recv_int(broker_socket);
	if(result != OPT_OK) {
		log_error(LOGGER, "Error Subscribing");
	} else {
		log_info(LOGGER, "Success Subscribing");
	}
	return result;
}
int unsubscribe_from_queue(int broker_socket, _message_queue_name queue) {
	net_message_header header;
	header.type = DOWN_SUBSCRIPTION;
	send_header(broker_socket, &header);

	send_int(broker_socket, queue);
	int result = recv_int(broker_socket);
	if(result != OPT_OK) {
		log_error(LOGGER, "Error Subscribing");
	} else {
		log_info(LOGGER, "Success Subscribing");
	}
	return result;
}
queue_message * send_pokemon_message(int socket, queue_message * message, int going_to_broker, int correlative_id) {
	return send_pokemon_message_with_id(socket, message, going_to_broker, -1, correlative_id);
}
queue_message * send_pokemon_message_with_id(int socket, queue_message * message, int going_to_broker, int message_id, int correlative_id) {
	net_message_header header;
	header.type = NEW_MESSAGE;
	send_header(socket, &header);

	if(going_to_broker) {
		message->header->message_id = recv_int(socket);
	}
	if(message_id != -1 || !going_to_broker) {
		message->header->message_id = message_id;
	}

	message->header->correlative_id = correlative_id;

	_send_stream(socket, message->header, sizeof(pokemon_message_header));
	send_int(socket, message->is_serialized);

	switch(message->header->type) {
		case NEW_POKEMON:;
			new_pokemon_message * npm = message->payload;

			send_uint32(socket, npm->name_length);
			_send_stream(socket, npm->pokemon, npm->name_length);

			send_uint32(socket, npm->x);
			send_uint32(socket, npm->y);
			send_uint32(socket, npm->count);
			break;
		case APPEARED_POKEMON:;
			appeared_pokemon_message * apm = message->payload;

			send_uint32(socket, apm->name_length);
			_send_stream(socket, apm->pokemon, apm->name_length);

			send_uint32(socket, apm->x);
			send_uint32(socket, apm->y);
			break;
		case CATCH_POKEMON:;
			catch_pokemon_message * chpm = message->payload;

			send_uint32(socket, chpm->name_length);
			_send_stream(socket, chpm->pokemon, chpm->name_length);

			send_uint32(socket, chpm->x);
			send_uint32(socket, chpm->y);
			break;
		case CAUGHT_POKEMON:;
			caught_pokemon_message * ctpm = message->payload;

			send_uint32(socket, ctpm->result);
			break;
		case GET_POKEMON:;
			get_pokemon_message * gpm = message->payload;

			send_uint32(socket, gpm->name_length);
			_send_stream(socket, gpm->pokemon, gpm->name_length);
			break;
		case LOCALIZED_POKEMON:;
			localized_pokemon_message * lpm = message->payload;

			send_uint32(socket, lpm->name_length);
			_send_stream(socket, lpm->pokemon, lpm->name_length);

			lpm->locations_counter = list_size(lpm->locations);
			send_uint32(socket, lpm->locations_counter);
			int i;
			for(i=0 ; i<lpm->locations_counter ; i++) {
				location * tlocation = list_get(lpm->locations, i);
				send_uint32(socket, tlocation->x);
				send_uint32(socket, tlocation->y);
			}
			break;
	}

	return message;
}

queue_message * receive_pokemon_message(int socket) {
	queue_message * message = malloc(sizeof(queue_message));
	message->header = _recv_stream(socket, sizeof(pokemon_message_header));

	message->is_serialized = recv_int(socket);

	switch(message->header->type) {
		case NEW_POKEMON:;
			new_pokemon_message * npm = malloc(sizeof(new_pokemon_message));
			message->payload = npm;

			npm->name_length = recv_uint32(socket);
			npm->pokemon = _recv_stream_add_string_end(socket, npm->name_length * sizeof(char));
			npm->x = recv_uint32(socket);
			npm->y = recv_uint32(socket);
			npm->count = recv_uint32(socket);

			break;
		case APPEARED_POKEMON:;
			appeared_pokemon_message * apm = malloc(sizeof(appeared_pokemon_message));
			message->payload = apm;

			apm->name_length = recv_uint32(socket);
			apm->pokemon = _recv_stream_add_string_end(socket, apm->name_length * sizeof(char));
			apm->x = recv_uint32(socket);
			apm->y = recv_uint32(socket);
			break;
		case CATCH_POKEMON:;
			catch_pokemon_message * chpm = malloc(sizeof(catch_pokemon_message));
			message->payload = chpm;

			chpm->name_length = recv_uint32(socket);
			chpm->pokemon = _recv_stream_add_string_end(socket, chpm->name_length * sizeof(char));
			chpm->x = recv_uint32(socket);
			chpm->y = recv_uint32(socket);
			break;
		case CAUGHT_POKEMON:;
			caught_pokemon_message * ctpm = malloc(sizeof(caught_pokemon_message));
			message->payload = ctpm;
			ctpm->result = recv_uint32(socket);
			break;
		case GET_POKEMON:;
			get_pokemon_message * gpm = malloc(sizeof(get_pokemon_message));
			message->payload = gpm;

			gpm->name_length = recv_uint32(socket);
			gpm->pokemon = _recv_stream_add_string_end(socket, gpm->name_length * sizeof(char));
			break;
		case LOCALIZED_POKEMON:;
			localized_pokemon_message * lpm = malloc(sizeof(localized_pokemon_message));
			message->payload = lpm;

			lpm->name_length = recv_uint32(socket);
			lpm->pokemon = _recv_stream_add_string_end(socket, lpm->name_length * sizeof(char));
			lpm->locations_counter = recv_uint32(socket);
			lpm->locations = list_create();
			int i;
			for(i=0 ; i<lpm->locations_counter ; i++) {
				location * tlocation = malloc(sizeof(location));
				tlocation->x = recv_uint32(socket);
				tlocation->y = recv_uint32(socket);
				list_add(lpm->locations, tlocation);
			}
			break;
	}

	return message;
}

queue_message * send_message_acknowledge(queue_message * message, int broker_socket) {
	net_message_header header;
	header.type = MESSAGE_ACK;
	send_header(broker_socket, &header);

	send_int(broker_socket, message->header->message_id);

	return message;
}

void print_pokemon_message_by_printf(queue_message * message) {
	if(message->is_serialized) {
		printf("Cannot display message %d. Its serialized\n", message->header->message_id);
		return;
	}
	char * name;
	switch(message->header->type) {
		case NEW_POKEMON:;
			new_pokemon_message * npm = message->payload;

			name = malloc(sizeof(char) * (npm->name_length + 1));
			memcpy(name, npm->pokemon, sizeof(char) * (npm->name_length + 1));
			name[npm->name_length] = '\0';

			printf("NEW POKEMON %s @[%d :: %d] COUNT %d\n",
					name,
					npm->x,
					npm->y,
					npm->count);

			free(name);
			break;
		case APPEARED_POKEMON:;
			appeared_pokemon_message * apm = message->payload;

			name = malloc(sizeof(char) * (apm->name_length + 1));
			memcpy(name, apm->pokemon, sizeof(char) * (apm->name_length + 1));
			name[apm->name_length] = '\0';

			printf("APPEARED POKEMON %s @[%d :: %d]\n",
					name,
					apm->x,
					apm->y);

			free(name);
			break;
		case CATCH_POKEMON:;
			catch_pokemon_message * chpm = message->payload;

			name = malloc(sizeof(char) * (chpm->name_length + 1));
			memcpy(name, chpm->pokemon, sizeof(char) * (chpm->name_length + 1));
			name[chpm->name_length] = '\0';

			printf("CATCH POKEMON %s @[%d :: %d]\n",
					name,
					chpm->x,
					chpm->y);

			free(name);
			break;
		case CAUGHT_POKEMON:;
			caught_pokemon_message * ctpm = message->payload;
			printf("CAUGHT POKEMON [%d]\n",
					ctpm->result);
			break;
		case GET_POKEMON:;
			get_pokemon_message * gpm = message->payload;

			name = malloc(sizeof(char) * (gpm->name_length + 1));
			memcpy(name, gpm->pokemon, sizeof(char) * (gpm->name_length + 1));
			name[gpm->name_length] = '\0';

			printf("GET POKEMON %s\n",
					name);

			free(name);
			break;
		case LOCALIZED_POKEMON:;
			localized_pokemon_message * lpm = message->payload;

			name = malloc(sizeof(char) * (lpm->name_length + 1));
			memcpy(name, lpm->pokemon, sizeof(char) * (lpm->name_length + 1));
			name[lpm->name_length] = '\0';

			int i;
			printf("LOCALIZED POKEMON %s @[%d] LOCATIONS ::\n",
					name,
					lpm->locations_counter);
			for(i=0 ; i<lpm->locations_counter ; i++) {
				location * tlocation = list_get(lpm->locations, i);
				printf("\t\t@[%d :: %d]\n", tlocation->x, tlocation->y);
			}

			free(name);
			break;
	}
}

void print_pokemon_message(queue_message * message) {
	if(message->is_serialized) {
		log_info(LOGGER, "Cannot display message %d. Its serialized", message->header->message_id);
		return;
	}
	char * name;
	switch(message->header->type) {
		case NEW_POKEMON:;
			new_pokemon_message * npm = message->payload;

			name = malloc(sizeof(char) * (npm->name_length + 1));
			memcpy(name, npm->pokemon, sizeof(char) * (npm->name_length + 1));
			name[npm->name_length] = '\0';

			log_info(LOGGER, "NEW POKEMON %s @[%d :: %d] COUNT %d",
					name,
					npm->x,
					npm->y,
					npm->count);

			free(name);
			break;
		case APPEARED_POKEMON:;
			appeared_pokemon_message * apm = message->payload;

			name = malloc(sizeof(char) * (apm->name_length + 1));
			memcpy(name, apm->pokemon, sizeof(char) * (apm->name_length + 1));
			name[apm->name_length] = '\0';

			log_info(LOGGER, "APPEARED POKEMON %s @[%d :: %d]",
					name,
					apm->x,
					apm->y);

			free(name);
			break;
		case CATCH_POKEMON:;
			catch_pokemon_message * chpm = message->payload;

			name = malloc(sizeof(char) * (chpm->name_length + 1));
			memcpy(name, chpm->pokemon, sizeof(char) * (chpm->name_length + 1));
			name[chpm->name_length] = '\0';

			log_info(LOGGER, "CATCH POKEMON %s @[%d :: %d]",
					name,
					chpm->x,
					chpm->y);

			free(name);
			break;
		case CAUGHT_POKEMON:;
			caught_pokemon_message * ctpm = message->payload;
			log_info(LOGGER, "CAUGHT POKEMON [%d]",
					ctpm->result);
			break;
		case GET_POKEMON:;
			get_pokemon_message * gpm = message->payload;

			name = malloc(sizeof(char) * (gpm->name_length + 1));
			memcpy(name, gpm->pokemon, sizeof(char) * (gpm->name_length + 1));
			name[gpm->name_length] = '\0';

			log_info(LOGGER, "GET POKEMON %s",
					name);

			free(name);
			break;
		case LOCALIZED_POKEMON:;
			localized_pokemon_message * lpm = message->payload;

			name = malloc(sizeof(char) * (lpm->name_length + 1));
			memcpy(name, lpm->pokemon, sizeof(char) * (lpm->name_length + 1));
			name[lpm->name_length] = '\0';

			int i;
			log_info(LOGGER, "LOCALIZED POKEMON %s @[%d] LOCATIONS ::",
					name,
					lpm->locations_counter);
			for(i=0 ; i<lpm->locations_counter ; i++) {
				location * tlocation = list_get(lpm->locations, i);
				log_info(LOGGER, "\t\t@[%d :: %d]", tlocation->x, tlocation->y);
			}

			free(name);
			break;
	}
}

location * location_create(int x, int y) {
	location * tlocation = malloc(sizeof(location));
	tlocation->x = x;
	tlocation->y = y;
	return tlocation;
}

client * build_client(int socket, char * ip, int port) {
	client * c = malloc(sizeof(client));
	c->ip = ip;
	c->socket = socket;
	c->port = port;
	pthread_mutex_init(&c->mutex, NULL);
	return c;
}

queue_message * new_pokemon_create(char * pokemon, uint32_t x, uint32_t y, uint32_t count) {
	queue_message * mymessage = malloc(sizeof(queue_message));
	mymessage->header = malloc(sizeof(pokemon_message_header));

	mymessage->header->correlative_id = -1;
	mymessage->header->message_id = -1;
	mymessage->header->queue = QUEUE_NEW_POKEMON;
	mymessage->header->type = NEW_POKEMON;

	mymessage->is_serialized = false;

	new_pokemon_message * npm = malloc(sizeof(new_pokemon_message));
	npm->pokemon = string_duplicate(pokemon);
	npm->name_length = strlen(pokemon);
	npm->x = x;
	npm->y = y;
	npm->count = count;

	mymessage->payload = npm;

	return mymessage;
}
queue_message * appeared_pokemon_create(char * pokemon, uint32_t x, uint32_t y) {
	queue_message * mymessage = malloc(sizeof(queue_message));
	mymessage->header = malloc(sizeof(pokemon_message_header));

	mymessage->header->correlative_id = -1;
	mymessage->header->message_id = -1;
	mymessage->header->queue = QUEUE_APPEARED_POKEMON;
	mymessage->header->type = APPEARED_POKEMON;

	mymessage->is_serialized = false;

	appeared_pokemon_message * apm = malloc(sizeof(appeared_pokemon_message));
	apm->pokemon = string_duplicate(pokemon);
	apm->name_length = strlen(pokemon);
	apm->x = x;
	apm->y = y;

	mymessage->payload = apm;

	return mymessage;
}
queue_message * catch_pokemon_create(char * pokemon, uint32_t x, uint32_t y) {
	queue_message * mymessage = malloc(sizeof(queue_message));
	mymessage->header = malloc(sizeof(pokemon_message_header));

	mymessage->header->correlative_id = -1;
	mymessage->header->message_id = -1;
	mymessage->header->queue = QUEUE_CATCH_POKEMON;
	mymessage->header->type = CATCH_POKEMON;

	mymessage->is_serialized = false;

	catch_pokemon_message * chpm = malloc(sizeof(catch_pokemon_message));
	chpm->pokemon = string_duplicate(pokemon);
	chpm->name_length = strlen(pokemon);
	chpm->x = x;
	chpm->y = y;

	mymessage->payload = chpm;

	return mymessage;
}
queue_message * caught_pokemon_create(uint32_t result) {
	queue_message * mymessage = malloc(sizeof(queue_message));
	mymessage->header = malloc(sizeof(pokemon_message_header));

	mymessage->header->correlative_id = -1;
	mymessage->header->message_id = -1;
	mymessage->header->queue = QUEUE_CAUGHT_POKEMON;
	mymessage->header->type = CAUGHT_POKEMON;

	mymessage->is_serialized = false;

	caught_pokemon_message * ctpm = malloc(sizeof(caught_pokemon_message));
	ctpm->result = result;

	mymessage->payload = ctpm;

	return mymessage;
}
queue_message * get_pokemon_create(char * pokemon) {
	queue_message * mymessage = malloc(sizeof(queue_message));
	mymessage->header = malloc(sizeof(pokemon_message_header));

	mymessage->header->correlative_id = -1;
	mymessage->header->message_id = -1;
	mymessage->header->queue = QUEUE_GET_POKEMON;
	mymessage->header->type = GET_POKEMON;

	mymessage->is_serialized = false;

	get_pokemon_message * gpm = malloc(sizeof(get_pokemon_message));
	gpm->pokemon = string_duplicate(pokemon);
	gpm->name_length = strlen(pokemon);

	mymessage->payload = gpm;

	return mymessage;
}
queue_message * localized_pokemon_create(char * pokemon, t_list * locations) {
	queue_message * mymessage = malloc(sizeof(queue_message));
	mymessage->header = malloc(sizeof(pokemon_message_header));

	mymessage->header->correlative_id = -1;
	mymessage->header->message_id = -1;
	mymessage->header->queue = QUEUE_LOCALIZED_POKEMON;
	mymessage->header->type = LOCALIZED_POKEMON;

	mymessage->is_serialized = false;

	localized_pokemon_message * lpm = malloc(sizeof(localized_pokemon_message));
	lpm->pokemon = string_duplicate(pokemon);
	lpm->name_length = strlen(pokemon);
	lpm->locations_counter = locations->elements_count;
	lpm->locations = locations;

	mymessage->payload = lpm;

	return mymessage;
}
localized_pokemon_message * localized_pokemon_add_location(localized_pokemon_message * message, location * tlocation) {
	message->locations_counter++;
	list_add(message->locations, tlocation);
	return message;
}

char * enum_to_queue_name(_message_queue_name queue) {
	switch(queue) {
		case QUEUE_NEW_POKEMON:
			return "NEW_POKEMON";
		case QUEUE_APPEARED_POKEMON:
			return "APPEARED_POKEMON";
		case QUEUE_CATCH_POKEMON:
			return "CATCH_POKEMON";
		case QUEUE_CAUGHT_POKEMON:
			return "CAUGHT_POKEMON";
		case QUEUE_GET_POKEMON:
			return "GET_POKEMON";
		case QUEUE_LOCALIZED_POKEMON:
			return "LOCALIZED_POKEMON";
	}
	return "Not Found";
}
_message_queue_name queue_name_to_enum(char * name) {
	if(strcmp(name, "NEW_POKEMON") == 0) {
		return QUEUE_NEW_POKEMON;
	} else if(strcmp(name, "APPEARED_POKEMON") == 0) {
		return QUEUE_APPEARED_POKEMON;
	} else if(strcmp(name, "CATCH_POKEMON") == 0) {
		return QUEUE_CATCH_POKEMON;
	} else if(strcmp(name, "CAUGHT_POKEMON") == 0) {
		return QUEUE_CAUGHT_POKEMON;
	} else if(strcmp(name, "GET_POKEMON") == 0) {
		return QUEUE_GET_POKEMON;
	} else if(strcmp(name, "LOCALIZED_POKEMON") == 0) {
		return QUEUE_LOCALIZED_POKEMON;
	}
	return -1;
}
/*
void custom_print(const char* message, ...){
	fflush(stdin);
	fflush(stdout);

	va_list var;
	va_start(var, message);
	vprintf(message, var);
	va_end(var);

	fflush(stdin);
	fflush(stdout);
}
*/
