CC = gcc
CFLAGS = -Wall -Wextra -std=c17
FUENTES = src/main.c src/tablero.c src/config.c

torneo: $(FUENTES) src/tablero.h src/config.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -pthread $(FUENTES) -o $@

.PHONY: clean
clean:
	$(RM) torneo
