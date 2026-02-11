# Installing the dependencies for building the project {#howto-install-dev}

Simply clone/copy the project repository onto the Debian device and (from the repository's root) run:

```
sudo ./tools/setup-on-debian.sh
```

If you don't have permission, run `chmod +x ./tools/setup-on-debian.sh` and then try again.

The script will setup all the dependencies and build the executables once.
See [this guide](@ref howto-build-check) for more information on building the project components.
