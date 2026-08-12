idf_component_register(
    SRCS "pcap_extract.c"
    INCLUDE_DIRS "include"
    REQUIRES pcap_reader fatfs mbedtls
)
