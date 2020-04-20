# La Ultima Cursada de Sistemas Operativos

![](Logo.png)

## Integrantes

* *Nicolas Molina*
* *Dante Bado*
* *Matias Iglesias*
* *Agustin De Cesare*
* *Franco Montenegro*

### Libreria

La misma incluye:

- Carga de todas las configuraciones en los 4 procesos.
- Todas las conexiones hacia el broker y el levante de cada servidor.
- La escucha de los mensajes tanto por parte de las colas como del socket interno (para los mensajes del gameboy).
- Cada proceso está suscrito a las colas que tiene que estar (incluido el reintento de conexión cuando el broker no está disponible) y le llegan los mensajes que llegan a la misma.
- Como los mensajes de red son limitados (los 6 descritos en el tp más los handshakes y mensajitos internos), hicimos unas funciones mágicas bien debugueadas que te mandan toda la estructura serializada sin tener que hacer nada más. Cada proceso ya recibe solito los mensajes y, ya sea que vienen del broker o del gameboy, los manda a una función de procesar, asi ya lo único que tocamos es ahí.

Al margen, podemos ver en detalle qué cosas tiene incluidas la librería en el siguiente link:
[Libreria](https://docs.google.com/document/d/1MZCtApwEUY8TVrS6YAI4e2pkuRfoEMNKL59Bz40vGZE/edit#)
