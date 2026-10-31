#include "soa/app.h"

int main(int argc, char *argv[])
{
    return soa_app_main(argc, argv, PROGRAM_PRODUCER, "producer",
                        "generar e insertar mensajes",
                        "--name /instancia --id N --mean-ms MS --seed N "
                        "[--messages N] --log results/runs/productor.csv");
}
