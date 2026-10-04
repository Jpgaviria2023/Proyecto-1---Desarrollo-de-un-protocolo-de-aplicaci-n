# Fase 3 (en progreso) — Concurrencia con hilos (SMCD)

Primero fue la Fase 2 (comunicación básica con `select()`, un solo hilo). Ahora el servidor usa **hilos de verdad**: un hilo por cada conexión TCP (nodo o cliente), un hilo dedicado al socket UDP de telemetría, y un hilo que revisa cada 5 segundos si algún nodo dejó de reportar. Eso es lo que pide la Fase 3 como mínimo de concurrencia.

## Archivos

- `server.c` — servidor central en C, con hilos (`pthread`).
- `Makefile` — compila con `make` (ya enlaza `-pthread`).
- `nodo_cliente.py` — simula un nodo: se registra, manda telemetría por UDP y puede mandar un evento crítico.
- `admin_cliente.py` — simula un cliente de administración: se autentica y consulta un nodo.
- `logs/` — ahí quedan `servidor.log` y la salida de consola. Cada línea del log ahora incluye el **id del hilo** que la generó, para que puedas mostrar en la sustentación que de verdad son hilos distintos atendiendo en paralelo.

## 1. Compilar

```bash
make
```

## 2. Arrancar el servidor

```bash
./servidor <puerto_tcp> <archivoDeLogs>
```

Ejemplo:

```bash
./servidor 5000 logs/servidor.log
```

El puerto UDP se calcula como `puerto_tcp + 1` (en el ejemplo, 5001).

## 3. Probar la concurrencia de verdad

Abre varias terminales (o corre los comandos en paralelo con `&`) y lanza varios nodos **al mismo tiempo**:

```bash
python3 nodo_cliente.py localhost 5000 nodo-01 --evento --intervalo 1 --ciclos 6 &
python3 nodo_cliente.py localhost 5000 nodo-02 --intervalo 1 --ciclos 6 &
python3 nodo_cliente.py localhost 5000 nodo-03 --evento --intervalo 1 --ciclos 6 &
python3 admin_cliente.py localhost 5000 nodo-02
```

Luego revisa `logs/servidor.log` y vas a ver, por ejemplo:

```
[hilo 139849604511424] CONEXION_TCP | 127.0.0.1   <- nodo-01, su propio hilo
[hilo 139849612904128] CONEXION_TCP | 127.0.0.1   <- nodo-02, otro hilo distinto
[hilo 139849638082240] ENTRADA(UDP) ...            <- el hilo dedicado a UDP, atendiendo a los tres nodos
[hilo 139849596118720] ENTRADA(TCP) ... LOGIN ...   <- el cliente admin, en otro hilo aparte
```

Eso es justo la evidencia que te van a pedir sustentar: cada cliente corre en su propio hilo del sistema operativo, no uno detrás del otro.

## 4. Sobre la tabla compartida de nodos (`nodos[]`)

Varios hilos (el de UDP, cada hilo de cliente TCP, el de timeouts) pueden tocar la misma tabla de nodos al mismo tiempo. Por eso **todo** acceso a `nodos[]` está protegido con `pthread_mutex_t mutex_nodos` — se toma el mutex, se lee o escribe lo que se necesite, y se suelta. Sin esto, dos hilos podrían pisarse datos entre sí (por ejemplo, el hilo UDP actualizando el histórico mientras otro hilo lo está leyendo para responder un `QUERY`) y producir resultados corruptos o inconsistentes. Esto es exactamente lo que hay que poder explicar en la sustentación si preguntan "¿cómo evitas condiciones de carrera?".

También hay un segundo mutex (`mutex_log`) solo para que el log no quede con líneas de distintos hilos mezcladas entre sí.

## 5. Qué falta todavía para la entrega completa

- **Autenticación desacoplada**: el usuario/clave (`admin`/`admin123`) sigue hardcodeado en el servidor — hay que sacarlo a un servicio aparte (requerimiento 6 del enunciado).
- **Reintentos de EVENT por timeout**: el diseño dice que si el nodo no recibe `EVENT_ACK` a tiempo, debe reintentar (máximo 3 veces); eso vive en `nodo_cliente.py` y todavía no está implementado ahí.
- **Informe actualizado**: el documento de la Entrega 1 se escribió antes de programar — hay que revisarlo para que la especificación coincida exactamente con lo que terminó implementado (puertos, formato de mensajes, etc.).
- **Repositorio con commits incrementales**: subir esto como un commit de "Fase 2" y luego cada cambio de Fase 3 en commits separados, no todo junto al final.
