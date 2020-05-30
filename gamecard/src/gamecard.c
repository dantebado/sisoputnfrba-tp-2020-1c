#include <library/library.h>

//GLOBAL VARIABLES
gamecard_config CONFIG;

tall_grass_fs * tall_grass;

sem_t * file_operation_mutex;
sem_t * directory_operation_mutex;

int internal_broker_need;
sem_t * broker_mutex;

//PROTOTYPES
int process_pokemon_message(gamecard_thread_payload * payload);
void setup(int argc, char **argv);
int broker_server_function();
int server_function();

void save_bitmap();
void debug_bitmap();
int count_character_in_string(char*str, char character);
char ** split_directory_tree(char * full_path);
void setup_tall_grass();
char * tall_grass_get_or_create_directory(char * path);
t_list * find_free_blocks(int count);
int aux_round_up(int int_value, float float_value);
int try_open_file(char * path, char * filename);
int try_close_file(char * path, char * filename);
void * tall_grass_read_file(char * path, char * filename, bool pre_opened);
int tall_grass_save_file(char * path, char * filename, void * payload, int payload_size);
int tall_grass_save_string_in_file(char * path, char * filename, char * content);
pokemon_file_serialized * serialize_pokemon_file(pokemon_file * pf);
pokemon_file * deserialize_pokemon_file(char * contents);
pokemon_file_line * find_pokemon_file_line_for_location(pokemon_file * pf, int x, int y);
pokemon_file * pokemon_file_create();
pokemon_file_line * pokemon_file_line_create(int x, int y, int q);
void debug_pokemon_file(pokemon_file * pf);

char * int_to_string(int number);

int main(int argc, char **argv) {
	setup(argc, argv);

	return EXIT_SUCCESS;
}

int process_pokemon_message(gamecard_thread_payload * payload) {
	queue_message * message = payload->message;
	int from_broker = payload->from_broker;

	internal_broker_need = true;

	print_pokemon_message(message);
	//Message Processing
	switch(message->header->type) {
		case NEW_POKEMON:;
			new_pokemon_message * npm = message->payload;

			char * fileContent = tall_grass_read_file("/Pokemon", npm->pokemon, false);
			if(fileContent != NULL) {

				pokemon_file * pf = deserialize_pokemon_file(fileContent);
				find_pokemon_file_line_for_location(pf, npm->x, npm->y)->quantity += npm->count;
				tall_grass_save_string_in_file("/Pokemon", npm->pokemon, serialize_pokemon_file(pf)->content);

				//TODO Checkear si se guardo el archivo. Sino, quiere decir que otro hilo lo abrió. NO DEBERIA OCURRIR

				queue_message * response = appeared_pokemon_create(npm->pokemon, npm->x, npm->y);
				send_pokemon_message(CONFIG.broker_socket, response, 1, -1);
				log_info(LOGGER, "Saved new pokemon location");

			} else {
				//TODO Archivo abierto, reintentar
			}

			break;
		case CATCH_POKEMON:;
			catch_pokemon_message * chpm = message->payload;

			log_info(LOGGER, "Attempt to catch pokemon %s at [%d::%d]", chpm->pokemon, chpm->x, chpm->y);

			/*
			 * success = 1
			 * already_open = -1
			 * directory_non_existant = -2
			 * non-existant = -3
			 * is_directory = -4
			 * */
			int open_result = try_open_file("/Pokemon", chpm->pokemon);
			switch(open_result) {
				case 1: ;
					char * fileContent = tall_grass_read_file("/Pokemon", chpm->pokemon, true);
					if(fileContent != NULL) {
						pokemon_file * pf = deserialize_pokemon_file(fileContent);
						pokemon_file_line * line = find_pokemon_file_line_for_location(pf, chpm->x, chpm->y);

						if(line->quantity == 0) {
							queue_message * response = caught_pokemon_create(0);
							send_pokemon_message(CONFIG.broker_socket, response, 1, message->header->message_id);
							log_info(LOGGER, "Pokemon caught failed");
						} else {
							line->quantity--;
							tall_grass_save_string_in_file("/Pokemon", chpm->pokemon, serialize_pokemon_file(pf)->content);

							queue_message * response = caught_pokemon_create(1);
							send_pokemon_message(CONFIG.broker_socket, response, 1, message->header->message_id);
							log_info(LOGGER, "Pokemon caught successfully");
						}
					}
					break;
				case -1:
					log_info(LOGGER, "File is already open. Retrying");
					break;
				default:
					log_error(LOGGER, "File does not exists or is a directory");
					queue_message * response = caught_pokemon_create(0);
					send_pokemon_message(CONFIG.broker_socket, response, 1, message->header->message_id);
					break;
			}

			break;
		case GET_POKEMON:;
		{
			get_pokemon_message * gpm = message->payload;
			open_result = try_open_file("/Pokemon", gpm->pokemon);

			switch(open_result) {
			case 1: ;
				char * fileContent = tall_grass_read_file("/Pokemon", gpm->pokemon, true);
				if(fileContent != NULL) {
					pokemon_file * pf = deserialize_pokemon_file(fileContent);

					t_list* listaPosiciones = list_create();
					int i;
					for(i=0; i < pf->locations->elements_count; i++){
						pokemon_file_line * pfl = list_get(pf->locations, i);
						list_add(listaPosiciones, pfl->position);
					}
					queue_message * response = localized_pokemon_create(gpm->pokemon, listaPosiciones);
					send_pokemon_message(CONFIG.broker_socket, response, 1, message->header->message_id);
				}

			break;
			case -1:
				log_info(LOGGER, "Pokemon not exists");
				break;
			default:

			break;
			}
			break;
			default:
			break;
		}
	}

	internal_broker_need = false;

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

	broker_mutex = malloc(sizeof(sem_t));
	sem_init(broker_mutex, 0, 1);

	internal_broker_need = false;

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

		sem_wait(broker_mutex);
		recv(CONFIG.broker_socket, header, 1, MSG_PEEK);
		if(!internal_broker_need) {
			read(CONFIG.broker_socket, header, sizeof(net_message_header));
			queue_message * message = receive_pokemon_message(CONFIG.broker_socket);
			send_message_acknowledge(message, CONFIG.broker_socket);

			gamecard_thread_payload * payload = malloc(sizeof(gamecard_thread_payload));
			payload->from_broker = 1;
			payload->message = message;

			pthread_t the_thread;
			pthread_create(&the_thread, NULL, process_pokemon_message, payload);
		}
		sem_post(broker_mutex);
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

				gamecard_thread_payload * payload = malloc(sizeof(gamecard_thread_payload));
				payload->from_broker = 0;
				payload->message = message;

				pthread_t * the_thread;
				pthread_create(the_thread, NULL, process_pokemon_message, payload);
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

	file_operation_mutex = malloc(sizeof(sem_t));
	sem_init(file_operation_mutex, NULL, 1);

	directory_operation_mutex = malloc(sizeof(sem_t));
	sem_init(directory_operation_mutex, NULL, 1);

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

int count_character_in_string(char*str, char character) {
	int i, counter = 0;
	for(i=0 ; i<strlen(str) ; i++) {
		if(str[i] == character) {
			counter++;
		}
	}
	return counter;
}

char ** split_directory_tree(char * full_path) {
	return string_split(full_path, "/");
}

char * tall_grass_get_or_create_directory(char * path) {
	sem_wait(directory_operation_mutex);

	char ** dp = split_directory_tree(path);
	int count = count_character_in_string(path, '/');

	int l;
	char * directory_path = NULL;
	char * acumulator_for_path = malloc(sizeof(1)); acumulator_for_path[0] = '\0';
	for(l=0 ; l<count ; l++) {
		string_append(&acumulator_for_path, "/");
		string_append(&acumulator_for_path, dp[l]);

		directory_path = string_duplicate(config_get_string_value(_CONFIG, "PUNTO_MONTAJE_TALLGRASS"));
		string_append(&directory_path, "/Files");
		string_append(&directory_path, acumulator_for_path);

		char * directory_metadata = string_duplicate(directory_path);
		string_append(&directory_metadata, "/Metadata.bin");

		FILE * directory_metadata_file = fopen(directory_metadata, "r");

		if(directory_metadata_file == NULL) {
			directory_metadata_file = fopen(directory_metadata, "w");

			if(directory_metadata_file == NULL) {
				mkdir(directory_path, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
			} else {
				fclose(directory_metadata_file);
			}

			directory_metadata_file = fopen(directory_metadata, "w+");
			fprintf(directory_metadata_file, "DIRECTORY=Y");
		}
		fclose(directory_metadata_file);

		t_config * dconfig = config_create(directory_metadata);

		char * is_directory = config_get_string_value(dconfig, "DIRECTORY");

		if(strcmp(is_directory, "Y") == 0) {
		} else {
			log_error(LOGGER, "Desired path %s exists as a file", acumulator_for_path);
		}

		config_destroy(dconfig);
	}

	sem_post(directory_operation_mutex);
	return directory_path;
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
		list_destroy(li);
		return NULL;
	}

	return li;
}

int aux_round_up(int int_value, float float_value) {
	return float_value - int_value > 0 ? int_value + 1 : int_value;
}

/*
 * success = 1
 * already_open = -1
 * directory_non_existant = -2
 * non-existant = -3
 * is_directory = -4
 * */
int try_open_file(char * path, char * filename) {
	sem_wait(file_operation_mutex);

	char * directory_path = tall_grass_get_or_create_directory(path);

	if(directory_path == NULL) {
		log_error(LOGGER, "Cannot open file. Directory doesnt exist");

		sem_post(file_operation_mutex);
		return -2;
	}

	char * file_metadata_path = string_duplicate(directory_path);
	string_append(&file_metadata_path, "/");
	string_append(&file_metadata_path, filename);
	string_append(&file_metadata_path, "/Metadata.bin");

	FILE * file_metadata_file = fopen(file_metadata_path, "r");
	if(file_metadata_file == NULL) {
		log_error(LOGGER, "File doesnt exists");

		sem_post(file_operation_mutex);
		return -3;
	}
	t_config * existing_config = config_create(file_metadata_path);

	char * is_directory = config_get_string_value(existing_config, "DIRECTORY");
	if(strcmp(is_directory, "Y") == 0) {
		log_error(LOGGER, "Cannot close file %s, is a directory", filename);
		config_destroy(existing_config);
		return -4;
	}

	char * is_open = config_get_string_value(existing_config, "OPEN");
	if(strcmp(is_open, "Y") == 0) {
		log_error(LOGGER, "Cannot open file %s, is already open", filename);
		config_destroy(existing_config);

		sem_post(file_operation_mutex);
		return -1;
	}

	config_set_value(existing_config, "OPEN", "Y");
	config_save(existing_config);

	sem_post(file_operation_mutex);

	return 1;
}

int try_close_file(char * path, char * filename) {
	sem_wait(file_operation_mutex);

	char * directory_path = tall_grass_get_or_create_directory(path);

	if(directory_path == NULL) {
		log_error(LOGGER, "Cannot close file. Directory doesnt exist");

		sem_post(file_operation_mutex);
		return false;
	}

	char * file_metadata_path = string_duplicate(directory_path);
	string_append(&file_metadata_path, "/");
	string_append(&file_metadata_path, filename);
	string_append(&file_metadata_path, "/Metadata.bin");

	FILE * file_metadata_file = fopen(file_metadata_path, "r");
	if(file_metadata_file == NULL) {
		log_error(LOGGER, "Cannot close file %s. File does not exist", filename);

		sem_post(file_operation_mutex);
		return false;
	}
	t_config * existing_config = config_create(file_metadata_path);

	char * is_directory = config_get_string_value(existing_config, "DIRECTORY");
	if(strcmp(is_directory, "Y") == 0) {
		log_error(LOGGER, "Cannot open file %s, it is a directory", filename);
		config_destroy(existing_config);
		return false;
	}

	char * is_open = config_get_string_value(existing_config, "OPEN");
	if(strcmp(is_open, "N") == 0) {
		log_error(LOGGER, "Cannot close file %s, it is already closed", filename);
		config_destroy(existing_config);

		sem_post(file_operation_mutex);
		return false;
	}

	config_set_value(existing_config, "OPEN", "N");
	config_save(existing_config);

	sem_post(file_operation_mutex);

	return true;
}

void * tall_grass_read_file(char * path, char * filename, bool pre_opened) {
	char * directory_path = tall_grass_get_or_create_directory(path);

	if(directory_path == NULL) {
		log_error(LOGGER, "Cannot read file. Directory does not exist");
		return false;
	}

	if(!pre_opened) {
		int open = try_open_file(path, filename);
		if(open < 0) {
			if(open == -3) {
				tall_grass_save_string_in_file(path, filename, "");
				try_open_file(path, filename);
			} else {
				return NULL;
			}
		}
	}

	char * file_metadata_path = string_duplicate(directory_path);
	string_append(&file_metadata_path, "/");
	string_append(&file_metadata_path, filename);
	string_append(&file_metadata_path, "/Metadata.bin");

	FILE * file_metadata_file = fopen(file_metadata_path, "r");
	if(file_metadata_file == NULL) {
		log_error(LOGGER, "File does not exist");
		return NULL;
	}
	t_config * existing_config = config_create(file_metadata_path);

	char ** allocated_blocks = config_get_array_value(existing_config, "BLOCKS");
	int content_size = config_get_int_value(existing_config, "SIZE");
	int existing_blocks = aux_round_up(content_size / tall_grass->block_size,
			content_size / (float)tall_grass->block_size);

	int o, readed = 0;
	void * content = malloc(sizeof(content_size));
	for(o=0 ; o<existing_blocks ; o++) {
		char * string_block = allocated_blocks[o];
		char * block_path = string_duplicate(config_get_string_value(_CONFIG, "PUNTO_MONTAJE_TALLGRASS"));
		string_append(&block_path, "/Blocks/");
		string_append(&block_path, string_block);
		string_append(&block_path, ".bin");

		FILE * block_file = fopen(block_path, "r");
		if(block_file == NULL) {
			log_error(LOGGER, "Cannot read file. Error fetching block %s", string_block);
		}

		int to_read = 0;
		if(content_size - readed > tall_grass->block_size) {
			to_read = tall_grass->block_size;
		} else {
			to_read = content_size - readed;
		}

		char * tbc = malloc(sizeof(char) * to_read);
		fread(tbc, to_read, 1, block_file);
		memcpy(content + readed, tbc, to_read);
		free(tbc);
		fclose(block_file);
		readed += to_read;
	}

	try_close_file(path, filename);

	return content;
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

	int new_occupied_blocks = necessary_blocks;

	t_list * blocks = NULL;

	char * file_metadata_path = string_duplicate(directory_path);
	string_append(&file_metadata_path, "/");
	string_append(&file_metadata_path, filename);

	char * file_metadata_directory = string_duplicate(file_metadata_path);

	string_append(&file_metadata_path, "/Metadata.bin");

	FILE * file_metadata_file = fopen(file_metadata_path, "r");
	if(file_metadata_file == NULL) {

		blocks = find_free_blocks(necessary_blocks);
		if(blocks == NULL) {
			log_error(LOGGER, "Cannot find necessary free blocks (%d)", necessary_blocks);
			return false;
		}

		mkdir(file_metadata_directory, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);

		file_metadata_file = fopen(file_metadata_path, "w");
	} else {
		fclose(file_metadata_file);

		if(try_open_file(path, filename) < 0) {
			return false;
		}

		t_config * existing_config = config_create(file_metadata_path);

		char * is_directory = config_get_string_value(existing_config, "DIRECTORY");
		if(strcmp(is_directory, "Y") == 0) {
			log_error(LOGGER, "Cannot edit file %s, it is a directory", filename);
			config_destroy(existing_config);

			try_close_file(path, filename);
			return false;
		}

		char ** allocated_blocks = config_get_array_value(existing_config, "BLOCKS");

		int existing_blocks = aux_round_up(config_get_int_value(existing_config, "SIZE") / tall_grass->block_size,
				config_get_int_value(existing_config, "SIZE") / (float)tall_grass->block_size);
		int blocks_left = necessary_blocks - existing_blocks;

		new_occupied_blocks -= existing_blocks;

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
				log_error(LOGGER, "Not enough free blocks");

				try_close_file(path, filename);
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

	string_append(&metadata_content, "]\nOPEN=Y");

	fprintf(file_metadata_file, "%s", metadata_content);
	fclose(file_metadata_file);

	tall_grass->free_bytes = new_occupied_blocks * tall_grass->block_size;

	try_close_file(path, filename);

	return true;
}

int tall_grass_save_string_in_file(char * path, char * filename, char * content) {
	return tall_grass_save_file(path, filename, content, strlen(content) + 1);
}

pokemon_file_serialized * serialize_pokemon_file(pokemon_file * pf) {
	pokemon_file_serialized * data = malloc(sizeof(pokemon_file_serialized));

	int f;
	char * contents = malloc(1); contents[0] = '\0';
	int contents_length = 1;
	for(f=0 ; f<pf->locations->elements_count ; f++) {
		pokemon_file_line * tl = list_get(pf->locations, f);

		if(tl->quantity > 0) {
			char * _x = int_to_string(tl->position->x);
			char * _y = int_to_string(tl->position->y);
			char * _q = int_to_string(tl->quantity);

			contents_length += strlen(_x) + 1 + strlen(_y) + 1 +
					strlen(_q) + 1;

			string_append(&contents, _x);
			string_append(&contents, "-");
			string_append(&contents, _y);
			string_append(&contents, "=");
			string_append(&contents, _q);

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

pokemon_file * deserialize_pokemon_file(char * contents) {
	pokemon_file * file = pokemon_file_create();

	if(strcmp("", contents) == 0) {
		return file;
	}

	char ** lines = string_split(contents, "\n");
	int lines_q = count_character_in_string(contents, '\n') + 1;

	int a;
	for(a=0 ; a<lines_q && lines[a] != NULL ; a++) {
		char * line = lines[a];
		if(strcmp(line, "") != 0) {
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

void debug_pokemon_file(pokemon_file * pf) {
	int n;
	for(n=0 ; n<pf->locations->elements_count ; n++) {
		pokemon_file_line * tl = list_get(pf->locations, n);
		log_info(LOGGER, "   [%d-%d] = %d", tl->position->x, tl->position->y, tl->quantity);
	}
}

char * int_to_string(int number) {
	char * s = malloc(sizeof(char) * 10);
	sprintf(s, "%d", number);
	return s;
}
