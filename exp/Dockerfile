FROM val01:5000/rdma-ubuntu:16.04-ofed-driver

COPY krc_client_sync /krc_client_sync
CMD ["/krc_client_sync", "-payload_sz", "4096"]