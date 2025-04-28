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

install: all
	cp ./server /usr/local/bin/astream
	cp ./audio-stream.service /usr/lib/systemd/system/
	systemctl daemon-reload

uninstall:
	rm /usr/local/bin/astream
	rm /usr/lib/systemd/system/audio-stream.service
	systemctl daemon-reload
