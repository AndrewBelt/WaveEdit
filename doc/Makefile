
build: manual.html manual.pdf

view: manual.pdf
	mupdf $^

%.html: %.pug
	pug -P $^

MARGIN = 20mm
%.pdf: %.html
	wkhtmltopdf \
		--margin-left $(MARGIN) --margin-right $(MARGIN) --margin-top $(MARGIN) --margin-bottom $(MARGIN) \
		$^ $@

clean:
	rm -rfv *.pdf *.html
