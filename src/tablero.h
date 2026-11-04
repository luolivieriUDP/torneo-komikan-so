#ifndef KOMIKAN_TABLERO_H
#define KOMIKAN_TABLERO_H

#include <stdbool.h>
#include <stdio.h>

enum { CASILLAS = 28, FICHAS = 13, PUMA = 1 };
typedef struct { int casillas[CASILLAS], capturas; } Tablero;
typedef struct { int origen, destino; } Jugada;
typedef enum { RES_JUGANDO, RES_PUMA, RES_PERROS, RES_INTERRUMPIDA, RES_ERROR } Resultado;

void preparar_grafo(void);
void iniciar_tablero(Tablero* tablero, int puma, const int dogs[12]);
int jugadas_validas(const Tablero* tablero, int ficha, Jugada* jugadas);
bool aplicar_jugada(Tablero* tablero, int ficha, Jugada jugada, int* comio);
bool es_salto(Jugada jugada);

#endif
