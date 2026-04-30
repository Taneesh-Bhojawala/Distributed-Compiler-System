# Distributed Compiler System 🚀

A highly concurrent, multi-threaded distributed compilation cluster built purely in C using POSIX Sockets. This system offloads CPU-intensive compilation tasks (C and C++) from a client machine to a cluster of worker nodes, perfectly managed and load-balanced by a central Master server.

## 🌟 Architecture Overview

The system operates on a Star Topology with a central **Master Node** acting as the load balancer and session manager.
1. **Clients** upload directories of source files to the Master.
2. The **Master** multiplexes these files across a pool of available **Workers** in real-time using POSIX threads.
3. **Workers** compile the code using `gcc` or `g++` and stream the binary `.o` files (or multi-line compilation errors) back to the Master.
4. The **Master** safely routes the results back to the correct Client, ensuring absolute session isolation.

## ✨ Key Features

* **Thread-Safe Concurrency:** Utilizes `pthread` mutexes and condition variables to safely handle multiple concurrent client sessions and up to 100 worker nodes without data interleaving or race conditions.
* **TCP Stream Chunking:** Built to handle massive files. Source files, binary `.o` objects, and massive compilation errors are safely split into chunks (e.g., 4096 bytes) and reconstructed over the network, completely avoiding buffer overflows.
* **Role-Based Access Control:** Built-in Admin panel to register `client`, `worker`, and `admin` credentials, securely stored in a persistent binary datastore (`users.bin`).
* **Session Isolation:** The Master node perfectly separates multi-client traffic, ensuring Client A never receives Client B's object files or error logs, even under extreme load.
* **Dynamic C/C++ Support:** Workers dynamically extract file extensions and invoke either `gcc` or `g++` accordingly.

## 📁 Directory Structure
```text
DISTRIBUTED-COMPILER-SYSTEM/
├── build/         # Temporary storage for master routing
├── include/       # Shared headers (common.h, network structs)
├── logs/          # Global and session-specific master logs
├── src/
│   ├── admin/     # Admin panel source
│   ├── client/    # Client connection and chunk uploader
│   ├── master/    # Central load balancer and session controller
│   └── worker/    # Execution nodes and compiler logic
├── temp/          # Worker-specific temporary build space
├── tests/         # Automated load testing and benchmark scripts
├── Makefile       # Global build configuration
└── README.md
```

## ⚙️ Configuration & Installation

### 1. Prerequisites
Ensure you have a POSIX-compliant environment (Linux/macOS or WSL on Windows) with `gcc`, `g++`, and `make` installed.

### 2. Network Configuration
To run the cluster across multiple physical laptops, update the server IP in `include/common.h`:
```c
// include/common.h
#define PORT 8080
#define SERVER_IP "127.0.0.1" // Change this local address to the Master node's IP address (can be found on the machine master will be running using ifconfig)
#define MAX_BUFF 4096           // Network packet chunk size
```

### 3. Build the Project
Compile the entire suite using the provided Makefile in the root directory:
```bash
make clean
make
```

This will generate four executables in your root directory: `master`, `worker`, `client`, and `admin`.

## 🚀 Usage Guide

For a full cluster setup, open multiple terminal windows (or across multiple machines).

### Step 1: Start the Master Server
The Master node must be running before any other components connect.
```bash
./master
```

### Step 2: Setup Users (Admin)
Launch the admin panel to create accounts. (If `users.bin` is missing, the master automatically creates a default `admin`:`admin` account).
```bash
./admin admin admin
```

Create at least one client (`client1`) and one worker (`node1`) through the interactive menu.

### Step 3: Spin up Worker Nodes
Start one or more worker nodes to process the compilation jobs.
```bash
./worker node1 <password>
```

*Note: You can run multiple workers on the same machine or across different laptops to create a true cluster.*

### Step 4: Submit a Compilation Job (Client)
Point the client to a directory containing your `.c` or `.cpp` files.
```bash
./client <path_of_directory_you_want_to_compile> client1 <password>
```

The Client will upload the files, wait for the cluster to finish, and download the compiled `.o` files and the `build_log.log` into a new `./src/some_code_compiled` directory.
For an interactive menu, just run the client executable with no arguments:
```bash
./client
```

## 🧪 Benchmarking & Stress Testing

This project includes automated bash scripts in the root directory to test concurrency and network load limits. All test data is safely stored inside the `tests/` directory.

* **The Heavy Stress Test (`./heavy_test_cpp.sh`):** A brute-force benchmark that generates 50 massive, computationally heavy C++ files and blasts them concurrently from two different clients to prove session isolation and TCP chunking integrity under extreme load.
```bash
chmod +x heavy_test_cpp.sh
./heavy_test.sh
```
### Why is the Heavy Test "Heavy"?

The `heavy_test_cpp.sh` script does not just generate large files using useless whitespace; it generates code specifically designed to exhaust the C++ compiler's frontend and stress the distributed network layer.

1. **Defeating Optimization:** By generating 100s of uniquely named functions per file, the compiler cannot optimize the code into a single loop. It is forced to allocate memory for 100s of unique Abstract Syntax Trees (ASTs) and write 100 distinct Unresolved Symbol entries into the `.o` file's symbol table.
2. **Parsing Load:** Including `<cmath>` forces the preprocessor to copy thousands of lines of standard library definitions into memory. Furthermore, calling heavily overloaded functions like `std::sin()` requires the compiler to perform type-checking and Overload Resolution 10,000 times across the suite.
3. **Forcing TCP Chunking:** By generating thousands of lines of valid syntax, the physical `.cpp` files and resulting binary `.o` files balloon to over 8KB. This explicitly forces the Master and Worker nodes to slice the files into multiple 4096-byte TCP chunks, rigorously testing the system's network reassembly and race-condition locks.

Similarly for `heavy_test_c.sh`.

## 📜 License
[MIT License](LICENSE)