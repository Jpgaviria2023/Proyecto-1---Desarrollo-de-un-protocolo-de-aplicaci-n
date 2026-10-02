# Fase 2 — Comunicación básica (SMCD)

Implementa lo mínimo que pide la Fase 2: creación/configuración de sockets, registro, envío/recepción de mensajes, interpretación, respuestas y las reglas básicas del protocolo de la Entrega 1. Todavía **no** tiene concurrencia con hilos (eso es Fase 3) — el servidor atiende TCP y UDP en un solo bucle con `select()`, lo cual ya te permite demostrar varios nodos y clientes sin bloquearse.

## Archivos

- `server.c` — servidor central (obligatorio en C, sockets Berkeley).
- `Makefile` — compila con `make`.
- `nodo_cliente.py` — simula un nodo (sensor): se registra, manda telemetría por UDP y puede mandar un evento crítico.
- `admin_cliente.py` — simula un cliente de administración: se autentica y consulta un nodo.
- `logs/` — ahí quedan `servidor.log` (archivo de logs) y lo que imprime el servidor en consola.

## 1. Compilar el servidor

```bash
make
```

Esto genera el binario `servidor`.

## 2. Arrancarlo

```bash
./servidor <puerto_tcp> <archivoDeLogs>
```

Ejemplo:

```bash
./servidor 5000 logs/servidor.log
```

El puerto UDP de telemetría se calcula solo como `puerto_tcp + 1` (en el ejemplo, 5001) — no hace falta pasarlo aparte, ya con eso cumples que el servidor reciba `puerto` y `archivoDeLogs` como pide el enunciado.

Déjalo corriendo en una terminal (o mándalo a segundo plano con `&` si quieres seguir usando la misma terminal).

## 3. Simular un nodo

En otra terminal:

```bash
python3 nodo_cliente.py localhost 5000 nodo-temp-01 --evento --intervalo 2 --ciclos 5
```

- `localhost` es el **nombre** que se resuelve (no es una IP codificada — usa `socket.getaddrinfo`, y si la resolución falla reintenta solo en vez de terminar el programa).
- `--evento` hace que a la mitad del ciclo mande también un `EVENT` crítico.
- `--intervalo` y `--ciclos` controlan cada cuánto y cuántas veces manda `STATUS`.

Puedes correr varios nodos a la vez (en distintas terminales) con distinto `id_nodo` para demostrar que el servidor atiende a más de uno.

## 4. Simular un cliente de administración

```bash
python3 admin_cliente.py localhost 5000 nodo-temp-01
```

Se autentica con el usuario/clave de prueba (`admin` / `admin123`, hardcodeados en el servidor solo para esta fase — antes de la sustentación final hay que sacar la autenticación a un servicio aparte, como pide el enunciado) y consulta el estado + histórico del nodo indicado.

## 5. Revisar los logs

```bash
cat logs/servidor.log
```

Ahí queda cada mensaje que entra y sale, con IP y puerto de origen, tal como pide el enunciado.

## Qué falta para Fase 3

- Cambiar de `select()` a un hilo por conexión (o justificar por qué `select()` ya resuelve la concurrencia, que también es una estrategia válida).
- Sacar la autenticación del servidor principal (usuario/clave fijos) a un servicio/BD aparte.
- Formalizar el manejo de errores adicionales que pide la Fase 3 (duplicados, mensajes fuera de orden, etc., si aplican a tu diseño).
