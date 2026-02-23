# Installing the software switch

On the Debian device you want to run the software switch on, simply clone/copy the project repository onto the device and (from the repository's root) run:

```
sudo ./tools/setup-on-debian.sh
```

If you don't have permission, run `chmod +x ./tools/setup-on-debian.sh` and then try again.

The script will automatically fetch the necessary dependencies and build the executables.
