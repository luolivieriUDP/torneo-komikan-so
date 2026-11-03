# Torneo Komikan - SO Tarea 2
Contenido
src/util.h / src/util.c: utilidades chicas que usará todo el programa.
Relojes: ahora_ns (monotónico) y plazo_realtime (para los *_timedwait).
dormir_ms: duerme sin perder tiempo si lo interrumpe una señal.
Generador aleatorio propio (splitmix64), con estado por ficha para que la semilla de config.txt reproduzca las mismas secuencias.
src/tablero.c y src/tablero.h: grafo de 28 casillas y posiciones iniciales.
