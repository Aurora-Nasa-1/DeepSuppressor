# AMMF2-overlay

[中文](./README.md)

## Introduction

AMMF2-overlay is a rapid module development tool for the AMMF2 framework. It provides a simple framework that allows developers to quickly create and maintain Magisk modules based on AMMF2.

## Quick Start

1. Clone the repository
```bash
git clone https://github.com/your-username/AMMF2-overlay.git
cd AMMF2-overlay
```

2. Modify the configuration
```bash
# Edit module_settings/build_config.sh
action_id="your_module_id"
action_name="Your Module Name"
```

3. Create a repository, push code and create tags
```bash
git tag v1.0.0
git push --tags
```

## Documentation

- [Module Development Guide](./docs/module_development.md)
- [Module Development Guide (English)](./docs/module_development_en.md)

## License

This project is licensed under the MIT License.

## Contributing

Contributions via Issues and Pull Requests are welcome!

## Acknowledgements

- [AMMF2](https://github.com/Aurora-Nasa-1/AMMF2)
- [Magisk](https://github.com/topjohnwu/Magisk)