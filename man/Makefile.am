.0.1:
	@[ -z "$$SOURCE_DATE_EPOCH" ] || d=--date=@$$SOURCE_DATE_EPOCH ; sed -e "s/!VERSION!/@JACK_RELEASE@/g" -e "s/!DATE!/`date $$d '+%B %Y'`/g" < $*.0 > $@
	@echo Built $*.1 from template

manpages = $(patsubst %.0,%.1,$(wildcard *.0))

clean-local:
	-rm -rf $(manpages)

man1_MANS = $(manpages)

BUILT_SOURCES = $(manpages)

#dist_man_MANS = $(manpages)
