#include <library/library.h>

//GLOBAL VARIABLES
team_config CONFIG;
t_list * trainers;

//COLAS
t_list * new_queue;
t_list * ready_queue;
t_list * exec_threads;
t_list * blocked_queue;
t_list * exit_queue;

t_list * allocations;

//ENTRENADOR EJECUTANDO
trainer_action * executing_trainer;

//SEMAFOROS
sem_t * ready_queue_mutex;

//PROTOTYPES
void setup(int argc, char **argv);
int broker_server_function();
int server_function();

void executing(trainer*t);
void sort_queues();
void sort_by_burst();
void sort_by_RR();
float estimate(trainer_action*t);
void start_running_trainer(trainer*t);
int exec_trainer_action(trainer_action * ta);


int main(int argc, char **argv) {
	setup(argc, argv);

	return EXIT_SUCCESS;
}

//Recibo mensaje, estoy ejecutando. Como salto de esto al exec??

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
	return 1;
}

int broker_server_function() {
	int success_listening = failed;
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

void start_running_trainer(trainer*t){
	pthread_t thread_exec;
	pthread_create(&thread_exec, NULL, executing, t);
	list_add(exec_threads, &thread_exec);
}

void setup(int argc, char **argv) {
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
	CONFIG.broker_ip = config_get_string_value(_CONFIG, "IP_BROKER");
	CONFIG.broker_port = config_get_int_value(_CONFIG, "PUERTO_BROKER");
	CONFIG.team_port = config_get_int_value(_CONFIG, "PUERTO_TEAM");
	CONFIG.initial_estimate = config_get_int_value(_CONFIG, "ESTIMACION_INICIAL");

	char * temp_positions = config_get_string_value(_CONFIG, "POSICIONES_ENTRENADORES");
	int trainers_count = -1, aux_counter;
	for(aux_counter=0 ; aux_counter<strlen(temp_positions) ; aux_counter++) {
		if(temp_positions[aux_counter] == '[') trainers_count++;
	}

	char * positions = config_get_string_value(_CONFIG, "POSICIONES_ENTRENADORES");
	char * pokemons = config_get_string_value(_CONFIG, "POKEMON_ENTRENADORES");
	char * targets = config_get_string_value(_CONFIG, "OBJETIVOS_ENTRENADORES");

	trainers = list_create();
	new_queue = list_create();
	ready_queue = list_create();
	blocked_queue = list_create();
	exit_queue = list_create();

	allocations = list_create();

	if(trainers_count > 0) {
		int i;
		for(aux_counter=0 ; aux_counter<trainers_count ; aux_counter++) {
			trainer t;

			char * this_positions = string_split(positions+1, "]")[aux_counter];
			char * original_positions = this_positions;
			string_append(&this_positions, "]");
			if(this_positions[0] == ',') this_positions++;
			while(this_positions[0] == ' ') {
				this_positions++;
			}
			t.x = atoi(string_get_string_as_array(this_positions)[0]);
			t.y = atoi(string_get_string_as_array(this_positions)[1]);
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
			t.pokemons = list_create();
			for(i=0 ; i<count_pokemons ; i++){
				list_add(t.pokemons, string_get_string_as_array(this_pokemons)[i]);
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
			t.targets = list_create();
			for(i=0 ; i<count_targets ; i++){
				list_add(t.targets, string_get_string_as_array(this_targets)[i]);
			}

			//Se puede pasar el entrenador de esta forma o va con puntero??
			t.id = aux_counter;
			start_running_trainer(&t);

			//Agrego al entrenador recien entrado a la cola de nuevos ??????
			//list_add(new_queue, t);

			/*printf("ENTRENADOR %d", aux_counter);
			printf("\n\tX = %d", t.x);
			printf("\n\tY = %d", t.y);
			printf("\n\tPOKEMONS = %d ( ", t.pokemons->elements_count);
			for(i=0 ; i<count_pokemons ; i++) {
				printf("%s ", list_get(t.pokemons, i));
			}
			printf(")");
			printf("\n\tTARGETS = %d ( ", t.targets->elements_count);
			for(i=0 ; i<count_targets ; i++) {
				printf("%s ", list_get(t.targets, i));
			}
			printf(")\n\n");*/
		}
	}

	/*
	if(trainers_count > 0){
		int i;
		for(aux_counter=0; aux_counter < trainers_count; aux_counter++){
			Hay que agregar los entrenadores que entraron a la cola de nuevos. En la funcion de arriba estan
		}
	}
	*/

	CONFIG.broker_socket = create_socket();
	connect_socket(CONFIG.broker_socket, CONFIG.broker_ip, CONFIG.broker_port);

	if((CONFIG.internal_socket = create_socket()) == failed) {
		log_info(LOGGER, "Cannot create socket");
		return;
	}
	if(bind_socket(CONFIG.internal_socket, CONFIG.team_port) == failed) {
		log_info(LOGGER, "Cannot bind internal socket");
		return;
	}
	pthread_create(&CONFIG.server_thread, NULL, server_function, CONFIG.internal_socket);
	pthread_create(&CONFIG.broker_thread, NULL, broker_server_function, CONFIG.broker_socket);

	//Ya que habra varios entrenadores ejecutando, seran varios hilos...
	for(int exec_t = 0; exec_t < trainers_count; exec_t++){

	}

	pthread_join(CONFIG.server_thread, NULL);
	pthread_join(CONFIG.broker_thread, NULL);

	//Creo los hilos de los entrenadores, ya que son varios que van a estar ejecutando
	for(int exec_t = 0; exec_t < trainers_count; exec_t++){
		pthread_t * t = list_get(exec_threads, exec_t);
		pthread_join(*t, NULL);
	}
}

float estimate(trainer_action*t){
	int alpha = CONFIG.initial_estimate;
	float estimation = (alpha/100)*(t->quantum_counter) + (1-(alpha/100))*(t->last_estimation);
	t->quantum_counter = 0;
	//log_info()
	return estimation;
}

void sort_by_burst(){
	int sort_burst_trainer(trainer_action * a_trainer, trainer_action * another_trainer){
		return (a_trainer->estimation <= another_trainer->estimation);
	}
	if(list_size(ready_queue) > 0){
		list_sort(ready_queue, (void*)sort_burst_trainer);
	}
}

void sort_by_RR(){

}

//Hace falta el estado new?
void sort_queues(){

	//OJO ACA
	//Hay entrenadores haciendo nada?
	for(int i=0; i<blocked_queue->elements_count; i++){
		trainer_action * ta = list_get(blocked_queue, i);
		ta->status = READY_ACTION;
		for(int j=0; j < allocations->elements_count; j++){ //Esta queriendo agarrar un pokemon?
			pokemon_allocation * pa = list_get(allocations, j);
			if(pa->status == WAITING_POKEMON){
				ta->status = BLOCKED_ACTION;
			}
		}
		if(ta->status == READY_ACTION){
			ta->estimation = estimate(ta);
			ta->last_estimation = ta->estimation;
			list_add(ready_queue, list_remove(blocked_queue, i));
		}
	}

	//Hay que cambiar los algoritmos de planificacion en tiempo real
	switch(CONFIG.planning_alg){
		case FIFO_PLANNING: //Queda todo igual
			break;
		case SJF_CD:
			if(executing_trainer){ //Me fijo si hay un entrenador ejecutando
				list_add(ready_queue, executing_trainer);
				executing_trainer = NULL;
				sort_by_burst();
			}
			break;
		case SJF_SD:
			sort_by_burst();
			break;
		case RR:
			sort_by_RR();
	}

	//Hay que correr el primero en la cola de ready
	if(!list_is_empty(ready_queue) && executing_trainer == NULL){
		executing_trainer = list_remove(ready_queue, 0);
		executing_trainer->status = EXEC_ACTION;
	}
}

int exec_trainer_action(trainer_action * ta){

}

void executing(trainer*t){
	//Es el entrenador ejecutando acciones

	trainer_action * this_trainer_action = NULL;

	sem_init(&ready_queue_mutex, 0, 1);

	while(1){
		if(this_trainer_action == NULL){
			//Guarda los semaforos
			sem_wait(&ready_queue_mutex);
			{
				if(ready_queue->elements_count != 0){
					this_trainer_action = list_get(ready_queue, 0);
					list_remove(ready_queue, 0);
				}
			}
			sem_post(&ready_queue_mutex);
		}else{
			this_trainer_action->status = EXEC_ACTION;
			log_info("RUNNING TRAINER %d\n", t->id);

			if(exec_trainer_action(this_trainer_action)){
				this_trainer_action->quantum_counter++;

				if(list_is_empty(t->targets)){ //GUARDA QUE NO ME GUSTA EL CAMBIO DE LISTAS
					//Salida del entrenador por fin de acciones que debe realizar
					this_trainer_action->status = EXIT_ACTION;
					list_add(exit_queue, executing_trainer);
					log_info("TRAINER %d HAS FINISHED\n", t->id);

					list_remove(exec_threads, t->id); //Seguro?
					this_trainer_action = NULL;
				}else{
					if(this_trainer_action->quantum_counter == CONFIG.quantum){
						//Salida del entrenador por fin de quantum
						sem_wait(&ready_queue_mutex);
						{
							this_trainer_action->status = READY_ACTION;
							list_add(ready_queue, this_trainer_action);
						}
						sem_post(&ready_queue_mutex);

						this_trainer_action->quantum_counter = 0;
						log_info("QUANTUM FINISHED FOR TRAINER %d\n", t->id);
						this_trainer_action = NULL;
					}else{
						//Nada, el entrenador continua
					}
				}
			}else{
				//Hubo error
				this_trainer_action->status = EXIT_ACTION;
				list_add(exit_queue, this_trainer_action);
				log_info("ACTION COULD NOT BE RESOLVED FOR TRAINER %d\n", t->id);
				this_trainer_action = NULL;
			}
		}
		usleep(CONFIG.cpu_delay * 1000);
	}
}










