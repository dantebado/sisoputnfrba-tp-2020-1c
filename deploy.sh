#cd ..
#git clone https://github.com/sisoputnfrba/so-commons-library.git
#cd so-commons-library
#sudo make uninstall
#sudo make install
#cd ..

#cd tp-2020-1c-LUCSO-

cd library
sudo make uninstall
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
