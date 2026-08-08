idf_component_register(
    SRCS "janos_file_transfer.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_http_client fatfs json
)
