#include "common.h"
#include <ctype.h>

/* ─────────────────────────────────────────────────────────────
 * main - programa cliente. Se conecta al servidor, muestra el
 * menú y envía los comandos correspondientes a cada opción.
 *
 * Protocolo enviado al servidor:
 *   SET_ID <valor>    - establece filtro por ID
 *   SET_NAME <valor>  - establece filtro por nombre
 *   SET_CITY <valor>  - establece filtro por ciudad
 *   SEARCH            - solicita búsqueda con filtros actuales
 *   EXIT              - cierra la conexión
 * ───────────────────────────────────────────────────────────── */
int main(void) {
    /* ── crear socket y conectar al servidor ── */
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) { perror("socket"); return 1; }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port   = htons(PORT);
    inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr);

    if (connect(sock, (struct sockaddr *)&server_addr,
                sizeof(server_addr)) < 0) {
        printf("No se pudo conectar con el servidor. "
               "¿Está ejecutándose?\n");
        close(sock);
        return 1;
    }

    /* ── leer primer mensaje: READY o FULL ── */
    char first[16];
    int n = (int)recv(sock, first, sizeof(first) - 1, 0);
    if (n <= 0) {
        printf("El servidor cerró la conexión inesperadamente.\n");
        close(sock);
        return 1;
    }
    first[n] = '\0';

    if (strncmp(first, "FULL", 4) == 0) {
        printf("Servidor lleno (máximo %d clientes). "
               "Intente más tarde.\n", MAX_CLIENTS);
        close(sock);
        return 1;
    }
    /* si es "READY", continuamos normalmente */

    /* ── variables de sesión ── */
    char id[50]      = "";
    char nombre[100] = "";
    char ciudad[100] = "";
    char request[MAX_LINE_LEN];
    char resp[MAX_LINE_LEN * 4];
    int  option;
    int  c;

    /* ── menú principal ── */
    do {
        printf("\nBienvenido\n");
        printf("1. Ingresar ID de persona\n");
        printf("2. Ingresar nombre\n");
        printf("3. Ingresar ciudad\n");
        printf("4. Realizar búsqueda\n");
        printf("5. Salir\n");
        printf("Opción: ");

        if (scanf("%d", &option) != 1) {
            while ((c = getchar()) != '\n' && c != EOF);
            option = 0;
        } else {
            while ((c = getchar()) != '\n' && c != EOF);
        }

        switch (option) {

            case 1:
                printf("ID: ");
                fgets(id, sizeof(id), stdin);
                id[strcspn(id, "\n")] = '\0';

                snprintf(request, sizeof(request), "SET_ID %s", id);
                send(sock, request, strlen(request), 0);

                n = (int)recv(sock, resp, sizeof(resp) - 1, 0);
                if (n > 0) { resp[n] = '\0'; printf("%s", resp); }

                printf("\n--- Presiona Enter para continuar ---");
                while ((c = getchar()) != '\n' && c != EOF);
                break;

            case 2:
                printf("Nombre (puede ser parte del nombre): ");
                fgets(nombre, sizeof(nombre), stdin);
                nombre[strcspn(nombre, "\n")] = '\0';

                snprintf(request, sizeof(request), "SET_NAME %s", nombre);
                send(sock, request, strlen(request), 0);

                n = (int)recv(sock, resp, sizeof(resp) - 1, 0);
                if (n > 0) { resp[n] = '\0'; printf("%s", resp); }

                printf("\n--- Presiona Enter para continuar ---");
                while ((c = getchar()) != '\n' && c != EOF);
                break;

            case 3:
                printf("Ciudad (puede ser parte del nombre): ");
                fgets(ciudad, sizeof(ciudad), stdin);
                ciudad[strcspn(ciudad, "\n")] = '\0';

                snprintf(request, sizeof(request), "SET_CITY %s", ciudad);
                send(sock, request, strlen(request), 0);

                n = (int)recv(sock, resp, sizeof(resp) - 1, 0);
                if (n > 0) { resp[n] = '\0'; printf("%s", resp); }

                printf("\n--- Presiona Enter para continuar ---");
                while ((c = getchar()) != '\n' && c != EOF);
                break;

            case 4: {
                send(sock, "SEARCH", 6, 0);
                printf("\nBuscando...\n");
                printf("Resultados (máximo 3):\n");

                /* Recibir resultados hasta encontrar "END" */
                while (1) {
                    n = (int)recv(sock, resp, sizeof(resp) - 1, 0);
                    if (n <= 0) break;
                    resp[n] = '\0';

                    /* "END" puede llegar junto con la última línea de datos */
                    char *end_pos = strstr(resp, "END");
                    if (end_pos) {
                        *end_pos = '\0';          /* cortar antes de END */
                        if (strlen(resp) > 0)
                            printf("%s", resp);
                        break;
                    }
                    printf("%s", resp);
                }

                printf("\n--- Presiona Enter para continuar ---");
                while ((c = getchar()) != '\n' && c != EOF);
                break;
            }

            case 5:
                send(sock, "EXIT", 4, 0);
                n = (int)recv(sock, resp, sizeof(resp) - 1, 0);
                if (n > 0) { resp[n] = '\0'; printf("%s", resp); }
                printf("Saliendo...\n");
                break;

            default:
                printf("Opción no válida. Intente de nuevo.\n");
                break;
        }

    } while (option != 5);

    close(sock);
    return 0;
}