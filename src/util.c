#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <time.h>
#include "util.h"

long long ahora_ns(void){
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts); //monotonic: no salta si alguien le cambia la hora al pc
    return (long long)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

void dormir_ms(int ms){
    if(ms <= 0)
        return;
    struct timespec t;
    t.tv_sec = ms / 1000;
    t.tv_nsec = (long)(ms % 1000) * 1000000L;
    while(nanosleep(&t, &t) == -1 && errno == EINTR){} //si una señal lo corta, duerme lo q le faltaba
}

void plazo_realtime(struct timespec* ts, int ms){
    //sem_timedwait y pthread_cond_timedwait piden un instante ABSOLUTO en CLOCK_REALTIME, no un "cuanto rato"
    clock_gettime(CLOCK_REALTIME, ts);
    ts->tv_sec += ms / 1000;
    ts->tv_nsec += (long)(ms % 1000) * 1000000L;
    if(ts->tv_nsec >= 1000000000L){
        ts->tv_sec++;
        ts->tv_nsec -= 1000000000L;
    }
}

unsigned long long azar_siguiente(unsigned long long* estado){
    //splitmix64: chiquito, rapido y da la misma secuencia en cualquier maquina (rand_r no, depende de la libc)
    unsigned long long z = (*estado += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

unsigned long long azar_semilla(unsigned long long base, unsigned long long a, unsigned long long b){
    //mezcla la semilla global con ids estables (partida, ficha...). asi la semilla de cada ficha
    //no depende del orden en q el sistema operativo despierta a los hilos
    unsigned long long s = base ^ (a * 0xD1B54A32D192ED03ULL);
    unsigned long long x = azar_siguiente(&s);
    s = x ^ (b * 0xABC98388FB8FAC03ULL);
    return azar_siguiente(&s);
}

int azar_rango(unsigned long long* estado, int min, int max){
    if(max <= min)
        return min;
    unsigned long long largo = (unsigned long long)(max - min) + 1ULL;
    return min + (int)(azar_siguiente(estado) % largo);
}
