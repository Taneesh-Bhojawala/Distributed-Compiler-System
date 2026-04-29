echo "Firing 3 concurrent clients at the Master..."

../src/client/client test_dir/ taneesh 1234&
../src/client/client test_dir2/ taneesh 1234&

wait
echo "All concurrent transfers complete!"
