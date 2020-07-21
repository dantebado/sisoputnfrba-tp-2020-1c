#include "commands.h"

int _command_connect_to_broker(gameboy_config CONFIG) {
	int a_socket = create_socket();
	if(connect_socket(a_socket, CONFIG.broker_ip, CONFIG.broker_port) == failed) {
		log_error(LOGGER, "Error opening broker socket");
		return failed;
	} else {
		return a_socket;
	}
}
int _command_connect_to_team(gameboy_config CONFIG) {
	int a_socket = create_socket();
	if(connect_socket(a_socket, CONFIG.team_ip, CONFIG.team_port) == failed) {
		log_error(LOGGER, "Error opening team socket");
		return failed;
	} else {
		return a_socket;
	}
}
int _command_connect_to_gamecard(gameboy_config CONFIG) {
	int a_socket = create_socket();
	if(connect_socket(a_socket, CONFIG.gamecard_ip, CONFIG.gamecard_port) == failed) {
		log_error(LOGGER, "Error opening gamecard socket");
		return failed;
	} else {
		return a_socket;
	}
}

void info() {
	printf("----GAMEBOY CONSOLE----\n");
	printf("-----------Comandos de Broker:\n");
	printf("-------------------BROKER NEW_POKEMON [POKEMON] [POSX] [POSY] [CANTIDAD]\n");
	printf("-------------------BROKER APPEARED_POKEMON [POKEMON] [POSX] [POSY] [ID_MENSAJE_CORRELATIVO]\n");
	printf("-------------------BROKER CATCH_POKEMON [POKEMON] [POSX] [POSY]\n");
	printf("-------------------BROKER CAUGHT_POKEMON [ID_MENSAJE_CORRELATIVO] [OK/FAIL]\n");
	printf("-------------------BROKER GET_POKEMON [POKEMON]\n");
	printf("-----------Comandos de Team:\n");
	printf("-------------------TEAM APPEARED_POKEMON [POKEMON] [POSX] [POSY]\n");
	printf("-----------Comandos de Gamecard:\n");
	printf("-------------------GAMECARD NEW_POKEMON [POKEMON] [POSX] [POSY] [CANTIDAD] [ID_MENSAJE]\n");
	printf("-------------------GAMECARD CATCH_POKEMON [POKEMON] [POSX] [POSY] [ID_MENSAJE]\n");
	printf("-------------------GAMECARD GET_POKEMON [POKEMON] [ID_MENSAJE]\n");
	printf("-----------Comandos de Suscripción:\n");
	printf("-------------------SUSCRIPTOR [COLA_DE_MENSAJES] [TIEMPO]\n");
}

void _command_broker_new_pokemon(queue_message * data, gameboy_config CONFIG) {
	int socket = _command_connect_to_broker(CONFIG);
	send_pokemon_message(socket, data, 1, -1);

	close_socket(socket);
}
void _command_broker_appeared_pokemon(queue_message * data, int correlative_id, gameboy_config CONFIG) {
	int socket = _command_connect_to_broker(CONFIG);
	send_pokemon_message_with_id(socket, data, 1, -1, correlative_id);

	close_socket(socket);
}
void _command_broker_catch_pokemon(queue_message * data, gameboy_config CONFIG) {
	int socket = _command_connect_to_broker(CONFIG);
	send_pokemon_message(socket, data, 1, -1);

	close_socket(socket);
}
void _command_broker_caught_pokemon(queue_message * data, int correlative_id, gameboy_config CONFIG) {
	int socket = _command_connect_to_broker(CONFIG);
	send_pokemon_message_with_id(socket, data, 1, -1, correlative_id);

	close_socket(socket);
}
void _command_broker_get_pokemon(queue_message * data, gameboy_config CONFIG) {
	int socket = _command_connect_to_broker(CONFIG);
	send_pokemon_message(socket, data, 1, -1);

	close_socket(socket);
}

void _command_team_appeared_pokemon(queue_message * data, gameboy_config CONFIG) {
	int socket = _command_connect_to_team(CONFIG);
	send_pokemon_message(socket, data, 0, -1);

	close_socket(socket);
}

void _command_gamecard_new_pokemon(queue_message * data, int message_id, gameboy_config CONFIG) {
	int socket = _command_connect_to_gamecard(CONFIG);
	send_pokemon_message_with_id(socket, data, 0, message_id, -1);

	close_socket(socket);
}
void _command_gamecard_catch_pokemon(queue_message * data, int message_id, gameboy_config CONFIG) {
	int socket = _command_connect_to_gamecard(CONFIG);
	send_pokemon_message_with_id(socket, data, 0, message_id, -1);

	close_socket(socket);
}
void _command_gamecard_get_pokemon(queue_message * data, int message_id, gameboy_config CONFIG) {
	int socket = _command_connect_to_gamecard(CONFIG);
	send_pokemon_message_with_id(socket, data, 0, message_id, -1);

	close_socket(socket);
}

void _command_subscribe_to_queue(_message_queue_name queue, int seconds, gameboy_config CONFIG) {
	if(queue == -1){
		printf("Cola No Reconocida\n");
		return;
	}
	int reading = 1;
	int broker_socket = _command_connect_to_broker(CONFIG);
	void * await() {
		sleep(seconds);
		reading = 0;

		unsubscribe_from_queue(broker_socket, queue);
		close_socket(broker_socket);
		printf("Finalizada la Escucha\n");

		exit(1);
		return NULL;
	}
	pthread_t awaiting_thread;
	pthread_create(&awaiting_thread, NULL, await, NULL);
	subscribe_to_queue(broker_socket, queue);
	void * listen() {
		printf("Aguardando mensajes...\n\n");
		while(reading) {
			net_message_header * header = malloc(sizeof(net_message_header));
			read(broker_socket, header, sizeof(net_message_header));

			queue_message * message = receive_pokemon_message(broker_socket);
			send_message_acknowledge(message, broker_socket);

			printf("Mensaje entrante con MID %d\n", message->header->message_id);
			print_pokemon_message_by_printf(message);
			printf("\n");
		}
		return NULL;
	}
	pthread_t listening_thread;
	pthread_create(&listening_thread, NULL, listen, NULL);

	pthread_join(awaiting_thread, NULL);
	pthread_cancel(listening_thread);
}
