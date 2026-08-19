all: toyforth

toyforth: toyforth.c
	$(CC) toyforth.c -Wall -W -O2 -std=c2x -o toyforth

clean:
	rm -rf toyforth