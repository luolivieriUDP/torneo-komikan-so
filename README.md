# Torneo concurrente de Komikan

# Lukas Diaz y Luciano Olivieri

## Compilar y ejecutar 

Como compilarlo ahora con el make instalado y gcc desde la carpeta:

```sh
make
./torneo 8 2
./torneo 8 2 config.txt
```

compilar directamente:

```sh
gcc -Wall -Wextra -std=c17 -pthread src/main.c src/tablero.c src/config.c -o torneo
```

N cuenta jugadores y M limita las mesas simultáneas. Ambos deben ser enteros positivos. Cada pareja usa una mesa. El primero en la fila controla el Puma y el segundo los Perros. Si N es impar, el servidor informa qué cliente quedó sin pareja. Ctrl+C solicita el cierre ordenado. Para borrar el ejecutable, usar `make clean`.

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

Las casillas 0–24 se numeran por filas, desde la esquina superior izquierda. 25 identifica el vértice del trono, 26 el lateral izquierdo y 27 el derecho. En la consola, P representa al Puma, d a un perro y el punto a una casilla libre. El signo + en el trono indica un cruce de líneas sin casilla. El Puma puede saltar sobre un perro siguiendo dos aristas en línea recta hasta una casilla libre. Gana en la sexta captura. Los perros ganan cuando el Puma pierde tanto los pasos simples como los saltos legales.

## Cómo seguir el código

| Archivo | Qué leer |
|---|---|
| src/main.c | Servidor, clientes, emparejamiento, fichas, juez, señales y cierre. Para seguir una jugada, leer hilo_ficha, ficha_pedir y juez_atender. |
| src/tablero.c y src/tablero.h | Topología del tablero, jugadas válidas, capturas, victoria y dibujo. |
| src/config.c y src/config.h | Valores predeterminados, lectura del archivo opcional y validaciones. |

El mutex del servidor protege la fila y la asignación de mesas. Cada mesa tiene su propio mutex. Solo el juez cambia el tablero, después de revalidar el pedido. Las condiciones despiertan a quienes esperan una asignación, un movimiento o el fin de la partida.
