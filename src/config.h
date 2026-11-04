#ifndef KOMIKAN_CONFIG_H
#define KOMIKAN_CONFIG_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    int min_delay_ms, max_delay_ms, puma, dogs[12], log_every;
    int abandon_player, abandon_after_ms;
    uint32_t seed;
} Config;

void configuracion_inicial(Config* config);
int cargar_configuracion(Config* config, const char* ruta, char* detalle, size_t tam);

#endif
