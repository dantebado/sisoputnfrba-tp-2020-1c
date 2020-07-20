cd library
sudo make install
cd ./../

cd gameboy/src
make
mv gameboy ./../../deploy
cd ./../../

cd gamecard/src
make
mv gamecard ./../../deploy
cd ./../../

cd broker/src
make
mv broker ./../../deploy
cd ./../../

cd team/src
make
mv team ./../../deploy
cd ./../../
