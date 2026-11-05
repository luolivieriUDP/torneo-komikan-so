#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "config.h"
#include "tablero.h"

typedef struct {
    Config config;
    int total, capacidad;
} Servidor;

static int entero_positivo(const char* texto, int* valor){
    char* fin;
    errno = 0;
    long leido = strtol(texto, &fin, 10);
    if(errno || fin == texto || *fin || leido < 1 || leido > INT_MAX)
        return -1;
    *valor = (int)leido;
    return 0;
}

static int leer_argumentos(Servidor* servidor, int argc, char** argv){
    char detalle[256];
    if(argc < 3 || argc > 4 || entero_positivo(argv[1], &servidor->total) != 0 ||
       entero_positivo(argv[2], &servidor->capacidad) != 0){
        fprintf(stderr, "Uso: %s N M [config.txt] (N y M deben ser positivos)\n", argv[0]);
        return -1;
    }
    configuracion_inicial(&servidor->config);
    if(argc == 4 && cargar_configuracion(&servidor->config, argv[3], detalle, sizeof(detalle)) != 0){
        fprintf(stderr, "Configuracion: %s\n", detalle);
        return -1;
    }
    if(servidor->config.abandon_player > servidor->total){
        fprintf(stderr, "abandon_player no puede superar N.\n");
        return -1;
    }
    return 0;
}

int main(int argc, char** argv){
    Servidor servidor = {0};
    if(leer_argumentos(&servidor, argc, argv) != 0)
        return EXIT_FAILURE;
    preparar_grafo();
    Tablero tablero;
    iniciar_tablero(&tablero, servidor.config.puma, servidor.config.dogs);
    printf("Configuracion cargada: %d jugadores, limite %d mesas.\n", servidor.total, servidor.capacidad);
    imprimir_tablero(&tablero, stdout);
    return EXIT_SUCCESS;
}
