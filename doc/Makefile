WEBSITE = ardour.sourceforge.net

HTML-FILES := index.html \
	      issues.html \
              manual.html \
	      features.html \
              mailinglist.html \
	      contributors.html \
	      intro.html \
	      download.html \
	      index.html \
	      news.html \
	      links.html \
	      requirements.html \
	      configuring.html \
	      helping.html \
	      compiling.html \
	      todo.html

user = $(shell whoami)
fullname = $(shell awk -F: '/$(user)/ { print $$5}' /etc/passwd)

%.html: %.m4 header.html trailer.html
	m4 -P -E -I. $< > $@
	sed -e "s/@LASTMOD@/`date`/" -e "s/@USER@/$(fullname)/" \
	          < $@ > $@.XXX && mv $@.XXX $@

.PHONY: manual
.PHONY: upload

html: $(HTML-FILES)

upload: build-updir
	cd updir ; \
	if [ "`ls`" ] ; then \
	    (echo "cd /home/groups/j/ja/jackit/htdocs && tar -zxvf - ; exit"; tar cf - *.html | gzip) | \
		 ssh \$(user)@shell.sourceforge.net; \
	    cd .. ; \
	    touch last-upload ; \
        fi

upload-images: build-updir
	(echo "cd /home/groups/j/ja/jackit/htdocs && tar -zxvf - ; exit"; tar cf - *.png | gzip) | \
		ssh \$(user)@shell.sourceforge.net; \

build-updir:
	if [ ! -d updir ] ; then mkdir updir ; else rm -rf updir/* ; fi ; \
        if [ ! -f last-upload ] ; then touch --date="Jan 1st 1970" last-upload; fi  ; \
	uptime=`ls -l --full-time last-upload | awk '{printf ("%s %s %s %s %s\n", $$6, $$7, $$8, $$9, $$10)}'`; \
	tar -chf - --newer="$$uptime" \
	      --exclude=updir \
	      --exclude="*.m4" \
	      --exclude=Makefile \
	      --exclude=CVS \
	      --exclude=manual \
	      --exclude=header.html \
	      --exclude=trailer.html \
	      --exclude=last-upload . | (cd updir; tar xf - ) ; \

clean:
	rm -f $(HTML-FILES)

