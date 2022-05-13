#include "kernel_helper.h"
#define BUF_LENGTH 256
#define DEFAULT_PERMISSION S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH

char gids_arr[BUF_LENGTH] = "fe80:0000:0000:0000:ec0d:9a03:0078:645e";
char* meta_server_gid = gids_arr;
module_param_string(meta_server_gid, gids_arr, BUF_LENGTH, DEFAULT_PERMISSION);

