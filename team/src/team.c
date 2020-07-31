#include <library/library.h>

//GLOBAL VARIABLES
team_config CONFIG;

int has_broker_connection = false;

t_list * trainers;
t_list * global_requirements;
//Son los pokemones que el team necesita EN TOTAL
t_list * deadlock_groups;

pthread_mutex_t required_pokemons_mutex;
t_list * required_pokemons;
//Son los pokemones que el team necesita Y ESTAN EN EL MAPA

pthread_mutex_t ready_queue_mutex;
t_list * ready_queue;

trainer * executing_trainer;

//SEMAPHORES
pthread_mutex_t executing_mutex;
pthread_mutex_t broker_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t internal_need_mutex = PTHREAD_MUTEX_INITIALIZER;
int internal_broker_need;

pthread_mutex_t rtr_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t all_gets_ready = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t gets_left_mutex = PTHREAD_MUTEX_INITIALIZER;

pthread_t exec_thread;
t_list * get_pokemon_msgs_ids;

//STATISTICS
team_statistics * statistics;

//PROTOTYPES
//Extras
void list_replace_in_index(t_list * list, void * element, int index);

//Connections
void setup(int argc, char **argv);
void _diff_my_caught_pokemons(trainer * ttrainer);
int broker_server_function();
int server_function();

//Queues
void executing(trainer * t);
void sort_queues();
void exec_thread_function();

pokemon_requirement * find_requirement_by_pokemon_name(char * name);

//Trainers
int is_globally_completed();
void block_trainer(trainer * t);
trainer * closest_free_trainer(int pos_x, int pos_y);
void detect_circular_chains();
t_list * find_pokemons_allocators(trainer * myself, t_list * tg);
int got_one_of_my_pokemons(trainer * suspected_trainer, trainer * myself);
int need_that_pokemon(trainer * myself, char * suspected_pokemon);
bool move_to(trainer * t, int x, int y);

//Pokemons
int is_required(char * pokemon);
void set_required_pokemons();
int is_id_in_list(t_list * list, int value);
void print_current_requirements();
int requirements_are_finished();
void print_statistics();
int is_trainer_completed(trainer * t);

//Deadlock
int exists_path_to(trainer * first, trainer * from, trainer * to, t_list * tg, t_list * steps);
void solve_deadlock_for(t_list * tg, trainer * root);
int detect_deadlock_from(trainer * root, t_list * tg);

t_list * all_possible_combinations(t_list * all_trainers);
t_list * all_combinations_of_size(t_list * all_trainers, int size);
void combinations(t_list * all_trainers, int, int length, int start_position,
		t_list * current_result, t_list * all_results);

int set_internal_need(int value) {
	pthread_mutex_lock(&internal_need_mutex);
	internal_broker_need = value;
	pthread_mutex_unlock(&internal_need_mutex);
	return internal_broker_need;
}
int get_internal_need() {
	int value = 0;
	pthread_mutex_lock(&internal_need_mutex);
	value = internal_broker_need;
	pthread_mutex_unlock(&internal_need_mutex);
	return value;
}

//Main
int main(int argc, char **argv) {
	setup(argc, argv);

	return EXIT_SUCCESS;
}

//Explicit definitions
int is_id_in_list(t_list * list, int value) {
	int i;
	for(i=0 ; i<list->elements_count ; i++) {
		int * tid = list_get(list, i);
		if(value == (*tid)) {
			return true;
		}
	}
	return false;
}

void block_trainer(trainer * t){
	t->stats->status = BLOCKED_ACTION;
}

int process_pokemon_message(queue_message * message, int from_broker) {
	//Aca estoy suscrito a las colas del broker, que me va a ir pasando mensajes
	//No voy a manejar todos los tipos de mensajes, solo los que debo como proceso

	print_pokemon_message(message);
	//Message Processing
	switch(message->header->type) {
		//APPEARED, GET, CATCH, LOCALIZED, CAUGHT

		case APPEARED_POKEMON:;
			//Broker me avisa que aparecio un nuevo pokemon
			{
				appeared_pokemon_message * apm = message->payload;

				log_info(LOGGER, "A new %s has appeared in the map! It is @ [%d - %d]", apm->pokemon, apm->x, apm->y);

				//Veo si me sirve el pokemon que aparecio
				if(is_required(apm->pokemon)) {
					log_info(LOGGER, "Adding to required pokemons");
					pthread_mutex_lock(&required_pokemons_mutex);
					list_add(required_pokemons, apm);
					pthread_mutex_unlock(&required_pokemons_mutex);
					//Agregado a la lista de pokemones requeridos
					//En el momento que un entrenador se encuentre dormido o libre hay que planificarlo
				} else {
					//Nada, el pokemon no me sirve y lo dejo libre
				}
			}
			break;
		case CAUGHT_POKEMON:;
			{
				caught_pokemon_message * ctpm = message->payload;

				int i;
				for(i=0 ; i<trainers->elements_count ; i++){
					trainer * ttrainer = list_get(trainers, i);
					if(ttrainer->stats->current_activity != NULL) {
						if(ttrainer->stats->current_activity->correlative_id_awaiting == message->header->correlative_id) {
							if(ttrainer->stats->current_activity->type == AWAITING_CAPTURE_RESULT) {

								log_info(LOGGER, "Trainer %d was expecting answer to MID %d",
										ttrainer->id,
										message->header->correlative_id);

								if(ctpm->result) {
									appeared_pokemon_message * tapm = ttrainer->stats->current_activity->data;
									list_add(ttrainer->pokemons, tapm->pokemon);
									log_info(LOGGER, "Trainer %d has captured his objective successfully!", ttrainer->id);

									_diff_my_caught_pokemons(ttrainer);
									pokemon_requirement * treq = find_requirement_by_pokemon_name(tapm->pokemon);
									treq->currently_in_progress -= 1;
									treq->total_caught++;

									print_current_requirements();

									if(is_trainer_completed(ttrainer)){
										ttrainer->stats->status = EXIT_ACTION;
										log_info(LOGGER, "Trainer %d is in exit! He caught his last pokemon", ttrainer->id);
										if(is_globally_completed()){
											log_info(LOGGER, "Team has finished! Statistics:");
											print_statistics();
											exit(1);
										}
									} else { }

									free(ttrainer->stats->current_activity);
									ttrainer->stats->current_activity = NULL;
									ttrainer->stats->status = NEW_ACTION;

									//Si ya estan todos los pokemones que el team necesita
									if(requirements_are_finished()){
										log_info(LOGGER, "Deadlock detection algorithm started!");

										deadlock_groups = all_possible_combinations(trainers);
										detect_circular_chains();
									}
								} else {
									appeared_pokemon_message * tapm = ttrainer->stats->current_activity->data;
									pokemon_requirement * treq = find_requirement_by_pokemon_name(tapm->pokemon);
									treq->currently_in_progress -= 1;

									free(ttrainer->stats->current_activity);
									ttrainer->stats->current_activity = NULL;
									ttrainer->stats->status = NEW_ACTION;

									log_info(LOGGER, "Trainer %d could not capture his objective!", ttrainer->id);

									print_current_requirements();
								}
							}
						}
					}
				}
			}
			break;
		case LOCALIZED_POKEMON:;
			localized_pokemon_message * lpm = message->payload;
			{
				log_info(LOGGER, "%s has been localized!", lpm->pokemon);

				for(int i=0 ; i<lpm->locations_counter ; i++) {
					if(is_required(lpm->pokemon)) {
						log_info(LOGGER, "Adding to required pokemons");
							location * tlocation = list_get(lpm->locations, i);
							pthread_mutex_lock(&required_pokemons_mutex);
							list_add(required_pokemons, appeared_pokemon_create(lpm->pokemon, tlocation->x, tlocation->y)->payload);
							pthread_mutex_unlock(&required_pokemons_mutex);
					} else { }
				}
			}
			break;
		default:
			break;
	}

	if(from_broker == 1 && has_broker_connection == true) {
		already_processed(CONFIG.broker_socket);
	}

	set_internal_need(0);
	pthread_mutex_unlock(&broker_mutex);
	pthread_mutex_unlock(&all_gets_ready);

	return 1;
}

pokemon_requirement * find_requirement_by_pokemon_name(char * name) {
	int i;
	for(i=0 ; i<global_requirements->elements_count ; i++) {
		pokemon_requirement * treq = list_get(global_requirements, i);
		if(strcmp(treq->name, name) == 0) {
			return treq;
		}
	}
	return NULL;
}

int is_required(char * pokemon) {
	pokemon_requirement * req = find_requirement_by_pokemon_name(pokemon);
	if(req != NULL) {
		if((req->total_caught + req->currently_in_progress) < req->total_count) {
			req->currently_in_progress += 1;
			return 1;
		}
	}
	return 0;
}

void set_required_pokemons(){
	log_info(LOGGER, "We have a total of %d trainers", trainers->elements_count);

	int i = 0, j;
	trainer * t = list_get(trainers, i);
	char * aux_pokemon;

	while(t != NULL){

		j = 0;
		aux_pokemon = list_get(t->targets, j);

		while(aux_pokemon != NULL){
			pokemon_requirement * trequirement = find_requirement_by_pokemon_name(aux_pokemon);

			if(trequirement == NULL){
				trequirement = malloc(sizeof(pokemon_requirement));
				trequirement->name = aux_pokemon;
				trequirement->total_caught = 0;
				trequirement->currently_in_progress = 0;
				trequirement->total_count = 1;

				list_add(global_requirements, trequirement);

				free(trequirement);
			} else {
				trequirement->total_count++;
			}
			j++;
			aux_pokemon = list_get(t->targets, j);
		}

		i++;
		t = list_get(trainers, i);
	}
}

int gets_left = 0;
void * send_rtr() {
	log_info(LOGGER, "Team is now Ready to Recieve messages (%d)", ready_to_recieve(CONFIG.broker_socket));
	return NULL;
}

void * set_gets_left(int n) {
	pthread_mutex_lock(&gets_left_mutex);
	gets_left = n;
	pthread_mutex_unlock(&gets_left_mutex);
	return NULL;
}

int get_gets_left() {
	int n;
	pthread_mutex_lock(&gets_left_mutex);
	n = gets_left;
	pthread_mutex_unlock(&gets_left_mutex);
	return n;
}

void * send_initial_get(char * pokemon_name) {
	pthread_mutex_lock(&broker_mutex);

	queue_message * msg = get_pokemon_create(pokemon_name);

	log_info(LOGGER, "Sending GET for %s", pokemon_name);
	send_pokemon_message(CONFIG.broker_socket, msg, 1, -1);

	int * tid = malloc(sizeof(int));
	memcpy(tid, &msg->header->message_id, sizeof(int));

	set_gets_left(get_gets_left() - 1);
	log_info(LOGGER, "  GET was assigned MID %d (%d)", *tid, gets_left);
	list_add(get_pokemon_msgs_ids, tid);

	free(tid);

	if(get_gets_left() == 0) {
		send_rtr();
		set_internal_need(0);
		pthread_mutex_unlock(&all_gets_ready);
	}

	pthread_mutex_unlock(&broker_mutex);
	return NULL;
}

void * send_gets(void * n) {
	sleep(2);
	gets_left = global_requirements->elements_count;
	for(int i=0 ; i<global_requirements->elements_count ; i++) {
		pokemon_requirement * treq = list_get(global_requirements, i);

		pthread_t tt;
		pthread_create(&tt, NULL, send_initial_get, treq->name);
	}
	return NULL;
}

int broker_server_function() {
	set_internal_need(0);

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

			log_info(LOGGER, "Broker connection success!");
		}
	} while (success_listening == failed);

	log_info(LOGGER, "Subscribing to Queue APPEARED_POKEMON");
	subscribe_to_queue(CONFIG.broker_socket, QUEUE_APPEARED_POKEMON);
	log_info(LOGGER, "Subscribing to Queue LOCALIZED_POKEMON");
	subscribe_to_queue(CONFIG.broker_socket, QUEUE_LOCALIZED_POKEMON);
	log_info(LOGGER, "Subscribing to Queue CAUGHT_POKEMON");
	subscribe_to_queue(CONFIG.broker_socket, QUEUE_CAUGHT_POKEMON);

	set_internal_need(1);
	pthread_mutex_lock(&all_gets_ready);

	pthread_t thread_gets;
	pthread_create(&thread_gets, NULL, send_gets, NULL);

	while(1) {
		void * buffer = malloc(sizeof(net_message_header));
		net_message_header * header = malloc(sizeof(net_message_header));

		pthread_mutex_lock(&all_gets_ready);
		pthread_mutex_unlock(&all_gets_ready);

		log_info(LOGGER, "Awaiting data from Broker");
		int checkrecv = recv(CONFIG.broker_socket, buffer, 1, MSG_PEEK);
		free(buffer);

		if(checkrecv != 0 && checkrecv != -1) {
			pthread_mutex_lock(&broker_mutex);
			int in = get_internal_need();
			if(in == 0 && get_gets_left() == 0) {
				recv(CONFIG.broker_socket, header, sizeof(net_message_header), 0);
				queue_message * message = receive_pokemon_message(CONFIG.broker_socket);
				send_message_acknowledge(message, CONFIG.broker_socket);
				set_internal_need(1);
				process_pokemon_message(message, 1);
			}
		} else {
			log_info(LOGGER, "Broker connection lost...");
			broker_server_function();
		}

		free(header);
	}

	return 0;
}

int server_function() {
	log_info(LOGGER, "Server Started. Listening on port %d", CONFIG.team_port);
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
				log_error(LOGGER, "Team received unknown message type %d from external source", header->type);
				break;
		}
	}
	start_server(CONFIG.internal_socket, &new, &lost, &incoming);
	return 0;
}

int calculate_distance_to(trainer * t, int x, int y) {
	return fabs(t->x - x) + fabs(t->y - y);
}

int is_free(trainer * t) {
	return t->stats->current_activity == NULL && t->stats->status == NEW_ACTION;
}

trainer * closest_free_trainer(int pos_x, int pos_y){
	int closest_distance = -1;
	trainer * closest_trainer = NULL;
	int i = 0;

	log_info(LOGGER, "Finding closest trainer to %d %d", pos_x, pos_y);

	for(i=0 ; i<trainers->elements_count ; i++) {
		trainer * ttrainer = list_get(trainers, i);

		if(is_free(ttrainer) && ttrainer->pokemons->elements_count < ttrainer->targets->elements_count) {
			int tdistance = calculate_distance_to(ttrainer, pos_x, pos_y);
			if(closest_trainer == NULL) {
				closest_distance = tdistance;
				closest_trainer = ttrainer;
			} else {
				if(closest_distance > tdistance) {
					closest_distance = tdistance;
					closest_trainer = ttrainer;
				}
			}
		} else {
			if(is_free(ttrainer)) {
				log_info(LOGGER, "  %d is not a candidate, has catched %d of %d total",
						ttrainer->id,
						ttrainer->pokemons->elements_count,
						ttrainer->targets->elements_count);
			} else {
				if(ttrainer->stats->current_activity == NULL) {
					log_info(LOGGER, "  %d is not a candidate, but is has no activity");
				} else {
					switch(ttrainer->stats->current_activity->type) {
						case CAPTURING:;
							appeared_pokemon_message * apm = ttrainer->stats->current_activity->data;
							log_info(LOGGER, "  %d is not a candidate, is capturing %s @[%d-%d]",
									ttrainer->id,
									apm->pokemon,
									apm->x,
									apm->y);
							break;
						case AWAITING_CAPTURE_RESULT:
							log_info(LOGGER, "  %d is not a candidate, is awaiting catch result");
							break;
						case TRADING:
							log_info(LOGGER, "  %d is not a candidate, is currently trading");
							break;
					}
				}
			}
		}
	}

	if(closest_trainer == NULL) {
	} else {
		log_info(LOGGER, "Closest trainer to %d %d is %d with position %d %d", pos_x, pos_y, closest_trainer->id, closest_trainer->x, closest_trainer->y);
	}

	return closest_trainer;
}

void setup(int argc, char **argv) {
	//HACEMOS SETUP DEL CONFIG
	char * cfg_path = string_new();
	string_append(&cfg_path, (argc > 1) ? argv[1] : "team");
	string_append(&cfg_path, ".cfg");
	_CONFIG = config_create(cfg_path);

	LOGGER = log_create(config_get_string_value(_CONFIG, "LOG_FILE"), (argc > 1) ? argv[1] : "team", true, LOG_LEVEL_INFO);

	CONFIG.retry_time_conn = config_get_int_value(_CONFIG, "TIEMPO_RECONEXION");
	CONFIG.cpu_delay = config_get_int_value(_CONFIG, "RETARDO_CICLO_CPU");
	if(strcmp("FIFO", config_get_string_value(_CONFIG, "ALGORITMO_PLANIFICACION")) == 0) {
		CONFIG.planning_alg = FIFO_PLANNING;
	} else if(strcmp("RR", config_get_string_value(_CONFIG, "ALGORITMO_PLANIFICACION")) == 0) {
		CONFIG.planning_alg = RR;
	}

	CONFIG.quantum = config_get_int_value(_CONFIG, "QUANTUM");
	CONFIG.alpha = config_get_int_value(_CONFIG, "ALPHA");
	CONFIG.broker_ip = config_get_string_value(_CONFIG, "IP_BROKER");
	CONFIG.broker_port = config_get_int_value(_CONFIG, "PUERTO_BROKER");
	CONFIG.team_port = config_get_int_value(_CONFIG, "PUERTO_TEAM");
	CONFIG.initial_estimate = config_get_int_value(_CONFIG, "ESTIMACION_INICIAL");

	statistics = malloc(sizeof(team_statistics));

	statistics->context_switch_counter = 0;
	statistics->global_cpu_counter = 0;
	statistics->solved_deadlocks = 0;

	get_pokemon_msgs_ids = list_create();
	required_pokemons = list_create();

	ready_queue = list_create();
	global_requirements = list_create();
	executing_trainer = NULL;

	pthread_mutex_init(&required_pokemons_mutex, NULL);

	if((CONFIG.internal_socket = create_socket()) == failed) {
		log_info(LOGGER, "Cannot create socket");
		return;
	}

	//CREAMOS SOCKET DE SERVER
	if(bind_socket(CONFIG.internal_socket, CONFIG.team_port) == failed) {
		log_info(LOGGER, "Cannot bind internal socket");
		return;
	}
	pthread_create(&CONFIG.server_thread, NULL, server_function, CONFIG.internal_socket);


	//HACEMOS CONFIG DE LOS ENTRENADORES
	char * temp_positions = config_get_string_value(_CONFIG, "POSICIONES_ENTRENADORES");
	int trainers_count = -1, aux_counter;
	for(aux_counter=0 ; aux_counter<strlen(temp_positions) ; aux_counter++) {
		if(temp_positions[aux_counter] == '[') trainers_count++;
	}

	char * positions = config_get_string_value(_CONFIG, "POSICIONES_ENTRENADORES");
	char * pokemons = config_get_string_value(_CONFIG, "POKEMON_ENTRENADORES");
	char * targets = config_get_string_value(_CONFIG, "OBJETIVOS_ENTRENADORES");

	//CREAMOS LAS LISTAS PARA LOS ENTRENADORES
	trainers = list_create();

	//log_info(LOGGER, "Creating pokemons");

	//CREAMOS LOS ENTRENADORES
	if(trainers_count > 0) {
		int i;
		for(aux_counter=0 ; aux_counter<trainers_count ; aux_counter++) {
			trainer * t = malloc(sizeof(trainer));

			char * this_positions = string_split(positions+1, "]")[aux_counter];
			char * original_positions = this_positions;
			string_append(&this_positions, "]");
			if(this_positions[0] == ',') this_positions++;
			while(this_positions[0] == ' ') {
				this_positions++;
			}
			t->x = atoi(string_get_string_as_array(this_positions)[0]);
			t->y = atoi(string_get_string_as_array(this_positions)[1]);
			free(original_positions);

			char * this_pokemons = string_split(pokemons+1, "]")[aux_counter];
			char * original_pokemons = this_pokemons;
			string_append(&this_pokemons, "]");
			if(this_pokemons[0] == ',') this_pokemons++;
			while(this_pokemons[0] == ' ') {
				this_pokemons++;
			}
			int count_pokemons = 1;
			for(i=0 ; i<string_length(this_pokemons) ; i++) {
				if(this_pokemons[i] == ',') count_pokemons++;
			}
			if(strlen(this_pokemons) == 2) count_pokemons--;
			t->pokemons = list_create();
			for(i=0 ; i<count_pokemons ; i++){
				list_add(t->pokemons, string_get_string_as_array(this_pokemons)[i]);
			}

			char * this_targets = string_split(targets+1, "]")[aux_counter];
			char * original_targets = this_targets;

			string_append(&this_targets, "]");
			if(this_targets[0] == ',') this_targets++;
			while(this_targets[0] == ' ') {
				this_targets++;
			}
			int count_targets = 1;
			for(i=0 ; i<string_length(this_targets) ; i++) {
				if(this_targets[i] == ',') count_targets++;
			}
			if(strlen(this_targets) == 2) count_targets--;

			t->targets = list_create();
			for(i=0 ; i<count_targets ; i++){
				list_add(t->targets, string_get_string_as_array(this_targets)[i]);
			}

			_diff_my_caught_pokemons(t);

			t->stats = malloc(sizeof(trainer_action));
			t->stats->estimation = CONFIG.initial_estimate;
			t->stats->last_estimation = 0;
			t->stats->quantum_counter = 0;
			t->stats->status = NEW_ACTION;
			t->stats->last_job_counter = 0;
			t->stats->summation_quantum_counter = 0;
			t->stats->max_catch = t->targets->elements_count;

			//CREAMOS EL HILO DE EJECUCION DEL ENTRENADOR
			t->id = aux_counter;

			t->stats->current_activity = NULL;

			pthread_mutex_init(&t->stats->mutex, NULL);
			pthread_mutex_lock(&t->stats->mutex);

			pthread_t * thread_exec = malloc(sizeof(pthread_t));
			pthread_create(thread_exec, NULL, executing, t);
			t->stats->thread = thread_exec;

			list_add(trainers, t);

			log_info(LOGGER, "There is a new trainer with id %d @ [%d - %d]", t->id, t->x, t->y);

			free(t);
		}
	}

	deadlock_groups = all_possible_combinations(trainers);
	log_info(LOGGER, "Team setup has been loaded!");

	pthread_mutex_init(&executing_mutex, NULL);
	pthread_mutex_init(&ready_queue_mutex, NULL);

	set_required_pokemons();
	print_current_requirements();

	//CREAMOS SOCKET DE ESCUCHA CON EL BROKER

	CONFIG.broker_socket = create_socket();
	log_info(LOGGER, "Broker socket %d", CONFIG.broker_socket);
	pthread_create(&CONFIG.broker_thread, NULL, broker_server_function, NULL);

	pthread_create(&exec_thread, NULL, exec_thread_function, NULL);

	pthread_join(CONFIG.server_thread, NULL);
	pthread_join(CONFIG.broker_thread, NULL);
}

void _diff_my_caught_pokemons(trainer * ttrainer){
	char * tmp_pokemon;
	char * another_tmp_pokemon;

	for(int i=0; i<ttrainer->targets->elements_count; i++){
		tmp_pokemon = list_get(ttrainer->targets, i);
		for(int j=0; j<ttrainer->pokemons->elements_count; j++){
			another_tmp_pokemon = list_get(ttrainer->pokemons, j);
			if(strcmp(tmp_pokemon, another_tmp_pokemon) == 0){
				list_remove(ttrainer->pokemons, j);
				list_remove(ttrainer->targets, i);
				j = ttrainer->pokemons->elements_count + 1;
			}
		}
	}
}

int requirements_are_finished(){
	for(int i=0; i<global_requirements->elements_count; i++){
		pokemon_requirement * req = list_get(global_requirements, i);
		if(req->total_count - req->total_caught > 0) return false;
	}
	return true;
}

void print_current_requirements() {
	int i;
	log_info(LOGGER, "GLOBAL REQUIREMENTS");
	for(i=0 ; i<global_requirements->elements_count ; i++) {
		pokemon_requirement * req = list_get(global_requirements, i);
		log_info(LOGGER, "\tRequirement %s - Total %d - Caught %d - Left %d - In Progress %d", req->name,
					req->total_count, req->total_caught, req->total_count - req->total_caught, req->currently_in_progress);
	}
}

void sort_queues(){
	//Hay que cambiar los algoritmos de planificacion en tiempo real
	pthread_mutex_lock(&ready_queue_mutex);
	switch(CONFIG.planning_alg){
		case FIFO_PLANNING: //Queda igual
			break;
		case RR:
			break; //Queda igual porque planifica FIFO, despues controlamos el quantum
	}

	//Hay que correr el primero en la cola de ready
	if(!list_is_empty(ready_queue) && executing_trainer == NULL){
		executing_trainer = list_remove(ready_queue, 0);
		executing_trainer->stats->status = EXEC_ACTION;

		statistics->context_switch_counter++;
	}
	pthread_mutex_unlock(&ready_queue_mutex);
}

void print_statistics(){
	log_info(LOGGER, "\t%d CPU cycles have been consumed", statistics->global_cpu_counter);
	for(int i=0; i<trainers->elements_count; i++){
		trainer * t = list_get(trainers, i);

		log_info(LOGGER, "Trainer %d has consumed %d CPU cycles", t->id, t->stats->summation_quantum_counter);
	}
	log_info(LOGGER, "%d context switches have been done", statistics->context_switch_counter);
	log_info(LOGGER, "%d deadlocks have been found and solved", statistics->solved_deadlocks);
}

int is_trainer_completed(trainer * t) {
	return (list_is_empty(t->targets));
}

int is_globally_completed() {
	int i;
	for(i=0 ; i<trainers->elements_count ; i++) {
		trainer * ttrainer = list_get(trainers, i);
		if(!is_trainer_completed(ttrainer)) {
			return false;
		}
	}
	return true;
}

int location_are_the_same(location * l1, location * l2) {
	return (l1->x == l2->x && l1->y == l2->y);
}

bool move_to(trainer * t, int x, int y){
	if(location_are_the_same(location_create(x, y), location_create(t->x, t->y))) {
		return true;
	} else {
		if(t->x != x) {
			if(t->x < x) {
				t->x++;
			} else {
				t->x--;
			}
		} else {
			if(t->y < y) {
				t->y++;
			} else {
				t->y--;
			}
		}
		log_info(LOGGER, "\tTrainer %d moved to new Position [%d - %d]", t->id, t->x, t->y);
	}
	return false;
}

int need_that_pokemon(trainer * myself, char * suspected_pokemon){
	for(int i=0; i<myself->targets->elements_count; i++){
		if(strcmp(list_get(myself->targets, i), suspected_pokemon) == 0) return true;
	}

	return false;
}

void executing(trainer * t){
	//Es el entrenador ejecutando acciones
	int trading_counter = 0, i;
	while(1){
		pthread_mutex_lock(&t->stats->mutex);

		log_info(LOGGER, "Trainer %d is executing! He is the first in ready queue", t->id);

		int quantum_ended = false;

		t->stats->last_job_counter++; //Iba para el SJF...
		t->stats->summation_quantum_counter++;

		statistics->global_cpu_counter++;

		t->stats->quantum_counter++;
		if(CONFIG.planning_alg == RR) {
			if(CONFIG.quantum == t->stats->quantum_counter) {
				quantum_ended = true;
			}
		}

		switch(t->stats->current_activity->type) {
			case CAPTURING:
				{
					appeared_pokemon_message * apm = t->stats->current_activity->data;

					log_info(LOGGER, "\tIs capturing pokemon %s at %d %d",
							apm->pokemon, apm->x, apm->y);

					if(move_to(t, apm->x, apm->y)) {
						queue_message * msg = catch_pokemon_create(apm->pokemon, apm->x, apm->y);
						log_info(LOGGER, "\tIs already there, sending Catch message");

						executing_trainer = NULL;
						for(i=0 ; i<ready_queue->elements_count ; i++) {
							trainer * t1 = list_get(ready_queue, i);
							if(t1->id == t->id) {
								list_remove(ready_queue, i);
								i = ready_queue->elements_count + 1;
							}
						}

						if(has_broker_connection == true) {
							pthread_mutex_lock(&broker_mutex);

								send_pokemon_message(CONFIG.broker_socket, msg, 1, -1);
								t->stats->current_activity->correlative_id_awaiting = msg->header->message_id;
								t->stats->current_activity->type = AWAITING_CAPTURE_RESULT;
								t->stats->status = BLOCKED_ACTION;

								log_info(LOGGER, "\t\tNow Trainer %d is waiting for capture ID %d", t->id, t->stats->current_activity->correlative_id_awaiting);
							pthread_mutex_unlock(&broker_mutex);
						}else {
							list_add(t->pokemons, apm->pokemon);

							_diff_my_caught_pokemons(t);
							pokemon_requirement * treq = find_requirement_by_pokemon_name(apm->pokemon);
							treq->total_caught++;
							treq->currently_in_progress -= 1;

							log_info(LOGGER, "Capture registered successfully due to unconnected broker");

							print_current_requirements();

							t->stats->current_activity = NULL;
							t->stats->status = NEW_ACTION;

							if(is_trainer_completed(t)){
								t->stats->status = EXIT_ACTION;
								log_info(LOGGER, "Trainer %d is in exit! He caught his last pokemon", t->id);
								if(is_globally_completed()){
									log_info(LOGGER, "Team has finished! Statistics:");
									print_statistics();
									exit(1);
								}
							} else { }

							//Si ya estan todos los pokemones que el team necesita
							if(requirements_are_finished()){
								log_info(LOGGER, "Deadlock detection algorithm started!");
								detect_circular_chains();
							}
						}
					}
				}
				break;
			case AWAITING_CAPTURE_RESULT: ;
				//Aca el entrenador espera recibir el mensaje del catch que envio
				//Lo hacemos en recibo de mensajes process_pokemon_message


				//creamos una nueva locacion de pokemon con este entrenador y el pokemon atrapado
				//El deadlock lo chequearemos cuando se hayan capturado todos los pokemones que el team necesita
				//if(requirements_are_finished)....
				break;
			case TRADING: ;
				//Por que data?? Confiamos que ahi esta el entrenador con el que voy a hacer trade
				trainer * trading_trainer = t->stats->current_activity->data;
				log_info(LOGGER, "\tIs trading with %d at %d %d", trading_trainer->id, trading_trainer->x, trading_trainer->y);

				if(move_to(t, trading_trainer->x, trading_trainer->y)) {
					void _trade(trainer * self, trainer * friend){
						char * tmp_pokemon;
						char * another_tmp_pokemon;

						int s_h_f = 0;

						for(int i=0; i<self->pokemons->elements_count; i++){
							tmp_pokemon = list_get(self->pokemons, i);
							for(int j=0; j<friend->targets->elements_count; j++){
								another_tmp_pokemon = list_get(friend->targets, j);
								if(strcmp(tmp_pokemon, another_tmp_pokemon) == 0) {
									s_h_f = 1;
								}
							}
						}

						trainer * from = self;
						trainer * to = friend;

						if(s_h_f == 0) {
							from = friend;
							to = self;
						}

						for(int i=0; i<from->pokemons->elements_count; i++){
							tmp_pokemon = list_get(from->pokemons, i);
							for(int j=0; j<to->targets->elements_count; j++){
								another_tmp_pokemon = list_get(to->targets, j);
								if(strcmp(tmp_pokemon, another_tmp_pokemon) == 0) {
									list_remove(to->targets, j);
									list_remove(from->pokemons, i);
									log_info(LOGGER, "Trainer %d has given %s to %d", from->id, tmp_pokemon, to->id);
								}
							}
						}

						trainer * swap_trainer = to;
						to = from;
						from = swap_trainer;

						int reverse_trading = 0;
						for(int i=0; i<from->pokemons->elements_count; i++){
							tmp_pokemon = list_get(from->pokemons, i);
							for(int j=0; j<to->targets->elements_count; j++){
								another_tmp_pokemon = list_get(to->targets, j);
								if(strcmp(tmp_pokemon, another_tmp_pokemon) == 0 && reverse_trading == 0) {
									reverse_trading = 1;
									list_remove(to->targets, j);
									list_remove(from->pokemons, i);
									log_info(LOGGER, "Trainer %d has given %s to %d", from->id, tmp_pokemon, to->id);
								}
							}
						}

						if(reverse_trading == 0) {
							if(from->pokemons->elements_count > 0) {
								tmp_pokemon = list_get(from->pokemons, 0);
								for(i=0 ; i<from->pokemons->elements_count ; i++) {
									another_tmp_pokemon = list_get(from->pokemons, i);
									if(strcmp(tmp_pokemon, another_tmp_pokemon) == 0) {
										list_remove(from->pokemons, i);
										list_add(to->pokemons, tmp_pokemon);
										log_info(LOGGER, "Trainer %d has given %s to %d", from->id, tmp_pokemon, to->id);
									}
								}
							}
						}
					}
					if(trading_counter < 5){
						//El entrenador espera...
						log_info(LOGGER, "Trainer %d is trading with %d! (counter %d)", t->id, trading_trainer->id, trading_counter);
						trading_counter++;
					}else{
						_trade(t, trading_trainer);
						log_info(LOGGER, "Trainers %d %d have traded a pokemon!", t->id, trading_trainer->id);

						executing_trainer = NULL;
						t->stats->current_activity = NULL;
						trading_trainer->stats->current_activity = NULL;

						trading_counter = 0;

						for(i=0 ; i<ready_queue->elements_count ; i++) {
							trainer * t1 = list_get(ready_queue, i);
							if(t1->id == t->id) {
								list_remove(ready_queue, i);
								i = ready_queue->elements_count + 1;
							}
						}

						if(is_trainer_completed(t)){
							t->stats->status = EXIT_ACTION;
							log_info(LOGGER, "Trainer %d is in exit! He has traded his last pokemon", t->id);
						} if (is_trainer_completed(trading_trainer)) {
							trading_trainer->stats->status = EXIT_ACTION;
							log_info(LOGGER, "Trainer %d is in exit! He has traded his last pokemon", trading_trainer->id);
						} if (is_globally_completed()) {
							log_info(LOGGER, "Team has finished! Statistics:");
							print_statistics();
							exit(1);
						} else {
							t->stats->status = NEW_ACTION;
							log_info(LOGGER, "Trainer %d is waiting for a new activity! He has recently traded", t->id);

							trading_trainer->stats->status = NEW_ACTION;
							log_info(LOGGER, "Trainer %d is waiting for a new activity! He has recently traded", trading_trainer->id);
						}

						detect_circular_chains();
					}
				}

				break;
		}

		if(quantum_ended && executing_trainer != NULL) {
			executing_trainer = NULL;
			list_add(ready_queue, t);
		}
		if(quantum_ended) {
			t->stats->quantum_counter = 0;
		}

		pthread_mutex_unlock(&executing_mutex);
	}
}

trainer * find_free_trainer() {
	int i;
	for(i=0 ; i<trainers->elements_count ; i++) {
		trainer * t = list_get(trainers, i);
		if(is_free(t)) {
			return t;
		}
	}
	return NULL;
}

void compute_pending_actions() {
	pthread_mutex_lock(&required_pokemons_mutex);
	int no_available_trainers = 0;
	log_info(LOGGER, "Planning catch for %d pokemons", required_pokemons->elements_count);
	while(required_pokemons->elements_count > 0 && no_available_trainers == 0) {
		appeared_pokemon_message * apm = list_get(required_pokemons, 0);
		trainer * free_trainer = closest_free_trainer(apm->x, apm->y);

		if(free_trainer != NULL) {
			trainer_activity * activity = malloc(sizeof(trainer_activity));
			activity->type = CAPTURING;
			activity->data = apm;
			activity->correlative_id_awaiting = -1;
			free_trainer->stats->current_activity = activity;
			free_trainer->stats->status = READY_ACTION;
			list_add(ready_queue, free_trainer);

			free(activity);

			log_info(LOGGER, "Trainer %d is ready! He is the closest to catch %s", free_trainer->id, apm->pokemon);
			list_remove(required_pokemons, 0);
		} else {
			no_available_trainers = 1;
			log_info(LOGGER, "  No available trainers to catch %s", apm->pokemon);
		}
	}
	pthread_mutex_unlock(&required_pokemons_mutex);
}

void exec_thread_function() {
	while(!is_globally_completed()) {
		usleep(CONFIG.cpu_delay * 1000 * 1000);
		log_info(LOGGER, "Planning & Sorting Queues");

		compute_pending_actions();
		sort_queues();
		if(executing_trainer != NULL) {
			pthread_mutex_lock(&executing_mutex);
			pthread_mutex_unlock(&executing_trainer->stats->mutex);
		} else {
			//OVERHEARD
		}
	}
}

int got_one_of_my_pokemons(trainer * suspected_trainer, trainer * myself){
	bool _matches_my_pokemons(char * a_pokemon){
		int i = 0;
		char * tmp_pokemon = list_get(suspected_trainer->pokemons, i);

		while(tmp_pokemon != NULL){
			if(strcmp(a_pokemon, tmp_pokemon) == 0 && !need_that_pokemon(suspected_trainer, tmp_pokemon)){
				//Es el pokemon que necesito yo "Y" el otro entrenador no lo necesita
				return true;
			}
			i++;
			tmp_pokemon = list_get(suspected_trainer->pokemons, i);
		}
		return false;
	}

	return (list_find(myself->targets, (void*) _matches_my_pokemons) != NULL ? true : false);
}

t_list * find_pokemons_allocators(trainer * myself, t_list * tg){
	t_list * trainers_got_one_of_my_pokemons = list_create();

	int i = 0;
	trainer * a_trainer = list_get(tg, i);

	while(a_trainer != NULL){
		if(a_trainer->id != myself->id) {
			if(got_one_of_my_pokemons(a_trainer, myself)){
				list_add(trainers_got_one_of_my_pokemons, a_trainer);
			}
		}
		i++;
		a_trainer = list_get(tg, i);
	}

	return trainers_got_one_of_my_pokemons;
}

void list_replace_in_index(t_list * list, void * element, int index){
	list_add_in_index(list, index, element);
	list_remove(list, index + 1);
}

t_list * clone_list(t_list * source) {
	t_list * list = list_create();
	int i;
	for(i=0 ; i<source->elements_count ; i++) {
		list_add(list, list_get(source, i));
	}
	return list;
}

void combinations(t_list * all_trainers, int original_length, int length, int start_position,
		t_list * current_result, t_list * all_results){

	//list_clone de java
	if(length == 0){
		t_list * cloned = clone_list(current_result);
		list_add(all_results, cloned);

		list_destroy(cloned);
		return;
	}

	for(int i=start_position; i<=all_trainers->elements_count - length; i++){
		trainer * ttrainer = list_get(all_trainers, i);

		if(current_result->elements_count > original_length - length) {
			list_replace_in_index(current_result, ttrainer, original_length - length);
		} else {
			list_add(current_result, ttrainer);
		}

		combinations(all_trainers, original_length, length-1, i+1, current_result, all_results);
	}
}

t_list * all_combinations_of_size(t_list * all_trainers, int size){
	t_list * all_results_of_size = list_create();
	t_list * trainers_of_size = list_create();

	combinations(all_trainers, size, size, 0, trainers_of_size, all_results_of_size);
	list_destroy(trainers_of_size);

	return all_results_of_size;
}

t_list * all_possible_combinations(t_list * all_trainers){
	t_list * final_result = list_create();

	for(int i=2; i<=all_trainers->elements_count; i++){
		t_list * partial_result = list_create();
		partial_result = all_combinations_of_size(all_trainers, i);

		for(int j=0; j<partial_result->elements_count; j++){
			list_add(final_result, list_get(partial_result, j));
		}

		list_destroy(partial_result);
	}

	return final_result;
}

int exists_path_to(trainer * first, trainer * from, trainer * to, t_list * tg, t_list * steps) {
	if(from->id == to->id) return true;

	t_list * base_list;
	trainer * ttrainer;

	base_list = find_pokemons_allocators(from, tg);

	if(list_size(base_list) == 0) return false;

	int list_contains(t_list * list, trainer * finding_trainer){
		for(int i=0; i<list->elements_count; i++){
			trainer * aux_trainer = list_get(list, i);
			if(aux_trainer->id == finding_trainer->id) return true;
		}
		return false;
	}

	for(int i=0; i<base_list->elements_count; i++){
		ttrainer = list_get(base_list, i);

		if(to->id == ttrainer->id) return true;

		if(ttrainer->id != first->id && !list_contains(steps, ttrainer)){
			list_add(steps, ttrainer);
			list_destroy(base_list);
			if(exists_path_to(first, ttrainer, to, tg, steps)) return true;
		}
	}
	return false;
}

//Funcion PADRE de los deadlocks
void detect_circular_chains(){
	int flag_detected_deadlocks = 0;

	log_info(LOGGER, "All required pokemons for this team are catched. Now detecting deadlocks");
	for(int i=0; i<deadlock_groups->elements_count; i++){
		t_list * tg = list_get(deadlock_groups, i);
		if(detect_deadlock_from(list_get(tg, 0), tg) && flag_detected_deadlocks == 0){
			int is_in_deadlock = true;
			for(int j=1; j<tg->elements_count; j++){
				t_list * aux_trainer_list = list_create();
				if(!exists_path_to(list_get(tg, j), list_get(tg, j), list_get(tg, 0), tg, aux_trainer_list)){
					is_in_deadlock = false;
				}
				list_destroy(aux_trainer_list);

				t_list * another_aux_trainer_list = list_create();
				if(!exists_path_to(list_get(tg, 0), list_get(tg, 0), list_get(tg, j), tg, another_aux_trainer_list)){
					is_in_deadlock = false;
				}
				list_destroy(another_aux_trainer_list);
			}

			if(is_in_deadlock){
				flag_detected_deadlocks = 1;
				statistics->solved_deadlocks++;
				log_info(LOGGER, "A deadlock has been found!");
				solve_deadlock_for(tg, list_get(tg, 0));
			}
		}
	}

	if(flag_detected_deadlocks == 0){
		log_info(LOGGER, "No deadlocks have been found in this detection round!");
	}
}

int detect_deadlock_from(trainer * root, t_list * tg){
	if(is_trainer_completed(root)) return false;

	t_list * base_list = find_pokemons_allocators(root, tg);
	if(list_size(base_list) == 0) return false;

	for(int i=0; i<base_list->elements_count; i++){
		trainer * t = list_get(base_list, i);
		t_list * trainer_steps = list_create();
		if(exists_path_to(root, t, root, tg, trainer_steps)){
			list_destroy(base_list);
			list_destroy(trainer_steps);

			return true;
		}
		list_destroy(trainer_steps);
	}
	return false;
}

void solve_deadlock_for(t_list * tg, trainer * root){
	int went_to_solve = 0;
	trainer * deadlock_solver = NULL;
	block_trainer(root);
	log_info(LOGGER, "Trainer %d is blocked! He is waiting another trainer to solve a deadlock", root->id);

	for(int i=0; i<tg->elements_count; i++) {
		trainer * ttrainer = list_get(tg, i);

		if(ttrainer->id != root->id) {
			log_info(LOGGER, "Trainer %d is in deadlock too", ttrainer->id);

			for(int j=0 ; j<root->targets->elements_count ; j++) {
				char * pt = list_get(root->targets, j);

				for(int k=0 ; k<ttrainer->pokemons->elements_count ; k++) {
					char * pt2 = list_get(ttrainer->pokemons, k);

					if(strcmp(pt, pt2) == 0 && went_to_solve == 0) {
						went_to_solve = 1;
						trainer_activity * activity = malloc(sizeof(trainer_activity));
						activity->type = TRADING;
						activity->data = root;
						activity->correlative_id_awaiting = -1;

						ttrainer->stats->current_activity = activity;
						ttrainer->stats->status = READY_ACTION;
						list_add(ready_queue, ttrainer);

						free(activity);

						deadlock_solver = ttrainer;
					}
				}
			}
		}
	}
	if(deadlock_solver != NULL) {
		log_info(LOGGER, "  Trainer %d is ready! He is about to solve a deadlock (Activity No. %d)",
				deadlock_solver->id,
				deadlock_solver->stats->current_activity->type);
	}
}
