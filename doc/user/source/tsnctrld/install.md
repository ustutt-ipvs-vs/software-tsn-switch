# Installing the software switch

## Requirements

The network interface cards you're planning to use for the data plane need to support hardware timestamping.
To check this, run `sudo ethtool -T <NIC-NAME>` and look for the `hardware-transmit` and `hardware receive` capabilities.

## 1. Install the programs

(If you've already done this step to install the CNC, then you can skip to the next step)

On the Debian device you want to run the software switch on, simply clone/copy the project repository onto the device and (from the repository's root) run:

```
sudo ./tools/setup-on-debian.sh
```

**DO NOT RUN THIS SCRIPT TWICE.** If you've already installed the CNC, do not run this script again, just proceed with the next step. 

If you don't have permission, run `chmod +x ./tools/setup-on-debian.sh` and then try again.

The script will automatically fetch the necessary dependencies and build the executables.

## 2. Disable default NTP

Disable the system's default NTP service, it might interfere with our own clock synchronization

```
sudo timedatectl set-ntp false
```

## 3. Add NTP servers

**This step is only necessary if you want/need to use custom NTP servers**, for example if your network's firewall blocks NTP packets.
Advanced users that use a custom method of time synchronization, so that NTP won't be used on this device, can skip this step as well.

Create a file in `/etc/chrony/sources.d/` with the file name ending with `.sources`.
Then, for a default NTP configuration, add as many `server <NTP-SERVER-ADDRESS> iburst` directives as needed, one directive per line, and the file must have a trailing newline character at the end.

For example, if you are inside the network of the Universität Stuttgart, which blocks NTP servers outside the network, this would be the example content of the file `/etc/chrony/sources.d/uni-stuttgart-ntp.sources`:

```
server time1.uni-stuttgart.de iburst
server time2.uni-stuttgart.de iburst
server time3.uni-stuttgart.de iburst

```

Don't forget the trailing newline character!  
For more advanced NTP configurations, check the [chrony.conf documentation](https://chrony-project.org/doc/latest/chrony.conf.html), since instead of `server` directives you can also use `pool` and `peer` directives in this file.

## 4. Set password
The default NETCONF password is `private`, you shouldn't leave it like this.
Use `sudo passwd netconf-api` to change it to something more secure.
You will need to specify this password in the `inventory.json` file when you install the CNC!


## Further reading
Since you've now installed `tsnctrld`, start learning how to use it with our [quickstart guide](quickstart.md).
