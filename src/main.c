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
    Servidor* servidor;
    pthread_t hilo;
    int id, mesa, rol;           // rol: JUEGA_PUMA o JUEGAN_PERROS
    bool creado, terminado;      // El servidor espera solo los hilos creados.
    struct timespec sentado_en;
} Cliente;

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
    bool creado, ocupada;        // protegidos por el mutex del servidor
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

typedef struct {                 // todo protegido por el mutex del servidor
    unsigned int resultados[RES_ERROR + 1], abandonos;
    unsigned long movimientos, rechazos;
    long long latencia_total, latencia_maxima;
} Estadisticas;

struct Servidor {
    Config config;
    int total, capacidad, cantidad_mesas;
    Cliente* clientes;
    Mesa* mesas;
    int* fila;                   // clientes esperando pareja, en orden de llegada
    int inicio_fila, fin_fila, llegados, completados, activas, pico;
    unsigned int partidas;
    bool apagar, fallo, fin;
    pthread_mutex_t mutex;
    pthread_cond_t cambio;       //"algo cambio": llegada, mesa asignada, partida terminada o apagado
    sigset_t senales;
    bool bloqueos_listos;        // permite limpiar una inicializacion parcial
    int mesas_listas;
    Estadisticas est;
    long long inicio_ns;
};

// Utilidades.

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

// Mesas: bloqueos y despertar.

static bool partida_activa(const Mesa* mesa){
    return mesa->resultado == RES_JUGANDO && !mesa->apagar && mesa->abandono < 0;
}

// con el mutex de la mesa tomado: despierta al juez y a todas las fichas (fin, abandono o Ctrl+C)
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

// Apagado y señales.

// se llama con el mutex del servidor tomado. orden: servidor -> mesa
static void servidor_apagar(Servidor* servidor){
    servidor->apagar = true;
    for(int i = 0; i < servidor->cantidad_mesas; i++){
        Mesa* mesa = &servidor->mesas[i];
        pthread_mutex_lock(&mesa->mutex);
        mesa->apagar = true;
        despertar_mesa(mesa);
        pthread_mutex_unlock(&mesa->mutex);
    }
    pthread_cond_broadcast(&servidor->cambio);
}

static void* hilo_senales(void* arg){
    Servidor* servidor = arg;
    // SIGUSR1 es el aviso interno de fin. Durante el torneo se ignora.
    int sig = 0;
    int error;
    for(;;){
        error = sigwait(&servidor->senales, &sig);
        pthread_mutex_lock(&servidor->mutex);
        if(error || sig != SIGUSR1) break;
        bool fin = servidor->fin;
        pthread_mutex_unlock(&servidor->mutex);
        if(fin) return NULL;
    }
    if(error != 0){
        servidor->fallo = true;
        printf("[Servidor] error esperando senales: %s.\n", strerror(error));
    } else
        printf("[Servidor] senal %d: no se aceptan mas jugadores y se cierran las partidas.\n", sig);
    servidor_apagar(servidor);
    pthread_mutex_unlock(&servidor->mutex);
    return NULL;
}

// Clientes.

// Al cancelar una espera, POSIX recupera el mutex antes de ejecutar esta limpieza.
// Un cliente en fila deja su lugar; uno sentado comunica el abandono a su mesa.
static void cliente_limpieza(void* arg){
    Cliente* cliente = arg;
    Servidor* servidor = cliente->servidor;
    if(!cliente->terminado && !servidor->apagar && cliente->mesa < 0){
        for(int i = servidor->inicio_fila; i < servidor->fin_fila; i++){
            if(servidor->fila[i] != cliente->id - 1) continue;
            servidor->fin_fila--;
            memmove(&servidor->fila[i], &servidor->fila[i+1], (size_t)(servidor->fin_fila-i) * sizeof(*servidor->fila));
            servidor->completados++;
            cliente->terminado = true;
            pthread_cond_broadcast(&servidor->cambio);
            break;
        }
    }
    if(!cliente->terminado && !servidor->apagar && cliente->mesa >= 0){
        Mesa* mesa = &servidor->mesas[cliente->mesa];
        pthread_mutex_lock(&mesa->mutex);
        if(mesa->resultado == RES_JUGANDO && mesa->abandono < 0){
            mesa->abandono = cliente->rol;
            registrar_mesa(mesa, "jugador %d desconectado.\n", cliente->id);
            despertar_mesa(mesa);
        }
        pthread_mutex_unlock(&mesa->mutex);
    }
    pthread_mutex_unlock(&servidor->mutex);
}

static void cliente_esperar_mesa(Cliente* cliente){
    Servidor* servidor = cliente->servidor;
    while(cliente->mesa < 0 && !cliente->terminado && !servidor->apagar)
        pthread_cond_wait(&servidor->cambio, &servidor->mutex);
}

// El cliente configurado para abandonar espera hasta su plazo.
// Al vencer, pthread_exit ejecuta la limpieza y avisa al juez.
static void cliente_esperar_resultado(Cliente* cliente){
    Servidor* servidor = cliente->servidor;
    while(!cliente->terminado && !servidor->apagar){
        if(cliente->id != servidor->config.abandon_player){
            pthread_cond_wait(&servidor->cambio, &servidor->mutex);
            continue;
        }
        struct timespec plazo = sumar_ms(cliente->sentado_en, servidor->config.abandon_after_ms);
        struct timespec ahora = ahora_ts();
        if(ahora.tv_sec > plazo.tv_sec || (ahora.tv_sec == plazo.tv_sec && ahora.tv_nsec >= plazo.tv_nsec))
            pthread_exit(NULL);
        pthread_cond_timedwait(&servidor->cambio, &servidor->mutex, &plazo);
    }
}

static void* hilo_cliente(void* arg){
    Cliente* cliente = arg;
    Servidor* servidor = cliente->servidor;
    pthread_mutex_lock(&servidor->mutex);
    pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
    pthread_cleanup_push(cliente_limpieza, cliente);
    if(!servidor->apagar){
        servidor->fila[servidor->fin_fila++] = cliente->id - 1;
        servidor->llegados++;
        printf("[Servidor] Llega jugador %d.\n", cliente->id);
        pthread_cond_broadcast(&servidor->cambio);
        pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
        cliente_esperar_mesa(cliente);
        cliente_esperar_resultado(cliente);
    }
    pthread_cleanup_pop(1);
    return NULL;
}

// Fichas.

static bool ficha_viva(const Mesa* mesa, int numero){
    for(int i = 0; i < CASILLAS; i++)
        if(mesa->tablero.casillas[i] == numero)
            return true;
    return false;
}

// La espera libera el mutex y puede interrumpirse si termina la partida.
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

// El Puma prefiere capturas. Entre las opciones restantes elige al azar.
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

// La ficha encola su propuesta y espera la respuesta en su propia condicion.
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

// Juez.

// Se comprueba primero el apagado del servidor y luego el abandono.
static Resultado resultado_externo(const Mesa* mesa){
    if(mesa->apagar)
        return RES_INTERRUMPIDA;
    if(mesa->abandono >= 0)
        return mesa->abandono == JUEGA_PUMA ? RES_PERROS : RES_PUMA;
    return RES_JUGANDO;
}

// El juez copia el tablero antes de soltar el mutex. Las fichas pueden seguir
// proponiendo jugadas mientras se imprime esa copia.
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

// El contador permite esperar solo las fichas que llegaron a crearse.
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

// El juez retira el pedido mas antiguo y lo valida con el tablero actual.
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

static void sumar_estadisticas(Servidor* servidor, const Mesa* mesa){
    Estadisticas* estadisticas = &servidor->est;
    estadisticas->resultados[mesa->resultado]++;
    if(mesa->abandono >= 0 && (mesa->resultado == RES_PUMA || mesa->resultado == RES_PERROS))
        estadisticas->abandonos++;
    estadisticas->movimientos += mesa->movimientos;
    estadisticas->rechazos += mesa->rechazos;
    estadisticas->latencia_total += mesa->latencia_total;
    if(mesa->latencia_maxima > estadisticas->latencia_maxima)
        estadisticas->latencia_maxima = mesa->latencia_maxima;
}

// Las fichas necesitan el mutex de la mesa para salir. El juez lo suelta antes del join.
static void juez_cerrar(Mesa* mesa, pthread_t* hilos, int creadas){
    Servidor* servidor = mesa->servidor;
    for(int i = 0; i < creadas; i++)
        pthread_join(hilos[i], NULL);
    pthread_mutex_lock(&servidor->mutex);
    for(int i = 0; i < 2; i++)
        servidor->clientes[mesa->jugadores[i] - 1].terminado = true;
    servidor->completados += 2;
    servidor->activas--;
    mesa->ocupada = false;
    sumar_estadisticas(servidor, mesa);
    if(mesa->resultado == RES_ERROR){
        servidor->fallo = true;
        servidor_apagar(servidor);
    }
    pthread_cond_broadcast(&servidor->cambio);
    pthread_mutex_unlock(&servidor->mutex);
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
    juez_cerrar(mesa, hilos, creadas);
    return NULL;
}

// Emparejador (matchmaker).

static int mesa_libre(const Servidor* servidor){
    for(int i = 0; i < servidor->cantidad_mesas; i++)
        if(!servidor->mesas[i].ocupada)
            return i;
    return -1;
}

// Con el mutex del servidor tomado, asigna la mesa a los dos primeros clientes.
static void armar_partida(Servidor* servidor, int indice_mesa){
    Mesa* mesa = &servidor->mesas[indice_mesa];
    if(mesa->creado){ // Se espera al juez anterior antes de reutilizar la mesa.
        pthread_join(mesa->juez, NULL); // el juez ya libero el mutex del servidor
        mesa->creado = false;
    }
    pthread_mutex_lock(&mesa->mutex);
    mesa->resultado = RES_JUGANDO;
    mesa->apagar = false;
    mesa->abandono = -1;
    mesa->movimientos = mesa->rechazos = 0;
    mesa->latencia_total = mesa->latencia_maxima = 0;
    mesa->inicio_cola = mesa->pedidos_en_cola = 0;
    memset(mesa->pendiente, 0, sizeof(mesa->pendiente));
    for(int rol = 0; rol < 2; rol++){
        int indice_cliente = servidor->fila[servidor->inicio_fila++];
        Cliente* cliente = &servidor->clientes[indice_cliente];
        cliente->mesa = indice_mesa;
        cliente->rol = rol;
        cliente->sentado_en = ahora_ts();
        mesa->jugadores[rol] = cliente->id;
    }
    mesa->partida = servidor->partidas + 1;
    pthread_mutex_unlock(&mesa->mutex);
    int error = pthread_create(&mesa->juez, NULL, hilo_juez, mesa);
    if(error != 0){
        printf("[Servidor] No se pudo crear el juez: %s.\n", strerror(error));
        servidor->fallo = true;
        servidor_apagar(servidor);
        return;
    }
    mesa->creado = mesa->ocupada = true;
    servidor->partidas++;
    servidor->activas++;
    if(servidor->activas > servidor->pico)
        servidor->pico = servidor->activas;
    printf("[Servidor] Mesa %d, partida %u: Puma=%d, Perros=%d. Activas=%d/%d.\n", mesa->id,
              mesa->partida, mesa->jugadores[JUEGA_PUMA], mesa->jugadores[JUEGAN_PERROS], servidor->activas, servidor->capacidad);
    pthread_cond_broadcast(&servidor->cambio);
}

static void descartar_sobrante(Servidor* servidor){
    Cliente* cliente = &servidor->clientes[servidor->fila[servidor->inicio_fila++]];
    cliente->terminado = true;
    servidor->completados++;
    printf("[Servidor] Jugador %d sin pareja: N es impar.\n", cliente->id);
    pthread_cond_broadcast(&servidor->cambio);
}

static void* emparejar(void* arg){
    Servidor* servidor = arg;
    pthread_mutex_lock(&servidor->mutex);
    while(!servidor->apagar && servidor->completados < servidor->total){
        int libre = mesa_libre(servidor);
        int en_espera = servidor->fin_fila - servidor->inicio_fila;
        if(en_espera >= 2 && libre >= 0)
            armar_partida(servidor, libre);
        else if(servidor->llegados == servidor->total && en_espera == 1)
            descartar_sobrante(servidor);
        else
            pthread_cond_wait(&servidor->cambio, &servidor->mutex); // espera pasiva: sin pareja o sin mesa libre
    }
    pthread_mutex_unlock(&servidor->mutex);
    return NULL;
}

// Servidor.

static void imprimir_resumen(const Servidor* servidor){
    const Estadisticas* estadisticas = &servidor->est;
    double seg = (double)(ahora_ns() - servidor->inicio_ns) / 1e9;
    unsigned long pedidos = estadisticas->movimientos + estadisticas->rechazos;
    double media_ms = pedidos ? (double)estadisticas->latencia_total / (double)pedidos / 1e6 : 0;
    printf("[Servidor] Resumen: %u partidas, %d jugadores finalizados, maximo %d mesas activas (limite %d).\n",
              servidor->partidas, servidor->completados, servidor->pico, servidor->capacidad);
    printf("[Servidor] Resultados: gana puma %u, ganan perros %u, victorias por abandono %u, interrumpidas %u, error %u.\n",
              estadisticas->resultados[RES_PUMA], estadisticas->resultados[RES_PERROS], estadisticas->abandonos, estadisticas->resultados[RES_INTERRUMPIDA], estadisticas->resultados[RES_ERROR]);
    printf("[Servidor] Movimientos: aceptados %lu, rechazados %lu | atencion de pedidos: media %.3f ms, maxima %.3f ms.\n",
              estadisticas->movimientos, estadisticas->rechazos, media_ms, (double)estadisticas->latencia_maxima / 1e6);
    printf("[Servidor] Rendimiento: %.2f partidas/s, %.0f movimientos/s en %.2f s.\n",
              seg > 0 ? servidor->partidas / seg : 0, seg > 0 ? (double)estadisticas->movimientos / seg : 0, seg);
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

static int servidor_reservar(Servidor* servidor){
    servidor->cantidad_mesas = servidor->capacidad < servidor->total / 2 ? servidor->capacidad : servidor->total / 2;
    servidor->clientes = calloc((size_t)servidor->total, sizeof(*servidor->clientes));
    servidor->fila = calloc((size_t)servidor->total, sizeof(*servidor->fila));
    servidor->mesas = calloc((size_t)(servidor->cantidad_mesas ? servidor->cantidad_mesas : 1), sizeof(*servidor->mesas));
    return servidor->clientes && servidor->fila && servidor->mesas ? 0 : ENOMEM;
}

// Todos los hilos heredan las señales bloqueadas. Solo hilo_senales las recibe.
// El servidor registra los recursos inicializados para poder limpiar tras un error.
static int servidor_iniciar(Servidor* servidor){
    pthread_condattr_t at;
    sigemptyset(&servidor->senales);
    sigaddset(&servidor->senales, SIGINT);
    sigaddset(&servidor->senales, SIGTERM);
    sigaddset(&servidor->senales, SIGUSR1);
    int error = pthread_sigmask(SIG_BLOCK, &servidor->senales, NULL);
    if(error == 0)
        error = pthread_condattr_init(&at);
    if(error != 0)
        return error;
    error = pthread_condattr_setclock(&at, CLOCK_MONOTONIC); // mismo reloj que usan los plazos
    if(error == 0)
        error = pthread_mutex_init(&servidor->mutex, NULL);
    if(error == 0){
        error = pthread_cond_init(&servidor->cambio, &at);
        if(error != 0)
            pthread_mutex_destroy(&servidor->mutex);
        else
            servidor->bloqueos_listos = true;
    }
    for(int i = 0; error == 0 && i < servidor->cantidad_mesas; i++){
        servidor->mesas[i].servidor = servidor;
        servidor->mesas[i].id = i + 1;
        error = mesa_iniciar_bloqueos(&servidor->mesas[i], &at);
        if(error == 0)
            servidor->mesas_listas++;
    }
    pthread_condattr_destroy(&at);
    return error;
}

static void servidor_destruir(Servidor* servidor){
    for(int i = 0; i < servidor->mesas_listas; i++)
        mesa_destruir_bloqueos(&servidor->mesas[i]);
    if(servidor->bloqueos_listos){
        pthread_cond_destroy(&servidor->cambio);
        pthread_mutex_destroy(&servidor->mutex);
    }
    free(servidor->mesas);
    free(servidor->fila);
    free(servidor->clientes);
}

static void crear_clientes(Servidor* servidor){
    for(int i = 0; i < servidor->total; i++){
        Cliente* cliente = &servidor->clientes[i];
        *cliente = (Cliente){.servidor = servidor, .id = i + 1, .mesa = -1};
        pthread_mutex_lock(&servidor->mutex);
        bool apagar = servidor->apagar; // si llego Ctrl+C no entra nadie mas
        pthread_mutex_unlock(&servidor->mutex);
        if(apagar)
            break;
        int error = pthread_create(&cliente->hilo, NULL, hilo_cliente, cliente);
        if(error != 0){
            printf("[Servidor] No se pudo crear jugador %d: %s.\n", i + 1, strerror(error));
            pthread_mutex_lock(&servidor->mutex);
            servidor->fallo = true;
            servidor_apagar(servidor);
            pthread_mutex_unlock(&servidor->mutex);
            break;
        }
        cliente->creado = true;
    }
}

static int servidor_correr(Servidor* servidor){
    pthread_t hilo_sig;
    int error = pthread_create(&hilo_sig, NULL, hilo_senales, servidor);
    if(error != 0)
        return error;
    servidor->inicio_ns = ahora_ns();
    pthread_t parejas;
    error = pthread_create(&parejas, NULL, emparejar, servidor);
    if(error == 0){
        crear_clientes(servidor);
        pthread_join(parejas, NULL);
    }
    for(int i = 0; i < servidor->total; i++)
        if(servidor->clientes[i].creado)
            pthread_join(servidor->clientes[i].hilo, NULL);
    for(int i = 0; i < servidor->cantidad_mesas; i++)
        if(servidor->mesas[i].creado)
            pthread_join(servidor->mesas[i].juez, NULL);
    pthread_mutex_lock(&servidor->mutex);
    servidor->fin = true;
    pthread_mutex_unlock(&servidor->mutex);
    pthread_kill(hilo_sig, SIGUSR1); // Despierta al hilo de señales si sigue esperando el fin normal.
    pthread_join(hilo_sig, NULL);
    imprimir_resumen(servidor);
    return servidor->fallo ? EIO : error;
}

int main(int argc, char** argv){
    Servidor servidor = {0};
    if(leer_argumentos(&servidor, argc, argv) != 0)
        return EXIT_FAILURE;
    setvbuf(stdout, NULL, _IOLBF, 0);
    preparar_grafo();
    int error = servidor_reservar(&servidor);
    if(error == 0)
        error = servidor_iniciar(&servidor);
    if(error == 0)
        error = servidor_correr(&servidor);
    servidor_destruir(&servidor);
    if(error != 0)
        fprintf(stderr, "El torneo termino con error: %s.\n", strerror(error));
    return error == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
