.DEFAULT_GOAL := all

SRC = $(wildcard arch*.tex fsm.tex)
PDF = $(SRC:.tex=.pdf)
SVG = $(SRC:.tex=.svg)
PNG = $(SRC:.tex=.png)

# If USE_DOCKER variable is defined, we're working with docker and use the tools
# from image/container instead of current OS.
ifdef USE_DOCKER
TIKZ_IMAGE = hapostgres/latexmk-pdftocairo:latest
endif

.PHONY: all
all: pdf png svg ;

.PHONY: pdf
pdf: $(SRC) $(PDF) ;
.PHONY: png
png: $(SRC) $(PNG) ;
.PHONY: svg
svg: $(SRC) $(SVG) ;

.PHONY: clean
clean:
	$(LATEXMK) -C
	rm -rf $(PDF)
	rm -rf $(PNG)
	rm -rf $(SVG)
	rm -f docker.id

.PHONY: maintainer-clean
maintainer-clean: clean
ifdef USE_DOCKER
	docker image rm $(TIKZ_IMAGE)
endif

ifdef USE_DOCKER
LATEXMK    = docker run --rm -u $(shell id -u):$(shell id -g) -v ./:/tikz $(TIKZ_IMAGE) latexmk
PDFTOCAIRO = docker run --rm -u $(shell id -u):$(shell id -g) -v ./:/tikz $(TIKZ_IMAGE) pdftocairo
docker.id: Dockerfile
	docker build --iidfile docker.id --file $< -t $(TIKZ_IMAGE) .
else
LATEXMK    = latexmk
PDFTOCAIRO = pdftocairo
docker.id: ;
endif

%.pdf: %.tex common.tex docker.id
	$(LATEXMK) -silent -lualatex --interaction=nonstopmode -shell-escape $<
	$(LATEXMK) -c

%.png: %.pdf docker.id
	$(PDFTOCAIRO) -singlefile -r 300 -transp -png $<

%.svg: %.pdf docker.id
	$(PDFTOCAIRO) -svg $<
