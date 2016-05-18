## To build the tool, use:
```
cd /vagrant/pin
```
For 64-bit architectures:
make PIN_ROOT=~/pin-current obj-intel64/calltrace.so
```
For 32-bit architectures:
```
make PIN_ROOT=~/pin-current obj-ia32/calltrace.so
```
Alternatively, you can have it build automatically with:
```
make PIN_ROOT=~/pin-current
```
