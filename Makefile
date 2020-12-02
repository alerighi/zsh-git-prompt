gitstatus: gitstatus.c
	$(CC) $< -ansi -o $@ -O2 -Wall -Wextra -pedantic

test: gitstatus
	pytest -s -vvvvv -rEfsxX --showlocals
