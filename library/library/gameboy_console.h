#ifndef GAMEBOY_CONSOLE_H_
#define GAMEBOY_CONSOLE_H_

#include "commands.h"

typedef enum{ EXIT=0, INFO,
	BROKER_NEW, BROKER_APPEARED, BROKER_CATCH, BROKER_CAUGHT, BROKER_GET,
	TEAM_APPEARED,
	GAMECARD_NEW, GAMECARD_CATCH, GAMECARD_GET,
	SUSCRIPTOR} commands;

void * gameboy_console_launcher(gameboy_config CONFIG);
int parse(char **linea, char **command, char **param1, char **param2, char **param3, char **param4, char **param5);
void execute(int, char*, char*, char*, char*, char*, gameboy_config CONFIG);
int find_in_array(char*, const char**, int);
int execute_full_line(char * linea, int console_mode, int * quit, gameboy_config CONFIG);

#endif
