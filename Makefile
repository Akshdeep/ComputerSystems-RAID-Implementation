mirror-test: homework.c image.c mirror-test.c
	gcc -g3 $^ -o  $@

# Add other targets for raid0 and raid4 tests

clean:
	rm -f mirror-test 
