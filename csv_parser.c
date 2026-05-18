#include <string.h>

/*
 * parse_csv_line - Divide una línea CSV en campos individuales.
 *
 * Maneja correctamente campos entre comillas dobles que puedan
 * contener comas internas, por ejemplo: "Unit 487JJ,1729"
 *
 * Parámetros:
 *   line       - línea a parsear (se modifica in-place)
 *   fields[]   - arreglo donde se almacenan los punteros a cada campo
 *   max_fields - número máximo de campos a extraer
 *
 * Retorna el número de campos encontrados.
 */
int parse_csv_line(char *line, char *fields[], int max_fields) {
    int field_count = 0;
    char *p = line;

    while (*p && field_count < max_fields) {
        if (*p == '"') {
            /* Campo entre comillas: avanzar tras la comilla de apertura */
            p++;
            fields[field_count++] = p;
            /* Buscar la comilla de cierre */
            while (*p && *p != '"') p++;
            if (*p == '"') *p++ = '\0';  /* terminar el campo */
            if (*p == ',') p++;          /* saltar la coma separadora */
        } else {
            /* Campo sin comillas */
            fields[field_count++] = p;
            while (*p && *p != ',' && *p != '\n' && *p != '\r') p++;
            if (*p == ',' || *p == '\n' || *p == '\r') *p++ = '\0';
        }
    }
    return field_count;
}