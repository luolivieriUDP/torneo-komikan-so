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

enum { AVISO_JUEZ = 0 };

#ifndef MOSTRAR_RECHAZOS
#define MOSTRAR_RECHAZOS 0      // Activar con -DMOSTRAR_RECHAZOS=1 para registrar las jugadas rechazadas.
#endif

typedef struct Servidor Servidor;
typedef struct Mesa Mesa;
enum { JUEGA_PUMA = 0, JUEGAN_PERROS = 1 };

typedef struct {
    Mesa* mesa;
    int numero;                  // 1 corresponde al Puma y 2..13 a los perros.
    uint32_t azar;               // Cada ficha mantiene su propia secuencia aleatoria.
} Ficha;

typedef struct {
    int ficha;
    Jugada jugada;
    long long envio_ns;           // Instante en que se encolo el pedido, en nanosegundos.
} Pedido;

struct Mesa {
    Servidor* servidor;
    int id, jugadores[2];        // jugadores[rol] = id del cliente
    unsigned int partida;
    pthread_t juez;
    bool creado, ocupada;
    pthread_mutex_t mutex;       // protege el estado de la partida
    pthread_cond_t aviso[FICHAS + 1]; // 0 para el juez, 1..13 para cada ficha
    Tablero tablero;
    Resultado resultado;
    bool apagar;
    int abandono;                // -1 nadie, 0 puma, 1 perros
    unsigned long movimientos, rechazos;
    long long latencia_total, latencia_maxima;
    Pedido cola[FICHAS];    // Cada ficha espera su respuesta antes de enviar otro pedido.
    int inicio_cola, pedidos_en_cola;
    bool pendiente[FICHAS + 1];
    Ficha fichas[FICHAS];
};

struct Servidor {
    Config config;
    int total, capacidad;
};

static void registrar_mesa(const Mesa* mesa, const char* fmt, ...){
    va_list args;
    flockfile(stdout);
    printf("[Mesa %d | partida %u] ", mesa->id, mesa->partida);
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    funlockfile(stdout);
}

static struct timespec ahora_ts(void){
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t;
}

static long long ahora_ns(void){
    struct timespec t = ahora_ts();
    return (long long)t.tv_sec * 1000000000LL + t.tv_nsec;
}

static struct timespec sumar_ms(struct timespec t, int ms){
    t.tv_sec += ms / 1000;
    t.tv_nsec += (long)(ms % 1000) * 1000000L;
    if(t.tv_nsec >= 1000000000L){
        t.tv_sec++;
        t.tv_nsec -= 1000000000L;
    }
    return t;
}

static uint32_t azar_siguiente(uint32_t* estado){
    uint32_t x = *estado; // xorshift32
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *estado = x;
    return x;
}

static int entero_positivo(const char* texto, int* valor){
    char* fin;
    errno = 0;
    long leido = strtol(texto, &fin, 10);
    if(errno || fin == texto || *fin || leido < 1 || leido > INT_MAX)
        return -1;
    *valor = (int)leido;
    return 0;
}

static bool partida_activa(const Mesa* mesa){
    return mesa->resultado == RES_JUGANDO && !mesa->apagar && mesa->abandono < 0;
}

static void despertar_mesa(Mesa* mesa){
    for(int i = 0; i <= FICHAS; i++)
        pthread_cond_broadcast(&mesa->aviso[i]);
}

static int mesa_iniciar_bloqueos(Mesa* mesa, const pthread_condattr_t* at){
    int inicializadas = 0;
    int error = pthread_mutex_init(&mesa->mutex, NULL);
    if(error != 0)
        return error;
    while(error == 0 && inicializadas <= FICHAS){
        error = pthread_cond_init(&mesa->aviso[inicializadas], at);
        if(error == 0)
            inicializadas++;
    }
    if(error != 0){ // Si falla una inicializacion, se deshacen las anteriores.
        while(inicializadas-- > 0)
            pthread_cond_destroy(&mesa->aviso[inicializadas]);
        pthread_mutex_destroy(&mesa->mutex);
    }
    return error;
}

static void mesa_destruir_bloqueos(Mesa* mesa){
    for(int i = 0; i <= FICHAS; i++)
        pthread_cond_destroy(&mesa->aviso[i]);
    pthread_mutex_destroy(&mesa->mutex);
}

static bool ficha_viva(const Mesa* mesa, int numero){
    for(int i = 0; i < CASILLAS; i++)
        if(mesa->tablero.casillas[i] == numero)
            return true;
    return false;
}

static bool ficha_dormir(Ficha* ficha){
    Mesa* mesa = ficha->mesa;
    const Config* config = &mesa->servidor->config;
    unsigned int rango = (unsigned int)(config->max_delay_ms - config->min_delay_ms + 1);
    int retardo = config->min_delay_ms + (int)(azar_siguiente(&ficha->azar) % rango);
    struct timespec plazo = sumar_ms(ahora_ts(), retardo);
    int error = 0;
    while(error != ETIMEDOUT && partida_activa(mesa))
        error = pthread_cond_timedwait(&mesa->aviso[ficha->numero], &mesa->mutex, &plazo);
    return partida_activa(mesa);
}

static bool ficha_elegir(Ficha* ficha, Jugada* elegida){
    Jugada jugadas[CASILLAS];
    int cantidad = jugadas_validas(&ficha->mesa->tablero, ficha->numero, jugadas);
    if(ficha->numero == PUMA){
        int capturas = 0;
        for(int i = 0; i < cantidad; i++)
            if(es_salto(jugadas[i]))
                jugadas[capturas++] = jugadas[i];
        if(capturas > 0) cantidad = capturas;
    }
    if(cantidad == 0)
        return false;
    *elegida = jugadas[azar_siguiente(&ficha->azar) % (unsigned int)cantidad];
    return true;
}

static void ficha_pedir(Ficha* ficha, Jugada jugada){
    Mesa* mesa = ficha->mesa;
    int cola_fin = (mesa->inicio_cola + mesa->pedidos_en_cola) % FICHAS;
    mesa->cola[cola_fin] = (Pedido){ficha->numero, jugada, ahora_ns()};
    mesa->pedidos_en_cola++;
    mesa->pendiente[ficha->numero] = true;
    pthread_cond_signal(&mesa->aviso[AVISO_JUEZ]); // solo el juez espera pedidos
    while(mesa->pendiente[ficha->numero] && partida_activa(mesa))
        pthread_cond_wait(&mesa->aviso[ficha->numero], &mesa->mutex);
}

static void* hilo_ficha(void* arg){
    Ficha* ficha = arg;
    Mesa* mesa = ficha->mesa;
    pthread_mutex_lock(&mesa->mutex);
    while(partida_activa(mesa)){
        if(!ficha_dormir(ficha)) break;
        Jugada jugada;
        if(ficha_elegir(ficha, &jugada))
            ficha_pedir(ficha, jugada);
        else if(!ficha_viva(mesa, ficha->numero))
            break; // El perro fue capturado.
    }
    pthread_mutex_unlock(&mesa->mutex);
    return NULL;
}

static Resultado resultado_externo(const Mesa* mesa){
    if(mesa->apagar)
        return RES_INTERRUMPIDA;
    if(mesa->abandono >= 0)
        return mesa->abandono == JUEGA_PUMA ? RES_PERROS : RES_PUMA;
    return RES_JUGANDO;
}

static void juez_imprimir(Mesa* mesa, const char* linea, bool con_tablero){
    Tablero foto = mesa->tablero;
    unsigned long movs = mesa->movimientos;
    pthread_mutex_unlock(&mesa->mutex);
    flockfile(stdout);
    if(linea)
        printf("[Mesa %d | partida %u] %s", mesa->id, mesa->partida, linea);
    if(con_tablero){
        printf("[Mesa %d | partida %u] Tablero, movimiento %lu, capturas %d:\n",
               mesa->id, mesa->partida, movs, foto.capturas);
        imprimir_tablero(&foto, stdout);
    }
    funlockfile(stdout);
    pthread_mutex_lock(&mesa->mutex);
}

static void juez_iniciar(Mesa* mesa){
    iniciar_tablero(&mesa->tablero, mesa->servidor->config.puma, mesa->servidor->config.dogs);
    mesa->resultado = resultado_externo(mesa);
    if(mesa->resultado == RES_JUGANDO)
        mesa->resultado = resultado_tablero(&mesa->tablero);
    juez_imprimir(mesa, NULL, true);
}

static int juez_crear_fichas(Mesa* mesa, pthread_t* hilos){
    int creadas = 0;
    for(int i = 0; i < FICHAS && mesa->resultado == RES_JUGANDO; i++){
        uint32_t semilla = mesa->servidor->config.seed ^ (mesa->partida * UINT32_C(2654435761)) ^
                           ((uint32_t)(i + 1) * UINT32_C(2246822519));
        mesa->fichas[i] = (Ficha){mesa, i + 1, semilla ? semilla : 1};
        int error = pthread_create(&hilos[i], NULL, hilo_ficha, &mesa->fichas[i]);
        if(error != 0){
            registrar_mesa(mesa, "no se pudo crear una ficha: %s.\n", strerror(error));
            mesa->resultado = RES_ERROR;
            break;
        }
        creadas++;
    }
    return creadas;
}

static void juez_atender(Mesa* mesa){
    Pedido pedido = mesa->cola[mesa->inicio_cola];
    mesa->inicio_cola = (mesa->inicio_cola + 1) % FICHAS;
    mesa->pedidos_en_cola--;
    int comio = 0;
    bool movio = aplicar_jugada(&mesa->tablero, pedido.ficha, pedido.jugada, &comio);
    mesa->pendiente[pedido.ficha] = false;
    pthread_cond_signal(&mesa->aviso[pedido.ficha]);
    long long espera = ahora_ns() - pedido.envio_ns;
    mesa->latencia_total += espera;
    if(espera > mesa->latencia_maxima)
        mesa->latencia_maxima = espera;
    char linea[96];
    if(movio){
        mesa->movimientos++;
        mesa->resultado = resultado_tablero(&mesa->tablero);
        if(comio || mesa->movimientos % (unsigned int)mesa->servidor->config.log_every == 0){
            snprintf(linea, sizeof(linea), "Ficha %d: %d -> %d%s.\n", pedido.ficha, pedido.jugada.origen,
                     pedido.jugada.destino, comio ? " (captura)" : "");
            juez_imprimir(mesa, linea, true);
        }
    } else {
        mesa->rechazos++; // la casilla ya no estaba libre o el pedido quedo obsoleto
        if(MOSTRAR_RECHAZOS){
            snprintf(linea, sizeof(linea), "Ficha %d: %d -> %d rechazado.\n", pedido.ficha,
                     pedido.jugada.origen, pedido.jugada.destino);
            juez_imprimir(mesa, linea, false);
        }
    }
}

static void juez_paso(Mesa* mesa){
    Resultado externo = resultado_externo(mesa);
    if(externo != RES_JUGANDO)
        mesa->resultado = externo;
    else if(mesa->pedidos_en_cola > 0)
        juez_atender(mesa);
    else
        pthread_cond_wait(&mesa->aviso[AVISO_JUEZ], &mesa->mutex);
}

static void juez_anunciar_fin(Mesa* mesa){
    if(mesa->resultado == RES_PUMA || mesa->resultado == RES_PERROS){
        int rol = mesa->resultado == RES_PUMA ? JUEGA_PUMA : JUEGAN_PERROS;
        const char* ganador = rol == JUEGA_PUMA ? "Puma" : "Perros";
        const char* motivo = rol == JUEGA_PUMA ? "6 capturas" : "acorralamiento";
        if(mesa->abandono >= 0) motivo = "abandono";
        registrar_mesa(mesa, "Gana %s (jugador %d) por %s.\n", ganador, mesa->jugadores[rol], motivo);
    } else
        registrar_mesa(mesa, "Partida %s.\n", mesa->resultado == RES_INTERRUMPIDA ? "interrumpida" : "abortada por error");
    registrar_mesa(mesa, "movimientos aceptados: %lu, rechazados: %lu.\n", mesa->movimientos, mesa->rechazos);
}

static void* hilo_juez(void* arg){
    Mesa* mesa = arg;
    pthread_t hilos[FICHAS];
    pthread_mutex_lock(&mesa->mutex);
    juez_iniciar(mesa);
    int creadas = juez_crear_fichas(mesa, hilos);
    while(mesa->resultado == RES_JUGANDO)
        juez_paso(mesa);
    despertar_mesa(mesa);
    juez_imprimir(mesa, NULL, true);
    juez_anunciar_fin(mesa);
    pthread_mutex_unlock(&mesa->mutex);
    for(int i = 0; i < creadas; i++) pthread_join(hilos[i], NULL);
    return NULL;
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
    if(servidor.total != 2 || servidor.capacidad != 1 || servidor.config.abandon_player != 0){
        fprintf(stderr, "Esta version admite N=2, M=1 y abandon_player=0.\n");
        return EXIT_FAILURE;
    }
    setvbuf(stdout, NULL, _IOLBF, 0);
    Mesa mesa = {.servidor = &servidor, .id = 1, .partida = 1,
                 .jugadores = {1, 2}, .abandono = -1};
    pthread_condattr_t at;
    int error = pthread_condattr_init(&at);
    if(error != 0) return EXIT_FAILURE;
    error = pthread_condattr_setclock(&at, CLOCK_MONOTONIC);
    if(error == 0) error = mesa_iniciar_bloqueos(&mesa, &at);
    pthread_condattr_destroy(&at);
    if(error != 0){
        fprintf(stderr, "No se pudo iniciar la mesa: %s.\n", strerror(error));
        return EXIT_FAILURE;
    }
    error = pthread_create(&mesa.juez, NULL, hilo_juez, &mesa);
    if(error == 0)
        pthread_join(mesa.juez, NULL);
    else
        fprintf(stderr, "No se pudo crear el juez: %s.\n", strerror(error));
    mesa_destruir_bloqueos(&mesa);
    return error == 0 && mesa.resultado != RES_ERROR ? EXIT_SUCCESS : EXIT_FAILURE;
}
