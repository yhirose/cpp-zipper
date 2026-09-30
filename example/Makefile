MINIZIP = $(shell pkg-config --variable=includedir minizip 2>/dev/null)
CXXFLAGS = -std=c++17 -Wall -Wextra -I.. $(if $(MINIZIP),-I$(MINIZIP))
LDLIBS = $(shell pkg-config --libs minizip 2>/dev/null || echo -lminizip) -lz

all: example test
	./example
	./test

example: ../zipper.h example.cpp
	$(CXX) $(CXXFLAGS) -o example example.cpp $(LDLIBS)

test: ../zipper.h test.cpp
	$(CXX) $(CXXFLAGS) -o test test.cpp $(LDLIBS)

clean:
	rm -f example test test_memory.zip
