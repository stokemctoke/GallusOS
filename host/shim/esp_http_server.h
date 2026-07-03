#pragma once

#include "esp_err.h"

typedef void* httpd_handle_t;

struct httpd_req;
typedef struct httpd_req httpd_req_t;

typedef enum {
    HTTP_GET,
    HTTP_POST,
    HTTP_PUT,
    HTTP_DELETE,
} httpd_method_t;

typedef esp_err_t (*httpd_uri_match_fn_t)(const char* uri_template,
                                          const char* uri_to_match,
                                          size_t match_len);
