echo "Firing 3 concurrent clients at the Master..."

../src/client test_dir/&
../src/client test_dir/&

wait
echo "All concurrent transfers complete!"
