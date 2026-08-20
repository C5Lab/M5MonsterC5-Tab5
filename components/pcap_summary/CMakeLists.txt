idf_component_register(
    SRCS "pcap_summary_reducers.c" "pcap_summary.c" "pcap_summary_report.c"
    INCLUDE_DIRS "include"
    REQUIRES pcap_reader
)
