#include <library/library.h>
#include <library/gameboy_console.h>
#include <time.h>

//GLOBAL VARIABLES
gameboy_config CONFIG;

//PROTOTYPES
void setup(int argc, char **argv);

int main(int argc, char **argv) {
	setup(argc, argv);

	int test = 0;

	if(test) {
		char * pokemons[] = { "Bulbasaur", "Ivysaur", "Venusaur", "Charmander", "Charmeleon",
			"Charizard", "Squirtle", "Wartortle", "Blastoise", "Caterpie", "Metapod", "Butterfree", "Weedle", "Pidgeotto", "Pidgey", "Beedrill", "Kakuna",
			"Nidoran-F", "Sandslash", "Sandshrew", "Raichu", "Pikachu", "Arbok", "Ekans", "Fearow", "Spearow", "Raticate", "Rattata", "Pidgeot",
			"Ninetales", "Vulpix", "Clefable", "Clefairy", "Nidoking", "Nidorino", "Nidoran-M", "Nidoqueen", "Nidorina"
		};
		int pokemons_c = (int) sizeof(pokemons) /
				sizeof(pokemons[0]);

		srand (time(NULL));

		int socket = _command_connect_to_broker(CONFIG);
		int i, type;

		for(i=0 ; i>=0; i++) {

			char * selected_pokemon = pokemons[rand() % pokemons_c];

			type = rand()%6;
			type = 4;

			switch(type) {
				case NEW_POKEMON:;
						send_pokemon_message(socket, new_pokemon_create(selected_pokemon, rand()%21, rand()%21, rand()%6), 1, -1);
					break;
				case APPEARED_POKEMON:;
						send_pokemon_message(socket, appeared_pokemon_create(selected_pokemon, rand()%21, rand()%21), 1, -1);
					break;
				case CATCH_POKEMON:;
						send_pokemon_message(socket, catch_pokemon_create(selected_pokemon, rand()%21, rand()%21), 1, -1);
					break;
				case CAUGHT_POKEMON:;
						send_pokemon_message(socket, caught_pokemon_create(rand()%2), 1, -1);
					break;
				case GET_POKEMON:;
						send_pokemon_message(socket, get_pokemon_create(selected_pokemon), 1, -1);
					break;
				case LOCALIZED_POKEMON:;
						queue_message * mymessage = localized_pokemon_create(selected_pokemon, list_create());
						localized_pokemon_add_location(mymessage->payload, location_create(rand()%21, rand()%21));
						localized_pokemon_add_location(mymessage->payload, location_create(rand()%21, rand()%21));
						localized_pokemon_add_location(mymessage->payload, location_create(rand()%21, rand()%21));
						send_pokemon_message(socket, mymessage, 1, -1);
						send_pokemon_message(socket, mymessage, 1, -1);
					break;
			}

			usleep(1000 * 1000);
		}

		close_socket(socket);
	} else {
		gameboy_console_launcher(CONFIG);
	}

	/*_command_broker_new_pokemon(new_pokemon_create("Pikachu", 5, 2, 3), CONFIG);
	_command_broker_appeared_pokemon(appeared_pokemon_create("Pikachu", 5, 2), 32, CONFIG);
	_command_broker_catch_pokemon(catch_pokemon_create("Pikachu", 5, 2), CONFIG);
	_command_broker_caught_pokemon(caught_pokemon_create(1), 33, CONFIG);
	_command_broker_get_pokemon(get_pokemon_create("Pikachu"), CONFIG);

	_command_team_appeared_pokemon(appeared_pokemon_create("Pikachu", 5, 7), CONFIG);

	_command_gamecard_new_pokemon(new_pokemon_create("Pikachu", 5, 2, 3), CONFIG);
	_command_gamecard_catch_pokemon(catch_pokemon_create("Pikachu", 5, 2), CONFIG);
	_command_gamecard_get_pokemon(get_pokemon_create("Pikachu"), CONFIG);*/

	return EXIT_SUCCESS;
}

void setup(int argc, char **argv) {
	char * log_path = string_new();
	string_append(&log_path, (argc > 1) ? argv[1] : "gameboy");
	string_append(&log_path, ".log");
	LOGGER = log_create(log_path, (argc > 1) ? argv[1] : "gameboy", false, LOG_LEVEL_INFO);

	char * cfg_path = string_new();
	string_append(&cfg_path, (argc > 1) ? argv[1] : "gameboy");
	string_append(&cfg_path, ".cfg");
	_CONFIG = config_create(cfg_path);

	CONFIG.broker_ip = config_get_string_value(_CONFIG, "IP_BROKER");
	CONFIG.team_ip = config_get_string_value(_CONFIG, "IP_TEAM");
	CONFIG.gamecard_ip = config_get_string_value(_CONFIG, "IP_GAMECARD");
	CONFIG.broker_port = config_get_int_value(_CONFIG, "PUERTO_BROKER");
	CONFIG.team_port = config_get_int_value(_CONFIG, "PUERTO_TEAM");
	CONFIG.gamecard_port = config_get_int_value(_CONFIG, "PUERTO_GAMECARD");
}
