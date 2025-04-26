# Makefile for video_stream.cpp

# Compiler
CXX = g++

# Compiler flags
CXXFLAGS = -std=c++11 `pkg-config --cflags opencv4`

# Linker flags
LDFLAGS = `pkg-config --libs opencv4`

# Target executable
TARGET = main

# Source file
SOURCES = main.cpp

# Object file
OBJECTS = $(SOURCES:.cpp=.o)

# Default target
all: $(TARGET)

# Link object files to create executable
$(TARGET): $(OBJECTS)
	$(CXX) $(OBJECTS) -o $(TARGET) $(LDFLAGS)

# Compile source files to object files
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Clean up build artifacts
clean:
	rm -f $(OBJECTS) $(TARGET)

install:
	cp ./main /usr/local/bin/vstream
	cp ./video-stream.service /usr/lib/systemd/system/
	systemctl daemon-reload

# Phony targets
.PHONY: all clean
