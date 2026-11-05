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

static uint32_t azar_siguiente(uint32_t* estado){
    uint32_t x = *estado; // xorshift32
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *estado = x;
    return x;
}

int main(int argc, char** argv){
    Servidor servidor = {0};
    if(leer_argumentos(&servidor, argc, argv) != 0)
        return EXIT_FAILURE;
    preparar_grafo();
    if(servidor.total != 2 || servidor.capacidad != 1 || servidor.config.abandon_player != 0){
        fprintf(stderr, "Esta version admite N=2, M=1 y abandon_player=0.\n");
        return EXIT_FAILURE;
    }
    Tablero tablero;
    iniciar_tablero(&tablero, servidor.config.puma, servidor.config.dogs);
    uint32_t azar = servidor.config.seed ? servidor.config.seed : 1;
    Resultado resultado = resultado_tablero(&tablero);
    unsigned long movimientos = 0;
    while(resultado == RES_JUGANDO){
        for(int ficha = 1; ficha <= FICHAS && resultado == RES_JUGANDO; ficha++){
            Jugada jugadas[CASILLAS];
            int cantidad = jugadas_validas(&tablero, ficha, jugadas);
            if(ficha == PUMA){
                int capturas = 0;
                for(int i = 0; i < cantidad; i++)
                    if(es_salto(jugadas[i])) jugadas[capturas++] = jugadas[i];
                if(capturas) cantidad = capturas;
            }
            if(!cantidad) continue;
            Jugada jugada = jugadas[azar_siguiente(&azar) % (unsigned int)cantidad];
            int comio;
            if(aplicar_jugada(&tablero, ficha, jugada, &comio)){
                movimientos++;
                resultado = resultado_tablero(&tablero);
                if(comio || movimientos % (unsigned int)servidor.config.log_every == 0)
                    imprimir_tablero(&tablero, stdout);
            }
        }
    }
    imprimir_tablero(&tablero, stdout);
    printf("Gana %s. Movimientos: %lu.\n", resultado == RES_PUMA ? "Puma" : "Perros", movimientos);
    return EXIT_SUCCESS;
}
