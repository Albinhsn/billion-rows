CC := clang++-18
CFLAGS := -flto -march=native -mtune=native -pthread -O2 -Wno-missing-braces
TARGET := main 


b: 
	$(CC) ./src/main.cpp -o $(TARGET) $(CFLAGS)

len:
	find . -name '*.cpp' | xargs wc -l

clean:
	rm -rf obj/ $(TARGET)

.PHONY: all clean

