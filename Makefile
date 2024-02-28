CC := g++
CFLAGS := -flto -march=native -mtune=native -pthread -O2 -Wall -Wno-missing-braces
TARGET := main 

SRCS = $(wildcard src/*.cpp)
OBJS = $(patsubst src/%.cpp,obj/%.o,$(SRCS))

b: $(TARGET)
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ -lm

obj/%.o: src/%.cpp
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@

r:
	./main > image.ppm && display image.ppm

len:
	find . -name '*.cpp' | xargs wc -l

clean:
	rm -rf obj/ $(TARGET)

.PHONY: all clean

