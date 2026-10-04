#!/usr/bin/env python3
"""
nodo_cliente.py -- Simula un NODO del sistema (sensor).
Se registra por TCP, luego manda telemetria periodica por UDP,
y puede mandar un EVENT critico por TCP.

Uso:
    python3 nodo_cliente.py <host> <puerto_tcp> <id_nodo> [--evento] [--intervalo N] [--ciclos N]

Ejemplo:
    python3 nodo_cliente.py localhost 5000 nodo-temp-01 --evento --intervalo 2 --ciclos 5
"""
import socket
import sys
import time
import random
import argparse


def resolver_servidor(host, puerto):
    """Requisito: no usar IPs codificadas, resolver por nombre y no
    terminar el programa si la resolucion falla (se reintenta)."""
    while True:
        try:
            info = socket.getaddrinfo(host, puerto, proto=socket.IPPROTO_TCP)
            return info[0][4][0]  # ip resuelta
        except socket.gaierror as e:
            print(f"[nodo] no se pudo resolver '{host}': {e}. Reintentando en 3s...")
            time.sleep(3)


def leer_linea(sock):
    """Lee del socket TCP hasta encontrar '\\n' (una respuesta)."""
    datos = b""
    while not datos.endswith(b"\n"):
        chunk = sock.recv(1024)
        if not chunk:
            raise ConnectionError("el servidor cerro la conexion")
        datos += chunk
    return datos.decode().strip()


def parsear_payload(payload):
    resultado = {}
    for par in payload.split(";"):
        if "=" in par:
            k, v = par.split("=", 1)
            resultado[k] = v
    return resultado


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("host")
    ap.add_argument("puerto_tcp", type=int)
    ap.add_argument("id_nodo")
    ap.add_argument("--evento", action="store_true", help="manda un EVENT critico despues del primer STATUS")
    ap.add_argument("--intervalo", type=float, default=2.0, help="segundos entre reportes STATUS")
    ap.add_argument("--ciclos", type=int, default=5, help="cuantos STATUS mandar antes de terminar")
    args = ap.parse_args()

    ip_servidor = resolver_servidor(args.host, args.puerto_tcp)
    print(f"[nodo:{args.id_nodo}] servidor resuelto en {ip_servidor}")

    # --- 1) registro por TCP ---
    tcp = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    tcp.connect((ip_servidor, args.puerto_tcp))

    msg = f"REG|1|{args.id_nodo}|tipo=sensor_temp\n"
    tcp.sendall(msg.encode())
    print(f"[nodo:{args.id_nodo}] -> {msg.strip()}")

    resp = leer_linea(tcp)
    print(f"[nodo:{args.id_nodo}] <- {resp}")
    tipo, id_msg, origen, payload = resp.split("|", 3)
    if tipo != "REG_ACK":
        print(f"[nodo:{args.id_nodo}] registro rechazado: {payload}")
        return
    datos = parsear_payload(payload)
    session_id = datos["session"]
    puerto_udp = int(datos["puerto_udp"])
    print(f"[nodo:{args.id_nodo}] registrado. session={session_id} puerto_udp={puerto_udp}")

    # --- 2) telemetria periodica por UDP ---
    udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    seq = 0
    for i in range(args.ciclos):
        seq += 1
        cpu = random.randint(5, 95)
        temp = round(random.uniform(20, 40), 1)
        bateria = random.randint(10, 100)
        status = f"STATUS|{seq}|{session_id}|cpu={cpu};temp={temp};bateria={bateria};estado=ok"
        udp.sendto(status.encode(), (ip_servidor, puerto_udp))
        print(f"[nodo:{args.id_nodo}] -> (UDP) {status}")

        # dispara un evento critico a medio camino, si se pidio
        if args.evento and i == args.ciclos // 2:
            ev = "EVENT|2|" + args.id_nodo + "|tipo=umbral;severidad=alta;valor=95\n"
            tcp.sendall(ev.encode())
            print(f"[nodo:{args.id_nodo}] -> {ev.strip()}")
            resp_ev = leer_linea(tcp)
            print(f"[nodo:{args.id_nodo}] <- {resp_ev}")

        time.sleep(args.intervalo)

    tcp.close()
    udp.close()
    print(f"[nodo:{args.id_nodo}] listo, termina la simulacion.")


if __name__ == "__main__":
    main()
