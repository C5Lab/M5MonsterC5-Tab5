idf_component_register(
    SRCS "pcap_investigation.c"
    INCLUDE_DIRS "include"
    REQUIRES pcap_flow pcap_summary fatfs
)
