all: here.txt
ifneq ($(shell uname -s),NotAnOs)
here.txt:
	echo here > $@
endif
ifeq ($(shell uname -s),NotAnOs)
other.txt:
	echo one > $@
else
other.txt:
	echo two > $@
endif
