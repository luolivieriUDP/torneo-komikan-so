#define _POSIX_C_SOURCE 200809L
#include "tablero.h"

enum { TRONO = 25, LATERAL_IZQUIERDO = 26, LATERAL_DERECHO = 27 };

// Grafo inmutable despues del arranque. salto[a][b] guarda la casilla intermedia + 1.
static bool vecino[CASILLAS][CASILLAS];
static int salto[CASILLAS][CASILLAS];

static void conectar(int a, int b){
    vecino[a][b] = vecino[b][a] = true;
}

void preparar_grafo(void){
    int x[CASILLAS], y[CASILLAS];
    for(int i = 0; i < 25; i++){
        x[i] = i % 5;
        y[i] = i / 5 + 2;
        if(x[i] > 0) conectar(i, i - 1);
        if(i >= 5) conectar(i, i - 5);
    }
    x[TRONO] = 2; y[TRONO] = 0;
    x[LATERAL_IZQUIERDO] = 1; y[LATERAL_IZQUIERDO] = 1;
    x[LATERAL_DERECHO] = 3; y[LATERAL_DERECHO] = 1;
    conectar(TRONO, LATERAL_IZQUIERDO);
    conectar(TRONO, LATERAL_DERECHO);
    conectar(TRONO, 2);
    conectar(LATERAL_IZQUIERDO, LATERAL_DERECHO);
    conectar(LATERAL_IZQUIERDO, 0);
    conectar(LATERAL_IZQUIERDO, 1);
    conectar(LATERAL_DERECHO, 3);
    conectar(LATERAL_DERECHO, 4);
    // Dos aristas forman un salto cuando sus tres casillas estan alineadas.
    for(int a = 0; a < CASILLAS; a++){
        for(int medio = 0; medio < CASILLAS; medio++){
            if(!vecino[a][medio]) continue;
            for(int b = 0; b < CASILLAS; b++){
                if(b == a || !vecino[medio][b]) continue;
                int dx = x[medio] - x[a], dy = y[medio] - y[a];
                if(dx * (y[b] - y[medio]) == dy * (x[b] - x[medio]))
                    salto[a][b] = medio + 1;
            }
        }
    }
}

void iniciar_tablero(Tablero* tablero, int puma, const int dogs[12]){
    *tablero = (Tablero){0};
    tablero->casillas[puma] = PUMA;
    for(int i = 0; i < 12; i++) tablero->casillas[dogs[i]] = i + 2;
}

// -1 invalido, 0 paso simple, positivo indice intermedio + 1.
static int tipo_jugada(const Tablero* tablero, int ficha, Jugada jugada){
    if(tablero->casillas[jugada.origen] != ficha || tablero->casillas[jugada.destino] != 0) return -1;
    if(vecino[jugada.origen][jugada.destino]) return 0;
    int paso = salto[jugada.origen][jugada.destino];
    if(ficha == PUMA && paso && tablero->casillas[paso - 1] > PUMA) return paso;
    return -1;
}

int jugadas_validas(const Tablero* tablero, int ficha, Jugada* jugadas){
    int desde = 0, n = 0;
    while(desde < CASILLAS && tablero->casillas[desde] != ficha) desde++;
    if(desde == CASILLAS) return 0;
    for(int i = 0; i < CASILLAS; i++){
        Jugada jugada = {desde, i};
        if(tipo_jugada(tablero, ficha, jugada) >= 0) jugadas[n++] = jugada;
    }
    return n;
}

bool aplicar_jugada(Tablero* tablero, int ficha, Jugada jugada, int* comio){
    // El tablero puede haber cambiado mientras la solicitud esperaba en la cola.
    int paso = tipo_jugada(tablero, ficha, jugada);
    *comio = paso > 0;
    if(paso < 0) return false;
    if(paso > 0){
        tablero->casillas[paso - 1] = 0;
        tablero->capturas++;
    }
    tablero->casillas[jugada.origen] = 0;
    tablero->casillas[jugada.destino] = ficha;
    return true;
}

bool es_salto(Jugada jugada){
    return !vecino[jugada.origen][jugada.destino];
}
