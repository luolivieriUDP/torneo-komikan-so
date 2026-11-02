# Torneo Komikan - SO Tarea 2
Contenido
src/util.h / src/util.c: utilidades chicas que usará todo el programa.
Relojes: ahora_ns (monotónico) y plazo_realtime (para los *_timedwait).
dormir_ms: duerme sin perder tiempo si lo interrumpe una señal.
Generador aleatorio propio (splitmix64), con estado por ficha para que la semilla de config.txt reproduzca las mismas secuencias.
src/tablero.h: interfaz del tablero (solo declaraciones, la implementación llega en el siguiente commit).
Tablero de 28 nodos: cuadrícula 5x5 (A1..E5) y trono (T, TI, TD).
Estructuras Tablero, Movimiento y Salto, y los códigos de rechazo de movimientos.