# MIP104 ChibiOS Shell

This frontend uses the ChibiOS 5 tree at `/opt/gbc/sama5d2x/ChibiOS`.

## Build Targets

```bash
make shell-spi1
make shell-spi-flex2
```

Each target has a separate output directory under `build/`. The SPI runs at
1 MHz in mode 0 and transfers octets MSB-first. Both controllers are enabled
in each target. The active-low SPI signal buffer is enabled at startup through
`LINE_ENABLE_BUFFER`.

The target selects which controller implements endpoint A, while endpoint B
uses the other controller:

```text
shell-spi1:      endpoint A = SPI1 master, endpoint B = FLEXCOM2 slave
shell-spi-flex2: endpoint A = FLEXCOM2 master, endpoint B = SPI1 slave
```

Run `exchange` without `--endpoint` for an intraboard test. The SPI adapter
supports NRM/TWA only and uses the slave controller's DATA_READY output to
notify the master controller.

## SPI Pin Assignments

| Signal | FLEXCOM2 | SPI1 |
|--------|----------|------|
| Clock | PA08 | PC01 |
| MOSI | PA06 | PC02 |
| MISO | PA07 | PC03 |
| Chip select | PA09 | PC04 |
| DATA_READY (`SISPn_IRQ`) | PA10 | PB16 |

Connect the corresponding signals between the two headers:

```text
PA08 <-> PC01
PA06 <-> PC02
PA07 <-> PC03
PA09 <-> PC04
PA10 <-> PB16
```

The same wiring supports both SPI targets; changing the target reverses the
master/slave roles and signal directions.
