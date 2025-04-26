CXX = g++
CXXFLAGS = -std=c++11 -Wall
LDFLAGS = -lasound -pthread

all: server

server: server.o
	$(CXX) -o server server.o $(LDFLAGS)

server.o: server.cpp
	$(CXX) $(CXXFLAGS) -c server.cpp -o server.o

clean:
	rm -f server server.o
