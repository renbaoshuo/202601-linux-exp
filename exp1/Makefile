all: main hello

main: main.o child.o
	gcc -o main main.o child.o

hello: hello.c
	gcc -o hello hello.c

clean:
	rm -f main hello *.o

.PHONY: all clean
