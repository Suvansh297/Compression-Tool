CXX = clang++
CXXFLAGS = -std=c++17 -Wall -Wextra -Wpedantic

.PHONY: all run test clean

all: main

main: main.cpp
	$(CXX) $(CXXFLAGS) main.cpp -o main

run: main
	./main test.txt

test: main
	python3 -m unittest discover -s tests -v

clean:
	rm -f main
