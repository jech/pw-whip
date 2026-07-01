# pw-whip: a bridge between PipeWire and WHIP

*PipeWire* is a multimedia (audio and video) framework for Linux.  *WHIP*
([RFC 9725](https://datatracker.ietf.org/doc/rfc9725/)) is a standardised
protocol for streaming audio and video to network servers.  WHIP is
notably implemented by the [Galene](https://galene.org/) videoconferencing
server.

The program `pw-whip` acts as a bridge between *PipeWire* and *WHIP*: it
allows any Linux program that is able to speak to *PipeWire* to stream its
audio over *WHIP*.

## Building

### Install `libopus` and `libpipewire`.

For example, on Debian, do
```sh
apt install libopus-dev libpipewire-0.3-dev
```

### Build and install libdtachannel

```sh
cd ~/src
git clone https://github.com/paullouisageneau/libdatachannel
cd libdatachannel
cmake -Bbuild
cd build
make -j
sudo mv libdatachannel.so* /usr/local/lib/
sudo ldconfig
```

### Build pw-whip

```sh
cd ~/src/pw-whip/
make -j
```

If your copy of the `libdatachannel` sources doesn't live at
`~/src/libdatachannel`, specify its location as `LIBDATACHANNEL_ROOT`:
```sh
make -j LIBDATACHANNEL_ROOT=/usr/local/src/libdatachannel
```

You may optionally install `pw-whip`:
```sh
sudo make install
```

## Usage

```sh
./pw-whip https://galene.org:8443/group/public/pw-whip/.whip
```
By default, `pw-whip` appears as an audio sink to *PipeWire*, but isn't
connected to a source.  You will need to connect it manually to a source
using something like `qpwgraph`.

You may request automatic connection using the `-c` flag, or connection to
a specific target using the `-C` flag:
```sh
./pw-whip -C mpv https://galene.org:8443/group/public/pw-whip/.whip
```

For more information, please consult the manual page:
```sh
man ./pw-whip.man
```

## Limitations

* No support for ICE restarts, even if supported by the server.
* No support for trickling local ICE candidates, even if supported by the
  server.
* No support for video.
* We do not currently adapt the throughput and the amount of FEC to
  varying network conditions.

— Juliusz Chroboczek <https://www.irif.fr/~jch/>
