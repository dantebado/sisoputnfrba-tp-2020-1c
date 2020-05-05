#include <library/library.h>

//GLOBAL VARIABLES
gamecard_config CONFIG;

tall_grass_fs * tall_grass;

//PROTOTYPES
int process_pokemon_message(queue_message * message, int from_broker);
void setup(int argc, char **argv);
int broker_server_function();
int server_function();

void save_bitmap();
void debug_bitmap();
void setup_tall_grass();
char * tall_grass_get_or_create_directory(char * path);
t_list * find_free_blocks(int count);
int tall_grass_save_file(char * path, char * filename, void * payload, int payload_size);

char * int_to_string(int number);

int main(int argc, char **argv) {
	setup(argc, argv);

	return EXIT_SUCCESS;
}

int process_pokemon_message(queue_message * message, int from_broker) {
	print_pokemon_message(message);
	//Message Processing
	switch(message->header->type) {
		case NEW_POKEMON:;
			new_pokemon_message * npm = message->payload;
			break;
		case APPEARED_POKEMON:;
			appeared_pokemon_message * apm = message->payload;
			break;
		case CATCH_POKEMON:;
			catch_pokemon_message * chpm = message->payload;
			break;
		case CAUGHT_POKEMON:;
			caught_pokemon_message * ctpm = message->payload;
			break;
		case GET_POKEMON:;
			get_pokemon_message * gpm = message->payload;
			break;
		case LOCALIZED_POKEMON:;
			localized_pokemon_message * lpm = message->payload;
			break;
	}
	return success;
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

	setup_tall_grass();

	/*if((CONFIG.internal_socket = create_socket()) == failed) {
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
	pthread_join(CONFIG.broker_thread, NULL);*/
}

void save_bitmap() {
	char * _tall_grass_bitmap_path = string_duplicate(config_get_string_value(_CONFIG, "PUNTO_MONTAJE_TALLGRASS"));
	string_append(&_tall_grass_bitmap_path, "/Metadata/Bitmap.bin");
	FILE * bitmap_file = fopen(_tall_grass_bitmap_path, "w");

	fwrite(tall_grass->bitmap->bitarray, tall_grass->blocks / 8, 1, bitmap_file);

	fclose(bitmap_file);
	free(_tall_grass_bitmap_path);
}

void debug_bitmap() {
	int i;
	for(i=0 ; i<tall_grass->blocks ; i++) {
		log_info(LOGGER, "Block %d %d", i, bitarray_test_bit(tall_grass->bitmap, i));
	}
}

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
		tall_grass->bitmap = bitarray_create_with_mode(bitmap_data, tall_grass->blocks_in_bytes, LSB_FIRST);
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
		tall_grass->bitmap = bitarray_create_with_mode(bitmap_data, tall_grass->blocks/8, LSB_FIRST);

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


	//TEST

	tall_grass_save_file("/home", "test1", "hol", 3);
}

char * tall_grass_get_or_create_directory(char * path) {
	char * directory_path = string_duplicate(config_get_string_value(_CONFIG, "PUNTO_MONTAJE_TALLGRASS"));
	string_append(&directory_path, "/Files");
	string_append(&directory_path, path);

	char * directory_metadata = string_duplicate(directory_path);
	string_append(&directory_metadata, "/Metadata.bin");

	FILE * directory_metadata_file = fopen(directory_metadata, "r");

	if(directory_metadata_file == NULL) {
		log_info(LOGGER, "Creating directory");
		directory_metadata_file = fopen(directory_metadata, "w");

		if(directory_metadata_file == NULL) {
			log_info(LOGGER, "Must create in file system");
			mkdir(directory_path, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
		} else {
			log_info(LOGGER, "Folder was present in host");
		}

		directory_metadata_file = fopen(directory_metadata, "w+");
		fprintf(directory_metadata_file, "DIRECTORY=Y");
	}
	fclose(directory_metadata_file);

	log_info(LOGGER, "Loading directory %s", path);
	t_config * dconfig = config_create(directory_metadata);

	char * is_directory = config_get_string_value(dconfig, "DIRECTORY");

	if(strcmp(is_directory, "Y") == 0) {
		log_info(LOGGER, "Loaded directory %s", path);
		return directory_path;
	} else {
		log_error(LOGGER, "Desired path %s exists as a path", path);
	}
	free(is_directory);

	config_destroy(dconfig);
	return NULL;
}

t_list * find_free_blocks(int count) {
	t_list * li = list_create();

	int i, allocated = 0;
	for(i=0 ; i<tall_grass->blocks && allocated < count ; i++) {
		if(!bitarray_test_bit(tall_grass->bitmap, i)) {
			int *v = malloc(sizeof(int));
			memcpy(v, &i, sizeof(int));
			list_add(li, v);
			allocated++;
		}
	}

	if(allocated < count) {
		return NULL;
		list_destroy(li);
	}

	return li;
}

int aux_round_up(int int_value, float float_value) {
	return float_value - int_value > 0 ? int_value + 1 : int_value;
}

int tall_grass_save_file(char * path, char * filename, void * payload, int payload_size) {
	char * directory_path = tall_grass_get_or_create_directory(path);

	if(directory_path == NULL) {
		log_error(LOGGER, "Cannot create file");
		return false;
	}

	int necessary_blocks = aux_round_up(payload_size / tall_grass->block_size,
			payload_size / (float)tall_grass->block_size);
	if(necessary_blocks == 0) necessary_blocks++;

	t_list * blocks = NULL;

	char * file_metadata_path = string_duplicate(directory_path);
	string_append(&file_metadata_path, "/");
	string_append(&file_metadata_path, filename);

	char * file_metadata_directory = string_duplicate(file_metadata_path);

	string_append(&file_metadata_path, "/Metadata.bin");

	FILE * file_metadata_file = fopen(file_metadata_path, "r");
	if(file_metadata_file == NULL) {
		log_info(LOGGER, "File doesnt exists, creating.");

		blocks = find_free_blocks(necessary_blocks);
		if(blocks == NULL) {
			log_error(LOGGER, "Cannot find necessary free blocks (%d)", necessary_blocks);
			return false;
		}

		mkdir(file_metadata_directory, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);

		file_metadata_file = fopen(file_metadata_path, "w");
	} else {
		fclose(file_metadata_file);

		log_info(LOGGER, "File exists, loading current metadata");

		t_config * existing_config = config_create(file_metadata_path);

		char * is_directory = config_get_string_value(existing_config, "DIRECTORY");
		if(strcmp(is_directory, "Y") == 0) {
			log_error(LOGGER, "Cannot edit file %s, is a directory", filename);
			config_destroy(existing_config);
			return false;
		}

		char * is_open = config_get_string_value(existing_config, "OPEN");
		if(strcmp(is_open, "Y") == 0) {
			log_error(LOGGER, "Cannot edit file %s, is open", filename);
			config_destroy(existing_config);
			return false;
		}

		char ** allocated_blocks = config_get_array_value(existing_config, "BLOCKS");

		int existing_blocks = aux_round_up(config_get_int_value(existing_config, "SIZE") / tall_grass->block_size,
				config_get_int_value(existing_config, "SIZE") / (float)tall_grass->block_size);
		int blocks_left = necessary_blocks - existing_blocks;

		blocks = list_create();

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
				log_error(LOGGER, "No enough free blocks");
				return false;
			}
			int k;
			for(k=0 ; k<blocks_left ; k++) {
				int * some_block = list_get(more_blocks, k);
				list_add(blocks, some_block);
			}
			list_destroy(more_blocks);
		}

		config_destroy(existing_config);

		file_metadata_file = fopen(file_metadata_path, "w");
	}

	char * metadata_content = malloc(1);
	metadata_content[0] = '\0';
	string_append(&metadata_content, "DIRECTORY=N\n");
	string_append(&metadata_content, "SIZE=");
	string_append(&metadata_content, int_to_string(payload_size));
	string_append(&metadata_content, "\nBLOCKS=[");

	int i, written_bytes = 0;
	for(i=0 ; i<necessary_blocks ; i++) {
		int * v = list_get(blocks, i);
		int vv = *v;
		int to_write = 0;

		if(i != 0) {
			string_append(&metadata_content, ",");
		}
		string_append(&metadata_content, int_to_string(vv));

		if(payload_size - written_bytes > tall_grass->block_size) {
			to_write = tall_grass->block_size;
		} else {
			to_write = payload_size - written_bytes;
		}

		char * block_path = string_duplicate(config_get_string_value(_CONFIG, "PUNTO_MONTAJE_TALLGRASS"));
		string_append(&block_path, "/Blocks/");
		string_append(&block_path, int_to_string(vv));
		string_append(&block_path, ".bin");

		FILE * block_file = fopen(block_path, "w");
			fwrite(payload + i*tall_grass->block_size, to_write, 1, block_file);
		fclose(block_file);

		bitarray_set_bit(tall_grass->bitmap, vv);

		written_bytes += to_write;
	}
	while(i < blocks->elements_count) {
		int * v = list_get(blocks, i);
		int vv = *v;

		bitarray_clean_bit(tall_grass->bitmap, vv);

		i++;
	}

	save_bitmap();

	string_append(&metadata_content, "]\nOPEN=N");

	fprintf(file_metadata_file, "%s", metadata_content);
	fclose(file_metadata_file);

	log_info(LOGGER, "File saved");
	return true;
}

char * int_to_string(int number) {
	char * s = malloc(sizeof(char) * 10);
	sprintf(s, "%d", number);
	return s;
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
		}
	} while (success_listening == failed);

	log_info(LOGGER, "Subscribing to Queue NEW_POKEMON");
	subscribe_to_queue(CONFIG.broker_socket, QUEUE_NEW_POKEMON);
	log_info(LOGGER, "Subscribing to Queue CATCH_POKEMON");
	subscribe_to_queue(CONFIG.broker_socket, QUEUE_CATCH_POKEMON);
	log_info(LOGGER, "Subscribing to Queue GET_POKEMON");
	subscribe_to_queue(CONFIG.broker_socket, QUEUE_GET_POKEMON);

	log_info(LOGGER, "Awaiting message from Broker");
	while(1) {
		net_message_header * header = malloc(sizeof(net_message_header));
		read(CONFIG.broker_socket, header, sizeof(net_message_header));

		queue_message * message = receive_pokemon_message(CONFIG.broker_socket);
		send_message_acknowledge(message, CONFIG.broker_socket);

		process_pokemon_message(message, 1);
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
				queue_message * message = receive_pokemon_message(fd);
				process_pokemon_message(message, 0);
				break;
			default:
				log_error(LOGGER, "Gamecard received unknown message type %d from external source", header->type);
				break;
		}
	}
	start_server(CONFIG.internal_socket, &new, &lost, &incoming);
	return 0;
}
