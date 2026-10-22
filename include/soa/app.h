#ifndef SOA_APP_H
#define SOA_APP_H

/* Interfaz privada a la cáscara CLI: no define el protocolo de memoria compartida.
 * --help retorna 0; cualquier operación aún pendiente retorna un error.
 * No crea recursos IPC ni abre archivos de resultados.
 */
int soa_app_main(int argc, char *argv[], const char *name,
                 const char *description, const char *options);

#endif
