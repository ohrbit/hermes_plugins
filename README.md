# hermes_plugins

A collection of [Hermes Agent](https://hermes-agent.nousresearch.com) plugins by **ohrbit**.

[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)

## Plugins

### [esp-hermes](esp-hermes/) — ESP32-S3 voice / IO channel
Turn an M5Stack ESP32-S3 into a first-class Hermes voice + hardware channel. Hermes (the gateway) is the brain; the ESP is a thin physical client with GPIO / I2C / PWM / IMU access and an LCD pet companion. Design is frozen; the firmware is drafted until hardware arrives. See [`esp-hermes/README.md`](esp-hermes/README.md).

### [galaxy_brain](galaxy_brain/) — 3D knowledge-graph plugin
Interactive 3D force-directed graph of your Obsidian vault, rendered in the Hermes dashboard. See [`galaxy_brain/README.md`](galaxy_brain/README.md).

## Layout

Each plugin lives in its own subdirectory with its own `README.md`, `LICENSE`,
and implementation notes. Build / install instructions are per-plugin — start
from each plugin's README.

## License

MIT — © 2026 ohrbit. See [LICENSE](LICENSE).
