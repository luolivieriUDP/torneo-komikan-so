# Torneo de Komikan

**Lukas Diaz y Luciano Olivieri**  
Sistemas Operativos — Tarea 2  
Universidad Diego Portales

Simulación en C de un torneo de Komikan con hilos POSIX. Los jugadores se emparejan por orden de llegada y comparten un número limitado de mesas. Cada partida enfrenta un puma contra doce perros, sin turnos fijos.

## Compilar y ejecutar

Necesario Linux con GCC y Make para poder ejecutarlo como lo hicimos nosotros: (a continuacion todo sobre como ejecutarlo y explicaciones del còdigo màs a detalle)

```sh
make
./torneo 8 2 config.txt
```

El programa recibe:

```sh
./torneo N M [archivo_configuracion]
```

- `N`: cantidad de jugadores.
- `M`: máximo de mesas que pueden estar ocupadas al mismo tiempo.
- `archivo_configuracion`: archivo opcional. Si se omite, se usan los valores predeterminados.

`N` y `M` deben ser enteros positivos. Por ejemplo, ocho jugadores forman cuatro partidas; con dos mesas, pueden jugar hasta dos parejas simultáneamente.

También se puede ejecutar sin archivo de configuración:

```sh
./torneo 8 2
```

Para compilar directamente con GCC:

```sh
gcc -Wall -Wextra -std=c17 -pthread src/main.c src/tablero.c src/config.c -o torneo
```

Para eliminar el ejecutable:

```sh
make clean
```

## Configuración

El archivo usa el formato `clave=valor`, con una opción por línea. Se permiten espacios, líneas vacías y comentarios desde `#`. Las opciones omitidas conservan su valor predeterminado.

El `config.txt` incluido contiene:

```ini
min_delay_ms=50
max_delay_ms=300
seed=1
puma=25
dogs=13 14 15 16 17 18 19 20 21 22 23 24
log_every=1
abandon_player=0
abandon_after_ms=1000
```

| Clave | Descripción |
|---|---|
| `min_delay_ms` | Retardo mínimo de las fichas, en milisegundos. |
| `max_delay_ms` | Retardo máximo. Debe cumplirse `50 <= min_delay_ms <= max_delay_ms <= 300`. |
| `seed` | Semilla entre `0` y `4294967295`. Cada ficha mantiene su propio estado pseudoaleatorio. |
| `puma` | Casilla inicial del puma, entre `0` y `27`. |
| `dogs` | Doce casillas distintas entre `0` y `27`, separadas por espacios y sin repetir la del puma. |
| `log_every` | Mostrar el tablero cada esta cantidad de movimientos aceptados. Debe ser positivo. Las capturas siempre se muestran. |
| `abandon_player` | Identificador del jugador que abandonará una vez sentado. `0` desactiva la simulación; para activarla, debe estar entre `1` y `N`. |
| `abandon_after_ms` | Tiempo desde la asignación de mesa hasta el abandono, en milisegundos. Admite `0`. |

El programa rechaza claves desconocidas, valores fuera de rango y posiciones repetidas. Si se indica un archivo que no puede abrirse, informa el error y termina.

Usar la misma semilla no garantiza una partida idéntica: el scheduler puede cambiar el orden en que los hilos envían sus solicitudes.

## Tablero y reglas

El tablero tiene 28 casillas:

- `0` a `24`: cuadrícula de cinco por cinco, numerada por filas desde la esquina superior izquierda.
- `25`: vértice del trono.
- `26` y `27`: posiciones laterales izquierda y derecha del trono.

En la salida, `P` representa al puma, `d` a un perro y `.` a una casilla vacía. El signo `+` del trono es un cruce de líneas, no una casilla.

Las fichas se mueven hacia posiciones vecinas libres según las conexiones del tablero. El puma también puede capturar saltando sobre un perro por dos conexiones en línea recta, siempre que el destino esté vacío.

El puma gana al capturar seis perros. Los perros ganan cuando el puma no tiene pasos ni saltos legales. No se establece un máximo de movimientos por partida.

## Organización del torneo

Cada jugador tiene un hilo y entra a una fila de espera. El emparejador toma a los dos primeros cuando hay una mesa disponible: el primero controla al puma y el segundo a los perros.

Si todas las mesas están ocupadas, los jugadores esperan hasta que se libere una. Si `N` es impar, el jugador que queda sin pareja es informado y termina su espera.

Cada mesa tiene un juez y hasta trece hilos de fichas. Las fichas esperan un intervalo aleatorio, proponen movimientos y esperan su respuesta. El juez atiende los pedidos en orden FIFO y vuelve a comprobar cada jugada antes de aplicarla.

Una solicitud puede ser rechazada si el tablero cambió mientras esperaba. Por ejemplo, otra ficha puede haber ocupado su destino. Estos rechazos forman parte del funcionamiento concurrente.

## Sincronización y cierre

El mutex del servidor protege la fila, la asignación de mesas y los contadores generales. Cada mesa tiene otro mutex para proteger su tablero, resultado y cola de solicitudes.

Solo el juez modifica el tablero durante la partida. La validación, el movimiento, la captura y la revisión del ganador se realizan dentro de la misma sección crítica.

Las variables de condición permiten esperar una mesa, una solicitud o una respuesta sin busy waiting. Cuando se necesitan ambos mutexes, se toma primero el del servidor y después el de la mesa.

Al presionar `Ctrl+C`, el servidor detiene las nuevas asignaciones, interrumpe las partidas y despierta a los hilos en espera. Antes de salir, espera su término y libera los recursos inicializados.

El abandono configurado simula la salida del hilo de un cliente. El juez concede la victoria al rival y la mesa queda disponible al terminar la limpieza. No simula un fallo fatal de memoria: un segmentation fault afecta al proceso completo.

## Pruebas de ejecución

### Varias partidas y límite de mesas

```sh
./torneo 8 2 config.txt
```

Sin interrupciones, deben terminar cuatro partidas y ocho jugadores. El máximo de mesas activas no debe superar dos.

### Jugador sin pareja

```sh
./torneo 9 2 config.txt
```

Deben formarse cuatro parejas e informarse que un jugador quedó sin pareja.

### Cierre con Ctrl+C

```sh
./torneo 30 3 config.txt
```

Presionar `Ctrl+C` mientras haya partidas activas. El programa debe interrumpirlas, mostrar el resumen y terminar.

### Abandono y reutilización de la mesa

Crear un archivo aparte llamado `config_abandono.txt`:

```ini
abandon_player=1
abandon_after_ms=0
```

Ejecutar:

```sh
./torneo 4 1 config_abandono.txt
```

Debe registrarse una victoria por abandono y completarse otra partida usando la misma mesa.

### Puma acorralado desde el inicio

Crear un archivo llamado `config_acorralado.txt`:

```ini
puma=0
dogs=1 2 5 10 26 25 6 7 8 9 11 12
```

Ejecutar:

```sh
./torneo 2 1 config_acorralado.txt
```

Los perros deben ganar por acorralamiento, sin movimientos aceptados.

### Argumentos inválidos

```sh
./torneo 0 2
./torneo 8 0
./torneo ocho 2
```

El programa debe informar el uso correcto y terminar con error.

## Resumen de resultados

Al finalizar se muestran las partidas, los jugadores finalizados, el máximo de mesas activas, los ganadores y los movimientos aceptados y rechazados.

También se informa el rendimiento en partidas y movimientos por segundo. La latencia se mide desde que una solicitud entra a la cola hasta que el juez la resuelve; no incluye el retardo previo de la ficha ni la espera para adquirir el mutex antes de encolar.

Los tiempos y los resultados pueden variar entre ejecuciones por la planificación de los hilos, la carga del equipo y la impresión del tablero.

## Archivos principales

| Archivo | Contenido |
|---|---|
| `Makefile` | Compilación y eliminación del ejecutable. |
| `config.txt` | Configuración de ejemplo con los valores predeterminados. |
| `src/main.c` | Servidor, clientes, emparejador, fichas, juez, señales y limpieza. |
| `src/tablero.c` y `src/tablero.h` | Conexiones, movimientos, capturas, condiciones de victoria e impresión del tablero. |
| `src/config.c` y `src/config.h` | Lectura de la configuración y validación de sus valores. |
| `docs/informe.pdf` | Diseño de sincronización, riesgos de concurrencia y evaluación. |

Para seguir el recorrido de una jugada, revisar `hilo_ficha`, `ficha_pedir` y `juez_atender` en `src/main.c`.

El análisis del diseño y las mediciones de una ejecución están en el [informe](docs/informe.pdf).