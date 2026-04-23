echo "Firing 3 concurrent clients at the Master..."

../src/client file1.cpp 101 &
../src/client file2.cpp 102 &
../src/client file3.cpp 103 &

wait
echo "All concurrent transfers complete!"
