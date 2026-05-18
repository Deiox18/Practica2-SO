#ifndef COMMON_H
#define COMMON_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/wait.h>

/* Tamaño máximo de una línea del CSV */
#define MAX_LINE_LEN  500

/* Cada cuántos registros se guarda un punto de control en el índice */
#define BLOQUE_SIZE   1000

/* Puerto TCP del servidor */
#define PORT          8080

/* Máximo de clientes simultáneos */
#define MAX_CLIENTS   32

/* Archivo de log del servidor */
#define LOG_FILE      "server.log"

/* Entrada del índice de bloques en disco */
typedef struct {
    uint32_t person_id;
    uint32_t offset;
} BloqueInfo;

/* Estado de sesión de cada cliente (vive en el proceso hijo) */
typedef struct {
    char *id;
    char *nombre;
    char *ciudad;
} ClientSession;

#endif