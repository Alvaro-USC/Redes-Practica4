#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <resolv.h>

#define MAX_LINE 128
#define MAX_ROUTES 128

// Estructura para almacenar una entrada de la tabla de reenvío
typedef struct {
    struct in_addr net;   // Dirección de red (en orden de red)
    int sufijo;           // Longitud del sufijo
    int iface;            // interfaz de salida
} route_t;

// Devuelve la máscara (en orden de red) a partir del sufijo
static uint32_t mask_from_sufix(int sufijo) {
    if (sufijo == 0) return 0;
    // Primero, se crea un número de 32 bits, y se le pone un uno en la posición 32 - prefix (el /)
    // Segundo, le restamos 1 a lo anterior para que ese único uno solitario se vuelve un secuenci de 32-prefix unos al final de los bits
    // Tercero, se niegan todos los bits, dándoles la vuelta, de tal forma que : 
    return htonl(~((1u << (32 - sufijo)) - 1));
}

// Muestra mensaje de uso
static void usage(const char *prog) {
    fprintf(stderr, "Uso: %s <tabla> <ip_destino>\n", prog);
    exit(EXIT_FAILURE);
}

int main(int argc, char *argv[]) {
    if (argc != 3)
        usage(argv[0]);

    const char *filename = argv[1];
    const char *ip_dest_str = argv[2];

    FILE *f = fopen(filename, "r");
    if (!f) {
        perror("Error al abrir el archivo de tabla");
        exit(EXIT_FAILURE);
    }

    // Reservamos dinámicamente espacio para las rutas
    int capacidad = MAX_ROUTES;
    route_t *rutas = malloc(capacidad * sizeof(route_t));
    if (!rutas) {
        perror("Error en malloc");
        fclose(f);
        exit(EXIT_FAILURE);
    }

    int n = 0;
    char linea[MAX_LINE];

    // Leer la tabla de reenvío
    while (fgets(linea, sizeof(linea), f)) {
        if (linea[0] == '\n')
            continue;

        if (n == capacidad) {
            capacidad *= 2;
            route_t *tmp = realloc(rutas, capacidad * sizeof(route_t));
        if (!tmp) {
                perror("Error en realloc");
                free(rutas);
                fclose(f);
                exit(EXIT_FAILURE);
            }
            rutas = tmp;
        }

        char *coma = strchr(linea, ',');
        if (!coma)
            continue;

        *coma = '\0';
        char *iface_str = coma + 1;

        rutas[n].iface = atoi(iface_str);
        rutas[n].net.s_addr = 0;

        // inet_net_pton convierte texto (IP/sufijo) a binario
        rutas[n].sufijo = inet_net_pton(AF_INET, linea, &rutas[n].net, sizeof(struct in_addr));
        if (rutas[n].sufijo < 0) {
            fprintf(stderr, "Advertencia: formato inválido en línea: %s\n", linea);
            continue;
        }
        n++;
    }

    fclose(f);

    if (n == 0) {
        fprintf(stderr, "Error: tabla vacía o inválida.\nSe usa la red por defecto 0.0.0.0/0\n");
        // Creamos la ruta por defecto manualmente
        rutas = realloc(rutas, sizeof(route_t));
        if (!rutas) {
            perror("Error al crear ruta por defecto");
            exit(EXIT_FAILURE);
        }
    
        rutas[0].net.s_addr = inet_addr("0.0.0.0");
        rutas[0].sufijo = 0;
        rutas[0].iface = 0;
        n = 1;
    }

    // Convertir IP destino a formato binario
    struct in_addr ip_dest;
    if (inet_pton(AF_INET, ip_dest_str, &ip_dest) != 1) {
        free(rutas);
        fprintf(stderr, "Dirección IP destino inválida: %s\n", ip_dest_str);
        exit(EXIT_FAILURE);
    }

    // Buscar mejor coincidencia (sufijo más largo)
    int mejor_iface = 0;       // interfaz por defecto
    int mejor_sufijo = -1;
    struct in_addr mejor_red;
    for (int i = 0; i < n; i++) {
        uint32_t mask = mask_from_sufix(rutas[i].sufijo);
        // Lo que hacemos es coger la IP de destino en formato binario, y le aplicamos la máscara,
        // esto supone eliminar los últimos 32 - prefix bits de la ip, ya que la máscara tiene todo ceros 
        // en las posiciones últimas 32-sufijo
        // Exactamente lo mismo lo hacemos con la IP del archivo que estmaos iterando con el array
        // De esta forma en ambas nos estamos quedando solo con los primeros sufijo bits de cada IP
        // el resto se pierden, por lo que solo tenemos la parte de la IP que identifca esa IP en ese sufijo
        // Por ejemplo: 194.64.20.5/16 aplicándole la maścara para 16 bits de sufijo (32-16), nos devolvería:
        // 194.64.0.0
        if ((ip_dest.s_addr & mask) == (rutas[i].net.s_addr & mask)) {
            if (rutas[i].sufijo > mejor_sufijo) {
                mejor_sufijo = rutas[i].sufijo;
                mejor_iface = rutas[i].iface;
                mejor_red = rutas[i].net;
            }
        }
    }

    if (mejor_sufijo < 0)
        mejor_sufijo = 0;

    char red_str[INET_ADDRSTRLEN];
    if (inet_ntop(AF_INET, &mejor_red, red_str, sizeof(red_str)) == NULL) {
        free(rutas);
        fprintf(stderr, "Dirección IP de mejor red de la tabla  inválida: %s\n", red_str);
        exit(EXIT_FAILURE);
    }
   
    printf("IP destino: %s\n", ip_dest_str);
    printf("Coincide con red: %s/%d\n", red_str, mejor_sufijo);
    printf("Interfaz de salida: %d\n", mejor_iface);
    printf("Sufijo aplicado: %d bits\n", mejor_sufijo);

    free(rutas);

    return 0;
}
