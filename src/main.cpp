#include "UsdSimApp.h"

int main(int argc, char *argv[])
{
    UsdSimApp app;
    app.register_types();
    app.init(argc, argv);
    int ret = app.exec_app();
    app.uninit();
    app.unregister_types();
    return ret;
}
