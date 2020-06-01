#include <library/library.h>

//GLOBAL VARIABLES
team_config CONFIG;

int internal_broker_need;

t_list * trainers;

t_list * global_requirements;

pthread_mutex_t required_pokemons_mutex;
t_list * required_pokemons;

pthread_mutex_t ready_queue_mutex;
t_list * ready_queue;

trainer * executing_trainer;

//SEMAPHORES
pthread_mutex_t executing_mutex;
pthread_mutex_t broker_mutex;

pthread_t * exec_thread;

t_list * get_pokemon_msgs_ids;


//STATISTICS
team_statistics * statistics;

//PROTOTYPES
//Connections
void setup(int argc, char **argv);
void _diff_my_caught_pokemons(trainer * ttrainer);
int broker_server_function();
int server_function();

//Queues
void executing(trainer * t);
void sort_queues();
void sort_by_burst();
float estimate(trainer*t);
void exec_thread_function();

pokemon_requirement * find_requirement_by_pokemon_name(char * name);

//Trainers
void block_trainer(trainer * t);
trainer * closest_free_trainer(int pos_x, int pos_y);
int is_in_deadlock(trainer * waiting_trainer);
int circular_chain(t_list * trainers_to_check);
t_list * find_pokemons_allocators(trainer * myself);
int got_one_of_my_pokemons(trainer * suspected_trainer, trainer * myself);
int need_that_pokemon(trainer * myself, char * suspected_pokemon);
bool move_to(trainer * t, int x, int y);

//Pokemons
int is_required(char * pokemon);
void set_required_pokemons();

int is_id_in_list(t_list * list, int value);

void print_current_requirements();
void compute_deadlocks();

bool exists_path_to(trainer * from, trainer * to);

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

				//Veo si me sirve el pokemon que aparecio
				if(is_required(apm->pokemon)) {
					log_info(LOGGER, "Adding to required pokemons");
					pthread_mutex_lock(&required_pokemons_mutex);
					list_add(required_pokemons, apm);
					pthread_mutex_unlock(&required_pokemons_mutex);
					log_info(LOGGER, "\tAdded");
					//Agregado a la lista de pokemones requeridos
					//En el momento que un entrenador se encuentre dormido o libre hay que planificarlo
				} else {
					//Nada, el pokemon no me sirve y lo dejo libre
				}
			}
			break;
		case CATCH_POKEMON:;
			catch_pokemon_message * chpm = message->payload;
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

								log_info(LOGGER, "Trainer %d was expecting this message", ttrainer->id);

								if(ctpm->result) {
									appeared_pokemon_message * tapm = ttrainer->stats->current_activity->data;
									list_add(ttrainer->pokemons, tapm->pokemon);

									_diff_my_caught_pokemons(ttrainer);
									pokemon_requirement * treq = find_requirement_by_pokemon_name(tapm->pokemon);
									treq->total_caught++;

									log_info(LOGGER, "Capture registered successfully");
									print_current_requirements();

									compute_deadlocks();
								}

								ttrainer->stats->status = BLOCKED_ACTION;

								free(ttrainer->stats->current_activity);
								ttrainer->stats->current_activity = NULL;
							}
						}
					}
				}
			}
			break;
		case GET_POKEMON:;
			get_pokemon_message * gpm = message->payload;
			break;
		case LOCALIZED_POKEMON:;
			localized_pokemon_message * lpm = message->payload;
			{
				int i, already_recieved = false;
				for(i=0 ; i<required_pokemons->elements_count ; i++) {
					appeared_pokemon_message * tapm = list_get(required_pokemons, i);
					if(strcmp(tapm->pokemon, lpm->pokemon) == 0) {
						already_recieved = true;
					}
				}
				if(already_recieved == false) {
					if(is_required(lpm->pokemon)) {
						for(i=0 ; i<lpm->locations_counter ; i++) {
							location * tlocation = list_get(lpm->locations, i);
							pthread_mutex_lock(&required_pokemons_mutex);
							list_add(required_pokemons, appeared_pokemon_create(lpm->pokemon, tlocation->x, tlocation->y));
							pthread_mutex_unlock(&required_pokemons_mutex);
						}
					} else {
					}
				}
			}
			break;
		default:
			break;
	}
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
	return req == false ? NULL : req->total_caught < req->total_count;
}

void set_required_pokemons(){
	log_info(LOGGER, "Hay %d trainers", trainers->elements_count);

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
				trequirement->total_count = 1;

				list_add(global_requirements, trequirement);
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

int broker_server_function() {
	int success_listening = failed, i;
	do {
		log_info(LOGGER, "Atempting to connect to broker");
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

	log_info(LOGGER, "Subscribing to Queue APPEARED_POKEMON");
	subscribe_to_queue(CONFIG.broker_socket, QUEUE_APPEARED_POKEMON);
	log_info(LOGGER, "Subscribing to Queue LOCALIZED_POKEMON");
	subscribe_to_queue(CONFIG.broker_socket, QUEUE_LOCALIZED_POKEMON);
	log_info(LOGGER, "Subscribing to Queue CAUGHT_POKEMON");
	subscribe_to_queue(CONFIG.broker_socket, QUEUE_CAUGHT_POKEMON);

	for(i=0 ; i<global_requirements->elements_count ; i++) {
		pokemon_requirement * treq = list_get(global_requirements, i);
		queue_message * msg = get_pokemon_create(treq->name);

		send_pokemon_message(CONFIG.broker_socket, msg, 1, -1);

		int * tid = malloc(sizeof(int));
		memcpy(tid, &msg->header->message_id, sizeof(int));

		list_add(get_pokemon_msgs_ids, tid);
	}

	log_info(LOGGER, "Awaiting message from Broker");
	while(1) {
		net_message_header * header = malloc(sizeof(net_message_header));

		pthread_mutex_lock(&broker_mutex);
		recv(CONFIG.broker_socket, header, 1, MSG_PEEK);
		if(!internal_broker_need) {
			read(CONFIG.broker_socket, header, sizeof(net_message_header));
			queue_message * message = receive_pokemon_message(CONFIG.broker_socket);
			send_message_acknowledge(message, CONFIG.broker_socket);

			process_pokemon_message(message, 1);
		}
		pthread_mutex_unlock(&broker_mutex);
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
	return t->stats->current_activity == NULL;
}

trainer * closest_free_trainer(int pos_x, int pos_y){
	int closest_distance = -1;
	trainer * closest_trainer = NULL;
	int i = 0;

	for(i=0 ; i<trainers->elements_count ; i++) {
		trainer * ttrainer = list_get(trainers, i);

		if(is_free(ttrainer)) {
			int tdistance = calculate_distance_to(ttrainer, pos_x, pos_y);
			if(closest_trainer == NULL) {
				closest_distance = tdistance;
				closest_trainer = ttrainer;
			} else {
				if(closest_distance > 1) {
					closest_distance = tdistance;
					closest_trainer = ttrainer;
				}
			}
		}
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
	} else if(strcmp("SJF-CD", config_get_string_value(_CONFIG, "ALGORITMO_PLANIFICACION")) == 0) {
		CONFIG.planning_alg = SJF_CD;
	} else if(strcmp("SJF-SD", config_get_string_value(_CONFIG, "ALGORITMO_PLANIFICACION")) == 0) {
		CONFIG.planning_alg = SJF_SD;;
	}
	CONFIG.quantum = config_get_int_value(_CONFIG, "QUANTUM");
	CONFIG.alpha = config_get_int_value(_CONFIG, "ALPHA");
	CONFIG.broker_ip = config_get_string_value(_CONFIG, "IP_BROKER");
	CONFIG.broker_port = config_get_int_value(_CONFIG, "PUERTO_BROKER");
	CONFIG.team_port = config_get_int_value(_CONFIG, "PUERTO_TEAM");
	CONFIG.initial_estimate = config_get_int_value(_CONFIG, "ESTIMACION_INICIAL");

	internal_broker_need = false;

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
	pthread_mutex_init(&broker_mutex, NULL);

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

	log_info(LOGGER, "Creating pokemons");

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

			//CREAMOS EL HILO DE EJECUCION DEL ENTRENADOR
			t->id = aux_counter;

			t->stats->current_activity = NULL;

			pthread_mutex_init(&t->stats->mutex, NULL);
			pthread_mutex_lock(&t->stats->mutex);

			pthread_t * thread_exec = malloc(sizeof(pthread_t));
			pthread_create(thread_exec, NULL, executing, t);
			t->stats->thread = thread_exec;

			list_add(trainers, t);
		}
	}

	//TODO debug de deadlock
	//log_info(LOGGER, "asdasd");
	//log_info(LOGGER, "%d", exists_path_to( list_get(trainers, 3), list_get(trainers, 2) ));

	pthread_mutex_init(&executing_mutex, NULL);
	pthread_mutex_init(&ready_queue_mutex, NULL);

	set_required_pokemons();
	print_current_requirements();

	//CREAMOS SOCKET DE ESCUCHA CON EL BROKER
	CONFIG.broker_socket = create_socket();
	connect_socket(CONFIG.broker_socket, CONFIG.broker_ip, CONFIG.broker_port);
	pthread_create(&CONFIG.broker_thread, NULL, broker_server_function, CONFIG.broker_socket);

	exec_thread = malloc(sizeof(pthread_t));
	pthread_create(exec_thread, NULL, exec_thread_function, NULL);
	pthread_join(exec_thread, NULL);

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

void print_current_requirements() {
	int i;
	log_info(LOGGER, "GLOBAL REQUIREMENTS");
	for(i=0 ; i<global_requirements->elements_count ; i++) {
		pokemon_requirement * req = list_get(global_requirements, i);
		log_info(LOGGER, "\tRequirement %s - Total %d - Caught %d - Left %d", req->name,
					req->total_count, req->total_caught, req->total_count - req->total_caught);
	}
}

float estimate(trainer*t){
	float alpha = CONFIG.alpha; //Alpha dado por archivo de configuracion

	//estimacion proxima = estimacion anterior * (1-alfa) + real anterior * alfa
	float estimation = (t->stats->last_estimation) * (1 - alpha) + (t->stats->last_job_counter) * alpha;

	//float estimation = (alpha/100)*(t->quantum_counter) + (1-(alpha/100))*(t->last_estimation);
	//t->stats->quantum_counter = 0;
	log_info(LOGGER, "Trainer %d now has new estimation: %f", t->stats->estimation, estimation);

	return estimation;
}//TODO cuando se modifica la estimacion??

void sort_by_burst(){
	//CASO DE SOLAMENTE DOS ENTRENADORES
	int sort_burst_trainer(trainer * a_trainer, trainer * another_trainer){
		return (a_trainer->stats->estimation <= another_trainer->stats->estimation);
	}

	//CASO GENERAL
	if(list_size(ready_queue) > 0){
		list_sort(ready_queue, (void*)sort_burst_trainer);
	}
}

void sort_queues(){
	//Hay que cambiar los algoritmos de planificacion en tiempo real
	pthread_mutex_lock(&ready_queue_mutex);
	switch(CONFIG.planning_alg){
		case FIFO_PLANNING: //Queda igual
			break;
		case SJF_CD:
			if(executing_trainer){ //Me fijo si hay un entrenador ejecutando
				list_add(ready_queue, executing_trainer); //Pimba, desalojado
				executing_trainer = NULL;
				sort_by_burst();
			}
			break;
		case SJF_SD:
			sort_by_burst(); //Este no desaloja a nadie
			break;
		case RR:
			break; //Queda igual porque planifica FIFO, despues controlamos el quantum
	}

	log_info(LOGGER, "Sorted queues");

	//Hay que correr el primero en la cola de ready
	if(!list_is_empty(ready_queue) && executing_trainer == NULL){
		executing_trainer = list_remove(ready_queue, 0);
		executing_trainer->stats->status = EXEC_ACTION;

		log_info(LOGGER, "Now executing trainer %d", executing_trainer->id);
	}
	pthread_mutex_unlock(&ready_queue_mutex);
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
		log_info(LOGGER, "\tIs already there");
		return true;
	} else {
		log_info(LOGGER, "\tIs moving");
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
		log_info(LOGGER, "\t\tNew Position %d %d", t->x, t->y);
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
	log_info(LOGGER, "Init thread, trainer %d", t->id);

	while(1){
		pthread_mutex_lock(&t->stats->mutex);

		log_info(LOGGER, "Trainer %d executing", t->id);

		int quantum_ended = false;

		t->stats->last_job_counter++;
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

						internal_broker_need = true;
							log_info(LOGGER, "\tIs already there, sending catch msg");
							send_pokemon_message(CONFIG.broker_socket, msg, 1, -1);
						internal_broker_need = false;

						t->stats->current_activity->correlative_id_awaiting = msg->header->message_id;

						log_info(LOGGER, "\t\tCatch msg sent with ID %d", t->stats->current_activity->correlative_id_awaiting);
						t->stats->current_activity->type = AWAITING_CAPTURE_RESULT;

						t->stats->status = BLOCKED_ACTION;
						executing_trainer = NULL;
					}
				}
				break;
			case AWAITING_CAPTURE_RESULT: ;
				//Aca el entrenador espera recibir el mensaje del catch que envio
				//Luego si el resultado es positivo, se computan los deadlocks
				break;
			case TRADING: ;
				trainer * trading_trainer = t->stats->current_activity->data;
				log_info(LOGGER, "\tIs trading with %d at %d %d",
											trading_trainer->id, trading_trainer->x, trading_trainer->y);

				if(move_to(t, trading_trainer->x, trading_trainer->y)) {
					void _trade(trainer * self, trainer * friend){
						char * tmp_pokemon;
						char * another_tmp_pokemon;

						for(int i=0; i<self->pokemons->elements_count; i++){
							tmp_pokemon = list_get(self->pokemons, i);
							for(int j=0; j<friend->targets->elements_count; j++){
								another_tmp_pokemon = list_get(friend->targets, j);
								if(strcmp(tmp_pokemon, another_tmp_pokemon) == 0 &&
										!need_that_pokemon(self, tmp_pokemon)){
									list_remove(friend->targets, j);
									list_remove(self->pokemons, i);
								}
							}
						}
					}
					_trade(t, trading_trainer);
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
	while(required_pokemons->elements_count > 0) {
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
		}

		list_remove(required_pokemons, 0);
	}
	pthread_mutex_unlock(&required_pokemons_mutex);
}

void exec_thread_function() {
	while(!is_globally_completed()) {
		log_info(LOGGER, "Planning");

		compute_pending_actions();
		sort_queues();

		usleep(CONFIG.cpu_delay * 1000 * 1000);
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

t_list * find_pokemons_allocators(trainer * myself){
	t_list * trainers_got_one_of_my_pokemons = list_create();

	int i = 0;
	trainer * a_trainer = list_get(trainers, i);

	while(a_trainer != NULL){
		if(a_trainer->id != myself->id) {
			if(got_one_of_my_pokemons(a_trainer, myself)){
				list_add(trainers_got_one_of_my_pokemons, a_trainer);
			}
		}
		i++;
		a_trainer = list_get(trainers, i);
	}

	return trainers_got_one_of_my_pokemons;
}

int circular_chain(t_list * trainers_to_check){
	int i = 0;
	trainer * head_trainer = list_get(trainers_to_check, 0);
	trainer * cycle_trainer = list_get(trainers_to_check, i);
	trainer * trainer_next_to_me = list_get(trainers_to_check, i+1);

	do{
		if(!got_one_of_my_pokemons(trainer_next_to_me, cycle_trainer)) return false;

		i++;
		cycle_trainer = list_get(trainers_to_check, i);
		trainer_next_to_me = list_get(trainers_to_check, i+1);
	}while(cycle_trainer->id != head_trainer->id);

	return true;
}

//NO VA
int is_in_deadlock(trainer * waiting_trainer){
	t_list * possible_deadlocked_trainers;
	possible_deadlocked_trainers = list_create();

	possible_deadlocked_trainers =	find_pokemons_allocators(waiting_trainer);

	return circular_chain(possible_deadlocked_trainers);
}

bool exists_path_to(trainer * from, trainer * to) {
	trainer * t;
	t_list * allocations = find_pokemons_allocators(from);	//Entrenadores que tengan algo que necesito
	bool any_is = false;
	for(int i=0 ; i<allocations->elements_count ; i++) {	//Para cada entrenador que tiene algo que necesito
		t = list_get(allocations, i);
		if(t->id == to->id) {
			any_is = true;
			return any_is;
		} else {
			any_is = any_is | exists_path_to(t, to);
		}
	}
	return any_is;
}

void compute_path_to(trainer * start, trainer * target, t_list * paths, t_list * tpath) {
	int i, j;

	t_list * nodes = find_pokemons_allocators(start);

	if(nodes->elements_count == 0) {

	} else {
		for(i=0 ; i<nodes->elements_count ; i++) {
			trainer * ttrainer = list_get(nodes, i);

			if(ttrainer->id == target->id) {
				list_add(tpath, ttrainer);
			} else {
				t_list * new_path = list_create();
				list_add(paths, new_path);
				compute_path_to(start, ttrainer, paths, new_path);
			}
		}
	}
}

void compute_deadlocks() {
	int i, j;

	for(i=0 ; i<trainers->elements_count ; i++) {
		trainer * ttrainer = list_get(trainers, i);
		//t_list * tspares = spare_pokemons(ttrainer);
		//t_list * need_my_resource = in_need_of_a_spare(tspares, ttrainer);

		trainer * color = ttrainer;

		t_list * root_nodes;
		root_nodes = find_pokemons_allocators(ttrainer);

		for(j=0 ; j<root_nodes->elements_count ; j++) {
			trainer * root_trainer = list_get(root_nodes, j);
			t_list * my_allocators = find_pokemons_allocators(root_trainer);
		}
	}

}


