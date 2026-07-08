CXX = g++
CXXFLAGS = -Wall -Wextra -Wpedantic -O2 -std=c++17 -Iinclude
SRC = src/allocator.cpp src/main.cpp
OUT = allocator.exe

all: $(OUT)

$(OUT): $(SRC) include/allocator.hpp
	$(CXX) $(CXXFLAGS) -o $(OUT) $(SRC)

run: $(OUT)
	./$(OUT)

clean:
	del /Q $(OUT) 2>nul

.PHONY: all clean run
