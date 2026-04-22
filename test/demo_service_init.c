/**
 * Service 端 SDK 初始化示例：svc_set_global_config -> svc_init -> svc_uninit
 * 编译：在工程根目录用 CMake 构建目标 demo_service_init（见 test/CMakeLists.txt）
 */
#include <stdio.h>
#include <stdlib.h>

#include "rflow/Service/librflow_service_api.h"

static void print_err(const char *what, rflow_err_t err) {
    fprintf(stderr, "%s: %s (%d)\n", what, librflow_err_to_string(err), (int)err);
    const char *le = librflow_get_last_error();
    if (le && le[0] != '\0') {
        fprintf(stderr, "  last_error: %s\n", le);
    }
}

int main(void) {
    uint32_t maj = 0, min = 0, pat = 0;
    librflow_get_version(&maj, &min, &pat);
    printf("librflow_svc version %u.%u.%u\n", maj, min, pat);
    const char *build = librflow_get_build_info();
    if (build) {
        printf("%s\n", build);
    }

    librflow_global_config_t gcfg = librflow_global_config_create();
    if (!gcfg) {
        fprintf(stderr, "librflow_global_config_create failed\n");
        return EXIT_FAILURE;
    }

    rflow_err_t err = librflow_svc_set_global_config(gcfg);
    librflow_global_config_destroy(gcfg);
    gcfg = NULL;
    if (err != RFLOW_OK) {
        print_err("librflow_svc_set_global_config", err);
        return EXIT_FAILURE;
    }

    err = librflow_svc_init();
    if (err != RFLOW_OK) {
        print_err("librflow_svc_init", err);
        return EXIT_FAILURE;
    }
    printf("librflow_svc_init OK\n");

    err = librflow_svc_uninit();
    if (err != RFLOW_OK) {
        print_err("librflow_svc_uninit", err);
        return EXIT_FAILURE;
    }
    printf("librflow_svc_uninit OK\n");

    return EXIT_SUCCESS;
}
