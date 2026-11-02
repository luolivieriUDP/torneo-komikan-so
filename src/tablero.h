#ifndef TABLERO_H
#define TABLERO_H

#include <stddef.h>

//tablero del komikan tal cual el dibujo del pdf: cuadricula de 5x5 + trono de 3 nodos arriba
//
//              T                 filas: A (la de arriba, pegada al trono) ... E (la de abajo)
//          TI     TD             columnas: 1 ... 5
//     A  A1 A2 A3 A4 A5          trono: T (la punta), TI (trono izquierda), TD (trono derecha)
//     B  B1 B2 B3 B4 B5
//     C  C1 C2 C3 C4 C5          en total 25 + 3 = 28 nodos. estos mismos nombres se usan
//     D  D1 D2 D3 D4 D5          en el config.txt y en el log, pa no tener dos convenciones
//     E  E1 E2 E3 E4 E5

#define FILAS 5
#define COLUMNAS 5
#define NODOS_GRILLA (FILAS * COLUMNAS)
#define NODO_T (NODOS_GRILLA)          //25
#define NODO_TI (NODOS_GRILLA + 1)     //26
#define NODO_TD (NODOS_GRILLA + 2)     //27
#define NODOS (NODOS_GRILLA + 3)       //28
#define NODO_GRILLA(f, c) ((f) * COLUMNAS + (c))

#define PIEZAS 13             //0 = puma, 1..12 = perros
#define PUMA 0
#define NUM_PERROS 12
#define CAPTURAS_PARA_GANAR 6
#define VACIO (-1)
#define MAX_VECINOS 8         //en este tablero el maximo real es 4, dejamos holgura
#define MAX_SALTOS 8
#define MAX_MOVS (MAX_VECINOS + MAX_SALTOS)
#define TAM_DIBUJO 512

typedef struct {
    int ocupante[NODOS];   //VACIO, PUMA o el numero del perro (1..12) q esta en cada nodo
    int pos[PIEZAS];       //nodo de cada pieza, VACIO si el perro ya fue capturado
    int capturas;          //perros capturados DE VERDAD (no intentos ni rechazos)
} Tablero;

typedef struct {
    int pieza;
    int origen;
    int destino;
    int capturado_nodo;    //VACIO si es un paso normal
    int capturado_perro;   //q perro se come el puma, VACIO si no hay captura
} Movimiento;

typedef struct {
    int sobre;     //nodo q se salta (ahi tiene q haber un perro)
    int destino;   //donde aterriza el puma
} Salto;

//resultado de validar un movimiento
enum {
    MOV_OK = 0,
    MOV_PIEZA_INVALIDA,    //id de pieza fuera de rango
    MOV_CAPTURADA,         //ese perro ya no esta en el tablero
    MOV_ORIGEN_DISTINTO,   //la pieza ya no esta donde decia estar (decidio con una foto vieja)
    MOV_NODO_INVALIDO,     //origen o destino q no existen
    MOV_NO_ADYACENTE,      //no hay linea q los una (ni un salto valido, pal puma)
    MOV_DESTINO_OCUPADO,
    MOV_SALTO_SIN_PERRO    //el puma quiso saltar pero en medio no hay perro
};

//como queda la partida segun las reglas del tablero
enum { SIGUE = 0, GANA_PUMA_CAPTURAS, GANAN_PERROS_ACORRALADO };

void tablero_iniciar_topologia(void);  //arma vecinos, saltos y distancias. se llama UNA vez antes de crear hilos
const char* tablero_nombre(int nodo);
int tablero_nodo(const char* nombre);   //"C3" -> nodo, -1 si no existe
int tablero_adyacentes(int a, int b);
int tablero_vecinos(int nodo, const int** lista);
int tablero_saltos(int nodo, const Salto** lista);
int tablero_distancia(int a, int b);
int tablero_colocar(Tablero* t, int puma, const int perros[NUM_PERROS]);
int tablero_validar(const Tablero* t, int pieza, int origen, int destino, Movimiento* mov);
void tablero_aplicar(Tablero* t, const Movimiento* mov);
int tablero_movs_puma(const Tablero* t, Movimiento* out);           //out puede ser NULL (solo contar)
int tablero_movs_perro(const Tablero* t, int perro, Movimiento* out);
int tablero_evaluar(const Tablero* t);
int tablero_verificar(const Tablero* t, char* err, size_t n);
void tablero_dibujar(const Tablero* t, char* buf, size_t n);
const char* tablero_motivo_rechazo(int codigo);

#endif
