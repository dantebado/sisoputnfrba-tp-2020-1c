#include "gameboy_console.h"

#include <readline/readline.h>
#include <readline/history.h>

void * gameboy_console_launcher(gameboy_config CONFIG) {
	char *linea;
	int quit = 0;

	printf("Bienvenido al GameBoy\n");
	printf("Escribi 'info' para obtener una lista de comandos\n");

	while(quit == 0){
		linea = readline(">>> ");
		add_history(linea);

		execute_full_line(linea, true, &quit, CONFIG);

		free(linea);
	}
	return EXIT_SUCCESS;
}

int execute_full_line(char * linea, int console_mode, int * quit, gameboy_config CONFIG) {
	char *command = NULL, *param1, *param2, *param3, *param4, *param5;
	int command_number;

	const char* command_list[] = { "salir", "info",
			"BROKER NEW_POKEMON", "BROKER APPEARED_POKEMON", "BROKER CATCH_POKEMON", "BROKER CAUGHT_POKEMON", "BROKER GET_POKEMON",
			"TEAM APPEARED_POKEMON",
			"GAMECARD NEW_POKEMON", "GAMECARD CATCH_POKEMON", "GAMECARD GET_POKEMON",
			"SUSCRIPTOR"};

	int command_list_length = (int) sizeof(command_list) /
			sizeof(command_list[0]);

	command = NULL;
	param1 = NULL;
	param2 = NULL;
	param3 = NULL;
	param4 = NULL;
	param5 = NULL;

	if(parse(&linea, &command, &param1, &param2, &param3, &param4, &param5)){
		if(console_mode)
			printf("Demasiados parámetros\n");
	} else {

		char * to_search = NULL;

		if(command == NULL) {
			to_search = linea;
		} else {
			to_search = malloc(sizeof(char) * (strlen(linea) + strlen(command) + 2));
			memcpy(to_search, linea, strlen(linea) * sizeof(char));
			to_search[strlen(linea)] = ' ';
			memcpy(to_search+strlen(linea)+1, command, strlen(command) * sizeof(char));
			to_search[strlen(linea) + strlen(command) + 1] = '\0';
		}

		command_number = find_in_array(to_search,
				command_list, command_list_length);

		command_number == EXIT ? *quit = 1 : execute(command_number,
				param1, param2, param3, param4, param5, CONFIG);
	}

	return true;
}

int parse(char **linea, char **command, char **param1, char **param2, char **param3, char **param4, char **param5){
	strtok(*linea, " ");
	int cvalue = strcmp(*linea, "SUSCRIPTOR");
	if(cvalue == 0) {
	} else {
		*command = strtok(NULL, " ");
	}
	*param1 = strtok(NULL, " ");
	*param2 = strtok(NULL, " ");
	*param3 = strtok(NULL, " ");
	*param4 = strtok(NULL, " ");
	*param5 = strtok(NULL, " ");
	return 0;
}

void execute(int command_number, char* param1, char* param2, char* param3, char* param4, char* param5, gameboy_config CONFIG){
	switch(command_number){
		case -1:
			printf("Comando no reconocido\n");
			break;
		case INFO:
			if(!param1 && !param2 && !param3 && !param4 && !param5){
				info();
			} else {
				printf("El comando 'info' no recibe parametros\n");
			}
			break;
		case BROKER_NEW:
			if(param1 && param2 && param3 && param4 && !param5){
				uint32_t x = atoi(param2);
				uint32_t y = atoi(param3);
				uint32_t count = atoi(param4);
				_command_broker_new_pokemon(new_pokemon_create(param1, x, y, count), CONFIG);
			} else {
				printf("Revise los parámetros\n");
			}
			break;
		case BROKER_APPEARED:
			if(param1 && param2 && param3 && param4 && !param5){
				uint32_t x = atoi(param2);
				uint32_t y = atoi(param3);
				int correlative_id = atoi(param4);
				_command_broker_appeared_pokemon(appeared_pokemon_create(param1, x, y), correlative_id, CONFIG);
			} else {
				printf("Revise los parámetros\n");
			}
			break;
		case BROKER_CATCH:
			if(param1 && param2 && param3 && !param4 && !param5){
				uint32_t x = atoi(param2);
				uint32_t y = atoi(param3);
				_command_broker_catch_pokemon(catch_pokemon_create(param1, x, y), CONFIG);
			} else {
				printf("Revise los parámetros\n");
			}
			break;
		case BROKER_CAUGHT:
			if(param1 && param2 && !param3 && !param4 && !param5){
				uint32_t result = 0;
				if(strcmp(param2, "OK") == 0) {
					result = 1;
				} else {
					result = 0;
				}
				uint32_t correlative_id = atoi(param1);
				_command_broker_caught_pokemon(caught_pokemon_create(result), correlative_id, CONFIG);
			} else {
				printf("Revise los parámetros\n");
			}
			break;
		case BROKER_GET:
			if(param1 && !param2 && !param3 && !param4 && !param5){
				_command_broker_get_pokemon(get_pokemon_create(param1), CONFIG);
			} else {
				printf("Revise los parámetros\n");
			}
			break;
		case TEAM_APPEARED:
			if(param1 && param2 && param3 && !param4 && !param5){
				uint32_t x = atoi(param2);
				uint32_t y = atoi(param3);
				_command_team_appeared_pokemon(appeared_pokemon_create(param1, x, y), CONFIG);
			} else {
				printf("Revise los parámetros\n");
			}
			break;
		case GAMECARD_NEW:
			if(param1 && param2 && param3 && param4 && param5){
				uint32_t x = atoi(param2);
				uint32_t y = atoi(param3);
				uint32_t count = atoi(param4);
				uint32_t message_id = atoi(param5);
				_command_gamecard_new_pokemon(new_pokemon_create(param1, x, y, count), message_id, CONFIG);
			} else {
				printf("Revise los parámetros\n");
			}
			break;
		case GAMECARD_CATCH:
			if(param1 && param2 && param3 && param4 && !param5){
				uint32_t x = atoi(param2);
				uint32_t y = atoi(param3);
				uint32_t id = atoi(param4);
				_command_gamecard_catch_pokemon(catch_pokemon_create(param1, x, y), id, CONFIG);
			} else {
				printf("Revise los parámetros\n");
			}
			break;
		case GAMECARD_GET:
			if(param1 && param2 && !param3 && !param4 && !param5){
				uint32_t id = atoi(param2);
				_command_gamecard_get_pokemon(get_pokemon_create(param1), id, CONFIG);
			} else {
				printf("Revise los parámetros\n");
			}
			break;
		case SUSCRIPTOR:
			if(param1 && param2 && !param3 && !param4 && !param5){
				int seconds = atoi(param2);
				_command_subscribe_to_queue(queue_name_to_enum(param1), seconds, CONFIG);
			} else {
				printf("Revise los parámetros\n");
			}
			break;
	}
}

int find_in_array(char* linea, const char** command_list, int length){
	for(int i = 0; i < length; i++) {
		if(strcmp(linea, command_list[i]) == 0) return i;
	}
	return -1;
}
