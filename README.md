# Torneo Komikan - SO Tarea 2
Contenido
src/util.h / src/util.c: utilidades chicas que usará todo el programa.
Relojes: ahora_ns (monotónico) y plazo_realtime (para los *_timedwait).
dormir_ms: duerme sin perder tiempo si lo interrumpe una señal.
Generador aleatorio propio (splitmix64), con estado por ficha para que la semilla de config.txt reproduzca las mismas secuencias.
src/tablero.c y src/tablero.h: grafo de 28 casillas y posiciones iniciales.

## Configuración

El archivo opcional usa una línea por clave, en cualquier orden. Se admiten espacios, líneas vacías y comentarios desde `#`. Las claves omitidas conservan el valor por defecto. El ejemplo incluido contiene los valores predeterminados.

| Clave | Uso |
|---|---|
| min_delay_ms, max_delay_ms | Retardo mínimo y máximo, entre 50 y 300 ms inclusive. El mínimo debe ser menor o igual al máximo. |
| puma | Casilla inicial del Puma, entre 0 y 27. |
| dogs | Doce casillas distintas separadas por espacios, sin repetir la del Puma. |
| seed | Entero entre 0 y 4294967295. Inicializa las secuencias por ficha. El planificador puede cambiar el orden efectivo de los pedidos. |
| log_every | Mostrar el tablero cada tantos movimientos aceptados. Debe ser positivo. Las capturas siempre se muestran. |
| abandon_player | ID del cliente que simula un abandono una vez sentado. 0 desactiva la simulación. |
| abandon_after_ms | Milisegundos desde la asignación hasta el abandono, incluyendo 0. |

