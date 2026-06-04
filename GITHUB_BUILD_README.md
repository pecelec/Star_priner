# Building the RUT240 OSC listener in GitHub

I cannot compile the MIPS binary directly in this ChatGPT environment because there is no MIPS/OpenWrt cross compiler available here. This pack adds a GitHub Actions workflow that will compile it for you in your GitHub repo.

## Files to add to your repo

Copy these into the root of your repo:

```text
osc_star_printer.c
Makefile
.github/workflows/build-rut240-osc.yml
```

## Run the build

Push to GitHub, then go to:

```text
GitHub repo
  -> Actions
  -> Build RUT240 OSC listener
  -> Run workflow
```

The workflow builds two artifacts:

```text
osc_star_printer_mips_static
osc_star_printer_openwrt_ar71xx
```

Try the OpenWrt one first. If that workflow step fails or the binary does not run on the router, try the generic static MIPS one.

## Copy to the RUT240

Download the artifact ZIP from GitHub Actions, extract the binary, then:

```powershell
scp .\osc_star_printer_openwrt_ar71xx root@192.168.178.1:/root/osc_star_printer
```

or, if using the static one:

```powershell
scp .\osc_star_printer_mips_static root@192.168.178.1:/root/osc_star_printer
```

On the router:

```sh
chmod +x /root/osc_star_printer
/root/osc_star_printer -l 9000 -h 192.168.178.121 -p 9100
```

Send OSC to:

```text
router_ip:9000
address: /print
args: "George", "Hello from OSC"
```

## Startup script

```sh
sleep 30

if ! ps | grep osc_star_printer | grep -v grep >/dev/null; then
  /root/osc_star_printer -l 9000 -h 192.168.178.121 -p 9100 >/tmp/osc_star.log 2>&1 &
fi

if ! ps | grep rut240_star_worker.sh | grep -v grep >/dev/null; then
  sh /root/rut240_star_worker.sh loop >/tmp/star_worker.log 2>&1 &
fi
```

## If it says "not found" even though the file exists

That usually means the binary was compiled for the wrong ABI/dynamic loader. Try the other artifact, or build with the exact Teltonika/RutOS SDK for your firmware.
