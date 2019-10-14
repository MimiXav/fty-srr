# fty-srr

fty-srr is an agent who has in charge Save, Restore, and Reset the system.

## How to build

To build fty-srr project run:

```bash
./autogen.sh
./configure
make
make check # to run self-test
```

## How to run

To run fty-srr project:

* from within the source tree, run:

```bash
./src/fty-srr
```

For the other options available, refer to the manual page of fty-srr.

* from an installed base, using systemd, run:

```bash
systemctl start fty-srr
```

### Configuration file

Agent has a configuration file: fty-srr.cfg.
Except standard server and malamute options, there are two other options:
* server/check_interval for how often to publish Linux system metrics
* parameters/path for REST API root used by IPM Infra software
Agent reads environment variable BIOS_LOG_LEVEL, which sets verbosity level of the agent.

## Architecture

### Overview
