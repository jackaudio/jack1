.0.1:
	@sed -e "s/!VERSION!/@JACK_RELEASE@/g" -e "s/!DATE!/`date '+%B %Y'`/g" < $*.0 > $@
	@echo Built $*.1 from template

manpages := $(patsubst %.0,%.1,$(wildcard *.0))

all: $(manpages)

clean: 
	-rm $(manpages)	