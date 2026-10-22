#include "soa/app.h"

int main(int argc, char *argv[])
{
    return soa_app_main(argc, argv, "consumer", "extraer y procesar mensajes FIFO",
                        "--name /instancia --id N --mean-ms MS --seed N "
                        "--log results/runs/consumidor.csv");
}
