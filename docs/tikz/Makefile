SRC = $(wildcard arch*.tex fsm.tex)
PDF = $(SRC:.tex=.pdf)
SVG = $(SRC:.tex=.svg)
PNG = $(SRC:.tex=.png)

all: pdf svg png ;

pdf: $(SRC) $(PDF) ;
svg: $(SRC) $(SVG) ;
png: $(SRC) $(PNG) ;

clean:
	latexmk -C
	rm -rf $(PDF)
	rm -rf $(SVG)
	rm -rf $(PNG)

%.pdf: %.tex common.tex
	latexmk -lualatex --interaction=nonstopmode -shell-escape $< $@
	latexmk -c

%.png: %.pdf
	pdftocairo -singlefile -r 300 -transp -png $<

%.svg: %.pdf
	pdftocairo -svg $<

.PHONY: clean
