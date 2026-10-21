#include "soa/app.h"

int main(int argc, char *argv[])
{
    return soa_app_main(argc, argv, "creator", "crear e inicializar una instancia",
                        "--name /instancia --capacity N");
}
