all: tracer

clean:
	rm -f tracer

tracer: main.cpp
	g++ -std=c++17 -O2 main.cpp -lpthread -o tracer

run: tracer
	./tracer $$(ps aux | grep bedrock_server | head -1 | awk '{print $$2}')

fmt:
	find . -name '*.cpp' | xargs -n 1 -P `nproc` clang-format -i
