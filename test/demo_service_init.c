/**
 * Service 端 SDK 初始化示例：svc_set_global_config -> svc_init -> svc_uninit
 * 编译：在工程根目录用 CMake 构建目标 demo_service_init（见 test/CMakeLists.txt）
 */
#include <stdio.h>
#include <stdlib.h>

#include "robrt/Service/librobrt_service_api.h"

static void print_err(const char *what, robrt_err_t err) {
    fprintf(stderr, "%s: %s (%d)\n", what, librobrt_err_to_string(err), (int)err);
    const char *le = librobrt_get_last_error();
    if (le && le[0] != '\0') {
        fprintf(stderr, "  last_error: %s\n", le);
    }
}

int main(void) {
    uint32_t maj = 0, min = 0, pat = 0;
    librobrt_get_version(&maj, &min, &pat);
    printf("librobrt_svc version %u.%u.%u\n", maj, min, pat);
    const char *build = librobrt_get_build_info();
    if (build) {
        printf("%s\n", build);
    }

    librobrt_global_config_t gcfg = librobrt_global_config_create();
    if (!gcfg) {
        fprintf(stderr, "librobrt_global_config_create failed\n");
        return EXIT_FAILURE;
    }

    robrt_err_t err = librobrt_svc_set_global_config(gcfg);
    librobrt_global_config_destroy(gcfg);
    gcfg = NULL;
    if (err != ROBRT_OK) {
        print_err("librobrt_svc_set_global_config", err);
        return EXIT_FAILURE;
    }

    err = librobrt_svc_init();
    if (err != ROBRT_OK) {
        print_err("librobrt_svc_init", err);
        return EXIT_FAILURE;
    }
    printf("librobrt_svc_init OK\n");

    err = librobrt_svc_uninit();
    if (err != ROBRT_OK) {
        print_err("librobrt_svc_uninit", err);
        return EXIT_FAILURE;
    }
    printf("librobrt_svc_uninit OK\n");

    return EXIT_SUCCESS;
}
