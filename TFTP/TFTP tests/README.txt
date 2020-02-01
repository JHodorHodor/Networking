./clientTFTP --action read --filename numbers --your_filename c2 --block_size 1500 --window_size 23 --verbose

./clientTFTP --action read --filename numbers --your_filename c3 --block_size 600 --window_size 3 --verbose

./clientTFTP --action read --filename numbers --your_filename c1 --window_size 31 --verbose


------

./clientTFTP --action write --filename s1 --your_filename numbers --window_size 12 --block_size 1200 --verbose
