#!/bin/bash

echo "======================================================"
echo "    DISTRIBUTED COMPILER: C++ HEAVY LOAD STRESS TEST"
echo "======================================================"

# 1. CLEANUP
echo "[*] Cleaning up old test environments inside tests/..."
rm -rf tests/heavy_client1_src tests/heavy_client2_src tests/heavy_client1_src_compiled tests/heavy_client2_src_compiled tests/heavy_single_core_compiled

mkdir -p tests/heavy_client1_src
mkdir -p tests/heavy_client2_src
mkdir -p tests/heavy_single_core_compiled

# 2. GENERATE MASSIVE FILES FOR CLIENT 1
echo "[*] Generating 50 massive files for Client 1 (>120KB each)..."
for i in {1..50}; do
    FILE="tests/heavy_client1_src/heavy1_$i.cpp"
    
    echo "#include <iostream>" > $FILE
    echo "#include <cmath>" >> $FILE
    echo "" >> $FILE
    
    # Generate 1500 simple, repetitive functions to bloat the file size
    for j in {1..1500}; do
        echo "double compute_heavy_math_${i}_${j}(double val) { return std::sin(val) * std::cos(val) * $j; }" >> $FILE
    done
done

# 3. GENERATE MASSIVE FILES FOR CLIENT 2 (With Errors)
echo "[*] Generating 50 massive files for Client 2 (with 2 intentional errors)..."
for i in {1..50}; do
    FILE="tests/heavy_client2_src/heavy2_$i.cpp"
    
    echo "#include <iostream>" > $FILE
    echo "#include <cmath>" >> $FILE
    echo "" >> $FILE

    for j in {1..1500}; do
        # Inject intentional syntax errors
        if [ $j -eq 750 ] && { [ $i -eq 20 ] || [ $i -eq 40 ]; }; then
            echo "double bad_func_${i}_${j}() { double x = ; return 0; } // INTENTIONAL ERROR" >> $FILE
        else
            echo "double compute_heavy_math_c2_${i}_${j}(double val) { return std::sin(val) * std::cos(val) * $j; }" >> $FILE
        fi
    done
done

echo ""
echo "======================================================"
echo "  PHASE 1: SINGLE-CORE BASELINE TEST (Sequential)"
echo "======================================================"
echo "[*] Compiling 50 massive files locally using standard g++..."
echo "[*] This simulates a normal, non-distributed build process."

time {
    for file in tests/heavy_client1_src/*.cpp; do
        filename=$(basename "$file")
        g++ -c "$file" -o "tests/heavy_single_core_compiled/${filename%.cpp}.o"
    done
}

echo ""
echo "[!] Single-core baseline complete. Note the 'real' time above!"
echo "======================================================"
echo "[!] CLUSTER CHECKLIST:"
echo "1. Buffer (MAX_BUFF) in common.h is set to 4096."
echo "2. Master Server is running? (./master)"
echo "3. Workers are running? (e.g., ./worker node1 pass)"
echo "======================================================"
read -p "Press [Enter] to launch Phase 2: The Distributed Cluster Benchmark... "

echo ""
echo "======================================================"
echo "  PHASE 2: DISTRIBUTED CLUSTER TEST (100 Files)"
echo "======================================================"
echo "[*] FIRING CLIENT 1 (50 Files) -> Background Thread"
time ./client ./tests/heavy_client1_src client1 1234 &
CLIENT1_PID=$!

# sleep 0.2 (Commented out for maximum concurrent chaos)

echo "[*] FIRING CLIENT 2 (50 Files) -> Background Thread"
time ./client ./tests/heavy_client2_src client2 1234 &
CLIENT2_PID=$!

echo "[*] Total 100 massive files are being chunked and distributed!"
echo "[*] Waiting for both sessions to complete..."

wait $CLIENT1_PID
wait $CLIENT2_PID

echo ""
echo "======================================================"
echo "                 TEST COMPLETE!"
echo "======================================================"
echo "[*] Check 'tests/heavy_single_core_compiled/' -> 50 baseline files."
echo "[*] Check 'tests/heavy_client1_src_compiled/' -> Should have 50 .o files."
echo "[*] Check 'tests/heavy_client2_src_compiled/' -> Should have 48 .o files and 1 build_log.log."