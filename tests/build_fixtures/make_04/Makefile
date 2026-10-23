all: out.txt
out.txt:
	@echo first > $@
	-false
	echo second \
		third >> $@
	@echo 'quoted "text"' >> $@
