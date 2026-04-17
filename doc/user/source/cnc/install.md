# Installing the CNC

## 1. Install the programs

(If you've already done this step to install the software switch (`tsnctrld`), then you can skip to the next step)

On the Debian device you want to run the CNC on, simply clone/copy the project repository onto the device and (from the repository's root) run:

```
sudo ./tools/setup-on-debian.sh
```

If you don't have permission, run `chmod +x ./tools/setup-on-debian.sh` and then try again.

The script will automatically fetch the necessary dependencies and build the executables.

## 2. Add configuration

It's a good idea to set a default configuration for the CNC Daemon `cncd`.
However, it's possible specify a custom configuration when your run the daemon by for example running `cncd path/to/inventory.json`

Create a file in `/etc/inventory.json` (with sudo) and fill it with data similar to this:

```
{
  "inventory": [
    {
      "hostName": "vstsn01",
      "management_ip": "192.168.216.141",
      "username": "netconf-api",
      "password": "private"
    },
    {
      "hostName": "vstsn02",
      "management_ip": "192.168.216.142",
      "username": "netconf-api",
      "password": "private"
    },
    {
      "hostName": "vstsn03",
      "management_ip": "192.168.216.143",
      "username": "netconf-api",
      "password": "private"
    }
  ]
}
```

Create a new subelement of `inventory` for each NETCONF-capable network device that you want to control with the CNC.
- **hostName**: How the CNC calls this device. Is used when setting schedules and fetching data.
- **management_ip**: The device's IP on the control plane.
- **username**: Leave this `netconf-api` for all `tsnctrld` software switches.
- **password**: For `tsnctrld` software switches, provide the password you have set (or will set) [during step 3 of the software switch installation](../tsnctrld/install.md#3-set-password)

You can always go back and change the `/etc/inventory.json` file when your network configuration changes.
