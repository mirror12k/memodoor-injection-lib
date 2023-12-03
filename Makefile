
install_dependencies:
	apt-get install -y python3-pip python3-dev libpython3.10-dev gcc

build:
	gcc -I/usr/include/python3.10 -shared -o library.so -fPIC -lpthread src/library.c -lpython3.10

test:
	LD_PRELOAD=./library.so sh
