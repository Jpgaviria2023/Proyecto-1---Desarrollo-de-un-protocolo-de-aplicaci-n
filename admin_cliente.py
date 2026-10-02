#!/usr/bin/env python3
"""
admin_cliente.py -- Simula un CLIENTE DE ADMINISTRACION.
Se autentica (LOGIN) y consulta el estado de un nodo (QUERY).

Uso:
    python3 admin_cliente.py <host> <puerto_tcp> <id_nodo_a_consultar>
"""
import socket
import sys
import time
import argparse


def resolver_servidor(host, puerto):
    while True:
        try:
            info = socket.getaddrinfo(host, puerto, proto=socket.IPPROTO_TCP)
            return info[0][4][0]
        except socket.gaierror as e:
            print(f"[admin] no se pudo resolver '{host}': {e}. Reintentando en 3s...")
            time.sleep(3)


def leer_linea(sock):
    datos = b""
    while not datos.endswith(b"\n"):
        chunk = sock.recv(2048)
        if not chunk:
            raise ConnectionError("el servidor cerro la conexion")
        datos += chunk
    return datos.decode().strip()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("host")
    ap.add_argument("puerto_tcp", type=int)
    ap.add_argument("nodo_id")
    ap.add_argument("--usuario", default="admin")
    ap.add_argument("--clave", default="admin123")
    args = ap.parse_args()

    ip_servidor = resolver_servidor(args.host, args.puerto_tcp)
    tcp = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    tcp.connect((ip_servidor, args.puerto_tcp))

    login = f"LOGIN|1|{args.usuario}|clave={args.clave}\n"
    tcp.sendall(login.encode())
    print(f"[admin] -> {login.strip()}")
    resp = leer_linea(tcp)
    print(f"[admin] <- {resp}")
    if not resp.startswith("LOGIN_ACK"):
        print("[admin] autenticacion rechazada")
        return

    query = f"QUERY|2|cliente-01|nodo={args.nodo_id};n=5\n"
    tcp.sendall(query.encode())
    print(f"[admin] -> {query.strip()}")
    resp2 = leer_linea(tcp)
    print(f"[admin] <- {resp2}")

    tcp.close()


if __name__ == "__main__":
    main()
