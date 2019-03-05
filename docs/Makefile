# STAR LAB PROPRIETARY & CONFIDENTIAL
# Copyright (c) 2017, Star Lab -- All Rights Reserved
# Unauthorized copying of this file, via any medium is strictly prohibited.

# Contract #: N00024-17-P-4502
# Expiration of SBIR/STTR Data Rights Period: 05/02/2022
# The Government's right to use, modify, reproduce, release, perform, display,
# or disclose technical data or computer software marked with this legend are
# restricted during the period shown as provided in paragraph (b)(4) of the
# Rights in Noncommercial Technical Data and Computer Software - Small Business
# Innovation Research (SBIR) Program clause contained in the above identified
# contract. No restrictions apply after the Expiration Date shown above. Any
# reproductions of technical data, computer software or portions therof marked
# with this legend must also reproduce the markings.

DOXY_DIR ?= doxygen/

TARGETS := \
    report.pdf \
	report.docx

.PHONY: all
.DEFAULT_GOAL := all
#all: doxygen $(TARGETS)
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
report.pdf: report/report.yaml report/report-pdf.md report/report-changes.md \
			report/overview.md report/summary_of_work.md report/architecture.md \
			report/design.md report/operation.md report/artifacts.md \
			report/development_environment.md report/future_enhancements.md
	pandoc --latex-engine=xelatex -S -o $@ $^

report.docx: report/report.yaml report/report-pdf.md report/report-changes.md report/report.md
	pandoc -S -o $@ $^

.PHONY: clean
clean:
	@-rm -rf $(DOXY_DIR)
	@-rm -f $(TARGETS)
