#ifndef UTIL_H
#define UTIL_H

#include <time.h>

//cosas chicas q usan todos los modulos: relojes, dormir y el generador aleatorio

long long ahora_ns(void);                         //reloj MONOTONIC en nanosegundos (pa medir duraciones)
void dormir_ms(int ms);
void plazo_realtime(struct timespec* ts, int ms); //ahora + ms en CLOCK_REALTIME, pa los *_timedwait

//generador propio (splitmix64): cada ficha tiene su propio estado, nada de rand() global compartido
unsigned long long azar_siguiente(unsigned long long* estado);
unsigned long long azar_semilla(unsigned long long base, unsigned long long a, unsigned long long b);
int azar_rango(unsigned long long* estado, int min, int max);

#endif
