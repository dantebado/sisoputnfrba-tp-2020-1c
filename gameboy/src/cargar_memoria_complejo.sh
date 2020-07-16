#!/bin/sh
./gameboy BROKER CATCH_POKEMON Pikachu 9 3 19
./gameboy BROKER CATCH_POKEMON Squirtle 9 3 20

./gameboy BROKER CAUGHT_POKEMON 10 OK
./gameboy BROKER CAUGHT_POKEMON 11 FAIL

./gameboy BROKER CATCH_POKEMON Bulbasaur 1 7 21
./gameboy BROKER CATCH_POKEMON Charmander 1 7 22

./gameboy BROKER GET_POKEMON Pichu 9
./gameboy BROKER GET_POKEMON Raichu 10
