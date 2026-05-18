#include "common.h"
#include <ctype.h>

/* ───────── constantes ───────── */
#define IDX_FILENAME  "bloques.idx"
#define CSV_FILENAME  "personas.csv"

/* ───────── variables globales ───────── */
static BloqueInfo             *bloques        = NULL;
static int                     num_bloques    = 0;
static volatile sig_atomic_t   active_clients = 0;  /* accedido en SIGCHLD */

#define MAX_RESULTADOS  3
#define MAX_LINEAS_SCAN 2000000   /* máximo 2M líneas escaneadas */
int  parse_csv_line(char *line, char *fields[], int max_fields);
void build_index(void);
int  buscar_bloque(uint32_t id);
int  cumple_filtros(char *fields[], int n,
                    const char *nombre, const char *ciudad);
int  search_by_id(uint32_t person_id,
                  const char *nombre, const char *ciudad,
                  char *result, FILE *f);
void search_sequential(const char *nombre, const char *ciudad,
                       int client_fd, FILE *f);
void write_log(const char *ip, const char *id,
               const char *nombre, const char *ciudad);
void handle_client(int client_fd, const char *ip);
void cleanup(int sig);
void sigchld_handler(int sig);

/* ─────────────────────────────────────────────────────────────
 * cleanup - libera recursos y termina el proceso servidor.
 * Se llama ante SIGINT o SIGTERM.
 * ───────────────────────────────────────────────────────────── */
void cleanup(int sig) {
    if (bloques) free(bloques);
    exit(0);
}

/* ─────────────────────────────────────────────────────────────
 * sigchld_handler - recolecta procesos hijo que terminaron y
 * decrementa el contador de clientes activos.
 * ───────────────────────────────────────────────────────────── */
void sigchld_handler(int sig) {
    while (waitpid(-1, NULL, WNOHANG) > 0)
        active_clients--;
}

/* ─────────────────────────────────────────────────────────────
 * build_index - construye el índice de bloques sobre el CSV y
 * lo carga en memoria. Si el archivo de índice ya existe, lo
 * carga directamente sin releer el CSV (importante con 1 GB+).
 * ───────────────────────────────────────────────────────────── */
void build_index(void) {
    /* ── intentar cargar índice existente ── */
    FILE *idx = fopen(IDX_FILENAME, "rb");
    if (idx) {
        fseek(idx, 0, SEEK_END);
        long sz = ftell(idx);
        num_bloques = (int)(sz / sizeof(BloqueInfo));
        bloques = (BloqueInfo *)malloc(sz);
        if (!bloques) { perror("malloc bloques"); fclose(idx); exit(1); }
        fseek(idx, 0, SEEK_SET);
        fread(bloques, sizeof(BloqueInfo), num_bloques, idx);
        fclose(idx);
        return;
    }

    /* ── construir índice desde el CSV ── */
    FILE *csv = fopen(CSV_FILENAME, "r");
    if (!csv) { perror("fopen CSV"); exit(1); }

    /* buffer de 64KB: acelera la lectura sin superar el límite de 1MB */
    setvbuf(csv, NULL, _IOFBF, 65536);

    idx = fopen(IDX_FILENAME, "wb");
    if (!idx) { perror("fopen idx"); fclose(csv); exit(1); }

    char line[MAX_LINE_LEN];
    long line_count = 0;
    BloqueInfo bi;

    /* saltar cabecera */
    if (!fgets(line, sizeof(line), csv)) {
        perror("fgets cabecera"); fclose(csv); fclose(idx); exit(1);
    }

    fprintf(stderr, "Construyendo indice por primera vez...\n");

    while (1) {
        long offset = ftell(csv);
        if (!fgets(line, sizeof(line), csv)) break;

        if (line_count % BLOQUE_SIZE == 0) {
            char tmp[MAX_LINE_LEN];
            strncpy(tmp, line, MAX_LINE_LEN - 1);
            tmp[MAX_LINE_LEN - 1] = '\0';
            char *fields[15];
            int n = parse_csv_line(tmp, fields, 15);
            if (n >= 1) {
                bi.person_id = (uint32_t)atoi(fields[0]);
                bi.offset    = (uint32_t)offset;
                fwrite(&bi, sizeof(BloqueInfo), 1, idx);
            }
        }
        line_count++;

        if (line_count % 500000 == 0)
            fprintf(stderr, "  -> %ld lineas procesadas...\n", line_count);
    }

    fprintf(stderr, "Indice construido: %ld registros.\n", line_count);
    fclose(csv);
    fclose(idx);

    /* ── cargar el índice recién creado ── */
    idx = fopen(IDX_FILENAME, "rb");
    if (!idx) { perror("fopen idx (carga)"); exit(1); }
    fseek(idx, 0, SEEK_END);
    long sz = ftell(idx);
    num_bloques = (int)(sz / sizeof(BloqueInfo));
    bloques = (BloqueInfo *)malloc(sz);
    if (!bloques) { perror("malloc bloques"); fclose(idx); exit(1); }
    fseek(idx, 0, SEEK_SET);
    fread(bloques, sizeof(BloqueInfo), num_bloques, idx);
    fclose(idx);
}

/* ─────────────────────────────────────────────────────────────
 * buscar_bloque - búsqueda binaria en el índice.
 * Devuelve el índice del bloque donde puede estar el ID dado.
 * ───────────────────────────────────────────────────────────── */
int buscar_bloque(uint32_t id) {
    int izq = 0, der = num_bloques - 1;
    while (izq <= der) {
        int mid = (izq + der) / 2;
        if      (bloques[mid].person_id == id) return mid;
        else if (bloques[mid].person_id  < id) izq = mid + 1;
        else                                   der = mid - 1;
    }
    return (der < 0) ? 0 : der;
}

/* ─────────────────────────────────────────────────────────────
 * cumple_filtros - verifica si los campos de una línea coinciden
 * con los filtros de nombre y ciudad ingresados.
 * ───────────────────────────────────────────────────────────── */
int cumple_filtros(char *fields[], int n,
                   const char *nombre, const char *ciudad) {
    if (n < 10) return 0;

    if (nombre[0] != '\0') {
        if (strstr(fields[1], nombre) == NULL &&
            strstr(fields[2], nombre) == NULL)
            return 0;
    }

    if (ciudad[0] != '\0') {
        if (strstr(fields[9], ciudad) == NULL)
            return 0;
    }

    return 1;
}

/* ─────────────────────────────────────────────────────────────
 * search_by_id - usa el índice para localizar el bloque y luego
 * recorre hasta BLOQUE_SIZE líneas buscando el ID exacto.
 * Recibe f: FILE* propio del proceso hijo (no el global).
 * ───────────────────────────────────────────────────────────── */
int search_by_id(uint32_t person_id,
                 const char *nombre, const char *ciudad,
                 char *result, FILE *f) {
    int bloque_idx = buscar_bloque(person_id);

    fseek(f, (long)bloques[bloque_idx].offset, SEEK_SET);

    char line[MAX_LINE_LEN];
    int leidas = 0;

    while (leidas < BLOQUE_SIZE && fgets(line, sizeof(line), f)) {
        leidas++;
        char tmp[MAX_LINE_LEN];
        strncpy(tmp, line, MAX_LINE_LEN - 1);
        tmp[MAX_LINE_LEN - 1] = '\0';
        char *fields[15];
        int n = parse_csv_line(tmp, fields, 15);
        if (n >= 10) {
            uint32_t cur = (uint32_t)atoi(fields[0]);
            if (cur == person_id) {
                if (cumple_filtros(fields, n, nombre, ciudad)) {
                    strncpy(result, line, MAX_LINE_LEN - 1);
                    result[MAX_LINE_LEN - 1] = '\0';
                    return 1;
                }
            } else if (cur > person_id) {
                break;
            }
        }
    }
    return 0;
}

/* ─────────────────────────────────────────────────────────────
 * search_sequential - recorre todo el CSV línea a línea y envía
 * al cliente cada registro que cumpla los filtros.
 * Lee desde disco con buffer fijo (no carga el CSV completo).
 * ───────────────────────────────────────────────────────────── */
void search_sequential(const char *nombre, const char *ciudad,
                       int client_fd, FILE *f) {
    char line[MAX_LINE_LEN];
    fseek(f, 0, SEEK_SET);

    /* buffer grande para acelerar la lectura secuencial (512 KB) */
    setvbuf(f, NULL, _IOFBF, 524288);

    fgets(line, sizeof(line), f); /* saltar cabecera */

    int encontrados = 0;
    long escaneadas = 0;

    while (encontrados < MAX_RESULTADOS &&
           escaneadas  < MAX_LINEAS_SCAN &&
           fgets(line, sizeof(line), f)) {
        escaneadas++;
        char tmp[MAX_LINE_LEN];
        strncpy(tmp, line, MAX_LINE_LEN - 1);
        tmp[MAX_LINE_LEN - 1] = '\0';
        char *fields[15];
        int n = parse_csv_line(tmp, fields, 15);
        if (n >= 10 && cumple_filtros(fields, n, nombre, ciudad)) {
            send(client_fd, line, strlen(line), 0);
            encontrados++;
        }
    }

    if (encontrados == 0)
        send(client_fd, "NA - No se encontraron resultados\n", 34, 0);
}

/* ─────────────────────────────────────────────────────────────
 * write_log - escribe una entrada en el archivo de log con el
 * formato: [YYYYMMDDTHHMMSS] Cliente [IP] [búsqueda - id - nombre - ciudad]
 * ───────────────────────────────────────────────────────────── */
void write_log(const char *ip, const char *id,
               const char *nombre, const char *ciudad) {
    FILE *log = fopen(LOG_FILE, "a");
    if (!log) return;

    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    char fecha[20];
    strftime(fecha, sizeof(fecha), "%Y%m%dT%H%M%S", t);

    fprintf(log, "[%s] Cliente [%s] [búsqueda - %s - %s - %s]\n",
            fecha, ip, id, nombre, ciudad);
    fclose(log);
}

/* ─────────────────────────────────────────────────────────────
 * handle_client - corre en el proceso hijo. Gestiona la sesión
 * completa de un cliente: recibe comandos, ejecuta operaciones
 * sobre disco y envía respuestas.
 *
 * Protocolo recibido:
 *   SET_ID <valor>    - establece filtro por ID
 *   SET_NAME <valor>  - establece filtro por nombre
 *   SET_CITY <valor>  - establece filtro por ciudad
 *   SEARCH            - ejecuta la búsqueda con los filtros actuales
 *   EXIT              - cierra la conexión
 * ───────────────────────────────────────────────────────────── */
void handle_client(int client_fd, const char *ip) {
    /* Enviar señal de que el servidor aceptó la conexión */
    send(client_fd, "READY\n", 6, 0);

    /* Sesión en memoria dinámica */
    ClientSession *s = (ClientSession *)malloc(sizeof(ClientSession));
    if (!s) { close(client_fd); return; }

    s->id     = (char *)malloc(50);
    s->nombre = (char *)malloc(100);
    s->ciudad = (char *)malloc(100);
    if (!s->id || !s->nombre || !s->ciudad) {
        free(s->id); free(s->nombre); free(s->ciudad);
        free(s); close(client_fd); return;
    }
    s->id[0] = s->nombre[0] = s->ciudad[0] = '\0';

    /* Cada hijo abre su propio descriptor del CSV para evitar
     * conflictos de posición con otros procesos */
    FILE *local_csv = fopen(CSV_FILENAME, "r");
    if (!local_csv) {
        send(client_fd, "ERROR No se pudo abrir el dataset\n", 34, 0);
        free(s->id); free(s->nombre); free(s->ciudad);
        free(s); close(client_fd); return;
    }

    char buffer[MAX_LINE_LEN];
    char response[MAX_LINE_LEN];

    while (1) {
        int n = (int)recv(client_fd, buffer, sizeof(buffer) - 1, 0);
        if (n <= 0) break;
        buffer[n] = '\0';
        buffer[strcspn(buffer, "\r\n")] = '\0';  /* quitar salto de línea */

        if (strncmp(buffer, "SET_ID ", 7) == 0) {
            strncpy(s->id, buffer + 7, 49);
            s->id[49] = '\0';
            snprintf(response, sizeof(response),
                     "OK ID establecido: %s\n", s->id);
            send(client_fd, response, strlen(response), 0);

        } else if (strncmp(buffer, "SET_NAME ", 9) == 0) {
            strncpy(s->nombre, buffer + 9, 99);
            s->nombre[99] = '\0';
            snprintf(response, sizeof(response),
                     "OK Nombre establecido: %s\n", s->nombre);
            send(client_fd, response, strlen(response), 0);

        } else if (strncmp(buffer, "SET_CITY ", 9) == 0) {
            strncpy(s->ciudad, buffer + 9, 99);
            s->ciudad[99] = '\0';
            snprintf(response, sizeof(response),
                     "OK Ciudad establecida: %s\n", s->ciudad);
            send(client_fd, response, strlen(response), 0);

        } else if (strcmp(buffer, "SEARCH") == 0) {
            write_log(ip, s->id, s->nombre, s->ciudad);
            if (s->id[0] != '\0') {
                char result[MAX_LINE_LEN];
                if (search_by_id((uint32_t)atoi(s->id),
                                 s->nombre, s->ciudad,
                                 result, local_csv)) {
                    send(client_fd, result, strlen(result), 0);
                } else {
                    send(client_fd, "NA\n", 3, 0);
                }
            } else {
                search_sequential(s->nombre, s->ciudad,
                                  client_fd, local_csv);
            }
            /* limpiar filtros para la siguiente búsqueda */
            s->id[0] = s->nombre[0] = s->ciudad[0] = '\0';
            send(client_fd, "END\n", 4, 0);

        } else if (strcmp(buffer, "EXIT") == 0) {
            send(client_fd, "OK Hasta luego\n", 15, 0);
            break;
        }
    }

    fclose(local_csv);
    free(s->id);
    free(s->nombre);
    free(s->ciudad);
    free(s);
    close(client_fd);
}

/* ─────────────────────────────────────────────────────────────
 * main - inicializa el servidor: construye el índice, abre el
 * socket TCP, y entra en el loop de aceptación de conexiones.
 * Por cada cliente acepta hace fork(); el hijo llama a
 * handle_client() y el padre sigue aceptando conexiones.
 * ───────────────────────────────────────────────────────────── */
int main(void) {
    signal(SIGINT,  cleanup);
    signal(SIGTERM, cleanup);
    signal(SIGCHLD, sigchld_handler);

    /* Construir / cargar índice de bloques */
    build_index();

    /* Crear socket TCP */
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) { perror("socket"); exit(1); }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind"); exit(1);
    }
    if (listen(server_fd, MAX_CLIENTS) < 0) {
        perror("listen"); exit(1);
    }

    fprintf(stderr, "Servidor listo en puerto %d.\n", PORT);

    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);

    while (1) {
        int client_fd = accept(server_fd,
                               (struct sockaddr *)&client_addr,
                               &client_len);
        if (client_fd < 0) continue;

        /* Rechazar si ya se alcanzó el límite de clientes */
        if (active_clients >= MAX_CLIENTS) {
            send(client_fd, "FULL\n", 5, 0);
            close(client_fd);
            continue;
        }

        /* Copiar la IP antes del fork (inet_ntoa usa buffer estático) */
        char ip_copy[INET_ADDRSTRLEN];
        strncpy(ip_copy, inet_ntoa(client_addr.sin_addr),
                INET_ADDRSTRLEN - 1);
        ip_copy[INET_ADDRSTRLEN - 1] = '\0';

        pid_t pid = fork();

        if (pid == 0) {
            /* ── proceso hijo ── */
            close(server_fd);
            handle_client(client_fd, ip_copy);
            exit(0);

        } else if (pid > 0) {
            /* ── proceso padre ── */
            active_clients++;
            close(client_fd);

        } else {
            perror("fork");
            close(client_fd);
        }
    }

    close(server_fd);
    if (bloques) free(bloques);
    return 0;
}