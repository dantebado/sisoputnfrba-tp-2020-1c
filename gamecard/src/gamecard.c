#include <library/library.h>

//GLOBAL VARIABLES
gamecard_config CONFIG;

tall_grass_fs * tall_grass;
pthread_mutex_t file_table_mutex = PTHREAD_MUTEX_INITIALIZER;

typedef struct {
	char * path;
	char * filename;
	int references;
	int last_reference;
} file_table_entry;
t_list * file_table;

int internal_broker_need;
pthread_mutex_t broker_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t op_mutex = PTHREAD_MUTEX_INITIALIZER;
int has_broker_connection = false;

//PROTOTYPES
void * process_pokemon_message(gamecard_thread_payload * payload);
void setup(int argc, char **argv);
int broker_server_function();
int server_function();
void save_bitmap();
void debug_bitmap();
void setup_tall_grass();
t_list * find_free_blocks(int count);
int aux_round_up(int int_value, float float_value);
pokemon_file_serialized * serialize_pokemon_file(pokemon_file * pf);
pokemon_file * deserialize_pokemon_file(char * contents);
pokemon_file_line * find_pokemon_file_line_for_location(pokemon_file * pf, int x, int y);
pokemon_file * pokemon_file_create();
pokemon_file_line * pokemon_file_line_create(int x, int y, int q);

char * int_to_string(int number);

int open_file(char * path, char * filename, int tid);
int close_file(char * path, char * filename, int tid);
void * write_payload_in_file(char * path, char * filename, char * payload, int payload_size, int tid);
char * read_file_content(char * path, char * filename, int tid);

int main(int argc, char **argv) {
	setup(argc, argv);

	return EXIT_SUCCESS;
}

void * process_pokemon_message(gamecard_thread_payload * payload) {
	queue_message * message = payload->message;
	int from_broker = payload->from_broker;

	int tid = syscall(__NR_gettid);

	internal_broker_need = 1;
	print_pokemon_message(message);
	switch(message->header->type) {
		case NEW_POKEMON:;
			{
				new_pokemon_message * npm = message->payload;

				char * fileContent = read_file_content("/Pokemon", npm->pokemon, tid);

				pokemon_file * pf = deserialize_pokemon_file(fileContent);
				pokemon_file_line * line = find_pokemon_file_line_for_location(pf, npm->x, npm->y);
				line->quantity += npm->count;

				pokemon_file_serialized * serialized = serialize_pokemon_file(pf);
				write_payload_in_file("/Pokemon", npm->pokemon,
						serialized->content,
						serialized->length,
						tid);

				close_file("/Pokemon", npm->pokemon, tid);

				queue_message * response = appeared_pokemon_create(npm->pokemon, npm->x, npm->y);
				print_pokemon_message(response);
				if(has_broker_connection == true) {
					log_info(LOGGER, "Sending answer");
					send_pokemon_message(CONFIG.broker_socket, response, 1, -1);
				} else {
					log_error(LOGGER, "Cannot notify broker");
				}
				log_info(LOGGER, "  Saved new pokemon location");
			}
			break;
		case CATCH_POKEMON:;
			{
				catch_pokemon_message * chpm = message->payload;

				log_info(LOGGER, "Attempt to catch pokemon %s at [%d::%d]", chpm->pokemon, chpm->x, chpm->y);
				char * fileContent = read_file_content("/Pokemon", chpm->pokemon, tid);

				if(fileContent != NULL) {
					pokemon_file * pf = deserialize_pokemon_file(fileContent);
					pokemon_file_line * line = find_pokemon_file_line_for_location(pf, chpm->x, chpm->y);

					if(line->quantity == 0) {
						queue_message * response = caught_pokemon_create(0);
						print_pokemon_message(response);
						if (has_broker_connection == true){
							log_info(LOGGER, "  Sending failed answer");
							response = send_pokemon_message(CONFIG.broker_socket, response, 1, message->header->message_id);
							log_info(LOGGER, "  Response was assigned MID %d", response->header->message_id, response->header->correlative_id);
						}
						log_info(LOGGER, "Pokemon catch failed");
					} else {
						line->quantity--;

						pokemon_file_serialized * serialized = serialize_pokemon_file(pf);
						write_payload_in_file("/Pokemon", chpm->pokemon,
								serialized->content,
								serialized->length,
								tid);

						queue_message * response = caught_pokemon_create(1);
						print_pokemon_message(response);
						if (has_broker_connection == true){
							log_info(LOGGER, "  Sending caught answer");
							send_pokemon_message(CONFIG.broker_socket, response, 1, message->header->message_id);
							log_info(LOGGER, "  Response was assigned MID %d", response->header->message_id, response->header->correlative_id);
						}
						log_info(LOGGER, "Pokemon caught successfully");
					}
				}

				close_file("/Pokemon", chpm->pokemon, tid);
			}
			break;
		case GET_POKEMON:;
			{
				get_pokemon_message * gpm = message->payload;

				char * fileContent = read_file_content("/Pokemon", gpm->pokemon, tid);
				close_file("/Pokemon", gpm->pokemon, tid);

				pokemon_file * pf = deserialize_pokemon_file(fileContent);

				t_list* listaPosiciones = list_create();
				int i;
				for(i=0; i < pf->locations->elements_count; i++){
					pokemon_file_line * pfl = list_get(pf->locations, i);
					list_add(listaPosiciones, pfl->position);
				}
				queue_message * response = localized_pokemon_create(gpm->pokemon, listaPosiciones);
				print_pokemon_message(response);
				if(listaPosiciones->elements_count > 0) {
					if (has_broker_connection == true){
						log_info(LOGGER, "Sending answer");
						send_pokemon_message(CONFIG.broker_socket, response, 1, message->header->message_id);
					}
				} else {
					log_info(LOGGER, "  No positions are available for this pokemon");
				}
				log_info(LOGGER, "  Answer sent to broker");
			}
	}

	if(from_broker == 1 && has_broker_connection) {
		already_processed(CONFIG.broker_socket);
	}

	internal_broker_need = 0;
	pthread_mutex_unlock(&op_mutex);
	pthread_mutex_unlock(&broker_mutex);

	return NULL;
}

void setup(int argc, char **argv) {
	char * cfg_path = string_new();
	string_append(&cfg_path, (argc > 1) ? argv[1] : "gamecard");

	char * log_path = string_duplicate(cfg_path);
	string_append(&log_path, ".log");

	string_append(&cfg_path, ".cfg");
	_CONFIG = config_create(cfg_path);

	LOGGER = log_create(log_path, (argc > 1) ? argv[1] : "gamecard", true, LOG_LEVEL_INFO);

	CONFIG.retry_time_conn = config_get_int_value(_CONFIG, "TIEMPO_DE_REINTENTO_CONEXION");
	CONFIG.retry_time_op = config_get_int_value(_CONFIG, "TIEMPO_DE_REINTENTO_OPERACION");
	CONFIG.tallgrass_mounting_point = config_get_string_value(_CONFIG, "PUNTO_MONTAJE_TALLGRASS");
	CONFIG.broker_ip = config_get_string_value(_CONFIG, "IP_BROKER");
	CONFIG.broker_port = config_get_int_value(_CONFIG, "PUERTO_BROKER");
	CONFIG.gamecard_port = config_get_int_value(_CONFIG, "PUERTO_GAMECARD");

	log_info(LOGGER, "Configuration Loaded");

	internal_broker_need = 0;
	file_table = list_create();
	setup_tall_grass();

	if((CONFIG.internal_socket = create_socket()) == failed) {
		log_info(LOGGER, "Cannot create socket");
		return;
	}
	if(bind_socket(CONFIG.internal_socket, CONFIG.gamecard_port) == failed) {
		log_info(LOGGER, "Cannot bind internal socket");
		return;
	}
	pthread_create(&CONFIG.server_thread, NULL, server_function, NULL);
	pthread_create(&CONFIG.broker_thread, NULL, broker_server_function, NULL);

	pthread_join(CONFIG.server_thread, NULL);
	pthread_join(CONFIG.broker_thread, NULL);
}

char * build_full_path(char * path, char * filename) {
	char * fp = string_new();

	string_append(&fp, path);
	string_append(&fp, "/");
	string_append(&fp, filename);

	return fp;
}

file_table_entry * find_entry(char * path, char * filename) {
	for(int i=0 ; i<file_table->elements_count ; i++) {
		file_table_entry * te = list_get(file_table, i);
		if(
			string_equals_ignore_case(path, te->path) &&
			string_equals_ignore_case(filename, te->filename)
		) {
			return te;
		}
	}

	file_table_entry * te = malloc(sizeof(file_table_entry));
	te->references = 0;
	te->last_reference = 0;
	te->path = string_duplicate(path);
	te->filename = string_duplicate(filename);

	list_add(file_table, te);

	return te;
}

/* *
 *
 * -1 YA ABIERTO
 *
 * */
int open_file(char * path, char * filename, int tid) {
	pthread_mutex_lock(&file_table_mutex);

	file_table_entry * entry = find_entry(path, filename);

	if(entry->references > 0 && entry->last_reference != tid && entry->last_reference != 0) {
		pthread_mutex_unlock(&file_table_mutex);
		return -1;
	}

	if(entry->last_reference != tid) {
		entry->last_reference = tid;
		entry->references++;
	}

	pthread_mutex_unlock(&file_table_mutex);
	return 0;
}

int close_file(char * path, char * filename, int tid) {
	pthread_mutex_lock(&file_table_mutex);

	file_table_entry * entry = find_entry(path, filename);

	if(entry->last_reference == tid) {
		entry->last_reference = 0;
		entry->references--;
		pthread_mutex_unlock(&file_table_mutex);
		return 0;
	} else { }

	pthread_mutex_unlock(&file_table_mutex);
	return -1;
}

t_config * get_config_for_file(char * path, char * filename) {
	char * full_path = build_full_path(path, filename);
	char * metadata_path = malloc(255);
	metadata_path[0] = '\0';

	char * metadata_directory = malloc(255);
	metadata_directory[0] = '\0';

	string_append(&metadata_path, CONFIG.tallgrass_mounting_point);
	string_append(&metadata_directory, CONFIG.tallgrass_mounting_point);
	string_append(&metadata_path, "/Files");
	string_append(&metadata_directory, "/Files");
	string_append(&metadata_path, full_path);
	string_append(&metadata_directory, full_path);
	string_append(&metadata_path, "/Metadata.bin");

	int just_created = 0;
	FILE * fmetadata = fopen(metadata_path, "r");
	if(fmetadata == NULL) {
		char * mkdircommand = malloc(6 + strlen(metadata_directory));
		mkdircommand[0] = '\0';
		string_append(&mkdircommand, "mkdir ");
		string_append(&mkdircommand, metadata_directory);
		system(mkdircommand);

		fmetadata = fopen(metadata_path, "w");
		just_created = 1;
	}
	fclose(fmetadata);
	t_config * cfg = config_create(metadata_path);
	if(just_created == 1) {
		config_set_value(cfg, "DIRECTORY", "N");
		config_set_value(cfg, "SIZE", "0");
		config_set_value(cfg, "BLOCKS", "[]");
		config_set_value(cfg, "OPEN", "N");
		config_save(cfg);
	}
	return cfg;
}

void * write_payload_in_file(char * path, char * filename, char * payload, int payload_size, int tid) {
	int open_result = open_file(path, filename, tid);
	if(open_result != 0) {
		log_info(LOGGER, "File could not be opened. Retrying in a few seconds...");
		sleep(CONFIG.retry_time_op);
		return write_payload_in_file(path, filename, payload, payload_size, tid);
	}

	t_config * config = get_config_for_file(path, filename);

	int necessary_blocks = aux_round_up(payload_size / tall_grass->block_size,
								payload_size / (float)tall_grass->block_size);
	int existing_size = config_get_int_value(config, "SIZE");

	char ** allocated_blocks = config_get_array_value(config, "BLOCKS");

	int existing_blocks = aux_round_up(existing_size / tall_grass->block_size,
								existing_size / (float)tall_grass->block_size);
	int blocks_left = necessary_blocks - existing_blocks;

	t_list * blocks = list_create();

	int o;
	for(o=0 ; o<existing_blocks ; o++) {
		char * string_block = allocated_blocks[o];
		int int_block = atoi(string_block);
		int * pint = malloc(sizeof(int));
		memcpy(pint, &int_block, sizeof(int));
		list_add(blocks, pint);
	}

	if(blocks_left > 0) {
		t_list * more_blocks = find_free_blocks(blocks_left);
		if(more_blocks == NULL) {
			log_error(LOGGER, "Not enough free blocks");
			return false;
		}
		int k;
		for(k=0 ; k<blocks_left ; k++) {
			int * some_block = list_get(more_blocks, k);
			list_add(blocks, some_block);
		}
	}

	char * blocks_for_config = string_new();
	string_append(&blocks_for_config, "[");

	int written_bytes = 0, gonzalo = 0;
	do {
		int * _b = list_get(blocks, gonzalo);
		int bn = *_b;

		char * this_block_as_string = int_to_string(bn);

		if(gonzalo != 0) {
			string_append(&blocks_for_config, ",");
		}
		string_append(&blocks_for_config, this_block_as_string);

		int this_block_size = payload_size - written_bytes;
		if(this_block_size > tall_grass->block_size) {
			this_block_size = tall_grass->block_size;
		}

		char * block_payload = malloc(this_block_size);
		memcpy(block_payload, payload + written_bytes, this_block_size);

		char * this_block_metadata_path = string_new();

		string_append(&this_block_metadata_path, CONFIG.tallgrass_mounting_point);
		string_append(&this_block_metadata_path, "/Blocks/");
		string_append(&this_block_metadata_path, this_block_as_string);
		string_append(&this_block_metadata_path, ".bin");

		FILE * block_file = fopen(this_block_metadata_path, "w");
		fwrite(block_payload, this_block_size, 1, block_file);
		fclose(block_file);

		bitarray_set_bit(tall_grass->bitmap, bn);

		gonzalo++;
		written_bytes += this_block_size;
	} while (written_bytes < payload_size);
	string_append(&blocks_for_config, "]");

	while(gonzalo < blocks->elements_count) {
		int * v = list_get(blocks, gonzalo);
		int vv = *v;
		bitarray_clean_bit(tall_grass->bitmap, vv);
		gonzalo++;
	}

	config_set_value(config, "SIZE", int_to_string(payload_size));
	config_set_value(config, "BLOCKS", blocks_for_config);
	config_save(config);

	save_bitmap();

	log_info(LOGGER, "Thread %d has written %d bytes in %s", tid, written_bytes, filename);
	return NULL;
}

char * read_file_content(char * path, char * filename, int tid) {
	int open_result = open_file(path, filename, tid);
	if(open_result != 0) {
		log_info(LOGGER, "File could not be opened. Retrying in a few seconds...");
		sleep(CONFIG.retry_time_op);
		return read_file_content(path, filename, tid);
	}

	t_config * config = get_config_for_file(path, filename);

	char ** allocated_blocks = config_get_array_value(config, "BLOCKS");
	int existing_blocks = aux_round_up(config_get_int_value(config, "SIZE") / tall_grass->block_size,
			config_get_int_value(config, "SIZE") / (float)tall_grass->block_size);

	int file_size = config_get_int_value(config, "SIZE");

	char * content = string_new();

	int o, readed_bytes = 0;
	for(o=0 ; o<existing_blocks ; o++) {
		char * string_block = allocated_blocks[o];
		int int_block = atoi(string_block);

		char * this_block_as_string = int_to_string(int_block);

		int to_read_bytes = file_size - readed_bytes;
		if(to_read_bytes > tall_grass->block_size) {
			to_read_bytes = tall_grass->block_size;
		}

		char * this_block_metadata_path = string_new();

		string_append(&this_block_metadata_path, CONFIG.tallgrass_mounting_point);
		string_append(&this_block_metadata_path, "/Blocks/");
		string_append(&this_block_metadata_path, this_block_as_string);
		string_append(&this_block_metadata_path, ".bin");

		FILE * block_file = fopen(this_block_metadata_path, "r");

		char * this_block_content = malloc(to_read_bytes + 1);
		fread(this_block_content, to_read_bytes, 1, block_file);
		this_block_content[to_read_bytes] = '\0';

		string_append(&content, this_block_content);

		fclose(block_file);
		readed_bytes += to_read_bytes;
	}

	log_info(LOGGER, "Thread %d has read %d bytes in %s", tid, readed_bytes, filename);
	return content;
}

int broker_server_function() {
	int success_listening = failed;
	do {
		log_info(LOGGER, "Attempting to connect to broker");
		if((CONFIG.broker_socket = create_socket()) == failed) {
			log_info(LOGGER, "Cannot create socket to connect to broker");
		} else if(connect_socket(CONFIG.broker_socket, CONFIG.broker_ip, CONFIG.broker_port) == failed) {
			log_info(LOGGER, "Cannot connect to broker terminal. Retrying...");
			close_socket(CONFIG.broker_socket);
			sleep(CONFIG.retry_time_conn);
		} else {
			success_listening = success;
			has_broker_connection = true;
		}
	} while (success_listening == failed);

	log_info(LOGGER, "Subscribing to Queue NEW_POKEMON");
	subscribe_to_queue(CONFIG.broker_socket, QUEUE_NEW_POKEMON);
	log_info(LOGGER, "Subscribing to Queue CATCH_POKEMON");
	subscribe_to_queue(CONFIG.broker_socket, QUEUE_CATCH_POKEMON);
	log_info(LOGGER, "Subscribing to Queue GET_POKEMON");
	subscribe_to_queue(CONFIG.broker_socket, QUEUE_GET_POKEMON);

	ready_to_recieve(CONFIG.broker_socket);

	int check_recv = 0;

	while(1) {
		net_message_header * header = malloc(sizeof(net_message_header));

		log_info(LOGGER, "Awaiting message from Broker");

		check_recv = recv(CONFIG.broker_socket, header, 1, MSG_PEEK);

		if(check_recv != 0 && check_recv != -1){
			pthread_mutex_lock(&broker_mutex);
			if(internal_broker_need == 0) {
				pthread_mutex_lock(&op_mutex);
				read(CONFIG.broker_socket, header, sizeof(net_message_header));
				queue_message * message = receive_pokemon_message(CONFIG.broker_socket);
				send_message_acknowledge(message, CONFIG.broker_socket);

				gamecard_thread_payload * payload = malloc(sizeof(gamecard_thread_payload));
				payload->from_broker = 1;
				payload->message = message;

				process_pokemon_message(payload);
			}
		} else {
			log_info(LOGGER, "Broker connection lost...");
			broker_server_function();
		}
	}

	return 0;
}

int server_function() {
	log_info(LOGGER, "Server Started. Listening on port %d", CONFIG.gamecard_port);
	void new(int fd, char * ip, int port) {
	}
	void lost(int fd, char * ip, int port) {
	}
	void incoming(int fd, char * ip, int port, net_message_header * header) {
		switch(header->type) {
			case NEW_MESSAGE:;
				pthread_mutex_lock(&op_mutex);
				queue_message * message = receive_pokemon_message(fd);

				gamecard_thread_payload * payload = malloc(sizeof(gamecard_thread_payload));
				payload->from_broker = 0;
				payload->message = message;

				pthread_t request_thread;
				pthread_create(&request_thread, NULL, &process_pokemon_message, payload);
				break;
			default:
				log_error(LOGGER, "Gamecard received unknown message type %d from external source", header->type);
				break;
		}
	}
	start_server(CONFIG.internal_socket, &new, &lost, &incoming);
	return 0;
}

//TALLGRASS

void setup_tall_grass() {
	log_info(LOGGER, "Starting TallGrass");

	char * _tall_grass_metadata_path = string_duplicate(config_get_string_value(_CONFIG, "PUNTO_MONTAJE_TALLGRASS"));
	string_append(&_tall_grass_metadata_path, "/Metadata/Metadata.bin");

	t_config * _TG_CONFIG = config_create(_tall_grass_metadata_path);

	tall_grass = malloc(sizeof(tall_grass_fs));

	tall_grass->block_size = config_get_int_value(_TG_CONFIG, "BLOCK_SIZE");
	tall_grass->blocks = config_get_int_value(_TG_CONFIG, "BLOCKS");
	tall_grass->magic_number = config_get_string_value(_TG_CONFIG, "MAGIC_NUMBER");

	tall_grass->blocks_in_bytes = tall_grass->blocks / 8; //TODO Que pasa si no son multiplos de ocho
	//Siempre son multiplos de 8, papu
	tall_grass->total_bytes = tall_grass->block_size * tall_grass->blocks;

	log_info(LOGGER, "Initing TallGrass with %d blocks of %d bytes and %s magic number",
			tall_grass->blocks, tall_grass->block_size, tall_grass->magic_number);

	char * _tall_grass_bitmap_path = string_duplicate(config_get_string_value(_CONFIG, "PUNTO_MONTAJE_TALLGRASS"));
	string_append(&_tall_grass_bitmap_path, "/Metadata/Bitmap.bin");
	FILE * bitmap_file = fopen(_tall_grass_bitmap_path, "r");
	if(bitmap_file == NULL) {
		log_info(LOGGER, "There was no Bitmap file, creating");
		bitmap_file = fopen(_tall_grass_bitmap_path, "w");
		fclose(bitmap_file);

		void * bitmap_data = malloc(tall_grass->blocks_in_bytes);
		tall_grass->bitmap = bitarray_create_with_mode(bitmap_data, tall_grass->blocks_in_bytes, MSB_FIRST);
		int i;
		for(i=0 ; i<tall_grass->blocks ; i++) {
			bitarray_clean_bit(tall_grass->bitmap, i);
		}

		tall_grass->free_bytes = tall_grass->block_size * tall_grass->blocks;

		save_bitmap();
	} else {
		log_info(LOGGER, "Loading Bitmap");

		void * bitmap_data = malloc(tall_grass->blocks_in_bytes);
		fread(bitmap_data, tall_grass->blocks / 8, 1, bitmap_file);
		tall_grass->bitmap = bitarray_create_with_mode(bitmap_data, tall_grass->blocks/8, MSB_FIRST);

		int d, free = 0;
		for(d=0 ; d<tall_grass->blocks ; d++) {
			if(!bitarray_test_bit(tall_grass->bitmap, d)) {
				free++;
			}
		}
		tall_grass->free_bytes = tall_grass->block_size * free;

		fclose(bitmap_file);
	}

	log_info(LOGGER, "%d total bytes, %d free", tall_grass->total_bytes, tall_grass->free_bytes);
	debug_bitmap();
}

void save_bitmap() {
	char * _tall_grass_bitmap_path = string_duplicate(config_get_string_value(_CONFIG, "PUNTO_MONTAJE_TALLGRASS"));
	string_append(&_tall_grass_bitmap_path, "/Metadata/Bitmap.bin");
	FILE * bitmap_file = fopen(_tall_grass_bitmap_path, "w");

	debug_bitmap();

	fwrite(tall_grass->bitmap->bitarray, tall_grass->blocks / 8, 1, bitmap_file);

	fclose(bitmap_file);
}

void debug_bitmap() {
	int i;
	for(i=0 ; i<tall_grass->blocks ; i++) {
		log_info(LOGGER, "Block %d %d", i, bitarray_test_bit(tall_grass->bitmap, i));
	}
}

t_list * find_free_blocks(int count) {
	t_list * li = list_create();

	int i, allocated = 0;
	for(i=0 ; i<tall_grass->blocks && allocated < count ; i++) {
		if(!bitarray_test_bit(tall_grass->bitmap, i)) {
			int * v = malloc(sizeof(int));
			memcpy(v, &i, sizeof(int));
			list_add(li, v);
			allocated++;
		}
	}

	if(allocated < count) {
		list_destroy(li);
		return NULL;
	}

	return li;
}

int aux_round_up(int int_value, float float_value) {
	return float_value - int_value > 0 ? int_value + 1 : int_value;
}

pokemon_file_serialized * serialize_pokemon_file(pokemon_file * pf) {
	pokemon_file_serialized * data = malloc(sizeof(pokemon_file_serialized));

	int f;
	char * contents = string_new();
	int contents_length = 0;
	for(f=0 ; f<pf->locations->elements_count ; f++) {
		pokemon_file_line * tl = list_get(pf->locations, f);

		if(tl->quantity > 0) {
			char * _x = int_to_string(tl->position->x);
			char * _y = int_to_string(tl->position->y);
			char * _q = int_to_string(tl->quantity);

			string_append(&contents, _x);
			string_append(&contents, "-");
			string_append(&contents, _y);
			string_append(&contents, "=");
			string_append(&contents, _q);

			contents_length += strlen(_x) + 1 + strlen(_y) + 1 +
					strlen(_q) + 1;

			if(f < pf->locations->elements_count - 1) {
				string_append(&contents, "\n");
			}
		}
	}

	contents[contents_length-1] = '\0';

	data->content = contents;
	data->length = contents_length;

	return data;
}

int count_character_in_string(char*str, char character) {
	int i, counter = 0;
	for(i=0 ; i<strlen(str) ; i++) {
		if(str[i] == character) {
			counter++;
		}
	}
	return counter;
}

pokemon_file * deserialize_pokemon_file(char * contents) {
	pokemon_file * file = pokemon_file_create();

	if(string_equals_ignore_case(contents, "")) {
		return file;
	}

	char ** lines = string_split(contents, "\n");
	int lines_q = count_character_in_string(contents, '\n') + 1;

	int a;
	for(a=0 ; a<lines_q && lines[a] != NULL ; a++) {
		char * line = lines[a];
		if(!string_equals_ignore_case(line, "")) {
			int x, y, q;
			sscanf(line, "%d-%d=%d", &x, &y, &q);
			list_add(file->locations, pokemon_file_line_create(x, y, q));
		}
	}

	return file;
}

pokemon_file_line * find_pokemon_file_line_for_location(pokemon_file * pf, int x, int y) {
	int o;
	pokemon_file_line * rr = NULL;
	for(o=0 ; o<pf->locations->elements_count ; o++) {
		pokemon_file_line * tl = list_get(pf->locations, o);
		if(tl->position->x == x && tl->position->y == y) {
			rr = tl;
		}
	}
	if(rr == NULL) {
		rr = pokemon_file_line_create(x, y, 0);
		list_add(pf->locations, rr);
	}
	return rr;
}

pokemon_file * pokemon_file_create() {
	pokemon_file * file = malloc(sizeof(pokemon_file));
	file->locations = list_create();
	return file;
}

pokemon_file_line * pokemon_file_line_create(int x, int y, int q) {
	pokemon_file_line * line = malloc(sizeof(pokemon_file_line));
	line->position = location_create(x, y);
	line->quantity = q;
	return line;
}

char * int_to_string(int number) {
	char * s = malloc(sizeof(char) * 10);
	sprintf(s, "%d", number);
	return s;
}
