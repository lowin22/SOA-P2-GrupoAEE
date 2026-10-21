#include "soa/app.h"

int main(int argc, char *argv[])
{
    return soa_app_main(argc, argv, "finisher", "coordinar el cierre y liberar recursos",
                        "--name /instancia --summary results/runs/resumen.csv");
}
