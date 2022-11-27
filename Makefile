CC=gcc
FLAGS=
OUT=multicopy

$(OUT): main.c
	$(CC) $(FLAGS) -o $(OUT) main.c

.PHONY: clean
clean:
	rm -rf $(OUT)
