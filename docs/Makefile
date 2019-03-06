# STAR LAB PROPRIETARY & CONFIDENTIAL
# Copyright (c) 2017, Star Lab -- All Rights Reserved
# Unauthorized copying of this file, via any medium is strictly prohibited.

TARGETS := \
    report.pdf \
    report.docx

OBJS = report/report.yaml report/report-pdf.md report/report-changes.md \
	report/overview.md report/summary_of_work.md report/architecture.md \
	report/design.md report/operation.md report/artifacts.md            \
	report/development_environment.md report/future_enhancements.md

.PHONY: all
.DEFAULT_GOAL := all
all:$(TARGETS)

#.PHONY: doxygen
#doxygen: doxy.conf
#	@doxygen $<

.PHONY: test
test:

# Graphviz diagrams
#%.pdf: %.gv
#	dot -Tpdf $< > $@


# Software Design Description (SDD)
report.pdf: $(OBJS)
	pandoc --latex-engine=xelatex -S -o $@ $^

report.docx: $(OBJS)
	pandoc -S -o $@ $^

.PHONY: clean
clean:
	@-rm -f $(TARGETS)
