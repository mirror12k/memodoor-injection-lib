
install_dependencies:
	apt-get install -y python3-pip python3-dev libpython3.10-dev gcc

build:
	gcc -I/usr/include/python3.10 -shared -o memodoor_inject.so -fPIC -lpthread src/library.c -lpython3.10

unittest:
	python3 unit_test.py

test:
	LD_PRELOAD=./memodoor_inject.so sh
