# THIS IS THE BACKUP, WE NEED TO COMBINE THIS ONE WITH THE CONTENT ON MY LAPTOP 2/28/26
# Install Qemu
sudo apt install qemu-system-x86 qemu-utils -y
sudo apt install bats gdb clang


- Note: When debugging and testing, you build with shared and debug info off 
# Compile BusyBox with asan/ubsan support using clang (Development Purposes)
```bash 
make clean
export CC="gcc"
export CXX="g++"
export CFLAGS="-fsanitize=undefined,address -fno-omit-frame-pointer -g -O0"
export LDFLAGS="-fsanitize=undefined,address"
make -j$(nproc)
ASAN_OPTIONS=verbosity=1 ./busybox_unstripped --help
```
# Build for production 
- Need to statically link it and remove the debug info off

# Build musl for production
- Download the latest version: https://musl.libc.org/
- Musl is a lightweight alternative version of glibc and is used to build application's with a small memory footprint, and should be used to build applications that are going to be ran in user-space.
# Sources: 
    - https://training.linuxfoundation.org/training/a-beginners-guide-to-linux-kernel-development-lfd103/
    - https://medium.com/@mail_89924/introduction-to-libc-free-programming-ad222e23df1d
    - https://wiki.musl-libc.org/functional-differences-from-glibc.html
```
bash
wget -qO- https://apt.llvm.org/llvm-snapshot.gpg.key | sudo tee /etc/apt/trusted.gpg.d/apt.llvm.org.asc

# Add the repository (for Ubuntu 24.04 - adjust if needed)
sudo add-apt-repository "deb http://apt.llvm.org/noble/ llvm-toolchain-noble-19 main"

sudo apt update
sudo apt install -y \
    clang-19 \
    clang-tools-19 \
    lld-19 \
    lldb-19 \
    libc++-19-dev \
    libc++abi-19-dev \
    libllvm19 \
    llvm-19 \
    llvm-19-dev \
    llvm-19-tools
make clean

export CC="clang-19"
export CXX="clang++-19"
export CFLAGS="-I$HOME/musl/include -fno-omit-frame-pointer -g -O2"
export LDFLAGS="-L$HOME/musl/lib \
  -Wl,-rpath,$HOME/musl/lib \
  -Wl,-dynamic-linker,$HOME/musl/lib/ld-musl-x86_64.so.1 \
  -fuse-ld=lld"

make -j$(nproc)
```

# Integrating bash scripts with C 
- There is a way where you can create a cmake file or a make file and integrate it into your build
- This whole repo consists of scripts, so it would be very useful when creating the executable to include all of them

# Windows 
- Download the .exe from the website
- Set the environment variable and run this in powershell:
```bash 
qemu-system-x86_64.exe `
	-kernel ".\Source\6.12.68-1-cachyos-lts\usr\lib\modules\6.12.68-1-cachyos-lts\vmlinuz" `
	-initrd ".\Initramfs\New\initramfs-linux-cachyos-lts-custom.img" `
	-append "console=tty0" `
	-m 2G
```

# Linux
```bash 
qemu-system-x86_64 \
    -kernel "./Source/6.12.68-1-cachyos-lts/usr/lib/modules/6.12.68-1-cachyos-lts/vmlinuz" \
    -initrd "./Initramfs/New/initramfs-linux-cachyos-lts-custom.img" \
    -append "console=tty0" \
    -m 2G
```
# Debugging
- We are doing print debugging, so whatever changes you add to a utility, you will need to recompile

- If you are wanting to package:
```bash
    cd Initramfs
    ./Package.sh --type=Old --process=Package --production=Dev
```

- If you need to unpackge:
```bash 
    cd Initramfs
    ./Package.sh --type=Old --process=Unpackage --production=Dev
```

- Must always cd back to root
```bash
qemu-system-x86_64 \
    -kernel "./Source/6.12.68-1-cachyos-lts/usr/lib/modules/6.12.68-1-cachyos-lts/vmlinuz" \
    -initrd "./Initramfs/New/initramfs-linux-cachyos-lts-custom.img" \
    -append "console=ttyS0" \
    -m 2G \
    -serial file:serial.log
```
rm -r Source/lib/modules/6.12.69-2-cachyos-lts Source/lib/modules/6.12.66-2-cachyos-lts
# Gdb Debugging


- Source: https://nickdesaulniers.github.io/blog/2018/10/24/booting-a-custom-linux-kernel-in-qemu-and-debugging-it-with-gdb/#:~:text=Just%20need%20to%20add%20%2Dcpu,until%20we%20continue%20in%20gdb.

# Folder Structure 
- Source: 
    1. Contains the kernel version folder with the bzImage
    2. It update and manages the kernel versions, this is enabled by default.

- InitRamfs: 
    1. There is a folder called New and Old. Old is the one that contains the testing initramfs
    2. If the old folder is empty, explain a initramfs needs to be there and exit script
    3. If New folder is empty, we run Extract.sh, which will extract the one from the Old folder
    4. Add the contents to a different directory called structure. It will Output the generated directory and this is where you can update the init or add files.
    5. Package.sh it looks for the folder that was generated i.e structure, packages it into a .img and moves it to New folder. You can now run the new updated initramfs in qemu or on your system.

- Busybox: 
    - Source folder that has zstd support avialable. A script exists to test the built busybox and      to validate the build

- Tests:
    - Contains a series of test files that have test units that test the bash code


# RoadMap
Confused about: 
	- What are pipes in c
	void xpipe(int *filedes) FAST_FUNC;
/* In this form code with pipes is much more readable */
struct fd_pair { int rd; int wr; };
#define piped_pair(pair)  pipe(&((pair).rd))
#define xpiped_pair(pair) xpipe(&((pair).rd))



`load_modules_dep` loads in the modules while `do_modprobe` actually probes them in
`decompressed_path` is still not needed when llist_t* struct contains a `char*` called data that can be used instead

Notes:

mmap: It maps the virtual memory the the physical memory and stores it in memory, it will return a pointer to that memory region that was mapped. You have special flags that make the memory region either sharable or not 

When we align, we need to align the underlyinging type with the pointer type


modprobe.c use cases:

Case One: 
	- Loading in the modules dependencies
		- Requires: load_modules_dep
			- Phase: Loading in compressed modules
				- Status: [ Failed ] 2/17/26 
			- Phase: Loading uncompressed modules
				- Status [ N/A ] 2/17/26

Case Two: 
	- Command line  
		- Requires: do_modprobe 
		- Case: User should be able to modprobe one module or multiple modules at the same time
			- Phase: Loading compressed modules 
				- Status: [ N/A ] 2/18/26
			- Phase: Loading uncompressed modules
				- Status: [ N/A ] 2/18/26

insmod use cases:
	Case One: 
		- Loading in compressed modules
			- Requires: find_module 
				- Phase: Loading in compressed modules
					- Status: [ N/A ] 2/18/26
				- Phase: Loading in uncompressed modules
					- Status: [ N/A ] 2/18/26 



# Grab the kernel source code
https://mirror.cachyos.org/repo/x86_64/cachyos/




# Sources
https://bats-core.readthedocs.io/en/stable/tutorial.html#your-first-test