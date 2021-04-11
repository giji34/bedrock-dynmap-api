all: main

clean:
	rm -f main

main: main.cpp
	g++ -std=c++17 main.cpp -o main

run: main
	./main $$(ps aux | grep bedrock_server | head -1 | awk '{print $$2}')

fmt:
	find . -name '*.cpp' | xargs -n 1 -P `nproc` clang-format -i
