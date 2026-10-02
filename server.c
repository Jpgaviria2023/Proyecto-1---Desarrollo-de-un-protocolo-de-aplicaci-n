#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/select.h>

#define MAX_NODOS 32
#define MAX_CONEXIONES_TCP 16
#define MAX_HIST 5
#define BUF_TAM 4096
#define TIMEOUT_OFFLINE_SEG 90

static const char *USUARIO_DEMO = "admin";
static const char *CLAVE_DEMO   = "admin123";

typedef struct {
    int en_uso;
    char id[64];
    char session_id[16];
    char estado[16];
    time_t last_seen;
    char historial[MAX_HIST][160];
    int hist_len;
    int hist_pos;
} Nodo;

typedef struct {
    int fd;
    int activo;
    char buffer[BUF_TAM];
    int buf_len;
    int es_nodo;
    int nodo_idx;
    int autenticado;
    struct sockaddr_in addr;
} ConexionTCP;

static Nodo nodos[MAX_NODOS];
static ConexionTCP conexiones[MAX_CONEXIONES_TCP];
static FILE *log_fp = NULL;
static int puerto_udp_global = 0;

static void log_evento(const char *origen, const char *linea) {
    time_t t = time(NULL);
    struct tm tm_info;
    localtime_r(&t, &tm_info);
    char ts[32];
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &tm_info);

    printf("[%s] %s | %s\n", ts, origen, linea);
    fflush(stdout);
    if (log_fp) {
        fprintf(log_fp, "[%s] %s | %s\n", ts, origen, linea);
        fflush(log_fp);
    }
}

static int split_mensaje(char *linea, char *tipo, char *id_msg, char *origen, char *payload) {
    char *p1 = strchr(linea, '|');
    if (!p1) return -1;
    *p1 = '\0';
    strncpy(tipo, linea, 31);

    char *p2 = strchr(p1 + 1, '|');
    if (!p2) return -1;
    *p2 = '\0';
    strncpy(id_msg, p1 + 1, 31);

    char *p3 = strchr(p2 + 1, '|');
    if (!p3) {
        strncpy(origen, p2 + 1, 63);
        payload[0] = '\0';
        return 0;
    }
    *p3 = '\0';
    strncpy(origen, p2 + 1, 63);
    strncpy(payload, p3 + 1, 900);
    return 0;
}

static int payload_get(const char *payload, const char *clave, char *out, size_t out_tam) {
    char copia[1024];
    strncpy(copia, payload, sizeof(copia) - 1);
    copia[sizeof(copia) - 1] = '\0';

    char *tok = strtok(copia, ";");
    size_t len_clave = strlen(clave);
    while (tok) {
        if (strncmp(tok, clave, len_clave) == 0 && tok[len_clave] == '=') {
            strncpy(out, tok + len_clave + 1, out_tam - 1);
            out[out_tam - 1] = '\0';
            return 0;
        }
        tok = strtok(NULL, ";");
    }
    return -1;
}

static Nodo *buscar_nodo_por_id(const char *id) {
    for (int i = 0; i < MAX_NODOS; i++)
        if (nodos[i].en_uso && strcmp(nodos[i].id, id) == 0) return &nodos[i];
    return NULL;
}

static Nodo *buscar_nodo_por_sesion(const char *sid) {
    for (int i = 0; i < MAX_NODOS; i++)
        if (nodos[i].en_uso && strcmp(nodos[i].session_id, sid) == 0) return &nodos[i];
    return NULL;
}

static Nodo *registrar_nodo(const char *id) {
    Nodo *n = buscar_nodo_por_id(id);
    if (n) return n;
    for (int i = 0; i < MAX_NODOS; i++) {
        if (!nodos[i].en_uso) {
            memset(&nodos[i], 0, sizeof(Nodo));
            nodos[i].en_uso = 1;
            strncpy(nodos[i].id, id, sizeof(nodos[i].id) - 1);
            snprintf(nodos[i].session_id, sizeof(nodos[i].session_id), "%04x%02x", (unsigned)time(NULL) & 0xFFFF, i);
            strcpy(nodos[i].estado, "REGISTRADO");
            nodos[i].last_seen = time(NULL);
            return &nodos[i];
        }
    }
    return NULL;
}

static void enviar_tcp(int fd, const char *tipo, const char *id_msg, const char *payload) {
    char linea[BUF_TAM];
    snprintf(linea, sizeof(linea), "%s|%s|servidor|%s\n", tipo, id_msg, payload);
    ssize_t n = write(fd, linea, strlen(linea));
    (void)n;
    linea[strcspn(linea, "\n")] = '\0';
    log_evento("SALIDA(TCP)", linea);
}

static void enviar_error(int fd, const char *id_msg, const char *motivo) {
    char payload[128];
    snprintf(payload, sizeof(payload), "motivo=%s", motivo);
    enviar_tcp(fd, "ERROR", id_msg, payload);
}

static void procesar_linea_tcp(ConexionTCP *c, char *linea) {
    char ip[32];
    strncpy(ip, inet_ntoa(c->addr.sin_addr), sizeof(ip) - 1);
    ip[sizeof(ip)-1] = '\0';
    int puerto = ntohs(c->addr.sin_port);
    char origen_log[64];
    snprintf(origen_log, sizeof(origen_log), "ENTRADA(TCP) %s:%d", ip, puerto);
    log_evento(origen_log, linea);

    char linea_copia[BUF_TAM];
    strncpy(linea_copia, linea, sizeof(linea_copia) - 1);
    linea_copia[sizeof(linea_copia) - 1] = '\0';

    char tipo[32] = "", id_msg[32] = "", origen[64] = "", payload[1024] = "";
    if (split_mensaje(linea_copia, tipo, id_msg, origen, payload) != 0) {
        enviar_error(c->fd, "0", "formato_invalido");
        return;
    }

    if (strcmp(tipo, "REG") == 0) {
        Nodo *n = registrar_nodo(origen);
        if (!n) { enviar_error(c->fd, id_msg, "tabla_de_nodos_llena"); return; }
        c->es_nodo = 1;
        c->nodo_idx = (int)(n - nodos);
        char resp[128];
        snprintf(resp, sizeof(resp), "session=%s;puerto_udp=%d", n->session_id, puerto_udp_global);
        enviar_tcp(c->fd, "REG_ACK", id_msg, resp);

    } else if (strcmp(tipo, "EVENT") == 0) {
        if (!c->es_nodo) { enviar_error(c->fd, id_msg, "no_registrado"); return; }
        Nodo *n = &nodos[c->nodo_idx];
        char linea_evento[1200];
        snprintf(linea_evento, sizeof(linea_evento), "EVENTO nodo=%s %s", n->id, payload);
        log_evento("EVENTO_CRITICO", linea_evento);
        enviar_tcp(c->fd, "EVENT_ACK", id_msg, "status=ok");

    } else if (strcmp(tipo, "LOGIN") == 0) {
        char clave[64];
        if (payload_get(payload, "clave", clave, sizeof(clave)) == 0 &&
            strcmp(origen, USUARIO_DEMO) == 0 && strcmp(clave, CLAVE_DEMO) == 0) {
            c->autenticado = 1;
            enviar_tcp(c->fd, "LOGIN_ACK", id_msg, "permisos=lectura");
        } else {
            enviar_error(c->fd, id_msg, "credenciales_invalidas");
        }

    } else if (strcmp(tipo, "QUERY") == 0) {
        if (!c->autenticado) { enviar_error(c->fd, id_msg, "no_autenticado"); return; }
        char nodo_id[64], n_str[8];
        if (payload_get(payload, "nodo", nodo_id, sizeof(nodo_id)) != 0) {
            enviar_error(c->fd, id_msg, "falta_parametro_nodo");
            return;
        }
        int n_pedidos = 5;
        if (payload_get(payload, "n", n_str, sizeof(n_str)) == 0) n_pedidos = atoi(n_str);

        Nodo *n = buscar_nodo_por_id(nodo_id);
        if (!n) { enviar_error(c->fd, id_msg, "nodo_no_existe"); return; }

        if (strcmp(n->estado, "OFFLINE") != 0 &&
            time(NULL) - n->last_seen > TIMEOUT_OFFLINE_SEG) {
            strcpy(n->estado, "OFFLINE");
        }

        char resp[BUF_TAM];
        int off = snprintf(resp, sizeof(resp), "estado=%s;historico=[", n->estado);
        int mostrar = n->hist_len < n_pedidos ? n->hist_len : n_pedidos;
        for (int k = 0; k < mostrar; k++) {
            int idx = (n->hist_pos - mostrar + k + MAX_HIST) % MAX_HIST;
            off += snprintf(resp + off, sizeof(resp) - off, "%s%s",
                             k == 0 ? "" : ",", n->historial[idx]);
        }
        snprintf(resp + off, sizeof(resp) - off, "]");
        enviar_tcp(c->fd, "QUERY_RESP", id_msg, resp);

    } else {
        enviar_error(c->fd, id_msg, "tipo_de_mensaje_desconocido");
    }
}

static void procesar_datagrama_udp(char *linea, struct sockaddr_in *addr) {
    char ip[32];
    strncpy(ip, inet_ntoa(addr->sin_addr), sizeof(ip) - 1);
    ip[sizeof(ip)-1] = '\0';
    int puerto = ntohs(addr->sin_port);
    char origen_log[64];
    snprintf(origen_log, sizeof(origen_log), "ENTRADA(UDP) %s:%d", ip, puerto);
    log_evento(origen_log, linea);

    char tipo[32] = "", id_msg[32] = "", origen[64] = "", payload[1024] = "";
    if (split_mensaje(linea, tipo, id_msg, origen, payload) != 0) {
        log_evento("UDP_DESCARTADO", "formato invalido");
        return;
    }
    if (strcmp(tipo, "STATUS") != 0) {
        log_evento("UDP_DESCARTADO", "tipo no esperado por este canal");
        return;
    }

    Nodo *n = buscar_nodo_por_sesion(origen);
    if (!n) {
        log_evento("UDP_DESCARTADO", "sesion desconocida (nodo no registrado)");
        return;
    }
    n->last_seen = time(NULL);
    strcpy(n->estado, "ONLINE");
    strncpy(n->historial[n->hist_pos], payload, sizeof(n->historial[0]) - 1);
    n->hist_pos = (n->hist_pos + 1) % MAX_HIST;
    if (n->hist_len < MAX_HIST) n->hist_len++;
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Uso: %s <puerto_tcp> <archivoDeLogs>\n", argv[0]);
        return 1;
    }
    int puerto_tcp = atoi(argv[1]);
    puerto_udp_global = puerto_tcp + 1;
    const char *archivo_logs = argv[2];

    log_fp = fopen(archivo_logs, "a");
    if (!log_fp) {
        perror("No se pudo abrir el archivo de logs, continuo solo con consola");
    }

    memset(nodos, 0, sizeof(nodos));
    memset(conexiones, 0, sizeof(conexiones));
    for (int i = 0; i < MAX_CONEXIONES_TCP; i++) conexiones[i].fd = -1;

    int fd_tcp = socket(AF_INET, SOCK_STREAM, 0);
    if (fd_tcp < 0) { perror("socket TCP"); return 1; }
    int opt = 1;
    setsockopt(fd_tcp, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in dir_tcp;
    memset(&dir_tcp, 0, sizeof(dir_tcp));
    dir_tcp.sin_family = AF_INET;
    dir_tcp.sin_addr.s_addr = INADDR_ANY;
    dir_tcp.sin_port = htons(puerto_tcp);

    if (bind(fd_tcp, (struct sockaddr *)&dir_tcp, sizeof(dir_tcp)) < 0) {
        perror("bind TCP"); return 1;
    }
    if (listen(fd_tcp, 8) < 0) { perror("listen"); return 1; }

    int fd_udp = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd_udp < 0) { perror("socket UDP"); return 1; }
    struct sockaddr_in dir_udp;
    memset(&dir_udp, 0, sizeof(dir_udp));
    dir_udp.sin_family = AF_INET;
    dir_udp.sin_addr.s_addr = INADDR_ANY;
    dir_udp.sin_port = htons(puerto_udp_global);
    if (bind(fd_udp, (struct sockaddr *)&dir_udp, sizeof(dir_udp)) < 0) {
        perror("bind UDP"); return 1;
    }

    printf("Servidor SMCD escuchando: TCP=%d  UDP=%d  logs=%s\n",
           puerto_tcp, puerto_udp_global, archivo_logs);
    fflush(stdout);

    while (1) {
        fd_set lectura;
        FD_ZERO(&lectura);
        FD_SET(fd_tcp, &lectura);
        FD_SET(fd_udp, &lectura);
        int max_fd = fd_tcp > fd_udp ? fd_tcp : fd_udp;

        for (int i = 0; i < MAX_CONEXIONES_TCP; i++) {
            if (conexiones[i].activo) {
                FD_SET(conexiones[i].fd, &lectura);
                if (conexiones[i].fd > max_fd) max_fd = conexiones[i].fd;
            }
        }

        struct timeval tv = {5, 0};
        int listos = select(max_fd + 1, &lectura, NULL, NULL, &tv);
        if (listos < 0) {
            if (errno == EINTR) continue;
            perror("select");
            break;
        }

        if (listos > 0 && FD_ISSET(fd_tcp, &lectura)) {
            struct sockaddr_in cli_addr;
            socklen_t cli_len = sizeof(cli_addr);
            int fd_cli = accept(fd_tcp, (struct sockaddr *)&cli_addr, &cli_len);
            if (fd_cli >= 0) {
                int slot = -1;
                for (int i = 0; i < MAX_CONEXIONES_TCP; i++)
                    if (!conexiones[i].activo) { slot = i; break; }
                if (slot == -1) {
                    fprintf(stderr, "Maximo de conexiones alcanzado, se rechaza cliente\n");
                    close(fd_cli);
                } else {
                    memset(&conexiones[slot], 0, sizeof(ConexionTCP));
                    conexiones[slot].fd = fd_cli;
                    conexiones[slot].activo = 1;
                    conexiones[slot].addr = cli_addr;
                    log_evento("CONEXION_TCP", inet_ntoa(cli_addr.sin_addr));
                }
            }
        }

        if (listos > 0 && FD_ISSET(fd_udp, &lectura)) {
            char buf[BUF_TAM];
            struct sockaddr_in origen;
            socklen_t origen_len = sizeof(origen);
            ssize_t n = recvfrom(fd_udp, buf, sizeof(buf) - 1, 0,
                                  (struct sockaddr *)&origen, &origen_len);
            if (n > 0) {
                buf[n] = '\0';
                buf[strcspn(buf, "\r\n")] = '\0';
                procesar_datagrama_udp(buf, &origen);
            }
        }

        for (int i = 0; i < MAX_CONEXIONES_TCP; i++) {
            if (!conexiones[i].activo || !FD_ISSET(conexiones[i].fd, &lectura)) continue;
            ConexionTCP *c = &conexiones[i];
            char tmp[BUF_TAM];
            ssize_t n = read(c->fd, tmp, sizeof(tmp) - 1);
            if (n <= 0) {
                log_evento("DESCONEXION_TCP", inet_ntoa(c->addr.sin_addr));
                close(c->fd);
                c->activo = 0;
                continue;
            }
            tmp[n] = '\0';
            if (c->buf_len + n >= BUF_TAM) c->buf_len = 0;
            memcpy(c->buffer + c->buf_len, tmp, n);
            c->buf_len += n;
            c->buffer[c->buf_len] = '\0';

            char *inicio = c->buffer;
            char *salto;
            while ((salto = strchr(inicio, '\n')) != NULL) {
                *salto = '\0';
                if (strlen(inicio) > 0) procesar_linea_tcp(c, inicio);
                inicio = salto + 1;
            }
            int restante = c->buf_len - (int)(inicio - c->buffer);
            memmove(c->buffer, inicio, restante);
            c->buf_len = restante;
        }

        for (int i = 0; i < MAX_NODOS; i++) {
            if (nodos[i].en_uso && strcmp(nodos[i].estado, "ONLINE") == 0 &&
                time(NULL) - nodos[i].last_seen > TIMEOUT_OFFLINE_SEG) {
                strcpy(nodos[i].estado, "OFFLINE");
                log_evento("TIMEOUT", nodos[i].id);
            }
        }
    }

    close(fd_tcp);
    close(fd_udp);
    if (log_fp) fclose(log_fp);
    return 0;
}
