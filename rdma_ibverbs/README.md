# RDMA Build & Run Guide

## Install Development Libraries

On Debian/Ubuntu-like systems:

``` bash
sudo apt-get install libibverbs-dev librdmacm-dev
```

## Compile

It is recommended to use **pkg-config** to get the correct flags:

``` bash
gcc rdma_server.c -o rdma_server $(pkg-config --cflags --libs libibverbs)
gcc rdma_client.c -o rdma_client $(pkg-config --cflags --libs libibverbs)
```

If pkg-config for libibverbs isn't available on your platform, you can
try:

``` bash
gcc rdma_server.c -o rdma_server -libverbs
gcc rdma_client.c -o rdma_client -libverbs
```

> Note: pkg-config is more reliable; adjust include/link flags as
> needed on your distro.

## Run

On the **server machine** (replace with a port, e.g. 18515):

``` bash
./rdma_server 18515
```

On the **client machine** (replace `SERVER_IP` with the server's IP):

``` bash
./rdma_client SERVER_IP 18515
```

## Notes

-   The example assumes HCA port number **1**. If your HCA uses a
    different port, change `port_num`.
-   You might need to run as **root** or with capabilities that allow
    access to the HCA (or configure RDMA udev rules). On many test
    systems people run as root.
-   If there are multiple HCAs, the code opens the **first device**
    returned by `ibv_get_device_list`. For multi-device setups, pick the
    correct device by name.
-   This sample uses a **simple TCP socket** to exchange the out-of-band
    parameters required to put QPs into RTR/RTS (this is how real apps
    like `rping` / `librdmacm` do handshakes).
-   This is a minimal example meant to teach the flow --- production
    code must handle retries, timeouts, more robust error checking,
    big-endian/network order conversions if desired, and security of the
    TCP channel.

