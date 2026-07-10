#include "signal/server.h"

#include "common/demo_privilege.h"

int main(int argc, char* argv[]) {
    rflow::apps::common::DropRootPrivilegesIfSudoInvoked();
    return rflow::signal::server::RunMain(argc, argv);
}

