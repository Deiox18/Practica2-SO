# Práctica 2 — Sistemas Operativos

Sistema cliente-servidor en C para consulta de registros de personas sobre un dataset de más de 1GB, con restricción de uso máximo de 1MB de RAM en el proceso servidor.

---

## Descripción

El sistema está compuesto por dos programas:

- **p2-server** — gestiona el dataset en disco, atiende hasta 32 clientes simultáneos mediante `fork()`, registra operaciones en un archivo log y mantiene un índice de bloques en memoria para búsquedas eficientes.
- **p2-client** — interfaz de usuario en consola que se conecta al servidor por TCP y permite buscar registros por ID, nombre o ciudad.

---

## Archivos

```
├── common.h          # Estructuras, constantes e includes compartidos
├── csv_parser.c      # Parser de líneas CSV con soporte para campos entre comillas
├── p2-server.c       # Programa servidor
├── p2-client.c       # Programa cliente
├── Makefile          # Compilación automática
└── LEEME             # Manual de uso en español
```

---

## Requisitos

- GCC
- Sistema operativo Linux
- Dataset `personas.csv` en la misma carpeta que los ejecutables (no incluido por su tamaño)

---

## Compilación

```bash
make
```

Para limpiar los archivos compilados:

```bash
make clean
```

---

## Uso

**1. Iniciar el servidor** (en una terminal):
```bash
./p2-server
```
El servidor no muestra menú. La primera vez construye el índice del dataset (puede tardar varios minutos con datasets grandes). Las siguientes veces carga el índice desde disco y arranca al instante. Cuando muestre:
```
Servidor listo en puerto 8080.
```
ya está listo para recibir clientes.

**2. Iniciar el cliente** (en otra terminal):
```bash
./p2-client
```

---

## Menú del cliente

```
Bienvenido
1. Ingresar ID de persona
2. Ingresar nombre
3. Ingresar ciudad
4. Realizar búsqueda
5. Salir
```

Los filtros se pueden combinar. Por ejemplo: ingresar nombre y ciudad antes de buscar aplica los dos filtros al mismo tiempo. Los filtros se limpian automáticamente después de cada búsqueda.

---

## Protocolo de comunicación

La comunicación entre cliente y servidor usa **TCP** sobre el puerto **8080** con comandos de texto plano:

| Cliente → Servidor | Descripción |
|---|---|
| `SET_ID <valor>` | Establece filtro por ID exacto |
| `SET_NAME <valor>` | Establece filtro por nombre (parcial) |
| `SET_CITY <valor>` | Establece filtro por ciudad (parcial) |
| `SEARCH` | Ejecuta la búsqueda con los filtros activos |
| `EXIT` | Cierra la sesión |

| Servidor → Cliente | Descripción |
|---|---|
| `READY` | Conexión aceptada |
| `FULL` | Servidor lleno, máximo 32 clientes |
| `OK <mensaje>` | Confirmación de cada SET |
| `(líneas CSV)` | Resultados de la búsqueda |
| `NA` | Sin resultados |
| `END` | Fin de los resultados |

---

## Formato del log

Cada búsqueda queda registrada en `server.log` con el formato:

```
[20260518T143022] Cliente [127.0.0.1] [búsqueda - 1234 - Tim - port]
```

---

## Consideraciones técnicas

- El servidor usa un **índice de bloques** guardado en `bloques.idx` que almacena la posición exacta en bytes de cada 1000 registros del CSV. Esto permite hacer búsquedas por ID sin cargar el dataset en memoria.
- La búsqueda secuencial (por nombre o ciudad) lee el archivo directamente desde disco con un buffer fijo, devolviendo máximo **3 resultados**.
- El uso de memoria del proceso servidor se mantiene bajo **1MB de datos propios** (`VmData`), verificable con:
```bash
cat /proc/$(pgrep p2-server)/status | grep -E "VmRSS|VmData|VmStk"
```
- Cada cliente es atendido por un **proceso hijo independiente** creado con `fork()`.
- El servidor admite un máximo de **32 clientes simultáneos**.

---

## Comandos útiles

```bash
# verificar que el servidor escucha en el puerto 8080
ss -tlnp | grep 8080

# ver memoria del servidor
cat /proc/$(pgrep p2-server)/status | grep -E "VmRSS|VmData|VmStk"

# ver clientes conectados
ps --ppid $(pgrep p2-server)

# ver log en tiempo real
tail -f server.log

# probar límite de clientes
for i in $(seq 1 35); do ./p2-client & done

# matar todos los clientes
kill -9 $(pgrep p2-client)
```

---

## Autor
Diever Santiago Urbano Samboni
