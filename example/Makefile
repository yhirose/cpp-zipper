all: example
	rm -rf out
	./example

example: ../zipper.h example.cpp
	g++ -std=c++17 -o example example.cpp -I.. -lminizip
