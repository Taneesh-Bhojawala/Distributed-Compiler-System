echo "Firing 3 concurrent clients at the Master..."

../src/client file1.c 101 &
../src/client file2.c 102 &
../src/client file3.c 103 &

wait
echo "All concurrent transfers complete!"
