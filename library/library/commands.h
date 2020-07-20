#ifndef COMMANDS_H_
#define COMMANDS_H_

#include "library.h"

int _command_connect_to_broker(gameboy_config CONFIG);
int _command_connect_to_team(gameboy_config CONFIG);
int _command_connect_to_gamecard(gameboy_config CONFIG);

void info();

void _command_broker_new_pokemon(queue_message * data, gameboy_config CONFIG);
void _command_broker_appeared_pokemon(queue_message * data, int correlative_id, gameboy_config CONFIG);
void _command_broker_catch_pokemon(queue_message * data, gameboy_config CONFIG);
void _command_broker_caught_pokemon(queue_message * data, int correlative_id, gameboy_config CONFIG);
void _command_broker_get_pokemon(queue_message * data, gameboy_config CONFIG);

void _command_team_appeared_pokemon(queue_message * data, gameboy_config CONFIG);

void _command_gamecard_new_pokemon(queue_message * data, int message_id, gameboy_config CONFIG);
void _command_gamecard_catch_pokemon(queue_message * data, int message_id, gameboy_config CONFIG);
void _command_gamecard_get_pokemon(queue_message * data, int message_id, gameboy_config CONFIG);

void _command_subscribe_to_queue(_message_queue_name queue, int seconds, gameboy_config CONFIG);

#endif /* COMMANDS_H_ */
