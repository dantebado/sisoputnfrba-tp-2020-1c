#ifndef LIBRARY_FUNCTIONS_H_
#define LIBRARY_FUNCTIONS_H_

int create_socket();
int bind_socket(int socket, int port);
int connect_socket(int socket, char * addr, int port);
int close_socket(int socket);

int start_server (int socket,
	void (*new_connection)(int fd, char * ip, int port),
	void (*lost_connection)(int fd, char * ip, int port),
	void (*incoming_message)(int fd, char * ip, int port, net_message_header * header));

/*GENERIC*/

int _send_stream(int socket, void * stream, int size);
void * _recv_stream(int socket, int size);
char * _recv_stream_add_string_end(int socket, int size);

//void custom_print(const char* message, ...);

/*PRIMITIVE*/

int send_char(int socket, char value);
int send_int(int socket, int value);
int send_long(int socket, long value);
int send_float(int socket, float value);
int send_double(int socket, double value);

char recv_char(int socket);
int recv_int(int socket);
long recv_long(int socket);
float recv_float(int socket);
double recv_double(int socket);

/*STRING & STRUCTS*/

int send_string(int socket, char * string);
char * recv_string(int socket);
int send_header(int socket, net_message_header * header);



/*BROKER*/
int ready_to_recieve(int broker_socket);
int subscribe_to_queue(int broker_socket, _message_queue_name queue);
int unsubscribe_from_queue(int broker_socket, _message_queue_name queue);
queue_message * send_pokemon_message(int socket, queue_message * message, int going_to_broker, int correlative_id);
queue_message * send_pokemon_message_with_id(int socket, queue_message * message, int going_to_broker, int message_id, int correlative_id);
queue_message * receive_pokemon_message(int socket);
queue_message * send_message_acknowledge(queue_message * message, int broker_socket);
void print_pokemon_message(queue_message * message);
void print_pokemon_message_by_printf(queue_message * message);
queue_message * new_pokemon_create(char * pokemon, uint32_t x, uint32_t y, uint32_t count);
queue_message * appeared_pokemon_create(char * pokemon, uint32_t x, uint32_t y);
queue_message * catch_pokemon_create(char * pokemon, uint32_t x, uint32_t y);
queue_message * caught_pokemon_create(uint32_t result);
queue_message * get_pokemon_create(char * pokemon);
queue_message * localized_pokemon_create(char * pokemon, t_list * locations);
localized_pokemon_message * localized_pokemon_add_location(localized_pokemon_message * message, location * tlocation);

location * location_create(int x, int y);

client * build_client(int socket, char * ip, int port);

char * enum_to_queue_name(_message_queue_name queue);
_message_queue_name queue_name_to_enum(char * name);

#endif /* LIBRARY_FUNCTIONS_H_ */
