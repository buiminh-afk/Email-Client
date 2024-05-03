# Makefile for compiling a single C++ file without specifying version or flags

# Compiler
CXX = g++

# Source file
SRC = 21127105_21127466_21127584.cpp

# Executable name
TARGET = 21127105_21127466_21127584

all: $(TARGET)

$(TARGET): $(SRC)
	$(CXX) -o $@ $<

clean:
	rm -f $(TARGET)
