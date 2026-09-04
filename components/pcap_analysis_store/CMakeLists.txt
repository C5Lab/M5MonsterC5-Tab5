idf_component_register(
    SRCS "pcap_analysis_store.c"
    INCLUDE_DIRS "include"
    REQUIRES pcap_reader pcap_summary pcap_flow pcap_investigation fatfs
)
