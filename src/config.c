#define _POSIX_C_SOURCE 200809L
#include "config.h"
#include "tablero.h"
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

// Configuracion de la partida y lectura del archivo opcional.
void configuracion_inicial(Config* config){
    *config = (Config){.min_delay_ms=50, .max_delay_ms=300, .puma=25, .log_every=1,
                  .abandon_after_ms=1000, .seed=1};
    for(int i = 0; i < 12; i++) config->dogs[i] = i + 13;
}

static char* recortar(char* s){
    s += strspn(s, " \t\r\n");
    size_t n = strlen(s);
    while(n && strchr(" \t\r\n", s[n-1])) s[--n] = '\0';
    return s;
}

static bool leer_numero(char** texto, unsigned long maximo, unsigned long* v){
    char* p = *texto + strspn(*texto, " \t");
    if(*p < '0' || *p > '9') return false;
    errno = 0;
    *v = strtoul(p, texto, 10);
    return !errno && *v <= maximo;
}

static bool leer_opcion(Config* config, char* clave, char* texto){
    unsigned long v;
    if(strcmp(clave, "dogs") == 0){
        for(int i = 0; i < 12; i++){
            if(!leer_numero(&texto, CASILLAS - 1, &v)) return false;
            config->dogs[i] = (int)v;
        }
    } else if(strcmp(clave, "seed") == 0){
        if(!leer_numero(&texto, UINT32_MAX, &v)) return false;
        config->seed = (uint32_t)v;
    } else {
        struct { const char* nombre; int* campo; } opciones[] = {
            {"min_delay_ms", &config->min_delay_ms}, {"max_delay_ms", &config->max_delay_ms},
            {"puma", &config->puma}, {"log_every", &config->log_every},
            {"abandon_player", &config->abandon_player}, {"abandon_after_ms", &config->abandon_after_ms}
        };
        size_t i = 0, n = sizeof(opciones) / sizeof(opciones[0]);
        while(i < n && strcmp(clave, opciones[i].nombre)) i++;
        if(i == n || !leer_numero(&texto, INT_MAX, &v)) return false;
        *opciones[i].campo = (int)v;
    }
    return *recortar(texto) == '\0';
}

int cargar_configuracion(Config* config, const char* ruta, char* detalle, size_t tam){
    FILE* f = fopen(ruta, "r");
    if(!f){
        snprintf(detalle, tam, "%s", strerror(errno));
        return -1;
    }
    char* linea = NULL;
    size_t capacidad = 0;
    unsigned long numero = 0;
    bool ok = true;
    ssize_t largo;
    while((largo = getline(&linea, &capacidad, f)) >= 0){
        numero++;
        if(memchr(linea, '\0', (size_t)largo)){ ok = false; break; }
        linea[strcspn(linea, "#")] = '\0';
        char* clave = recortar(linea);
        if(!*clave) continue;
        char* valor = strchr(clave, '=');
        if(!valor){ ok = false; break; }
        *valor = '\0';
        valor++;
        if(!leer_opcion(config, recortar(clave), valor)){ ok = false; break; }
    }
    if(!ok) snprintf(detalle, tam, "clave o valor invalido en linea %lu", numero);
    else if(!feof(f)){ ok = false; snprintf(detalle, tam, "error de lectura"); }
    free(linea);
    fclose(f);
    if(!ok) return -1;
    if(config->min_delay_ms < 50 || config->max_delay_ms > 300 ||
       config->min_delay_ms > config->max_delay_ms || config->log_every == 0){
        snprintf(detalle, tam, "se requiere 50 <= min <= max <= 300 y log_every >= 1");
        return -1;
    }
    bool usado[CASILLAS] = {false};
    for(int i = 0; i < FICHAS; i++){
        int p = i == 0 ? config->puma : config->dogs[i-1];
        if(p >= CASILLAS || usado[p]){
            snprintf(detalle, tam, "casilla repetida o fuera del tablero: %d", p);
            return -1;
        }
        usado[p] = true;
    }
    return 0;
}
