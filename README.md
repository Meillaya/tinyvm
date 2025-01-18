# tinyvm LC-3

LVM-3 is a virtual machine (VM) that implements the LC-3 architecture and can be used to run LC-3 programs on modern computers. The VM currently only supports Linux, but it can be easily ported to other operating systems with minor API changes.


## Usage
Compile the program:

```c
make
```
Then run the test file:
```c
./tiny tests/2048.obj
```

## Resources 
- [Introduction to Computing Systems: From bits & gates to C & beyond](https://www.amazon.com/dp/0072467509)
- [Write your Own Virtual Machine](https://www.jmeiners.com/lc3-vm/)
- [LC3 Architecture](https://en.wikipedia.org/wiki/Little_Computer_3)