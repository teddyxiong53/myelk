.PHONY : all

all: 
	gcc -c elk.c -o elk.o
	gcc -c test.c -o test.o
	gcc test.o elk.o -o test

clean:
	rm -f test *.o