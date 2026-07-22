# BV9000CL ChibiOS Shell

This frontend uses the ChibiOS 5 tree at `/opt/gbc/sama5d2x/ChibiOS`.

## Build Targets

```bash
make shell-uart
```

The shell uses UART1 at 115200 baud for the console and UART3 at 115200 baud
for ioHdlc. The frontend exposes one physical HDLC port, so run the exchange
test as either endpoint A or endpoint B:

```text
exchange --endpoint=a
exchange --endpoint=b
```
